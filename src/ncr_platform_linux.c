#define _POSIX_C_SOURCE 200809L
#include "ncr_internal.h"

#if !defined(__linux__)
#error This module requires an Ubuntu/Linux target compiler
#endif

#include <errno.h>
#include <stddef.h>
#include <time.h>

_Static_assert(sizeof(time_t) >= sizeof(int64_t), "Build with 64-bit time_t");

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

NcrPlatform_t GetNcrDefaultPlatform(void)
{
    NcrPlatform_t stPlatform = {ReadLinuxClock, SetLinuxClock, NULL};
    return stPlatform;
}
