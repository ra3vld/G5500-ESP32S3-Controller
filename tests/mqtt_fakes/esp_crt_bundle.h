#pragma once
#define CONFIG_MBEDTLS_CERTIFICATE_BUNDLE 1
#define CONFIG_MQTT_TRANSPORT_SSL 1
inline int esp_crt_bundle_attach(void *){return 0;}
