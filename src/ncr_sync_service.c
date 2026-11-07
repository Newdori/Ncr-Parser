#include "ncr_internal.h"
#include <stdlib.h>
#include <string.h>

NcrStatus ReadNcrServiceClock(NcrSync_t *pstSync, NcrClockStamp_t *pstStamp)
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
            IncrementNcrCounter(&pstSync->stCounters.ullClockJumps);
            IncrementNcrCounter(&pstSync->ullClockGeneration);
        }
    }
    pstSync->bHasClockStamp = true;
    pstSync->stLastClockStamp = *pstStamp;
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
    pstSync->ullClockGeneration = 1U;
    pstSync->ullPolicyGeneration = 1U;
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
        IncrementNcrCounter(&pstSync->stCounters.ullStepFailures);
        return iStatus;
    }
    pstDecision->bStepApplied = true;
    IncrementNcrCounter(&pstSync->stCounters.ullAppliedSteps);
    IncrementNcrCounter(&pstSync->ullClockGeneration);
    /* Do not compare this intentional step to the pre-step clock anchor. */
    pstSync->bHasClockStamp = false;
    iStatus = ReadNcrServiceClock(pstSync, &stAfter);
    if ((NCR_STATUS_OK != iStatus) ||
        (stAfter.ullMonotonicMs < stBefore.ullMonotonicMs)) {
        BeginNcrReacquisition(pstSync);
        pstDecision->iAction = NCR_TIME_STEP_FAILED;
        IncrementNcrCounter(&pstSync->stCounters.ullStepFailures);
        return NCR_STATUS_CLOCK_ERROR;
    }
    ullElapsed = stAfter.ullMonotonicMs - stBefore.ullMonotonicMs;
    llStepMs = stAfter.llRealtimeMs - stBefore.llRealtimeMs - (int64_t)ullElapsed;
    pstSync->stTime.stLastSample.llRealtimeMs += llStepMs;
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
        IncrementNcrCounter(&pstSync->stCounters.ullStepFailures);
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
        if ((NCR_STATUS_OK == iStatus) || (NCR_STATUS_REACQUIRING == iStatus)) {
            IncrementNcrCounter(&pstSync->stCounters.ullAcceptedSamples);
        }
        else {
            IncrementNcrCounter(&pstSync->stCounters.ullRejectedSamples);
        }
        if ((NCR_TIME_STEP_RECOMMENDED == pstDecision->iAction) &&
            pstSync->stConfig.bAllowClockStep) {
            iStatus = ApplyClockCorrection(pstSync, &stStamp, pstDecision);
        }
    }
    else {
        IncrementNcrCounter(&pstSync->stCounters.ullRejectedSamples);
    }
    pstDecision->iStatus = iStatus;
    pstSync->stCounters.iLastUpdateStatus = iStatus;
    if ((iStatus >= NCR_STATUS_OK) && (iStatus < NCR_STATUS_COUNT)) {
        IncrementNcrCounter(&pstSync->stCounters.aullUpdateByStatus[iStatus]);
    }
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
        IncrementNcrCounter(&pstSync->stCounters.ullRestoreSuccesses);
    }
    else {
        IncrementNcrCounter(&pstSync->stCounters.ullRestoreFailures);
    }
    if ((iStatus >= NCR_RESTORE_OK) && (iStatus < NCR_RESTORE_STATUS_COUNT)) {
        IncrementNcrCounter(&pstSync->stCounters.aullRestoreByStatus[iStatus]);
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
    *pstStatus = pstSync->stCounters;
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
    IncrementNcrCounter(&pstSync->ullClockGeneration);
    pstSync->stCounters.iLastUpdateStatus = NCR_STATUS_OK;
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
