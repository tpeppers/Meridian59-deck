// Harness context in native stats real estate. Local files only; never game orders.
#ifndef M59_CONTEXT_UI_TEST
#include "client.h"
#include "merintr.h"
#endif
#include "inventoryintent.h"
#include "clientcontext.h"
#include "clientcontextmodel.h"
#include "planpane.h"

namespace {
using namespace M59Context;
HWND panel=NULL, list=NULL, detail=NULL, edit=NULL, apply=NULL, cancel=NULL;
WNDPROC oldList=NULL;
View view;
std::string root, binding, requestToken, op, strategy, field, value, editToken;
Time sequence=0, lastSequence=0;
StatGroup pane=STATS_DUM;
bool configured=false, editing=false;
std::string editingStrategy, editingField;
struct SeenCondition {std::string name;int level;Time at;};
std::map<unsigned int,SeenCondition> conditions;
std::map<unsigned int,std::string> carried;
unsigned int conditionPlayer=0;
struct SeenStat {unsigned int group,num;std::string name;int value,min,max,current;Time at;};
std::map<unsigned int,SeenStat> numericStats;
unsigned int statsPlayer=0;
std::string conditionIdentity,statIdentity;
Time Now(){return M59InventoryIntent::Now();}
std::string Encode(const std::string& s){return M59InventoryIntent::Encode(s.c_str());}
std::string Binding() {
  auto* p=cinfo->player;if(!p||!p->id)return "";
  char server[192]={};DWORD n=GetEnvironmentVariableA("M59_INVENTORY_SERVER",server,sizeof(server));
  if(n==0||n>=sizeof(server))_snprintf_s(server,sizeof(server),_TRUNCATE,"%s:%d",cinfo->config->comm.hostname,cinfo->config->comm.sockport);
  std::ostringstream s;s<<GetCurrentProcessId()<<'\t'<<Encode(server)<<'\t'<<Encode(cinfo->config->username)<<'\t'
    <<Encode(LookupNameRsc(p->name_res))<<'\t'<<GetObjId(p->id)<<'\t'<<GetObjId(p->room_id)<<'\t'<<p->room_res<<'\t'<<(unsigned int)p->room_security;
  return s.str();
}
std::string Identity() {auto fields=M59Context::Split(Binding());std::string key;for(size_t i=0;i<fields.size()&&i<5;i++)key+=fields[i]+'\t';return key;}
std::wstring Wide(const std::string& s) {
  int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.c_str(),-1,NULL,0);
  if(n<1)return L"Invalid text";std::wstring w(n,L'\0');MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,&w[0],n);w.pop_back();return w;
}
void Text(HWND hwnd,const std::string& text){SetWindowTextW(hwnd,Wide(text).c_str());}
std::string Read(const std::string& path) {
  HANDLE f=CreateFileA(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
  if(f==INVALID_HANDLE_VALUE)return "";LARGE_INTEGER size;DWORD got=0;std::string s;
  if(GetFileSizeEx(f,&size)&&size.QuadPart>0&&size.QuadPart<=262144){s.resize(static_cast<size_t>(size.QuadPart));
    if(!ReadFile(f,&s[0],static_cast<DWORD>(s.size()),&got,NULL)||got!=s.size())s.clear();}
  CloseHandle(f);return s;
}
void Write(const std::string& path,const std::string& s) {
  if(s.size()>262144)return;std::string tmp=path+".tmp";
  HANDLE f=CreateFileA(tmp.c_str(),GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
  if(f==INVALID_HANDLE_VALUE)return;DWORD wrote=0;
  bool ok=WriteFile(f,s.data(),static_cast<DWORD>(s.size()),&wrote,NULL)&&wrote==s.size();CloseHandle(f);
  if(ok)MoveFileExA(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING);
}
bool Current(){return !binding.empty()&&binding==Binding()&&Now()<=view.at+6000;}
int Selected(){return static_cast<int>(SendMessage(list,LB_GETCURSEL,0,0));}
void Describe() {
  if(editing)return;
  if(!Current()){
    std::string cached="Cached context; changes paused. Your client retains control.\r\n\r\n";
    if(pane==STATS_PLAN)cached+=view.plan;
    else {auto i=Selected();if(i>=0&&static_cast<size_t>(i)<view.strategies.size())cached+=view.strategies[i].title+"\r\n"+view.strategies[i].description;}
    Text(detail,cached);return;
  }
  if(pane==STATS_PLAN){Text(detail,view.plan+"\r\n"+view.status);return;}
  auto i=Selected();std::string s="Human control; DUM execution paused.\r\nLeft-click: configure on/off. Right-click: settings.\r\n"+view.status;
  if(i>=0&&static_cast<size_t>(i)<view.strategies.size()){
    const auto& x=view.strategies[i];s=x.title+"\r\n"+(x.enabled?"Configured ON (paused during possession)":"Configured OFF")+"\r\n\r\n"+x.description+"\r\n"+s;
  }Text(detail,s);
}
void PaintList() {
  auto i=Selected();std::string selected=i>=0&&static_cast<size_t>(i)<view.strategies.size()?view.strategies[i].id:"";
  SendMessage(list,WM_SETREDRAW,FALSE,0);SendMessage(list,LB_RESETCONTENT,0,0);
  for(const auto& s:view.strategies){auto w=Wide(std::string(s.enabled?"[ON] ":"[off] ")+s.title);SendMessageW(list,LB_ADDSTRING,0,(LPARAM)w.c_str());}
  if(i>=0&&static_cast<size_t>(i)<view.strategies.size())SendMessage(list,LB_SETCURSEL,i,0);
  SendMessage(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,NULL,TRUE);Describe();
}
void Submit(const std::string& action,const std::string& id,const std::string& key,const std::string& val,const std::string& token) {
  if(!Current()||token!=view.token||sequence>view.ack){Text(detail,"Context changed or a save is pending; wait for a fresh view.");return;}
  sequence=std::max(Now(),lastSequence+1);lastSequence=sequence;requestToken=token;op=action;strategy=id;field=key;value=val;
  Text(detail,"Saving local DUM configuration... No game action is executed.");
}
void EndEdit(){editing=false;ShowWindow(edit,SW_HIDE);ShowWindow(apply,SW_HIDE);ShowWindow(cancel,SW_HIDE);ClientContextResize();Describe();}
void Settings(int i,POINT point) {
  if(!Current()||i<0||static_cast<size_t>(i)>=view.strategies.size())return;
  const auto s=view.strategies[i];const auto token=view.token;
  HMENU menu=CreatePopupMenu();
  for(size_t n=0;n<s.fields.size();n++){auto w=Wide(s.fields[n].title+" = "+s.fields[n].value);AppendMenuW(menu,MF_STRING,n+1,w.c_str());}
  if(s.fields.empty())AppendMenuA(menu,MF_GRAYED,1000,"No configurable settings");
  UINT choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,panel,NULL);DestroyMenu(menu);
  if(choice==0||choice>s.fields.size()||!Current()||token!=view.token)return;
  auto x=s.fields[choice-1];editing=true;editingStrategy=s.id;editingField=x.id;editToken=token;
  Text(detail,x.title+" ("+x.type+")\r\n"+x.description+"\r\nUse JSON for lists and true/false for switches. Changes are local policy, not immediate game actions.");
  Text(edit,x.value);ShowWindow(edit,SW_SHOW);ShowWindow(apply,SW_SHOW);ShowWindow(cancel,SW_SHOW);ClientContextResize();SetFocus(edit);
}
LRESULT CALLBACK ListProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
  if(msg==WM_LBUTTONUP){auto result=CallWindowProc(oldList,hwnd,msg,wp,lp);DWORD hit=static_cast<DWORD>(SendMessage(hwnd,LB_ITEMFROMPOINT,0,lp));
    int i=LOWORD(hit);if(!HIWORD(hit)&&Current()&&static_cast<size_t>(i)<view.strategies.size()){
      const auto s=view.strategies[i];Submit("toggle",s.id,"",s.enabled?"false":"true",view.token);}return result;}
  if(msg==WM_CONTEXTMENU){POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};int i=Selected();
    if(p.x==-1){RECT r;GetWindowRect(hwnd,&r);p.x=r.left+8;p.y=r.top+8;}
    else {POINT q=p;ScreenToClient(hwnd,&q);DWORD hit=static_cast<DWORD>(SendMessage(hwnd,LB_ITEMFROMPOINT,0,MAKELPARAM(q.x,q.y)));if(HIWORD(hit))return 0;i=LOWORD(hit);SendMessage(hwnd,LB_SETCURSEL,i,0);}
    Settings(i,p);return 0;}
  if(msg==WM_KEYDOWN&&wp==VK_SPACE){int i=Selected();if(Current()&&i>=0&&static_cast<size_t>(i)<view.strategies.size()){
    const auto s=view.strategies[i];Submit("toggle",s.id,"",s.enabled?"false":"true",view.token);}return 0;}
  return CallWindowProc(oldList,hwnd,msg,wp,lp);
}
void Tick() {
  const auto next=Binding();if(next!=binding){binding=next;view=View();sequence=0;op.clear();EndEdit();PaintList();}
  if(binding.empty())return;
  if(sequence && Now()>sequence+6000){op.clear();sequence=0;Text(detail,"Save expired; check the refreshed configuration before trying again.");}
  const auto path=root+"\\views\\"+std::to_string(GetCurrentProcessId())+".tsv";
  View candidate;if(Parse(Read(path),binding,Now(),candidate)){
    bool changed=candidate.token!=view.token||candidate.status!=view.status||candidate.plan!=view.plan;
    view=std::move(candidate);if(sequence&&view.ack>=sequence){op.clear();sequence=0;}
    if(changed)PaintList();
  }
  if(!Current()){EnableWindow(list,FALSE);Describe();}else EnableWindow(list,TRUE);
  std::ostringstream s;s<<"M59CTXREQ\t1\t"<<Now()<<'\t'<<binding<<'\t'
    <<(IsWindowVisible(panel)?(pane==STATS_DUM?"DUM":"PLAN"):"hidden")<<'\t'<<sequence<<'\t'
    <<requestToken<<'\t'<<op<<'\t'<<strategy<<'\t'<<field<<'\t'<<Encode(value)<<'\n';
  if(conditionIdentity==Identity())for(const auto& pair:conditions)s<<"C\t"<<pair.first<<'\t'<<Encode(pair.second.name)<<'\t'<<pair.second.level<<'\t'<<pair.second.at<<'\n';
  const auto* player=cinfo->player;
  // Native/BSP units (1024 per square), NOT protocol fine units (64).
  if(player->x>=0&&player->y>=0&&player->x<1024*1023&&player->y<1024*1023&&player->angle>=0&&player->angle<4096)
    s<<"P\t"<<player->x<<'\t'<<player->y<<'\t'<<player->angle<<'\n';
  if(statsPlayer==GetObjId(player->id)&&statIdentity==Identity())for(const auto& pair:numericStats){const auto& n=pair.second;
    s<<"N\t"<<n.group<<'\t'<<n.num<<'\t'<<Encode(n.name)<<'\t'<<n.value<<'\t'<<n.min<<'\t'<<n.max<<'\t'<<n.current<<'\t'<<n.at<<'\n';}
  s<<"END\n";Write(root+"\\native\\"+std::to_string(GetCurrentProcessId())+".tsv",s.str());
}
LRESULT CALLBACK PanelProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
  if(msg==WM_TIMER){Tick();return 0;}
  if(msg==WM_COMMAND){int id=LOWORD(wp);
    if(id==1&&HIWORD(wp)==LBN_SELCHANGE){Describe();return 0;}
    if(id==3){char input[4097]={};GetWindowTextA(edit,input,sizeof(input));auto token=editToken,s=editingStrategy,f=editingField;EndEdit();Submit("setting",s,f,input,token);return 0;}
    if(id==4){EndEdit();return 0;}}
  return DefWindowProc(hwnd,msg,wp,lp);
}
}
std::string ClientContextBinding(){return Binding();}
bool ClientContextEnabled() {
  if(!configured){configured=true;char path[MAX_PATH]={};auto n=GetEnvironmentVariableA("M59_CLIENT_CONTEXT_DIR",path,sizeof(path));
    if(n>0&&n<sizeof(path)&&n<MAX_PATH-64)root=path;
  }return !root.empty();
}
bool ClientContextGroup(StatGroup group){return group==STATS_DUM||group==STATS_PLAN;}
void ClientContextCreate(HWND parent) {
  if(!ClientContextEnabled())return;
  WNDCLASSA wc={};wc.lpfnWndProc=PanelProc;wc.hInstance=hInst;wc.lpszClassName="M59NativeContext";wc.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);wc.hCursor=LoadCursor(NULL,IDC_ARROW);RegisterClassA(&wc);
  panel=CreateWindowExA(0,wc.lpszClassName,"",WS_CHILD|WS_CLIPCHILDREN,0,0,1,1,parent,NULL,hInst,NULL);
  list=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_TABSTOP,0,0,1,1,panel,(HMENU)1,hInst,NULL);
  oldList=(WNDPROC)SetWindowLongPtr(list,GWLP_WNDPROC,(LONG_PTR)ListProc);
  detail=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0,0,1,1,panel,(HMENU)2,hInst,NULL);
  edit=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",WS_CHILD|WS_TABSTOP|ES_AUTOHSCROLL,0,0,1,1,panel,(HMENU)5,hInst,NULL);
  SendMessage(edit,EM_SETLIMITTEXT,4096,0);
  apply=CreateWindowA("BUTTON","Apply",WS_CHILD|WS_TABSTOP,0,0,1,1,panel,(HMENU)3,hInst,NULL);
  cancel=CreateWindowA("BUTTON","Cancel",WS_CHILD|WS_TABSTOP,0,0,1,1,panel,(HMENU)4,hInst,NULL);
  for(HWND w:{list,detail,edit,apply,cancel})SendMessage(w,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);
  CreateDirectoryA(root.c_str(),NULL);CreateDirectoryA((root+"\\native").c_str(),NULL);
  SetTimer(panel,1,1000,NULL);Describe();
  // The PLAN tab is its own pane now (planpane.c): fleet intents from the harness plan desk.
  PlanPaneCreate(parent,root);
}
void ClientContextDestroy(){PlanPaneDestroy();if(panel)DestroyWindow(panel);panel=list=detail=edit=apply=cancel=NULL;view=View();binding.clear();conditions.clear();carried.clear();numericStats.clear();conditionPlayer=statsPlayer=0;sequence=0;}
void ClientContextResize(){if(!panel)return;RECT r;GetClientRect(GetParent(panel),&r);int top=StatsGetButtonBorder(),w=std::max(1,(int)r.right),h=std::max(1,(int)r.bottom-top);
  PlanPaneResize(top,w,h);
  MoveWindow(panel,0,top,w,h,TRUE);int lh=pane==STATS_DUM?std::max(32,h*45/100):0;ShowWindow(list,lh?SW_SHOW:SW_HIDE);MoveWindow(list,0,0,w,lh,TRUE);
  int eh=editing?58:0;MoveWindow(detail,0,lh,w,std::max(1,h-lh-eh),TRUE);
  MoveWindow(edit,0,h-58,w,26,TRUE);MoveWindow(apply,0,h-28,w/2,28,TRUE);MoveWindow(cancel,w/2,h-28,w-w/2,28,TRUE);
}
void ClientContextShow(StatGroup group){pane=group;EndEdit();ClientContextResize();
  if(group==STATS_PLAN){ShowWindow(panel,SW_HIDE);PlanPaneShow();return;}
  PlanPaneHide();ShowWindow(panel,SW_SHOW);Tick();}
