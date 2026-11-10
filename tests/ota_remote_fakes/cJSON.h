#pragma once
#include <map>
#include <string>
#include <cstdlib>
#include <cctype>
// Only the flat manifest DTO is modeled. Production uses ESP-IDF's cJSON.
struct cJSON {std::map<std::string,cJSON> fields;std::string string;char *valuestring=nullptr;double valuedouble=0;int kind=0;};
inline cJSON *cJSON_ParseWithLengthOpts(const char *data,size_t n,const char **,bool){
  std::string s(data,n-1);size_t at=0;auto ws=[&]{while(at<s.size()&&std::isspace(s[at]))++at;};
  auto quoted=[&](std::string &out){if(at==s.size()||s[at++]!='"')return false;auto end=s.find('"',at);if(end==s.npos)return false;out=s.substr(at,end-at);at=end+1;return true;};
  auto *j=new cJSON;ws();if(at==s.size()||s[at++]!='{'){delete j;return nullptr;}
  for(;;){ws();std::string key;if(!quoted(key)){delete j;return nullptr;}ws();if(at==s.size()||s[at++]!=':'){delete j;return nullptr;}ws();auto &v=j->fields[key];
    if(at<s.size()&&s[at]=='"'){if(!quoted(v.string)){delete j;return nullptr;}v.valuestring=v.string.data();v.kind=1;}
    else {char *end=nullptr;v.valuedouble=std::strtod(s.c_str()+at,&end);if(end==s.c_str()+at){delete j;return nullptr;}at=end-s.c_str();v.kind=2;}
    ws();if(at==s.size()){delete j;return nullptr;}if(s[at++]=='}')break;if(s[at-1]!=','){delete j;return nullptr;}
  }
  ws();if(at!=s.size()){delete j;return nullptr;}return j;
}
inline cJSON *cJSON_GetObjectItemCaseSensitive(cJSON *j,const char *key){if(!j)return nullptr;auto i=j->fields.find(key);return i==j->fields.end()?nullptr:&i->second;}
inline bool cJSON_IsString(cJSON *j){return j&&j->kind==1;}
inline bool cJSON_IsNumber(cJSON *j){return j&&j->kind==2;}
inline void cJSON_Delete(cJSON *j){delete j;}
