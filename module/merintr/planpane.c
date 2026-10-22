// THE MANAGE TAB (once PLAN): what the fleet can be asked to do, as a list that works like Spells.
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
// shows what a row does. The same rows run from the chat box: `manage <row> key=value` (or
// `plan`, its old name), and `fs <fleetscript line>` for the FleetScript REPL. The tab, its
// files and the desk keep the PLAN name internally (M59PLAN, plan/, STATS_PLAN): only what a
// person reads was renamed, so a desk and a client of either age still find each other.
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
#include <memory>

namespace {
using M59Context::Time;
struct Option { std::string value, label, when; };
struct Bound { long min=0, max=0; std::string when; };
struct Arg { std::string id, type, title, def, help; std::vector<Option> options; std::vector<Bound> bounds; };
struct PlanEntry { std::string id, group, icon, title, describe; bool running=false, disabled=false, configure=false; std::vector<Arg> args; };
struct Message { Time seq=0; bool ok=false; std::string text; };
struct PlanView { Time at=0, ack=0; std::string control="none", status; std::vector<PlanEntry> entries; std::vector<Message> messages; };
struct Pending { Time seq; std::string entry, args; Time queued; };
struct Row { bool header; int entry; std::string group; };
struct Invent { unsigned int id, amount; std::string name; };

// Protocol 2 (the desk's m59-plan-desk.mjs header): sixteen arguments, the agents/number/combo/task
// types, options and ranges that depend on another argument (O/N rows' <when>), per-argument help.
const int MAX_ARGS=16, MAX_OPTIONS=4000;
HWND panel=NULL, list=NULL, detail=NULL, dialog=NULL;
std::string root;
PlanView view;
std::vector<Row> rows;
std::vector<Pending> pending;
std::vector<Invent> inventory;
std::map<std::string,ID> icons;
Time lastSeq=0, lastShownMessage=0, lastVetoNotice=0;
// THE PROTOCOL THIS CLIENT ASKS FOR. A desk from before protocol 2 rejects a "2" request outright
// (and every desk on a deploy cut before 2026-10-21 is one), so a client that asked only for 2
// would show "not answering" until the next deploy. Unanswered for three seconds, it asks the
// other one; answered, it stays. Against an old desk that settles on 1 within a few seconds.
int protocol=2;Time protocolSince=0;
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
void Say(const std::string& s){std::string m="MANAGE: "+s;std::vector<char> b(m.begin(),m.end());b.push_back(0);GameMessage(b.data());}

// ------------------------------------------------------------------ the desk's file
bool Parse(const std::string& text,PlanView& out){
  if(text.size()<5||text.compare(text.size()-5,5,"\nEND\n")!=0)return false;
  std::istringstream in(text);std::string line;std::getline(in,line);auto h=M59Context::Split(line);
  PlanView v;
  if(h.size()!=7||h[0]!="M59PLAN"||(h[1]!="1"&&h[1]!="2")||!M59Context::Number(h[2],v.at)||h[3]!=Pid()||!M59Context::Number(h[4],v.ack))return false;
  if(h[5]!="none"&&h[5]!="keeper"&&h[5]!="other")return false;
  v.control=h[5];if(!Dec(h[6],v.status))return false;
  while(std::getline(in,line)){
    if(line=="END"){out=std::move(v);return true;}
    auto f=M59Context::Split(line);
    if(f[0]=="E"&&f.size()==7&&v.entries.size()<256){PlanEntry e;e.id=f[1];
      if(!Dec(f[2],e.group)||!Dec(f[3],e.icon)||!Dec(f[4],e.title)||!Dec(f[6],e.describe))return false;
      e.running=f[5].find('r')!=std::string::npos;e.disabled=f[5].find('d')!=std::string::npos;e.configure=f[5].find('c')!=std::string::npos;v.entries.push_back(e);}
    else if(f[0]=="A"&&(f.size()==6||f.size()==7)&&!v.entries.empty()&&v.entries.back().id==f[1]&&v.entries.back().args.size()<MAX_ARGS){Arg a;a.id=f[2];a.type=f[3];
      if(!Dec(f[4],a.title)||!Dec(f[5],a.def)||(f.size()==7&&!Dec(f[6],a.help)))return false;v.entries.back().args.push_back(a);}
    else if(f[0]=="O"&&(f.size()==5||f.size()==6)&&!v.entries.empty()&&v.entries.back().id==f[1]&&!v.entries.back().args.empty()&&v.entries.back().args.back().id==f[2]){
      Option o;if(!Dec(f[3],o.value)||!Dec(f[4],o.label)||(f.size()==6&&!Dec(f[5],o.when)))return false;if(v.entries.back().args.back().options.size()<(size_t)MAX_OPTIONS)v.entries.back().args.back().options.push_back(o);}
    else if(f[0]=="N"&&f.size()==6&&!v.entries.empty()&&v.entries.back().id==f[1]&&!v.entries.back().args.empty()&&v.entries.back().args.back().id==f[2]){
      Bound b;Time lo=0,hi=0;if(!M59Context::Number(f[3],lo)||!M59Context::Number(f[4],hi)||lo>hi||hi>2000000000||!Dec(f[5],b.when))return false;
      b.min=(long)lo;b.max=(long)hi;if(v.entries.back().args.back().bounds.size()<64)v.entries.back().args.back().bounds.push_back(b);}
    else if(f[0]=="M"&&f.size()==4){Message m;if(!M59Context::Number(f[1],m.seq)||!Dec(f[3],m.text))return false;m.ok=f[2]=="1";v.messages.push_back(m);}
    // A well-formed row past a cap (a 257th entry, a 7th argument, the options of a dropped argument)
    // is SKIPPED, not fatal: one FleetScript with seven params once blanked the whole tab. Unknown
    // row types and malformed fields still reject the reply.
    else if(f[0]!="E"&&f[0]!="A"&&f[0]!="O"&&f[0]!="N"&&f[0]!="M")return false;
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
  std::string s;int i=SelectedEntry();
  if(!Fresh())s="The plan desk is not answering. Start it with:\r\n  node tools/m59-plan-desk.mjs --fleet <name>\r\nfrom maps/m59-harness (the broker normally runs it). Your controls are your own.\r\n\r\n";
  else if(view.control=="keeper")s="KEEPER HAS THE CONTROLS. You are spectating; movement, combat and items are refused until you take control back.\r\n\r\n";
  if(i>=0){const PlanEntry& e=view.entries[i];s+=e.title+(e.running?"   [running]":"")+(e.disabled?"   [unavailable]":"")+"\r\n"+e.describe+"\r\n";}
  else s+=view.status+"\r\nDouble-click a row to run it. Right-click to read it. Chat: manage <row> key=value, or fs <line>.";
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
// ARGUMENTS AND SETTINGS OPEN IN THEIR OWN WINDOW, like the Guild page: the stats pane is a
// strip a few hundred pixels tall, and a settings row (flag 'c') is a small form, not a spell.
//
// EACH ARGUMENT IS THE PICKER ITS TYPE NAMES: a checkbox (boolean), a dropdown (choice), an edit box
// with suggestions (combo), a multi-select of the fleet (agents), a number box with a slider when
// the desk gives it a range (number), or a FleetScript picked and configured in a form of its own
// (task). An option or a range with a <when> follows another argument: pick a merchant and the item
// list becomes his shelf; pick a bank and the withdraw slider tops out at what is banked there.
//
// FORMS STACK. "Configure..." on a task argument opens the chosen script's own form ON TOP of this
// one, without its agents (the outer run owns the bodies); its Save hands "<entry>?<args>" back to
// the argument that opened it. Only the top form takes input; the ones under it are disabled.
struct Form {
  PlanEntry e;                        // a copy: the desk's reply can renumber the rows under an open form
  HWND dlg=NULL, heading=NULL, run=NULL, cancel=NULL;
  HWND labels[MAX_ARGS]={}, inputs[MAX_ARGS]={}, extras[MAX_ARGS]={};
  std::vector<int> shown[MAX_ARGS];   // choice/combo/task: list position -> option index
  std::string composed[MAX_ARGS];     // task: "<entry>?<args>" from its nested form
  bool compose=false;                 // a nested form: Save hands its args to the one below
  int parentArg=-1;
  bool syncing=false;
};
std::vector<std::unique_ptr<Form>> forms;
Form* Top(){return forms.empty()?nullptr:forms.back().get();}
const int ID_ARG=200, ID_EXTRA=300;

// Percent-encoding of UTF-8 as it is (ArgEnc's Encode converts from the ANSI codepage first).
std::string ArgEnc(const std::string& s){std::string o;const char* hex="0123456789ABCDEF";
  for(unsigned char c:s){if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')o+=(char)c;else{o+='%';o+=hex[c>>4];o+=hex[c&15];}}return o;}
std::string Utf8(HWND h){wchar_t buf[4001]={};GetWindowTextW(h,buf,4000);int k=WideCharToMultiByte(CP_UTF8,0,buf,-1,NULL,0,NULL,NULL);
  std::string v;if(k>1){v.resize(k-1);WideCharToMultiByte(CP_UTF8,0,buf,-1,&v[0],k,NULL,NULL);}return v;}

std::string ValueOf(Form& f,int n){
  const Arg& a=f.e.args[n];HWND in=f.inputs[n];
  if(!in)return a.def;
  if(a.type=="boolean")return SendMessage(in,BM_GETCHECK,0,0)==BST_CHECKED?"true":"false";
  if(a.type=="choice"||a.type=="task"){int s=(int)SendMessage(in,CB_GETCURSEL,0,0);
    if(s<0||(size_t)s>=f.shown[n].size())return "";
    std::string v=a.options[f.shown[n][s]].value;
    if(a.type=="task"&&!v.empty()&&f.composed[n].rfind(v+"?",0)==0)return f.composed[n];
    return v;}
  if(a.type=="agents"){std::string v;int c=(int)SendMessage(in,LB_GETCOUNT,0,0);
    for(int k=0;k<c&&k<(int)a.options.size();k++)if(SendMessage(in,LB_GETSEL,k,0)>0){if(!v.empty())v+=",";v+=a.options[k].value;}
    return v;}
  std::string t=Utf8(in);
  if(a.type=="combo")for(int k:f.shown[n])if(a.options[k].label==t)return a.options[k].value;
  return t;
}
// "<arg>=<value>": that argument holds the value, or (a list) contains it. The desk's whenHolds.
bool Holds(Form& f,const std::string& when){
  if(when.empty())return true;
  auto eq=when.find('=');if(eq==std::string::npos||eq==0)return false;
  std::string id=when.substr(0,eq),want=when.substr(eq+1);
  for(size_t n=0;n<f.e.args.size()&&n<(size_t)MAX_ARGS;n++)if(f.e.args[n].id==id){
    std::string v=ValueOf(f,(int)n);if(v==want)return true;
    std::stringstream ss(v);std::string p;while(std::getline(ss,p,','))if(p==want)return true;return false;}
  return false;
}
bool Dependent(const Arg& a){for(const auto& o:a.options)if(!o.when.empty())return true;
  for(const auto& b:a.bounds)if(!b.when.empty())return true;return false;}

void FillOptions(Form& f,int n,const std::string& keep){
  const Arg& a=f.e.args[n];HWND in=f.inputs[n];if(!in)return;
  f.syncing=true;
  std::string text=a.type=="combo"?Utf8(in):"";
  SendMessage(in,CB_RESETCONTENT,0,0);f.shown[n].clear();int sel=-1,def=-1;
  for(size_t k=0;k<a.options.size();k++){if(!Holds(f,a.options[k].when))continue;
    int at=(int)f.shown[n].size();if(a.options[k].value==keep)sel=at;if(a.options[k].value==a.def)def=at;
    f.shown[n].push_back((int)k);std::wstring w=Wide(a.options[k].label);SendMessageW(in,CB_ADDSTRING,0,(LPARAM)w.c_str());}
  if(a.type=="combo"){
    // An edit box keeps what was typed; a remembered value shows as its label when it is offered.
    std::string show=keep;for(int k:f.shown[n])if(a.options[k].value==keep)show=a.options[k].label;
    Text(in,keep.empty()?text:show);
  } else SendMessage(in,CB_SETCURSEL,sel>=0?sel:def>=0?def:(f.shown[n].empty()?-1:0),0);
  f.syncing=false;
}
const Bound* BoundOf(Form& f,int n){for(const auto& b:f.e.args[n].bounds)if(Holds(f,b.when))return &b;return nullptr;}
void ApplyBounds(Form& f,int n){
  const Arg& a=f.e.args[n];if(a.type!="number"||!f.extras[n])return;
  const Bound* b=BoundOf(f,n);
  ShowWindow(f.extras[n],b?SW_SHOW:SW_HIDE);
  std::string title=a.title;
  if(b){SendMessage(f.extras[n],TBM_SETRANGEMIN,FALSE,b->min);SendMessage(f.extras[n],TBM_SETRANGEMAX,TRUE,b->max);
    long span=b->max-b->min;SendMessage(f.extras[n],TBM_SETPAGESIZE,0,std::max(1L,span/10));
    long v=atol(ValueOf(f,n).c_str());SendMessage(f.extras[n],TBM_SETPOS,TRUE,std::min(b->max,std::max(b->min,v)));
    title+="   ["+std::to_string(b->min)+" - "+std::to_string(b->max)+"]";}
  if(f.labels[n])Text(f.labels[n],title);
}
// Something changed: every argument that depends on another is recomputed, twice for a chain of them.
void Refresh(Form& f,int changed){
  if(f.syncing)return;
  for(int pass=0;pass<2;pass++)
    for(size_t n=0;n<f.e.args.size()&&n<(size_t)MAX_ARGS;n++){
      if((int)n==changed||!Dependent(f.e.args[n]))continue;
      const std::string& t=f.e.args[n].type;
      if(t=="choice"||t=="combo"||t=="task")FillOptions(f,(int)n,ValueOf(f,(int)n));
      else if(t=="number")ApplyBounds(f,(int)n);
    }
}
void ShowHelp(Form& f,int n){
  const Arg& a=f.e.args[n];std::string about=f.e.describe;if(about.size()>700)about=about.substr(0,700)+"...";
  if(!a.help.empty())about=a.title+": "+a.help+"\r\n\r\n"+about;
  Text(f.heading,about);
}

void CloseForm(Form* f);
void CloseArgs(){while(!forms.empty())CloseForm(forms.front().get());}
LRESULT CALLBACK DialogProc(HWND h,UINT msg,WPARAM wp,LPARAM lp);

Form* OpenForm(const PlanEntry& e,bool compose,int parentArg){
  static bool registered=false;
  if(!registered){WNDCLASSA wc={};wc.lpfnWndProc=DialogProc;wc.hInstance=hInst;wc.lpszClassName="M59PlanSettings";
    wc.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);wc.hCursor=LoadCursor(NULL,IDC_ARROW);RegisterClassA(&wc);registered=true;}
  Form* under=Top();
  forms.push_back(std::make_unique<Form>());Form& f=*forms.back();f.e=e;f.compose=compose;f.parentArg=parentArg;
  const int W=480,pad=10,cw=W-2*pad-16;int y=pad;
  HWND owner=under?under->dlg:GetAncestor(panel,GA_ROOT);RECT o;GetWindowRect(owner?owner:panel,&o);
  f.dlg=CreateWindowExW(WS_EX_DLGMODALFRAME,L"M59PlanSettings",Wide(compose?e.title+" (nested)":e.title).c_str(),WS_POPUP|WS_CAPTION|WS_SYSMENU,
    0,0,W,200,owner,NULL,hInst,NULL);
  SetWindowLongPtr(f.dlg,GWLP_USERDATA,(LONG_PTR)&f);
  HFONT font=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
  auto make=[&](const wchar_t* cls,const std::string& text,DWORD style,DWORD ex,int x,int yy,int w,int hh,int id){
    HWND c=CreateWindowExW(ex,cls,Wide(text).c_str(),WS_CHILD|WS_VISIBLE|style,x,yy,w,hh,f.dlg,(HMENU)(INT_PTR)id,hInst,NULL);
    SendMessage(c,WM_SETFONT,(WPARAM)font,TRUE);return c;};
  f.heading=make(L"EDIT","",ES_MULTILINE|ES_READONLY|WS_VSCROLL,WS_EX_CLIENTEDGE,pad,y,cw,72,0);y+=80;
  for(size_t n=0;n<e.args.size()&&n<(size_t)MAX_ARGS;n++){const Arg& a=e.args[n];int id=ID_ARG+(int)n;
    if(compose&&a.type=="agents")continue;   // the outer form's agents run every stage
    if(a.type=="boolean"){
      f.inputs[n]=make(L"BUTTON",a.title,BS_AUTOCHECKBOX|BS_NOTIFY|WS_TABSTOP,0,pad,y,cw,20,id);
      SendMessage(f.inputs[n],BM_SETCHECK,a.def=="true"?BST_CHECKED:BST_UNCHECKED,0);y+=24;continue;}
    f.labels[n]=make(L"STATIC",a.title,SS_LEFT|SS_ENDELLIPSIS,0,pad,y,cw,15,0);y+=16;
    if(a.type=="agents"){
      int rowsShown=std::max(3,std::min(8,(int)a.options.size()));
      f.inputs[n]=make(L"LISTBOX",std::string(),LBS_MULTIPLESEL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_TABSTOP,WS_EX_CLIENTEDGE,pad,y,cw,rowsShown*15+6,id);
      std::string want=","+a.def+",";
      for(size_t k=0;k<a.options.size();k++){std::wstring w=Wide(a.options[k].label);SendMessageW(f.inputs[n],LB_ADDSTRING,0,(LPARAM)w.c_str());
        if(want.find(","+a.options[k].value+",")!=std::string::npos)SendMessage(f.inputs[n],LB_SETSEL,TRUE,(LPARAM)k);}
      y+=rowsShown*15+12;continue;}
    if(a.type=="choice"||a.type=="combo"||a.type=="task"){
      int w=a.type=="task"?cw-96:cw;
      f.inputs[n]=make(L"COMBOBOX",std::string(),(a.type=="combo"?CBS_DROPDOWN|CBS_AUTOHSCROLL:CBS_DROPDOWNLIST)|WS_VSCROLL|WS_TABSTOP,0,pad,y,w,300,id);
      if(a.type=="task")f.extras[n]=make(L"BUTTON","Configure...",WS_TABSTOP,0,pad+w+6,y,90,23,ID_EXTRA+(int)n);
      FillOptions(f,(int)n,a.def);
    } else if(a.type=="number"){
      f.inputs[n]=make(L"EDIT",a.def,ES_AUTOHSCROLL|WS_TABSTOP,WS_EX_CLIENTEDGE,pad,y,110,22,id);
      f.extras[n]=make(TRACKBAR_CLASSW,std::string(),TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,0,pad+118,y,cw-118,24,ID_EXTRA+(int)n);
      ShowWindow(f.extras[n],SW_HIDE);
    } else {
      f.inputs[n]=make(L"EDIT",a.def,ES_AUTOHSCROLL|WS_TABSTOP|(a.type=="count"?ES_NUMBER:0),WS_EX_CLIENTEDGE,pad,y,cw,22,id);
      SendMessage(f.inputs[n],EM_SETLIMITTEXT,4000,0);
    }
    y+=28;
  }
  // Now every control exists: the ones that follow another argument (even a LATER one) settle.
  for(size_t n=0;n<e.args.size()&&n<(size_t)MAX_ARGS;n++)if(e.args[n].type=="number")ApplyBounds(f,(int)n);
  Refresh(f,-1);
  std::string about=e.describe;if(about.size()>900)about=about.substr(0,900)+"...";Text(f.heading,about);
  y+=4;const int bw=(cw-10)/2;
  f.run=make(L"BUTTON",compose?"OK":e.configure?"Save":"Run",BS_DEFPUSHBUTTON|WS_TABSTOP,0,pad,y,bw,26,3);
  f.cancel=make(L"BUTTON","Cancel",WS_TABSTOP,0,pad+bw+10,y,bw,26,4);y+=26+pad;
  RECT want={0,0,W,y};AdjustWindowRectEx(&want,WS_POPUP|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);
  int ww=want.right-want.left,wh=want.bottom-want.top,dx=under?24:0;
  SetWindowPos(f.dlg,NULL,o.left+((o.right-o.left)-ww)/2+dx,o.top+((o.bottom-o.top)-wh)/2+dx,ww,wh,SWP_NOZORDER);
  if(under)EnableWindow(under->dlg,FALSE);
  ShowWindow(f.dlg,SW_SHOW);
  for(int n=0;n<MAX_ARGS;n++)if(f.inputs[n]){SetFocus(f.inputs[n]);break;}
  dialog=f.dlg;
  return &f;
}
// Closing a form closes every form stacked on it.
void CloseForm(Form* f){
  for(;;){
    if(forms.empty())break;
    Form* top=forms.back().get();HWND d=top->dlg;bool last=top==f;
    std::unique_ptr<Form> gone=std::move(forms.back());forms.pop_back();
    SetWindowLongPtr(d,GWLP_USERDATA,0);DestroyWindow(d);
    if(Form* under=Top()){EnableWindow(under->dlg,TRUE);SetForegroundWindow(under->dlg);}
    if(last)break;
  }
  dialog=forms.empty()?NULL:forms.back()->dlg;
  if(forms.empty()){editing=-1;Describe();}
}
void OpenArgs(int i){CloseArgs();editing=i;OpenForm(view.entries[i],false,-1);}
std::string ArgsOf(Form& f){
  std::string args;
  for(size_t n=0;n<f.e.args.size()&&n<(size_t)MAX_ARGS;n++){const Arg& a=f.e.args[n];
    if(f.compose&&a.type=="agents")continue;
    if(!args.empty())args+="&";args+=ArgEnc(a.id)+"="+ArgEnc(ValueOf(f,(int)n));}
  return args;
}
int EntryById(const std::string& id){for(size_t n=0;n<view.entries.size();n++)if(view.entries[n].id==id)return (int)n;return -1;}
// "Configure..." on a task argument: the chosen script's own form, stacked on this one.
void OpenNested(Form& f,int n){
  std::string id=ValueOf(f,n);id=id.substr(0,id.find('?'));
  if(id.empty()){Say("pick a script first");return;}
  int i=EntryById(id);
  if(i<0){Say("the desk no longer offers "+id);return;}
  std::string prior=f.composed[n];
  Form* child=OpenForm(view.entries[i],true,n);
  // Reopening a configured stage shows what it was configured with.
  if(prior.rfind(id+"?",0)==0){std::string q=prior.substr(id.size()+1),part;std::stringstream ss(q);
    while(std::getline(ss,part,'&')){auto eq=part.find('=');if(eq==std::string::npos)continue;std::string k,v;
      if(!Dec(part.substr(0,eq),k)||!Dec(part.substr(eq+1),v))continue;
      for(size_t m=0;m<child->e.args.size()&&m<(size_t)MAX_ARGS;m++){const Arg& a=child->e.args[m];if(a.id!=k||!child->inputs[m])continue;
        if(a.type=="boolean")SendMessage(child->inputs[m],BM_SETCHECK,v=="true"?BST_CHECKED:BST_UNCHECKED,0);
        else if(a.type=="choice"||a.type=="combo"||a.type=="task")FillOptions(*child,(int)m,v);
        else Text(child->inputs[m],v);}}
    Refresh(*child,-1);}
}
void SubmitForm(Form* f){
  if(!f)return;
  if(f->compose){
    Form* under=forms.size()>=2?forms[forms.size()-2].get():nullptr;
    std::string args=ArgsOf(*f),id=f->e.id;int n=f->parentArg;
    CloseForm(f);
    if(under&&n>=0&&n<MAX_ARGS){under->composed[n]=id+(args.empty()?"":"?"+args);
      if(under->extras[n])Text(under->extras[n],"Configured");Refresh(*under,-1);}
    return;
  }
  std::string id=f->e.id,args=ArgsOf(*f);CloseArgs();Queue(id,args);
}
void SubmitArgs(){SubmitForm(Top());}
LRESULT CALLBACK DialogProc(HWND h,UINT msg,WPARAM wp,LPARAM lp){
  Form* f=(Form*)GetWindowLongPtr(h,GWLP_USERDATA);
  if(!f)return DefWindowProc(h,msg,wp,lp);
  if(msg==WM_COMMAND){int id=LOWORD(wp),code=HIWORD(wp);
    if(id==3){SubmitForm(f);return 0;}
    if(id==4){CloseForm(f);return 0;}
    if(id>=ID_EXTRA&&id<ID_EXTRA+MAX_ARGS&&code==BN_CLICKED&&f->e.args[id-ID_EXTRA].type=="task"){OpenNested(*f,id-ID_EXTRA);return 0;}
    if(id>=ID_ARG&&id<ID_ARG+MAX_ARGS&&(size_t)(id-ID_ARG)<f->e.args.size()){int n=id-ID_ARG;const std::string t=f->e.args[n].type;
      bool list=t=="choice"||t=="combo"||t=="task",edit=t=="number"||t=="text"||t=="count";
      if((list&&code==CBN_SETFOCUS)||(t=="agents"&&code==LBN_SETFOCUS)||(edit&&code==EN_SETFOCUS)||(t=="boolean"&&code==BN_SETFOCUS))ShowHelp(*f,n);
      if(t=="task"&&code==CBN_SELCHANGE&&!f->syncing){f->composed[n].clear();if(f->extras[n])Text(f->extras[n],"Configure...");}
      if(t=="number"&&code==EN_CHANGE&&!f->syncing&&f->extras[n]){const Bound* b=BoundOf(*f,n);
        if(b)SendMessage(f->extras[n],TBM_SETPOS,TRUE,std::min(b->max,std::max(b->min,atol(ValueOf(*f,n).c_str()))));}
      bool changed=(list&&(code==CBN_SELCHANGE||code==CBN_EDITCHANGE))||(t=="agents"&&code==LBN_SELCHANGE)||(t=="boolean"&&code==BN_CLICKED)||(edit&&code==EN_CHANGE);
      if(changed)Refresh(*f,n);
      return 0;}
  }
  if(msg==WM_HSCROLL&&lp){for(int n=0;n<MAX_ARGS;n++)if(f->extras[n]==(HWND)lp&&f->inputs[n]){
      long pos=(long)SendMessage(f->extras[n],TBM_GETPOS,0,0);f->syncing=true;Text(f->inputs[n],std::to_string(pos));f->syncing=false;Refresh(*f,n);return 0;}}
  if(msg==WM_CLOSE){CloseForm(f);return 0;}
  return DefWindowProc(h,msg,wp,lp);
}
void Activate(int i){
  if(i<0)return;const PlanEntry& e=view.entries[i];
  if(e.disabled){Describe();Say(e.title+": "+e.describe);return;}
  if(e.args.empty())Queue(e.id,"");else OpenArgs(i);
}

// ------------------------------------------------------------------ the loop
// The other players in this room, by the names only this client can resolve. The courier's Full
// Auto needs to know it is standing next to you; the broker has no room for a piloted body.
#ifndef M59_CONTEXT_UI_TEST
std::vector<std::pair<unsigned int,std::string>> RoomPlayers(){
  std::vector<std::pair<unsigned int,std::string>> out;
  if(!cinfo->current_room)return out;
  for(list_type l=cinfo->current_room->contents;l&&out.size()<128;l=l->next){
    room_contents_node* r=(room_contents_node*)l->data;
    if(r&&(r->obj.flags&OF_PLAYER)&&r->obj.id!=cinfo->player->id)out.push_back({GetObjId(r->obj.id),LookupNameRsc(r->obj.name_res)});
  }
  return out;
}
#else
std::vector<std::pair<unsigned int,std::string>> testRoom;
std::vector<std::pair<unsigned int,std::string>> RoomPlayers(){return testRoom;}
#endif
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
    if(control=="keeper")Say("Keeper has the controls. You are spectating; take control back from the MANAGE tab or log off.");
    else if(lastControl=="keeper")Say(Fresh()?"The controls are yours again.":"The plan desk stopped answering; the proxy gate lapses on its own within two minutes.");
    // The tab button reads KEEPER while the gate is held; repaint it (it is a sibling under hStats).
    lastControl=control;RedrawWindow(GetParent(panel),NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN);
  }
  if(!protocolSince||Fresh())protocolSince=Now();
  else if(Now()>protocolSince+3000){protocol=protocol==2?1:2;protocolSince=Now();}
  std::ostringstream s;s<<"M59PLANREQ\t"<<protocol<<'\t'<<Now()<<'\t'<<b[0]<<'\t'<<b[1]<<'\t'<<b[2]<<'\t'<<b[3]<<'\t'<<b[4]<<'\t'<<(shown?1:0)<<'\n';
  for(size_t n=0;n<inventory.size()&&n<512;n++)s<<"I\t"<<inventory[n].id<<'\t'<<Enc(inventory[n].name)<<'\t'<<inventory[n].amount<<'\n';
  for(const auto& o:RoomPlayers())s<<"W\t"<<o.first<<'\t'<<Enc(o.second)<<'\n';
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
  SendMessage(detail,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
  CreateDirectoryA((root+"\\plan").c_str(),NULL);CreateDirectoryA((root+"\\plan\\in").c_str(),NULL);
  SetTimer(panel,1,1000,NULL);Describe();
}
void PlanPaneDestroy(){CloseArgs();if(panel)DestroyWindow(panel);panel=list=detail=NULL;
  view=PlanView();rows.clear();pending.clear();inventory.clear();editing=-1;shown=false;protocol=2;protocolSince=0;}
void PlanPaneShow(){if(!panel)return;shown=true;ShowWindow(panel,SW_SHOW);Tick();Rebuild();}
void PlanPaneHide(){if(!panel)return;shown=false;if(editing>=0)CloseArgs();ShowWindow(panel,SW_HIDE);}
void PlanPaneResize(int top,int w,int h){
  if(!panel)return;
  if(top>=0)MoveWindow(panel,0,top,std::max(1,w),std::max(1,h),TRUE);
  w=std::max(1,w);h=std::max(1,h);
  const int dh=std::max(48,h*35/100);
  MoveWindow(list,0,0,w,h-dh,TRUE);MoveWindow(detail,0,h-dh,w,dh,TRUE);
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
  if(veto&&Now()>lastVetoNotice+5000){lastVetoNotice=Now();Say("Keeper has the controls (MANAGE tab: Take control back).");}
  return veto;
}
const char* PlanPaneButtonLabel(bool* locked){bool l=PlanPaneLocked();if(locked)*locked=l;return l?"KEEPER":"MANAGE";}

void CommandPlan(char* args){
  std::string a=args?args:"";while(!a.empty()&&a[0]==' ')a.erase(0,1);
  if(root.empty()){Say("the MANAGE tab is off (M59_CLIENT_CONTEXT_DIR is not set)");return;}
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
