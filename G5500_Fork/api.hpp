#pragma once
#include "control.hpp"
#include <map>
namespace forkrot {
struct Settings {unsigned n1mmPort=12040,pstPort=1234,reportPort=13010;std::string reportHost;};
struct Response {int status=200;std::string body;};
using Form=std::map<std::string,std::string>;
std::string jsonQuote(const std::string& s);
std::string stateJson(const Controller& c,const Protocol& p,const Settings& settings,uint32_t now);
Response api(Controller& c,Protocol& p,Settings& settings,const std::string& path,const Form& form,uint32_t now);
Form decodeForm(const std::string& input);
}
