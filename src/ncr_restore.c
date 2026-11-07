#include "ncr_internal.h"
#include <string.h>

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
