#include "ncr_test_support.h"
#include <stdio.h>
#include <string.h>

static NcrTimeDecision_t ObserveSample(NcrSync_t *pstSync, int64_t llSourceMs,
                                       NcrClockStatus iSourceStatus)
{
    GpsTime_t stGps = MakeTestGps(llSourceMs, iSourceStatus);
    NcrTimeDecision_t stDecision;
    NcrStatus iStatus = UpdateGpsTime(pstSync, &stGps, &stDecision);
    CHECK(iStatus == stDecision.iStatus);
    return stDecision;
}

static NcrSyncStatus_t ReadStatus(NcrSync_t *pstSync)
{
    NcrSyncStatus_t stStatus;
    CHECK(NCR_STATUS_OK == GetNcrSyncStatus(pstSync, &stStatus));
    return stStatus;
}

static void TestStartupAndStatus(void)
{
    NcrConfig_t stConfig = GetNcrDefaultConfig();
    NcrTestClock_t stClock;
    NcrSync_t *pstSync;
    NcrTimeDecision_t stDecision;
    NcrSyncStatus_t stStatus;
    GpsTime_t stGps;
    uint32_t uiIndex;
    InitializeTestClock(&stClock, TEST_SOURCE_MS);
    pstSync = CreateTestSync(&stConfig, &stClock);
    stDecision = ObserveSample(pstSync, TEST_SOURCE_MS, NCR_CLOCK_HOLDOVER);
    CHECK(NCR_RESTORE_NOT_SYNCED == stDecision.stQuality.iUsability);
    CHECK(!stDecision.stQuality.bHasLockedReference);
    CHECK(NCR_STATUS_OK == ResetNcrSync(pstSync));
    AcquireTestClock(pstSync, &stClock, TEST_SOURCE_MS);
    stStatus = ReadStatus(pstSync);
    CHECK(NCR_STATE_READY == stStatus.stQuality.iState);
    AdvanceTestClock(&stClock, 1000U);
    stGps = MakeTestGps(TEST_SOURCE_MS + 3000, NCR_CLOCK_LOCKED);
    stGps.ucClockStatus = 99U;
    CHECK(NCR_STATUS_INVALID_GPS == UpdateGpsTime(pstSync, &stGps, &stDecision));
    CHECK(1000U == stDecision.stQuality.ullMessageAgeMs);
    stDecision = ObserveSample(pstSync, TEST_SOURCE_MS + 3000, NCR_CLOCK_UNSYNCED);
    CHECK(NCR_STATUS_UNSYNCED == stDecision.iStatus);
    CHECK(NCR_CLOCK_UNSYNCED == stDecision.stQuality.iSourceStatus);
    CHECK(NCR_STATE_HOLDOVER == stDecision.stQuality.iState);
    CHECK(stDecision.stQuality.bHoldover);
    for (uiIndex = 4U; uiIndex <= 6U; uiIndex++) {
        AdvanceTestClock(&stClock, 1000U);
        stDecision = ObserveSample(pstSync, TEST_SOURCE_MS + (int64_t)uiIndex * 1000,
                                    NCR_CLOCK_LOCKED);
        CHECK(NCR_STATUS_OK == stDecision.iStatus);
    }
    CHECK(NCR_STATE_READY == stDecision.stQuality.iState);
    CHECK(!stDecision.stQuality.bHoldover);
    stDecision = ObserveSample(pstSync, TEST_SOURCE_MS + 6000, NCR_CLOCK_LOCKED);
    CHECK(NCR_STATUS_DUPLICATE == stDecision.iStatus);
    DestroyNcrSync(pstSync);
    DestroyTestClock(&stClock);
    puts("PASS startup/HOLDOVER/status99/UNSYNCED/requalification/duplicate");
}

