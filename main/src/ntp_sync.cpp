#include "include/ntp_sync.hpp"


static const char *TAG = "NTP";
static bool time_synced = false;

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "NTP synchronisé");
    time_synced = true;
}

void ntp_init(void)
{
    ESP_LOGI(TAG, "Initialisation NTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_setservername(2, "fr.pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

time_t get_timestamp(void)
{
    time_t now;
    time(&now);
    return now;
}

uint64_t get_timestamp_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)(tv.tv_sec) * 1000LL + (uint64_t)(tv.tv_usec) / 1000LL;
}

bool is_ntp_synced(void)
{
    return time_synced && (get_timestamp() > 1600000000);
}

void ntp_resync(void)
{
    ESP_LOGI(TAG, "Resynchronisation NTP");
    sntp_restart();
}

void stop_ntp(void)
{
    ntp_stop();
    time_synced = false;
}