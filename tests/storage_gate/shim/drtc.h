#ifndef SHIM_DRTC_H
#define SHIM_DRTC_H
#include <stdint.h>
#include <stdbool.h>
uint64_t rtc_get_timeStamp(void);
uint64_t rtc_get_local_timestamp(void);
bool rtc_setup_by_timestamp(uint64_t timestamp, int timezone_offset_hours);
#endif
