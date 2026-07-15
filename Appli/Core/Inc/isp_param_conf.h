/**
 ******************************************************************************
 * @file    isp_param_conf.h
 * @author  AIS Application Team
 * @brief   Header file for IQT parameters of the ISP middleware.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2024 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under SLA0044 terms that can be found here:
 * https://www.st.com/resource/en/license_agreement/SLA0044.txt
 *
 ******************************************************************************
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __ISP_PARAM_CONF__H
#define __ISP_PARAM_CONF__H
#include "cmw_camera.h"

/* DCMIPP ISP configuration for VD66GY sensor */
#if SENSOR_VD66GY_FLIP == CMW_MIRRORFLIP_NONE
  #define BAYER_TYPE ISP_DEMOS_TYPE_GRBG
#elif SENSOR_VD66GY_FLIP == CMW_MIRRORFLIP_FLIP
  #define BAYER_TYPE ISP_DEMOS_TYPE_BGGR
#elif SENSOR_VD66GY_FLIP == CMW_MIRRORFLIP_MIRROR
  #define BAYER_TYPE ISP_DEMOS_TYPE_RGGB
#elif SENSOR_VD66GY_FLIP == CMW_MIRRORFLIP_FLIP_MIRROR
  #define BAYER_TYPE ISP_DEMOS_TYPE_GBRG
#endif

/* copy of vd66gy_JudgeIIBox_isp_param_conf.h so we can change bayer type.
 * also disable aec at isp level.
 */
static const ISP_IQParamTypeDef ISP_IQParamCacheInit_VD66GY = {
    .sensorGainStatic = {
        .gain = 0,
    },
    .sensorExposureStatic = {
        .exposure = 0,
    },
    .AECAlgo = {
        .enable = 0,
        .exposureCompensation = 0,
    },
    .statRemoval = {
        .enable = 0,
        .nbHeadLines = 0,
        .nbValidLines = 0,
    },
    .badPixelStatic = {
        .enable = 0,
        .strength = 0,
    },
    .badPixelAlgo = {
        .enable = 0,
        .threshold = 0,
    },
    .blackLevelStatic = {
        .enable = 1,
        .BLCR = 16,
        .BLCG = 16,
        .BLCB = 16,
    },
    .demosaicing = {
        .enable = 1,
        .type = BAYER_TYPE,
        .peak = 2,
        .lineV = 4,
        .lineH = 4,
        .edge = 6,
    },
    .ispGainStatic = {
        .enable = 0,
        .ispGainR = 0,
        .ispGainG = 0,
        .ispGainB = 0,
    },
    .colorConvStatic = {
        .enable = 0,
        .coeff = { { 0, 0, 0, }, { 0, 0, 0, }, { 0, 0, 0, }, }
    },
    .AWBAlgo = {
        .enable = 1,
        .label = { "JudgeII A", "JudegII TL84", "JudegeII DAY", "", "", },
        .referenceColorTemp = { 2750, 4150, 6750, 0, 0, },
        .ispGainR = { 95000000, 117000000, 156000000, 0, 0, },
        .ispGainG = { 100000000, 100000000, 100000000, 0, 0, },
        .ispGainB = { 238000000, 189000000, 150000000, 0, 0, },
        .coeff = {
            { { 133939999, -20660000, -31280000, }, { -37890000, 149680000, -26179999, }, { 2040000, -89240000, 221830000, }, },
            { { 147680000, -38330000, -29360000, }, { -40320000, 146010000, -31400000, }, { 1100000, -61240000, 174790000, }, },
            { { 146010000, -39280000, -14060000, }, { -26750000, 152490000, -42520000, }, { 1160000, -55410000, 143910000, }, },
            { { 0, 0, 0, }, { 0, 0, 0, }, { 0, 0, 0, }, },
            { { 0, 0, 0, }, { 0, 0, 0, }, { 0, 0, 0, }, },
        },
        .referenceRGB = {
            { 61, 66, 27 },
            { 49, 65, 34 },
            { 39, 66, 42 },
            { 0, 0, 0 },
            { 0, 0, 0 },
        },
    },
    .contrast = {
        .enable = 0,
        .coeff.LUM_0 = 0,
        .coeff.LUM_32 = 0,
        .coeff.LUM_64 = 0,
        .coeff.LUM_96 = 0,
        .coeff.LUM_128 = 0,
        .coeff.LUM_160 = 0,
        .coeff.LUM_192 = 0,
        .coeff.LUM_224 = 0,
        .coeff.LUM_256 = 0,
    },
    .statAreaStatic = {
        .X0 = 140,
        .Y0 = 341,
        .XSize = 840,
        .YSize = 682,
    },
    .gamma = {
        .enable = 1,
        // .enablePipe1 = 1,
        // .enablePipe2 = 1,
    },
    .sensorDelay = {
        .delay = 4,
    },
    .luxRef = {
        .HL_LuxRef = 1400,
        .HL_Expo1 = 2000,
        .HL_Lum1 = 42,
        .HL_Expo2 = 20000,
        .HL_Lum2 = 200,
        .LL_LuxRef = 300,
        .LL_Expo1 = 5000,
        .LL_Lum1 = 20,
        .LL_Expo2 = 29827,
        .LL_Lum2 = 111,
        .calibFactor = 0.738f,
    },
};

