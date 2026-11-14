#include "json_writer.hpp"
#include "compact_decimal.hpp"
#include <cassert>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <string_view>
static bool deny=false;
void *operator new(size_t n){if(deny)throw std::bad_alloc();if(auto *p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void *operator new[](size_t n){return ::operator new(n);}
void operator delete(void *p)noexcept{std::free(p);}
void operator delete[](void *p)noexcept{std::free(p);}
void operator delete(void *p,size_t)noexcept{std::free(p);}
void operator delete[](void *p,size_t)noexcept{std::free(p);}
template<class T>static void verify(T value,int precision){
 if(!std::isfinite(value))return;
 char expected[64],actual[66];std::memset(actual,'X',sizeof actual);
 const auto ref=std::to_chars(expected,expected+63,value,precision<0?std::chars_format::general:std::chars_format::fixed,precision<0?6:precision);
 deny=true;const auto n=forkrot::compact_decimal::format(actual,63,value,precision);deny=false;
 assert(actual[63]=='X');
 if(ref.ec!=std::errc()){assert(!n);return;}
 const std::string_view a(actual,n),b(expected,ref.ptr-expected);
 if(a!=b){std::cerr<<"Mismatch "<<std::hexfloat<<double(value)<<" precision="<<precision<<" expected="<<b<<" got="<<a<<'\n';std::abort();}
 char json[64];forkrot::JsonWriter j(json,sizeof json);deny=true;j.decimal(value,precision);deny=false;assert(j.good()&&std::string_view(j.data(),j.size())==b);
}
int main(){
 for(double x:{0.,-0.,.5,-.5,1.005,2.675,999999.5,999999.99999,99999.95,0.00009999995,1e-4,1e-5,1e-6,1e20,1e30,1e60,1e300,std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::max()})
  for(int p:{-1,0,1,2,6,12,30,61}){verify(x,p);verify(float(x),p);}
 std::mt19937 random(0x5500);
 for(unsigned i=0;i<100000;++i){const uint32_t bits=random();float v;std::memcpy(&v,&bits,4);for(int p:{-1,0,1,2,6})verify(v,p);}
 for(unsigned i=0;i<20000;++i){const uint64_t bits=(uint64_t(random())<<32)|random();double v;std::memcpy(&v,&bits,8);for(int p:{-1,2,6})verify(v,p);}
 for(float value:{12.345f,-0.0001f,3600.f,1e-6f})for(int p:{-1,1,2,6}){
  char full[64];const auto n=forkrot::compact_decimal::format(full,sizeof full,value,p);
  for(size_t size=0;size<n;++size){char tiny[64];std::memset(tiny,'X',sizeof tiny);assert(!forkrot::compact_decimal::format(tiny,size,value,p));assert(tiny[size]=='X');}
 }
 char b[64];forkrot::JsonWriter j(b,sizeof b);j<<std::numeric_limits<float>::infinity();assert(j.good()&&std::string_view(j.data())=="null");
 std::cout<<"Compact decimal: exact charconv parity, binary32/binary64 boundaries and random samples, fixed/general, no allocations, output bounds PASS\n";
}
