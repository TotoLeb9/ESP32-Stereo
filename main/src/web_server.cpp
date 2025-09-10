#include "esp_http_server.h"
#include "esp_log.h"
#include <string>
#include <vector>
#include "include/web_server.hpp"
#include "nvs.h"
#include "include/comm_esp_now.hpp" // For send_wifi_config_to_peer

static const char *TAG_WEBSERVER = "WEBSERVER";

// Variable globale pour stocker le handle du serveur
static httpd_handle_t global_server_handle = NULL;

// Helper to decode URL-encoded strings
static std::string url_decode(const std::string& str) {
    std::string decoded_string;
    char ch;
    int i, ii;
    for (i = 0; i < str.length(); i++) {
        if (str[i] == '%') {
            sscanf(str.substr(i + 1, 2).c_str(), "%x", &ii);
            ch = static_cast<char>(ii);
            decoded_string += ch;
            i = i + 2;
        } else if (str[i] == '+') {
            decoded_string += ' ';
        } else {
            decoded_string += str[i];
        }
    }
    return decoded_string;
}

// Tâche de redémarrage (fonction C normale)
static void restart_task(void* param) {
    ESP_LOGI(TAG_WEBSERVER, "🔄 Preparing to restart...");
    
    // Attendre que la réponse HTTP soit envoyée
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Arrêter le serveur web
    if (global_server_handle) {
        ESP_LOGI(TAG_WEBSERVER, "🛑 Stopping web server...");
        httpd_stop(global_server_handle);
        global_server_handle = NULL;
    }
    
    ESP_LOGI(TAG_WEBSERVER, "🔁 Restarting ESP32...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    
    vTaskDelete(NULL);
}

// Handler for the root URL, redirects to the form
static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/form");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// Handler to serve the HTML form
static esp_err_t form_get_handler(httpd_req_t *req) {
    const char form_html[] = R"rawliteral(
        <!DOCTYPE html>
        <html>
        <head>
            <title>ESP32-CAM Setup</title>
            <meta name="viewport" content="width=device-width, initial-scale=1">
            <style>
                body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }
                .container { max-width: 400px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
                h1 { color: #333; text-align: center; }
                form { display: flex; flex-direction: column; }
                label { margin-top: 10px; color: #555; font-weight: bold; }
                input[type="text"], input[type="password"] { margin-bottom: 15px; padding: 12px; font-size: 1em; border: 1px solid #ddd; border-radius: 4px; }
                input[type='submit'] { background-color: #4CAF50; color: white; border: none; cursor: pointer; padding: 12px; font-size: 1.1em; border-radius: 4px; }
                input[type='submit']:hover { background-color: #45a049; }
                .info { background-color: #e7f3ff; padding: 10px; border-radius: 4px; margin-bottom: 15px; font-size: 0.9em; }
            </style>
        </head>
        <body>
            <div class="container">
                <h1>📶 WiFi Configuration</h1>
                <div class="info">
                    Connectez votre ESP32-CAM à votre réseau WiFi principal.
                </div>
                <form action="/submit" method="post">
                    <label for="ssid">Nom du réseau (SSID):</label>
                    <input type="text" id="ssid" name="ssid" placeholder="Mon_WiFi" required>
                    
                    <label for="password">Mot de passe:</label>
                    <input type="password" id="password" name="password" placeholder="••••••••" required>
                    
                    <input type="submit" value="💾 Sauvegarder et Redémarrer">
                </form>
            </div>
        </body>
        </html>
    )rawliteral";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, form_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Handler for the form submission
static esp_err_t submit_post_handler(httpd_req_t *req) {
    char buf[512];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf)) {
        ESP_LOGE(TAG_WEBSERVER, "❌ Payload too large");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Payload too large");
        return ESP_FAIL;
    }

    // Read the POST data
    if ((ret = httpd_req_recv(req, buf, remaining)) <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    ESP_LOGI(TAG_WEBSERVER, "📨 Received form data: %s", buf);

    char ssid[64] = {0};
    char password[128] = {0};

    if (httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid)) == ESP_OK &&
        httpd_query_key_value(buf, "password", password, sizeof(password)) == ESP_OK) {
        
        std::string decoded_ssid = url_decode(ssid);
        std::string decoded_password = url_decode(password);

        ESP_LOGI(TAG_WEBSERVER, "📡 Received SSID: '%s'", decoded_ssid.c_str());
        ESP_LOGI(TAG_WEBSERVER, "🔐 Password length: %d characters", decoded_password.length());

        // Sauvegarder les credentials
        nvs_handle_t nvs_handle_wifi;
        esp_err_t err = nvs_open("wifi", NVS_READWRITE, &nvs_handle_wifi);
        if (err == ESP_OK) {
            ESP_LOGI(TAG_WEBSERVER, "📂 NVS partition opened successfully");
            
            err = nvs_set_str(nvs_handle_wifi, "ssid", decoded_ssid.c_str());
            if (err == ESP_OK) {
                ESP_LOGI(TAG_WEBSERVER, "✅ SSID saved to NVS");
                err = nvs_set_str(nvs_handle_wifi, "password", decoded_password.c_str());
                if (err == ESP_OK) {
                    ESP_LOGI(TAG_WEBSERVER, "✅ Password saved to NVS");
                    err = nvs_commit(nvs_handle_wifi);
                    if (err == ESP_OK) {
                        ESP_LOGI(TAG_WEBSERVER, "✅ NVS commit successful");
                    } else {
                        ESP_LOGE(TAG_WEBSERVER, "❌ NVS commit failed: %s", esp_err_to_name(err));
                    }
                } else {
                    ESP_LOGE(TAG_WEBSERVER, "❌ Failed to save password: %s", esp_err_to_name(err));
                }
            } else {
                ESP_LOGE(TAG_WEBSERVER, "❌ Failed to save SSID: %s", esp_err_to_name(err));
            }
            nvs_close(nvs_handle_wifi);
            
            if (err == ESP_OK) {
                ESP_LOGI(TAG_WEBSERVER, "💾 WiFi credentials successfully saved to NVS!");
                
                // Envoyer une belle page de confirmation
                const char* resp_str = R"rawliteral(
                <!DOCTYPE html>
                <html>
                <head>
                    <title>Configuration réussie</title>
                    <meta name="viewport" content="width=device-width, initial-scale=1">
                    <style>
                        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f8ff; text-align: center; }
                        .container { max-width: 400px; margin: 50px auto; background: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
                        h1 { color: #4CAF50; }
                        .success { color: #4CAF50; font-size: 1.2em; margin: 20px 0; }
                        .spinner { border: 4px solid #f3f3f3; border-top: 4px solid #4CAF50; border-radius: 50%; width: 40px; height: 40px; animation: spin 2s linear infinite; margin: 20px auto; }
                        @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
                    </style>
                </head>
                <body>
                    <div class="container">
                        <h1>✅ Configuration réussie!</h1>
                        <div class="success">Les paramètres WiFi ont été sauvegardés.</div>
                        <div class="spinner"></div>
                        <p>L'ESP32 va redémarrer et se connecter à votre réseau WiFi...</p>
                        <p><small>Cette page se fermera automatiquement.</small></p>
                    </div>
                </body>
                </html>
                )rawliteral";
                
                httpd_resp_set_type(req, "text/html");
                httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
                
                // Créer la tâche de redémarrage
                xTaskCreate(restart_task, "restart_task", 4096, NULL, 5, NULL);
                
                return ESP_OK;
            } else {
                ESP_LOGE(TAG_WEBSERVER, "❌ Error saving to NVS: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGE(TAG_WEBSERVER, "❌ Error opening NVS: %s", esp_err_to_name(err));
        }
        
        // En cas d'erreur NVS
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS storage error");
        return ESP_FAIL;
    }

    ESP_LOGE(TAG_WEBSERVER, "❌ Failed to parse form data");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form data");
    return ESP_FAIL;
}

// Function to start the web server
httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_resp_headers = 8;
    config.max_uri_handlers = 8;

    ESP_LOGI(TAG_WEBSERVER, "🚀 Starting web server...");
    if (httpd_start(&server, &config) == ESP_OK) {
        // Stocker le handle globalement
        global_server_handle = server;
        
        httpd_uri_t root_uri = { "/", HTTP_GET, root_get_handler, NULL };
        httpd_register_uri_handler(server, &root_uri);

        httpd_uri_t form_uri = { "/form", HTTP_GET, form_get_handler, NULL };
        httpd_register_uri_handler(server, &form_uri);

        httpd_uri_t submit_uri = { "/submit", HTTP_POST, submit_post_handler, NULL };
        httpd_register_uri_handler(server, &submit_uri);
        
        ESP_LOGI(TAG_WEBSERVER, "✅ Web server started successfully");
        return server;
    }

    ESP_LOGE(TAG_WEBSERVER, "❌ Error starting server!");
    return NULL;
}