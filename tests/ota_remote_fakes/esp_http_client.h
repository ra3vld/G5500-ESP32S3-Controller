#pragma once
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <esp_heap_caps.h>
using esp_err_t=int;
inline constexpr int ESP_FAIL=-1;
struct esp_http_client_config_t {const char *url=nullptr;int timeout_ms=0,buffer_size=0,buffer_size_tx=0;int(*crt_bundle_attach)(void *)=nullptr;bool disable_auto_redirect=false;const char *user_agent=nullptr;};
struct OtaReply {std::string body;int status=200;std::string location;bool complete=true;size_t chunk=337;OtaReply(std::string b="",int s=200,std::string l=""):body(std::move(b)),status(s),location(std::move(l)){} };
inline std::map<std::string,OtaReply> otaReplies;
inline std::vector<std::string> otaRequests;
struct OtaClient {std::string url;size_t offset=0;OtaReply reply;};
using esp_http_client_handle_t=OtaClient*;
inline OtaClient *esp_http_client_init(const esp_http_client_config_t *c){if(!c->crt_bundle_attach||!c->disable_auto_redirect)return nullptr;return new OtaClient{c->url,0,{}};}
inline int esp_http_client_open(OtaClient *c,int){otaRequests.push_back(c->url);auto i=otaReplies.find(c->url);if(i==otaReplies.end())return ESP_FAIL;c->reply=i->second;c->offset=0;return ESP_OK;}
inline int64_t esp_http_client_fetch_headers(OtaClient *c){return c->reply.body.size();}
inline int esp_http_client_get_status_code(OtaClient *c){return c->reply.status;}
inline void esp_http_client_close(OtaClient *){}
inline int esp_http_client_set_redirection(OtaClient *c){if(c->reply.location.empty())return ESP_FAIL;c->url=c->reply.location;return ESP_OK;}
inline int esp_http_client_get_url(OtaClient *c,char *p,size_t n){if(c->url.size()>=n)return ESP_FAIL;std::strcpy(p,c->url.c_str());return ESP_OK;}
inline int esp_http_client_read(OtaClient *c,char *p,size_t n){n=std::min({n,c->reply.chunk,c->reply.body.size()-c->offset});std::memcpy(p,c->reply.body.data()+c->offset,n);c->offset+=n;return n;}
inline bool esp_http_client_is_complete_data_received(OtaClient *c){return c->reply.complete;}
inline void esp_http_client_cleanup(OtaClient *c){delete c;}
