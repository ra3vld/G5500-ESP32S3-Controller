// Bounded decimal formatting for small-flash builds. GPL-3.0.
#pragma once
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
namespace forkrot::compact_decimal {
// IEEE significand * 2^exponent is scaled by 10^precision and rounded exactly
// to nearest/even using integers. No locale, malloc, or large power tables.
struct Big {
  static constexpr unsigned Count=40;
  std::array<uint32_t,Count> word{};
  explicit Big(uint64_t n=0){word[0]=uint32_t(n);word[1]=uint32_t(n>>32);}
  unsigned bits() const {
    for(unsigned i=Count;i;--i)if(word[i-1])return (i-1)*32+32-unsigned(__builtin_clz(word[i-1]));
    return 0;
  }
  int compare(const Big &b) const {
    for(unsigned i=Count;i;--i)if(word[i-1]!=b.word[i-1])return word[i-1]<b.word[i-1]?-1:1;
    return 0;
  }
  void subtract(const Big &b) {
    uint64_t borrow=0;
    for(unsigned i=0;i<Count;++i){const uint64_t n=uint64_t(b.word[i])+borrow;const auto old=word[i];word[i]=uint32_t(uint64_t(old)-n);borrow=uint64_t(old)<n;}
  }
  void multiply5(){uint64_t carry=0;for(auto &n:word){carry+=uint64_t(n)*5;n=uint32_t(carry);carry>>=32;}}
  void left(unsigned shift) {
    const unsigned limbs=shift/32,rem=shift%32;
    for(unsigned i=Count;i;--i){const unsigned at=i-1;uint32_t n=0;
      if(at>=limbs){n=word[at-limbs]<<rem;if(rem && at>limbs)n|=word[at-limbs-1]>>(32-rem);}word[at]=n;
    }
  }
  void right(){uint32_t carry=0;for(unsigned i=Count;i;--i){const auto n=word[i-1];word[i-1]=(n>>1)|carry;carry=n<<31;}}
  void increment(){for(auto &n:word)if(++n)break;}
  unsigned divide10(){uint64_t rem=0;for(unsigned i=Count;i;--i){const uint64_t n=(rem<<32)|word[i-1];word[i-1]=uint32_t(n/10);rem=n%10;}return unsigned(rem);}
};
// This slow path is separate so ordinary angle/velocity formatting keeps a
// small stack frame. It also covers subnormals and extreme binary64 inputs.
inline __attribute__((noinline)) size_t slowDigits(uint64_t mantissa,int exponent,int precision,char *digits) {
  Big n(mantissa),d(1),q;
  if(precision>=0)for(int i=0;i<precision;++i)n.multiply5();
  else for(int i=0;i<-precision;++i)d.multiply5();
  exponent+=precision;
  if(exponent>=0)n.left(unsigned(exponent));else d.left(unsigned(-exponent));
  const int shift=int(n.bits())-int(d.bits());
  if(shift>=0){Big trial=d;trial.left(unsigned(shift));for(int i=shift;i>=0;--i){if(n.compare(trial)>=0){n.subtract(trial);q.word[unsigned(i)/32]|=uint32_t(1)<<(unsigned(i)%32);}trial.right();}}
  Big other=d;other.subtract(n);const int halfway=n.compare(other);
  if(halfway>0 || (halfway==0 && (q.word[0]&1)))q.increment();
  size_t count=0;do{if(count==64)return 0;digits[count++]=char('0'+q.divide10());}while(q.bits());
  for(size_t i=0;i<count/2;++i){const char c=digits[i];digits[i]=digits[count-1-i];digits[count-1-i]=c;}
  return count;
}
inline size_t scaledDigits(uint64_t mantissa,int exponent,int precision,char *digits) {
  uint64_t n=mantissa,d=1;bool small=true;
  auto &scaled=precision>=0?n:d;
  for(int i=0;i<(precision>=0?precision:-precision);++i){if(scaled>UINT64_MAX/5){small=false;break;}scaled*=5;}
  exponent+=precision;
  if(small){auto &shifted=exponent>=0?n:d;const unsigned shift=unsigned(exponent>=0?exponent:-exponent);
    if(shift>=64 || shifted>(UINT64_MAX>>shift))small=false;else shifted<<=shift;
  }
  if(!small)return slowDigits(mantissa,exponent-precision,precision,digits);
  uint64_t q=n/d;const uint64_t rem=n%d;
  if(rem>d-rem || (rem==d-rem && (q&1)))++q;
  const auto result=std::to_chars(digits,digits+64,q);
  return result.ec==std::errc()?size_t(result.ptr-digits):0;
}
template<class Number> size_t format(char *out,size_t capacity,Number value,int precision) {
  static_assert(std::is_same_v<Number,float> || std::is_same_v<Number,double>);
  static_assert(std::numeric_limits<Number>::is_iec559);
  if(!std::isfinite(value) || precision>61)return 0;
  const bool negative=std::signbit(value),general=precision<0;
  int exponent=0;uint64_t mantissa=0;
  if constexpr(sizeof(Number)==4){uint32_t bits;std::memcpy(&bits,&value,4);const unsigned e=(bits>>23)&255;mantissa=bits&0x7fffff;exponent=e?int(e)-150:-149;if(e)mantissa|=1u<<23;}
  else {uint64_t bits;std::memcpy(&bits,&value,8);const unsigned e=unsigned((bits>>52)&2047);mantissa=bits&0xfffffffffffffULL;exponent=e?int(e)-1075:-1074;if(e)mantissa|=1ULL<<52;}
  if(mantissa)while(!(mantissa&1)){mantissa>>=1;++exponent;}
  const double magnitude=std::fabs(double(value));
  const int decimalExponent=mantissa && (general || magnitude>1e50)?int(std::floor(std::log10(magnitude))):0;
  if(general)precision=5-decimalExponent;
  else if(decimalExponent+precision+int(negative)+(precision?1:0)>int(capacity))return 0;
  char digits[64];size_t count=mantissa?scaledDigits(mantissa,exponent,precision,digits):1;
  if(!mantissa)digits[0]='0';
  if(!count)return 0;
  size_t used=0;bool good=true;
  const auto put=[&](char c){if(used<capacity)out[used++]=c;else good=false;};
  if(negative)put('-');
  if(!general) {
    const int point=int(count)-precision;
    if(point<=0){put('0');if(precision)put('.');for(int i=0;i<-point;++i)put('0');for(size_t i=0;i<count;++i)put(digits[i]);}
    else for(size_t i=0;i<count;++i){if(int(i)==point)put('.');put(digits[i]);}
  } else {
    const int e=int(count)-1-precision;
    while(count>1 && digits[count-1]=='0')--count;
    if(!mantissa){put('0');}
    else if(e<-4 || e>=6) {
      put(digits[0]);if(count>1)put('.');for(size_t i=1;i<count;++i)put(digits[i]);
      put('e');put(e<0?'-':'+');char text[8];const auto r=std::to_chars(text,text+8,e<0?-e:e);const auto n=size_t(r.ptr-text);if(n<2)put('0');for(size_t i=0;i<n;++i)put(text[i]);
    } else if(e<0){put('0');put('.');for(int i=0;i<-e-1;++i)put('0');for(size_t i=0;i<count;++i)put(digits[i]);}
    else {const size_t point=size_t(e+1);for(size_t i=0;i<count || i<point;++i){if(i==point)put('.');put(i<count?digits[i]:'0');}}
  }
  return good?used:0;
}
} // namespace forkrot::compact_decimal
