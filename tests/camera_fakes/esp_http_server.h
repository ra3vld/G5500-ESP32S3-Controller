#pragma once
#include "esp_camera.h"
#include <map>
#include <string>
#include <cstring>
using httpd_handle_t=void *;
struct httpd_req_t { std::string status,type,body; std::map<std::string,std::string> headers; };
struct httpd_config_t { int server_port,ctrl_port,task_priority,stack_size,max_open_sockets,max_uri_handlers; bool lru_purge_enable; int recv_wait_timeout,send_wait_timeout; };
#define HTTPD_DEFAULT_CONFIG() httpd_config_t{}
inline constexpr int HTTP_GET=0,HTTPD_RESP_USE_STRLEN=-1;
struct httpd_uri_t { const char *uri=nullptr; int method=0; int (*handler)(httpd_req_t *)=nullptr; };
inline httpd_config_t fakeHttpConfig{};
inline httpd_uri_t fakeEndpoint{};
inline bool fakeHttpStartOk=true,fakeHttpRegisterOk=true,fakeHttpSendOk=true;
inline unsigned fakeHttpStops=0;
inline int httpd_start(httpd_handle_t *handle,const httpd_config_t *config) { fakeHttpConfig=*config; *handle=reinterpret_cast<void *>(1); return fakeHttpStartOk?0:-1; }
inline int httpd_stop(httpd_handle_t) { ++fakeHttpStops;return 0; }
inline int httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t *uri) { fakeEndpoint=*uri;return fakeHttpRegisterOk?0:-1; }
inline int httpd_resp_set_hdr(httpd_req_t *r,const char *name,const char *value) { r->headers[name]=value;return 0; }
inline int httpd_resp_set_type(httpd_req_t *r,const char *type) { r->type=type;return 0; }
inline int httpd_resp_set_status(httpd_req_t *r,const char *status) { r->status=status;return 0; }
inline int httpd_resp_send(httpd_req_t *r,const char *data,int size) { r->body.assign(data,size<0?std::strlen(data):size); return fakeHttpSendOk?0:-1; }
