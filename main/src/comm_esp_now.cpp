#include "include/comm_esp_now.hpp"
#include "esp_mac.h"
#include <string>
#include "nvs_flash.h"
#include "nvs.h"

// --- Static variables ---
static const char *TAG_ESP_NOW = "ESP_NOW_COMMS";
static uint8_t broadcast_mac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static uint8_t my_mac[6];
static bool peer_found = false;
static nvs_handle_t nvs_handle_wifi;

// Global variables accessible from other files
uint8_t peer_mac[6] = {0};
SemaphoreHandle_t config_received_sem = xSemaphoreCreateBinary(); // Pour l'esclave
SemaphoreHandle_t config_sent_ack_sem = xSemaphoreCreateBinary(); // Pour le maître
extern bool is_master;

// --- Utility Functions ---

std::string mac_to_string(const uint8_t *mac) {
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(mac_str);
}

bool string_to_mac(const std::string &mac_str, uint8_t *mac) {
    if (mac_str.length() < 17) return false;
    
    int values[6];
    if (sscanf(mac_str.c_str(), "%02X:%02X:%02X:%02X:%02X:%02X",
               &values[0], &values[1], &values[2], 
               &values[3], &values[4], &values[5]) == 6) {
        for (int i = 0; i < 6; i++) {
            mac[i] = (uint8_t)values[i];
        }
        return true;
    }
    return false;
}

// --- ESP-NOW Callbacks ---

