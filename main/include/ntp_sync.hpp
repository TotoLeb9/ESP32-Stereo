#ifndef NTP_SYNC_H
#define NTP_SYNC_H

#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_sntp.h"
#include <stdbool.h>

void ntp_init(void);
time_t get_timestamp(void);
uint64_t get_timestamp_ms(void);
uint64_t get_timestamp_us(void);
bool is_ntp_synced(void);
void ntp_resync(void);
void ntp_stop(void);

#endif