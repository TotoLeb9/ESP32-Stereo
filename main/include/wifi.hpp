#pragma once

#include <string>
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"


class WifiManager {
public:
    WifiManager();
    ~WifiManager();
    bool connect();
    void disconnect();
    void scan();
    bool isConnected();
    std::string getSsid() const { return ssid; }
    std::string getConnectedSsid() const { return connected_ssid; }
    int8_t getRssi() const { return rssi; }
    std::string getIpAddress() const { return ip_address; }
    std::string getPassword() const { return password; }
    void setSsid(const std::string& newSsid) { ssid = newSsid; }
    void setPassword(const std::string& newPassword) { password = newPassword; }
    void init();

private:
    static std::string ssid;
    static std::string password;
    static std::string connected_ssid;
    static std::string ip_address;
    static int8_t rssi;
    static bool connected;

    static EventGroupHandle_t wifi_event_group;
    
    static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

    
    
    static const int WIFI_CONNECTED_BIT = BIT0;
    static const int WIFI_FAIL_BIT = BIT1;
    
    int retry_count;
};