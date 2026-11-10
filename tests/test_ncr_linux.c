#define _POSIX_C_SOURCE 200809L
#include "ncr_test_support.h"
#include <stdio.h>
#include <time.h>

#if !defined(__linux__)
#error This test exercises the real Linux platform adapter
#endif

_Static_assert(sizeof(time_t) >= sizeof(int64_t), "Test requires 64-bit time_t");

static int64_t ReadRealtimeMs(void)
{
    struct timespec stTime;
    CHECK(0 == clock_gettime(CLOCK_REALTIME, &stTime));
    return (int64_t)stTime.tv_sec * 1000 + stTime.tv_nsec / 1000000;
}

static uint64_t ReadMonotonicMs(void)
{
    struct timespec stTime;
    CHECK(0 == clock_gettime(CLOCK_MONOTONIC, &stTime));
    return (uint64_t)stTime.tv_sec * 1000U + (uint64_t)stTime.tv_nsec / 1000000U;
}

int main(void)
{
    NcrConfig_t stConfig = GetNcrDefaultConfig();
    NcrSync_t *pstSync = NULL;
    NcrReceiveContext_t stContext;
    int64_t llBeforeRealMs;
    int64_t llAfterRealMs;
    uint64_t ullBeforeMonoMs;
    uint64_t ullAfterMonoMs;
    CHECK(!stConfig.bAllowClockStep);
    CHECK(NCR_STATUS_OK == CreateNcrSync(&stConfig, &pstSync));
    llBeforeRealMs = ReadRealtimeMs();
    ullBeforeMonoMs = ReadMonotonicMs();
    CHECK(NCR_STATUS_OK == CaptureNcrReceiveContext(pstSync, &stContext));
    ullAfterMonoMs = ReadMonotonicMs();
    llAfterRealMs = ReadRealtimeMs();
    CHECK(stContext.stReceiveStamp.llRealtimeMs >= llBeforeRealMs - 1);
    CHECK(stContext.stReceiveStamp.llRealtimeMs <= llAfterRealMs + 1);
    CHECK(stContext.stReceiveStamp.ullMonotonicMs >= ullBeforeMonoMs);
    CHECK(stContext.stReceiveStamp.ullMonotonicMs <= ullAfterMonoMs);
    CHECK(NCR_RESTORE_OK == stContext.stQuality.iUsability);
    CHECK(stContext.stQuality.bUbuntuFallback);
    DestroyNcrSync(pstSync);

    /* Registration needs no CAP_SYS_TIME. No GPS samples or time changes. */
    stConfig.bAllowClockStep = true;
    CHECK(NCR_STATUS_OK == CreateNcrSync(&stConfig, &pstSync));
    DestroyNcrSync(pstSync);
    puts("PASS real Linux clock adapter/64-bit time_t/opt-in Step registration");
    return 0;
}
