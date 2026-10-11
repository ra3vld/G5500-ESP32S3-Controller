// Bounded, fully validated XML subset for N1MM/PST; no DTD or entities expansion.
#include "control.hpp"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <algorithm>
namespace forkrot {
bool LineBuffer::feed(char byte,std::string& line){
 if(byte=='\r'||byte=='\n'){
  bool ready=!discarded_&&!buffer_.empty();
  if(ready)line=buffer_;
  clear();return ready;
 }
 if(discarded_)return false;
 if((byte<32&&byte!='\t')||byte>126||buffer_.size()>=512){buffer_.clear();discarded_=true;return false;}
 buffer_+=byte;return false;
}
namespace {
std::string trim(std::string s){size_t a=0,b=s.size();while(a<b&&(std::isspace((unsigned char)s[a])||s[a]==0))++a;while(b>a&&(std::isspace((unsigned char)s[b-1])||s[b-1]==0))--b;return s.substr(a,b-a);}
struct Node {std::string name,text;std::vector<Node> children;};
class Xml {
 const std::string& s;size_t p=0;unsigned nodes=0;
 void ws(){while(p<s.size()&&std::isspace((unsigned char)s[p]))++p;}
 std::string name(){size_t start=p;while(p<s.size()&&(std::isalnum((unsigned char)s[p])||s[p]=='_'||s[p]=='+'||s[p]=='-'))++p;return s.substr(start,p-start);}
 bool node(Node& n,unsigned depth){
  if(depth>3||++nodes>40||p>=s.size()||s[p++]!='<')return false;
  n.name=name();if(n.name.empty()||p>=s.size())return false;
  if(s.compare(p,2,"/>")==0){p+=2;return true;}
  if(s[p++]!='>')return false;
  for(;;){
   if(p>=s.size())return false;
   if(s.compare(p,2,"</")==0){p+=2;auto end=name();if(end!=n.name||p>=s.size()||s[p++]!='>')return false;n.text=trim(n.text);return true;}
   if(s[p]=='<'){Node child;if(!node(child,depth+1))return false;n.children.push_back(child);}
   else n.text+=s[p++];
  }
 }
 public: explicit Xml(const std::string& input):s(input){}
 bool parse(Node& n){ws();if(s.compare(p,5,"<?xml")==0){size_t end=s.find("?>",p);if(end==std::string::npos)return false;p=end+2;ws();}if(!node(n,0))return false;ws();return p==s.size();}
};
const Node* field(const Node& n,const std::string& key){for(const auto& c:n.children)if(c.name==key)return &c;return nullptr;}
bool unique(const Node& n){for(size_t i=0;i<n.children.size();++i){if(!unique(n.children[i]))return false;for(size_t j=i+1;j<n.children.size();++j)if(n.children[i].name==n.children[j].name)return false;}return true;}
bool flag(const Node* n,bool& b){if(!n){b=false;return true;}if(!n->children.empty()||(n->text!="0"&&n->text!="1"))return false;b=n->text=="1";return true;}
std::vector<std::string> words(std::string s){std::istringstream stream(s);std::vector<std::string> v;std::string word;while(stream>>word)v.push_back(word);return v;}
}
Packet Protocol::datagram(const std::string& input,uint32_t now){
 Packet p;auto s=trim(input);
 if(s.size()>4096||s.find("<!")!=std::string::npos||s.find('&')!=std::string::npos)return p;
 Node root;if(!Xml(s).parse(root)||!unique(root))return p;
 if(root.name=="N1MMRotor"){
  if(!root.text.empty())return p;
  for(const auto& n:root.children){
   if(n.name!="stop"&&n.name!="rotor"&&n.name!="goazi"&&n.name!="offset"&&n.name!="bidirectional"&&n.name!="freqband"&&n.name!="app")return p;
   if(n.name!="stop"&&!n.children.empty())return p;
  }
  const Node* stopNode=field(root,"stop");
  if(stopNode){
   if(!stopNode->text.empty()||field(root,"goazi"))return p;
   for(const auto& n:stopNode->children)if((n.name!="rotor"&&n.name!="freqband")||!n.children.empty())return p;
  }
  const Node* rotor=field(root,"rotor");if(!rotor&&stopNode)rotor=field(*stopNode,"rotor");
  if(!rotor||!rotor->children.empty())return p;
  bool match=false;std::istringstream names(rotor->text);std::string name;while(std::getline(names,name,','))if(trim(name)==rotorName||trim(name)=="*")match=true;
  if(!match){p.result=Result::Ignored;return p;}p.matched=true;
  if(stopNode){float offset;bool bi;if(auto off=field(root,"offset")){if(!number(off->text,offset)||std::abs(offset)>360)return p;}if(!flag(field(root,"bidirectional"),bi))return p;}
  if(stopNode){c_.stop();p.result=Result::Ok;return p;}
  auto az=field(root,"goazi");float value=0,offset=0;bool bi=false;
  if(!az||!az->children.empty()||!number(az->text,value)||value<0||value>360)return p;
  auto off=field(root,"offset");if(off&&(!off->children.empty()||!number(off->text,offset)||std::abs(offset)>360))return p;
  if(!flag(field(root,"bidirectional"),bi))return p;
  value=nearest(value+offset,c_.axes[0].current,c_.maximum,bi);
  p.result=c_.target({value,0,true,false},true,now);return p;
 }
 if(root.name!="PST")return p;
 p.matched=true;
 if(!root.children.empty()&&!root.text.empty())return p;
 for(const auto& n:root.children)if(!n.children.empty())return p;
 bool stop=false;
 for(const char* tag:{"STOP","STOPR","LEFT","RIGHT","LP","TRACK","PARK"}){bool b=false;auto n=field(root,tag);if(!flag(n,b))return p;if((std::string(tag)=="STOP"||std::string(tag)=="STOPR")&&b)stop=true;}
 auto az=field(root,"AZIMUTH"),el=field(root,"ELEVATION");float av=0,ev=0;
 if(az&&(!number(az->text,av)||av<0||av>c_.maximum))return p;
 if(el&&(!number(el->text,ev)||ev<0||ev>180||(c_.model==Model::G800DXA&&ev!=0)))return p;
 for(const auto& n:root.children)if(n.name!="AZIMUTH"&&n.name!="ELEVATION"&&n.name!="STOP"&&n.name!="STOPR"&&n.name!="LEFT"&&n.name!="RIGHT"&&n.name!="LP"&&n.name!="TRACK"&&n.name!="PARK"&&n.name!="CALL"&&n.name!="FREQUENCY"&&n.name!="RADIO"&&n.name!="AZ"&&n.name!="TGA")return p;
 if(stop){c_.stop();p.result=Result::Ok;return p;}
 auto query=root.text;
 if(auto n=field(root,"AZ")){if(n->text!="?")return p;query="AZ?";}
 if(auto n=field(root,"TGA")){if(n->text!="?")return p;query="TGA?";}
 if(!query.empty()){
  if(query!="AZ?"&&query!="TGA?")return p;
  float value=c_.axes[0].current;
  if(query=="TGA?")value=c_.pendingValid&&c_.pending.hasAz?c_.pending.az:c_.axes[0].target;
  char b[40];std::snprintf(b,sizeof b,"%s:%.1f\r",query=="AZ?"?"AZ":"TGA",heading(value));p.reply=b;p.result=Result::Ok;return p;
 }
 bool left=false,right=false;flag(field(root,"LEFT"),left);flag(field(root,"RIGHT"),right);
 if(left&&right)return p;
 bool park=false;flag(field(root,"PARK"),park);
 if(park){
  if(az||el||left||right||field(root,"LP")||!c_.valid(c_.presets[0].target))return p;
  // UR5EQF's Park button uses preset 1 as the configurable parking position.
  // It remains an external numerical target: manual confirmation and preset priority apply.
  p.result=c_.target(c_.presets[0].target,true,now);return p;
 }
 // Preset priority also prevents ignored packets altering cached LP/base state.
 if(c_.presetActive&&(az||el||left||right||field(root,"LP"))){p.result=Result::Ignored;return p;}
 if(left||right){p.result=c_.jog(0,right?1:-1,true,now);return p;}
 bool lp=longPath_;if(auto n=field(root,"LP"))flag(n,lp);
 bool changed=lp!=longPath_;
 if(az){pstBase_=av;pstBaseValid_=true;}
 longPath_=lp;
 if(az||(changed&&pstBaseValid_)){
  float value=longPath_?heading(pstBase_+180):pstBase_;
  value=nearest(value,c_.axes[0].current,c_.maximum);
  p.result=c_.target({value,ev,true,el!=nullptr},true,now);
 }else if(el)p.result=c_.target({0,ev,false,true},true,now);
 else p.result=Result::Ok;
 return p;
}
std::string Protocol::positionReport(float mechanicalAz) const {
 // Real N1MM Logger+ decodes the heading in tenths of a degree.
 return rotorName+"@"+std::to_string(int(std::lround(heading(mechanicalAz)*10))%3600);
}
Packet Protocol::line(const std::string& input,uint32_t now,bool tcp){
 if(loadedEpoch_!=c_.stopEpoch){loaded_.clear();loadedEpoch_=c_.stopEpoch;}
 Packet p;auto s=trim(input);if(s.empty()||s.size()>512)return p;
 auto v=words(s);std::string cmd=v[0],u=cmd;std::transform(u.begin(),u.end(),u.begin(),[](unsigned char ch){return std::toupper(ch);});
 if(u=="SET_POS"||u=="\\SET_POS"||u=="GET_POS"||u=="\\GET_POS"||u=="STOP"||u=="\\STOP")std::transform(cmd.begin(),cmd.end(),cmd.begin(),[](unsigned char ch){return std::tolower(ch);});
 bool ham=(cmd=="P"&&v.size()>1)||cmd=="p"||cmd=="\\set_pos"||cmd=="set_pos"||cmd=="\\get_pos"||cmd=="get_pos"||cmd=="\\stop"||cmd=="stop"||(tcp&&u=="S");
 p.matched=true;p.result=Result::Ok;
 char b[100];
 if(cmd=="p"||cmd=="\\get_pos"||cmd=="get_pos"){
  std::snprintf(b,sizeof b,"%.6f\n%.6f\n",c_.axes[0].current,c_.axes[1].current);p.reply=b;return p;
 }
 if(u=="C"||u=="C2"||u=="B"){
  if(u=="C")std::snprintf(b,sizeof b,"AZ=%03d\r\n",int(std::lround(c_.axes[0].current)));
  else if(u=="B")std::snprintf(b,sizeof b,"EL=%03d\r\n",int(std::lround(c_.axes[1].current)));
  else std::snprintf(b,sizeof b,"AZ=%03d EL=%03d\r\n",int(std::lround(c_.axes[0].current)),int(std::lround(c_.axes[1].current)));
  p.reply=b;return p;
 }
 if(u=="S"||cmd=="\\stop"||cmd=="stop")c_.stop();
 else if(u=="A"||u=="E")c_.stop(u=="A"?0:1);
 else if(u=="L"||u=="R"||u=="U"||u=="D")p.result=c_.jog(u=="U"||u=="D"?1:0,u=="R"||u=="U"?1:-1,true,now);
 else if(u=="X1"||u=="X2"||u=="X3"||u=="X4")c_.speedPercent=(u[1]-'0')*25;
 else if(cmd=="P"||cmd=="\\set_pos"||cmd=="set_pos"){
  float az=0,el=0;if(v.size()!=3||!number(v[1],az)||!number(v[2],el))p.result=Result::Invalid;
  else p.result=c_.target({az,el,true,true},true,now);
 }else if(u[0]=='M'||u[0]=='W'){
  bool hasEl=u[0]=='W';auto args=words(s.substr(1));std::vector<float> nums;
  for(auto a:args){float value;if(!number(a,value)){p.result=Result::Invalid;break;}nums.push_back(value);}
  if(p.result==Result::Ok){
   if(nums.size()==(hasEl?2u:1u))p.result=c_.target({nums[0],hasEl?nums[1]:0,true,hasEl},true,now);
   else if(nums.size()>=(hasEl?3u:2u)&&(!hasEl||(nums.size()-1)%2==0)&&nums[0]>=1&&nums[0]<=999&&std::floor(nums[0])==nums[0]){
    std::vector<Target> points;for(size_t i=1;i<nums.size();i+=hasEl?2:1)points.push_back({nums[i],hasEl?nums[i+1]:0,true,hasEl});
    bool valid=points.size()<=16;for(auto t:points)valid=valid&&c_.valid(t);
    if(!valid)p.result=Result::Invalid;
    else if(c_.presetActive)p.result=Result::Ignored;
    else {loaded_=points;loadedEpoch_=c_.stopEpoch;loadedInterval_=uint32_t(nums[0])*1000;p.result=c_.target(points[0],true,now);}
   }else p.result=Result::Invalid;
  }
 }else if(u=="T")p.result=c_.sequence(loaded_,loadedInterval_,now);
 else if(u=="N"){p.reply="N="+std::to_string(loaded_.size())+"\r\n";return p;}
 else {p.matched=false;return p;}
 if(ham)p.reply=p.result==Result::Ok||p.result==Result::Pending?"RPRT 0\n":p.result==Result::Ignored?"RPRT -8\n":"RPRT -1\n";
 else if(p.result==Result::Invalid||p.result==Result::NotCalibrated)p.reply="?>\r\n";
 return p;
}
} // namespace forkrot
