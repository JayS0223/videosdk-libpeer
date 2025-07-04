#ifndef VIDEOSDK_H
#define VIDEOSDK_H
#include <stdbool.h> 
#include "esp_err.h"
typedef void (*videosdk_on_connection_state_changed_cb)(PeerConnectionState state);
esp_err_t videosdk_init(const char* meetingId, const char* token, const char* displayName);
esp_err_t videosdk_connect();
void videosdk_task();
char* create_meeting( const char *auth_token);
char* validate_meeting(
    const char *auth_token, const char *meetingId);
#endif

