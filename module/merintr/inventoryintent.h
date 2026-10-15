// Local harness inventory annotations. No game packets, actions, or credentials.
#ifndef M59_INVENTORY_INTENT_H
#define M59_INVENTORY_INTENT_H
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <cstdint>
#include <algorithm>
namespace M59InventoryIntent {
struct Item { unsigned int id; std::string name; unsigned int amount; bool equipped; unsigned int name_res, flags; };
struct Mark { std::string name, state; };
static std::map<unsigned int,Mark> marks;
static unsigned int mark_player=0;
static std::string mark_account, mark_identity;
static std::uint64_t mark_time=0;
static std::string root;
static bool configured=false;
static std::uint64_t Now() {
  FILETIME ft; GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER v;
  v.LowPart=ft.dwLowDateTime;v.HighPart=ft.dwHighDateTime;
  return (v.QuadPart-116444736000000000ULL)/10000ULL;
}
static std::string Encode(const char* input) {
  if(!input)return "";
  int count=MultiByteToWideChar(CP_ACP,0,input,-1,NULL,0);
  if(count<1||count>1024)return "";
  std::vector<wchar_t> wide(count);MultiByteToWideChar(CP_ACP,0,input,-1,wide.data(),count);
  int bytes=WideCharToMultiByte(CP_UTF8,0,wide.data(),-1,NULL,0,NULL,NULL);
  if(bytes<1||bytes>4096)return "";
  std::vector<char> utf8(bytes);WideCharToMultiByte(CP_UTF8,0,wide.data(),-1,utf8.data(),bytes,NULL,NULL);
  std::string out;const char* hex="0123456789ABCDEF";
  for(int i=0;i<bytes-1;i++){unsigned char c=static_cast<unsigned char>(utf8[i]);
    if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out+=c;
    else {out+='%';out+=hex[c>>4];out+=hex[c&15];}}
  return out;
}
static bool Enabled() {
  if(!configured){configured=true;char path[MAX_PATH]={};
    DWORD n=GetEnvironmentVariableA("M59_INVENTORY_INTENT_DIR",path,sizeof(path));
    if(n>0&&n<sizeof(path))root=path;
    else {n=GetEnvironmentVariableA("M59_OVERLAY_DIR",path,sizeof(path));
      if(n>0&&n<sizeof(path)){root=path;auto at=root.find_last_of("/\\");
        if(at!=std::string::npos)root=root.substr(0,at)+"\\inventory-intent";else root.clear();}}
    if(root.size()>MAX_PATH-64)root.clear();
  } return !root.empty();
}
static bool Number(const std::string& s,std::uint64_t& n) {
  if(s.empty()||s.size()>16)return false;n=0;
  for(unsigned char c:s){if(c<'0'||c>'9')return false;n=n*10+c-'0';}return true;
}
static std::vector<std::string> Split(const std::string& s) {
  std::vector<std::string> f;std::size_t begin=0;
  for(;;){auto end=s.find('\t',begin);f.push_back(s.substr(begin,end-begin));if(end==std::string::npos)break;begin=end+1;}return f;
}
static bool Parse(const std::string& text,const std::string& account,unsigned int player,std::uint64_t now,
                  std::map<unsigned int,Mark>& output,std::uint64_t& stamp) {
  if(text.size()>262144||text.size()<5||text.substr(text.size()-5)!="\nEND\n")return false;
  std::istringstream stream(text);std::string line;std::getline(stream,line);auto h=Split(line);
  std::uint64_t at,pid,id;
  if(h.size()!=6||h[0]!="M59SELL"||h[1]!="1"||!Number(h[2],at)||at>now+1000||now>at+6000||
    !Number(h[3],pid)||pid!=GetCurrentProcessId()||h[4]!=account||!Number(h[5],id)||id!=player)return false;
  std::map<unsigned int,Mark> next;bool end=false;
  while(std::getline(stream,line)){if(line=="END"){end=true;break;}auto f=Split(line);
    if(f.size()!=4||f[0]!="I"||!Number(f[1],id)||id==0||id>0x0fffffff||f[2].empty()||f[2].size()>3072||
      (f[3]!="sell"&&f[3]!="keep"&&f[3]!="blocked")||next.size()>=2048||
      !next.emplace(static_cast<unsigned int>(id),Mark{f[2],f[3]}).second)return false;
  }
  if(!end||std::getline(stream,line))return false;output=std::move(next);stamp=at;return true;
}
static void Poll(const char* account,unsigned int player) {
  if(!Enabled())return;
  const auto now=Now();const std::string path=root+"\\views\\"+std::to_string(GetCurrentProcessId())+".tsv";
  HANDLE h=CreateFileA(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
  if(h!=INVALID_HANDLE_VALUE){LARGE_INTEGER size;DWORD got=0;
    if(GetFileSizeEx(h,&size)&&size.QuadPart>0&&size.QuadPart<=262144){std::string text(static_cast<size_t>(size.QuadPart),'\0');
      if(ReadFile(h,&text[0],static_cast<DWORD>(text.size()),&got,NULL)&&got==text.size()&&
        Parse(text,Encode(account),player,now,marks,mark_time))mark_player=player;}
    CloseHandle(h);
  }
  // A lost annotation heartbeat is not an instruction to erase the operator's
  // labels. They are display-only; a complete newer list still removes marks.
  const auto account_key=Encode(account);
  if(mark_player!=player||(!mark_account.empty()&&mark_account!=account_key)){marks.clear();mark_player=0;}
  mark_account=account_key;
}
static void Publish(const char* host,int port,const char* account,const char* character,unsigned int player,
                    unsigned int room,unsigned int resource,unsigned int security,const std::vector<Item>& items) {
  if(!Enabled()||!player||items.size()>2048)return;
  char server[192]={};DWORD len=GetEnvironmentVariableA("M59_INVENTORY_SERVER",server,sizeof(server));
  if(len==0||len>=sizeof(server))_snprintf_s(server,sizeof(server),_TRUNCATE,"%s:%d",host,port);
  const std::string identity=std::string(server)+"|"+Encode(account)+"|"+Encode(character)+"|"+std::to_string(player);
  if(mark_identity!=identity){marks.clear();mark_player=0;mark_identity=identity;}
  for(auto i=marks.begin();i!=marks.end();){
    const auto found=std::find_if(items.begin(),items.end(),[&](const Item& item){return item.id==i->first&&Encode(item.name.c_str())==i->second.name;});
    if(found==items.end())i=marks.erase(i);else ++i;
  }
  std::ostringstream text;text<<"M59PACK\t1\t"<<Now()<<'\t'<<GetCurrentProcessId()<<'\t'<<Encode(server)<<'\t'
    <<Encode(account)<<'\t'<<Encode(character)<<'\t'<<player<<'\t'<<room<<'\t'<<resource<<'\t'<<security<<'\n';
  for(const auto& item:items)text<<"I\t"<<item.id<<'\t'<<Encode(item.name.c_str())<<'\t'<<item.amount<<'\t'
    <<(item.equipped?1:0)<<'\t'<<item.name_res<<'\t'<<item.flags<<'\n';
  text<<"END\n";const auto bytes=text.str();if(bytes.size()>262144)return;
  CreateDirectoryA(root.c_str(),NULL);CreateDirectoryA((root+"\\clients").c_str(),NULL);
  const auto path=root+"\\clients\\"+std::to_string(GetCurrentProcessId())+".tsv",tmp=path+".tmp";
  HANDLE file=CreateFileA(tmp.c_str(),GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
  if(file==INVALID_HANDLE_VALUE)return;DWORD written=0;
  bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,NULL)&&written==bytes.size();
  CloseHandle(file);if(ok)MoveFileExA(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING);
}
static void Draw(HDC hdc,const RECT& area,unsigned int player,unsigned int id,const char* name) {
  if(mark_player!=player)return;
  auto at=marks.find(id);if(at==marks.end()||at->second.name!=Encode(name))return;
  const auto& state=at->second.state;const COLORREF color=Now()>mark_time+6000?RGB(155,160,145):state=="sell"?RGB(70,235,110):state=="keep"?RGB(120,185,255):RGB(255,190,55);
  const int saved=SaveDC(hdc);RECT badge={area.right-20,area.bottom-20,area.right-3,area.bottom-3};
  HBRUSH background=CreateSolidBrush(RGB(16,25,18)),border=CreateSolidBrush(color);
  FillRect(hdc,&badge,background);FrameRect(hdc,&badge,border);
  SetBkMode(hdc,TRANSPARENT);SetTextColor(hdc,color);SelectObject(hdc,GetStockObject(DEFAULT_GUI_FONT));
  const char* label=state=="keep"?"K":"$";DrawTextA(hdc,label,1,&badge,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
  DeleteObject(background);DeleteObject(border);RestoreDC(hdc,saved);
}
}
#endif
