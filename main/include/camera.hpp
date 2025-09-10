#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39

#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15

#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13
#include <stdint.h>
#include <string>
#define CHUNK_SIZE 768
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h> // Pour htons et htonl
#include "esp_camera.h"
#define RTP_VERSION 2
#define RTP_PAYLOAD_TYPE_JPEG 26
struct Header {
    uint16_t frameStart;     // e.g. FRAME_START_MARKER
    uint32_t frameId;        // un compteur ou timestamp pour l'image
    uint16_t totalChunks;
    uint16_t chunkIndex;
    uint16_t chunkSize;
    uint16_t checksum;
    uint32_t timestamp;
};
#pragma pack(push, 1)
struct udp_chunk_header_t {
    uint32_t magic;        // Doit valoir 0xABCDEF01
    uint32_t frame_id;     // ID unique de la frame
    uint32_t total_size;   // Taille totale de l'image JPEG
    uint16_t chunk_idx;    // Index du chunk [0..total_chunks-1]
    uint16_t total_chunks; // Nombre total de chunks dans cette frame
    uint16_t payload_len;  // Longueur des données utiles dans ce paquet
};
#pragma pack(pop)

static const uint32_t UDP_MAGIC = 0xABCDEF01;


struct FrameBuffer {
    camera_fb_t* fb = nullptr;
    uint32_t frameId = 0;
};

class Camera {
public:
    Camera();
    bool init();
    bool streamFrameUDP(int sock, struct sockaddr_in* remoteAddr);

private:
    uint32_t _imageId = 0;
    uint8_t _jpeg_quality = 15;
    bool _isInitialized;
    FrameBuffer buffers[2];
    uint8_t currentBuffer = 0;
    uint32_t _packet_loss_count;
    uint32_t _retry_count; 
    uint32_t _total_chunks_sent;
    

    uint16_t calculateChecksum(const uint8_t* data, size_t length);
    bool sendFrameUDP(FrameBuffer& buffer, int sock, struct sockaddr_in* remoteAddr);
    void startStreaming(int sock, struct sockaddr_in* remoteAddr);
    int sendChunkWithRetry(int sock, const void* data, size_t len, 
                              const struct sockaddr* addr, socklen_t addrlen, 
                              uint16_t chunk_idx, uint32_t frame_id);
};