static void TestContinuousHoldoverAndTimeouts(void)
{
    NcrConfig_t stConfig = GetNcrDefaultConfig();
    NcrTestClock_t stClock;
    NcrSync_t *pstSync;
    NcrTimeDecision_t stDecision;
    NcrSyncStatus_t stStatus;
    uint32_t uiIndex;
    stConfig.uiMaxLocalHoldoverMs = 1000000U;
    InitializeTestClock(&stClock, TEST_SOURCE_MS);
    pstSync = CreateTestSync(&stConfig, &stClock);
    AcquireTestClock(pstSync, &stClock, TEST_SOURCE_MS);
    for (uiIndex = 3U; uiIndex <= 362U; uiIndex++) {
        AdvanceTestClock(&stClock, 1000U);
        stDecision = ObserveSample(pstSync, TEST_SOURCE_MS + (int64_t)uiIndex * 1000,
                                    NCR_CLOCK_HOLDOVER);
        CHECK(NCR_STATUS_OK == stDecision.iStatus);
    }
    CHECK(0U == stDecision.stQuality.ullMessageAgeMs);
    CHECK(359000U == stDecision.stQuality.ullSourceHoldoverAgeMs);
    CHECK(NCR_RESTORE_STALE == stDecision.stQuality.iUsability);
    CHECK(NCR_STATE_STALE == stDecision.stQuality.iState);
    for (uiIndex = 363U; uiIndex <= 365U; uiIndex++) {
        AdvanceTestClock(&stClock, 1000U);
        stDecision = ObserveSample(pstSync, TEST_SOURCE_MS + (int64_t)uiIndex * 1000,
                                    NCR_CLOCK_LOCKED);
    }
    CHECK(NCR_STATE_READY == stDecision.stQuality.iState);
    AdvanceTestClock(&stClock, 3001U);
    stStatus = ReadStatus(pstSync);
    CHECK(NCR_STATE_HOLDOVER == stStatus.stQuality.iState);
    AdvanceTestClock(&stClock, 300000U);
    stStatus = ReadStatus(pstSync);
    CHECK(NCR_RESTORE_STALE == stStatus.stQuality.iUsability);
    DestroyNcrSync(pstSync);
    DestroyTestClock(&stClock);
    puts("PASS continuous source HOLDOVER expiry/return/message loss");
}

static void TestReacquisitionAndResetGuard(void)
{
    NcrConfig_t stConfig = GetNcrDefaultConfig();
    NcrTestClock_t stClock;
    NcrSync_t *pstSync;
    NcrTimeDecision_t stDecision;
    uint32_t uiIndex;
    InitializeTestClock(&stClock, TEST_SOURCE_MS);
    pstSync = CreateTestSync(&stConfig, &stClock);
    stDecision = ObserveSample(pstSync, TEST_SOURCE_MS + 1000, NCR_CLOCK_LOCKED);
    CHECK(NCR_STATUS_OK == stDecision.iStatus);
    AdvanceTestClock(&stClock, 1000U);
    stDecision = ObserveSample(pstSync, TEST_SOURCE_MS + 1000, NCR_CLOCK_LOCKED);
    CHECK(NCR_STATUS_DUPLICATE == stDecision.iStatus);
    for (uiIndex = 2U; uiIndex <= 4U; uiIndex++) {
        AdvanceTestClock(&stClock, 1000U);
        stDecision = ObserveSample(pstSync, TEST_SOURCE_MS + (int64_t)uiIndex * 1000,
                                    NCR_CLOCK_LOCKED);
    }
    CHECK(NCR_STATE_READY == stDecision.stQuality.iState);
    CHECK(NCR_STATUS_OK == ResetNcrSync(pstSync));
    stDecision = ObserveSample(pstSync, TEST_SOURCE_MS + 124000, NCR_CLOCK_LOCKED);
    CHECK(NCR_STATUS_RESET_REQUIRED == stDecision.iStatus);
    AdvanceTestClock(&stClock, 1000U);
    stDecision = ObserveSample(pstSync, TEST_SOURCE_MS + 5000, NCR_CLOCK_LOCKED);
    CHECK(NCR_STATUS_RESET_REQUIRED == stDecision.iStatus);
    CHECK(NCR_STATUS_OK == ResetNcrSync(pstSync));
    AcquireTestClock(pstSync, &stClock, TEST_SOURCE_MS + 5000);
    CHECK(NCR_STATE_READY == ReadStatus(pstSync).stQuality.iState);
    DestroyNcrSync(pstSync);
    DestroyTestClock(&stClock);
    puts("PASS source +1s correction reacquires/large offset guard/manual reset");
}

