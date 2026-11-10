#define _POSIX_C_SOURCE 200809L
#include "ncr_sync.h"

#if !defined(__linux__)
#error This module requires an Ubuntu/Linux target compiler
#endif

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

_Static_assert(sizeof(time_t) >= sizeof(int64_t), "Build with 64-bit time_t");

#define NCR_MS_PER_DAY INT64_C(86400000)
#define NCR_MIN_SOURCE_MS INT64_C(946684800000)
#define NCR_END_SOURCE_MS INT64_C(4102444800000)
#define NCR_MAX_REALTIME_MS INT64_C(253402300799999)

typedef struct NcrTimeState {
    bool bHasSample;
    bool bHasLocked;
    bool bQualified;
    bool bReacquiring;
    bool bResetRequired;
    bool bSourceHoldover;
    int64_t llLastSourceMs;
    uint64_t ullLastSampleMonoMs;
    NcrClockStamp_t stLastLocked;
    int64_t llObservedOffsetMs;
    int64_t llTrustedOffsetMs;
    int64_t llPreviousStepOffsetMs;
    uint64_t ullSourceHoldoverStartMs;
    uint32_t uiAcquireCount;
    uint32_t uiStepCount;
    NcrClockStatus iSourceStatus;
} NcrTimeState_t;

struct NcrSync {
    pthread_mutex_t stMutex;
    NcrConfig_t stConfig;
    NcrPlatform_t stPlatform;
    NcrTimeState_t stTime;
    NcrStatus iLastUpdateStatus;
    NcrGpsEvents_t stGpsEvents;
    uint64_t ullLastGpsMessageMonoMs;
    bool bHasClockStamp;
    NcrClockStamp_t stLastClockStamp;
    uint64_t ullPolicyGeneration;
};

/* Configuration and shared arithmetic. */
NcrConfig_t GetNcrDefaultConfig(void)
{
    NcrConfig_t stConfig = {0};
    stConfig.uiAcquireSamples = 3U;
    stConfig.uiConfirmSteps = 3U;
    stConfig.uiStepThresholdMs = 500U;
    stConfig.uiMaxSampleJitterMs = 250U;
    stConfig.uiGpsMessageTimeoutMs = 3000U;
    stConfig.uiMaxSourceHoldoverMs = 300000U;
    stConfig.uiMaxLocalHoldoverMs = 300000U;
    stConfig.uiMaxClockOffsetMs = 2000U;
    stConfig.uiMaxUncertaintyMs = 2000U;
    stConfig.uiSourceUncertaintyMs = 1U;
    stConfig.uiReceiveUncertaintyMs = 1U;
    stConfig.uiLocalDriftPpm = 100U;
    stConfig.uiMaxLinkDelayMs = 5000U;
    stConfig.uiMaxQueueAgeMs = 5000U;
    stConfig.uiMaxClockJumpMs = 1000U;
    stConfig.uiMaxReacquireOffsetMs = 60000U;
    return stConfig;
}

NcrStatus ValidateNcrConfig(const NcrConfig_t *pstConfig)
{
    uint64_t ullBaseUncertainty;
    if (NULL == pstConfig) {
        return NCR_STATUS_BAD_CONFIG;
    }
    ullBaseUncertainty = (uint64_t)pstConfig->uiSourceUncertaintyMs +
                        pstConfig->uiReceiveUncertaintyMs +
                        pstConfig->uiDelayUncertaintyMs;
    if ((pstConfig->iSourceMinusUtcSec < -86400) ||
        (pstConfig->iSourceMinusUtcSec > 86400) ||
        (pstConfig->uiTimeMessageDelayMs > 60000U) ||
        (0U == pstConfig->uiAcquireSamples) ||
        (pstConfig->uiAcquireSamples > 1000U) ||
        (0U == pstConfig->uiConfirmSteps) ||
        (pstConfig->uiConfirmSteps > 1000U) ||
        (0U == pstConfig->uiGpsMessageTimeoutMs) ||
        (0U == pstConfig->uiMaxSourceHoldoverMs) ||
        (0U == pstConfig->uiMaxLocalHoldoverMs) ||
        (0U == pstConfig->uiMaxQueueAgeMs) ||
        (0U == pstConfig->uiMaxClockJumpMs) ||
        (0U == pstConfig->uiMaxReacquireOffsetMs) ||
        (pstConfig->uiMaxClockOffsetMs > pstConfig->uiMaxReacquireOffsetMs) ||
        (pstConfig->uiStepThresholdMs > pstConfig->uiMaxReacquireOffsetMs) ||
        (pstConfig->uiLocalDriftPpm > 1000000U) ||
        (pstConfig->uiMaxUncertaintyMs > 30000U) ||
        (ullBaseUncertainty > pstConfig->uiMaxUncertaintyMs) ||
        (pstConfig->uiMaxLinkDelayMs >= 90000U)) {
        return NCR_STATUS_BAD_CONFIG;
    }
    return NCR_STATUS_OK;
}

static uint64_t GetNcrMagnitude(int64_t llValue)
{
    if (llValue < 0) {
        return (uint64_t)(-(llValue + 1)) + 1U;
    }
    return (uint64_t)llValue;
}

static void IncrementNcrCounter(uint64_t *pullCounter)
{
    if (*pullCounter < UINT64_MAX) {
        (*pullCounter)++;
    }
}

