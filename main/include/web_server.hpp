#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"

typedef struct {
    char ssid[64];
    char password[64];
    char ip[16];
    int port;
} form_data_t;

extern form_data_t g_form_data;
extern SemaphoreHandle_t g_form_mutex;


httpd_handle_t start_webserver(void);
