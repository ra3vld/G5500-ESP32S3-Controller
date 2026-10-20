#include "command_api.hpp"
#include "input.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <cstdlib>
#include <new>
using namespace forkrot;
static bool forbidAllocation=false;
void *operator new(size_t n) {if(forbidAllocation)std::abort();if(auto *p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void *operator new[](size_t n) {return ::operator new(n);}
void operator delete(void *p) noexcept {std::free(p);}
void operator delete[](void *p) noexcept {std::free(p);}
void operator delete(void *p,size_t) noexcept {std::free(p);}
void operator delete[](void *p,size_t) noexcept {std::free(p);}
static uint32_t field(const std::string &s,const char *key) {
  const auto pos=s.find(std::string("\"")+key+"\":");assert(pos!=s.npos);
  return std::stoul(s.substr(pos+std::strlen(key)+3));
}
struct CommandModel {unsigned writes=0;std::string offset;};
static void dispatch(void *context,std::string_view method,std::string_view path,FormView form,
                     JsonWriter &out,int &status,const char *&type) {
  auto &model=*static_cast<CommandModel *>(context);type="application/json";status=200;
  for(auto e:form) if(e.first=="offset")model.offset=e.second;
  if(method=="POST") {
    ++model.writes;
    if(path=="/api/large-write") {for(unsigned i=0;i<1000;++i)out << 'x';}
    else out << "{\"writes\":" << model.writes << '}';
  }
  else if(path=="/api/large") {out << "{\"value\":\"";for(unsigned i=0;i<2000;++i)out << char('a'+i%26);out << "\"}";}
  else out << "{\"ok\":true}";
}
int main() {
  CommandForm form;
  assert(form.parse("name=%D0%90+%26%3D&offset=2"));
  assert(form.get("name")=="А &=");
  for(const auto *bad:{"a=1&a=2","a=%0","a=%GG","a=%00","a=%0A","a=1&","=1"})assert(!form.parse(bad));
  assert(!form.parse("a="+std::string(129,'x')));
  ApiLineBuffer line;std::string_view completed;
  std::string longLine="@"+std::string(1023,'x');
  for(char c:longLine)assert(!line.feed(c,completed));
  assert(line.feed('\n',completed) && completed.size()==1024);
  for(char c:longLine+"x")line.feed(c,completed);
  assert(!line.feed('\n',completed));
  CommandModel model;CommandServer server;server.begin(0x1234,100,dispatch,&model);
  char buffer[COMMAND_REPLY_MAX+1];uint32_t now=0;
  auto request=[&](uint64_t peer,const std::string &text) {
    const auto n=server.process(peer,text,++now,buffer,sizeof buffer);
    assert(n && n<=COMMAND_REPLY_MAX && buffer[n]==0);return std::string(buffer,n);
  };
  const auto hello=request(1,"@v=1&cid=1&method=GET&path=/api/hello&client=7");
  const auto sid=field(hello,"sid");
  const std::string prefix="@v=1&boot=0000000000001234&sid="+std::to_string(sid);
  auto command=[&](unsigned cid,const std::string &operation){return prefix+"&cid="+std::to_string(cid)+operation;};
  const auto mutation=command(2,"&method=POST&path=/api/settings&speedPercent=50");
  const auto first=request(1,mutation);assert(field(first,"status")==200 && model.writes==1);
  assert(request(1,mutation)==first && model.writes==1); // lost response, identical retry
  assert(field(request(1,mutation+"&bad=1"),"status")==409 && model.writes==1);
  assert(field(request(2,mutation),"status")==409 && model.writes==1); // endpoint binding
  assert(field(request(1,command(3,"&method=POST&path=/api/settings&page=1")),"status")==400);
  const auto page=request(1,command(4,"&method=GET&path=/api/large&offset=2"));
  assert(field(page,"total")==2012 && field(page,"next")==672 && model.offset=="2");
  const auto second=request(1,command(5,"&method=GET&path=/api/large&page=672"));
  assert(field(page,"revision")==field(second,"revision") && field(second,"next")==1344);
  const auto last=request(1,command(6,"&method=GET&path=/api/large&page=1344"));
  assert(last.find("\"next\":null")!=last.npos && field(last,"offset")==1344);
  assert(field(request(1,mutation),"status")==409 && model.writes==1); // old write after later requests
  assert(field(request(1,command(7,"&method=GET&path=/api/cal&p=az0")),"status")==405);
  assert(field(request(1,std::string(1025,'x')),"status")==400);
  // Session eviction never permits an old datagram to create a new session.
  for(unsigned i=0;i<4;++i)request(10+i,"@v=1&cid=1&method=GET&path=/api/hello&client="+std::to_string(10+i));
  assert(field(request(1,command(100,"&method=POST&path=/api/settings")),"status")==409 && model.writes==1);
  // A stale packet from the previous boot cannot write after a reset.
  server.begin(0x5678,200,dispatch,&model);
  assert(field(request(1,mutation),"status")==409 && model.writes==1);
  char small[4];JsonWriter j(small,sizeof small);j.page(100);j << "abcdef";
  assert(j.good() && j.size()==0 && j.total()==6);
  const auto fresh=request(3,"@v=1&cid=1&method=GET&path=/api/hello&client=30");
  const auto freshSid=field(fresh,"sid");
  for(unsigned i=2;i<10002;++i) {
    const auto text="@v=1&boot=0000000000005678&sid="+std::to_string(freshSid)+"&cid="+std::to_string(i)+"&method=GET&path=/api/large";
    forbidAllocation=true;
    const auto n=server.process(3,text,++now,buffer,sizeof buffer);
    forbidAllocation=false;
    assert(n && n<=COMMAND_REPLY_MAX);
  }
  const auto oversized="@v=1&boot=0000000000005678&sid="+std::to_string(freshSid)+"&cid=10002&method=POST&path=/api/large-write";
  const auto writesBefore=model.writes;
  const auto failure=request(3,oversized);
  assert(field(failure,"status")==503 && model.writes==writesBefore+1);
  assert(request(3,oversized)==failure && model.writes==writesBefore+1);
  uint32_t selection=10;
  server.begin(0xabcd,300,dispatch,&model,&selection);
  const auto selectedSid=field(request(4,"@v=1&cid=1&method=GET&path=/api/hello&client=40"),"sid");
  const auto selectedPrefix="@v=1&boot=000000000000abcd&sid="+std::to_string(selectedSid)+"&method=POST&path=/api/target";
  const auto selectedRequest=selectedPrefix+"&cid=2&selection=10";
  const auto selectedReply=request(4,selectedRequest);
  const auto afterSelected=model.writes;
  ++selection;
  assert(request(4,selectedRequest)==selectedReply && model.writes==afterSelected);
  assert(field(request(4,selectedPrefix+"&cid=3&selection=10"),"status")==409 && model.writes==afterSelected);
  assert(field(request(4,selectedPrefix+"&cid=4&selection=11"),"status")==200 && model.writes==afterSelected+1);
  std::cout << "PASS: bounded command parser, UTF-8, stream limits, endpoint/boot/session guards, loss/replay, pagination and stale writes\n";
}
