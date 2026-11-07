#ifndef NCR_TEST_SUPPORT_H
#define NCR_TEST_SUPPORT_H

#include "ncr_sync.h"
#include <pthread.h>

#define TEST_SOURCE_MS INT64_C(1794052800000)
#define CHECK(Condition) CheckTestCondition((Condition), #Condition, __FILE__, __LINE__)

typedef struct NcrTestClock {
    pthread_mutex_t stMutex;
    int64_t llRealtimeMs;
    uint64_t ullMonotonicMs;
    uint32_t uiReadCount;
    uint32_t uiFailReadNumber;
    uint32_t uiStepCalls;
    uint32_t uiStepDurationMs;
    bool bFailStep;
    bool bFailReadAfterStep;
} NcrTestClock_t;

void CheckTestCondition(bool bCondition, const char *pcCondition,
                        const char *pcFile, uint32_t uiLine);
void InitializeTestClock(NcrTestClock_t *pstClock, int64_t llRealtimeMs);
void DestroyTestClock(NcrTestClock_t *pstClock);
void AdvanceTestClock(NcrTestClock_t *pstClock, uint32_t uiElapsedMs);
void ShiftTestRealtime(NcrTestClock_t *pstClock, int64_t llDifferenceMs);
NcrPlatform_t GetTestPlatform(NcrTestClock_t *pstClock);
GpsTime_t MakeTestGps(int64_t llSourceMs, NcrClockStatus iStatus);
void EncodeTestContext(int64_t llSourceMs, uint8_t aucContext[NCR_CONTEXT_LENGTH]);
void AcquireTestClock(NcrSync_t *pstSync, NcrTestClock_t *pstClock,
                      int64_t llFirstSourceMs);
NcrSync_t *CreateTestSync(const NcrConfig_t *pstConfig, NcrTestClock_t *pstClock);

#endif
