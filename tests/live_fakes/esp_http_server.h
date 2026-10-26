#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#include <set>
#include <map>
#include <cerrno>
#include <algorithm>
using esp_err_t=int;
inline constexpr int ESP_OK=0,ESP_FAIL=-1,HTTP_GET=1,HTTP_POST=2;
enum httpd_ws_type_t {HTTPD_WS_TYPE_TEXT=1,HTTPD_WS_TYPE_CLOSE=8,HTTPD_WS_TYPE_PING=9,HTTPD_WS_TYPE_PONG=10};
enum httpd_ws_client_info_t {HTTPD_WS_CLIENT_INVALID,HTTPD_WS_CLIENT_HTTP,HTTPD_WS_CLIENT_WEBSOCKET};
struct httpd_ws_frame_t {bool final=true,fragmented=false;httpd_ws_type_t type=HTTPD_WS_TYPE_TEXT;uint8_t *payload=nullptr;size_t len=0;};
struct FakeHttpd;
using httpd_handle_t=FakeHttpd*;
struct httpd_req_t {void *user_ctx;int method,fd;std::string payload;httpd_ws_type_t type=HTTPD_WS_TYPE_TEXT;bool final=true;unsigned receiveCalls=0;};
struct httpd_config_t {int server_port=0,ctrl_port=0,max_open_sockets=0,max_uri_handlers=0,backlog_conn=0,stack_size=0,recv_wait_timeout=0,send_wait_timeout=0;esp_err_t(*open_fn)(httpd_handle_t,int)=nullptr;void(*close_fn)(httpd_handle_t,int)=nullptr;void *global_user_ctx=nullptr;};
struct httpd_uri_t {const char *uri=nullptr;int method=0;esp_err_t(*handler)(httpd_req_t*)=nullptr;void *user_ctx=nullptr;bool is_websocket=false,handle_ws_control_frames=false;};
inline httpd_config_t HTTPD_DEFAULT_CONFIG(){return {};}
struct FakeHttpd {httpd_config_t cfg;httpd_uri_t uri;std::vector<std::string> registrations;void(*work)(void*)=nullptr;void *context=nullptr;int failedFd=-1;std::vector<int> closed;std::vector<std::pair<int,std::string>> sent;std::set<int> websocketFds,blockedFds;int blockedFd=-1;size_t maxWrite=SIZE_MAX;std::map<int,std::string> wire,pendingWire;std::vector<std::pair<int,int>> opcodes;};
inline FakeHttpd fakeServer;
inline int fakeAbortiveCloseFd=-1;
inline unsigned fakeAbortiveCloses=0;
inline int(*fakeSendOverride)(httpd_handle_t,int,const char*,size_t,int)=nullptr;
inline esp_err_t httpd_start(httpd_handle_t *p,const httpd_config_t *cfg){fakeServer={};fakeServer.cfg=*cfg;*p=&fakeServer;return ESP_OK;}
inline esp_err_t httpd_stop(httpd_handle_t){return ESP_OK;}
inline esp_err_t httpd_register_uri_handler(httpd_handle_t s,const httpd_uri_t *u){s->uri=*u;s->registrations.emplace_back(u->uri);return ESP_OK;}
inline void *httpd_get_global_user_ctx(httpd_handle_t s){return s->cfg.global_user_ctx;}
inline esp_err_t httpd_sess_set_send_override(httpd_handle_t,int,int(*fn)(httpd_handle_t,int,const char*,size_t,int)){fakeSendOverride=fn;return ESP_OK;}
inline int httpd_req_to_sockfd(httpd_req_t *r){return r->fd;}
inline httpd_ws_client_info_t httpd_ws_get_fd_info(httpd_handle_t s,int fd){return s->websocketFds.count(fd)?HTTPD_WS_CLIENT_WEBSOCKET:HTTPD_WS_CLIENT_HTTP;}
inline esp_err_t httpd_ws_recv_frame(httpd_req_t *r,httpd_ws_frame_t *f,size_t max){++r->receiveCalls;f->type=r->type;f->final=r->final;f->len=r->payload.size();if(max){if(max<f->len)return ESP_FAIL;std::memcpy(f->payload,r->payload.data(),f->len);}return ESP_OK;}
inline esp_err_t httpd_queue_work(httpd_handle_t s,void(*fn)(void*),void *p){s->work=fn;s->context=p;return ESP_OK;}
inline esp_err_t httpd_ws_send_frame_async(httpd_handle_t s,int fd,httpd_ws_frame_t *f){if(fd==s->failedFd)return ESP_FAIL;s->sent.emplace_back(fd,std::string(reinterpret_cast<char*>(f->payload),f->len));return ESP_OK;}
inline esp_err_t httpd_ws_send_frame(httpd_req_t *r,httpd_ws_frame_t *f){return httpd_ws_send_frame_async(&fakeServer,r->fd,f);}
inline int fakeFrameSend(int fd,const char *data,size_t size){
 if(fd==fakeServer.failedFd){errno=EPIPE;return -1;}
 if(fd==fakeServer.blockedFd||fakeServer.blockedFds.count(fd)){errno=EAGAIN;return -1;}
 size=std::min(size,fakeServer.maxWrite);fakeServer.wire[fd].append(data,size);
 auto &pending=fakeServer.pendingWire[fd];pending.append(data,size);
 while(pending.size()>=2){
  size_t n=static_cast<unsigned char>(pending[1])&127,h=2;
  if(n==126){if(pending.size()<4)break;n=(static_cast<unsigned char>(pending[2])<<8)|static_cast<unsigned char>(pending[3]);h=4;}
  if(pending.size()<n+h)break;
  fakeServer.sent.emplace_back(fd,pending.substr(h,n));fakeServer.opcodes.emplace_back(fd,pending[0]&15);pending.erase(0,h+n);
 }
 return int(size);
}
inline esp_err_t httpd_sess_trigger_close(httpd_handle_t s,int fd){s->closed.push_back(fd);s->websocketFds.erase(fd);s->pendingWire.erase(fd);s->cfg.close_fn(s,fd);return ESP_OK;}
inline void pumpLive(){if(fakeServer.work){auto fn=fakeServer.work;fakeServer.work=nullptr;fn(fakeServer.context);}}