void on_data_recv_common(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    char message[ESP_NOW_MAX_DATA_LEN + 1] = {0};
    if (len > 0 && len <= ESP_NOW_MAX_DATA_LEN) {
        memcpy(message, data, len);
    } else {
        ESP_LOGW(TAG_ESP_NOW, "Invalid message size: %d", len);
        return;
    }
    ESP_LOGI(TAG_ESP_NOW, "📨 Message received from " MACSTR ": %s", MAC2STR(recv_info->src_addr), message);

    std::string msg_str(message);

    // --- Handle MAC Address Exchange ---
    if (msg_str.rfind("MAC:", 0) == 0) {
        std::string received_mac_str = msg_str.substr(4);
        if (received_mac_str == mac_to_string(my_mac)) {
            ESP_LOGW(TAG_ESP_NOW, "❌ Ignored own MAC broadcast.");
            return;
        }
        
        ESP_LOGI(TAG_ESP_NOW, "🔍 Peer MAC received in message: %s", received_mac_str.c_str());
        if (string_to_mac(received_mac_str, peer_mac)) {
            peer_found = true;
            ESP_LOGI(TAG_ESP_NOW, "✅ Peer MAC saved: " MACSTR, MAC2STR(peer_mac));
            
            if (!esp_now_is_peer_exist(recv_info->src_addr)) {
                esp_now_peer_info_t peer_info = {};
                memcpy(peer_info.peer_addr, recv_info->src_addr, 6);
                peer_info.channel = 1;
                peer_info.encrypt = false;
                peer_info.ifidx = WIFI_IF_STA;
                if (esp_now_add_peer(&peer_info) != ESP_OK) {
                    ESP_LOGE(TAG_ESP_NOW, "Failed to add peer: " MACSTR, MAC2STR(recv_info->src_addr));
                } else {
                    ESP_LOGI(TAG_ESP_NOW, "➕ Peer added for future communication: " MACSTR, MAC2STR(recv_info->src_addr));
                }
            }

            esp_err_t err = nvs_open("wifi", NVS_READWRITE, &nvs_handle_wifi);
            if (err == ESP_OK) {
                nvs_set_str(nvs_handle_wifi, "peer_mac", mac_to_string(peer_mac).c_str());
                nvs_commit(nvs_handle_wifi);
                nvs_close(nvs_handle_wifi);
                ESP_LOGI(TAG_ESP_NOW, "💾 Peer MAC saved to NVS.");
            }

            // Répond avec notre propre MAC pour que l'autre appareil nous trouve aussi
            std::string response = "MAC:" + mac_to_string(my_mac);
            esp_now_send(recv_info->src_addr, (uint8_t*)response.c_str(), response.length());
        }
    // --- Handle WiFi Configuration for SLAVE ---
    } else if (msg_str.rfind("CONFIG:", 0) == 0 && !is_master) {
        std::string config_data = msg_str.substr(7);
        ESP_LOGI(TAG_ESP_NOW, "🔧 WiFi Configuration received: %s", config_data.c_str());
        
        std::string ssid, password;
        size_t sep = config_data.find(',');
        if (sep != std::string::npos) {
            ssid = config_data.substr(0, sep);
            password = config_data.substr(sep + 1);
            
            ESP_LOGI(TAG_ESP_NOW, "📡 SSID: %s, 🔐 Password: [HIDDEN]", ssid.c_str());
            
            esp_err_t err = nvs_open("wifi", NVS_READWRITE, &nvs_handle_wifi);
            if (err == ESP_OK) {
                nvs_set_str(nvs_handle_wifi, "ssid", ssid.c_str());
                nvs_set_str(nvs_handle_wifi, "password", password.c_str());
                nvs_commit(nvs_handle_wifi);
                nvs_close(nvs_handle_wifi);
                ESP_LOGI(TAG_ESP_NOW, "💾 WiFi credentials saved to NVS.");
                std::string response = "OK_CONFIG";
                esp_now_send(recv_info->src_addr, (uint8_t*)response.c_str(), response.length());
                ESP_LOGI(TAG_ESP_NOW, "📤 Sent acknowledgement to master.");

                xSemaphoreGive(config_received_sem);
                ESP_LOGI(TAG_ESP_NOW, "✅ Semaphore given. Main task will now unblock.");
            } else {
                ESP_LOGE(TAG_ESP_NOW, "Failed to open NVS to save credentials: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGW(TAG_ESP_NOW, "Invalid CONFIG format received.");
        }
    } else if (msg_str == "OK_CONFIG" && is_master) {
        ESP_LOGI(TAG_ESP_NOW, "🎉 Received ACK from slave!");
        set_ack_slave_flag(true);
        xSemaphoreGive(config_sent_ack_sem);
    } else {
        ESP_LOGI(TAG_ESP_NOW, "ℹ️ Generic message received: %s", msg_str.c_str());
    }
}

void on_data_sent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
    ESP_LOGI(TAG_ESP_NOW, "📤Status: %s",
             status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

// --- Initialization and Discovery ---

void init_esp_now() {
    ESP_LOGI(TAG_ESP_NOW, "🚀 Initializing ESP-NOW...");
    esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG_ESP_NOW, "📍 My MAC: " MACSTR, MAC2STR(my_mac));
    
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_data_recv_common));
    ESP_ERROR_CHECK(esp_now_register_send_cb(on_data_sent));
}

void setup_broadcast_peer() {
    esp_now_peer_info_t broadcast_peer = {};
    memcpy(broadcast_peer.peer_addr, broadcast_mac, 6);
    broadcast_peer.channel = 1;
    broadcast_peer.encrypt = false; 
    broadcast_peer.ifidx = WIFI_IF_STA;
    
    if (esp_now_add_peer(&broadcast_peer) != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGI(TAG_ESP_NOW, "✅ Broadcast peer configured.");
    }
}

void init_esp_now_broadcast() {
    init_esp_now();
    setup_broadcast_peer();
    ESP_LOGI(TAG_ESP_NOW, "🎯 ESP-NOW initialized with broadcast configured");
}

