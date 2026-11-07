#include "ncr_internal.h"
#include <stddef.h>

static bool CheckLeapYear(uint16_t usYear)
{
    return (0U == usYear % 4U) &&
           ((0U != usYear % 100U) || (0U == usYear % 400U));
}

static uint32_t GetMonthDays(uint16_t usYear, uint8_t ucMonth)
{
    static const uint8_t aucMonthDays[12] = {
        31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U
    };
    uint32_t uiDays = aucMonthDays[ucMonth - 1U];
    if ((2U == ucMonth) && CheckLeapYear(usYear)) {
        uiDays++;
    }
    return uiDays;
}

NcrStatus ValidateGpsTime(const GpsTime_t *pstGpsTime)
{
    if ((NULL == pstGpsTime) || (pstGpsTime->usYear < 2000U) ||
        (pstGpsTime->usYear > 2099U) || (pstGpsTime->ucMonth < 1U) ||
        (pstGpsTime->ucMonth > 12U) || (pstGpsTime->ucDay < 1U) ||
        (pstGpsTime->ucHour > 23U) || (pstGpsTime->ucMin > 59U) ||
        (pstGpsTime->ucSec > 59U) || (pstGpsTime->uiMillisecond > 999U) ||
        (pstGpsTime->ucClockStatus > (uint8_t)NCR_CLOCK_HOLDOVER)) {
        return NCR_STATUS_INVALID_GPS;
    }
    if (pstGpsTime->ucDay > GetMonthDays(pstGpsTime->usYear, pstGpsTime->ucMonth)) {
        return NCR_STATUS_INVALID_GPS;
    }
    return NCR_STATUS_OK;
}

NcrStatus ConvertGpsTimeToSourceMs(const GpsTime_t *pstGpsTime,
                                  int64_t *pllSourceMs)
{
    uint16_t usYear;
    uint8_t ucMonth;
    int64_t llDays = 0;
    int64_t llSeconds;
    if (NULL == pllSourceMs) {
        return NCR_STATUS_BAD_ARGUMENT;
    }
    *pllSourceMs = 0;
    if (NCR_STATUS_OK != ValidateGpsTime(pstGpsTime)) {
        return NCR_STATUS_INVALID_GPS;
    }
    for (usYear = 1970U; usYear < pstGpsTime->usYear; usYear++) {
        llDays += CheckLeapYear(usYear) ? 366 : 365;
    }
    for (ucMonth = 1U; ucMonth < pstGpsTime->ucMonth; ucMonth++) {
        llDays += GetMonthDays(pstGpsTime->usYear, ucMonth);
    }
    llDays += (int64_t)pstGpsTime->ucDay - 1;
    llSeconds = llDays * 86400 + (int64_t)pstGpsTime->ucHour * 3600 +
                (int64_t)pstGpsTime->ucMin * 60 + pstGpsTime->ucSec;
    *pllSourceMs = llSeconds * 1000 + pstGpsTime->uiMillisecond;
    return NCR_STATUS_OK;
}

bool ConvertNcrSourceMsToDate(int64_t llSourceMs, uint16_t *pusYear,
                             uint8_t *pucMonth, uint8_t *pucDay)
{
    uint16_t usYear = 2000U;
    uint8_t ucMonth = 1U;
    int64_t llDays;
    uint32_t uiDays;
    if ((NULL == pusYear) || (NULL == pucMonth) || (NULL == pucDay) ||
        (llSourceMs < NCR_MIN_SOURCE_MS) || (llSourceMs >= NCR_END_SOURCE_MS)) {
        return false;
    }
    llDays = (llSourceMs - NCR_MIN_SOURCE_MS) / NCR_MS_PER_DAY;
    uiDays = CheckLeapYear(usYear) ? 366U : 365U;
    while (llDays >= (int64_t)uiDays) {
        llDays -= uiDays;
        usYear++;
        uiDays = CheckLeapYear(usYear) ? 366U : 365U;
    }
    while (llDays >= (int64_t)GetMonthDays(usYear, ucMonth)) {
        llDays -= GetMonthDays(usYear, ucMonth);
        ucMonth++;
    }
    *pusYear = usYear;
    *pucMonth = ucMonth;
    *pucDay = (uint8_t)(llDays + 1);
    return true;
}
