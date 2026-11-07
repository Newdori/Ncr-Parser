#include "ncr_sync.h"

int main(void)
{
    HostGpsTime_t stHostGps = {2026U, 11U, 7U, 12U, 0U, 0U, 0U, 1U};
    int64_t llSourceMs = 0;
    if (NCR_STATUS_OK != ConvertGpsTimeToSourceMs(&stHostGps, &llSourceMs)) {
        return 1;
    }
    if (INT64_C(1794052800000) != llSourceMs) {
        return 1;
    }
    stHostGps.uiMillisecond = 65536U;
    if (NCR_STATUS_INVALID_GPS != ValidateGpsTime(&stHostGps)) {
        return 1;
    }
    return 0;
}
