#pragma once
#include <cstddef>
#include <cstdint>
// A deterministic digest fake tests the production digest lifecycle and reject
// path; cryptographic SHA-256 itself is supplied by mbedTLS on the ESP32.
struct mbedtls_sha256_context {uint32_t sum=2166136261u;};
inline void mbedtls_sha256_init(mbedtls_sha256_context *s){*s={};}
inline int mbedtls_sha256_starts(mbedtls_sha256_context *s,int){*s={};return 0;}
inline int mbedtls_sha256_update(mbedtls_sha256_context *s,const unsigned char *p,size_t n){while(n--)s->sum=(s->sum^*p++)*16777619u;return 0;}
inline int mbedtls_sha256_finish(mbedtls_sha256_context *s,unsigned char *p){for(unsigned i=0;i<32;++i)p[i]=uint8_t(s->sum>>((i%4)*8));return 0;}
inline void mbedtls_sha256_free(mbedtls_sha256_context *){}
