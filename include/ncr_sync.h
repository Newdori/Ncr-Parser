#ifndef NCR_SYNC_H
#define NCR_SYNC_H

#include <stdbool.h>
#include <stdint.h>
#include "ncr_gps_time.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NCR_CONTEXT_LENGTH 3U
#define NCR_LSB24_MASK UINT32_C(0x00FFFFFF)
#define NCR_TICKS_PER_SECOND UINT64_C(90000)
#define NCR_RECEIVE_CONTEXT_VERSION 1U

typedef enum NcrStatus {
    NCR_STATUS_OK = 0,
    NCR_STATUS_BAD_ARGUMENT,
    NCR_STATUS_BAD_CONFIG,
    NCR_STATUS_INVALID_GPS,
    NCR_STATUS_UNSYNCED,
    NCR_STATUS_DUPLICATE,
    NCR_STATUS_REACQUIRING,
    NCR_STATUS_RESET_REQUIRED,
    NCR_STATUS_CLOCK_ERROR,
    NCR_STATUS_STEP_FAILED,
    NCR_STATUS_PERMISSION_DENIED,
    NCR_STATUS_UNSUPPORTED,
    NCR_STATUS_NO_MEMORY,
    NCR_STATUS_LOCK_ERROR,
    NCR_STATUS_COUNT
} NcrStatus;

typedef enum NcrClockStatus {
    NCR_CLOCK_UNSYNCED = 0,
    NCR_CLOCK_LOCKED = 1,
    NCR_CLOCK_HOLDOVER = 2
} NcrClockStatus;

typedef enum NcrState {
    NCR_STATE_UNINITIALIZED = 0,
    NCR_STATE_ACQUIRING,
    NCR_STATE_READY,
    NCR_STATE_HOLDOVER,
    NCR_STATE_REACQUIRING,
    NCR_STATE_STALE
} NcrState;

typedef enum NcrTimeAction {
    NCR_TIME_REJECTED = 0,
    NCR_TIME_ACCEPTED,
    NCR_TIME_REACQUIRING,
    NCR_TIME_STEP_RECOMMENDED,
    NCR_TIME_STEP_APPLIED,
    NCR_TIME_STEP_FAILED
} NcrTimeAction;

typedef enum NcrRestoreStatus {
    NCR_RESTORE_OK = 0,
    NCR_RESTORE_BAD_INPUT,
    NCR_RESTORE_NOT_SYNCED,
    NCR_RESTORE_REACQUIRING,
    NCR_RESTORE_STALE,
    NCR_RESTORE_CLOCK_OFFSET,
    NCR_RESTORE_UNCERTAINTY,
    NCR_RESTORE_NO_CANDIDATE,
    NCR_RESTORE_AMBIGUOUS,
    NCR_RESTORE_QUEUE_EXPIRED,
    NCR_RESTORE_CONTEXT_INVALIDATED,
    NCR_RESTORE_CLOCK_ERROR,
    NCR_RESTORE_STATUS_COUNT
} NcrRestoreStatus;

typedef enum NcrModel {
    NCR_MODEL_DAILY_SECONDS_90KHZ = 0
} NcrModel;

typedef struct NcrConfig {
    NcrModel iModel;
    bool bAllowClockStep;
    int32_t iSourceMinusUtcSec;
    uint32_t uiTimeMessageDelayMs;
    uint32_t uiAcquireSamples;
    uint32_t uiConfirmSteps;
    uint32_t uiStepThresholdMs;
    uint32_t uiMaxSampleJitterMs;
    uint32_t uiMessageGapToHoldoverMs;
    uint32_t uiMaxMessageAgeMs;
    uint32_t uiMaxSourceHoldoverMs;
    uint32_t uiMaxLocalHoldoverMs;
    uint32_t uiMaxClockOffsetMs;
    uint32_t uiMaxUncertaintyMs;
    uint32_t uiSourceUncertaintyMs;
    uint32_t uiReceiveUncertaintyMs;
    uint32_t uiDelayUncertaintyMs;
    uint32_t uiLocalDriftPpm;
    uint32_t uiExpectedLinkDelayMs;
    uint32_t uiMaxLinkDelayMs;
    uint32_t uiMaxQueueAgeMs;
    uint32_t uiMaxClockJumpMs;
    uint32_t uiMaxReacquireOffsetMs;
} NcrConfig_t;

typedef struct NcrClockStamp {
    int64_t llRealtimeMs;
    uint64_t ullMonotonicMs;
} NcrClockStamp_t;

/* Called while the service mutex is held: callbacks must not reenter it.
 * pReadClock must return a coherent, bounded-span realtime/monotonic pair.
 * Caller owns pvContext and keeps it alive until all service calls finish. */
typedef struct NcrPlatform {
    NcrStatus (*pReadClock)(void *pvContext, NcrClockStamp_t *pstStamp);
    NcrStatus (*pSetClock)(void *pvContext, int64_t llTargetUtcMs);
    void *pvContext;
} NcrPlatform_t;