void ClientContextHide(){PlanPaneHide();if(panel){EndEdit();ShowWindow(panel,SW_HIDE);}}
void ClientContextInventory(const std::vector<M59InventoryIntent::Item>& items) {
  unsigned int player=cinfo->player?GetObjId(cinfo->player->id):0;auto identity=Identity();
  if(conditionPlayer!=player||conditionIdentity!=identity){conditions.clear();conditionPlayer=player;conditionIdentity=identity;}
  PlanPaneInventory(items);
  carried.clear();for(const auto& item:items)carried[item.id]=item.name;
  for(auto i=conditions.begin();i!=conditions.end();)if(!carried.count(i->first)||carried[i->first]!=i->second.name)i=conditions.erase(i);else ++i;
}
void ClientContextStat(StatGroup group,const Statistic* s) {
  if(s&&s->type==STATS_LIST)PlanPaneNoteIcon(LookupNameRsc(s->name_res),s->list.icon);
  if(!ClientContextEnabled()||!cinfo->player||!s||group<1||group>4||s->type!=STATS_NUMERIC||s->numeric.tag!=STAT_INT)return;
  unsigned int player=GetObjId(cinfo->player->id);auto identity=Identity();
  if(statsPlayer!=player||statIdentity!=identity){numericStats.clear();statsPlayer=player;statIdentity=identity;}
  unsigned int key=static_cast<unsigned int>(group)*256+s->num;
  if(numericStats.size()>=256&&!numericStats.count(key))return;
  numericStats[key]={static_cast<unsigned int>(group),s->num,LookupNameRsc(s->name_res),static_cast<int>(s->numeric.value),
    s->numeric.min,s->numeric.max,s->numeric.current_max,Now()};
}
// Called by the debug executable AFTER decoding an already-received BP_LOOK.
// No packet parsing, polling/inspection request, or core/module ABI extension.
extern "C" void __cdecl M59ObservedDescription(unsigned int id,const char* name,const char* text) {
  if(!ClientContextEnabled()||conditionIdentity!=Identity()||!text||!name||!carried.count(id)||carried[id]!=name)return;
  int level=M59Context::Condition(text);if(level<0)conditions.erase(id);else conditions[id]={name,level,Now()};
  InventoryRedraw();
}
void ClientContextConditionDraw(HDC dc,const RECT& area,unsigned int id,const char* name) {
  if(!ClientContextEnabled()||!cinfo->player||conditionPlayer!=GetObjId(cinfo->player->id)||conditionIdentity!=Identity())return;
  auto found=conditions.find(id);if(found==conditions.end()||found->second.name!=name)return;
  const auto& c=found->second;COLORREF colors[]={RGB(220,55,55),RGB(220,55,55),RGB(240,140,35),RGB(240,215,50),RGB(55,205,90)};
  RECT r={area.left+3,area.top+3,area.left+12,area.top+12};HBRUSH brush=CreateSolidBrush(colors[c.level]);
  // Fresh means "recently inspected", never a guarantee against subsequent wear.
  if(Now()<=c.at+60000)FillRect(dc,&r,brush);else FrameRect(dc,&r,brush);DeleteObject(brush);
}
