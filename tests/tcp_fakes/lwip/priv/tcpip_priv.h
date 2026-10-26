#pragma once
#include <lwip/tcpip.h>
struct tcpip_api_call_data {uint8_t dummy=0;};
inline err_t fakeTcpApiResult=ERR_OK;
inline unsigned fakeTcpApiCalls=0;
inline err_t tcpip_api_call(err_t (*callback)(tcpip_api_call_data *),tcpip_api_call_data *data) {
  ++fakeTcpApiCalls;
  if(fakeTcpApiResult!=ERR_OK)return fakeTcpApiResult;
  LOCK_TCPIP_CORE();const auto result=callback(data);UNLOCK_TCPIP_CORE();return result;
}
