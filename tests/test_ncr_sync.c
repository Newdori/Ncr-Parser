#include "ncr_test_support.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void TestInputAndConfig(void)
{
    GpsTime_t stGps = MakeTestGps(TEST_SOURCE_MS, NCR_CLOCK_LOCKED);
    NcrConfig_t stConfig = GetNcrDefaultConfig();
    int64_t llSourceMs = 0;
    CHECK(NCR_STATUS_OK == ConvertGpsTimeToSourceMs(&stGps, &llSourceMs));
    CHECK(TEST_SOURCE_MS == llSourceMs);
    CHECK(12U == stGps.ucHour);
    stGps.uiMillisecond = 65536U;
    CHECK(NCR_STATUS_INVALID_GPS == ValidateGpsTime(&stGps));
    stGps.uiMillisecond = 999U;
    stGps.ucClockStatus = 0U;
    CHECK(NCR_STATUS_INVALID_GPS == ValidateGpsTime(&stGps));
    stGps.ucClockStatus = 99U;
    CHECK(NCR_STATUS_INVALID_GPS == ValidateGpsTime(&stGps));
    stGps.ucClockStatus = 2U;
    CHECK(NCR_STATUS_OK == ValidateGpsTime(&stGps));
    stGps.ucClockStatus = 3U;
    CHECK(NCR_STATUS_OK == ValidateGpsTime(&stGps));
    stGps.ucClockStatus = 1U;
    CHECK(NCR_STATUS_OK == ValidateGpsTime(&stGps));
    stGps.ucSec = 60U;
    CHECK(NCR_STATUS_INVALID_GPS == ValidateGpsTime(&stGps));
    stGps.ucSec = 59U;
    stGps.usYear = 2027U;
    stGps.ucMonth = 2U;
    stGps.ucDay = 29U;
    CHECK(NCR_STATUS_INVALID_GPS == ValidateGpsTime(&stGps));
    stGps.usYear = 2028U;
    CHECK(NCR_STATUS_OK == ValidateGpsTime(&stGps));
    CHECK(NCR_STATUS_INVALID_GPS == ValidateGpsTime(NULL));
    CHECK(NCR_STATUS_BAD_ARGUMENT == ConvertGpsTimeToSourceMs(&stGps, NULL));
    CHECK(NCR_STATUS_OK == ValidateNcrConfig(&stConfig));
    stConfig.uiAcquireSamples = 0U;
    CHECK(NCR_STATUS_BAD_CONFIG == ValidateNcrConfig(&stConfig));
    stConfig = GetNcrDefaultConfig();
    stConfig.uiMaxLinkDelayMs = 200U;
    CHECK(NCR_STATUS_OK == ValidateNcrConfig(&stConfig));
    stConfig = GetNcrDefaultConfig();
    stConfig.uiMaxLinkDelayMs = 90000U;
    CHECK(NCR_STATUS_BAD_CONFIG == ValidateNcrConfig(&stConfig));
    stConfig = GetNcrDefaultConfig();
    stConfig.uiMaxUncertaintyMs = UINT32_MAX;
    CHECK(NCR_STATUS_BAD_CONFIG == ValidateNcrConfig(&stConfig));
    puts("PASS input/hour/ms32/status/config validation");
}

static void TestCalendarSweep(void)
{
    int64_t llDayMs;
    int64_t llResult;
    GpsTime_t stGps;
    uint32_t uiChecks = 0U;
    for (llDayMs = INT64_C(946684800000); llDayMs < INT64_C(4102444800000);
         llDayMs += INT64_C(86400000)) {
        stGps = MakeTestGps(llDayMs + INT64_C(86399999), NCR_CLOCK_LOCKED);
        CHECK(NCR_STATUS_OK == ConvertGpsTimeToSourceMs(&stGps, &llResult));
        CHECK(llResult == llDayMs + INT64_C(86399999));
        uiChecks++;
    }
    CHECK(36525U == uiChecks);
    printf("PASS calendar sweep: %" PRIu32 " dates (2000..2099)\n", uiChecks);
}

