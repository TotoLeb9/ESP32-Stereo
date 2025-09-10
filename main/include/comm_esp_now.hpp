#include "esp_now.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_log.h"
#include <string>
#include "esp_wifi_types.h"

extern SemaphoreHandle_t config_received_sem; // Pour l'esclave
extern SemaphoreHandle_t config_sent_ack_sem; // Pour le maître
extern uint8_t peer_mac[6];
void on_data_recv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);
// Callback appelé quand un message est envoyé
std::string mac_to_string(const uint8_t *mac);
void send_wifi_config_to_peer(const std::string& ssid, const std::string& password);

bool string_to_mac(const std::string &mac_str, uint8_t *mac) ;

void on_data_recv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);

void on_data_sent(const wifi_tx_info_t *info, esp_now_send_status_t status);
void init_esp_now();
bool load_peer_mac_from_nvs();
bool discover_peer_with_timeout(int timeout_seconds);
bool wait_for_slave_config(int timeout_ms);
esp_err_t send_my_mac_broadcast();
void set_ack_slave_flag(bool received);
bool ack_from_slave_received();
bool wifi_credentials_exist(std::string& ssid, std::string& password);
void setup_broadcast();
void init_esp_now_broadcast();

bool is_master_device();