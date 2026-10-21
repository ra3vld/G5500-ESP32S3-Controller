#include "ruckig.hpp"
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string_view>
static bool forbidAllocations=false;
void *operator new(size_t n) {
  assert(!forbidAllocations);
  if(void *p=std::malloc(n?n:1))return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept {std::free(p);}
void operator delete(void *p,size_t) noexcept {std::free(p);}
template<size_t D> void exercise(bool dump) {
  ruckig::Ruckig<D> solver;
  for(unsigned n=0;n<1000;++n) {
    ruckig::InputParameter<D> input;
    const unsigned kind=n%5;
    input.control_interface=kind<3?ruckig::ControlInterface::Position:ruckig::ControlInterface::Velocity;
    if(n%3==0)input.minimum_duration=3.;
    for(unsigned d=0;d<D;++d) {
      input.current_position[d]=double(int(n%21)-10)+d;
      input.target_position[d]=double(int((n*17+d*13)%101)-50)/3.;
      input.current_velocity[d]=kind<3?0.:double(int(n%7)-3)/2.;
      input.target_velocity[d]=kind<3?0.:double(int((n*3+d)%9)-4)/2.;
      input.max_velocity[d]=4.+d;
      input.max_acceleration[d]=kind==2?INFINITY:3.+d;
      input.max_jerk[d]=(kind==0 || kind==3)?8.+d:INFINITY;
    }
    ruckig::Trajectory<D> reused,fresh,cloned;
    ruckig::Ruckig<D> independent;
    forbidAllocations=true;
    const auto result=solver.calculate(input,reused);
    assert(result>=0 && independent.calculate(input,fresh)==result);
    auto copy=solver;
    assert(copy.calculate(input,cloned)==result);
    assert(reused.get_duration()==fresh.get_duration() && reused.get_duration()==cloned.get_duration());
    std::array<double,D> p,v,a,fp,fv,fa,cp,cv,ca;
    for(const double fraction : {0.,.25,.5,1.}) {
      const double at=reused.get_duration()*fraction;
      reused.at_time(at,p,v,a);fresh.at_time(at,fp,fv,fa);cloned.at_time(at,cp,cv,ca);
      assert(p==fp && v==fv && a==fa && p==cp && v==cv && a==ca);
      for(unsigned d=0;d<D;++d)assert(std::isfinite(p[d]) && std::isfinite(v[d]) && std::isfinite(a[d]));
    }
    for(unsigned d=0;d<D;++d) {
      assert(std::abs(v[d]-input.target_velocity[d])<1e-7);
      if(kind<3)assert(std::abs(p[d]-input.target_position[d])<1e-7);
    }
    forbidAllocations=false;
    if(dump) {
      std::cout<<D<<' '<<n<<' '<<std::hexfloat<<reused.get_duration();
      for(unsigned d=0;d<D;++d)std::cout<<' '<<p[d]<<' '<<v[d]<<' '<<a[d];
      std::cout<<'\n';
    }
  }
}
int main(int argc,char **argv) {
  const bool dump=argc==2 && std::string_view(argv[1])=="--dump";
  exercise<1>(dump);exercise<2>(dump);
  if(!dump)std::cout<<"PASS: 2000 mixed-order/position/velocity trajectories, reused/independent/copied solvers, no calculation allocations\n";
}
