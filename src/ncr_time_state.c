#include "ncr_internal.h"
#include <string.h>

void InitializeNcrTimeState(NcrTimeState_t *pstTime)
{
    memset(pstTime, 0, sizeof(*pstTime));
    pstTime->iSourceStatus = NCR_CLOCK_UNSYNCED;
}

void RejectNcrTimeSample(NcrTimeState_t *pstTime)
{
    pstTime->uiStepCount = 0U;
    pstTime->uiAcquireCount = 0U;
}

void BeginNcrReacquisition(NcrSync_t *pstSync)
{
    if (!pstSync->stTime.bReacquiring) {
        IncrementNcrCounter(&pstSync->stCounters.ullReacquireEvents);
    }
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

NcrStatus ObserveNcrTime(NcrSync_t *pstSync, const GpsTime_t *pstGpsTime,
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

    if (NCR_STATUS_OK != ConvertGpsTimeToSourceMs(pstGpsTime, &llSourceMs)) {
        RejectNcrTimeSample(pstTime);
        return NCR_STATUS_INVALID_GPS;
    }
    if (NCR_CLOCK_UNSYNCED == (NcrClockStatus)pstGpsTime->ucClockStatus) {
        pstTime->iSourceStatus = NCR_CLOCK_UNSYNCED;
        pstTime->bQualified = false;
        StartSourceHoldover(pstTime, pstStamp->ullMonotonicMs);
        RejectNcrTimeSample(pstTime);
        return NCR_STATUS_UNSYNCED;
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
        if (pstStamp->ullMonotonicMs <= pstTime->stLastSample.ullMonotonicMs) {
            RejectNcrTimeSample(pstTime);
            return NCR_STATUS_DUPLICATE;
        }
        if (llSourceMs == pstTime->llLastSourceMs) {
            RejectNcrTimeSample(pstTime);
            return NCR_STATUS_DUPLICATE;
        }
        ullMonoDelta = pstStamp->ullMonotonicMs - pstTime->stLastSample.ullMonotonicMs;
        if (ullMonoDelta > (uint64_t)INT64_MAX) {
            BeginNcrReacquisition(pstSync);
            return NCR_STATUS_CLOCK_ERROR;
        }
        llSourceDelta = llSourceMs - pstTime->llLastSourceMs;
        bDiscontinuous = (llSourceDelta < 0) ||
            (GetNcrMagnitude(llSourceDelta - (int64_t)ullMonoDelta) >
             pstConfig->uiMaxSampleJitterMs);
        bGap = ullMonoDelta > pstConfig->uiMessageGapToHoldoverMs;
    }
    if (bDiscontinuous) {
        BeginNcrReacquisition(pstSync);
    }
    else if (bGap || (NCR_CLOCK_LOCKED != iPreviousStatus)) {
        pstTime->bQualified = false;
        RejectNcrTimeSample(pstTime);
    }

    pstTime->bHasSample = true;
    pstTime->llLastSourceMs = llSourceMs;
    pstTime->stLastSample = *pstStamp;
    pstTime->llObservedOffsetMs = llOffsetMs;
    pstTime->iSourceStatus = (NcrClockStatus)pstGpsTime->ucClockStatus;
    if (NCR_CLOCK_HOLDOVER == pstTime->iSourceStatus) {
        StartSourceHoldover(pstTime, pstStamp->ullMonotonicMs);
        pstTime->bQualified = false;
        RejectNcrTimeSample(pstTime);
        pstDecision->iAction = NCR_TIME_ACCEPTED;
        return pstTime->bReacquiring ? NCR_STATUS_REACQUIRING : NCR_STATUS_OK;
    }

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

void EvaluateNcrQuality(const NcrSync_t *pstSync,
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
    pstQuality->ullClockGeneration = pstSync->ullClockGeneration;
    pstQuality->ullPolicyGeneration = pstSync->ullPolicyGeneration;
    if (pstTime->bHasSample) {
        if (pstStamp->ullMonotonicMs < pstTime->stLastSample.ullMonotonicMs) {
            pstQuality->iState = NCR_STATE_REACQUIRING;
            pstQuality->iUsability = NCR_RESTORE_CLOCK_ERROR;
            return;
        }
        pstQuality->ullMessageAgeMs = pstStamp->ullMonotonicMs -
                                      pstTime->stLastSample.ullMonotonicMs;
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
    if (pstTime->bResetRequired || pstTime->bReacquiring) {
        pstQuality->iState = NCR_STATE_REACQUIRING;
        pstQuality->iUsability = NCR_RESTORE_REACQUIRING;
        return;
    }
    if (!pstTime->bHasLocked) {
        return;
    }
    /* Avoid overflow on arbitrarily old contexts; limits are uint32_t ms. */
    if ((pstQuality->ullMessageAgeMs > pstConfig->uiMaxMessageAgeMs) ||
        (pstQuality->ullLockedAgeMs > pstConfig->uiMaxLocalHoldoverMs) ||
        (pstTime->bSourceHoldover &&
         (pstQuality->ullSourceHoldoverAgeMs > pstConfig->uiMaxSourceHoldoverMs))) {
        pstQuality->iState = NCR_STATE_STALE;
        pstQuality->iUsability = NCR_RESTORE_STALE;
        pstQuality->bHoldover = true;
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
        (NCR_CLOCK_LOCKED != pstTime->iSourceStatus) ||
        (pstQuality->ullMessageAgeMs > pstConfig->uiMessageGapToHoldoverMs);
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