typedef struct NcrTimeQuality {
    NcrState iState;
    NcrClockStatus iSourceStatus;
    NcrRestoreStatus iUsability;
    bool bHoldover;
    bool bHasLockedReference;
    uint64_t ullMessageAgeMs;
    uint64_t ullLockedAgeMs;
    uint64_t ullSourceHoldoverAgeMs;
    uint64_t ullUncertaintyMs;
    int64_t llObservedOffsetMs;
    int64_t llTrustedOffsetMs;
    uint64_t ullClockGeneration;
    uint64_t ullPolicyGeneration;
} NcrTimeQuality_t;

typedef struct NcrTimeDecision {
    NcrTimeAction iAction;
    NcrStatus iStatus;
    int64_t llOffsetMs;
    int64_t llTargetUtcAtReceiveMs;
    int64_t llAppliedStepMs;
    bool bStepAttempted;
    bool bStepApplied;
    bool bStepVerified;
    uint32_t uiConfirmCount;
    NcrTimeQuality_t stQuality;
} NcrTimeDecision_t;

typedef struct NcrSync NcrSync_t;

/* Read-only after capture. Keep this value beside a queued RLE packet.
 * The service must outlive all calls using its contexts. */
typedef struct NcrReceiveContext {
    uint32_t uiVersion;
    const NcrSync_t *pstOwner;
    NcrClockStamp_t stReceiveStamp;
    int64_t llReceiveSourceMs;
    NcrTimeQuality_t stQuality;
} NcrReceiveContext_t;

typedef struct NcrRestoreResult {
    uint32_t uiNcr32;
    int64_t llSourceTxSecondMs;
    uint16_t usYear;
    uint8_t ucMonth;
    uint8_t ucDay;
    NcrTimeQuality_t stQuality;
} NcrRestoreResult_t;

typedef struct NcrSyncStatus {
    NcrTimeQuality_t stQuality;
    uint64_t ullAcceptedSamples;
    uint64_t ullRejectedSamples;
    uint64_t ullReacquireEvents;
    uint64_t ullClockJumps;
    uint64_t ullAppliedSteps;
    uint64_t ullStepFailures;
    uint64_t ullRestoreSuccesses;
    uint64_t ullRestoreFailures;
    uint64_t aullUpdateByStatus[NCR_STATUS_COUNT];
    uint64_t aullRestoreByStatus[NCR_RESTORE_STATUS_COUNT];
    NcrStatus iLastUpdateStatus;
} NcrSyncStatus_t;

/* Defaults are development examples; configure measured uncertainty for hardware. */
NcrConfig_t GetNcrDefaultConfig(void);
NcrStatus ValidateNcrConfig(const NcrConfig_t *pstConfig);
NcrStatus ValidateGpsTime(const GpsTime_t *pstGpsTime);
NcrStatus ConvertGpsTimeToSourceMs(const GpsTime_t *pstGpsTime,
                                  int64_t *pllSourceMs);
NcrStatus CreateNcrSync(const NcrConfig_t *pstConfig, NcrSync_t **ppstSync);
NcrStatus CreateNcrSyncWithPlatform(const NcrConfig_t *pstConfig,
                                   const NcrPlatform_t *pstPlatform,
                                   NcrSync_t **ppstSync);
NcrStatus UpdateGpsTime(NcrSync_t *pstSync, const GpsTime_t *pstGpsTime,
                        NcrTimeDecision_t *pstDecision);
NcrStatus CaptureNcrReceiveContext(NcrSync_t *pstSync,
                                  NcrReceiveContext_t *pstReceiveContext);
/* Service argument permits queue age and policy generation validation. */
NcrRestoreStatus RestoreNcr24(NcrSync_t *pstSync,
                              const NcrReceiveContext_t *pstReceiveContext,
                              const uint8_t *pucContext, uint32_t uiContextLength,
                              NcrRestoreResult_t *pstResult);
/* Pure, deterministic core; does not inspect synchronization or queue age. */
NcrRestoreStatus ReconstructNcr24(uint32_t uiLsb24, int64_t llReceiveSourceMs,
                                  uint32_t uiUncertaintyMs,
                                  const NcrConfig_t *pstConfig,
                                  NcrRestoreResult_t *pstResult);
NcrStatus GetNcrSyncStatus(NcrSync_t *pstSync, NcrSyncStatus_t *pstStatus);
/* Invalidates queued contexts. Does not bypass the configured offset guard. */
NcrStatus ResetNcrSync(NcrSync_t *pstSync);
/* Stop/join callers before destroying the service. */
void DestroyNcrSync(NcrSync_t *pstSync);
const char *GetNcrStatusText(NcrStatus iStatus);
const char *GetNcrRestoreStatusText(NcrRestoreStatus iStatus);
const char *GetNcrStateText(NcrState iState);

#ifdef __cplusplus
}
#endif
#endif
