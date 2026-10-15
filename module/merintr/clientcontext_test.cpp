// Offline tests instantiate ONLY self-owned hidden Win32 windows. No game session.
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <cassert>
#include <iostream>
#include <filesystem>
#include <string>
enum StatGroup : unsigned char { STATS_INVENTORY=5,STATS_DUM=6,STATS_PLAN=7 };
struct Player {unsigned int id=17,name_res=1,room_id=91,room_res=92,room_security=93;int x=61952,y=30208,angle=200;};
enum {STATS_NUMERIC=1,STATS_LIST=2,STAT_INT=1};
struct Statistic {unsigned int num,name_res;int type;struct{int tag;unsigned int value;int min,max,current_max;}numeric;struct{unsigned int icon;}list;};
struct Config {struct {const char* hostname="127.0.0.1";int sockport=15959;}comm;const char* username="fixture";};
struct Info {Player* player;Config* config;};
Player fixturePlayer;Config fixtureConfig;Info fixtureInfo{&fixturePlayer,&fixtureConfig};Info* cinfo=&fixtureInfo;
HINSTANCE hInst=GetModuleHandle(NULL);
unsigned int GetObjId(unsigned int id){return id;}
const char* LookupNameRsc(unsigned int){return "Test character";}
int StatsGetButtonBorder(){return 24;}
void InventoryRedraw(){}
#define M59_CONTEXT_UI_TEST
typedef unsigned int ID;
#include "clientcontext.c"
// The PLAN pane is its own file with its own test (planpane_test.cpp); here it is inert.
void PlanPaneCreate(HWND,const std::string&){}
void PlanPaneDestroy(){}
void PlanPaneShow(){}
void PlanPaneHide(){}
void PlanPaneResize(int,int,int){}
void PlanPaneInventory(const std::vector<M59InventoryIntent::Item>&){}
void PlanPaneNoteIcon(const char*,ID){}
int main(){
  char temp[MAX_PATH]={};GetTempPathA(MAX_PATH,temp);auto dir=std::filesystem::path(temp)/("m59-context-test-"+std::to_string(GetCurrentProcessId()));
  assert(!std::filesystem::exists(dir));std::filesystem::create_directory(dir);
  SetEnvironmentVariableA("M59_CLIENT_CONTEXT_DIR",dir.string().c_str());
  HWND parent=CreateWindowA("STATIC","fixture",WS_POPUP,0,0,434,760,NULL,NULL,hInst,NULL);
  ClientContextCreate(parent);ClientContextShow(STATS_DUM);
  auto header=std::string("M59CTX\t1\t")+std::to_string(Now())+'\t'+Binding()+'\t'+std::string(48,'a')+"\t0\thuman\tReady\n";
  auto data=header+"S\tfixture\tTest%20behavior\t1\tDescription\nF\tfixture\tlimit\tLimit\tinteger\t12\tAllowed%20number\nT\tPlan%20fixture\nEND\n";
  View parsed;assert(Parse(data,Binding(),Now(),parsed)&&parsed.strategies.size()==1&&parsed.strategies[0].fields.size()==1);
  assert(!Parse(data,Binding()+"x",Now(),parsed));assert(!Parse(data,Binding(),Now()+7000,parsed));
  assert(!Parse(data.substr(0,data.size()-1),Binding(),Now(),parsed));
  auto bad=data;bad.replace(bad.find("Test%20behavior"),15,"Test%00behavior");assert(!Parse(bad,Binding(),Now(),parsed));
  std::filesystem::create_directory(dir/"views");Write((dir/"views"/(std::to_string(GetCurrentProcessId())+".tsv")).string(),data);Tick();assert(Current());
  for(int w:{130,260,434,760})for(int h:{180,480,760}){
    SetWindowPos(parent,NULL,0,0,w,h,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);ClientContextResize();RECT p,r;
    GetClientRect(parent,&p);GetWindowRect(panel,&r);MapWindowPoints(NULL,parent,(POINT*)&r,2);
    assert(r.left==0&&r.top==24&&r.right==p.right&&r.bottom==p.bottom);
    for(HWND child:{list,detail}){RECT c;GetWindowRect(child,&c);MapWindowPoints(NULL,panel,(POINT*)&c,2);GetClientRect(panel,&p);assert(c.left>=0&&c.top>=0&&c.right<=p.right&&c.bottom<=p.bottom);}
    ClientContextShow(STATS_PLAN);assert(!(GetWindowLong(list,GWL_STYLE)&WS_VISIBLE));ClientContextShow(STATS_DUM);
  }
  const auto goodToken=view.token;Submit("toggle","fixture","","false",goodToken);assert(op=="toggle"&&sequence>0);auto first=sequence;
  ClientContextShow(STATS_PLAN);const auto originalAt=view.at;view.at=Now()-7000;Describe();
  wchar_t cachedText[1024]={};GetWindowTextW(detail,cachedText,1024);
  assert(std::wstring(cachedText).find(L"Plan fixture")!=std::wstring::npos);
  assert(!Current());view.at=originalAt;ClientContextShow(STATS_DUM);
  Submit("toggle","fixture","","true",goodToken);assert(sequence==first&&value=="false");
  ClientContextInventory({{99,"sword",1,true,1,0}});M59ObservedDescription(99,"sword","It is in flawless condition.");assert(conditions.at(99).level==4);
  M59ObservedDescription(99,"wrong","slightly tarnished");assert(conditions.at(99).level==4);
  assert(Condition("slightly tarnished")==3&&Condition("notched and stained")==2&&Condition("may not last much longer")==1&&Condition("is useless.")==0&&Condition("unknown")==-1);
  ClientContextInventory({});assert(conditions.empty());
  Statistic stat={1,1,STATS_NUMERIC,{STAT_INT,90,0,100,100}};ClientContextStat((StatGroup)2,&stat);assert(numericStats.size()==1);
  ClientContextStat(STATS_DUM,&stat);assert(numericStats.size()==1);
  fixturePlayer.id=18;Tick();assert(!Current()&&op.empty()&&view.strategies.empty());
  ClientContextDestroy();DestroyWindow(parent);
  // Exact test-owned temporary directory only.
  std::filesystem::remove_all(dir);
  std::cout<<"PASS native context: binding, expiry, bounded parser, hidden live resize, tabs, pending commands, condition cache\n";
}