static void TestOffsetAndSourceTimescale(void)
{
    NcrConfig_t stConfig = GetNcrDefaultConfig();
    NcrTestClock_t stClock;
    NcrSync_t *pstSync;
    NcrReceiveContext_t stContext;
    NcrRestoreResult_t stResult;
    uint8_t aucPayload[NCR_CONTEXT_LENGTH];
    InitializeTestClock(&stClock, TEST_SOURCE_MS - 1000);
    pstSync = CreateTestSync(&stConfig, &stClock);
    AcquireTestClock(pstSync, &stClock, TEST_SOURCE_MS);
    AdvanceTestClock(&stClock, 300U);
    CHECK(NCR_STATUS_OK == CaptureNcrReceiveContext(pstSync, &stContext));
    CHECK(stContext.stQuality.ullUncertaintyMs >= 1000U);
    EncodeTestContext(TEST_SOURCE_MS + 2000, aucPayload);
    CHECK(NCR_RESTORE_OK == RestoreNcr24(pstSync, &stContext, aucPayload,
                                         NCR_CONTEXT_LENGTH, &stResult));
    CHECK(TEST_SOURCE_MS + 2000 == stResult.llSourceTxSecondMs);
    DestroyNcrSync(pstSync);
    DestroyTestClock(&stClock);
    stConfig.iSourceMinusUtcSec = 18;
    InitializeTestClock(&stClock, TEST_SOURCE_MS - 18000);
    pstSync = CreateTestSync(&stConfig, &stClock);
    AcquireTestClock(pstSync, &stClock, TEST_SOURCE_MS);
    CHECK(0 == ReadStatus(pstSync).stQuality.llTrustedOffsetMs);
    CHECK(NCR_STATUS_OK == CaptureNcrReceiveContext(pstSync, &stContext));
    CHECK(TEST_SOURCE_MS + 2000 == stContext.llReceiveSourceMs);
    DestroyNcrSync(pstSync);
    DestroyTestClock(&stClock);
    puts("PASS allowed local offset participates in window/GPS-UTC conversion");
}

static void TestStepsAndContexts(void)
{
    NcrConfig_t stConfig = GetNcrDefaultConfig();
    NcrTestClock_t stClock;
    NcrSync_t *pstSync;
    NcrTimeDecision_t stDecision;
    NcrReceiveContext_t stContext;
    NcrRestoreResult_t stResult;
    uint8_t aucPayload[NCR_CONTEXT_LENGTH];
    stConfig.bAllowClockStep = true;
    stConfig.uiConfirmSteps = 4U;
    InitializeTestClock(&stClock, TEST_SOURCE_MS - 1000);
    stClock.uiStepDurationMs = 120U;
    pstSync = CreateTestSync(&stConfig, &stClock);
    AcquireTestClock(pstSync, &stClock, TEST_SOURCE_MS);
    CHECK(NCR_STATUS_OK == CaptureNcrReceiveContext(pstSync, &stContext));
    EncodeTestContext(TEST_SOURCE_MS + 2000, aucPayload);
    AdvanceTestClock(&stClock, 1000U);
    stDecision = ObserveSample(pstSync, TEST_SOURCE_MS + 3000, NCR_CLOCK_LOCKED);
    CHECK(NCR_TIME_STEP_APPLIED == stDecision.iAction);
    CHECK(stDecision.bStepApplied && stDecision.bStepVerified);
    CHECK(1000 == stDecision.llAppliedStepMs);
    CHECK(0 == stDecision.stQuality.llTrustedOffsetMs);
    CHECK(NCR_RESTORE_OK == RestoreNcr24(pstSync, &stContext, aucPayload,
                                         NCR_CONTEXT_LENGTH, &stResult));
    CHECK(NCR_STATUS_OK == ResetNcrSync(pstSync));
    CHECK(NCR_RESTORE_CONTEXT_INVALIDATED == RestoreNcr24(pstSync, &stContext,
        aucPayload, NCR_CONTEXT_LENGTH, &stResult));
    DestroyNcrSync(pstSync);
    DestroyTestClock(&stClock);
    puts("PASS verified fake Step excludes elapsed time/pre-Step queued context/reset invalidation");
}

