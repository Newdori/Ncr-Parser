#ifndef NCR_GPS_TIME_H
#define NCR_GPS_TIME_H

#include <stdint.h>

/* An optional host header must define GpsTime_t with these named fields. */
#ifdef NCR_GPS_TIME_HEADER
#include NCR_GPS_TIME_HEADER
#else
typedef struct GpsTime {
    uint16_t usYear;
    uint8_t ucMonth;
    uint8_t ucDay;
    uint8_t ucHour;
    uint8_t ucMin;
    uint8_t ucSec;
    uint32_t uiMillisecond;
    uint8_t ucClockStatus;
} GpsTime_t;
#endif

#endif