bool load_peer_mac_from_nvs() {
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs_handle_wifi);
    if (err != ESP_OK) return false;
    
    size_t required_size = 0;
    err = nvs_get_str(nvs_handle_wifi, "peer_mac", NULL, &required_size);
    if (err == ESP_OK && required_size > 0) {
        char* mac_str = (char*)malloc(required_size);
        if (mac_str) {
            nvs_get_str(nvs_handle_wifi, "peer_mac", mac_str, &required_size);
            if (string_to_mac(std::string(mac_str), peer_mac)) {
                peer_found = true;
                ESP_LOGI(TAG_ESP_NOW, "🔄 Peer MAC loaded from NVS: " MACSTR, MAC2STR(peer_mac));
                
                if (!esp_now_is_peer_exist(peer_mac)) {
                    esp_now_peer_info_t peer_info = {};
                    memcpy(peer_info.peer_addr, peer_mac, 6);
                    peer_info.channel = 1;
                    peer_info.encrypt = false;
                    peer_info.ifidx = WIFI_IF_STA;
                    esp_err_t add_err = esp_now_add_peer(&peer_info);
                    if (add_err == ESP_OK) {
                        ESP_LOGI(TAG_ESP_NOW, "➕ Peer loaded from NVS and added for communication: " MACSTR, MAC2STR(peer_mac));
                    } else {
                        ESP_LOGE(TAG_ESP_NOW, "❌ Failed to add peer loaded from NVS: %s", esp_err_to_name(add_err));
                        free(mac_str);
                        nvs_close(nvs_handle_wifi);
                        return false;
                    }
                } else {
                    ESP_LOGI(TAG_ESP_NOW, "✅ Peer from NVS already exists in ESP-NOW list");
                }
                
                free(mac_str);
                nvs_close(nvs_handle_wifi);
                return true;
            }
            free(mac_str);
        }
    }
    nvs_close(nvs_handle_wifi);
    return false;
}

bool discover_peer_with_timeout(int timeout_seconds) {
    ESP_LOGI(TAG_ESP_NOW, "🔍 Starting peer discovery...");
    
    if (load_peer_mac_from_nvs()) {
        ESP_LOGI(TAG_ESP_NOW, "🎉 Peer found in NVS. Discovery complete.");
        return true;
    }
    
    ESP_LOGI(TAG_ESP_NOW, "No peer in NVS. Starting active discovery for %d seconds.", timeout_seconds);
    std::string message = "MAC:" + mac_to_string(my_mac);
    
    int attempts = 0;
    while (!peer_found && attempts < timeout_seconds) {
        ESP_LOGI(TAG_ESP_NOW, "📡 Broadcasting my MAC... (Attempt %d/%d)", attempts + 1, timeout_seconds);
        esp_now_send(broadcast_mac, (uint8_t*)message.c_str(), message.length());
        vTaskDelay(pdMS_TO_TICKS(1000));
        attempts++;
    }
    
    if (peer_found) {
        ESP_LOGI(TAG_ESP_NOW, "🎉 Discovery successful after %d seconds!", attempts);
    } else {
        ESP_LOGW(TAG_ESP_NOW, "⏰ Discovery timed out after %d seconds.", attempts);
    }
    return peer_found;
}

// --- Application Logic Functions ---

bool is_master_device() {
    return mac_to_string(my_mac) < mac_to_string(peer_mac);
}

