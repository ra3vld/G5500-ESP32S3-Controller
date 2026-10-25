// Host peer for testing the PC gateway against the production MTR/1 core.
#include "modbus_token.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
int main() {
  forkrot::ModbusConfig config;config.count=2;config.node=201;config.uart1={115200,1,1};
  forkrot::mb::Token token;token.begin(config,true,111,0);
  std::string line;
  while(std::getline(std::cin,line)) {
    std::istringstream input(line);char operation;uint32_t now;input>>operation>>now;
    std::ostringstream frames;
    auto emit=[&](const forkrot::mb::Frame &frame){
      token.activity(now);frames<<' ';
      for(unsigned i=0;i<frame.size;++i)frames<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(frame.data[i]);
      return true;
    };
    if(operation=='s'){bool idle,outstanding;input>>idle>>outstanding;token.service(now,idle,outstanding,emit);if(token.owns(now))token.pass();}
    if(operation=='r'){
      std::string hex;input>>hex;forkrot::mb::Frame frame;frame.size=hex.size()/2;
      if(frame.size>forkrot::mb::Max)return 2;
      for(unsigned i=0;i<frame.size;++i)frame.data[i]=std::stoul(hex.substr(2*i,2),nullptr,16);
      if(!token.observe(frame,now,emit))token.activity(now);
    }
    if(operation=='b')token.begin(config,true,1,now);
    std::cout<<unsigned(token.owner)<<' '<<token.owns(now)<<' '<<token.fault<<frames.str()<<std::endl;
  }
}
