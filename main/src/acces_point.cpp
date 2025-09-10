#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_mac.h"        // ⭐ Pour MACSTR et MAC2STR
#include "lwip/inet.h"      // ⭐ Pour IP4_ADDR
#include <string.h>

#define WIFI_SSID      "ESP32_AP"
#define WIFI_PASS      "12345678"
#define WIFI_CHANNEL   1
#define MAX_STA_CONN   4

static const char *TAG = "wifi_ap";  // ⭐ TAGW corrigé en TAG

// Variables globales
static esp_netif_t *netif_ap = NULL;
static bool ap_started = false;

// Event handler CORRIGÉ
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        // ✅ CORRECTION : Espacement correct et TAG au lieu de TAGW
        ESP_LOGI(TAG, "📱 Station connectée: " MACSTR " AID=%d",
                 MAC2STR(event->mac), event->aid);
        
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        // ✅ CORRECTION : Utilisation de la variable event
        ESP_LOGI(TAG, "📱 Station déconnectée: " MACSTR " AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

bool start_wifi_ap()
{
    if (ap_started) {
        ESP_LOGW(TAG, "Access Point déjà démarré");
        return true;
    }

    ESP_LOGI(TAG, "🚀 Démarrage de l'Access Point...");
    ESP_ERROR_CHECK(esp_netif_init());
    netif_ap = esp_netif_create_default_wifi_ap();
    if (!netif_ap) {
        ESP_LOGE(TAG, "❌ Échec création interface AP");
        return false;
    }
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);        // IP de l'ESP32
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);        // Gateway  
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0); // Masque réseau

    ESP_ERROR_CHECK(esp_netif_dhcps_stop(netif_ap));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(netif_ap, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(netif_ap));

    // 6. Initialisation WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 7. Enregistrement event handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // 8. Mode AP
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    // 9. Configuration AP
    wifi_config_t ap_config = {};
    strncpy((char*)ap_config.ap.ssid, WIFI_SSID, sizeof(ap_config.ap.ssid) - 1);
    strncpy((char*)ap_config.ap.password, WIFI_PASS, sizeof(ap_config.ap.password) - 1);
    ap_config.ap.ssid_len = strlen(WIFI_SSID);
    ap_config.ap.channel = WIFI_CHANNEL;
    ap_config.ap.max_connection = MAX_STA_CONN;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    
    if (strlen(WIFI_PASS) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    // 10. Application config - ✅ CORRECTION : WIFI_IF_AP au lieu de ESP_IF_WIFI_AP
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    // 11. Démarrage WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    ap_started = true;

    ESP_LOGI(TAG, "✅ Access Point démarré !");
    ESP_LOGI(TAG, "📶 SSID: %s", WIFI_SSID);
    ESP_LOGI(TAG, "🔐 Password: %s", WIFI_PASS);
    ESP_LOGI(TAG, "📡 Channel: %d", WIFI_CHANNEL);
    ESP_LOGI(TAG, "👥 Max clients: %d", MAX_STA_CONN);
    ESP_LOGI(TAG, "🌐 IP ESP32: 192.168.4.1");

    return true;
}

void get_ap_info() {
    if (!ap_started) {
        ESP_LOGW(TAG, "Access Point non démarré");
        return;
    }

    wifi_sta_list_t sta_list;
    esp_err_t err = esp_wifi_ap_get_sta_list(&sta_list);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erreur récupération liste stations: %s", esp_err_to_name(err));
        return;
    }
    
    ESP_LOGI(TAG, "📊 === INFORMATIONS ACCESS POINT ===");
    ESP_LOGI(TAG, "👥 Nombre de clients connectés: %d", sta_list.num);
    
    if (sta_list.num > 0) {
        ESP_LOGI(TAG, "📱 Liste des clients connectés:");
        for (int i = 0; i < sta_list.num; i++) {
            ESP_LOGI(TAG, "   Client %d: " MACSTR, 
                     i + 1, MAC2STR(sta_list.sta[i].mac));
        }
    } else {
        ESP_LOGI(TAG, "🚫 Aucun client connecté");
    }

    // Informations réseau
    if (netif_ap) {
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(netif_ap, &ip_info);
        ESP_LOGI(TAG, "🌐 IP ESP32: " IPSTR, IP2STR(&ip_info.ip));
        ESP_LOGI(TAG, "🚪 Gateway: " IPSTR, IP2STR(&ip_info.gw));
        ESP_LOGI(TAG, "🎭 Netmask: " IPSTR, IP2STR(&ip_info.netmask));
    }
}

void stop_wifi_ap() {
    if (!ap_started) {
        ESP_LOGW(TAG, "Access Point déjà arrêté");
        return;
    }

    ESP_LOGI(TAG, "⏹️ Arrêt de l'Access Point...");
    
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());
    
    if (netif_ap) {
        esp_netif_destroy_default_wifi(netif_ap);
        netif_ap = NULL;
    }
    
    ap_started = false;
    ESP_LOGI(TAG, "✅ Access Point arrêté");
}

// Fonctions utilitaires
int get_connected_clients_count() {
    if (!ap_started) return 0;
    
    wifi_sta_list_t sta_list;
    esp_err_t err = esp_wifi_ap_get_sta_list(&sta_list);
    return (err == ESP_OK) ? sta_list.num : 0;
}

bool is_ap_running() {
    return ap_started;
}

// Version alternative sans macros MAC (si problèmes persistent)
void get_ap_info_alternative() {
    if (!ap_started) {
        ESP_LOGW(TAG, "Access Point non démarré");
        return;
    }

    wifi_sta_list_t sta_list;
    esp_err_t err = esp_wifi_ap_get_sta_list(&sta_list);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erreur récupération liste stations");
        return;
    }
    
    ESP_LOGI(TAG, "📊 === INFORMATIONS ACCESS POINT ===");
    ESP_LOGI(TAG, "👥 Nombre de clients connectés: %d", sta_list.num);
    
    if (sta_list.num > 0) {
        ESP_LOGI(TAG, "📱 Liste des clients connectés:");
        for (int i = 0; i < sta_list.num; i++) {
            uint8_t *mac = sta_list.sta[i].mac;
            ESP_LOGI(TAG, "   Client %d: %02X:%02X:%02X:%02X:%02X:%02X", 
                     i + 1, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
    } else {
        ESP_LOGI(TAG, "🚫 Aucun client connecté");
    }
}