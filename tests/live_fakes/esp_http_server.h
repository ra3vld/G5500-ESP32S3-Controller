#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#include <set>
using esp_err_t=int;
inline constexpr int ESP_OK=0,ESP_FAIL=-1,HTTP_GET=1,HTTP_POST=2;
enum httpd_ws_type_t {HTTPD_WS_TYPE_TEXT=1,HTTPD_WS_TYPE_CLOSE=8};
enum httpd_ws_client_info_t {HTTPD_WS_CLIENT_INVALID,HTTPD_WS_CLIENT_HTTP,HTTPD_WS_CLIENT_WEBSOCKET};
struct httpd_ws_frame_t {bool final=true,fragmented=false;httpd_ws_type_t type=HTTPD_WS_TYPE_TEXT;uint8_t *payload=nullptr;size_t len=0;};
struct FakeHttpd;
using httpd_handle_t=FakeHttpd*;
struct httpd_req_t {void *user_ctx;int method,fd;std::string payload;};
struct httpd_config_t {int server_port=0,ctrl_port=0,max_open_sockets=0,max_uri_handlers=0,stack_size=0,recv_wait_timeout=0,send_wait_timeout=0;esp_err_t(*open_fn)(httpd_handle_t,int)=nullptr;void(*close_fn)(httpd_handle_t,int)=nullptr;void *global_user_ctx=nullptr;};
struct httpd_uri_t {const char *uri=nullptr;int method=0;esp_err_t(*handler)(httpd_req_t*)=nullptr;void *user_ctx=nullptr;bool is_websocket=false;};
inline httpd_config_t HTTPD_DEFAULT_CONFIG(){return {};}
struct FakeHttpd {httpd_config_t cfg;httpd_uri_t uri;void(*work)(void*)=nullptr;void *context=nullptr;int failedFd=-1;std::vector<int> closed;std::vector<std::pair<int,std::string>> sent;std::set<int> websocketFds;};
inline FakeHttpd fakeServer;
inline esp_err_t httpd_start(httpd_handle_t *p,const httpd_config_t *cfg){fakeServer={};fakeServer.cfg=*cfg;*p=&fakeServer;return ESP_OK;}
inline esp_err_t httpd_stop(httpd_handle_t){return ESP_OK;}
inline esp_err_t httpd_register_uri_handler(httpd_handle_t s,const httpd_uri_t *u){s->uri=*u;return ESP_OK;}
inline void *httpd_get_global_user_ctx(httpd_handle_t s){return s->cfg.global_user_ctx;}
inline int httpd_req_to_sockfd(httpd_req_t *r){return r->fd;}
inline httpd_ws_client_info_t httpd_ws_get_fd_info(httpd_handle_t s,int fd){return s->websocketFds.count(fd)?HTTPD_WS_CLIENT_WEBSOCKET:HTTPD_WS_CLIENT_HTTP;}
inline esp_err_t httpd_ws_recv_frame(httpd_req_t *r,httpd_ws_frame_t *f,size_t max){f->len=r->payload.size();if(max){if(max<f->len)return ESP_FAIL;std::memcpy(f->payload,r->payload.data(),f->len);}return ESP_OK;}
inline esp_err_t httpd_queue_work(httpd_handle_t s,void(*fn)(void*),void *p){s->work=fn;s->context=p;return ESP_OK;}
inline esp_err_t httpd_ws_send_frame_async(httpd_handle_t s,int fd,httpd_ws_frame_t *f){if(fd==s->failedFd)return ESP_FAIL;s->sent.emplace_back(fd,std::string(reinterpret_cast<char*>(f->payload),f->len));return ESP_OK;}
inline esp_err_t httpd_sess_trigger_close(httpd_handle_t s,int fd){s->closed.push_back(fd);s->websocketFds.erase(fd);s->cfg.close_fn(s,fd);return ESP_OK;}
inline void pumpLive(){if(fakeServer.work){auto fn=fakeServer.work;fakeServer.work=nullptr;fn(fakeServer.context);}}
