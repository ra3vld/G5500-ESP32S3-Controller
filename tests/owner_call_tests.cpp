// G5500 Fork; GPL-3.0.
#include "owner_call.hpp"
#include <cassert>
#include <thread>
#include <iostream>
using namespace forkrot;
int main() {
  OwnerCall call;
  assert(!call.service() && !call.takeResult());
  assert(!call.post(nullptr,nullptr));
  struct Arguments { OwnerCall *call; unsigned input=0,output=0; } args{&call};
  const auto apply=[](void *context) {
    auto &args=*static_cast<Arguments *>(context);
    assert(!args.call->service()); // no re-entrance from a service checkpoint
    args.output=args.input*3+1;
  };
  assert(call.post(apply,&args) && !call.post(apply,&args));
  assert(!call.takeResult() && call.service() && !call.service());
  assert(!call.post(apply,&args) && call.takeResult() && !call.takeResult());
  constexpr unsigned Count=100000;
  std::thread producer([&] {
    for(unsigned i=1;i<=Count;++i) {
      // Stack-owned data is deliberately reused immediately after the reply.
      Arguments request{&call,i,0};
      assert(call.post(apply,&request));
      while(!call.takeResult())std::this_thread::yield();
      assert(request.output==i*3+1);
    }
  });
  for(unsigned i=0;i<Count;) {
    if(call.service())++i;
    else std::this_thread::yield();
  }
  producer.join();
  assert(!call.service());
  std::cout<<"owner_call_tests: 100000 cross-thread requests passed\n";
}
