#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/time.h>
#include "esp_event.h"
#include "esp_log.h"
#include "videosdk_audio.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "mdns.h"
#include "peer.h"
#include "videosdk.h"
#include <core_http_client.h>
#include "peer_signaling.h"
#include "ssl_transport.h"
#define MAX_HTTP_OUTPUT_BUFFER 512
static const char* TAG = "videosdk";
char* local_meetingID = NULL;
char* local_token = NULL;
bool local_enableMic = true;

struct MemoryStruct {
    char *memory;
    size_t size;
};

 PeerConnection* g_pc;
 PeerConnectionState eState = PEER_CONNECTION_CLOSED;
static TaskHandle_t xPcTaskHandle = NULL;
static TaskHandle_t xLoopTaskHandle = NULL;
SemaphoreHandle_t xSemaphore = NULL;

static TaskHandle_t xAudioTaskHandle = NULL;
extern esp_err_t videosdk_audio_init();
extern void videosdk_audio_task(void* pvParameters);


static videosdk_on_connection_state_changed_cb on_connection_state_changed_cb = NULL;
void videosdk_set_connection_state_changed_cb(videosdk_on_connection_state_changed_cb cb) {
  on_connection_state_changed_cb = cb;
}

int64_t get_timestamp() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL));
}


static void oniceconnectionstatechange(PeerConnectionState state, void* user_data) {
  ESP_LOGI(TAG, " changed: %d (%s)", state, peer_connection_state_to_string(state));
  eState = state;

  if (on_connection_state_changed_cb) {
    on_connection_state_changed_cb(state);  // Invoke the user-defined callback
  }

  switch (state) {
    case PEER_CONNECTION_CONNECTED:
      ESP_LOGI(TAG, "DTLS handshake completed, connection is now CONNECTED");
      break;
    case PEER_CONNECTION_COMPLETED:
      ESP_LOGI(TAG, "ICE and DTLS completed, connection is now COMPLETED");
      break;
    case PEER_CONNECTION_FAILED:
      ESP_LOGE(TAG, "PeerConnection FAILED");
      break;
    case PEER_CONNECTION_CLOSED:
      ESP_LOGW(TAG, "PeerConnection CLOSED");
      break;
    default:
      break;
  }
}

