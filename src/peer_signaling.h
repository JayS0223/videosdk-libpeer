#ifndef PEER_SIGNALING_H_
#define PEER_SIGNALING_H_

#include "peer_connection.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ServiceConfiguration {
  const char *mqtt_url;
  int mqtt_port;
  const char *client_id;
  const char *http_url;
  int http_port;
  const char *username;
  const char *password;
  PeerConnection *pc;
} ServiceConfiguration;

#define SERVICE_CONFIG_DEFAULT() { \
 .mqtt_url = "broker.emqx.io", \
 .mqtt_port = 8883,                \
 .client_id = "peer",              \
 .http_url = "",                   \
 .http_port = 443,                 \
 .username = "",                   \
 .password = "",                   \
 .pc = NULL                        \
}

void peer_signaling_set_config(ServiceConfiguration *config);

HTTPResponse_t peer_signaling_http_request(
    const TransportInterface_t *transport_interface,
    const char *method, size_t method_len,
    const char *host, size_t host_len,
    const char *path, size_t path_len,
    const char *auth, size_t auth_len,
    const char *body, size_t body_len);

int peer_signaling_whip_connect();

void peer_signaling_whip_disconnect();

int peer_signaling_join_channel();

void peer_signaling_leave_channel();

int peer_signaling_loop();

#ifdef __cplusplus
} 
#endif

#endif //PEER_SIGNALING_H_
