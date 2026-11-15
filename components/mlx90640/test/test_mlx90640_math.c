#include "unity.h"
#include "mlx90640_api.h"
#include "MLX90640_API.h"
#include "MLX90641_API.h"
#include <string.h>
#include <math.h>

TEST_CASE("mlx90640 parameter extraction basic bounds", "[mlx90640]")
{
    uint16_t dummy_ee[MLX90640_EEPROM_WORDS];
    memset(dummy_ee, 0, sizeof(dummy_ee));

    dummy_ee[48] = 0x2000; // alphaPTAT & gainEE
    dummy_ee[49] = 1000;   // vPTAT25
    dummy_ee[50] = 0x0500; // KvPTAT & KtPTAT
    dummy_ee[51] = 0x1010; // kVdd & vdd25
    dummy_ee[56] = 0x2000; // resolutionEE

    paramsMLX90640 params;
    memset(&params, 0, sizeof(params));

    int res = MLX90640_ExtractParameters(dummy_ee, &params);
    TEST_ASSERT_EQUAL(0, res);
    TEST_ASSERT_FALSE(isnan(params.KvPTAT));
    TEST_ASSERT_FALSE(isnan(params.KtPTAT));
    TEST_ASSERT_FALSE(isnan(params.alphaPTAT));
}

TEST_CASE("mlx90640 calculation no NaN", "[mlx90640]")
{
    uint16_t dummy_frame[MLX90640_FRAME_DATA_WORDS];
    memset(dummy_frame, 0, sizeof(dummy_frame));

    dummy_frame[778] = 1000; // gain
    dummy_frame[800] = 500;  // PTAT
    dummy_frame[810] = 300;  // VDD
    dummy_frame[832] = 0x1901; // ctrl
    dummy_frame[833] = 0x0001; // subpage

    paramsMLX90640 params;
    memset(&params, 0, sizeof(params));
    params.gainEE = 1000;
    params.kVdd = 32;
    params.vdd25 = -8192;
    params.vPTAT25 = 1000;
    params.KtPTAT = 8.0f;
    params.KvPTAT = 0.001f;
    params.alphaPTAT = 9.0f;

    params.alphaScale = 30;
    for (int i = 0; i < MLX90640_NUM_PIXELS; i++) {
        params.alpha[i] = 100;
    }

    float result[MLX90640_NUM_PIXELS];
    MLX90640_CalculateTo(dummy_frame, &params, 0.95f, 25.0f, result);

    for (int i = 0; i < MLX90640_NUM_PIXELS; i++) {
        TEST_ASSERT_FALSE(isnan(result[i]));
    }
}

TEST_CASE("mlx90641 parameter extraction basic bounds", "[mlx90641]")
{
    uint16_t dummy_ee[MLX90641_EEPROM_WORDS];
    memset(dummy_ee, 0, sizeof(dummy_ee));

    dummy_ee[10] = 0x0040; // Valid MLX90641 EEPROM marker (before Hamming decode words 16..831)
    dummy_ee[38] = 11402;
    dummy_ee[39] = 0x0410;
    dummy_ee[40] = 0x00A0;
    dummy_ee[41] = 0x0400;
    dummy_ee[42] = 0x0300;
    dummy_ee[43] = 0x0400;
    dummy_ee[51] = 0x0200; // emissivity / resolution bits

    paramsMLX90641 params;
    memset(&params, 0, sizeof(params));

    int res = MLX90641_ExtractParameters(dummy_ee, &params);
    TEST_ASSERT_EQUAL(0, res);
    TEST_ASSERT_FALSE(isnan(params.KvPTAT));
    TEST_ASSERT_FALSE(isnan(params.KtPTAT));
    TEST_ASSERT_FALSE(isnan(params.alphaPTAT));
}

TEST_CASE("mlx90641 calculation no NaN", "[mlx90641]")
{
    uint16_t dummy_frame[MLX90641_FRAME_DATA_WORDS];
    memset(dummy_frame, 0, sizeof(dummy_frame));

    dummy_frame[216] = 1000; // gain (0x0598 - 0x0580 + 192 = 24 + 192)
    dummy_frame[224] = 500;  // PTAT (0x05A0 - 0x0580 + 192 = 32 + 192)
    dummy_frame[234] = 300;  // VDD  (0x05AA - 0x0580 + 192 = 42 + 192)
    dummy_frame[240] = 0x0901; // ctrl
    dummy_frame[241] = 0x0000; // subpage

    paramsMLX90641 params;
    memset(&params, 0, sizeof(params));
    params.gainEE = 1000;
    params.kVdd = -3072;
    params.vdd25 = -12224;
    params.vPTAT25 = 11402;
    params.KtPTAT = 37.5f;
    params.KvPTAT = 0.00244f;
    params.alphaPTAT = 10.5f;
    params.emissivityEE = 1.0f;
    params.alphaScale = 20;

    for (int i = 0; i < MLX90641_NUM_PIXELS; i++) {
        params.alpha[i] = 100;
    }

    float result[MLX90641_NUM_PIXELS];
    MLX90641_CalculateTo(dummy_frame, &params, 0.95f, 25.0f, result);

    for (int i = 0; i < MLX90641_NUM_PIXELS; i++) {
        TEST_ASSERT_FALSE(isnan(result[i]));
    }
}