static void TestInterruptedStepsAndFailures(void)
{
    NcrConfig_t stConfig = GetNcrDefaultConfig();
    NcrTestClock_t stClock;
    NcrSync_t *pstSync;
    NcrTimeDecision_t stDecision;
    uint32_t uiIndex;
    InitializeTestClock(&stClock, TEST_SOURCE_MS - 1000);
    pstSync = CreateTestSync(&stConfig, &stClock);
    stDecision = ObserveSample(pstSync, TEST_SOURCE_MS, NCR_CLOCK_LOCKED);
    CHECK(1U == stDecision.uiConfirmCount);
    AdvanceTestClock(&stClock, 1000U);
    (void)ObserveSample(pstSync, TEST_SOURCE_MS + 1000, NCR_CLOCK_UNSYNCED);
    for (uiIndex = 2U; uiIndex <= 3U; uiIndex++) {
        AdvanceTestClock(&stClock, 1000U);
        stDecision = ObserveSample(pstSync, TEST_SOURCE_MS + (int64_t)uiIndex * 1000,
                                    NCR_CLOCK_LOCKED);
    }
    CHECK(NCR_TIME_STEP_RECOMMENDED != stDecision.iAction);
    CHECK(2U == stDecision.uiConfirmCount);
    CHECK(0U == stClock.uiStepCalls);
    DestroyNcrSync(pstSync);
    DestroyTestClock(&stClock);
    stConfig.bAllowClockStep = true;
    InitializeTestClock(&stClock, TEST_SOURCE_MS - 1000);
    stClock.bFailStep = true;
    pstSync = CreateTestSync(&stConfig, &stClock);
    for (uiIndex = 0U; uiIndex < 3U; uiIndex++) {
        if (uiIndex > 0U) {
            AdvanceTestClock(&stClock, 1000U);
        }
        stDecision = ObserveSample(pstSync, TEST_SOURCE_MS + (int64_t)uiIndex * 1000,
                                    NCR_CLOCK_LOCKED);
    }
    CHECK(NCR_STATUS_PERMISSION_DENIED == stDecision.iStatus);
    CHECK(!stDecision.bStepApplied);
    CHECK(1000 == stDecision.stQuality.llTrustedOffsetMs);
    DestroyNcrSync(pstSync);
    DestroyTestClock(&stClock);
    InitializeTestClock(&stClock, TEST_SOURCE_MS - 1000);
    stClock.bFailReadAfterStep = true;
    pstSync = CreateTestSync(&stConfig, &stClock);
    for (uiIndex = 0U; uiIndex < 3U; uiIndex++) {
        if (uiIndex > 0U) {
            AdvanceTestClock(&stClock, 1000U);
        }
        stDecision = ObserveSample(pstSync, TEST_SOURCE_MS + (int64_t)uiIndex * 1000,
                                    NCR_CLOCK_LOCKED);
    }
    CHECK(NCR_STATUS_CLOCK_ERROR == stDecision.iStatus);
    CHECK(stDecision.bStepApplied && !stDecision.bStepVerified);
    CHECK(NCR_STATE_REACQUIRING == ReadStatus(pstSync).stQuality.iState);
    DestroyNcrSync(pstSync);
    DestroyTestClock(&stClock);
    puts("PASS consecutive confirmation/disabled Step/permission failure/post-Step read failure");
}