static const ISP_IQParamTypeDef ISP_IQParamCacheInit_IMX335 = {
    .sensorGainStatic = {
        .gain = 0,
    },
    .sensorExposureStatic = {
        .exposure = 0,
    },
    .AECAlgo = {
        .enable = 1,
        .exposureCompensation = 0,
    },
    .statRemoval = {
        .enable = 0,
        .nbHeadLines = 0,
        .nbValidLines = 0,
    },
    .badPixelStatic = {
        .enable = 0,
        .strength = 0,
    },
    .badPixelAlgo = {
        .enable = 0,
        .threshold = 0,
    },
    .blackLevelStatic = {
        .enable = 1,
        .BLCR = 12,
        .BLCG = 12,
        .BLCB = 12,
    },
    .demosaicing = {
        .enable = 1,
        .type = 0,
        .peak = 2,
        .lineV = 4,
        .lineH = 4,
        .edge = 6,
    },
    .ispGainStatic = {
        .enable = 0,
        .ispGainR = 0,
        .ispGainG = 0,
        .ispGainB = 0,
    },
    .colorConvStatic = {
        .enable = 0,
        .coeff = { { 0, 0, 0, }, { 0, 0, 0, }, { 0, 0, 0, }, }
    },
    .AWBAlgo = {
        .enable = 1,
        .label = { "A", "TL84", "D50", "D65", "Free slot", },
        .referenceColorTemp = { 2856, 4000, 5000, 6500, 0, },
        .ispGainR = { 140000000, 177000000, 220000000, 245000000, 0, },
        .ispGainG = { 100000000, 100000000, 100000000, 100000000, 0, },
        .ispGainB = { 275000000, 235000000, 180000000, 155000000, 0, },
        .coeff = {
            { { 151460000, -102340000, 50892000, }, { -85991000, 210980000, -24984000, }, { 25000000, -261000000, 341000000, }, },
            { { 155134500, -69370000, 13106000, }, { -38671000, 167689800, -33936000, }, { 5546200, -66770000, 159944200, }, },
            { { 180080000, -64840000, -15230000, }, { -35550000, 169920000, -34380000, }, { 9770000, -95700000, 185940000, }, },
            { { 180080000, -64840000, -15230000, }, { -35550000, 169920000, -34380000, }, { 9770000, -95700000, 185940000, }, },
            { { 0, 0, 0, }, { 0, 0, 0, }, { 0, 0, 0, }, },
        },
        .referenceRGB = {
            { 61, 66, 27 },
            { 49, 65, 34 },
            { 39, 66, 42 },
            { 0, 0, 0 },
            { 0, 0, 0 },
        },
    },
    .contrast = {
        .enable = 0,
        .coeff.LUM_0 = 0,
        .coeff.LUM_32 = 0,
        .coeff.LUM_64 = 0,
        .coeff.LUM_96 = 0,
        .coeff.LUM_128 = 0,
        .coeff.LUM_160 = 0,
        .coeff.LUM_192 = 0,
        .coeff.LUM_224 = 0,
        .coeff.LUM_256 = 0,
    },
    .statAreaStatic = {
        .X0 = 648,
        .Y0 = 486,
        .XSize = 1296,
        .YSize = 972,
    },
    .gamma = {
        .enable = 1,
        // .enablePipe1 = 1,
        // .enablePipe2 = 1,
    },
    .sensorDelay = {
        .delay = 3,
    },
    .luxRef = {
        .HL_LuxRef = 1050,
        .HL_Expo1 = 5000,
        .HL_Lum1 = 20,
        .HL_Expo2 = 33266,
        .HL_Lum2 = 117,
        .LL_LuxRef = 270,
        .LL_Expo1 = 9000,
        .LL_Lum1 = 10,
        .LL_Expo2 = 33266,
        .LL_Lum2 = 36,
        .calibFactor = 0.905f,
    },
};

