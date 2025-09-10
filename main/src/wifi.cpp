#include "include/wifi.hpp"
#include "esp_log.h"
#include <cstring> // For strncpy

#define WIFI_STA_START_BIT BIT0  // Définir le bit ici aussi

// Define a logging tag
static const char* TAG = "WifiManager";
bool WifiManager::connected = false;
std::string WifiManager::connected_ssid = "";
int8_t WifiManager::rssi = 0;
std::string WifiManager::ip_address = "";
std::string WifiManager::ssid = "";
std::string WifiManager::password = "";

// Initialize static member
EventGroupHandle_t WifiManager::wifi_event_group;

// Variable externe pour le main
extern EventGroupHandle_t wifi_event_group;

// Constructor
WifiManager::WifiManager(){};

// Destructor
WifiManager::~WifiManager() {
    if (wifi_event_group != NULL) {
        vEventGroupDelete(wifi_event_group);
    }
}

void WifiManager::init() {
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        return;
    }
    
    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_t* netif = esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Event handlers
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    
    // Démarrer le WiFi station
    ESP_LOGI(TAG, "Starting WiFi station...");
    ESP_ERROR_CHECK(esp_wifi_start());
}

// Event Handler
void WifiManager::event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    static bool initial_connection_done = false;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi station started");
        // Définir le bit pour débloquer le main
        xEventGroupSetBits(WifiManager::wifi_event_group, WIFI_STA_START_BIT);
        // Aussi définir dans le groupe du main s'il existe
        if (wifi_event_group != NULL) {
            xEventGroupSetBits(wifi_event_group, WIFI_STA_START_BIT);
        }

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Disconnected from Wi-Fi. Retrying...");
        connected = false;

        if (!initial_connection_done) {
            xEventGroupSetBits(WifiManager::wifi_event_group, WIFI_FAIL_BIT);
        }

        esp_wifi_connect();

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;

        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        connected = true;
        initial_connection_done = true;
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            connected_ssid = std::string((char*)ap_info.ssid);
            rssi = ap_info.rssi;
        }

        char ip_buf[16];
        sprintf(ip_buf, IPSTR, IP2STR(&event->ip_info.ip));
        ip_address = std::string(ip_buf);
        xEventGroupSetBits(WifiManager::wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

bool WifiManager::connect() {
    if (ssid.empty() || password.empty()) {
        ESP_LOGE(TAG, "SSID or password not set");
        return false;
    }
    wifi_ap_record_t ap_info;
    esp_err_t status = esp_wifi_sta_get_ap_info(&ap_info);

    if (status == ESP_OK) {
        ESP_LOGW(TAG, "Already connected to %s, disconnecting first", ap_info.ssid);
        esp_wifi_disconnect();
    } else {
        ESP_LOGI(TAG, "Not connected, ensuring Wi-Fi is stopped before reconfig...");
        esp_wifi_stop();
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password.c_str(), sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mode: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set config: %s", esp_err_to_name(err));
        return false;
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Attempting to connect to SSID: %s", ssid.c_str());

    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(10000)
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "✅ Connected to %s", ssid.c_str());
        return true;
    } else {
        ESP_LOGE(TAG, "❌ Failed to connect to %s", ssid.c_str());
        return false;
    }
}


void WifiManager::disconnect() {
    if (connected) {
        ESP_LOGI(TAG, "Disconnecting from Wi-Fi...");
        ESP_ERROR_CHECK(esp_wifi_disconnect());
        ESP_ERROR_CHECK(esp_wifi_stop());
        connected = false;
        connected_ssid = "";
        ip_address = "";
        rssi = 0;
    }
}

// Scan Function
void WifiManager::scan() {
    ESP_LOGI(TAG, "Scanning for available networks...");
    
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 100,
                .max = 300
            }
        },
        .home_chan_dwell_time = 0,
        .channel_bitmap = {0},
        .coex_background_scan = false
    };

    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true)); // true = blocking call

    uint16_t num_aps = 0;
    esp_wifi_scan_get_ap_num(&num_aps);
    if (num_aps == 0) {
        ESP_LOGI(TAG, "No networks found.");
        return;
    }

    ESP_LOGI(TAG, "%d networks found:", num_aps);
    wifi_ap_record_t* ap_list = (wifi_ap_record_t*)malloc(num_aps * sizeof(wifi_ap_record_t));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&num_aps, ap_list));

    for (int i = 0; i < num_aps; i++) {
        ESP_LOGI(TAG, "%d: %s (%d dBm)", i + 1, ap_list[i].ssid, ap_list[i].rssi);
    }
    
    free(ap_list);
}

// isConnected Function
bool WifiManager::isConnected() {
    return connected;
}