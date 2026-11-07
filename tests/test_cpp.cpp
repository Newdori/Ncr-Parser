#include "ncr_sync.h"

int main()
{
    NcrConfig_t stConfig = GetNcrDefaultConfig();
    NcrSync_t *pstSync = nullptr;
    if (NCR_STATUS_OK == CreateNcrSync(&stConfig, &pstSync)) {
        DestroyNcrSync(pstSync);
        return 0;
    }
    return 1;
}
