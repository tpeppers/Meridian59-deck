// Offline test of the PLAN pane: hidden Win32 windows, a fake plan desk file, no game session.
//   cl /EHsc /std:c++20 planpane_test.cpp user32.lib gdi32.lib comctl32.lib && planpane_test
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
typedef unsigned int ID;
enum StatGroup : unsigned char { STATS_INVENTORY=5,STATS_DUM=6,STATS_PLAN=7 };
struct AREA { int x,y,cx,cy; };
struct Player {unsigned int id=17;};
struct Info {Player* player;};
Player fixturePlayer;Info fixtureInfo{&fixturePlayer};Info* cinfo=&fixtureInfo;
HINSTANCE hInst=GetModuleHandle(NULL);
enum { FONT_STATS=1 };
HFONT GetFont(int){return (HFONT)GetStockObject(DEFAULT_GUI_FONT);}
int GetFontHeight(HFONT){return 14;}
int iconsDrawn=0;
void DrawObjectIcon(HDC,ID,int,bool,AREA*,HBRUSH,int,int,bool){iconsDrawn++;}
std::vector<std::string> said;
void GameMessage(char* m){said.push_back(m);}
StatGroup activeGroup=STATS_INVENTORY;
void StatsSetActiveGroup(StatGroup g){activeGroup=g;}
enum { A_PICKUP=1,A_DROP=3,A_PUT=4,A_USE=8,A_UNUSE=9,A_OFFER=12,A_ATTACK=13,A_GO=15,A_BUY=17,A_DEPOSIT=18,A_WITHDRAW=19,A_APPLY=20,
       A_FORWARD=31,A_SLIDERIGHTBACKWARDFAST=46,A_TURNLEFT=47,A_TURNFASTRIGHT=58,A_TOGGLEUSE=63,A_ATTACKCLOSEST=76,A_TEXTCOMMAND=77,
       A_ACTIVATE=82,A_ACTIVATEMOUSE=83 };
#define IsMoveAction(a) ((a)>=A_FORWARD&&(a)<=A_SLIDERIGHTBACKWARDFAST)
#define IsTurnAction(a) ((a)>=A_TURNLEFT&&(a)<=A_TURNFASTRIGHT)
#define IsAttackAction(a) ((a)==A_ATTACK||(a)==A_ATTACKCLOSEST)
std::string ClientContextBinding(){return std::to_string(GetCurrentProcessId())+"\t127.0.0.1%3A5959\tt1\tKermit\t17\t91\t92\t93";}
#define M59_CONTEXT_UI_TEST
#include "planpane.c"

static std::string slurp(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return std::string((std::istreambuf_iterator<char>(f)),{});}
static void put(const std::filesystem::path& p,const std::string& s){std::ofstream f(p,std::ios::binary);f<<s;}

