#include "ncr_internal.h"
#include <stddef.h>

NcrConfig_t GetNcrDefaultConfig(void)
{
    NcrConfig_t stConfig = {0};
    stConfig.iModel = NCR_MODEL_DAILY_SECONDS_90KHZ;
    stConfig.uiAcquireSamples = 3U;
    stConfig.uiConfirmSteps = 3U;
    stConfig.uiStepThresholdMs = 500U;
    stConfig.uiMaxSampleJitterMs = 250U;
    stConfig.uiMessageGapToHoldoverMs = 3000U;
    stConfig.uiMaxMessageAgeMs = 300000U;
    stConfig.uiMaxSourceHoldoverMs = 300000U;
    stConfig.uiMaxLocalHoldoverMs = 300000U;
    stConfig.uiMaxClockOffsetMs = 2000U;
    stConfig.uiMaxUncertaintyMs = 2000U;
    stConfig.uiSourceUncertaintyMs = 1U;
    stConfig.uiReceiveUncertaintyMs = 1U;
    stConfig.uiLocalDriftPpm = 100U;
    stConfig.uiExpectedLinkDelayMs = 300U;
    stConfig.uiMaxLinkDelayMs = 5000U;
    stConfig.uiMaxQueueAgeMs = 5000U;
    stConfig.uiMaxClockJumpMs = 1000U;
    stConfig.uiMaxReacquireOffsetMs = 60000U;
    return stConfig;
}

NcrStatus ValidateNcrConfig(const NcrConfig_t *pstConfig)
{
    uint64_t ullBaseUncertainty;
    uint64_t ullWindow;
    if (NULL == pstConfig) {
        return NCR_STATUS_BAD_CONFIG;
    }
    ullBaseUncertainty = (uint64_t)pstConfig->uiSourceUncertaintyMs +
                        pstConfig->uiReceiveUncertaintyMs +
                        pstConfig->uiDelayUncertaintyMs;
    ullWindow = (uint64_t)pstConfig->uiMaxLinkDelayMs +
                2U * (uint64_t)pstConfig->uiMaxUncertaintyMs + 999U;
    if ((NCR_MODEL_DAILY_SECONDS_90KHZ != pstConfig->iModel) ||
        (pstConfig->iSourceMinusUtcSec < -86400) ||
        (pstConfig->iSourceMinusUtcSec > 86400) ||
        (pstConfig->uiTimeMessageDelayMs > 60000U) ||
        (0U == pstConfig->uiAcquireSamples) ||
        (pstConfig->uiAcquireSamples > 1000U) ||
        (0U == pstConfig->uiConfirmSteps) ||
        (pstConfig->uiConfirmSteps > 1000U) ||
        (0U == pstConfig->uiMessageGapToHoldoverMs) ||
        (pstConfig->uiMessageGapToHoldoverMs > pstConfig->uiMaxMessageAgeMs) ||
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
        (pstConfig->uiExpectedLinkDelayMs > pstConfig->uiMaxLinkDelayMs) ||
        (pstConfig->uiMaxLinkDelayMs >= 90000U) || (ullWindow >= 186413U)) {
        return NCR_STATUS_BAD_CONFIG;
    }
    return NCR_STATUS_OK;
}

uint64_t GetNcrMagnitude(int64_t llValue)
{
    if (llValue < 0) {
        return (uint64_t)(-(llValue + 1)) + 1U;
    }
    return (uint64_t)llValue;
}

void IncrementNcrCounter(uint64_t *pullCounter)
{
    if (*pullCounter < UINT64_MAX) {
        (*pullCounter)++;
    }
}
