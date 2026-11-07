#include "ncr_sync.h"

const char *GetNcrStatusText(NcrStatus iStatus)
{
    switch (iStatus) {
    case NCR_STATUS_OK:
        return "NCR_STATUS_OK";
    case NCR_STATUS_BAD_ARGUMENT:
        return "NCR_STATUS_BAD_ARGUMENT";
    case NCR_STATUS_BAD_CONFIG:
        return "NCR_STATUS_BAD_CONFIG";
    case NCR_STATUS_INVALID_GPS:
        return "NCR_STATUS_INVALID_GPS";
    case NCR_STATUS_UNSYNCED:
        return "NCR_STATUS_UNSYNCED";
    case NCR_STATUS_DUPLICATE:
        return "NCR_STATUS_DUPLICATE";
    case NCR_STATUS_REACQUIRING:
        return "NCR_STATUS_REACQUIRING";
    case NCR_STATUS_RESET_REQUIRED:
        return "NCR_STATUS_RESET_REQUIRED";
    case NCR_STATUS_CLOCK_ERROR:
        return "NCR_STATUS_CLOCK_ERROR";
    case NCR_STATUS_STEP_FAILED:
        return "NCR_STATUS_STEP_FAILED";
    case NCR_STATUS_PERMISSION_DENIED:
        return "NCR_STATUS_PERMISSION_DENIED";
    case NCR_STATUS_UNSUPPORTED:
        return "NCR_STATUS_UNSUPPORTED";
    case NCR_STATUS_NO_MEMORY:
        return "NCR_STATUS_NO_MEMORY";
    case NCR_STATUS_LOCK_ERROR:
        return "NCR_STATUS_LOCK_ERROR";
    default:
        return "NCR_STATUS_UNKNOWN";
    }
}

const char *GetNcrRestoreStatusText(NcrRestoreStatus iStatus)
{
    switch (iStatus) {
    case NCR_RESTORE_OK:
        return "NCR_RESTORE_OK";
    case NCR_RESTORE_BAD_INPUT:
        return "NCR_RESTORE_BAD_INPUT";
    case NCR_RESTORE_NOT_SYNCED:
        return "NCR_RESTORE_NOT_SYNCED";
    case NCR_RESTORE_REACQUIRING:
        return "NCR_RESTORE_REACQUIRING";
    case NCR_RESTORE_STALE:
        return "NCR_RESTORE_STALE";
    case NCR_RESTORE_CLOCK_OFFSET:
        return "NCR_RESTORE_CLOCK_OFFSET";
    case NCR_RESTORE_UNCERTAINTY:
        return "NCR_RESTORE_UNCERTAINTY";
    case NCR_RESTORE_NO_CANDIDATE:
        return "NCR_RESTORE_NO_CANDIDATE";
    case NCR_RESTORE_AMBIGUOUS:
        return "NCR_RESTORE_AMBIGUOUS";
    case NCR_RESTORE_QUEUE_EXPIRED:
        return "NCR_RESTORE_QUEUE_EXPIRED";
    case NCR_RESTORE_CONTEXT_INVALIDATED:
        return "NCR_RESTORE_CONTEXT_INVALIDATED";
    case NCR_RESTORE_CLOCK_ERROR:
        return "NCR_RESTORE_CLOCK_ERROR";
    default:
        return "NCR_RESTORE_UNKNOWN";
    }
}

const char *GetNcrStateText(NcrState iState)
{
    switch (iState) {
    case NCR_STATE_UNINITIALIZED:
        return "NCR_STATE_UNINITIALIZED";
    case NCR_STATE_ACQUIRING:
        return "NCR_STATE_ACQUIRING";
    case NCR_STATE_READY:
        return "NCR_STATE_READY";
    case NCR_STATE_HOLDOVER:
        return "NCR_STATE_HOLDOVER";
    case NCR_STATE_REACQUIRING:
        return "NCR_STATE_REACQUIRING";
    case NCR_STATE_STALE:
        return "NCR_STATE_STALE";
    default:
        return "NCR_STATE_UNKNOWN";
    }
}
