#include "camera.hpp" // Adaptez le chemin si nécessaire
#include "esp_log.h"
#include "esp_camera.h"
#include <string.h> // Pour memcpy
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Tag pour les logs ESP-IDF
static const char *TAG = "CAMERA";

// Le constructeur initialise les variables membres.
Camera::Camera() : 
    _imageId(0), 
    _isInitialized(false), 
    currentBuffer(0) 
{
    // S'assurer que les pointeurs de framebuffer sont nuls au départ
    buffers[0].fb = nullptr;
    buffers[1].fb = nullptr;
}

// Initialise la configuration du module caméra.
bool Camera::init() {
    // Configuration de la caméra basée sur les pins définies dans le .hpp
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 24000000;
    config.pixel_format = PIXFORMAT_JPEG; // Indispensable pour le streaming
    config.frame_size = FRAMESIZE_VGA;    // Taille de l'image (VGA, SVGA, etc.)
    config.jpeg_quality = 30;  // Qualité JPEG (0-63, plus bas = meilleure qualité)
    config.fb_count = 3;                  // On a besoin d'au moins 2 framebuffers pour le double-buffering
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_PSRAM;

    // Initialisation de la caméra
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "L'initialisation de la caméra a échoué avec l'erreur 0x%x", err);
        return false;
    }

    // Récupération du capteur pour des réglages fins
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        // Appliquer des réglages pour améliorer la qualité d'image
        s->set_quality(s, _jpeg_quality);
        s->set_framesize(s, FRAMESIZE_QVGA);
        s->set_vflip(s, 1); // Retournement vertical de l'image si nécessaire
        s->set_hmirror(s, 1); // Effet miroir horizontal si nécessaire
    }

    _isInitialized = true;
    ESP_LOGI(TAG, "Caméra initialisée avec succès.");
    return true;
}

// Calcule un simple checksum sur 16 bits.
// NOTE: Cette fonction est implémentée mais non utilisée car le header `udp_chunk_header_t` n'a pas de champ checksum.
uint16_t Camera::calculateChecksum(const uint8_t* data, size_t length) {
    uint32_t sum = 0;
    for (size_t i = 0; i < length; i++) {
        sum += data[i];
    }
    return (uint16_t)(sum & 0xFFFF);
}

