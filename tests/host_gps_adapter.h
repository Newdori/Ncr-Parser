#ifndef HOST_GPS_ADAPTER_H
#define HOST_GPS_ADAPTER_H
#include <stdint.h>

/* Stand-in for the host's already existing type. */
typedef struct HostGpsTime {
    uint16_t usYear;
    uint8_t ucMonth;
    uint8_t ucDay;
    uint8_t ucHour;
    uint8_t ucMin;
    uint8_t ucSec;
    uint32_t uiMillisecond;
    uint8_t ucClockStatus;
} HostGpsTime_t;

typedef HostGpsTime_t GpsTime_t;
#endif
