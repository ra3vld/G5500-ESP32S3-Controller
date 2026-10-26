#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <esp_http_server.h>
inline ssize_t fakeLiveSocketSend(int fd,const void *data,size_t size,int flags){
 if(fakeServer.websocketFds.count(fd))return fakeFrameSend(fd,static_cast<const char *>(data),size);
 return ::send(fd,data,size,flags);
}
inline int fakeLiveSetSockOpt(int fd,int level,int option,const void *value,socklen_t size){
 if(level==SOL_SOCKET&&option==SO_LINGER&&size==sizeof(linger)){
  const auto &mode=*static_cast<const linger *>(value);
  if(mode.l_onoff&&mode.l_linger==0){fakeAbortiveCloseFd=fd;++fakeAbortiveCloses;}
 }
 return ::setsockopt(fd,level,option,value,size);
}
#define send fakeLiveSocketSend
#define setsockopt fakeLiveSetSockOpt