void peer_connection_task(void* arg) {
  ESP_LOGI(TAG, "peer_connection_task started");

  for (;;) {
    if (xSemaphoreTake(xSemaphore, portMAX_DELAY)) {
      ESP_LOGD(TAG, "Calling peer_connection_loop, current state: %d (%s)", eState, peer_connection_state_to_string(eState));
      peer_connection_loop(g_pc);
      ESP_LOGI(TAG, "PeerConnection state after loop: %d (%s)", eState, peer_connection_state_to_string(eState));
      xSemaphoreGive(xSemaphore);
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void videosdk_publish(){
  ESP_LOGI(TAG, "Audio init  stream...");
  videosdk_audio_init();
  ESP_LOGI(TAG, "Audio initialized");

}
void videosdk_task(){
  #if defined(CONFIG_ESP32S3_XIAO_SENSE)
  StackType_t* stack_memory = (StackType_t*)heap_caps_malloc(8192 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
  StaticTask_t task_buffer;
  ESP_LOGI(TAG, "Creating videosdk task with stack size: %d", 8192 * sizeof(StackType_t));
  if (stack_memory) {
    ESP_LOGI(TAG, "Creating audio task with stack size: %d", 8192 * sizeof(StackType_t));
    xAudioTaskHandle = xTaskCreateStaticPinnedToCore(videosdk_audio_task, "audio", 8192, NULL, 7, stack_memory, &task_buffer, 0);
    ESP_LOGI(TAG, "Audio task created: %p", xAudioTaskHandle);
  }
#endif
ESP_LOGI(TAG, "Creating peer connection task with stack size: %d", 8192 * sizeof(StackType_t));
  if (xPcTaskHandle) {
    ESP_LOGI(TAG, "Peer connection task already exists, deleting it before creating a new one");
    vTaskDelete(xPcTaskHandle);
  }
  if (xLoopTaskHandle) {
    ESP_LOGI(TAG, "Loop task already exists, deleting it before creating a new one");
    vTaskDelete(xLoopTaskHandle);
  }
ESP_LOGI(TAG, "Creating peer connection task with stack size: %d", 4096 * sizeof(StackType_t));

  xTaskCreatePinnedToCore(peer_connection_task, "peer_connection", 8192, NULL, 5, &xPcTaskHandle, 1);
 while(1){
   vTaskDelay(pdMS_TO_TICKS(10));
 }


}


esp_err_t videosdk_init(const char* meetingId, const char* token, const char* displayName) {

local_meetingID = meetingId;
local_token = token;
local_enableMic = enableMic;
  ESP_LOGI(TAG, "Initializing Video SDK with meetingId: %s, token: %s, enableMic: %d, displayName: %s",
           local_meetingID, local_token, local_enableMic, displayName);
  // Initialize audio if microphone is enabled
xSemaphore = xSemaphoreCreateMutex();
  PeerConfiguration config = {
    .ice_servers = {
        {.urls = "stun:stun.l.google.com:19302"},
    },
    .audio_codec = local_enableMic ? CODEC_OPUS : CODEC_NONE

  };
peer_init();
videosdk_audio_init();

  ESP_LOGI(TAG, "Initializing Video SDK...");

  g_pc = peer_connection_create(&config);
  if (!g_pc) {
    ESP_LOGE(TAG, "Failed to create PeerConnection!");
    return ESP_FAIL;
  }
ESP_LOGI(TAG, "PeerConnection created: %p", g_pc);
peer_connection_oniceconnectionstatechange(g_pc, oniceconnectionstatechange);

  ESP_LOGI(TAG, "Video SDK initialized successfully");
  return ESP_OK;
}





esp_err_t videosdk_connect() {

  ServiceConfiguration service_config = SERVICE_CONFIG_DEFAULT();
  service_config.pc = g_pc;

#if defined(CONFIG_WHIP_URL)
  service_config.http_url = CONFIG_WHIP_URL;
  service_config.http_port = CONFIG_WHIP_PORT;
 // service_config.bearer_token = CONFIG_WHIP_BEARER_TOKEN;
#else
  service_config.client_id = deviceid;
  service_config.mqtt_url = "broker.emqx.io";
#endif
  ESP_LOGI(TAG, "Connecting to signaling server...");
  peer_signaling_set_config(&service_config);
  
  peer_signaling_whip_connect();
  ESP_LOGI(TAG, "Peer signaling configuration set (WHIP)");
  return ESP_OK;
}




void videosdk_subscribe(ServiceConfiguration service_config) {
  if (!g_pc) {
    ESP_LOGE(TAG, "PeerConnection is not initialized");
    return;
  }
  // Set the service configuration for signaling
  // peer_signaling_set_config(&service_config);
  // ESP_LOGI(TAG, "Subscribing to video stream...");
  // peer_signaling_whip_connect();
  ESP_LOGI(TAG, "Subscribed to video stream");
}

void videosdk_disconnect(){

}


#define API_HOST "api.videosdk.live"
#define API_PORT 443
static struct {
  char http_buf[8192]; // or bigger if needed
  // other fields
} g_ps;
char* create_meeting(const char *auth_token) {
  TransportInterface_t trans_if = {0};
  NetworkContext_t net_ctx;
  HTTPResponse_t res;

  const char *body = "";  // Empty body for POST
  int ret;

  trans_if.recv = ssl_transport_recv;
  trans_if.send = ssl_transport_send;
  trans_if.pNetworkContext = &net_ctx;

  ret = ssl_transport_connect(&net_ctx, API_HOST, API_PORT, NULL);
  if (ret < 0) {
    ESP_LOGE(TAG, "Connection failed to %s:%d", API_HOST, API_PORT);
    return NULL;
  }

  ESP_LOGI(TAG, "Calling VideoSDK /v2/rooms...");
  res = peer_signaling_http_request(
    &trans_if,
    "POST", strlen("POST"),
    API_HOST, strlen(API_HOST),
    "/v2/rooms", strlen("/v2/rooms"),
    auth_token, strlen(auth_token),
    body, strlen(body)
  );

  ssl_transport_disconnect(&net_ctx);

  if (res.pBody == NULL || res.statusCode != 200) {
    ESP_LOGE(TAG, "Create meeting failed. HTTP Status: %u", res.statusCode);
    return NULL;
  }

  ESP_LOGI(TAG, "Response: %s", res.pBody);

  // Simple extraction of "roomId" from JSON response
  // Assumes format: {"roomId":"abc123"} (naive parser for demo)
const char *body_str = (const char *)res.pBody;
char *room_id_ptr = strstr(body_str, "\"roomId\":\"");
  if (!room_id_ptr) return NULL;

  room_id_ptr += strlen("\"roomId\":\""); // Move past the key
  char *end_quote = strchr(room_id_ptr, '"');
  if (!end_quote) return NULL;

  size_t room_id_len = end_quote - room_id_ptr;
  char *room_id = (char *)malloc(room_id_len + 1);
  strncpy(room_id, room_id_ptr, room_id_len);
  room_id[room_id_len] = '\0';

  return room_id;
}

char* validate_meeting(const char *auth_token, const char *meetingId) {
  TransportInterface_t trans_if = {0};
  NetworkContext_t net_ctx;
  HTTPResponse_t res;

  const char *body = "";  // Empty body for POST
  int ret;

  trans_if.recv = ssl_transport_recv;
  trans_if.send = ssl_transport_send;
  trans_if.pNetworkContext = &net_ctx;

  ret = ssl_transport_connect(&net_ctx, API_HOST, API_PORT, NULL);
  if (ret < 0) {
    ESP_LOGE(TAG, "Connection failed to %s:%d", API_HOST, API_PORT);
    return NULL;
  }
  char path[128];
  snprintf(path, sizeof(path), "/v2/rooms/validate/%s", meetingId);
  ESP_LOGI(TAG, "Calling VideoSDK /v2/rooms...");
  res = peer_signaling_http_request(
    &trans_if,
    "GET", strlen("GET"),
    API_HOST, strlen(API_HOST),
    path, strlen(path),
    auth_token, strlen(auth_token),
    body, strlen(body)
  );

  ssl_transport_disconnect(&net_ctx);

  if (res.pBody == NULL || res.statusCode != 200) {
    ESP_LOGE(TAG, "Create meeting failed. HTTP Status: %u", res.statusCode);
    return NULL;
  }

  ESP_LOGI(TAG, "Response: %s", res.pBody);

  // Simple extraction of "roomId" from JSON response
  // Assumes format: {"roomId":"abc123"} (naive parser for demo)
const char *body_str = (const char *)res.pBody;
char *room_id_ptr = strstr(body_str, "\"roomId\":\"");
  if (!room_id_ptr) return NULL;

  room_id_ptr += strlen("\"roomId\":\""); // Move past the key
  char *end_quote = strchr(room_id_ptr, '"');
  if (!end_quote) return NULL;

  size_t room_id_len = end_quote - room_id_ptr;
  char *room_id = (char *)malloc(room_id_len + 1);
  strncpy(room_id, room_id_ptr, room_id_len);
  room_id[room_id_len] = '\0';

  return room_id;
}