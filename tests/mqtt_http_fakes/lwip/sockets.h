#pragma once
#include "WebServer.h"
#include <sys/socket.h>
#include <cerrno>
inline int mqttHttpRecv(int fd,void *out,size_t size,int){
  if(fd<0||testSocket.readError){errno=EBADF;return -1;}
  const auto count=std::min({size,testSocket.maxRead,testSocket.input.size()-testSocket.consumed});
  if(!count){if(testSocket.peerClosed)return 0;errno=EAGAIN;return -1;}
  std::memcpy(out,testSocket.input.data()+testSocket.consumed,count);testSocket.consumed+=count;return int(count);
}
inline int mqttHttpSend(int fd,const void *data,size_t size,int){
  if(fd<0)return -1;
  if(testSocket.blockWrites||!testSocket.sendQuota){errno=EAGAIN;return -1;}
  size=std::min(size,testSocket.sendQuota);testSocket.sendQuota-=size;
  testSocket.output.append(static_cast<const char *>(data),size);return int(size);
}
inline int mqttHttpSockOpt(int,int,int,const void *,size_t){return 0;}
#define recv mqttHttpRecv
#define send mqttHttpSend
#define setsockopt mqttHttpSockOpt
