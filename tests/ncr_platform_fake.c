#define _POSIX_C_SOURCE 200809L
#include "ncr_test_support.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void CheckTestCondition(bool bCondition, const char *pcCondition,
                        const char *pcFile, uint32_t uiLine)
{
    if (!bCondition) {
        fprintf(stderr, "FAIL %s:%" PRIu32 ": %s\n", pcFile, uiLine, pcCondition);
        exit(EXIT_FAILURE);
    }
}

void InitializeTestClock(NcrTestClock_t *pstClock, int64_t llRealtimeMs)
{
    memset(pstClock, 0, sizeof(*pstClock));
    CHECK(0 == pthread_mutex_init(&pstClock->stMutex, NULL));
    pstClock->llRealtimeMs = llRealtimeMs;
    pstClock->ullMonotonicMs = 100000U;
}

void DestroyTestClock(NcrTestClock_t *pstClock)
{
    CHECK(0 == pthread_mutex_destroy(&pstClock->stMutex));
}

void AdvanceTestClock(NcrTestClock_t *pstClock, uint32_t uiElapsedMs)
{
    CHECK(0 == pthread_mutex_lock(&pstClock->stMutex));
    pstClock->llRealtimeMs += uiElapsedMs;
    pstClock->ullMonotonicMs += uiElapsedMs;
    CHECK(0 == pthread_mutex_unlock(&pstClock->stMutex));
}

void ShiftTestRealtime(NcrTestClock_t *pstClock, int64_t llDifferenceMs)
{
    CHECK(0 == pthread_mutex_lock(&pstClock->stMutex));
    pstClock->llRealtimeMs += llDifferenceMs;
    CHECK(0 == pthread_mutex_unlock(&pstClock->stMutex));
}

static NcrStatus ReadTestClock(void *pvContext, NcrClockStamp_t *pstStamp)
{
    NcrTestClock_t *pstClock = pvContext;
    NcrStatus iStatus = NCR_STATUS_OK;
    CHECK(0 == pthread_mutex_lock(&pstClock->stMutex));
    pstClock->uiReadCount++;
    if (pstClock->uiReadCount == pstClock->uiFailReadNumber) {
        iStatus = NCR_STATUS_CLOCK_ERROR;
    }
    else {
        pstStamp->llRealtimeMs = pstClock->llRealtimeMs;
        pstStamp->ullMonotonicMs = pstClock->ullMonotonicMs;
    }
    CHECK(0 == pthread_mutex_unlock(&pstClock->stMutex));
    return iStatus;
}

static NcrStatus SetTestClock(void *pvContext, int64_t llTargetUtcMs)
{
    NcrTestClock_t *pstClock = pvContext;
    NcrStatus iStatus = NCR_STATUS_OK;
    CHECK(0 == pthread_mutex_lock(&pstClock->stMutex));
    pstClock->uiStepCalls++;
    if (pstClock->bFailStep) {
        iStatus = NCR_STATUS_PERMISSION_DENIED;
    }
    else {
        pstClock->llRealtimeMs = llTargetUtcMs + pstClock->uiStepDurationMs;
        pstClock->ullMonotonicMs += pstClock->uiStepDurationMs;
        if (pstClock->bFailReadAfterStep) {
            pstClock->uiFailReadNumber = pstClock->uiReadCount + 1U;
        }
    }
    CHECK(0 == pthread_mutex_unlock(&pstClock->stMutex));
    return iStatus;
}

NcrPlatform_t GetTestPlatform(NcrTestClock_t *pstClock)
{
    NcrPlatform_t stPlatform = {ReadTestClock, SetTestClock, pstClock};
    return stPlatform;
}

GpsTime_t MakeTestGps(int64_t llSourceMs, NcrClockStatus iStatus)
{
    time_t tSeconds = (time_t)(llSourceMs / 1000);
    struct tm stCalendar;
    GpsTime_t stGps = {0};
    CHECK(NULL != gmtime_r(&tSeconds, &stCalendar));
    stGps.usYear = (uint16_t)(stCalendar.tm_year + 1900);
    stGps.ucMonth = (uint8_t)(stCalendar.tm_mon + 1);
    stGps.ucDay = (uint8_t)stCalendar.tm_mday;
    stGps.ucHour = (uint8_t)stCalendar.tm_hour;
    stGps.ucMin = (uint8_t)stCalendar.tm_min;
    stGps.ucSec = (uint8_t)stCalendar.tm_sec;
    stGps.uiMillisecond = (uint32_t)(llSourceMs % 1000);
    stGps.ucClockStatus = (uint8_t)iStatus;
    return stGps;
}

void EncodeTestContext(int64_t llSourceMs, uint8_t aucContext[NCR_CONTEXT_LENGTH])
{
    uint32_t uiNcr = (uint32_t)((uint64_t)((llSourceMs / 1000) % 86400) *
                               UINT64_C(90000));
    aucContext[0] = (uint8_t)(uiNcr >> 16);
    aucContext[1] = (uint8_t)(uiNcr >> 8);
    aucContext[2] = (uint8_t)uiNcr;
}

NcrSync_t *CreateTestSync(const NcrConfig_t *pstConfig, NcrTestClock_t *pstClock)
{
    NcrPlatform_t stPlatform = GetTestPlatform(pstClock);
    NcrSync_t *pstSync = NULL;
    CHECK(NCR_STATUS_OK == CreateNcrSyncWithPlatform(pstConfig, &stPlatform, &pstSync));
    return pstSync;
}

void AcquireTestClock(NcrSync_t *pstSync, NcrTestClock_t *pstClock,
                      int64_t llFirstSourceMs)
{
    uint32_t uiIndex;
    GpsTime_t stGps;
    NcrTimeDecision_t stDecision;
    for (uiIndex = 0U; uiIndex < 3U; uiIndex++) {
        if (uiIndex > 0U) {
            AdvanceTestClock(pstClock, 1000U);
        }
        stGps = MakeTestGps(llFirstSourceMs + (int64_t)uiIndex * 1000, NCR_CLOCK_LOCKED);
        CHECK(NCR_STATUS_OK == UpdateGpsTime(pstSync, &stGps, &stDecision));
    }
}
