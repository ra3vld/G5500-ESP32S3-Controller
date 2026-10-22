#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <esp_http_server.h>
inline ssize_t fakeLiveSocketSend(int fd,const void *data,size_t size,int flags){
 if(fakeServer.websocketFds.count(fd))return fakeFrameSend(fd,static_cast<const char *>(data),size);
 return ::send(fd,data,size,flags);
}
#define send fakeLiveSocketSend
