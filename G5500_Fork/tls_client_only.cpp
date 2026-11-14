// Small-flash profiles expose HTTP servers and TLS clients only. GPL-3.0.
#if G5500_TLS_CLIENT_ONLY
#include <mbedtls/ssl.h>
// esp-tls selects MBEDTLS_SSL_IS_CLIENT for HTTPS. The SDK dispatcher also
// references the server implementation; fail closed instead of linking that
// unreachable server handshake. Certificate verification stays unchanged.
extern "C" int __wrap_mbedtls_ssl_handshake_server_step(mbedtls_ssl_context *) {
  return MBEDTLS_ERR_SSL_FEATURE_UNAVAILABLE;
}
#endif
