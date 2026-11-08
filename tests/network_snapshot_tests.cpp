#include <mutex>
using portMUX_TYPE=std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(p) (p)->lock()
#define portEXIT_CRITICAL(p) (p)->unlock()
#include "build_config.hpp"
#include "ethernet.hpp"
#define ARDUINO 1
#include "network_snapshot.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;
static_assert(FORK_BACKGROUND_NETWORK_SNAPSHOT,"Exercise the production dual-core snapshot path");
int main() {
  tcp_pcb peer;peer.local_port=80;peer.remote_port=8883;peer.nrtx=2;
  tcp_seg queued;queued.len=512;peer.unacked=&queued;tcp_active_pcbs=&peer;
  setNetworkSnapshotBackground(true);
  // A busy lwIP core must never be entered by the control owner, even while
  // the first sample or a different broker port is waiting for the worker.
  fakeCoreLocked=true;
  assert(!networkTcpSnapshot(8883).available);
  assert(!networkStatusSnapshot().tcpAvailable);
  assert(networkSnapshotAgeMs()==UINT32_MAX);
  fakeCoreLocked=false;publishNetworkSnapshot();
  fakeCoreLocked=true;
  const auto cached=networkTcpSnapshot(8883);
  assert(cached.available && cached.active==1 && cached.mqttPeers==1);
  assert(cached.httpUnacked==512 && cached.httpRetries==2);
  peer.nrtx=3;queued.len=1024;
  for(unsigned i=0;i<10000;++i)assert(networkTcpSnapshot().httpUnacked==512);
  assert(!networkTcpSnapshot(1883).available);
  fakeCoreLocked=false;publishNetworkSnapshot();fakeCoreLocked=true;
  const auto next=networkTcpSnapshot(1883);
  assert(next.available && next.mqttPeers==0 && next.httpUnacked==1024 && next.httpRetries==3);
  fakeCoreLocked=false;setNetworkSnapshotBackground(false);
  assert(networkTcpSnapshot(8883).mqttPeers==1); // synchronous fallback without worker
  std::cout<<"PASS: owner diagnostics use completed TCP snapshots while lwIP is busy; startup, freshness and broker-port changes remain explicit\n";
}