/* DCMIPP ISP configuration for OS04C10 sensor */
static const ISP_IQParamTypeDef ISP_IQParamCacheInit_OS04C10 = {
    .sensorGainStatic = {
        .gain = 0,
    },
    .sensorExposureStatic = {
        .exposure = 5779,
    },
    .AECAlgo = {
        .enable = 1,
        .exposureCompensation = EXPOSURE_TARGET_0_0_EV,
        .antiFlickerFreq = ANTIFLICKER_50HZ,
    },
    .statRemoval = {
        .enable = 0,
        .nbHeadLines = 0,
        .nbValidLines = 0,
    },
    .badPixelStatic = {
        .enable = 0,
        .strength = 0,
    },
    .badPixelAlgo = {
        .enable = 0,
        .threshold = 0,
    },
    .blackLevelStatic = {
        .enable = 1,
        .BLCR = 16,
        .BLCG = 16,
        .BLCB = 16,
    },
    .demosaicing = {
        .enable = 1,
        .type = ISP_DEMOS_TYPE_RGGB,
        .peak = 2,
        .lineV = 4,
        .lineH = 4,
        .edge = 6,
    },
    .ispGainStatic = {
        .enable = 1,
        .ispGainR = 218750000,
        .ispGainG = 100000000,
        .ispGainB = 169531250,
    },
    .colorConvStatic = {
        .enable = 1,
        .coeff = { { 161328125, -54687500, -6250000, }, { -44921875, 164843750, -19921875, }, { -17578125, -44921875, 162500000, }, }
    },
    .AWBAlgo = {
        .enable = 0,
        .label = { "A", "TL84", "D65", "Free Slot", "Free Slot", },
        .referenceColorTemp = { 2500, 3800, 6500, 0, 0, },
        .ispGainR = { 128000000, 158000000, 220000000, 0, 0, },
        .ispGainG = { 100000000, 100000000, 100000000, 0, 0, },
        .ispGainB = { 290000000, 227000000, 170000000, 0, 0, },
        .coeff = {
            { { 104870000, 83880000, -88750000, }, { -123659999, 295510000, -71850000, }, { -44000000, -5930000, 149930000, }, },
            { { 87740000, 70650000, -58390000, }, { -60729999, 198780000, -38050000, }, { -26910000, -1380000, 128290000, }, },
            { { 161540000, -55000000, -6540000, }, { -45000000, 165000000, -20000000, }, { -17590000, -45000000, 162590000, }, },
            { { 0, 0, 0, }, { 0, 0, 0, }, { 0, 0, 0, }, },
            { { 0, 0, 0, }, { 0, 0, 0, }, { 0, 0, 0, }, },
        },
        .referenceRGB = {
            { 63, 70, 33 },
            { 56, 71, 38 },
            { 60, 96, 63 },
            { 0, 0, 0 },
            { 0, 0, 0 },
        },
    },
    .contrast = {
        .enable = 1,
        .coeff.LUM_0 = 56,
        .coeff.LUM_32 = 36,
        .coeff.LUM_64 = 86,
        .coeff.LUM_96 = 117,
        .coeff.LUM_128 = 151,
        .coeff.LUM_160 = 193,
        .coeff.LUM_192 = 176,
        .coeff.LUM_224 = 254,
        .coeff.LUM_256 = 311,
    },
    .statAreaStatic = {
        .X0 = 214,
        .Y0 = 717,
        .XSize = 2280,
        .YSize = 729,
    },
    .gamma = {
        .enable = 1,
    },
    .sensorDelay = {
        .delay = 3,
    },
    .luxRef = {
        .HL_LuxRef = 972,
        .HL_Expo1 = 26866,
        .HL_Lum1 = 34,
        .HL_Expo2 = 59156,
        .HL_Lum2 = 98,
        .LL_LuxRef = 261,
        .LL_Expo1 = 78883,
        .LL_Lum1 = 28,
        .LL_Expo2 = 180712,
        .LL_Lum2 = 64,
        .calibFactor = 0.708f,
    },
};

