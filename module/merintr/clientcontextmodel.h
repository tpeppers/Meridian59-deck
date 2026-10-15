// Local metadata only. No Meridian protocol calls belong in this model.
#ifndef M59_CLIENT_CONTEXT_MODEL_H
#define M59_CLIENT_CONTEXT_MODEL_H
#include <string>
#include <vector>
#include <sstream>
#include <cstdint>
#include <cctype>
#include <set>
namespace M59Context {
using Time = std::uint64_t;
struct Field { std::string id,title,type,value,description; };
struct Strategy { std::string id,title,description; bool enabled=false; std::vector<Field> fields; };
struct View { Time at=0,ack=0; std::string token,status; std::vector<Strategy> strategies; std::string plan; };
inline std::vector<std::string> Split(const std::string& s) {
  std::vector<std::string> out; size_t p=0;
  for(;;){auto q=s.find('\t',p);out.push_back(s.substr(p,q-p));if(q==s.npos)return out;p=q+1;}
}
inline bool Number(const std::string& s,Time& n) {
  if(s.empty()||s.size()>16)return false;n=0;
  for(char c:s){if(c<'0'||c>'9')return false;n=n*10+c-'0';}return true;
}
inline int Hex(char c) { return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1; }
inline bool Decode(const std::string& s,std::string& out) {
  out.clear();if(s.size()>16384)return false;
  for(size_t i=0;i<s.size();i++){unsigned char c=s[i];if(c=='%'){
    if(i+2>=s.size()||Hex(s[i+1])<0||Hex(s[i+2])<0)return false;
    c=static_cast<unsigned char>(Hex(s[i+1])*16+Hex(s[i+2]));i+=2;}
    if(c==0||c<32||c==127)return false;out+=c;}
  return true;
}
inline bool Id(const std::string& s) {
  if(s.empty()||s.size()>100)return false;
  for(char c:s)if(!(c>='a'&&c<='z')&&!(c>='0'&&c<='9')&&c!='-'&&c!='_')return false;
  return true;
}
// Binding is the exact eight identity fields echoed from the native heartbeat.
inline bool Parse(const std::string& text,const std::string& binding,Time now,View& output) {
  if(text.size()>262144||text.size()<5||text.substr(text.size()-5)!="\nEND\n")return false;
  std::istringstream in(text);std::string line;std::getline(in,line);auto h=Split(line);
  View v;Time at=0,ack=0;
  if(h.size()!=15||h[0]!="M59CTX"||h[1]!="1"||!Number(h[2],at)||at>now+1000||now>at+6000)return false;
  std::string got;for(size_t i=3;i<11;i++){if(i>3)got+='\t';got+=h[i];}
  if(got!=binding||h[11].size()!=48||!Number(h[12],ack)||h[13]!="human"||!Decode(h[14],v.status))return false;
  for(char c:h[11])if(Hex(c)<0)return false;
  v.at=at;v.ack=ack;v.token=h[11];std::set<std::string> ids;
  while(std::getline(in,line)){
    if(line=="END"){if(std::getline(in,line))return false;output=std::move(v);return true;}
    auto f=Split(line);
    if(f.size()==2&&f[0]=="T") {std::string s;if(!Decode(f[1],s)||v.plan.size()+s.size()>65536)return false;v.plan+=s+"\r\n";}
    else if(f.size()==5&&f[0]=="S") {
      Strategy s;if(!Id(f[1])||!ids.insert(f[1]).second||v.strategies.size()>=64||
        !Decode(f[2],s.title)||s.title.size()>300||(f[3]!="0"&&f[3]!="1")||!Decode(f[4],s.description))return false;
      s.id=f[1];s.enabled=f[3]=="1";v.strategies.push_back(s);
    } else if(f.size()==7&&f[0]=="F") {
      if(v.strategies.empty()||f[1]!=v.strategies.back().id||!Id(f[2])||v.strategies.back().fields.size()>=32)return false;
      Field x;x.id=f[2];x.type=f[4];
      for(const auto& old:v.strategies.back().fields)if(old.id==x.id)return false;
      if(!Decode(f[3],x.title)||!Decode(f[5],x.value)||!Decode(f[6],x.description)||x.value.size()>4096||
        (x.type!="boolean"&&x.type!="integer"&&x.type!="number"&&x.type!="number-list"&&x.type!="item-list"))return false;
      v.strategies.back().fields.push_back(x);
    } else return false;
  }return false;
}
inline int Condition(std::string text) {
  for(auto& c:text)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  const std::vector<std::vector<std::string>> phrases={
    {"shattered by a powerful blow","is useless.","is useless,","shattered by a forceful blow","cleft in two"},
    {"may not last much longer","cracked in several places","dented and worn nearly to the point"},
    {"notched and stained","nicked and scarred","deeply scarred from battle","marked with the scars"},
    {"slightly tarnished","dent or two","deep gouge","stained with blood but otherwise"},
    {"flawless condition","without blemish","smooth perfection","excellent condition","great condition"}};
  for(int i=0;i<5;i++)for(const auto& p:phrases[i])if(text.find(p)!=text.npos)return i;
  return -1;
}
}
#endif
