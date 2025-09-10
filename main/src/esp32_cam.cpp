#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "include/wifi.hpp"
#include "include/memory.hpp"
#include "include/camera.hpp"
#include "include/ntp_sync.hpp"
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include "include/acces_point.hpp"
#include "include/web_server.hpp"
#include "include/comm_esp_now.hpp"

const char* remoteIp = "10.99.7.87";
const int CAPTURE_INTERVAL_MS = 100;
bool is_master;
static const char* TAG = "ESP32_CAM";
int udp_socket;
struct sockaddr_in destAddr;
WifiManager* wifiManager = nullptr;
TaskHandle_t sendImageTaskHandle = NULL;
Camera* camera = nullptr;
void send_image_udp_task(void *pvParameters);
void check_wifi_task(void *pvParameters);
bool master_sent_config = false;

class NetworkMonitor {
private:
    uint32_t bytes_sent_total = 0;
    uint32_t frames_sent_total = 0;
    uint32_t chunks_failed_total = 0;
    uint32_t last_reset_time = 0;
    
public:
    void recordFrame(size_t bytes, bool success, int failed_chunks) {
        bytes_sent_total += bytes;
        frames_sent_total++;
        if (failed_chunks > 0) {
            chunks_failed_total += failed_chunks;
        }
        
        uint32_t now = get_timestamp_ms();
        if (now - last_reset_time >= 10000) { 
            float mbps = (bytes_sent_total * 8.0f) / (10.0f * 1000.0f * 1000.0f);
            float fps = frames_sent_total / 10.0f;
            float chunk_fail_rate = (chunks_failed_total > 0) ? 
                ((float)chunks_failed_total / (frames_sent_total * 15)) * 100.0f : 0.0f;
            
            ESP_LOGI("NET_MONITOR", "📈 Network: %.2f Mbps, %.1f FPS, %.1f%% chunk failures", 
                     mbps, fps, chunk_fail_rate);
            
            if (chunk_fail_rate > 10.0f) {
                ESP_LOGW("NET_MONITOR", "💡 High failure rate - consider reducing image quality");
            }
            if (fps < 14.5f) {
                ESP_LOGW("NET_MONITOR", "💡 Low FPS - check processing time or network");
            }
            if (mbps > 5.0f) {
                ESP_LOGW("NET_MONITOR", "💡 High bandwidth usage - monitor network capacity");
            }
            
            bytes_sent_total = 0;
            frames_sent_total = 0;
            chunks_failed_total = 0;
            last_reset_time = now;
        }
    }
};

NetworkMonitor network_monitor;

void init_udp_socket() {
    udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (udp_socket < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket: %s", strerror(errno));
        return;
    }
    Memory wifi_nvs("wifi");
    wifi_nvs.open(NVS_READWRITE);
    int remotePort = 0;
    if (wifi_nvs.nvs_get("remote", remotePort) != ESP_OK || remotePort == 0) {
        remotePort = (is_master ? 12345 : 12346);
        wifi_nvs.nvs_set("remote", remotePort);
    }

    int send_buffer_size = 128*1024;  
    int recv_buffer_size = 8192;
    ESP_LOGI(TAG, "🔑 REMOTE PORT %d.",remotePort);
    
    if (setsockopt(udp_socket, SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size)) < 0) {
        ESP_LOGW(TAG, "Failed to set send buffer: %s", strerror(errno));
    } else {
        ESP_LOGI(TAG, "✅ UDP send buffer: %d bytes", send_buffer_size);
    }
    
    if (setsockopt(udp_socket, SOL_SOCKET, SO_RCVBUF, &recv_buffer_size, sizeof(recv_buffer_size)) < 0) {
        ESP_LOGW(TAG, "Failed to set recv buffer: %s", strerror(errno));
    }
    
    int nodelay = 1;
    if (setsockopt(udp_socket, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
    }
    
    int reuse = 1;
    if (setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        ESP_LOGW(TAG, "Failed to set SO_REUSEADDR: %s", strerror(errno));
    }
    
    memset(&destAddr, 0, sizeof(destAddr));
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(remotePort);
    destAddr.sin_addr.s_addr = inet_addr(remoteIp);
    
    ESP_LOGI(TAG, "✅ UDP socket optimized for %s:%d (15 FPS streaming)", remoteIp, remotePort);
}

void check_wifi_task(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    WifiManager *localWifiManager = static_cast<WifiManager*>(pvParameters);
    bool wasConnected = localWifiManager->isConnected();
    bool udp_initialized = false;

    ESP_LOGI(TAG, "🔍 Monitoring WiFi status (initial: %s)", wasConnected ? "Connected" : "Disconnected");

    for(;;) {
        bool isConnectedNow = localWifiManager->isConnected();

        if (isConnectedNow && !wasConnected) {
            ESP_LOGI(TAG, "📶 Wi-Fi reconnected! Setting up UDP and camera task...");
            if (!udp_initialized) {
                init_udp_socket();
                ntp_init();
                udp_initialized = true;
            }
            if (sendImageTaskHandle == NULL) {
                xTaskCreatePinnedToCore(send_image_udp_task, "Send Image UDP Task", 
                                        10240, camera, 5, &sendImageTaskHandle, 1);
                ESP_LOGI(TAG, "📷 Camera task created");
            } else {
                vTaskResume(sendImageTaskHandle);
                ESP_LOGI(TAG, "📷 Camera task resumed");
            }
            wasConnected = true;
        } else if (!isConnectedNow && wasConnected) {
            ESP_LOGW(TAG, "📶 Wi-Fi disconnected! Suspending camera task...");
            if (sendImageTaskHandle != NULL) {
                vTaskSuspend(sendImageTaskHandle);
            }
            wasConnected = false;
        } else if (!isConnectedNow && !wasConnected) {
            ESP_LOGW(TAG, "🔄 Wi-Fi still down. Attempting reconnection...");
            if (localWifiManager->connect()) {
                ESP_LOGI(TAG, "✅ Reconnected to: %s", localWifiManager->getConnectedSsid().c_str());
            }
        }
        
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(5000));
    }
}


