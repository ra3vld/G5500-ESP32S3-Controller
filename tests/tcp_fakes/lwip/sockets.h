#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cerrno>
#include <cassert>
inline bool fakeLingerSupported=false;
inline unsigned fakeLingerRequests=0;
inline int fakeNativeReset(int fd) {
  linger mode{1,0};return ::setsockopt(fd,SOL_SOCKET,SO_LINGER,&mode,sizeof mode);
}
inline int fakeTcpSetSockOpt(int fd,int level,int option,const void *value,socklen_t size) {
  if(level==SOL_SOCKET && option==SO_LINGER) {
    ++fakeLingerRequests;
    if(!fakeLingerSupported){errno=ENOPROTOOPT;return -1;}
  }
  return ::setsockopt(fd,level,option,value,size);
}
#define setsockopt fakeTcpSetSockOpt
