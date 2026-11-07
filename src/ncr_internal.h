#ifndef NCR_INTERNAL_H
#define NCR_INTERNAL_H

#include "ncr_sync.h"
#include <limits.h>
#include <pthread.h>

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
    NcrClockStamp_t stLastSample;
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
    NcrSyncStatus_t stCounters;
    bool bHasClockStamp;
    NcrClockStamp_t stLastClockStamp;
    uint64_t ullClockGeneration;
    uint64_t ullPolicyGeneration;
};

uint64_t GetNcrMagnitude(int64_t llValue);
void IncrementNcrCounter(uint64_t *pullCounter);
bool ConvertNcrSourceMsToDate(int64_t llSourceMs, uint16_t *pusYear,
                             uint8_t *pucMonth, uint8_t *pucDay);
void InitializeNcrTimeState(NcrTimeState_t *pstTime);
void RejectNcrTimeSample(NcrTimeState_t *pstTime);
void BeginNcrReacquisition(NcrSync_t *pstSync);
NcrStatus ObserveNcrTime(NcrSync_t *pstSync, const GpsTime_t *pstGpsTime,
                        const NcrClockStamp_t *pstStamp,
                        NcrTimeDecision_t *pstDecision);
void EvaluateNcrQuality(const NcrSync_t *pstSync,
                        const NcrClockStamp_t *pstStamp,
                        NcrTimeQuality_t *pstQuality);
NcrStatus ReadNcrServiceClock(NcrSync_t *pstSync, NcrClockStamp_t *pstStamp);
NcrPlatform_t GetNcrDefaultPlatform(void);

#endif