/* DCMIPP ISP configuration for OS04C10 night IR mode. */
__attribute__((unused)) static const ISP_IQParamTypeDef ISP_IQParamCacheInit_OS04C10_NIGHT = {
    .sensorGainStatic = {
        .gain = 0,
    },
    .sensorExposureStatic = {
        .exposure = 4207,
    },
    .AECAlgo = {
        .enable = 1,
        .exposureCompensation = EXPOSURE_TARGET_MINUS_0_5_EV,
        .antiFlickerFreq = ANTIFLICKER_50HZ,
    },
    .statRemoval = {
        .enable = 0,
        .nbHeadLines = 0,
        .nbValidLines = 0,
    },
    .badPixelStatic = {
        .enable = 0,
        .strength = 0,
    },
    .badPixelAlgo = {
        .enable = 0,
        .threshold = 0,
    },
    .blackLevelStatic = {
        .enable = 1,
        .BLCR = 16,
        .BLCG = 16,
        .BLCB = 16,
    },
    .demosaicing = {
        .enable = 1,
        .type = ISP_DEMOS_TYPE_RGGB,
        .peak = 2,
        .lineV = 4,
        .lineH = 4,
        .edge = 6,
    },
    .ispGainStatic = {
        .enable = 1,
        .ispGainR = 100000000,
        .ispGainG = 100000000,
        .ispGainB = 100000000,
    },
    .colorConvStatic = {
        .enable = 1,
        .coeff = { { 25000000, 70000000, 5000000, }, { 25000000, 70000000, 5000000, }, { 25000000, 70000000, 5000000, }, }
    },
    .AWBAlgo = {
        .enable = 0,
        .label = { "A", "Free Slot", "Free Slot", "Free Slot", "Free Slot", },
        .referenceColorTemp = { 2500, 0, 0, 0, 0, },
        .ispGainR = { 100000000, 0, 0, 0, 0, },
        .ispGainG = { 100000000, 0, 0, 0, 0, },
        .ispGainB = { 100000000, 0, 0, 0, 0, },
        .coeff = {
            { { 25000000, 70000000, 5000000, }, { 25000000, 70000000, 5000000, }, { 25000000, 70000000, 5000000, }, },
            { { 0, 0, 0, }, { 0, 0, 0, }, { 0, 0, 0, }, },
            { { 0, 0, 0, }, { 0, 0, 0, }, { 0, 0, 0, }, },
            { { 0, 0, 0, }, { 0, 0, 0, }, { 0, 0, 0, }, },
            { { 0, 0, 0, }, { 0, 0, 0, }, { 0, 0, 0, }, },
        },
        .referenceRGB = {
            { 60, 60, 60 },
            { 0, 0, 0 },
            { 0, 0, 0 },
            { 0, 0, 0 },
            { 0, 0, 0 },
        },
    },
    .contrast = {
        .enable = 1,
        .coeff.LUM_0 = 42,
        .coeff.LUM_32 = 36,
        .coeff.LUM_64 = 81,
        .coeff.LUM_96 = 121,
        .coeff.LUM_128 = 134,
        .coeff.LUM_160 = 143,
        .coeff.LUM_192 = 147,
        .coeff.LUM_224 = 183,
        .coeff.LUM_256 = 229,
    },
    .statAreaStatic = {
        .X0 = 478,
        .Y0 = 181,
        .XSize = 1918,
        .YSize = 1334,
    },
    .gamma = {
        .enable = 1,
    },
    .sensorDelay = {
        .delay = 3,
    },
    .luxRef = {
        .HL_LuxRef = 1000,
        .HL_Expo1 = 5000,
        .HL_Lum1 = 20,
        .HL_Expo2 = 30000,
        .HL_Lum2 = 100,
        .LL_LuxRef = 250,
        .LL_Expo1 = 10000,
        .LL_Lum1 = 10,
        .LL_Expo2 = 30000,
        .LL_Lum2 = 30,
        .calibFactor = 1.0f,
    },
};

__attribute__((unused)) static const ISP_IQParamTypeDef* ISP_IQParamCacheInit[] = {
    &ISP_IQParamCacheInit_IMX335,
    &ISP_IQParamCacheInit_VD66GY,
    &ISP_IQParamCacheInit_OS04C10
};

#endif /* __ISP_PARAM_CONF__H */