/* GPS message validation and calendar conversion. */
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
        (pstGpsTime->ucClockStatus < (uint8_t)NCR_CLOCK_GPS_SYNC) ||
        (pstGpsTime->ucClockStatus > (uint8_t)NCR_CLOCK_UNSYNCED)) {
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

static bool ConvertNcrSourceMsToDate(int64_t llSourceMs, uint16_t *pusYear,
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

/* GPS reception events and clock quality. */
static void InitializeNcrTimeState(NcrTimeState_t *pstTime)
{
    memset(pstTime, 0, sizeof(*pstTime));
    pstTime->iSourceStatus = NCR_CLOCK_UNSYNCED;
}

static void RejectNcrTimeSample(NcrTimeState_t *pstTime)
{
    pstTime->uiStepCount = 0U;
    pstTime->uiAcquireCount = 0U;
}

static void BeginNcrReacquisition(NcrSync_t *pstSync)
{
    pstSync->stTime.bReacquiring = true;
    pstSync->stTime.bQualified = false;
    RejectNcrTimeSample(&pstSync->stTime);
}

static void StartSourceHoldover(NcrTimeState_t *pstTime, uint64_t ullMonoMs)
{
    if (!pstTime->bSourceHoldover) {
        pstTime->bSourceHoldover = true;
        pstTime->ullSourceHoldoverStartMs = ullMonoMs;
    }
}

static void CheckNcrGpsReception(NcrSync_t *pstSync, const NcrClockStamp_t *pstStamp)
{
    NcrGpsEvents_t *pstEvents = &pstSync->stGpsEvents;
    if (UINT64_MAX == pstSync->ullLastGpsMessageMonoMs) {
        pstSync->ullLastGpsMessageMonoMs = pstStamp->ullMonotonicMs;
    }
    else if (!pstEvents->bGpsMessageMissing &&
             (pstStamp->ullMonotonicMs >= pstSync->ullLastGpsMessageMonoMs) &&
             (pstStamp->ullMonotonicMs - pstSync->ullLastGpsMessageMonoMs >
              pstSync->stConfig.uiGpsMessageTimeoutMs)) {
        pstEvents->llLastGpsMessageMissingUtcMs = pstStamp->llRealtimeMs;
        pstEvents->bGpsMessageMissing = true;
        pstSync->stTime.bHasLocked = false;
        pstSync->stTime.bQualified = false;
        RejectNcrTimeSample(&pstSync->stTime);
    }
}

static void RecordNcrGpsMessage(NcrSync_t *pstSync, const NcrClockStamp_t *pstStamp,
                               NcrClockStatus iSourceStatus)
{
    NcrGpsEvents_t *pstEvents = &pstSync->stGpsEvents;
    if ((NCR_CLOCK_GPS_SYNC == pstSync->stTime.iSourceStatus) &&
        (NCR_CLOCK_GPS_SYNC != iSourceStatus)) {
        pstEvents->llLastGpsSyncLostUtcMs = pstStamp->llRealtimeMs;
    }
    if (pstEvents->bGpsMessageMissing) {
        pstEvents->llLastGpsMessageRestoredUtcMs = pstStamp->llRealtimeMs;
        pstEvents->bGpsMessageMissing = false;
    }
    pstSync->ullLastGpsMessageMonoMs = pstStamp->ullMonotonicMs;
    pstSync->stTime.iSourceStatus = iSourceStatus;
}

static NcrStatus ObserveNcrTime(NcrSync_t *pstSync, const GpsTime_t *pstGpsTime,
                        const NcrClockStamp_t *pstStamp,
                        NcrTimeDecision_t *pstDecision)
{
    NcrTimeState_t *pstTime = &pstSync->stTime;
    const NcrConfig_t *pstConfig = &pstSync->stConfig;
    int64_t llSourceMs;
    int64_t llSourceDelta;
    int64_t llOffsetMs;
    uint64_t ullMonoDelta = 0U;
    bool bDiscontinuous = false;
    bool bGap = false;
    NcrClockStatus iPreviousStatus = pstTime->iSourceStatus;

    if ((NULL != pstGpsTime) &&
        (NCR_CLOCK_UNSYNCED == (NcrClockStatus)pstGpsTime->ucClockStatus)) {
        RecordNcrGpsMessage(pstSync, pstStamp, NCR_CLOCK_UNSYNCED);
        pstTime->bHasLocked = false;
        pstTime->bQualified = false;
        pstTime->bSourceHoldover = false;
        RejectNcrTimeSample(pstTime);
        return NCR_STATUS_UNSYNCED;
    }
    if (NCR_STATUS_OK != ConvertGpsTimeToSourceMs(pstGpsTime, &llSourceMs)) {
        RejectNcrTimeSample(pstTime);
        return NCR_STATUS_INVALID_GPS;
    }
    RecordNcrGpsMessage(pstSync, pstStamp, (NcrClockStatus)pstGpsTime->ucClockStatus);
    if (NCR_CLOCK_HOLDOVER == pstTime->iSourceStatus) {
        StartSourceHoldover(pstTime, pstStamp->ullMonotonicMs);
        pstTime->bQualified = false;
        RejectNcrTimeSample(pstTime);
        pstDecision->iAction = NCR_TIME_ACCEPTED;
        return NCR_STATUS_OK;
    }
    pstDecision->llTargetUtcAtReceiveMs = llSourceMs -
        (int64_t)pstConfig->iSourceMinusUtcSec * 1000 +
        pstConfig->uiTimeMessageDelayMs;
    llOffsetMs = pstDecision->llTargetUtcAtReceiveMs - pstStamp->llRealtimeMs;
    pstDecision->llOffsetMs = llOffsetMs;
    if (pstTime->bResetRequired) {
        RejectNcrTimeSample(pstTime);
        return NCR_STATUS_RESET_REQUIRED;
    }
    if (GetNcrMagnitude(llOffsetMs) > pstConfig->uiMaxReacquireOffsetMs) {
        pstTime->bResetRequired = true;
        BeginNcrReacquisition(pstSync);
        return NCR_STATUS_RESET_REQUIRED;
    }
    if (pstTime->bHasSample) {
        if (pstStamp->ullMonotonicMs <= pstTime->ullLastSampleMonoMs) {
            RejectNcrTimeSample(pstTime);
            return NCR_STATUS_DUPLICATE;
        }
        if (llSourceMs == pstTime->llLastSourceMs) {
            RejectNcrTimeSample(pstTime);
            return NCR_STATUS_DUPLICATE;
        }
        ullMonoDelta = pstStamp->ullMonotonicMs - pstTime->ullLastSampleMonoMs;
        if (ullMonoDelta > (uint64_t)INT64_MAX) {
            BeginNcrReacquisition(pstSync);
            return NCR_STATUS_CLOCK_ERROR;
        }
        llSourceDelta = llSourceMs - pstTime->llLastSourceMs;
        bDiscontinuous = (llSourceDelta < 0) ||
            (GetNcrMagnitude(llSourceDelta - (int64_t)ullMonoDelta) >
             pstConfig->uiMaxSampleJitterMs);
        bGap = ullMonoDelta > pstConfig->uiGpsMessageTimeoutMs;
    }
    if (bDiscontinuous) {
        BeginNcrReacquisition(pstSync);
    }
    else if (bGap || (NCR_CLOCK_GPS_SYNC != iPreviousStatus)) {
        pstTime->bQualified = false;
        RejectNcrTimeSample(pstTime);
    }

    pstTime->bHasSample = true;
    pstTime->llLastSourceMs = llSourceMs;
    pstTime->ullLastSampleMonoMs = pstStamp->ullMonotonicMs;
    pstTime->llObservedOffsetMs = llOffsetMs;
    if (pstTime->uiAcquireCount < pstConfig->uiAcquireSamples) {
        pstTime->uiAcquireCount++;
    }
    if (pstTime->uiAcquireCount >= pstConfig->uiAcquireSamples) {
        pstTime->bHasLocked = true;
        pstTime->bQualified = true;
        pstTime->bReacquiring = false;
        pstTime->bSourceHoldover = false;
        pstTime->stLastLocked = *pstStamp;
        pstTime->llTrustedOffsetMs = llOffsetMs;
    }

    if (GetNcrMagnitude(llOffsetMs) > pstConfig->uiStepThresholdMs) {
        if ((0U == pstTime->uiStepCount) ||
            ((llOffsetMs < 0) != (pstTime->llPreviousStepOffsetMs < 0)) ||
            (GetNcrMagnitude(llOffsetMs - pstTime->llPreviousStepOffsetMs) >
             pstConfig->uiMaxSampleJitterMs)) {
            pstTime->uiStepCount = 1U;
        }
        else if (pstTime->uiStepCount < pstConfig->uiConfirmSteps) {
            pstTime->uiStepCount++;
        }
        pstTime->llPreviousStepOffsetMs = llOffsetMs;
    }
    else {
        pstTime->uiStepCount = 0U;
    }
    pstDecision->uiConfirmCount = pstTime->uiStepCount;
    pstDecision->iAction = pstTime->bReacquiring ?
                          NCR_TIME_REACQUIRING : NCR_TIME_ACCEPTED;
    if (pstTime->bQualified && (pstTime->uiStepCount >= pstConfig->uiConfirmSteps)) {
        pstDecision->iAction = NCR_TIME_STEP_RECOMMENDED;
    }
    return pstTime->bReacquiring ? NCR_STATUS_REACQUIRING : NCR_STATUS_OK;
}

static void UseNcrUbuntuFallback(const NcrConfig_t *pstConfig,
                                NcrTimeQuality_t *pstQuality)
{
    pstQuality->bUbuntuFallback = true;
    pstQuality->bHoldover = false;
    pstQuality->ullUncertaintyMs = pstConfig->uiMaxUncertaintyMs;
    pstQuality->iUsability = NCR_RESTORE_OK;
}

static void EvaluateNcrQuality(const NcrSync_t *pstSync,
                        const NcrClockStamp_t *pstStamp,
                        NcrTimeQuality_t *pstQuality)
{
    const NcrTimeState_t *pstTime = &pstSync->stTime;
    const NcrConfig_t *pstConfig = &pstSync->stConfig;
    uint64_t ullDriftMs;
    memset(pstQuality, 0, sizeof(*pstQuality));
    pstQuality->iState = NCR_STATE_UNINITIALIZED;
    pstQuality->iSourceStatus = pstTime->iSourceStatus;
    pstQuality->iUsability = NCR_RESTORE_NOT_SYNCED;
    pstQuality->bHasLockedReference = pstTime->bHasLocked;
    pstQuality->llObservedOffsetMs = pstTime->llObservedOffsetMs;
    pstQuality->llTrustedOffsetMs = pstTime->llTrustedOffsetMs;
    pstQuality->ullPolicyGeneration = pstSync->ullPolicyGeneration;
    if (UINT64_MAX != pstSync->ullLastGpsMessageMonoMs) {
        if (pstStamp->ullMonotonicMs < pstSync->ullLastGpsMessageMonoMs) {
            pstQuality->iState = NCR_STATE_REACQUIRING;
            pstQuality->iUsability = NCR_RESTORE_CLOCK_ERROR;
            return;
        }
        pstQuality->ullMessageAgeMs = pstStamp->ullMonotonicMs -
                                      pstSync->ullLastGpsMessageMonoMs;
    }
    if (pstTime->bHasSample) {
        pstQuality->iState = NCR_STATE_ACQUIRING;
    }
    if (pstTime->bSourceHoldover) {
        pstQuality->ullSourceHoldoverAgeMs = pstStamp->ullMonotonicMs -
                                             pstTime->ullSourceHoldoverStartMs;
    }
    if (pstTime->bHasLocked) {
        if (pstStamp->ullMonotonicMs < pstTime->stLastLocked.ullMonotonicMs) {
            pstQuality->iState = NCR_STATE_REACQUIRING;
            pstQuality->iUsability = NCR_RESTORE_CLOCK_ERROR;
            return;
        }
        pstQuality->ullLockedAgeMs = pstStamp->ullMonotonicMs -
                                     pstTime->stLastLocked.ullMonotonicMs;
    }
    if ((NCR_CLOCK_UNSYNCED == pstTime->iSourceStatus) ||
        pstSync->stGpsEvents.bGpsMessageMissing) {
        if (pstTime->bHasSample) {
            pstQuality->iState = NCR_STATE_STALE;
        }
        UseNcrUbuntuFallback(pstConfig, pstQuality);
        return;
    }
    if ((NCR_CLOCK_HOLDOVER == pstTime->iSourceStatus) &&
        (!pstTime->bHasLocked || pstTime->bResetRequired || pstTime->bReacquiring)) {
        pstQuality->iState = NCR_STATE_HOLDOVER;
        UseNcrUbuntuFallback(pstConfig, pstQuality);
        return;
    }
    if (pstTime->bResetRequired || pstTime->bReacquiring) {
        pstQuality->iState = NCR_STATE_REACQUIRING;
        pstQuality->iUsability = NCR_RESTORE_REACQUIRING;
        return;
    }
    if (!pstTime->bHasLocked) {
        return;
    }
    /* Avoid overflow on arbitrarily old contexts; limits are uint32_t ms. */
    if ((pstQuality->ullLockedAgeMs > pstConfig->uiMaxLocalHoldoverMs) ||
        (pstTime->bSourceHoldover &&
         (pstQuality->ullSourceHoldoverAgeMs > pstConfig->uiMaxSourceHoldoverMs))) {
        pstQuality->iState = NCR_STATE_STALE;
        pstQuality->iUsability = NCR_RESTORE_STALE;
        pstQuality->bHoldover = true;
        if (NCR_CLOCK_HOLDOVER == pstTime->iSourceStatus) {
            UseNcrUbuntuFallback(pstConfig, pstQuality);
        }
        return;
    }
    ullDriftMs = (pstQuality->ullLockedAgeMs * pstConfig->uiLocalDriftPpm +
                  UINT64_C(999999)) / UINT64_C(1000000);
    pstQuality->ullUncertaintyMs = GetNcrMagnitude(pstTime->llTrustedOffsetMs) +
        pstConfig->uiSourceUncertaintyMs + (uint64_t)pstConfig->uiReceiveUncertaintyMs +
        pstConfig->uiDelayUncertaintyMs + ullDriftMs;
    /* Include sub-threshold realtime movement instead of silently ignoring it. */
    {
        int64_t llClockMovement = pstStamp->llRealtimeMs -
            pstTime->stLastLocked.llRealtimeMs - (int64_t)pstQuality->ullLockedAgeMs;
        pstQuality->ullUncertaintyMs += GetNcrMagnitude(llClockMovement);
    }
    pstQuality->bHoldover = !pstTime->bQualified ||
        (NCR_CLOCK_GPS_SYNC != pstTime->iSourceStatus);
    pstQuality->iState = pstQuality->bHoldover ? NCR_STATE_HOLDOVER : NCR_STATE_READY;
    if (GetNcrMagnitude(pstTime->llTrustedOffsetMs) > pstConfig->uiMaxClockOffsetMs) {
        pstQuality->iState = NCR_STATE_STALE;
        pstQuality->iUsability = NCR_RESTORE_CLOCK_OFFSET;
    }
    else if (pstQuality->ullUncertaintyMs > pstConfig->uiMaxUncertaintyMs) {
        pstQuality->iState = NCR_STATE_STALE;
        pstQuality->iUsability = NCR_RESTORE_UNCERTAINTY;
    }
    else {
        pstQuality->iUsability = NCR_RESTORE_OK;
    }
}

/* NCR candidate search in the receive window. */
NcrRestoreStatus ReconstructNcr24(uint32_t uiLsb24, int64_t llReceiveSourceMs,
                                  uint32_t uiUncertaintyMs,
                                  const NcrConfig_t *pstConfig,
                                  NcrRestoreResult_t *pstResult)
{
    int64_t llFirstMs;
    int64_t llLastMs;
    int64_t llSecond;
    int64_t llLastSecond;
    uint64_t ullCounter;
    uint32_t uiMatches = 0U;
    NcrRestoreResult_t stCandidate = {0};
    if (NULL == pstResult) {
        return NCR_RESTORE_BAD_INPUT;
    }
    memset(pstResult, 0, sizeof(*pstResult));
    if ((NCR_STATUS_OK != ValidateNcrConfig(pstConfig)) ||
        (uiLsb24 > NCR_LSB24_MASK) ||
        (uiUncertaintyMs > pstConfig->uiMaxUncertaintyMs) ||
        (llReceiveSourceMs < NCR_MIN_SOURCE_MS - NCR_MS_PER_DAY) ||
        (llReceiveSourceMs > NCR_END_SOURCE_MS + NCR_MS_PER_DAY)) {
        return NCR_RESTORE_BAD_INPUT;
    }
    llFirstMs = llReceiveSourceMs - uiUncertaintyMs - pstConfig->uiMaxLinkDelayMs;
    llLastMs = llReceiveSourceMs + uiUncertaintyMs;
    if (llFirstMs < NCR_MIN_SOURCE_MS) {
        llFirstMs = NCR_MIN_SOURCE_MS;
    }
    if (llLastMs >= NCR_END_SOURCE_MS) {
        llLastMs = NCR_END_SOURCE_MS - 1;
    }
    if (llFirstMs > llLastMs) {
        return NCR_RESTORE_NO_CANDIDATE;
    }
    /* Enumerate the bounded feasible seconds instead of guessing upper bits.
     * This also naturally crosses midnight, month/year and modulo-2^32 wraps.
     * Config validation bounds this loop to fewer than 188 seconds. */
    llLastSecond = llLastMs / 1000;
    for (llSecond = llFirstMs / 1000; llSecond <= llLastSecond; llSecond++) {
        ullCounter = (uint64_t)(llSecond % 86400) * NCR_TICKS_PER_SECOND;
        if (uiLsb24 == (uint32_t)(ullCounter & NCR_LSB24_MASK)) {
            uiMatches++;
            if (uiMatches > 1U) {
                return NCR_RESTORE_AMBIGUOUS;
            }
            stCandidate.uiNcr32 = (uint32_t)ullCounter;
            stCandidate.llSourceTxSecondMs = llSecond * 1000;
        }
    }
    if (0U == uiMatches) {
        return NCR_RESTORE_NO_CANDIDATE;
    }
    if (!ConvertNcrSourceMsToDate(stCandidate.llSourceTxSecondMs,
                                  &stCandidate.usYear, &stCandidate.ucMonth,
                                  &stCandidate.ucDay)) {
        return NCR_RESTORE_BAD_INPUT;
    }
    *pstResult = stCandidate;
    return NCR_RESTORE_OK;
}

/* Ubuntu clock access. */
static NcrStatus ReadLinuxClock(void *pvContext, NcrClockStamp_t *pstStamp)
{
    struct timespec stBefore;
    struct timespec stReal;
    struct timespec stAfter;
    uint64_t ullBeforeNs;
    uint64_t ullAfterNs;
    uint32_t uiAttempt;
    (void)pvContext;
    for (uiAttempt = 0U; uiAttempt < 3U; uiAttempt++) {
        if ((0 != clock_gettime(CLOCK_MONOTONIC, &stBefore)) ||
            (0 != clock_gettime(CLOCK_REALTIME, &stReal)) ||
            (0 != clock_gettime(CLOCK_MONOTONIC, &stAfter)) ||
            (stBefore.tv_sec < 0) || (stReal.tv_sec < 0) ||
            ((uint64_t)stBefore.tv_sec > (uint64_t)INT64_MAX / UINT64_C(1000000000)) ||
            ((uint64_t)stAfter.tv_sec > (uint64_t)INT64_MAX / UINT64_C(1000000000)) ||
            ((uint64_t)stReal.tv_sec > (uint64_t)NCR_MAX_REALTIME_MS / 1000U)) {
            return NCR_STATUS_CLOCK_ERROR;
        }
        ullBeforeNs = (uint64_t)stBefore.tv_sec * UINT64_C(1000000000) +
                      (uint64_t)stBefore.tv_nsec;
        ullAfterNs = (uint64_t)stAfter.tv_sec * UINT64_C(1000000000) +
                     (uint64_t)stAfter.tv_nsec;
        if ((ullAfterNs >= ullBeforeNs) &&
            (ullAfterNs - ullBeforeNs <= UINT64_C(5000000))) {
            pstStamp->llRealtimeMs = (int64_t)stReal.tv_sec * 1000 + stReal.tv_nsec / 1000000;
            pstStamp->ullMonotonicMs = (ullBeforeNs +
                (ullAfterNs - ullBeforeNs) / 2U) / UINT64_C(1000000);
            return NCR_STATUS_OK;
        }
    }
    return NCR_STATUS_CLOCK_ERROR;
}

static NcrStatus SetLinuxClock(void *pvContext, int64_t llTargetUtcMs)
{
    struct timespec stTarget;
    (void)pvContext;
    if ((llTargetUtcMs < 0) || (llTargetUtcMs > NCR_MAX_REALTIME_MS)) {
        return NCR_STATUS_BAD_ARGUMENT;
    }
    stTarget.tv_sec = (time_t)(llTargetUtcMs / 1000);
    stTarget.tv_nsec = (int32_t)((llTargetUtcMs % 1000) * 1000000);
    if ((int64_t)stTarget.tv_sec != llTargetUtcMs / 1000) {
        return NCR_STATUS_UNSUPPORTED;
    }
    if (0 != clock_settime(CLOCK_REALTIME, &stTarget)) {
        if ((EPERM == errno) || (EACCES == errno)) {
            return NCR_STATUS_PERMISSION_DENIED;
        }
        return NCR_STATUS_STEP_FAILED;
    }
    return NCR_STATUS_OK;
}

static NcrPlatform_t GetNcrDefaultPlatform(void)
{
    NcrPlatform_t stPlatform = {ReadLinuxClock, SetLinuxClock, NULL};
    return stPlatform;
}

/* Thread-safe application API. */
static NcrStatus ReadNcrServiceClock(NcrSync_t *pstSync, NcrClockStamp_t *pstStamp)
{
    int64_t llDifference;
    uint64_t ullElapsed;
    NcrStatus iStatus = pstSync->stPlatform.pReadClock(
        pstSync->stPlatform.pvContext, pstStamp);
    if ((NCR_STATUS_OK != iStatus) || (pstStamp->llRealtimeMs < 0) ||
        (pstStamp->llRealtimeMs > NCR_MAX_REALTIME_MS) ||
        (pstStamp->ullMonotonicMs > (uint64_t)INT64_MAX / 4U)) {
        BeginNcrReacquisition(pstSync);
        return NCR_STATUS_CLOCK_ERROR;
    }
    if (pstSync->bHasClockStamp) {
        if (pstStamp->ullMonotonicMs < pstSync->stLastClockStamp.ullMonotonicMs) {
            BeginNcrReacquisition(pstSync);
            return NCR_STATUS_CLOCK_ERROR;
        }
        ullElapsed = pstStamp->ullMonotonicMs -
                     pstSync->stLastClockStamp.ullMonotonicMs;
        llDifference = pstStamp->llRealtimeMs -
                        pstSync->stLastClockStamp.llRealtimeMs - (int64_t)ullElapsed;
        if (GetNcrMagnitude(llDifference) > pstSync->stConfig.uiMaxClockJumpMs) {
            BeginNcrReacquisition(pstSync);
        }
    }
    pstSync->bHasClockStamp = true;
    pstSync->stLastClockStamp = *pstStamp;
    CheckNcrGpsReception(pstSync, pstStamp);
    return NCR_STATUS_OK;
}

NcrStatus CreateNcrSync(const NcrConfig_t *pstConfig, NcrSync_t **ppstSync)
{
    NcrPlatform_t stPlatform = GetNcrDefaultPlatform();
    return CreateNcrSyncWithPlatform(pstConfig, &stPlatform, ppstSync);
}

NcrStatus CreateNcrSyncWithPlatform(const NcrConfig_t *pstConfig,
                                   const NcrPlatform_t *pstPlatform,
                                   NcrSync_t **ppstSync)
{
    NcrSync_t *pstSync;
    if (NULL == ppstSync) {
        return NCR_STATUS_BAD_ARGUMENT;
    }
    *ppstSync = NULL;
    if (NCR_STATUS_OK != ValidateNcrConfig(pstConfig)) {
        return NCR_STATUS_BAD_CONFIG;
    }
    if ((NULL == pstPlatform) || (NULL == pstPlatform->pReadClock)) {
        return NCR_STATUS_BAD_ARGUMENT;
    }
    if (pstConfig->bAllowClockStep && (NULL == pstPlatform->pSetClock)) {
        return NCR_STATUS_UNSUPPORTED;
    }
    pstSync = calloc(1U, sizeof(*pstSync));
    if (NULL == pstSync) {
        return NCR_STATUS_NO_MEMORY;
    }
    if (0 != pthread_mutex_init(&pstSync->stMutex, NULL)) {
        free(pstSync);
        return NCR_STATUS_LOCK_ERROR;
    }
    pstSync->stConfig = *pstConfig;
    pstSync->stPlatform = *pstPlatform;
    pstSync->ullPolicyGeneration = 1U;
    pstSync->ullLastGpsMessageMonoMs = UINT64_MAX;
    pstSync->stGpsEvents.llLastGpsSyncLostUtcMs = NCR_GPS_EVENT_NOT_RECORDED_MS;
    pstSync->stGpsEvents.llLastGpsMessageMissingUtcMs = NCR_GPS_EVENT_NOT_RECORDED_MS;
    pstSync->stGpsEvents.llLastGpsMessageRestoredUtcMs = NCR_GPS_EVENT_NOT_RECORDED_MS;
    InitializeNcrTimeState(&pstSync->stTime);
    *ppstSync = pstSync;
    return NCR_STATUS_OK;
}

static NcrStatus ApplyClockCorrection(NcrSync_t *pstSync,
                                     const NcrClockStamp_t *pstReceiveStamp,
                                     NcrTimeDecision_t *pstDecision)
{
    NcrClockStamp_t stBefore = {0};
    NcrClockStamp_t stAfter = {0};
    int64_t llTargetMs;
    int64_t llStepMs;
    uint64_t ullElapsed;
    NcrStatus iStatus = ReadNcrServiceClock(pstSync, &stBefore);
    if (NCR_STATUS_OK != iStatus) {
        return iStatus;
    }
    if (!pstSync->stTime.bQualified) {
        pstDecision->iAction = NCR_TIME_REACQUIRING;
        return NCR_STATUS_REACQUIRING;
    }
    ullElapsed = stBefore.ullMonotonicMs - pstReceiveStamp->ullMonotonicMs;
    llTargetMs = pstDecision->llTargetUtcAtReceiveMs + (int64_t)ullElapsed;
    if ((llTargetMs < 0) || (llTargetMs > NCR_MAX_REALTIME_MS)) {
        return NCR_STATUS_CLOCK_ERROR;
    }
    pstDecision->bStepAttempted = true;
    iStatus = pstSync->stPlatform.pSetClock(pstSync->stPlatform.pvContext, llTargetMs);
    if (NCR_STATUS_OK != iStatus) {
        pstDecision->iAction = NCR_TIME_STEP_FAILED;
        return iStatus;
    }
    pstDecision->bStepApplied = true;
    /* Do not compare this intentional step to the pre-step clock anchor. */
    pstSync->bHasClockStamp = false;
    iStatus = ReadNcrServiceClock(pstSync, &stAfter);
    if ((NCR_STATUS_OK != iStatus) ||
        (stAfter.ullMonotonicMs < stBefore.ullMonotonicMs)) {
        BeginNcrReacquisition(pstSync);
        pstDecision->iAction = NCR_TIME_STEP_FAILED;
        return NCR_STATUS_CLOCK_ERROR;
    }
    ullElapsed = stAfter.ullMonotonicMs - stBefore.ullMonotonicMs;
    llStepMs = stAfter.llRealtimeMs - stBefore.llRealtimeMs - (int64_t)ullElapsed;
    pstSync->stTime.stLastLocked.llRealtimeMs += llStepMs;
    pstSync->stTime.llObservedOffsetMs -= llStepMs;
    pstSync->stTime.llTrustedOffsetMs -= llStepMs;
    pstSync->stTime.uiStepCount = 0U;
    pstDecision->llAppliedStepMs = llStepMs;
    pstDecision->bStepVerified = true;
    pstDecision->iAction = NCR_TIME_STEP_APPLIED;
    if (GetNcrMagnitude(stAfter.llRealtimeMs - llTargetMs - (int64_t)ullElapsed) >
        pstSync->stConfig.uiMaxSampleJitterMs) {
        BeginNcrReacquisition(pstSync);
        pstDecision->bStepVerified = false;
        pstDecision->iAction = NCR_TIME_STEP_FAILED;
        return NCR_STATUS_STEP_FAILED;
    }
    return NCR_STATUS_OK;
}

NcrStatus UpdateGpsTime(NcrSync_t *pstSync, const GpsTime_t *pstGpsTime,
                        NcrTimeDecision_t *pstDecision)
{
    NcrClockStamp_t stStamp = {0};
    NcrStatus iStatus;
    if (NULL != pstDecision) {
        memset(pstDecision, 0, sizeof(*pstDecision));
        pstDecision->iStatus = NCR_STATUS_BAD_ARGUMENT;
        pstDecision->iAction = NCR_TIME_REJECTED;
    }
    if ((NULL == pstSync) || (NULL == pstDecision)) {
        return NCR_STATUS_BAD_ARGUMENT;
    }
    if (0 != pthread_mutex_lock(&pstSync->stMutex)) {
        pstDecision->iStatus = NCR_STATUS_LOCK_ERROR;
        return NCR_STATUS_LOCK_ERROR;
    }
    iStatus = ReadNcrServiceClock(pstSync, &stStamp);
    if (NCR_STATUS_OK == iStatus) {
        iStatus = ObserveNcrTime(pstSync, pstGpsTime, &stStamp, pstDecision);
        if ((NCR_TIME_STEP_RECOMMENDED == pstDecision->iAction) &&
            pstSync->stConfig.bAllowClockStep) {
            iStatus = ApplyClockCorrection(pstSync, &stStamp, pstDecision);
        }
    }
    pstDecision->iStatus = iStatus;
    pstSync->iLastUpdateStatus = iStatus;
    if (pstSync->bHasClockStamp) {
        EvaluateNcrQuality(pstSync, &pstSync->stLastClockStamp, &pstDecision->stQuality);
    }
    else {
        pstDecision->stQuality.iState = NCR_STATE_REACQUIRING;
        pstDecision->stQuality.iUsability = NCR_RESTORE_CLOCK_ERROR;
    }
    if (0 != pthread_mutex_unlock(&pstSync->stMutex)) {
        pstDecision->iStatus = NCR_STATUS_LOCK_ERROR;
        return NCR_STATUS_LOCK_ERROR;
    }
    return iStatus;
}

NcrStatus CaptureNcrReceiveContext(NcrSync_t *pstSync,
                                  NcrReceiveContext_t *pstReceiveContext)
{
    NcrClockStamp_t stStamp = {0};
    NcrStatus iStatus;
    if (NULL != pstReceiveContext) {
        memset(pstReceiveContext, 0, sizeof(*pstReceiveContext));
    }
    if ((NULL == pstSync) || (NULL == pstReceiveContext)) {
        return NCR_STATUS_BAD_ARGUMENT;
    }
    if (0 != pthread_mutex_lock(&pstSync->stMutex)) {
        return NCR_STATUS_LOCK_ERROR;
    }
    iStatus = ReadNcrServiceClock(pstSync, &stStamp);
    if (NCR_STATUS_OK == iStatus) {
        pstReceiveContext->uiVersion = NCR_RECEIVE_CONTEXT_VERSION;
        pstReceiveContext->pstOwner = pstSync;
        pstReceiveContext->stReceiveStamp = stStamp;
        pstReceiveContext->llReceiveSourceMs = stStamp.llRealtimeMs +
            (int64_t)pstSync->stConfig.iSourceMinusUtcSec * 1000;
        EvaluateNcrQuality(pstSync, &stStamp, &pstReceiveContext->stQuality);
    }
    if (0 != pthread_mutex_unlock(&pstSync->stMutex)) {
        memset(pstReceiveContext, 0, sizeof(*pstReceiveContext));
        return NCR_STATUS_LOCK_ERROR;
    }
    return iStatus;
}

static NcrRestoreStatus ValidateReceiveContext(NcrSync_t *pstSync,
                                               const NcrReceiveContext_t *pstContext)
{
    NcrClockStamp_t stNow = {0};
    if ((NCR_RECEIVE_CONTEXT_VERSION != pstContext->uiVersion) ||
        (pstSync != pstContext->pstOwner) ||
        ((NCR_RESTORE_OK == pstContext->stQuality.iUsability) &&
         (pstContext->stQuality.ullUncertaintyMs > pstSync->stConfig.uiMaxUncertaintyMs))) {
        return NCR_RESTORE_BAD_INPUT;
    }
    if (pstContext->stQuality.ullPolicyGeneration != pstSync->ullPolicyGeneration) {
        return NCR_RESTORE_CONTEXT_INVALIDATED;
    }
    if (NCR_STATUS_OK != ReadNcrServiceClock(pstSync, &stNow)) {
        return NCR_RESTORE_CLOCK_ERROR;
    }
    if (stNow.ullMonotonicMs < pstContext->stReceiveStamp.ullMonotonicMs) {
        return NCR_RESTORE_BAD_INPUT;
    }
    if (stNow.ullMonotonicMs - pstContext->stReceiveStamp.ullMonotonicMs >
        pstSync->stConfig.uiMaxQueueAgeMs) {
        return NCR_RESTORE_QUEUE_EXPIRED;
    }
    if ((pstContext->stQuality.iUsability < NCR_RESTORE_OK) ||
        (pstContext->stQuality.iUsability > NCR_RESTORE_CLOCK_ERROR)) {
        return NCR_RESTORE_BAD_INPUT;
    }
    return pstContext->stQuality.iUsability;
}

NcrRestoreStatus RestoreNcr24(NcrSync_t *pstSync,
                              const NcrReceiveContext_t *pstReceiveContext,
                              const uint8_t *pucContext, uint32_t uiContextLength,
                              NcrRestoreResult_t *pstResult)
{
    NcrRestoreStatus iStatus;
    NcrRestoreStatus iFinalStatus;
    NcrConfig_t stConfig;
    NcrReceiveContext_t stContext;
    NcrRestoreResult_t stResult = {0};
    uint32_t uiLsb24;
    if (NULL != pstResult) {
        memset(pstResult, 0, sizeof(*pstResult));
    }
    if ((NULL == pstSync) || (NULL == pstReceiveContext) || (NULL == pucContext) ||
        (NULL == pstResult) || (NCR_CONTEXT_LENGTH != uiContextLength)) {
        return NCR_RESTORE_BAD_INPUT;
    }
    /* The caller must not concurrently modify its context or payload. */
    stContext = *pstReceiveContext;
    uiLsb24 = ((uint32_t)pucContext[0] << 16) |
               ((uint32_t)pucContext[1] << 8) | pucContext[2];
    if (0 != pthread_mutex_lock(&pstSync->stMutex)) {
        return NCR_RESTORE_CLOCK_ERROR;
    }
    iStatus = ValidateReceiveContext(pstSync, &stContext);
    stConfig = pstSync->stConfig;
    if (0 != pthread_mutex_unlock(&pstSync->stMutex)) {
        return NCR_RESTORE_CLOCK_ERROR;
    }
    if (NCR_RESTORE_OK == iStatus) {
        iStatus = ReconstructNcr24(uiLsb24, stContext.llReceiveSourceMs,
            (uint32_t)stContext.stQuality.ullUncertaintyMs, &stConfig, &stResult);
    }
    if (0 != pthread_mutex_lock(&pstSync->stMutex)) {
        return NCR_RESTORE_CLOCK_ERROR;
    }
    iFinalStatus = ValidateReceiveContext(pstSync, &stContext);
    if (NCR_RESTORE_OK != iFinalStatus) {
        iStatus = iFinalStatus;
    }
    if (NCR_RESTORE_OK == iStatus) {
        stResult.stQuality = stContext.stQuality;
        *pstResult = stResult;
    }
    if (0 != pthread_mutex_unlock(&pstSync->stMutex)) {
        memset(pstResult, 0, sizeof(*pstResult));
        return NCR_RESTORE_CLOCK_ERROR;
    }
    return iStatus;
}

NcrStatus GetNcrSyncStatus(NcrSync_t *pstSync, NcrSyncStatus_t *pstStatus)
{
    NcrClockStamp_t stStamp = {0};
    NcrStatus iStatus;
    if (NULL != pstStatus) {
        memset(pstStatus, 0, sizeof(*pstStatus));
    }
    if ((NULL == pstSync) || (NULL == pstStatus)) {
        return NCR_STATUS_BAD_ARGUMENT;
    }
    if (0 != pthread_mutex_lock(&pstSync->stMutex)) {
        return NCR_STATUS_LOCK_ERROR;
    }
    iStatus = ReadNcrServiceClock(pstSync, &stStamp);
    pstStatus->iLastUpdateStatus = pstSync->iLastUpdateStatus;
    pstStatus->stGpsEvents = pstSync->stGpsEvents;
    if (NCR_STATUS_OK == iStatus) {
        EvaluateNcrQuality(pstSync, &stStamp, &pstStatus->stQuality);
    }
    else {
        pstStatus->stQuality.iState = NCR_STATE_REACQUIRING;
        pstStatus->stQuality.iUsability = NCR_RESTORE_CLOCK_ERROR;
    }
    if (0 != pthread_mutex_unlock(&pstSync->stMutex)) {
        return NCR_STATUS_LOCK_ERROR;
    }
    return iStatus;
}

NcrStatus ResetNcrSync(NcrSync_t *pstSync)
{
    if (NULL == pstSync) {
        return NCR_STATUS_BAD_ARGUMENT;
    }
    if (0 != pthread_mutex_lock(&pstSync->stMutex)) {
        return NCR_STATUS_LOCK_ERROR;
    }
    InitializeNcrTimeState(&pstSync->stTime);
    pstSync->bHasClockStamp = false;
    IncrementNcrCounter(&pstSync->ullPolicyGeneration);
    pstSync->iLastUpdateStatus = NCR_STATUS_OK;
    if (0 != pthread_mutex_unlock(&pstSync->stMutex)) {
        return NCR_STATUS_LOCK_ERROR;
    }
    return NCR_STATUS_OK;
}

void DestroyNcrSync(NcrSync_t *pstSync)
{
    if (NULL != pstSync) {
        (void)pthread_mutex_destroy(&pstSync->stMutex);
        free(pstSync);
    }
}

/* Diagnostic names for host logs. */
const char *GetNcrStatusText(NcrStatus iStatus)
{
    switch (iStatus) {
    case NCR_STATUS_OK:
        return "NCR_STATUS_OK";
    case NCR_STATUS_BAD_ARGUMENT:
        return "NCR_STATUS_BAD_ARGUMENT";
    case NCR_STATUS_BAD_CONFIG:
        return "NCR_STATUS_BAD_CONFIG";
    case NCR_STATUS_INVALID_GPS:
        return "NCR_STATUS_INVALID_GPS";
    case NCR_STATUS_UNSYNCED:
        return "NCR_STATUS_UNSYNCED";
    case NCR_STATUS_DUPLICATE:
        return "NCR_STATUS_DUPLICATE";
    case NCR_STATUS_REACQUIRING:
        return "NCR_STATUS_REACQUIRING";
    case NCR_STATUS_RESET_REQUIRED:
        return "NCR_STATUS_RESET_REQUIRED";
    case NCR_STATUS_CLOCK_ERROR:
        return "NCR_STATUS_CLOCK_ERROR";
    case NCR_STATUS_STEP_FAILED:
        return "NCR_STATUS_STEP_FAILED";
    case NCR_STATUS_PERMISSION_DENIED:
        return "NCR_STATUS_PERMISSION_DENIED";
    case NCR_STATUS_UNSUPPORTED:
        return "NCR_STATUS_UNSUPPORTED";
    case NCR_STATUS_NO_MEMORY:
        return "NCR_STATUS_NO_MEMORY";
    case NCR_STATUS_LOCK_ERROR:
        return "NCR_STATUS_LOCK_ERROR";
    default:
        return "NCR_STATUS_UNKNOWN";
    }
}

const char *GetNcrRestoreStatusText(NcrRestoreStatus iStatus)
{
    switch (iStatus) {
    case NCR_RESTORE_OK:
        return "NCR_RESTORE_OK";
    case NCR_RESTORE_BAD_INPUT:
        return "NCR_RESTORE_BAD_INPUT";
    case NCR_RESTORE_NOT_SYNCED:
        return "NCR_RESTORE_NOT_SYNCED";
    case NCR_RESTORE_REACQUIRING:
        return "NCR_RESTORE_REACQUIRING";
    case NCR_RESTORE_STALE:
        return "NCR_RESTORE_STALE";
    case NCR_RESTORE_CLOCK_OFFSET:
        return "NCR_RESTORE_CLOCK_OFFSET";
    case NCR_RESTORE_UNCERTAINTY:
        return "NCR_RESTORE_UNCERTAINTY";
    case NCR_RESTORE_NO_CANDIDATE:
        return "NCR_RESTORE_NO_CANDIDATE";
    case NCR_RESTORE_AMBIGUOUS:
        return "NCR_RESTORE_AMBIGUOUS";
    case NCR_RESTORE_QUEUE_EXPIRED:
        return "NCR_RESTORE_QUEUE_EXPIRED";
    case NCR_RESTORE_CONTEXT_INVALIDATED:
        return "NCR_RESTORE_CONTEXT_INVALIDATED";
    case NCR_RESTORE_CLOCK_ERROR:
        return "NCR_RESTORE_CLOCK_ERROR";
    default:
        return "NCR_RESTORE_UNKNOWN";
    }
}

const char *GetNcrStateText(NcrState iState)
{
    switch (iState) {
    case NCR_STATE_UNINITIALIZED:
        return "NCR_STATE_UNINITIALIZED";
    case NCR_STATE_ACQUIRING:
        return "NCR_STATE_ACQUIRING";
    case NCR_STATE_READY:
        return "NCR_STATE_READY";
    case NCR_STATE_HOLDOVER:
        return "NCR_STATE_HOLDOVER";
    case NCR_STATE_REACQUIRING:
        return "NCR_STATE_REACQUIRING";
    case NCR_STATE_STALE:
        return "NCR_STATE_STALE";
    default:
        return "NCR_STATE_UNKNOWN";
    }
}
