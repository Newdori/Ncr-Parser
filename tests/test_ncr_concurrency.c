#include "ncr_test_support.h"
#include <stdatomic.h>
#include <stdio.h>

typedef struct NcrThreadJob {
    NcrSync_t *pstSync;
    NcrTestClock_t *pstClock;
    _Atomic uint32_t uiFailures;
    _Atomic uint32_t uiSuccesses;
} NcrThreadJob_t;

static void *RunGpsWriter(void *pvContext)
{
    NcrThreadJob_t *pstJob = pvContext;
    uint32_t uiIndex;
    for (uiIndex = 3U; uiIndex < 10003U; uiIndex++) {
        NcrTimeDecision_t stDecision;
        GpsTime_t stGps = MakeTestGps(TEST_SOURCE_MS + (int64_t)uiIndex * 1000,
                                      NCR_CLOCK_LOCKED);
        AdvanceTestClock(pstJob->pstClock, 1000U);
        if (NCR_STATUS_OK != UpdateGpsTime(pstJob->pstSync, &stGps, &stDecision)) {
            (void)atomic_fetch_add(&pstJob->uiFailures, 1U);
        }
    }
    return NULL;
}

static void *RunRleReader(void *pvContext)
{
    NcrThreadJob_t *pstJob = pvContext;
    uint32_t uiIndex;
    for (uiIndex = 0U; uiIndex < 10000U; uiIndex++) {
        NcrReceiveContext_t stContext;
        NcrRestoreResult_t stResult;
        uint8_t aucPayload[NCR_CONTEXT_LENGTH];
        if (NCR_STATUS_OK != CaptureNcrReceiveContext(pstJob->pstSync, &stContext)) {
            (void)atomic_fetch_add(&pstJob->uiFailures, 1U);
            continue;
        }
        EncodeTestContext(stContext.llReceiveSourceMs, aucPayload);
        if ((NCR_RESTORE_OK != RestoreNcr24(pstJob->pstSync, &stContext,
                                            aucPayload, NCR_CONTEXT_LENGTH, &stResult)) ||
            (stResult.llSourceTxSecondMs != stContext.llReceiveSourceMs / 1000 * 1000)) {
            (void)atomic_fetch_add(&pstJob->uiFailures, 1U);
        }
        else {
            (void)atomic_fetch_add(&pstJob->uiSuccesses, 1U);
        }
    }
    return NULL;
}

int main(void)
{
    NcrConfig_t stConfig = GetNcrDefaultConfig();
    NcrTestClock_t stClock;
    NcrThreadJob_t stJob = {0};
    pthread_t stWriter;
    pthread_t astReaders[4];
    uint32_t uiIndex;
    stConfig.uiMaxQueueAgeMs = 20000000U;
    InitializeTestClock(&stClock, TEST_SOURCE_MS);
    stJob.pstSync = CreateTestSync(&stConfig, &stClock);
    stJob.pstClock = &stClock;
    atomic_init(&stJob.uiFailures, 0U);
    atomic_init(&stJob.uiSuccesses, 0U);
    AcquireTestClock(stJob.pstSync, &stClock, TEST_SOURCE_MS);
    CHECK(0 == pthread_create(&stWriter, NULL, RunGpsWriter, &stJob));
    for (uiIndex = 0U; uiIndex < 4U; uiIndex++) {
        CHECK(0 == pthread_create(&astReaders[uiIndex], NULL, RunRleReader, &stJob));
    }
    CHECK(0 == pthread_join(stWriter, NULL));
    for (uiIndex = 0U; uiIndex < 4U; uiIndex++) {
        CHECK(0 == pthread_join(astReaders[uiIndex], NULL));
    }
    CHECK(0U == atomic_load(&stJob.uiFailures));
    CHECK(40000U == atomic_load(&stJob.uiSuccesses));
    DestroyNcrSync(stJob.pstSync);
    DestroyTestClock(&stClock);
    puts("PASS concurrency: 10000 GPS updates + 4 readers, 40000 captures/restores");
    return 0;
}
