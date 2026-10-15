// THE PLAN TAB: what the fleet can be asked to do, as a list that works like Spells.
//
// Every row comes from the harness plan desk (m59-harness/tools/m59-plan-desk.mjs), which
// builds them from what is actually running: FleetScripts by reflection over their own
// params, the courier's options out of its own shopping tables, and who holds the
// controls from the proxy gate. This file draws them, lets you pick arguments from the
// lists the desk supplies, and queues what you run. It never speaks to the broker or the
// game server; it reads one file and writes one file, like the DUM pane beside it.
//
//   <M59_CLIENT_CONTEXT_DIR>\plan\in\<pid>.tsv    written here every second
//   <M59_CLIENT_CONTEXT_DIR>\plan\out\<pid>.tsv   written by the desk
//
// Double-click (or Enter) runs a row; a row with arguments opens them first. Right-click
// shows what a row does. The same rows run from the chat box: `plan <row> key=value`,
// and `fs <fleetscript line>` for the FleetScript REPL.
//
// THE LOCK. When the desk reports that Keeper holds this character's controls, the proxy
// in front of this client is already dropping its movement, combat and item packets — the
// lock is enforced there, where a stock client cannot get round it. This file refuses the
// same actions locally only so the screen does not pretend to move.
#ifndef M59_CONTEXT_UI_TEST
#include "client.h"
#include "merintr.h"
#endif
#include "inventoryintent.h"
#include "clientcontextmodel.h"
#include "planpane.h"
#include <algorithm>
#include <map>

namespace {
using M59Context::Time;
struct Option { std::string value, label; };
struct Arg { std::string id, type, title, def; std::vector<Option> options; };
struct PlanEntry { std::string id, group, icon, title, describe; bool running=false, disabled=false; std::vector<Arg> args; };
struct Message { Time seq=0; bool ok=false; std::string text; };
struct PlanView { Time at=0, ack=0; std::string control="none", status; std::vector<PlanEntry> entries; std::vector<Message> messages; };
struct Pending { Time seq; std::string entry, args; Time queued; };
struct Row { bool header; int entry; std::string group; };
struct Invent { unsigned int id, amount; std::string name; };

const int MAX_ARGS=6;
HWND panel=NULL, list=NULL, detail=NULL, heading=NULL, run=NULL, cancel=NULL;
HWND labels[MAX_ARGS]={}, inputs[MAX_ARGS]={};
std::string root;
PlanView view;
std::vector<Row> rows;
std::vector<Pending> pending;
std::vector<Invent> inventory;
std::map<std::string,ID> icons;
Time lastSeq=0, lastShownMessage=0, lastVetoNotice=0;
std::string lastControl="none";
int editing=-1;   // entry index whose arguments are open, or -1
bool shown=false;

Time Now(){return M59InventoryIntent::Now();}
std::string Enc(const std::string& s){return M59InventoryIntent::Encode(s.c_str());}
std::string Fold(const std::string& s){std::string o;for(unsigned char c:s)if(isalnum(c))o+=(char)tolower(c);
  if(o.size()>3&&o.compare(o.size()-3,3,"ies")==0)o=o.substr(0,o.size()-3)+"y";
  else if(o.size()>2&&o.back()=='s')o.pop_back();return o;}
// The desk keeps newlines (percent-encoded); an EDIT control wants CRLF.
bool Dec(const std::string& s,std::string& out) {
  out.clear();if(s.size()>16384)return false;
  for(size_t i=0;i<s.size();i++){unsigned char c=s[i];
    if(c=='%'){if(i+2>=s.size()||M59Context::Hex(s[i+1])<0||M59Context::Hex(s[i+2])<0)return false;
      c=(unsigned char)(M59Context::Hex(s[i+1])*16+M59Context::Hex(s[i+2]));i+=2;}
    if(c=='\n'){out+="\r\n";continue;}
    if(c==0||c<32||c==127)return false;out+=(char)c;}
  return true;
}
std::wstring Wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,NULL,0);if(n<1)return L"";
  std::wstring w(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,&w[0],n);w.pop_back();return w;}