int main(){
  InitCommonControls();
  char temp[MAX_PATH]={};GetTempPathA(MAX_PATH,temp);
  auto dir=std::filesystem::path(temp)/("m59-plan-test-"+std::to_string(GetCurrentProcessId()));
  std::filesystem::create_directories(dir/"plan"/"out");
  HWND parent=CreateWindowA("STATIC","fixture",WS_POPUP,0,0,434,760,NULL,NULL,hInst,NULL);
  PlanPaneCreate(parent,dir.string());PlanPaneResize(24,434,500);
  const auto pid=std::to_string(GetCurrentProcessId());
  auto out=dir/"plan"/"out"/(pid+".tsv"),in=dir/"plan"/"in"/(pid+".tsv");
  auto desk=[&](const std::string& control,const std::string& ack,const std::string& extra){
    put(out,"M59PLAN\t1\t"+std::to_string(Now())+"\t"+pid+"\t"+ack+"\t"+control+"\tt1%20(Kermit)\n"
      "E\tcontrol.keeper\tControl\tkeeper\tRelinquish%20control%20to%20Keeper\t-\tYou%20spectate.%0ALogging%20off%20releases.\n"
      "E\tcourier.request\tCourier\televerberry\tRequest%20a%20delivery\tr\tStanding%20request\n"
      "A\tcourier.request\titem\tchoice\tItem\televerberry\n"
      "O\tcourier.request\titem\televerberry\televerberry%20%E2%80%94%20Joguer\n"
      "O\tcourier.request\titem\torc%20tooth\torc%20tooth\n"
      "A\tcourier.request\tcount\tcount\tTop%20up%20to\t20\n"
      "E\tcourier.settings\tCourier\tscroll\tAutomated%20Courier\tc\tHow%20the%20courier%20trades%20with%20you\n"
      "A\tcourier.settings\tmode\tchoice\tHandling\tmanual\n"
      "O\tcourier.settings\tmode\tauto\tFull%20Auto\n"
      "O\tcourier.settings\tmode\tmanual\tManual\n"
      "A\tcourier.settings\tinstructions\tboolean\tWith%20instruction%20text\ttrue\n"
      "E\tfs.line\tFleetScript\tscroll\tRun%20a%20FleetScript%20line\t-\tAs%20typed\n"
      "A\tfs.line\tline\ttext\tLine\t\n"+extra+"END\n");};

  // 1. Before the desk answers: nothing queues, the lock is off.
  Tick();
  assert(std::filesystem::exists(in));
  auto req=slurp(in);
  assert(req.rfind("M59PLANREQ\t2\t",0)==0&&req.find("\t"+pid+"\t127.0.0.1%3A5959\tt1\tKermit\t17\t0\n")!=std::string::npos);
  assert(!PlanPaneLocked()&&std::string(PlanPaneButtonLabel(nullptr))=="MANAGE");
  CommandPlan((char*)"");assert(!said.empty()&&said.back().find("not answering")!=std::string::npos);

  // 2. The desk's rows become a grouped, owner-drawn list; the newline in a description becomes CRLF.
  PlanPaneInventory({{11,"diamond",40,false,0,0}});
  PlanPaneNoteIcon("elderberry",4242);
  desk("none","0","");Tick();PlanPaneShow();
  assert(view.entries.size()==4&&view.entries[1].running&&view.entries[1].args.size()==2&&view.entries[1].args[0].options.size()==2);
  assert(view.entries[0].describe=="You spectate.\r\nLogging off releases.");
  assert(rows.size()==7&&rows[0].header&&rows[0].group=="Control"&&!rows[1].header);
  assert(IconFor(view.entries[1])==0);              // "everberry" is not a known icon...
  view.entries[1].icon="elderberry";assert(IconFor(view.entries[1])==4242);   // ..."elderberry" is, from a spell or the pack
  RedrawWindow(list,NULL,NULL,RDW_INVALIDATE|RDW_UPDATENOW);
  assert(slurp(in).find("I\t11\tdiamond\t40\n")!=std::string::npos);

  // 3. Double-clicking a row without arguments queues it at once.
  SendMessage(list,LB_SETCURSEL,1,0);assert(SelectedEntry()==0);
  Activate(SelectedEntry());Tick();
  req=slurp(in);assert(req.find("R\t")!=std::string::npos&&req.find("\tcontrol.keeper\t\n")!=std::string::npos);

  // 4. A row with arguments opens them; the choice sends the VALUE, not the label.
  OpenArgs(1);assert(editing==1&&Top()&&IsWindowVisible(Top()->run));
  SendMessage(Top()->inputs[0],CB_SETCURSEL,1,0);SetWindowTextA(Top()->inputs[1],"30");SubmitArgs();
  assert(editing<0&&pending.size()==2&&pending.back().entry=="courier.request"&&pending.back().args=="item=orc%20tooth&count=30");

  // 5. The desk's ack clears what it took and its answer reaches the chat once.
  auto acked=std::to_string(pending.back().seq);
  desk("none",acked,"M\t"+acked+"\t1\tStanding%20request%20set\n");Tick();Tick();
  assert(pending.empty());
  int answers=0;for(auto& s:said)if(s=="MANAGE: Standing request set")answers++;assert(answers==1);

  // 6. Keeper holds the controls: movement, turning, combat and items are refused; chat is not.
  desk("keeper",acked,"");Tick();
  assert(PlanPaneLocked());bool held=false;assert(std::string(PlanPaneButtonLabel(&held))=="KEEPER"&&held);
  assert(PlanPaneVetoes(A_FORWARD)&&PlanPaneVetoes(A_TURNLEFT)&&PlanPaneVetoes(A_ATTACK)&&PlanPaneVetoes(A_PICKUP)&&PlanPaneVetoes(A_BUY));
  assert(!PlanPaneVetoes(A_TEXTCOMMAND));
  bool told=false;for(auto& s:said)if(s.find("Keeper has the controls. You are spectating")!=std::string::npos)told=true;assert(told);

  // 7. A desk that stops answering cannot keep anyone locked out locally.
  put(out,slurp(out).replace(slurp(out).find("\t1\t")+3,13,"1000000000000"));Tick();
  assert(!PlanPaneLocked()&&!PlanPaneVetoes(A_FORWARD));

  // 8. Typed commands: a unique prefix runs with key=value args; `fs` is the REPL line.
  desk("none",acked,"");Tick();
  CommandPlan((char*)"courier.req item=orc%20tooth count=5");
  assert(pending.size()==1&&pending.back().entry=="courier.request"&&pending.back().args=="item=orc%20tooth&count=5");
  CommandFleetScript((char*)" zheng-he-lap agents=hk2");
  assert(pending.back().entry=="fs.line"&&pending.back().args=="line=zheng-he-lap%20agents%3Dhk2");
  CommandPlan((char*)"nope");assert(said.back().find("no row")!=std::string::npos);
  CommandPlan((char*)"courier");assert(said.back().find("more than one row")!=std::string::npos);
  CommandPlan((char*)"courier.req");assert(activeGroup==STATS_PLAN&&editing==1&&dialog&&IsWindowVisible(dialog));CloseArgs();
  assert(!dialog&&editing<0);

  // 8b. A settings row opens as a form: dropdown + checkbox, a Save button, and the values it sends.
  OpenArgs(2);assert(view.entries[2].configure&&dialog);
  {char cap[16]={};GetWindowTextA(Top()->run,cap,sizeof(cap));assert(std::string(cap)=="Save");}
  assert(SendMessage(Top()->inputs[1],BM_GETCHECK,0,0)==BST_CHECKED);           // default from the desk: true
  SendMessage(Top()->inputs[0],CB_SETCURSEL,0,0);SendMessage(Top()->inputs[1],BM_SETCHECK,BST_UNCHECKED,0);SubmitArgs();
  assert(pending.back().entry=="courier.settings"&&pending.back().args=="mode=auto&instructions=false");


  // 8d. Protocol 2: a multi-select of agents, a dropdown that follows another argument, a slider whose
  //     range follows another, an editable combo, and a nested FleetScript configured in its own form.
  desk("none",acked,"");Tick();
  {std::string body=slurp(out);body=body.replace(body.find("M59PLAN\t1\t"),10,"M59PLAN\t2\t");
   std::string v2="E\tfs.buy\tFleetScript%3A%20public\tscroll\tbuy\t-\tBuy%20anything\n"
     "A\tfs.buy\tagents\tagents\tagents\thk2\twho%20buys\n"
     "O\tfs.buy\tagents\thk2\thk2%20%C2%B7%20Zheng%20He\t\n"
     "O\tfs.buy\tagents\tt9\tt9\t\n"
     "A\tfs.buy\tseller\tchoice\tseller\tA\t\n"
     "O\tfs.buy\tseller\tA\tShop%20A\t\nO\tfs.buy\tseller\tB\tShop%20B\t\n"
     "A\tfs.buy\titem\tchoice\titem\t\t\n"
     "O\tfs.buy\titem\tx\tX\tseller%3DA\nO\tfs.buy\titem\ty\tY\tseller%3DB\nO\tfs.buy\titem\tz\tZ\tseller%3DB\n"
     "A\tfs.buy\twithdraw\tnumber\twithdraw\t0\tshillings\n"
     "N\tfs.buy\twithdraw\t0\t7000\tbank%3D376\nN\tfs.buy\twithdraw\t0\t0\tbank%3D0\n"
     "A\tfs.buy\tbank\tchoice\tbank\t0\t\nO\tfs.buy\tbank\t0\tnone\t\nO\tfs.buy\tbank\t376\tJasper\t\n"
     "A\tfs.buy\tback\tcombo\tback\t\t\nO\tfs.buy\tback\t38\t38%20%E2%80%94%20Tos\t\n"
     "A\tfs.buy\ttask\ttask\tthen\t\t\nO\tfs.buy\ttask\t\t(none)\t\nO\tfs.buy\ttask\tfs.come-home\tcome-home\t\n"
     "E\tfs.come-home\tFleetScript%3A%20public\tscroll\tcome-home\t-\tWalk%20home\n"
     "A\tfs.come-home\tagents\tagents\tagents\t\t\nO\tfs.come-home\tagents\thk2\thk2\t\n"
     "A\tfs.come-home\thome\tcombo\thome\t\t\nO\tfs.come-home\thome\t52\t52%20%E2%80%94%20Tos%20inn\t\n";
   body.insert(body.rfind("END\n"),v2);put(out,body);}
  Tick();
  int buyAt=EntryById("fs.buy");assert(buyAt>=0&&view.entries[buyAt].args.size()==7&&view.entries[buyAt].args[0].help=="who buys");
  assert(view.entries[buyAt].args[3].bounds.size()==2&&view.entries[buyAt].args[2].options[0].when=="seller=A");
  OpenArgs(buyAt);Form* f=Top();assert(f&&!f->compose);
  assert(SendMessage(f->inputs[0],LB_GETSEL,0,0)>0&&SendMessage(f->inputs[0],LB_GETSEL,1,0)==0);       // default hk2 preselected
  SendMessage(f->inputs[0],LB_SETSEL,TRUE,1);assert(ValueOf(*f,0)=="hk2,t9");
  assert(SendMessage(f->inputs[2],CB_GETCOUNT,0,0)==1&&ValueOf(*f,2)=="x");                            // seller A's shelf
  SendMessage(f->inputs[1],CB_SETCURSEL,1,0);Refresh(*f,1);
  assert(SendMessage(f->inputs[2],CB_GETCOUNT,0,0)==2&&ValueOf(*f,2)=="y");                            // seller B's shelf
  assert(!IsWindowVisible(f->extras[3])||BoundOf(*f,3)->max==0);                                     // no bank: 0-0
  SendMessage(f->inputs[4],CB_SETCURSEL,1,0);Refresh(*f,4);
  assert(BoundOf(*f,3)&&BoundOf(*f,3)->max==7000&&SendMessage(f->extras[3],TBM_GETRANGEMAX,0,0)==7000);
  SendMessage(f->extras[3],TBM_SETPOS,TRUE,2500);SendMessage(f->dlg,WM_HSCROLL,0,(LPARAM)f->extras[3]);assert(ValueOf(*f,3)=="2500");
  SendMessage(f->inputs[5],CB_SETCURSEL,0,0);SetWindowTextW(f->inputs[5],L"38 \x2014 Tos");assert(ValueOf(*f,5)=="38");   // a picked label is its value
  SetWindowTextA(f->inputs[5],"599");assert(ValueOf(*f,5)=="599");                                     // free text still goes
  SendMessage(f->inputs[6],CB_SETCURSEL,1,0);assert(ValueOf(*f,6)=="fs.come-home");
  OpenNested(*f,6);Form* c=Top();assert(c&&c!=f&&c->compose&&!c->inputs[0]&&!IsWindowEnabled(f->dlg));  // nested: no agents, parent disabled
  SetWindowTextA(c->inputs[1],"52");SubmitArgs();
  assert(Top()==f&&IsWindowEnabled(f->dlg)&&ValueOf(*f,6)=="fs.come-home?home=52");
  OpenNested(*f,6);assert(ValueOf(*Top(),1)=="52");CloseForm(Top());assert(Top()==f&&ValueOf(*f,6)=="fs.come-home?home=52");  // reopened as configured
  SubmitArgs();
  assert(!Top()&&!dialog&&pending.back().entry=="fs.buy"&&
    pending.back().args=="agents=hk2%2Ct9&seller=B&item=y&withdraw=2500&bank=376&back=599&task=fs.come-home%3Fhome%3D52");
  // Closing the outer form closes a nested one with it.
  OpenArgs(buyAt);SendMessage(Top()->inputs[6],CB_SETCURSEL,1,0);OpenNested(*Top(),6);assert(forms.size()==2);CloseForm(forms.front().get());assert(forms.empty()&&editing<0);
  // A protocol-1 reply (an older desk) still parses: its rows have no help, no condition, no range.
  desk("none",acked,"");Tick();assert(EntryById("fs.buy")<0&&view.entries.size()==4);

  // 8c. The players in the room go to the desk by name: that is how Full Auto knows the courier is here.
  testRoom={{4242,"Zheng He"}};Tick();assert(slurp(in).find("W\t4242\tZheng%20He\n")!=std::string::npos);

  // 9. Someone else's file, or a torn one, is ignored rather than half-read.
  PlanView before=view;
  put(out,"M59PLAN\t1\t"+std::to_string(Now())+"\t999999\t0\tnone\tx\nEND\n");Tick();
  assert(view.entries.size()==before.entries.size());
  put(out,"M59PLAN\t1\t"+std::to_string(Now())+"\t"+pid+"\t0\tnone\tx\nE\tbroken\n");Tick();
  assert(view.entries.size()==before.entries.size());

  PlanPaneDestroy();DestroyWindow(parent);std::filesystem::remove_all(dir);
  std::cout<<"PASS plan pane: protocol, grouped rows, icons by name, run/args/ack, typed commands, keeper lock and its expiry\n";
}