static void TestQueueAndExternalJump(void)
{
    NcrConfig_t stConfig = GetNcrDefaultConfig();
    NcrTestClock_t stClock;
    NcrSync_t *pstSync;
    NcrReceiveContext_t stContext;
    NcrReceiveContext_t stBadContext;
    NcrRestoreResult_t stResult;
    NcrTimeDecision_t stDecision;
    uint8_t aucPayload[NCR_CONTEXT_LENGTH];
    uint32_t uiIndex;
    InitializeTestClock(&stClock, TEST_SOURCE_MS);
    pstSync = CreateTestSync(&stConfig, &stClock);
    AcquireTestClock(pstSync, &stClock, TEST_SOURCE_MS);
    CHECK(NCR_STATUS_OK == CaptureNcrReceiveContext(pstSync, &stContext));
    EncodeTestContext(TEST_SOURCE_MS + 2000, aucPayload);
    AdvanceTestClock(&stClock, 1000U);
    (void)ObserveSample(pstSync, TEST_SOURCE_MS + 3000, NCR_CLOCK_LOCKED);
    CHECK(NCR_RESTORE_OK == RestoreNcr24(pstSync, &stContext, aucPayload, 3U, &stResult));
    stBadContext = stContext;
    stBadContext.pstOwner = NULL;
    CHECK(NCR_RESTORE_BAD_INPUT == RestoreNcr24(pstSync, &stBadContext, aucPayload, 3U, &stResult));
    CHECK(NCR_RESTORE_BAD_INPUT == RestoreNcr24(pstSync, &stContext, aucPayload, 2U, &stResult));
    CHECK(0U == stResult.uiNcr32);
    AdvanceTestClock(&stClock, 5000U);
    CHECK(NCR_RESTORE_QUEUE_EXPIRED == RestoreNcr24(pstSync, &stContext, aucPayload, 3U, &stResult));
    ShiftTestRealtime(&stClock, 1500);
    CHECK(NCR_STATUS_OK == CaptureNcrReceiveContext(pstSync, &stBadContext));
    CHECK(NCR_RESTORE_REACQUIRING == stBadContext.stQuality.iUsability);
    for (uiIndex = 9U; uiIndex <= 11U; uiIndex++) {
        AdvanceTestClock(&stClock, 1000U);
        stDecision = ObserveSample(pstSync, TEST_SOURCE_MS + (int64_t)uiIndex * 1000,
                                    NCR_CLOCK_LOCKED);
    }
    CHECK(NCR_STATE_READY == stDecision.stQuality.iState);
    CHECK(1U == ReadStatus(pstSync).ullClockJumps);
    stClock.uiFailReadNumber = stClock.uiReadCount + 1U;
    memset(&stBadContext, 0xA5, sizeof(stBadContext));
    CHECK(NCR_STATUS_CLOCK_ERROR == CaptureNcrReceiveContext(pstSync, &stBadContext));
    CHECK(0U == stBadContext.uiVersion);
    DestroyNcrSync(pstSync);
    DestroyTestClock(&stClock);
    puts("PASS GPS updates preserve queued context/expiry/external jump/read error");
}