void Text(HWND h,const std::string& s){SetWindowTextW(h,Wide(s).c_str());}
std::string Read(const std::string& path){
  HANDLE f=CreateFileA(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
  if(f==INVALID_HANDLE_VALUE)return "";LARGE_INTEGER size;DWORD got=0;std::string s;
  if(GetFileSizeEx(f,&size)&&size.QuadPart>0&&size.QuadPart<=1048576){s.resize((size_t)size.QuadPart);
    if(!ReadFile(f,&s[0],(DWORD)s.size(),&got,NULL)||got!=s.size())s.clear();}
  CloseHandle(f);return s;
}
void Write(const std::string& path,const std::string& s){
  std::string tmp=path+".tmp";HANDLE f=CreateFileA(tmp.c_str(),GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
  if(f==INVALID_HANDLE_VALUE)return;DWORD wrote=0;bool ok=WriteFile(f,s.data(),(DWORD)s.size(),&wrote,NULL)&&wrote==s.size();CloseHandle(f);
  if(ok)MoveFileExA(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING);
}
std::string Pid(){return std::to_string(GetCurrentProcessId());}
bool Fresh(){return view.at&&Now()<=view.at+6000;}
void Say(const std::string& s){std::string m="PLAN: "+s;std::vector<char> b(m.begin(),m.end());b.push_back(0);GameMessage(b.data());}

// ------------------------------------------------------------------ the desk's file
bool Parse(const std::string& text,PlanView& out){
  if(text.size()<5||text.compare(text.size()-5,5,"\nEND\n")!=0)return false;
  std::istringstream in(text);std::string line;std::getline(in,line);auto h=M59Context::Split(line);
  PlanView v;
  if(h.size()!=7||h[0]!="M59PLAN"||h[1]!="1"||!M59Context::Number(h[2],v.at)||h[3]!=Pid()||!M59Context::Number(h[4],v.ack))return false;
  if(h[5]!="none"&&h[5]!="keeper"&&h[5]!="other")return false;
  v.control=h[5];if(!Dec(h[6],v.status))return false;
  while(std::getline(in,line)){
    if(line=="END"){out=std::move(v);return true;}
    auto f=M59Context::Split(line);
    if(f[0]=="E"&&f.size()==7&&v.entries.size()<256){PlanEntry e;e.id=f[1];
      if(!Dec(f[2],e.group)||!Dec(f[3],e.icon)||!Dec(f[4],e.title)||!Dec(f[6],e.describe))return false;
      e.running=f[5].find('r')!=std::string::npos;e.disabled=f[5].find('d')!=std::string::npos;v.entries.push_back(e);}
    else if(f[0]=="A"&&f.size()==6&&!v.entries.empty()&&v.entries.back().id==f[1]&&v.entries.back().args.size()<MAX_ARGS){Arg a;a.id=f[2];a.type=f[3];
      if(!Dec(f[4],a.title)||!Dec(f[5],a.def))return false;v.entries.back().args.push_back(a);}
    else if(f[0]=="O"&&f.size()==5&&!v.entries.empty()&&v.entries.back().id==f[1]&&!v.entries.back().args.empty()&&v.entries.back().args.back().id==f[2]){
      Option o;if(!Dec(f[3],o.value)||!Dec(f[4],o.label))return false;if(v.entries.back().args.back().options.size()<600)v.entries.back().args.back().options.push_back(o);}
    else if(f[0]=="M"&&f.size()==4){Message m;if(!M59Context::Number(f[1],m.seq)||!Dec(f[3],m.text))return false;m.ok=f[2]=="1";v.messages.push_back(m);}
    else return false;
  }
  return false;
}

// ------------------------------------------------------------------ drawing
ID IconFor(const PlanEntry& e){
  auto i=icons.find(Fold(e.icon));if(i!=icons.end())return i->second;
  i=icons.find(Fold(e.title));return i!=icons.end()?i->second:0;
}
COLORREF GroupColour(const std::string& g){
  if(g.rfind("Control",0)==0)return RGB(200,60,60);
  if(g.rfind("Courier",0)==0)return RGB(205,165,40);
  if(g.find("running")!=std::string::npos)return RGB(60,170,80);
  return RGB(70,110,190);
}
void Glyph(HDC dc,RECT r,const PlanEntry& e){
  HBRUSH b=CreateSolidBrush(e.disabled?RGB(140,140,140):GroupColour(e.group));FillRect(dc,&r,b);DeleteObject(b);
  char c[2]={(char)toupper((unsigned char)(e.title.empty()?'?':e.title[0])),0};
  SetTextColor(dc,RGB(255,255,255));DrawTextA(dc,c,1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}
void DrawRow(const DRAWITEMSTRUCT* d){
  if(d->itemID==(UINT)-1||d->itemID>=rows.size())return;
  const Row& row=rows[d->itemID];RECT r=d->rcItem;HDC dc=d->hDC;
  bool sel=(d->itemState&ODS_SELECTED)&&!row.header;
  FillRect(dc,&r,GetSysColorBrush(row.header?COLOR_3DFACE:(sel?COLOR_HIGHLIGHT:COLOR_WINDOW)));
  SetBkMode(dc,TRANSPARENT);HGDIOBJ old=SelectObject(dc,GetFont(FONT_STATS));
  if(row.header){
    RECT t=r;t.left+=4;SetTextColor(dc,GroupColour(row.group));
    std::wstring w=Wide(row.group);DrawTextW(dc,w.c_str(),-1,&t,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    SelectObject(dc,old);return;
  }
  const PlanEntry& e=view.entries[row.entry];int size=r.bottom-r.top-2;
  RECT ic={r.left+2,r.top+1,r.left+2+size,r.top+1+size};
  if(ID icon=IconFor(e)){AREA a={ic.left,ic.top,size,size};DrawObjectIcon(dc,icon,0,true,&a,NULL,0,0,true);}
  else Glyph(dc,ic,e);
  RECT t=r;t.left=ic.right+4;t.right-=14;
  SetTextColor(dc,e.disabled?GetSysColor(COLOR_GRAYTEXT):GetSysColor(sel?COLOR_HIGHLIGHTTEXT:COLOR_WINDOWTEXT));
  std::wstring w=Wide(e.title+(e.args.empty()?"":"…"));DrawTextW(dc,w.c_str(),-1,&t,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
  if(e.running){RECT dot={r.right-11,r.top+(r.bottom-r.top)/2-3,r.right-5,r.top+(r.bottom-r.top)/2+3};
    HBRUSH b=CreateSolidBrush(RGB(60,190,90));HGDIOBJ ob=SelectObject(dc,b);HGDIOBJ op=SelectObject(dc,GetStockObject(NULL_PEN));
    Ellipse(dc,dot.left,dot.top,dot.right,dot.bottom);SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(b);}
  SelectObject(dc,old);
}

int SelectedEntry(){int i=(int)SendMessage(list,LB_GETCURSEL,0,0);return i>=0&&(size_t)i<rows.size()&&!rows[i].header?rows[i].entry:-1;}

void Describe(){
  if(editing>=0)return;
  std::string s;int i=SelectedEntry();
  if(!Fresh())s="The plan desk is not answering. Start it with:\r\n  node tools/m59-plan-desk.mjs --fleet <name>\r\nfrom maps/m59-harness. Your controls are your own.\r\n\r\n";
  else if(view.control=="keeper")s="KEEPER HAS THE CONTROLS. You are spectating; movement, combat and items are refused until you take control back.\r\n\r\n";
  if(i>=0){const PlanEntry& e=view.entries[i];s+=e.title+(e.running?"   [running]":"")+(e.disabled?"   [unavailable]":"")+"\r\n"+e.describe+"\r\n";}
  else s+=view.status+"\r\nDouble-click a row to run it. Right-click to read it. Chat: plan <row> key=value, or fs <line>.";
  if(!view.messages.empty()){const auto& m=view.messages.back();s+="\r\n\r\nLast: "+std::string(m.ok?"":"REFUSED: ")+m.text;}
  for(const auto& p:pending)if(p.seq>view.ack){s+="\r\n\r\nWaiting for the desk to take: "+p.entry;break;}
  Text(detail,s);
}

void Rebuild(){
  std::string keep;int i=SelectedEntry();if(i>=0)keep=view.entries[i].id;
  rows.clear();std::string group="\x01";
  for(size_t n=0;n<view.entries.size();n++){
    if(view.entries[n].group!=group){group=view.entries[n].group;rows.push_back({true,-1,group});}
    rows.push_back({false,(int)n,group});
  }
  SendMessage(list,WM_SETREDRAW,FALSE,0);SendMessage(list,LB_RESETCONTENT,0,0);
  int select=-1;
  for(size_t n=0;n<rows.size();n++){SendMessageA(list,LB_ADDSTRING,0,(LPARAM)"");
    if(!rows[n].header&&view.entries[rows[n].entry].id==keep)select=(int)n;}
  if(select>=0)SendMessage(list,LB_SETCURSEL,select,0);
  SendMessage(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,NULL,TRUE);Describe();
}

// ------------------------------------------------------------------ running a row
std::string ArgEnc(const std::string& s){return M59InventoryIntent::Encode(s.c_str());}
void Queue(const std::string& entry,const std::string& args){
  if(!Fresh()){Say("the plan desk is not answering; nothing was queued");return;}
  Time seq=std::max(Now(),lastSeq+1);lastSeq=seq;
  if(pending.size()>=16)pending.erase(pending.begin());
  pending.push_back({seq,entry,args,Now()});Describe();
}
int FindEntry(const std::string& key){
  std::string k=Fold(key);int hit=-1;
  for(size_t n=0;n<view.entries.size();n++)if(view.entries[n].id==key)return (int)n;
  for(size_t n=0;n<view.entries.size();n++){const auto& e=view.entries[n];
    if(Fold(e.id).rfind(k,0)==0||Fold(e.title).rfind(k,0)==0){if(hit>=0)return -2;hit=(int)n;}}
  return hit;
}
void CloseArgs(){editing=-1;for(int n=0;n<MAX_ARGS;n++){ShowWindow(labels[n],SW_HIDE);ShowWindow(inputs[n],SW_HIDE);}
  ShowWindow(heading,SW_HIDE);ShowWindow(run,SW_HIDE);ShowWindow(cancel,SW_HIDE);ShowWindow(list,SW_SHOW);
  RECT r;GetClientRect(panel,&r);PlanPaneResize(-1,r.right,r.bottom);Describe();}
void OpenArgs(int i){
  const PlanEntry& e=view.entries[i];editing=i;Text(heading,e.title);
  for(int n=0;n<MAX_ARGS;n++){
    if(inputs[n])DestroyWindow(inputs[n]);inputs[n]=NULL;ShowWindow(labels[n],SW_HIDE);
    if(n>=(int)e.args.size())continue;const Arg& a=e.args[n];Text(labels[n],a.title);ShowWindow(labels[n],SW_SHOW);
    if(a.type=="choice"){
      inputs[n]=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_TABSTOP|CBS_DROPDOWNLIST,0,0,1,1,panel,(HMENU)(INT_PTR)(200+n),hInst,NULL);
      int def=0;for(size_t o=0;o<a.options.size();o++){std::wstring w=Wide(a.options[o].label);SendMessageW(inputs[n],CB_ADDSTRING,0,(LPARAM)w.c_str());if(a.options[o].value==a.def)def=(int)o;}
      SendMessage(inputs[n],CB_SETCURSEL,a.options.empty()?-1:def,0);
    } else {
      inputs[n]=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL|(a.type=="count"?ES_NUMBER:0),0,0,1,1,panel,(HMENU)(INT_PTR)(200+n),hInst,NULL);
      SendMessage(inputs[n],EM_SETLIMITTEXT,4000,0);Text(inputs[n],a.def);
    }
    SendMessage(inputs[n],WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
  }
  ShowWindow(list,SW_HIDE);ShowWindow(heading,SW_SHOW);ShowWindow(run,SW_SHOW);ShowWindow(cancel,SW_SHOW);
  RECT r;GetClientRect(panel,&r);PlanPaneResize(-1,r.right,r.bottom);
  Text(detail,e.describe);if(inputs[0])SetFocus(inputs[0]);
}
void SubmitArgs(){
  if(editing<0||(size_t)editing>=view.entries.size()){CloseArgs();return;}
  const PlanEntry& e=view.entries[editing];std::string args;
  for(size_t n=0;n<e.args.size()&&n<MAX_ARGS;n++){const Arg& a=e.args[n];std::string v;
    if(a.type=="choice"){int s=(int)SendMessage(inputs[n],CB_GETCURSEL,0,0);if(s>=0&&(size_t)s<a.options.size())v=a.options[s].value;}
    else {wchar_t buf[4001]={};GetWindowTextW(inputs[n],buf,4000);int k=WideCharToMultiByte(CP_UTF8,0,buf,-1,NULL,0,NULL,NULL);
      if(k>1){v.resize(k-1);WideCharToMultiByte(CP_UTF8,0,buf,-1,&v[0],k,NULL,NULL);}}
    if(!args.empty())args+="&";args+=ArgEnc(a.id)+"="+ArgEnc(v);
  }
  std::string id=e.id;CloseArgs();Queue(id,args);
}
void Activate(int i){
  if(i<0)return;const PlanEntry& e=view.entries[i];
  if(e.disabled){Describe();Say(e.title+": "+e.describe);return;}
  if(e.args.empty())Queue(e.id,"");else OpenArgs(i);
}

// ------------------------------------------------------------------ the loop
void Tick(){
  if(root.empty()||!cinfo||!cinfo->player||!cinfo->player->id)return;
  auto b=M59Context::Split(ClientContextBinding());if(b.size()<5)return;
  PlanView next;bool had=Fresh();
  if(Parse(Read(root+"\\plan\\out\\"+Pid()+".tsv"),next)){
    bool changed=next.entries.size()!=view.entries.size();
    for(size_t n=0;!changed&&n<next.entries.size();n++){const auto&a=next.entries[n],&c=view.entries[n];
      changed=a.id!=c.id||a.title!=c.title||a.running!=c.running||a.disabled!=c.disabled||a.describe!=c.describe||a.args.size()!=c.args.size();}
    view=std::move(next);
    for(const auto& m:view.messages)if(m.seq>lastShownMessage){lastShownMessage=m.seq;Say((m.ok?"":"refused: ")+m.text);}
    pending.erase(std::remove_if(pending.begin(),pending.end(),[](const Pending& p){return p.seq<=view.ack;}),pending.end());
    if(changed&&editing<0)Rebuild();else Describe();
  } else if(had&&!Fresh())Describe();
  for(auto it=pending.begin();it!=pending.end();)if(Now()>it->queued+30000){Say("the desk never took "+it->entry);it=pending.erase(it);}else ++it;
  std::string control=Fresh()?view.control:"none";
  if(control!=lastControl){
    if(control=="keeper")Say("Keeper has the controls. You are spectating; take control back from the PLAN tab or log off.");
    else if(lastControl=="keeper")Say(Fresh()?"The controls are yours again.":"The plan desk stopped answering; the proxy gate lapses on its own within two minutes.");
    // The tab button reads KEEPER while the gate is held; repaint it (it is a sibling under hStats).
    lastControl=control;RedrawWindow(GetParent(panel),NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN);
  }
  std::ostringstream s;s<<"M59PLANREQ\t1\t"<<Now()<<'\t'<<b[0]<<'\t'<<b[1]<<'\t'<<b[2]<<'\t'<<b[3]<<'\t'<<b[4]<<'\t'<<(shown?1:0)<<'\n';
  for(size_t n=0;n<inventory.size()&&n<512;n++)s<<"I\t"<<inventory[n].id<<'\t'<<Enc(inventory[n].name)<<'\t'<<inventory[n].amount<<'\n';
  for(const auto& p:pending)s<<"R\t"<<p.seq<<'\t'<<p.entry<<'\t'<<Enc(p.args)<<'\n';
  s<<"END\n";Write(root+"\\plan\\in\\"+Pid()+".tsv",s.str());
}

WNDPROC oldList=NULL;
LRESULT CALLBACK ListProc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
  if(msg==WM_KEYDOWN&&wp==VK_RETURN){Activate(SelectedEntry());return 0;}
  if(msg==WM_RBUTTONDOWN){DWORD hit=(DWORD)SendMessage(h,LB_ITEMFROMPOINT,0,lp);if(!HIWORD(hit))SendMessage(h,LB_SETCURSEL,LOWORD(hit),0);Describe();return 0;}
  return CallWindowProc(oldList,h,msg,wp,lp);
}
LRESULT CALLBACK PanelProc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
  switch(msg){
  case WM_TIMER:Tick();return 0;
  case WM_MEASUREITEM:((MEASUREITEMSTRUCT*)lp)->itemHeight=std::max(18,(int)GetFontHeight(GetFont(FONT_STATS))+4);return TRUE;
  case WM_DRAWITEM:DrawRow((const DRAWITEMSTRUCT*)lp);return TRUE;
  case WM_COMMAND:{int id=LOWORD(wp),code=HIWORD(wp);
    if(id==1&&code==LBN_SELCHANGE){int i=(int)SendMessage(list,LB_GETCURSEL,0,0);
      if(i>=0&&(size_t)i<rows.size()&&rows[i].header)SendMessage(list,LB_SETCURSEL,i+1<(int)rows.size()?i+1:-1,0);Describe();return 0;}
    if(id==1&&code==LBN_DBLCLK){Activate(SelectedEntry());return 0;}
    if(id==3){SubmitArgs();return 0;}
    if(id==4){CloseArgs();return 0;}
    break;}
  }
  return DefWindowProc(h,msg,wp,lp);
}
}  // namespace

void PlanPaneCreate(HWND parent,const std::string& dir){
  root=dir;
  WNDCLASSA wc={};wc.lpfnWndProc=PanelProc;wc.hInstance=hInst;wc.lpszClassName="M59PlanPane";wc.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);wc.hCursor=LoadCursor(NULL,IDC_ARROW);RegisterClassA(&wc);
  panel=CreateWindowExA(0,wc.lpszClassName,"",WS_CHILD|WS_CLIPCHILDREN,0,0,1,1,parent,NULL,hInst,NULL);
  list=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_NOINTEGRALHEIGHT|WS_TABSTOP,0,0,1,1,panel,(HMENU)1,hInst,NULL);
  oldList=(WNDPROC)SetWindowLongPtr(list,GWLP_WNDPROC,(LONG_PTR)ListProc);
  detail=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0,0,1,1,panel,(HMENU)2,hInst,NULL);
  heading=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|SS_LEFT|SS_ENDELLIPSIS,0,0,1,1,panel,NULL,hInst,NULL);
  run=CreateWindowA("BUTTON","Run",WS_CHILD|WS_TABSTOP|BS_DEFPUSHBUTTON,0,0,1,1,panel,(HMENU)3,hInst,NULL);
  cancel=CreateWindowA("BUTTON","Cancel",WS_CHILD|WS_TABSTOP,0,0,1,1,panel,(HMENU)4,hInst,NULL);
  for(int n=0;n<MAX_ARGS;n++)labels[n]=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|SS_LEFT|SS_ENDELLIPSIS,0,0,1,1,panel,NULL,hInst,NULL);
  for(HWND w:{detail,heading,run,cancel})SendMessage(w,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
  for(int n=0;n<MAX_ARGS;n++)SendMessage(labels[n],WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
  CreateDirectoryA((root+"\\plan").c_str(),NULL);CreateDirectoryA((root+"\\plan\\in").c_str(),NULL);
  SetTimer(panel,1,1000,NULL);Describe();
}
void PlanPaneDestroy(){if(panel)DestroyWindow(panel);panel=list=detail=heading=run=cancel=NULL;
  for(int n=0;n<MAX_ARGS;n++)labels[n]=inputs[n]=NULL;view=PlanView();rows.clear();pending.clear();inventory.clear();editing=-1;shown=false;}
void PlanPaneShow(){if(!panel)return;shown=true;ShowWindow(panel,SW_SHOW);Tick();Rebuild();}
void PlanPaneHide(){if(!panel)return;shown=false;if(editing>=0)CloseArgs();ShowWindow(panel,SW_HIDE);}
void PlanPaneResize(int top,int w,int h){
  if(!panel)return;
  if(top>=0)MoveWindow(panel,0,top,std::max(1,w),std::max(1,h),TRUE);
  w=std::max(1,w);h=std::max(1,h);
  const int dh=std::max(48,h*35/100);
  if(editing<0){MoveWindow(list,0,0,w,h-dh,TRUE);MoveWindow(detail,0,h-dh,w,dh,TRUE);return;}
  int y=2;MoveWindow(heading,4,y,w-8,18,TRUE);y+=20;
  const PlanEntry& e=view.entries[editing];
  for(size_t n=0;n<e.args.size()&&n<MAX_ARGS;n++){
    MoveWindow(labels[n],4,y,w-8,16,TRUE);y+=16;
    if(inputs[n])MoveWindow(inputs[n],4,y,w-8,e.args[n].type=="choice"?260:22,TRUE);y+=26;
  }
  MoveWindow(run,4,y,(w-12)/2,24,TRUE);MoveWindow(cancel,8+(w-12)/2,y,(w-12)/2,24,TRUE);y+=28;
  MoveWindow(detail,0,y,w,std::max(1,h-y),TRUE);
}
void PlanPaneInventory(const std::vector<M59InventoryIntent::Item>& items){
  inventory.clear();for(const auto& i:items)inventory.push_back({i.id,i.amount,i.name});
}
void PlanPaneNoteIcon(const char* name,ID icon){if(name&&icon&&icons.size()<4096)icons[Fold(name)]=icon;}
bool PlanPaneLocked(){return !root.empty()&&Fresh()&&view.control=="keeper";}
bool PlanPaneVetoes(int action){
  if(!PlanPaneLocked())return false;
  bool veto=IsMoveAction(action)||IsTurnAction(action)||IsAttackAction(action)||action==A_PICKUP||action==A_DROP||action==A_PUT||
    action==A_USE||action==A_UNUSE||action==A_OFFER||action==A_BUY||action==A_APPLY||action==A_GO||action==A_ACTIVATE||
    action==A_ACTIVATEMOUSE||action==A_DEPOSIT||action==A_WITHDRAW||action==A_TOGGLEUSE;
  if(veto&&Now()>lastVetoNotice+5000){lastVetoNotice=Now();Say("Keeper has the controls (PLAN tab: Take control back).");}
  return veto;
}
const char* PlanPaneButtonLabel(bool* locked){bool l=PlanPaneLocked();if(locked)*locked=l;return l?"KEEPER":"PLAN";}

void CommandPlan(char* args){
  std::string a=args?args:"";while(!a.empty()&&a[0]==' ')a.erase(0,1);
  if(root.empty()){Say("the PLAN tab is off (M59_CLIENT_CONTEXT_DIR is not set)");return;}
  if(!Fresh()){Say("the plan desk is not answering");return;}
  if(a.empty()){for(const auto& e:view.entries)if(!e.disabled)Say(e.id+"  —  "+e.title+(e.running?"  [running]":""));return;}
  std::istringstream in(a);std::string key,tok,query;in>>key;
  int i=FindEntry(key);
  if(i==-2){Say("\""+key+"\" matches more than one row; type more of it");return;}
  if(i<0){Say("no row \""+key+"\" (type plan alone for the list)");return;}
  const PlanEntry& e=view.entries[i];
  // The chat box splits on spaces, so a value with one is typed %20 (item=orc%20tooth): decode, then encode once.
  while(in>>tok){auto eq=tok.find('=');std::string value;
    if(eq==std::string::npos){Say("expected key=value, got "+tok);return;}
    if(!Dec(tok.substr(eq+1),value)){Say("cannot read the value in "+tok);return;}
    if(!query.empty())query+="&";query+=ArgEnc(tok.substr(0,eq))+"="+ArgEnc(value);}
  if(e.disabled){Say(e.title+": "+e.describe);return;}
  if(query.empty()&&!e.args.empty()){StatsSetActiveGroup(STATS_PLAN);OpenArgs(i);return;}
  Queue(e.id,query);
}
void CommandFleetScript(char* args){
  std::string a=args?args:"";while(!a.empty()&&a[0]==' ')a.erase(0,1);
  if(a.empty()){Say("usage: fs <fleetscript line>, e.g. fs zheng-he-lap agents=hk2");return;}
  if(root.empty()||!Fresh()){Say("the plan desk is not answering");return;}
  Queue("fs.line","line="+ArgEnc(a));
}