void send_image_udp_task(void *pvParameters) {
    ESP_LOGI(TAG, "🎬 Starting high-FPS streaming (30+ FPS target)");
    Camera *cam = static_cast<Camera*>(pvParameters);
    
    TickType_t frame_period = pdMS_TO_TICKS(CAPTURE_INTERVAL_MS);
    TickType_t next_frame_time = xTaskGetTickCount();
    
    int adaptive_quality = 20; 
    int consecutive_failures = 0;
    
    for (;;) {
        // Gestion précise du timing
        vTaskDelayUntil(&next_frame_time, frame_period);
        next_frame_time += frame_period;
        
        if (!wifiManager->isConnected()) {
            ESP_LOGV(TAG, "📶 No WiFi - skipping frame"); 
            continue;
        }
        
        if (consecutive_failures > 2) { 
            adaptive_quality = std::min(30, adaptive_quality + 3); 
            sensor_t *s = esp_camera_sensor_get();
            if (s) s->set_quality(s, adaptive_quality);
            ESP_LOGW(TAG, "📉 Reducing quality to %d due to failures", adaptive_quality);
            consecutive_failures = 0;
        } else if (consecutive_failures == 0 && adaptive_quality > 15) {
            adaptive_quality = std::max(15, adaptive_quality - 1);
            sensor_t *s = esp_camera_sensor_get();
            if (s) s->set_quality(s, adaptive_quality);
        }
        
        bool success = cam->streamFrameUDP(udp_socket, &destAddr);
        
        if (success) {
            consecutive_failures = 0;
        } else {
            consecutive_failures++;
        }
        
    }
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "🚀 Starting ESP32 CAM application...");
    esp_err_t ret = nvs_flash_init();
    
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing and reinitializing NVS...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    camera = new Camera();
    if (!camera->init()) {
        ESP_LOGE(TAG, "❌ Camera initialization failed! Restarting...");
        esp_restart();
    }
    ESP_LOGI(TAG, "📷 Camera initialized successfully");

    wifiManager = new WifiManager();
    wifiManager->init();
    init_esp_now_broadcast();
    
    ESP_LOGI(TAG, "🤝 Starting ESP-NOW peer discovery...");
    bool peer_discovered = discover_peer_with_timeout(30);
    is_master = false;
    if (peer_discovered) {
        is_master = is_master_device();
        ESP_LOGI(TAG, "🎯 Device role determined: %s", is_master ? "MASTER 👑" : "SLAVE 🤖");
    } else {
        ESP_LOGW(TAG, "⚠️ No peer found. Will operate in standalone mode.");
    }

    Memory wifi_nvs("wifi");
    Memory config_nvs("config");
    config_nvs.open(NVS_READWRITE);
    wifi_nvs.open(NVS_READWRITE);
    std::string ssid, password;
    int remote_port=0;
    if (wifi_nvs.nvs_get("remote", remote_port) != ESP_OK) {
        if (is_master) {
            remote_port = 12345;
        } else {
            remote_port = 12346;
        }
        wifi_nvs.nvs_set("remote", remote_port);
    }
    
    if (wifi_nvs.nvs_get("ssid", ssid) != ESP_OK || wifi_nvs.nvs_get("password", password) != ESP_OK) {
        ESP_LOGI(TAG, "🔑 No WiFi credentials found in NVS.");
        
        if (peer_discovered && is_master) {
            ESP_LOGW(TAG, "📡 MASTER: Basculant en mode AP pour la configuration...");
            esp_wifi_stop();
            
            if (start_wifi_ap()) {
                httpd_handle_t server = start_webserver();
                if (!server) {
                    ESP_LOGE(TAG, "❌ Web server failed to start. Restarting.");
                    esp_restart();
                }
                ESP_LOGI(TAG, "🌐 AP + Web server running. Connect to 'ESP32-CAM-AP' and go to 192.168.4.1");
            } else {
                ESP_LOGE(TAG, "❌ AP mode failed to start. Restarting.");
                esp_restart();
            }
        } else if (peer_discovered && !is_master) {
            ESP_LOGI(TAG, "📡 SLAVE: Waiting for CONFIG from master via ESP-NOW...");
            if (xSemaphoreTake(config_received_sem, portMAX_DELAY) == pdTRUE) {
                ESP_LOGI(TAG, "✅ CONFIG received! Restarting to connect to WiFi...");
                esp_restart();
            }
        } else {
            ESP_LOGE(TAG, "❌ No peer found and no WiFi credentials. Cannot proceed. Please configure one device.");
        }

    } 
    else if (is_master && peer_discovered && !ack_from_slave_received()) {
    ESP_LOGW(TAG, "📡 No ACK from slave - sending credentials...");
    send_wifi_config_to_peer(ssid, password);
    }
    else {
        ESP_LOGI(TAG, "🔑 WiFi credentials found in NVS. Connecting to '%s'", ssid.c_str());
        wifiManager->setSsid(ssid);
        wifiManager->setPassword(password);
        
        xTaskCreatePinnedToCore(check_wifi_task, "WiFi Monitor", 4096, wifiManager, 5, NULL, 0);

        if (!wifiManager->connect()) {
            ESP_LOGW(TAG, "⚠️ Initial connection failed - WiFi monitor task will keep retrying.");
        }
    }
    
    ESP_LOGI(TAG, "🎉 Setup complete - system is running.");
}