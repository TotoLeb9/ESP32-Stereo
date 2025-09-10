#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include <string.h>
#include "esp_mac.h"  // ⭐ AJOUT IMPORTANT pour les macros MAC

#define WIFI_SSID      "ESP32_AP"
#define WIFI_PASS      "12345678"
#define WIFI_CHANNEL   1
#define MAX_STA_CONN   4


static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data);

bool start_wifi_ap();
bool start_wifi_ap_sta();
