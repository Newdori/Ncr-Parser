#include "ncr_sync.h"

/* Call once during application initialization. The host supplies its measured
 * timing configuration. Default configuration leaves system time changes off. */
NcrStatus InitializeGatewayClock(const NcrConfig_t *pstConfig, NcrSync_t **ppstSync)
{
    return CreateNcrSync(pstConfig, ppstSync);
}

/* Existing GPS receive thread: call immediately after receiving a complete,
 * stable GpsTime_t. One call per received sample, normally once per second.
 * Check the return and pstDecision; no new thread or timer is created here. */
NcrStatus HandleReceivedGpsTime(NcrSync_t *pstSync, const GpsTime_t *pstGpsTime,
                               NcrTimeDecision_t *pstDecision)
{
    return UpdateGpsTime(pstSync, pstGpsTime, pstDecision);
}

/* RLE receive thread: call at packet arrival, then store the returned value
 * alongside that packet through queues and reassembly. */
NcrStatus CaptureRlePacketTime(NcrSync_t *pstSync, NcrReceiveContext_t *pstContext)
{
    return CaptureNcrReceiveContext(pstSync, pstContext);
}

/* After the host's real RLE parser locates Encryption_Context, pass exactly
 * those three bytes and the SAME packet's receive context. Only NCR_RESTORE_OK
 * permits using uiNcr32/usYear/ucMonth/ucDay to construct the agreed IV.
 * This example does not invent an RLE layout or a TRANSEC IV. */
NcrRestoreStatus HandleParsedEncryptionContext(NcrSync_t *pstSync,
                                               const NcrReceiveContext_t *pstContext,
                                               const uint8_t *pucEncryptionContext,
                                               uint32_t uiLength,
                                               NcrRestoreResult_t *pstResult)
{
    return RestoreNcr24(pstSync, pstContext, pucEncryptionContext, uiLength, pstResult);
}

/* Stop and join GPS/RLE callers first, then release the handle. */
void ReleaseGatewayClock(NcrSync_t *pstSync)
{
    DestroyNcrSync(pstSync);
}
