// Management transports. All controller access stays on the Arduino loop. GPL-3.0.
#include "network.hpp"
#include <lwip/sockets.h>
#include "hardware_form.hpp"
#include <new>
namespace forkrot {
namespace {
size_t unavailableReply(std::string_view input,char *reply,size_t capacity) {
  uint32_t cid=0;CommandForm form;
  if(!input.empty() && input.front()=='@' && form.parse(input.substr(1)))hardwareUnsigned(form.get("cid"),cid);
  JsonWriter out(reply,capacity);
  out << "{\"v\":1,\"cid\":" << cid << ",\"status\":503,\"error\":\"command_memory_unavailable\"}";
  return out.good()?out.size():0;
}
}
bool Network::beginCommandApi() {
  if(commandApi_)return true;
  // Ordinary web builds pay for sessions only when the command API is used.
  // Refuse before malloc rather than causing a failed-allocation recovery.
  if(heapFreeBytes()<sizeof(CommandServer)+MemoryGuard::CriticalFree ||
      heapMaxBlockBytes()<sizeof(CommandServer)+MemoryGuard::CriticalBlock)return false;
  commandApi_.reset(new(std::nothrow) CommandServer);
  if(!commandApi_)return false;
  commandApi_->begin(runtime_.hud.boot,esp_random(),
      [](void *context,std::string_view method,std::string_view path,FormView form,
          JsonWriter &out,int &status,const char *&type) {
        auto &self=*static_cast<Network *>(context);
        self.actions_.deviceRequest(method,path,form,out,status,type,true);
      },this,&runtime_.hud.selectionEpoch);
  return true;
}
size_t Network::runCommandApi(uint64_t peer,std::string_view input,char *reply,size_t capacity) {
  if(beginCommandApi())return commandApi_->process(peer,input,millis(),reply,capacity);
  return unavailableReply(input,reply,capacity);
}
void Network::setupCommandUdp() {
  if(!runtime_.recovery && !portsValid(runtime_.hud.config.port,runtime_.loggerSettings.n1mmPort,
      runtime_.loggerSettings.pstPort,runtime_.loggerSettings.reportPort,runtime_.loggerSettings.commandPort)) {
    runtime_.controller.fault="command_port_conflict";return;
  }
  if(commandUdp_.localPort() && commandUdp_.localPort()!=runtime_.loggerSettings.commandPort) {
    // Reply on the old port before applying a port change.
    commandRebindAt_=millis();commandRebind_=true;return;
  }
  if(!commandUdp_.begin(runtime_.loggerSettings.commandPort,2048))runtime_.controller.fault="command_udp_bind_failed";
}
void Network::serviceCommandUdp() {
  if(commandRebind_ && uint32_t(millis()-commandRebindAt_)>=1500) {
    commandRebind_=false;
    if(!commandUdp_.begin(runtime_.loggerSettings.commandPort,2048))runtime_.controller.fault="command_udp_bind_failed";
  }
  // The radio and dispatcher are serviced by one loop; this scratch buffer
  // need not occupy the nested settings/calibration call stack.
  static uint8_t input[COMMAND_INPUT_MAX];
  uint32_t address=0;uint16_t port=0;
  const int count=commandUdp_.receiveInto(input,sizeof input,address,port);
  if(count<=0)return;
  char reply[COMMAND_REPLY_MAX+1];
  const uint64_t peer=(uint64_t(1)<<63) | (uint64_t(address)<<16) | port;
  const auto size=runCommandApi(peer,{reinterpret_cast<const char *>(input),size_t(count)},reply,sizeof reply);
  if(size)commandUdp_.send(address,port,{reply,size});
  hardware_.applyOutputs();
}
bool Network::processCommandApi(std::string_view input,uint8_t port) {
  if(input.empty() || input.front()!='@')return false;
  if(port==1)return true; // Bluetooth remains the existing GS-232 transport.
  if(port==2)diagnostics_.nativeCommandMode();
  const uint64_t peer=port==3 ? (uint64_t(1)<<62)|tcpSession_ : uint64_t(port+1);
  // A newly received command may include STOP. Never postpone its execution
  // behind a slow reader. A delimiter lets the client discard an interrupted
  // old response; completed requests can still replay their cached response.
  if(commandReplySize_ && commandReplySent_)commandInterruptedPorts_|=uint8_t(1u<<commandReplyPort_);
  const bool interrupted=(commandInterruptedPorts_ & (1u<<port))!=0;
  commandInterruptedPorts_&=uint8_t(~(1u<<port));
  const size_t prefix=interrupted?2:0;
  if(prefix) {commandReply_[0]='\r';commandReply_[1]='\n';}
  const auto size=runCommandApi(peer,input,commandReply_+prefix,COMMAND_REPLY_MAX+1);
  commandReplyPort_=port;commandReplySession_=tcpSession_;commandReplySent_=0;
  commandReplySize_=size?prefix+size+2:0;commandReplyAt_=millis();
  if(size) {commandReply_[prefix+size]='\r';commandReply_[prefix+size+1]='\n';}
  hardware_.applyOutputs();serviceCommandReplies();return true;
}
void Network::serviceCommandReplies() {
  if(!commandReplySize_)return;
  if(uint32_t(millis()-commandReplyAt_)>=5000) {
    if(commandReplyPort_==3&&commandReplySession_==tcpSession_){closeRotctld();return;}
    if(commandReplySent_)commandInterruptedPorts_|=uint8_t(1u<<commandReplyPort_);
    commandReplySize_=0;return;
  }
  const auto *data=reinterpret_cast<const uint8_t *>(commandReply_+commandReplySent_);
  const size_t count=std::min(size_t(128),commandReplySize_-commandReplySent_);
  size_t sent=0;
  if(commandReplyPort_==2)sent=diagnostics_.nativeWrite(data,count);
  else if(commandReplyPort_==0) {
    if(runtime_.controller.hardware.pins.uartTx<0) {commandReplySize_=0;return;}
    const int available=Serial0.availableForWrite();
    if(available>0)sent=uartWrite(data,std::min(count,size_t(available)));
  } else if(commandReplyPort_==3) {
    if(commandReplySession_!=tcpSession_ || !rotctldClient_ || !rotctldClient_.connected()) {commandReplySize_=0;return;}
    const int n=::send(rotctldClient_.fd(),data,count,MSG_DONTWAIT);
    if(n>0)sent=size_t(n);
    else if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR && errno!=ENOMEM && errno!=ENOBUFS) {
      closeRotctld();return;
    }
  }
  commandReplySent_+=sent;
  if(commandReplySent_==commandReplySize_)commandReplySize_=0;
}
}