static void TestUncertaintyAndArguments(void)
{
    NcrConfig_t stConfig = GetNcrDefaultConfig();
    NcrTestClock_t stClock;
    NcrSync_t *pstSync = NULL;
    NcrReceiveContext_t stContext;
    NcrRestoreResult_t stResult;
    NcrPlatform_t stPlatform;
    uint8_t aucPayload[3] = {0U};
    stConfig.uiMaxUncertaintyMs = 2U;
    stConfig.uiLocalDriftPpm = 1000000U;
    InitializeTestClock(&stClock, TEST_SOURCE_MS);
    pstSync = CreateTestSync(&stConfig, &stClock);
    AcquireTestClock(pstSync, &stClock, TEST_SOURCE_MS);
    AdvanceTestClock(&stClock, 1U);
    CHECK(NCR_STATUS_OK == CaptureNcrReceiveContext(pstSync, &stContext));
    CHECK(NCR_RESTORE_UNCERTAINTY == stContext.stQuality.iUsability);
    CHECK(NCR_RESTORE_UNCERTAINTY == RestoreNcr24(pstSync, &stContext, aucPayload, 3U, &stResult));
    CHECK(NCR_STATUS_BAD_ARGUMENT == GetNcrSyncStatus(pstSync, NULL));
    CHECK(NCR_STATUS_BAD_ARGUMENT == ResetNcrSync(NULL));
    DestroyNcrSync(pstSync);
    stPlatform = GetTestPlatform(&stClock);
    stPlatform.pSetClock = NULL;
    stConfig.bAllowClockStep = true;
    CHECK(NCR_STATUS_UNSUPPORTED == CreateNcrSyncWithPlatform(&stConfig, &stPlatform, &pstSync));
    CHECK(NULL == pstSync);
    stConfig.uiAcquireSamples = 0U;
    CHECK(NCR_STATUS_BAD_CONFIG == CreateNcrSyncWithPlatform(&stConfig, &stPlatform, &pstSync));
    DestroyNcrSync(NULL);
    DestroyTestClock(&stClock);
    puts("PASS uncertainty growth/output contracts/unsupported platform/invalid arguments");
}

static void TestOffsetDirectionAndDefaultPlatform(void)
{
    NcrConfig_t stConfig = GetNcrDefaultConfig();
    NcrTestClock_t stClock;
    NcrSync_t *pstSync = NULL;
    NcrTimeDecision_t stDecision;
    NcrSyncStatus_t stStatus;
    NcrReceiveContext_t stContext;
    stConfig.uiStepThresholdMs = 10U;
    InitializeTestClock(&stClock, TEST_SOURCE_MS - 100);
    pstSync = CreateTestSync(&stConfig, &stClock);
    (void)ObserveSample(pstSync, TEST_SOURCE_MS, NCR_CLOCK_LOCKED);
    AdvanceTestClock(&stClock, 1000U);
    ShiftTestRealtime(&stClock, 200);
    stDecision = ObserveSample(pstSync, TEST_SOURCE_MS + 1000, NCR_CLOCK_LOCKED);
    CHECK(1U == stDecision.uiConfirmCount);
    AdvanceTestClock(&stClock, 1000U);
    ShiftTestRealtime(&stClock, -200);
    stDecision = ObserveSample(pstSync, TEST_SOURCE_MS + 2000, NCR_CLOCK_LOCKED);
    CHECK(1U == stDecision.uiConfirmCount);
    CHECK(NCR_TIME_STEP_RECOMMENDED != stDecision.iAction);
    DestroyNcrSync(pstSync);
    DestroyTestClock(&stClock);
    stConfig = GetNcrDefaultConfig();
    CHECK(NCR_STATUS_OK == CreateNcrSync(&stConfig, &pstSync));
    CHECK(NCR_STATUS_OK == GetNcrSyncStatus(pstSync, &stStatus));
    CHECK(NCR_STATE_UNINITIALIZED == stStatus.stQuality.iState);
    CHECK(NCR_STATUS_OK == CaptureNcrReceiveContext(pstSync, &stContext));
    CHECK(NCR_RESTORE_NOT_SYNCED == stContext.stQuality.iUsability);
    DestroyNcrSync(pstSync);
    puts("PASS offset sign changes reset confirmation/default platform read-only clocks");
}

int main(void)
{
    TestStartupAndStatus();
    TestContinuousHoldoverAndTimeouts();
    TestReacquisitionAndResetGuard();
    TestOffsetAndSourceTimescale();
    TestStepsAndContexts();
    TestInterruptedStepsAndFailures();
    TestQueueAndExternalJump();
    TestUncertaintyAndArguments();
    TestOffsetDirectionAndDefaultPlatform();
    puts("ALL STATE TESTS PASSED");
    return 0;
}