bool Camera::sendFrameUDP(FrameBuffer& buffer, int sock, struct sockaddr_in* remoteAddr) {
    // Si le framebuffer est invalide ou vide, on le libère et on arrête
    if (!buffer.fb || buffer.fb->len == 0) {
        if (buffer.fb) {
            esp_camera_fb_return(buffer.fb);
            buffer.fb = nullptr;
        }
        return false;
    }

    const size_t totalSize = buffer.fb->len;
    const uint16_t totalChunks = (totalSize + CHUNK_SIZE - 1) / CHUNK_SIZE;
    
    ESP_LOGD(TAG, "Envoi frame %u: %zu bytes, %d chunks", 
             buffer.frameId, totalSize, totalChunks);

    uint8_t packetBuffer[sizeof(udp_chunk_header_t) + CHUNK_SIZE];
    uint16_t chunks_sent_ok = 0;
    uint16_t chunks_failed = 0;

    for (uint16_t i = 0; i < totalChunks; i++) {
        size_t offset = i * CHUNK_SIZE;
        uint16_t payloadLen = (totalSize - offset) > CHUNK_SIZE ? CHUNK_SIZE : (totalSize - offset);

        udp_chunk_header_t header;
        header.magic = htonl(UDP_MAGIC);
        header.frame_id = htonl(buffer.frameId);
        header.total_size = htonl(totalSize);
        header.total_chunks = htons(totalChunks);
        header.chunk_idx = htons(i);
        header.payload_len = htons(payloadLen);

        memcpy(packetBuffer, &header, sizeof(header));
        memcpy(packetBuffer + sizeof(header), buffer.fb->buf + offset, payloadLen);

        // Envoi avec retry
        int sent_len = sendChunkWithRetry(sock, packetBuffer, sizeof(header) + payloadLen, 
                                         (struct sockaddr*)remoteAddr, sizeof(*remoteAddr), 
                                         i, buffer.frameId);
        
        if (sent_len > 0) {
            chunks_sent_ok++;
        } else {
            chunks_failed++;
        }

        if (i % 10 == 0 && i > 0) {  
            int delay_ms = chunks_failed > (i / 4) ? 2 : 1;
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
        
        if (i % 20 == 0) {
            taskYIELD();
        }
    }

    float success_rate = (float)chunks_sent_ok / totalChunks * 100.0f;
    if (chunks_failed > 0) {
        ESP_LOGW(TAG, "Frame %u: %d/%d chunks envoyés (%.1f%%), %d échecs", 
                 buffer.frameId, chunks_sent_ok, totalChunks, success_rate, chunks_failed);
    } else {
        ESP_LOGD(TAG, "Frame %u: %d chunks envoyés avec succès", 
                 buffer.frameId, chunks_sent_ok);
    }

    esp_camera_fb_return(buffer.fb);
    buffer.fb = nullptr;

    return success_rate > 75.0f;  
}

int Camera::sendChunkWithRetry(int sock, const void* data, size_t len, 
                              const struct sockaddr* addr, socklen_t addrlen, 
                              uint16_t chunk_idx, uint32_t frame_id) {
    const int MAX_RETRIES = 3;
    int retry_count = 0;
    
    while (retry_count <= MAX_RETRIES) {
        int result = sendto(sock, data, len, 0, addr, addrlen);
        
        if (result >= 0) {
            _total_chunks_sent++;
            if (retry_count > 0) {
                _retry_count++;
                ESP_LOGD(TAG, "Chunk %d frame %u envoyé après %d retry(s)", 
                        chunk_idx, frame_id, retry_count);
            }
            return result;
        }
        
        switch (errno) {
            case EWOULDBLOCK:
                retry_count++;
                if (retry_count <= MAX_RETRIES) {
                    int delay_ms = 1 << (retry_count - 1);
                    vTaskDelay(pdMS_TO_TICKS(delay_ms));
                    ESP_LOGD(TAG, "Retry %d/%d pour chunk %d frame %u (errno=%d)", 
                            retry_count, MAX_RETRIES, chunk_idx, frame_id, errno);
                } else {
                    ESP_LOGW(TAG, "Échec définitif chunk %d frame %u après %d tentatives", 
                            chunk_idx, frame_id, MAX_RETRIES);
                    _packet_loss_count++;
                }
                break;
                
        
            case EHOSTUNREACH:
                ESP_LOGE(TAG, "Erreur réseau critique pour chunk %d frame %u: %s", 
                        chunk_idx, frame_id, strerror(errno));
                return -1;
                
            default:
                ESP_LOGE(TAG, "Erreur sendto() inconnue pour chunk %d frame %u: errno=%d (%s)", 
                        chunk_idx, frame_id, errno, strerror(errno));
                return -1;
        }
    }
    
    return -1;  // Échec après tous les retries
}

bool Camera::streamFrameUDP(int sock, struct sockaddr_in* remoteAddr) {
    if (!_isInitialized) {
        ESP_LOGE(TAG, "La caméra n'est pas initialisée.");
        return false;
    }

    // Capture de la nouvelle image
    buffers[currentBuffer].fb = esp_camera_fb_get();
    if (!buffers[currentBuffer].fb) {
        ESP_LOGE(TAG, "Échec de la capture d'image.");
        return false;
    }
    buffers[currentBuffer].frameId = _imageId++;

    // Envoyer seulement s'il y a une image précédente
    if (buffers[1 - currentBuffer].fb != nullptr) {
        sendFrameUDP(buffers[1 - currentBuffer], sock, remoteAddr);
    }

    currentBuffer = 1 - currentBuffer;
    return true;
}

// Boucle de streaming infinie, à lancer dans une tâche FreeRTOS dédiée.
void Camera::startStreaming(int sock, struct sockaddr_in* remoteAddr) {
    if (!_isInitialized) {
        ESP_LOGE(TAG, "Impossible de démarrer le streaming, caméra non initialisée.");
        return;
    }
    
    // Boucle infinie pour streamer en continu
    while (true) {
        if (!streamFrameUDP(sock, remoteAddr)) {
            // Si la capture a échoué, petite pause avant de réessayer
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        // Un petit délai pour contrôler le framerate et laisser du temps aux autres tâches
        vTaskDelay(pdMS_TO_TICKS(1)); 
    }
}