void send_wifi_config_to_peer(const std::string& ssid, const std::string& password) {
    if (!peer_found) {
        ESP_LOGE(TAG_ESP_NOW, "❌ Cannot send config, peer not found! Restarting...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
        return;
    }

    std::string message = "CONFIG:" + ssid + "," + password;
    esp_err_t result;
    bool ack_received = false;

    for(int i = 0; i < 5 && !ack_received; ++i) {
        // Tenter d'envoyer le message plusieurs fois
        result = esp_now_send(peer_mac, (uint8_t*)message.c_str(), message.length());
        
        if (result == ESP_OK) {
            ESP_LOGI(TAG_ESP_NOW, "✅ Config sent successfully. Waiting for ACK... (Attempt %d)", i + 1);
            if (xSemaphoreTake(config_sent_ack_sem, pdMS_TO_TICKS(5000)) == pdTRUE) {
                ESP_LOGI(TAG_ESP_NOW, "🎉 ACK received. Both devices will now restart.");
                ack_received = true;
            } else {
                ESP_LOGW(TAG_ESP_NOW, "⏰ ACK timeout. Retrying...");
            }
        } else {
            ESP_LOGE(TAG_ESP_NOW, "❌ Error sending config: %s. Retrying...", esp_err_to_name(result));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    if (ack_received) {
        ESP_LOGI(TAG_ESP_NOW, "🎉 Mission accomplie ! Redémarrage...");
    } else {
        ESP_LOGE(TAG_ESP_NOW, "❌ Échec de la configuration après plusieurs tentatives. Redémarrage forcé...");
    }
    
    // Quoi qu'il arrive, on redémarre pour passer à la suite
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

bool wait_for_slave_config(int timeout_ms) {
    if (xSemaphoreTake(config_received_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        ESP_LOGI(TAG_ESP_NOW, "✅ Le slave a bien reçu et sauvegardé la config Wi-Fi.");
        return true;
    } else {
        ESP_LOGW(TAG_ESP_NOW, "⏰ Timeout: pas de config reçue dans le délai imparti.");
        return false;
    }
}

void set_ack_slave_flag(bool received) {
    esp_err_t err = nvs_open("wifi", NVS_READWRITE, &nvs_handle_wifi);
    if (err == ESP_OK) {
        nvs_set_u8(nvs_handle_wifi, "ack_slave", received ? 1 : 0);
        nvs_commit(nvs_handle_wifi);
        nvs_close(nvs_handle_wifi);
    }
}

bool ack_from_slave_received() {
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs_handle_wifi);
    if (err != ESP_OK) return false;
    uint8_t ack_flag = 0;
    nvs_get_u8(nvs_handle_wifi, "ack_slave", &ack_flag);
    nvs_close(nvs_handle_wifi);
    return ack_flag == 1;
}

bool wifi_credentials_exist(std::string& ssid, std::string& password) {
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs_handle_wifi);
    if (err != ESP_OK) return false;

    size_t ssid_size = 0, pass_size = 0;
    nvs_get_str(nvs_handle_wifi, "ssid", NULL, &ssid_size);
    nvs_get_str(nvs_handle_wifi, "password", NULL, &pass_size);

    if (ssid_size > 0 && pass_size > 0) {
        char* ssid_buf = (char*)malloc(ssid_size);
        char* pass_buf = (char*)malloc(pass_size);
        if (ssid_buf && pass_buf) {
            nvs_get_str(nvs_handle_wifi, "ssid", ssid_buf, &ssid_size);
            nvs_get_str(nvs_handle_wifi, "password", pass_buf, &pass_size);
            ssid = ssid_buf;
            password = pass_buf;
        }
        free(ssid_buf);
        free(pass_buf);
        nvs_close(nvs_handle_wifi);
        return true;
    }
    nvs_close(nvs_handle_wifi);
    return false;
}

void save_wifi_credentials_if_absent(const std::string& ssid, const std::string& password) {
    std::string stored_ssid, stored_password;
    if (!wifi_credentials_exist(stored_ssid, stored_password)) {
        esp_err_t err = nvs_open("wifi", NVS_READWRITE, &nvs_handle_wifi);
        if (err == ESP_OK) {
            nvs_set_str(nvs_handle_wifi, "ssid", ssid.c_str());
            nvs_set_str(nvs_handle_wifi, "password", password.c_str());
            nvs_commit(nvs_handle_wifi);
            ESP_LOGI(TAG_ESP_NOW, "💾 Credentials saved to NVS.");
            nvs_close(nvs_handle_wifi);
        }
    } else {
        ESP_LOGI(TAG_ESP_NOW, "📦 Credentials already in NVS. Skipping save.");
    }
}