static void CheckReconstruction(int64_t llTxMs, int64_t llRxMs, uint32_t uiUncertaintyMs)
{
    NcrConfig_t stConfig = GetNcrDefaultConfig();
    NcrRestoreResult_t stResult;
    GpsTime_t stGps = MakeTestGps(llTxMs, NCR_CLOCK_LOCKED);
    uint32_t uiNcr = (uint32_t)((uint64_t)((llTxMs / 1000) % 86400) * UINT64_C(90000));
    CHECK(NCR_RESTORE_OK == ReconstructNcr24(uiNcr & NCR_LSB24_MASK,
                                             llRxMs, uiUncertaintyMs, &stConfig, &stResult));
    CHECK(uiNcr == stResult.uiNcr32);
    CHECK(llTxMs / 1000 * 1000 == stResult.llSourceTxSecondMs);
    CHECK(stGps.usYear == stResult.usYear);
    CHECK(stGps.ucMonth == stResult.ucMonth);
    CHECK(stGps.ucDay == stResult.ucDay);
}

static void TestFullDaySweep(void)
{
    const int64_t llDayStart = TEST_SOURCE_MS - INT64_C(43200000);
    const uint32_t auiDelays[] = {50U, 250U, 1200U, 4800U};
    uint32_t uiSecond;
    uint32_t uiIndex;
    uint32_t uiChecks = 0U;
    for (uiSecond = 0U; uiSecond < 86400U; uiSecond++) {
        for (uiIndex = 0U; uiIndex < 4U; uiIndex++) {
            int64_t llTxMs = llDayStart + (int64_t)uiSecond * 1000 + 875;
            CheckReconstruction(llTxMs, llTxMs + auiDelays[uiIndex], 0U);
            uiChecks++;
        }
    }
    CHECK(345600U == uiChecks);
    printf("PASS full-day NCR sweep: %" PRIu32 " packet scenarios\n", uiChecks);
}

static void TestBoundariesAndUncertainty(void)
{
    const int64_t allDayStarts[] = {
        INT64_C(1794009600000), /* 2026-11-07 */
        INT64_C(1798761600000), /* 2027-01-01 */
        INT64_C(1835481600000), /* 2028-03-01 */
        INT64_C(4102358400000)  /* 2099-12-31 */
    };
    uint32_t uiIndex;
    NcrConfig_t stConfig = GetNcrDefaultConfig();
    NcrRestoreResult_t stResult;
    for (uiIndex = 0U; uiIndex < 4U; uiIndex++) {
        CheckReconstruction(allDayStarts[uiIndex] + INT64_C(86399900),
                              allDayStarts[uiIndex] + INT64_C(86400200), 0U);
    }
    CheckReconstruction(TEST_SOURCE_MS, TEST_SOURCE_MS - 700, 1000U);
    CheckReconstruction(TEST_SOURCE_MS, TEST_SOURCE_MS + 6800, 2000U);
    CheckReconstruction(TEST_SOURCE_MS + 999, TEST_SOURCE_MS + 5999, 0U);
    CHECK(NCR_RESTORE_NO_CANDIDATE == ReconstructNcr24(1U, TEST_SOURCE_MS,
                                                       0U, &stConfig, &stResult));
    memset(&stResult, 0xA5, sizeof(stResult));
    CHECK(NCR_RESTORE_BAD_INPUT == ReconstructNcr24(UINT32_MAX, TEST_SOURCE_MS,
                                                    0U, &stConfig, &stResult));
    CHECK(0U == stResult.uiNcr32);
    CHECK(0 == stResult.llSourceTxSecondMs);
    CHECK(NCR_RESTORE_BAD_INPUT == ReconstructNcr24(0U, INT64_MAX,
                                                    0U, &stConfig, &stResult));
    CHECK(NCR_RESTORE_BAD_INPUT == ReconstructNcr24(0U, TEST_SOURCE_MS,
                                                    0U, NULL, &stResult));
    puts("PASS midnight/month/year/32-bit/uncertainty/bad-input boundaries");
}

int main(void)
{
    TestInputAndConfig();
    TestCalendarSweep();
    TestFullDaySweep();
    TestBoundariesAndUncertainty();
    puts("ALL CORE TESTS PASSED");
    return 0;
}
