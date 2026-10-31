#pragma once
#include "WebServer.h"
#include <cerrno>
inline constexpr int MSG_DONTWAIT=1, SOL_SOCKET=1, SO_LINGER=2;
struct linger { int l_onoff,l_linger; };
inline int recv(int fd,void *out,size_t size,int) {
  if(fd<0 || testSocket.readError) { errno=EBADF;return -1; }
  const auto count=std::min({size,testSocket.maxRead,testSocket.input.size()-testSocket.consumed});
  if(!count) { if(testSocket.peerClosed) return 0;errno=EAGAIN;return -1; }
  std::memcpy(out,testSocket.input.data()+testSocket.consumed,count);
  testSocket.consumed+=count;return int(count);
}
inline int send(int fd,const void *data,size_t size,int) {
  testSocket.largestSend=std::max(testSocket.largestSend,size);
  if(fd<0) return -1;
  if(testSocket.writeError) { errno=testSocket.writeError;return -1; }
  if(testSocket.blockWrites || !testSocket.sendQuota) { errno=EAGAIN;return -1; }
  size=std::min(size,testSocket.sendQuota);
  if(size>testSocket.queueCapacity-testSocket.outstanding) { errno=ENOMEM;return -1; }
  testSocket.sendQuota-=size;
  testSocket.outstanding+=size;
  testSocket.output.append(static_cast<const char *>(data),size);return int(size);
}
inline int setsockopt(int,int,int,const void *,size_t) { testSocket.reset=true;return 0; }
