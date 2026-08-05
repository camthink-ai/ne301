/* Includes ------------------------------------------------------------------*/
#include "os04c10.h"
#include <math.h>
#include <stdio.h>
/** @addtogroup BSP
  * @{
  */

/** @addtogroup Components
  * @{
  */

/** @addtogroup OS04C10
  * @brief     This file provides a set of functions needed to drive the
  *            OS04C10 Camera module.
  * @{
  */

/** @defgroup OS04C10_Private_TypesDefinitions
  * @{
  */

/**
  * @}
  */
/** @defgroup OS04C10_Private_Variables
  * @{
  */

OS04C10_CAMERA_Drv_t   OS04C10_CAMERA_Driver =
{
  OS04C10_Init,
  OS04C10_DeInit,
  OS04C10_ReadID,
  OS04C10_GetCapabilities,
  OS04C10_SetLightMode,
  OS04C10_SetColorEffect,
  OS04C10_SetBrightness,
  OS04C10_SetSaturation,
  OS04C10_SetContrast,
  OS04C10_SetHueDegree,
  OS04C10_MirrorFlipConfig,
  OS04C10_ZoomConfig,
  OS04C10_SetResolution,
  OS04C10_GetResolution,
  OS04C10_SetPixelFormat,
  OS04C10_GetPixelFormat,
  OS04C10_NightModeConfig,
  OS04C10_SetFrequency,
  OS04C10_SetGain,
  OS04C10_SetExposure,
  OS04C10_SetFramerate
};

/**
  * @}
  */

struct regval {
  uint16_t addr;
  uint8_t val;
};

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define OS04C10_1H_PERIOD_USEC (1000000.0F / 3150 / 30)

/** @defgroup OS04C10_Private_Functions_Prototypes Private Functions Prototypes
  * @{
  */
static int32_t OS04C10_ReadRegWrap(void *handle, uint16_t Reg, uint8_t *Data, uint16_t Length);
static int32_t OS04C10_WriteRegWrap(void *handle, uint16_t Reg, uint8_t *Data, uint16_t Length);
static int32_t OS04C10_Delay(OS04C10_Object_t *pObj, uint32_t Delay);

/** @defgroup OS04C10_Private_Functions Private Functions
  * @{
  */
 static int32_t OS04C10_WriteTable(OS04C10_Object_t *pObj, const struct regval *regs, uint32_t size)
 {
   uint32_t index;
   int32_t ret = OS04C10_OK;
 
   /* Set registers */
   for(index=0; index<size ; index++)
   {
     if(ret != OS04C10_ERROR)
     {
       if(os04c10_write_reg(&pObj->Ctx, regs[index].addr, (uint8_t *)&(regs[index].val), 1) != OS04C10_OK)
       {
         ret = OS04C10_ERROR;
       }
     }
   }
   return ret;
 }
/**
  * @}
  */

/** @defgroup OS04C10_Exported_Functions OS04C10 Exported Functions
  * @{
  */
/**
  * @brief  Register component IO bus
  * @param  Component object pointer
  * @retval Component status
  */
int32_t OS04C10_RegisterBusIO(OS04C10_Object_t *pObj, OS04C10_IO_t *pIO)
{
  int32_t ret;

  if (pObj == NULL)
  {
    ret = OS04C10_ERROR;
  }
  else
  {
    pObj->IO.Init      = pIO->Init;
    pObj->IO.DeInit    = pIO->DeInit;
    pObj->IO.Address   = pIO->Address;
    pObj->IO.WriteReg  = pIO->WriteReg;
    pObj->IO.ReadReg   = pIO->ReadReg;
    pObj->IO.GetTick   = pIO->GetTick;

    pObj->Ctx.ReadReg  = OS04C10_ReadRegWrap;
    pObj->Ctx.WriteReg = OS04C10_WriteRegWrap;
    pObj->Ctx.handle   = pObj;

    if (pObj->IO.Init != NULL)
    {
      ret = pObj->IO.Init();
    }
    else
    {
      ret = OS04C10_ERROR;
    }
  }

  return ret;
}

/**
  * @brief  Initializes the OS04C10 CAMERA component.
  * @param  pObj  pointer to component object
  * @param  Resolution  Camera resolution
  * @param  PixelFormat pixel format to be configured
  * @retval Component status
  */
int32_t OS04C10_Init(OS04C10_Object_t *pObj, uint32_t Resolution, uint32_t PixelFormat)
{
  uint32_t index;
  int32_t ret = OS04C10_OK;
  /* Initialization sequence for OS04C10 4MP*/
  static const uint16_t OS04C10_Common[][2] =
  {
    {0x0103, 0x01},
    {0x0301, 0x84},
    {0x0303, 0x01},
    {0x0305, 0x5b},
    {0x0306, 0x00},
    {0x0307, 0x17},
    {0x0323, 0x04},
    {0x0324, 0x01},
    {0x0325, 0x62},
    {0x3012, 0x06},
    {0x3013, 0x02},
    {0x3016, 0x32},
    {0x3021, 0x03},
    {0x3106, 0x25},
    {0x3107, 0xa1},
    {0x3500, 0x00},
    {0x3501, 0x04},
    {0x3502, 0x40},
    {0x3503, 0x88},
    {0x3508, 0x00},
    {0x3509, 0x80},
    {0x350a, 0x04},
    {0x350b, 0x00},
    {0x350c, 0x00},
    {0x350d, 0x80},
    {0x350e, 0x04},
    {0x350f, 0x00},
    {0x3510, 0x00},
    {0x3511, 0x01},
    {0x3512, 0x20},
    {0x3624, 0x02},
    {0x3625, 0x4c},
    {0x3660, 0x00},
    {0x3666, 0xa5},
    {0x3667, 0xa5},
    {0x366a, 0x64},
    {0x3673, 0x0d},
    {0x3672, 0x0d},
    {0x3671, 0x0d},
    {0x3670, 0x0d},
    {0x3685, 0x00},
    {0x3694, 0x0d},
    {0x3693, 0x0d},
    {0x3692, 0x0d},
    {0x3691, 0x0d},
    {0x3696, 0x4c},
    {0x3697, 0x4c},
    {0x3698, 0x40},
    {0x3699, 0x80},
    {0x369a, 0x18},
    {0x369b, 0x1f},
    {0x369c, 0x14},
    {0x369d, 0x80},
    {0x369e, 0x40},
    {0x369f, 0x21},
    {0x36a0, 0x12},
    {0x36a1, 0x5d},
    {0x36a2, 0x66},
    {0x370a, 0x00},
    {0x370e, 0x0c},
    {0x3710, 0x00},
    {0x3713, 0x00},
    {0x3725, 0x02},
    {0x372a, 0x03},
    {0x3738, 0xce},
    {0x3748, 0x00},
    {0x374a, 0x00},
    {0x374c, 0x00},
    {0x374e, 0x00},
    {0x3756, 0x00},
    {0x3757, 0x0e},
    {0x3767, 0x00},
    {0x3771, 0x00},
    {0x377b, 0x20},
    {0x377c, 0x00},
    {0x377d, 0x0c},
    {0x3781, 0x03},
    {0x3782, 0x00},
    {0x3789, 0x14},
    {0x3795, 0x02},
    {0x379c, 0x00},
    {0x379d, 0x00},
    {0x37b8, 0x04},
    {0x37ba, 0x03},
    {0x37bb, 0x00},
    {0x37bc, 0x04},
    {0x37be, 0x08},
    {0x37c4, 0x11},
    {0x37c5, 0x80},
    {0x37c6, 0x14},
    {0x37c7, 0x08},
    {0x37da, 0x11},
    {0x381f, 0x08},
    {0x3829, 0x03},
    {0x3881, 0x00},
    {0x3888, 0x04},
    {0x388b, 0x00},
    {0x3c80, 0x10},
    {0x3c86, 0x00},
    {0x3c8c, 0x20},
    {0x3c9f, 0x01},
    {0x3d85, 0x1b},
    {0x3d8c, 0x71},
    {0x3d8d, 0xe2},
    {0x3f00, 0x0b},
    {0x3f06, 0x04},
    {0x400a, 0x01},
    {0x400b, 0x50},
    {0x400e, 0x08},
    {0x4043, 0x7e},
    {0x4045, 0x7e},
    {0x4047, 0x7e},
    {0x4049, 0x7e},
    {0x4090, 0x14},
    {0x40b0, 0x00},
    {0x40b1, 0x00},
    {0x40b2, 0x00},
    {0x40b3, 0x00},
    {0x40b4, 0x00},
    {0x40b5, 0x00},
    {0x40b7, 0x00},
    {0x40b8, 0x00},
    {0x40b9, 0x00},
    {0x40ba, 0x00},
    {0x4301, 0x00},
    {0x4303, 0x00},
    {0x4502, 0x04},
    {0x4503, 0x00},
    {0x4504, 0x06},
    {0x4506, 0x00},
    {0x4507, 0x64},
    {0x4803, 0x10},
    {0x480c, 0x32},
    {0x480e, 0x00},
    {0x4813, 0x00},
    {0x4819, 0x70},
    {0x481f, 0x30},
    {0x4823, 0x3c},
    {0x4825, 0x32},
    {0x4833, 0x10},
    {0x484b, 0x07},
    {0x488b, 0x00},
    {0x4d00, 0x04},
    {0x4d01, 0xad},
    {0x4d02, 0xbc},
    {0x4d03, 0xa1},
    {0x4d04, 0x1f},
    {0x4d05, 0x4c},
    {0x4d0b, 0x01},
    {0x4e00, 0x2a},
    {0x4e0d, 0x00},
    {0x5001, 0x09},
    {0x5004, 0x00},
    {0x5080, 0x04},
    {0x5036, 0x00},
    {0x5180, 0x70},
    {0x5181, 0x10},
    {0x520a, 0x03},
    {0x520b, 0x06},
    {0x520c, 0x0c},
    {0x580b, 0x0f},
    {0x580d, 0x00},
    {0x580f, 0x00},
    {0x5820, 0x00},
    {0x5821, 0x00},
    {0x301c, 0xf0},
    {0x301e, 0xb4},
    {0x301f, 0xd0},
    {0x3022, 0x01},
    {0x3109, 0xe7},
    {0x3600, 0x00},
    {0x3610, 0x65},
    {0x3611, 0x85},
    {0x3613, 0x3a},
    {0x3615, 0x60},
    {0x3621, 0x90},
    {0x3620, 0x0c},
    {0x3629, 0x00},
    {0x3661, 0x04},
    {0x3664, 0x70},
    {0x3665, 0x00},
    {0x3681, 0xa6},
    {0x3682, 0x53},
    {0x3683, 0x2a},
    {0x3684, 0x15},
    {0x3700, 0x2a},
    {0x3701, 0x12},
    {0x3703, 0x28},
    {0x3704, 0x0e},
    {0x3706, 0x4a},
    {0x3709, 0x4a},
    {0x370b, 0xa2},
    {0x370c, 0x01},
    {0x370f, 0x04},
    {0x3714, 0x24},
    {0x3716, 0x24},
    {0x3719, 0x11},
    {0x371a, 0x1e},
    {0x3720, 0x00},
    {0x3724, 0x13},
    {0x373f, 0xb0},
    {0x3741, 0x4a},
    {0x3743, 0x4a},
    {0x3745, 0x4a},
    {0x3747, 0x4a},
    {0x3749, 0xa2},
    {0x374b, 0xa2},
    {0x374d, 0xa2},
    {0x374f, 0xa2},
    {0x3755, 0x10},
    {0x376c, 0x00},
    {0x378d, 0x30},
    {0x3790, 0x4a},
    {0x3791, 0xa2},
    // {0x3798, 0x40},    //HCG mode
    {0x3798, 0xc0},       //LCG mode
    {0x379e, 0x00},
    {0x379f, 0x04},
    {0x37a1, 0x01},
    {0x37a2, 0x1e},
    {0x37a8, 0x01},
    {0x37a9, 0x1e},
    {0x37ac, 0xa0},
    {0x37b9, 0x01},
    {0x37bd, 0x01},
    {0x37bf, 0x26},
    {0x37c0, 0x11},
    {0x37c2, 0x04},
    {0x37cd, 0x19},
    {0x37e0, 0x08},
    {0x37e6, 0x04},
    {0x37e5, 0x02},
    {0x37e1, 0x0c},
    {0x3737, 0x04},
    {0x37d8, 0x02},
    {0x37e2, 0x10},
    {0x3739, 0x10},
    {0x3662, 0x10},
    {0x37e4, 0x20},
    {0x37e3, 0x08},
    {0x37d9, 0x08},
    {0x4040, 0x00},
    {0x4041, 0x07},
    {0x4008, 0x02},
    {0x4009, 0x0d},
    {0x3800, 0x00},
    {0x3801, 0x00},
    {0x3802, 0x00},
    {0x3803, 0x00},
    {0x3804, 0x0a},
    {0x3805, 0x8f},
    {0x3806, 0x05},
    {0x3807, 0xff},
    {0x3808, 0x0a},
    {0x3809, 0x80},
    {0x380a, 0x05},
    {0x380b, 0xf0},
    {0x380c, 0x04},
    {0x380d, 0x2e},
    {0x380e, 0x0c},  //    30fps
    {0x380f, 0x4e},
    // {0x380e, 0x18},  //    15fps
    // {0x380f, 0x9c},
    // {0x380e, 0x49},  //    5fps
    // {0x380f, 0xd4},
    {0x3811, 0x09},
    {0x3813, 0x09},
    {0x3814, 0x01},
    {0x3815, 0x01},
    {0x3816, 0x01},
    {0x3817, 0x01},
    {0x3820, 0x88},
    {0x3821, 0x00},
    {0x3880, 0x25},
    {0x3882, 0x20},
    {0x3c91, 0x0b},
    {0x3c94, 0x45},
    {0x4000, 0xf3},
    {0x4001, 0x60},
    {0x4003, 0x40},
    {0x4300, 0xff},
    {0x4302, 0x0f},
    {0x4305, 0x83},
    {0x4505, 0x84},
    {0x4809, 0x1e},
    {0x480a, 0x04},
    {0x4837, 0x0a},
    {0x4c00, 0x08},
    {0x4c01, 0x00},
    {0x4c04, 0x00},
    {0x4c05, 0x00},
    {0x5000, 0xf9},
    {0x3624, 0x00},
    {0x3822, 0x14},
    {0x0100, 0x00},
  };
  uint8_t tmp;

  if (pObj->IsInitialized == 0U)
  {
    /* Check if resolution is supported */
    if (Resolution > OS04C10_R2688x1520)
    {
      ret = OS04C10_ERROR;
    }
    else
    {
      /* Set common parameters for all resolutions */
      for (index = 0; index < (sizeof(OS04C10_Common) / 4U) ; index++)
      {
        if (ret != OS04C10_ERROR)
        {
          tmp = (uint8_t)OS04C10_Common[index][1];

          if (os04c10_write_reg(&pObj->Ctx, OS04C10_Common[index][0], &tmp, 1) != OS04C10_OK)
          {
            ret = OS04C10_ERROR;
          }
        }
      }
      // if(ret == OS04C10_OK)
      // {
      //   /* Set configuration for Serial Interface */
      //   if(pObj->Mode == SERIAL_MODE)
      //   {
      //     if(OS04C10_EnableMIPIMode(pObj) != OS04C10_OK)
      //     {
      //       ret = OS04C10_ERROR;
      //     }
      //     else if(OS04C10_SetMIPIVirtualChannel(pObj, pObj->VirtualChannelID) != OS04C10_OK)
      //     {
      //       ret = OS04C10_ERROR;
      //     }
      //   }
      //   else
      //   {
      //     /* Set configuration for parallel Interface */
      //     if(OS04C10_EnableDVPMode(pObj) != OS04C10_OK)
      //     {
      //       ret = OS04C10_ERROR;
      //     }
      //     else
      //     {
      //       ret = OS04C10_OK;
      //     }
      //   }
      // }


      // if (ret == OS04C10_OK)
      // {
      //   /* Set specific parameters for each resolution */
      //   if (OS04C10_SetResolution(pObj, Resolution) != OS04C10_OK)
      //   {
      //     ret = OS04C10_ERROR;
      //   }/* Set specific parameters for each pixel format */
      //   else if (OS04C10_SetPixelFormat(pObj, PixelFormat) != OS04C10_OK)
      //   {
      //     ret = OS04C10_ERROR;
      //   }/* Set PixelClock, Href and VSync Polarity */
      //   else if (OS04C10_SetPolarities(pObj, OS04C10_POLARITY_PCLK_HIGH, OS04C10_POLARITY_HREF_HIGH,
      //                                 OS04C10_POLARITY_VSYNC_HIGH) != OS04C10_OK)
      //   {
      //     ret = OS04C10_ERROR;
      //   }
      //   else
      //   {
      //     pObj->IsInitialized = 1U;
      //   }
      // }
    }
  }
  return ret;
}

/**
  * @brief  De-initializes the camera sensor.
  * @param  pObj  pointer to component object
  * @retval Component status
  */
int32_t OS04C10_DeInit(OS04C10_Object_t *pObj)
{
  if (pObj->IsInitialized == 1U)
  {
    /* De-initialize camera sensor interface */
    pObj->IsInitialized = 0U;
  }

  return OS04C10_OK;
}

/**
  * @brief  Set OS04C10 camera Pixel Format.
  * @param  pObj  pointer to component object
  * @param  PixelFormat pixel format to be configured
  * @retval Component status
  */
int32_t OS04C10_SetPixelFormat(OS04C10_Object_t *pObj, uint32_t PixelFormat)
{
  int32_t ret = OS04C10_OK;
  uint32_t index;
  uint8_t tmp;

  /* Initialization sequence for RGB565 pixel format */
  static const uint16_t OS04C10_PF_RGB565[][2] =
  {
    /*  SET PIXEL FORMAT: RGB565 */
    {OS04C10_FORMAT_CTRL00, 0x6F},
    {OS04C10_FORMAT_MUX_CTRL, 0x01},
  };

  /* Initialization sequence for YUV422 pixel format */
  static const uint16_t OS04C10_PF_YUV422[][2] =
  {
    /*  SET PIXEL FORMAT: YUV422 */
    {OS04C10_FORMAT_CTRL00, 0x30},
    {OS04C10_FORMAT_MUX_CTRL, 0x00},
  };

  /* Initialization sequence for RGB888 pixel format */
  static const uint16_t OS04C10_PF_RGB888[][2] =
  {
    /*  SET PIXEL FORMAT: RGB888 (RGBRGB)*/
    {OS04C10_FORMAT_CTRL00, 0x23},
    {OS04C10_FORMAT_MUX_CTRL, 0x01},
  };

  /* Initialization sequence for Monochrome 8bits pixel format */
  static const uint16_t OS04C10_PF_Y8[][2] =
  {
    /*  SET PIXEL FORMAT: Y 8bits */
    {OS04C10_FORMAT_CTRL00, 0x10},
    {OS04C10_FORMAT_MUX_CTRL, 0x00},
  };

  /* Initialization sequence for JPEG format */
  static const uint16_t OS04C10_PF_JPEG[][2] =
  {
    /*  SET PIXEL FORMAT: JPEG */
    {OS04C10_FORMAT_CTRL00, 0x30},
    {OS04C10_FORMAT_MUX_CTRL, 0x00},
  };

  /* Check if PixelFormat is supported */
  if ((PixelFormat != OS04C10_RGB565) && (PixelFormat != OS04C10_YUV422) &&
      (PixelFormat != OS04C10_RGB888) && (PixelFormat != OS04C10_Y8) &&
      (PixelFormat != OS04C10_JPEG))
  {
    /* Pixel format not supported */
    ret = OS04C10_ERROR;
  }
  else
  {
    /* Set specific parameters for each PixelFormat */
    switch (PixelFormat)
    {
      case OS04C10_YUV422:
        for (index = 0; index < (sizeof(OS04C10_PF_YUV422) / 4U); index++)
        {
          if (ret != OS04C10_ERROR)
          {
            tmp = (uint8_t)OS04C10_PF_YUV422[index][1];
            if (os04c10_write_reg(&pObj->Ctx, OS04C10_PF_YUV422[index][0], &tmp, 1) != OS04C10_OK)
            {
              ret = OS04C10_ERROR;
            }
            else
            {
              (void)OS04C10_Delay(pObj, 1);
            }
          }
        }
        break;

      case OS04C10_RGB888:
        for (index = 0; index < (sizeof(OS04C10_PF_RGB888) / 4U); index++)
        {
          if (ret != OS04C10_ERROR)
          {
            tmp = (uint8_t)OS04C10_PF_RGB888[index][1];
            if (os04c10_write_reg(&pObj->Ctx, OS04C10_PF_RGB888[index][0], &tmp, 1) != OS04C10_OK)
            {
              ret = OS04C10_ERROR;
            }
            else
            {
              (void)OS04C10_Delay(pObj, 1);
            }
          }
        }
        break;

      case OS04C10_Y8:
        for (index = 0; index < (sizeof(OS04C10_PF_Y8) / 4U); index++)
        {
          if (ret != OS04C10_ERROR)
          {
            tmp = (uint8_t)OS04C10_PF_Y8[index][1];
            if (os04c10_write_reg(&pObj->Ctx, OS04C10_PF_Y8[index][0], &tmp, 1) != OS04C10_OK)
            {
              ret = OS04C10_ERROR;
            }
            else
            {
              (void)OS04C10_Delay(pObj, 1);
            }
          }
        }
        break;

      case OS04C10_JPEG:
        for (index = 0; index < (sizeof(OS04C10_PF_JPEG) / 4U); index++)
        {
          if (ret != OS04C10_ERROR)
          {
            tmp = (uint8_t)OS04C10_PF_JPEG[index][1];
            if (os04c10_write_reg(&pObj->Ctx, OS04C10_PF_JPEG[index][0], &tmp, 1) != OS04C10_OK)
            {
              ret = OS04C10_ERROR;
            }
            else
            {
              (void)OS04C10_Delay(pObj, 1);
            }
          }
        }
        break;

      case OS04C10_RGB565:
      default:
        for (index = 0; index < (sizeof(OS04C10_PF_RGB565) / 4U); index++)
        {
          if (ret != OS04C10_ERROR)
          {
            tmp = (uint8_t)OS04C10_PF_RGB565[index][1];
            if (os04c10_write_reg(&pObj->Ctx, OS04C10_PF_RGB565[index][0], &tmp, 1) != OS04C10_OK)
            {
              ret = OS04C10_ERROR;
            }
            else
            {
              (void)OS04C10_Delay(pObj, 1);
            }
          }
        }
        break;

    }

    if (PixelFormat == OS04C10_JPEG)
    {
      if (os04c10_read_reg(&pObj->Ctx, OS04C10_TIMING_TC_REG21, &tmp, 1) != OS04C10_OK)
      {
        ret = OS04C10_ERROR;
      }
      else
      {
        tmp |= (1 << 5);
        if (os04c10_write_reg(&pObj->Ctx, OS04C10_TIMING_TC_REG21, &tmp, 1) != OS04C10_OK)
        {
          ret = OS04C10_ERROR;
        }
        else
        {
          if (os04c10_read_reg(&pObj->Ctx, OS04C10_SYSREM_RESET02, &tmp, 1) != OS04C10_OK)
          {
            ret = OS04C10_ERROR;
          }
          else
          {
            tmp &= ~((1 << 4) | (1 << 3) | (1 << 2));
            if (os04c10_write_reg(&pObj->Ctx, OS04C10_SYSREM_RESET02, &tmp, 1) != OS04C10_OK)
            {
              ret = OS04C10_ERROR;
            }
            else
            {
              if (os04c10_read_reg(&pObj->Ctx, OS04C10_CLOCK_ENABLE02, &tmp, 1) != OS04C10_OK)
              {
                ret = OS04C10_ERROR;
              }
              else
              {
                tmp |= ((1 << 5) | (1 << 3));
                if (os04c10_write_reg(&pObj->Ctx, OS04C10_CLOCK_ENABLE02, &tmp, 1) != OS04C10_OK)
                {
                  ret = OS04C10_ERROR;
                }
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

/**
  * @brief  Set OS04C10 camera Pixel Format.
  * @param  pObj  pointer to component object
  * @param  PixelFormat pixel format to be configured
  * @retval Component status
  */
int32_t OS04C10_GetPixelFormat(OS04C10_Object_t *pObj, uint32_t *PixelFormat)
{
  (void)(pObj);
  (void)(PixelFormat);

  return OS04C10_ERROR;
}

/**
  * @brief  Get OS04C10 camera resolution.
  * @param  pObj  pointer to component object
  * @param  Resolution  Camera resolution
  * @retval Component status
  */
int32_t OS04C10_SetResolution(OS04C10_Object_t *pObj, uint32_t Resolution)
{
  int32_t ret = OS04C10_OK;
  uint32_t index;
  uint8_t tmp;

  /* Initialization sequence for WVGA resolution (800x480)*/
  static const uint16_t OS04C10_WVGA[][2] =
  {
    {OS04C10_TIMING_DVPHO_HIGH, 0x03},
    {OS04C10_TIMING_DVPHO_LOW, 0x20},
    {OS04C10_TIMING_DVPVO_HIGH, 0x01},
    {OS04C10_TIMING_DVPVO_LOW, 0xE0},
  };

  /* Initialization sequence for VGA resolution (640x480)*/
  static const uint16_t OS04C10_VGA[][2] =
  {
    {OS04C10_TIMING_DVPHO_HIGH, 0x02},
    {OS04C10_TIMING_DVPHO_LOW, 0x80},
    {OS04C10_TIMING_DVPVO_HIGH, 0x01},
    {OS04C10_TIMING_DVPVO_LOW, 0xE0},
  };

  /* Initialization sequence for 480x272 resolution */
  static const uint16_t OS04C10_480x272[][2] =
  {
    {OS04C10_TIMING_DVPHO_HIGH, 0x01},
    {OS04C10_TIMING_DVPHO_LOW, 0xE0},
    {OS04C10_TIMING_DVPVO_HIGH, 0x01},
    {OS04C10_TIMING_DVPVO_LOW, 0x10},
  };

  /* Initialization sequence for QVGA resolution (320x240) */
  static const uint16_t OS04C10_QVGA[][2] =
  {
    {OS04C10_TIMING_DVPHO_HIGH, 0x01},
    {OS04C10_TIMING_DVPHO_LOW, 0x40},
    {OS04C10_TIMING_DVPVO_HIGH, 0x00},
    {OS04C10_TIMING_DVPVO_LOW, 0xF0},
  };

  /* Initialization sequence for QQVGA resolution (160x120) */
  static const uint16_t OS04C10_QQVGA[][2] =
  {
    {OS04C10_TIMING_DVPHO_HIGH, 0x00},
    {OS04C10_TIMING_DVPHO_LOW, 0xA0},
    {OS04C10_TIMING_DVPVO_HIGH, 0x00},
    {OS04C10_TIMING_DVPVO_LOW, 0x78},
  };

  /* Check if resolution is supported */
  if (Resolution > OS04C10_R800x480)
  {
    ret = OS04C10_ERROR;
  }
  else
  {
    /* Initialize OS04C10 */
    switch (Resolution)
    {
      case OS04C10_R160x120:
        for (index = 0; index < (sizeof(OS04C10_QQVGA) / 4U); index++)
        {
          if (ret != OS04C10_ERROR)
          {
            tmp = (uint8_t)OS04C10_QQVGA[index][1];
            if (os04c10_write_reg(&pObj->Ctx, OS04C10_QQVGA[index][0], &tmp, 1) != OS04C10_OK)
            {
              ret = OS04C10_ERROR;
            }
          }
        }
        break;
      case OS04C10_R320x240:
        for (index = 0; index < (sizeof(OS04C10_QVGA) / 4U); index++)
        {
          if (ret != OS04C10_ERROR)
          {
            tmp = (uint8_t)OS04C10_QVGA[index][1];
            if (os04c10_write_reg(&pObj->Ctx, OS04C10_QVGA[index][0], &tmp, 1) != OS04C10_OK)
            {
              ret = OS04C10_ERROR;
            }
          }
        }
        break;
      case OS04C10_R480x272:
        for (index = 0; index < (sizeof(OS04C10_480x272) / 4U); index++)
        {
          if (ret != OS04C10_ERROR)
          {
            tmp = (uint8_t)OS04C10_480x272[index][1];
            if (os04c10_write_reg(&pObj->Ctx, OS04C10_480x272[index][0], &tmp, 1) != OS04C10_OK)
            {
              ret = OS04C10_ERROR;
            }
          }
        }
        break;
      case OS04C10_R640x480:
        for (index = 0; index < (sizeof(OS04C10_VGA) / 4U); index++)
        {
          if (ret != OS04C10_ERROR)
          {
            tmp = (uint8_t)OS04C10_VGA[index][1];
            if (os04c10_write_reg(&pObj->Ctx, OS04C10_VGA[index][0], &tmp, 1) != OS04C10_OK)
            {
              ret = OS04C10_ERROR;
            }
          }
        }
        break;
      case OS04C10_R800x480:
        for (index = 0; index < (sizeof(OS04C10_WVGA) / 4U); index++)
        {
          if (ret != OS04C10_ERROR)
          {
            tmp = (uint8_t)OS04C10_WVGA[index][1];
            if (os04c10_write_reg(&pObj->Ctx, OS04C10_WVGA[index][0], &tmp, 1) != OS04C10_OK)
            {
              ret = OS04C10_ERROR;
            }
          }
        }
        break;
      default:
        ret = OS04C10_ERROR;
        break;
    }
  }

  return ret;
}

/**
  * @brief  Get OS04C10 camera resolution.
  * @param  pObj  pointer to component object
  * @param  Resolution  Camera resolution
  * @retval Component status
  */
int32_t OS04C10_GetResolution(OS04C10_Object_t *pObj, uint32_t *Resolution)
{
  int32_t ret;
  uint16_t x_size;
  uint16_t y_size;
  uint8_t tmp;

  if (os04c10_read_reg(&pObj->Ctx, OS04C10_TIMING_DVPHO_HIGH, &tmp, 1) != OS04C10_OK)
  {
    ret = OS04C10_ERROR;
  }
  else
  {
    x_size = (uint16_t)tmp << 8U;

    if (os04c10_read_reg(&pObj->Ctx, OS04C10_TIMING_DVPHO_LOW, &tmp, 1) != OS04C10_OK)
    {
      ret = OS04C10_ERROR;
    }
    else
    {
      x_size |= tmp;

      if (os04c10_read_reg(&pObj->Ctx, OS04C10_TIMING_DVPVO_HIGH, &tmp, 1) != OS04C10_OK)
      {
        ret = OS04C10_ERROR;
      }
      else
      {
        y_size = (uint16_t)tmp << 8U;
        if (os04c10_read_reg(&pObj->Ctx, OS04C10_TIMING_DVPVO_LOW, &tmp, 1) != OS04C10_OK)
        {
          ret = OS04C10_ERROR;
        }
        else
        {
          y_size |= tmp;

          if ((x_size == 800U) && (y_size == 480U))
          {
            *Resolution = OS04C10_R800x480;
            ret = OS04C10_OK;
          }
          else if ((x_size == 640U) && (y_size == 480U))
          {
            *Resolution = OS04C10_R640x480;
            ret = OS04C10_OK;
          }
          else if ((x_size == 480U) && (y_size == 272U))
          {
            *Resolution = OS04C10_R480x272;
            ret = OS04C10_OK;
          }
          else if ((x_size == 320U) && (y_size == 240U))
          {
            *Resolution = OS04C10_R320x240;
            ret = OS04C10_OK;
          }
          else if ((x_size == 160U) && (y_size == 120U))
          {
            *Resolution = OS04C10_R160x120;
            ret = OS04C10_OK;
          }
          else
          {
            ret = OS04C10_ERROR;
          }
        }
      }
    }
  }

  return ret;
}

/**
  * @brief  Set OS04C10 camera PCLK, HREF and VSYNC Polarities
  * @param  pObj  pointer to component object
  * @param  PclkPolarity Polarity of the PixelClock
  * @param  HrefPolarity Polarity of the Href
  * @param  VsyncPolarity Polarity of the Vsync
  * @retval Component status
  */
int32_t OS04C10_SetPolarities(OS04C10_Object_t *pObj, uint32_t PclkPolarity, uint32_t HrefPolarity,
                             uint32_t VsyncPolarity)
{
  uint8_t tmp;
  int32_t ret = OS04C10_OK;

  if ((pObj == NULL) || ((PclkPolarity != OS04C10_POLARITY_PCLK_LOW) && (PclkPolarity != OS04C10_POLARITY_PCLK_HIGH)) ||
      ((HrefPolarity != OS04C10_POLARITY_HREF_LOW) && (HrefPolarity != OS04C10_POLARITY_HREF_HIGH)) ||
      ((VsyncPolarity != OS04C10_POLARITY_VSYNC_LOW) && (VsyncPolarity != OS04C10_POLARITY_VSYNC_HIGH)))
  {
    ret = OS04C10_ERROR;
  }
  else
  {
    tmp = (uint8_t)(PclkPolarity << 5U) | (HrefPolarity << 1U) | VsyncPolarity;

    if (os04c10_write_reg(&pObj->Ctx, OS04C10_POLARITY_CTRL, &tmp, 1) != OS04C10_OK)
    {
      ret = OS04C10_ERROR;
    }
  }

  return ret;
}

/**
  * @brief  get OS04C10 camera PCLK, HREF and VSYNC Polarities
  * @param  pObj  pointer to component object
  * @param  PclkPolarity Polarity of the PixelClock
  * @param  HrefPolarity Polarity of the Href
  * @param  VsyncPolarity Polarity of the Vsync
  * @retval Component status
  */
int32_t OS04C10_GetPolarities(OS04C10_Object_t *pObj, uint32_t *PclkPolarity, uint32_t *HrefPolarity,
                             uint32_t *VsyncPolarity)
{
  uint8_t tmp;
  int32_t ret = OS04C10_OK;

  if ((pObj == NULL) || (PclkPolarity == NULL) || (HrefPolarity == NULL) || (VsyncPolarity == NULL))
  {
    ret = OS04C10_ERROR;
  }
  else if (os04c10_read_reg(&pObj->Ctx, OS04C10_POLARITY_CTRL, &tmp, 1) != OS04C10_OK)
  {
    ret = OS04C10_ERROR;
  }
  else
  {
    *PclkPolarity = (tmp >> 5U) & 0x01U;
    *HrefPolarity = (tmp >> 1U) & 0x01U;
    *VsyncPolarity = tmp & 0x01;
  }

  return ret;
}

/**
  * @brief  Read the OS04C10 Camera identity.
  * @param  pObj  pointer to component object
  * @param  Id    pointer to component ID
  * @retval Component status
  */
int32_t OS04C10_ReadID(OS04C10_Object_t *pObj, uint32_t *Id)
{
  int32_t ret;
  uint8_t tmp;

  // /* Initialize I2C */
  // pObj->IO.Init();

  /* Prepare the camera to be configured */
  tmp = 0x80;
  if (os04c10_write_reg(&pObj->Ctx, OS04C10_SYSTEM_CTROL0, &tmp, 1) != OS04C10_OK)
  {
    // printf("os04c10_write_reg failed\r\n");
    ret = OS04C10_ERROR;
  }
  else
  {
    (void)OS04C10_Delay(pObj, 5);

    if (os04c10_read_reg(&pObj->Ctx, OS04C10_CHIP_ID_HIGH_BYTE, &tmp, 1) != OS04C10_OK)
    {
      ret = OS04C10_ERROR;
    }
    else
    {
      *Id = (uint32_t)tmp << 8U;
      if (os04c10_read_reg(&pObj->Ctx, OS04C10_CHIP_ID_LOW_BYTE, &tmp, 1) != OS04C10_OK)
      {
        ret = OS04C10_ERROR;
      }
      else
      {
        *Id |= tmp;
        ret = OS04C10_OK;
      }
    }
  }

  /* Component status */
  return ret;
}

/**
  * @brief  Read the OS04C10 Camera Capabilities.
  * @param  pObj          pointer to component object
  * @param  Capabilities  pointer to component Capabilities
  * @retval Component status
  */
int32_t OS04C10_GetCapabilities(OS04C10_Object_t *pObj, OS04C10_Capabilities_t *Capabilities)
{
  int32_t ret;

  if (pObj == NULL)
  {
    ret = OS04C10_ERROR;
  }
  else
  {
    Capabilities->Config_Brightness    = 0;
    Capabilities->Config_Contrast      = 0;
    Capabilities->Config_HueDegree     = 0;
    Capabilities->Config_Gain          = 1;
    Capabilities->Config_Exposure      = 1;
    Capabilities->Config_LightMode     = 0;
    Capabilities->Config_MirrorFlip    = 1;
    Capabilities->Config_NightMode     = 0;
    Capabilities->Config_Resolution    = 0;
    Capabilities->Config_Saturation    = 0;
    Capabilities->Config_SpecialEffect = 0;
    Capabilities->Config_Zoom          = 0;
    ret = OS04C10_OK;
  }

  return ret;
}

/**
  * @brief  Set the OS04C10 camera Light Mode.
  * @param  pObj  pointer to component object
  * @param  Effect  Effect to be configured
  * @retval Component status
  */
int32_t OS04C10_SetLightMode(OS04C10_Object_t *pObj, uint32_t LightMode)
{
  int32_t ret;
  uint32_t index;
  uint8_t tmp;

  /* OS04C10 Light Mode setting */
  static const uint16_t OS04C10_LightModeAuto[][2] =
  {
    {OS04C10_AWB_MANUAL_CONTROL, 0x00},
    {OS04C10_AWB_R_GAIN_MSB, 0x04},
    {OS04C10_AWB_R_GAIN_LSB, 0x00},
    {OS04C10_AWB_G_GAIN_MSB, 0x04},
    {OS04C10_AWB_G_GAIN_LSB, 0x00},
    {OS04C10_AWB_B_GAIN_MSB, 0x04},
    {OS04C10_AWB_B_GAIN_LSB, 0x00},
  };

  static const uint16_t OS04C10_LightModeCloudy[][2] =
  {
    {OS04C10_AWB_MANUAL_CONTROL, 0x01},
    {OS04C10_AWB_R_GAIN_MSB, 0x06},
    {OS04C10_AWB_R_GAIN_LSB, 0x48},
    {OS04C10_AWB_G_GAIN_MSB, 0x04},
    {OS04C10_AWB_G_GAIN_LSB, 0x00},
    {OS04C10_AWB_B_GAIN_MSB, 0x04},
    {OS04C10_AWB_B_GAIN_LSB, 0xD3},
  };

  static const uint16_t OS04C10_LightModeOffice[][2] =
  {
    {OS04C10_AWB_MANUAL_CONTROL, 0x01},
    {OS04C10_AWB_R_GAIN_MSB, 0x05},
    {OS04C10_AWB_R_GAIN_LSB, 0x48},
    {OS04C10_AWB_G_GAIN_MSB, 0x04},
    {OS04C10_AWB_G_GAIN_LSB, 0x00},
    {OS04C10_AWB_B_GAIN_MSB, 0x07},
    {OS04C10_AWB_B_GAIN_LSB, 0xCF},
  };

  static const uint16_t OS04C10_LightModeHome[][2] =
  {
    {OS04C10_AWB_MANUAL_CONTROL, 0x01},
    {OS04C10_AWB_R_GAIN_MSB, 0x04},
    {OS04C10_AWB_R_GAIN_LSB, 0x10},
    {OS04C10_AWB_G_GAIN_MSB, 0x04},
    {OS04C10_AWB_G_GAIN_LSB, 0x00},
    {OS04C10_AWB_B_GAIN_MSB, 0x08},
    {OS04C10_AWB_B_GAIN_LSB, 0xB6},
  };

  static const uint16_t OS04C10_LightModeSunny[][2] =
  {
    {OS04C10_AWB_MANUAL_CONTROL, 0x01},
    {OS04C10_AWB_R_GAIN_MSB, 0x06},
    {OS04C10_AWB_R_GAIN_LSB, 0x1C},
    {OS04C10_AWB_G_GAIN_MSB, 0x04},
    {OS04C10_AWB_G_GAIN_LSB, 0x00},
    {OS04C10_AWB_B_GAIN_MSB, 0x04},
    {OS04C10_AWB_B_GAIN_LSB, 0xF3},
  };

  tmp = 0x00;
  ret = os04c10_write_reg(&pObj->Ctx, OS04C10_AWB_MANUAL_CONTROL, &tmp, 1);
  if (ret == OS04C10_OK)
  {
    tmp = 0x46;
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_AWB_CTRL16, &tmp, 1);
  }

  if (ret == OS04C10_OK)
  {
    tmp = 0xF8;
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_AWB_CTRL17, &tmp, 1);
  }

  if (ret == OS04C10_OK)
  {
    tmp = 0x04;
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_AWB_CTRL18, &tmp, 1);
  }

  if (ret == OS04C10_OK)
  {
    switch (LightMode)
    {
      case OS04C10_LIGHT_SUNNY:
        for (index = 0; index < (sizeof(OS04C10_LightModeSunny) / 4U) ; index++)
        {
          if (ret != OS04C10_ERROR)
          {
            tmp = (uint8_t)OS04C10_LightModeSunny[index][1];
            if (os04c10_write_reg(&pObj->Ctx, OS04C10_LightModeSunny[index][0], &tmp, 1) != OS04C10_OK)
            {
              ret = OS04C10_ERROR;
            }
          }
        }
        break;
      case OS04C10_LIGHT_OFFICE:
        for (index = 0; index < (sizeof(OS04C10_LightModeOffice) / 4U) ; index++)
        {
          if (ret != OS04C10_ERROR)
          {
            tmp = (uint8_t)OS04C10_LightModeOffice[index][1];
            if (os04c10_write_reg(&pObj->Ctx, OS04C10_LightModeOffice[index][0], &tmp, 1) != OS04C10_OK)
            {
              ret = OS04C10_ERROR;
            }
          }
        }
        break;
      case OS04C10_LIGHT_CLOUDY:
        for (index = 0; index < (sizeof(OS04C10_LightModeCloudy) / 4U) ; index++)
        {
          if (ret != OS04C10_ERROR)
          {
            tmp = (uint8_t)OS04C10_LightModeCloudy[index][1];
            if (os04c10_write_reg(&pObj->Ctx, OS04C10_LightModeCloudy[index][0], &tmp, 1) != OS04C10_OK)
            {
              ret = OS04C10_ERROR;
            }
          }
        }
        break;
      case OS04C10_LIGHT_HOME:
        for (index = 0; index < (sizeof(OS04C10_LightModeHome) / 4U) ; index++)
        {
          if (ret != OS04C10_ERROR)
          {
            tmp = (uint8_t)OS04C10_LightModeHome[index][1];
            if (os04c10_write_reg(&pObj->Ctx, OS04C10_LightModeHome[index][0], &tmp, 1) != OS04C10_OK)
            {
              ret = OS04C10_ERROR;
            }
          }
        }
        break;
      case OS04C10_LIGHT_AUTO:
      default :
        for (index = 0; index < (sizeof(OS04C10_LightModeAuto) / 4U) ; index++)
        {
          if (ret != OS04C10_ERROR)
          {
            tmp = (uint8_t)OS04C10_LightModeAuto[index][1];
            if (os04c10_write_reg(&pObj->Ctx, OS04C10_LightModeAuto[index][0], &tmp, 1) != OS04C10_OK)
            {
              ret = OS04C10_ERROR;
            }
          }
        }
        break;
    }
  }
  return ret;
}

/**
  * @brief  Set the OS04C10 camera Special Effect.
  * @param  pObj  pointer to component object
  * @param  Effect  Effect to be configured
  * @retval Component status
  */
int32_t OS04C10_SetColorEffect(OS04C10_Object_t *pObj, uint32_t Effect)
{
  int32_t ret;
  uint8_t tmp;

  switch (Effect)
  {
    case OS04C10_COLOR_EFFECT_BLUE:
      tmp = 0xFF;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_ISP_CONTROL01, &tmp, 1);

      if (ret == OS04C10_OK)
      {
        tmp = 0x18;
        ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL0, &tmp, 1);
      }
      if (ret == OS04C10_OK)
      {
        tmp = 0xA0;
        ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL3, &tmp, 1);
      }
      if (ret == OS04C10_OK)
      {
        tmp = 0x40;
        ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL4, &tmp, 1);
      }

      if (ret != OS04C10_OK)
      {
        ret = OS04C10_ERROR;
      }
      break;

    case OS04C10_COLOR_EFFECT_RED:
      tmp = 0xFF;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_ISP_CONTROL01, &tmp, 1);

      if (ret == OS04C10_OK)
      {
        tmp = 0x18;
        ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL0, &tmp, 1);
      }
      if (ret == OS04C10_OK)
      {
        tmp = 0x80;
        ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL3, &tmp, 1);
      }
      if (ret == OS04C10_OK)
      {
        tmp = 0xC0;
        ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL4, &tmp, 1);
      }

      if (ret != OS04C10_OK)
      {
        ret = OS04C10_ERROR;
      }
      break;

    case OS04C10_COLOR_EFFECT_GREEN:
      tmp = 0xFF;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_ISP_CONTROL01, &tmp, 1);

      if (ret == OS04C10_OK)
      {
        tmp = 0x18;
        ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL0, &tmp, 1);
      }
      if (ret == OS04C10_OK)
      {
        tmp = 0x60;
        ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL3, &tmp, 1);
      }
      if (ret == OS04C10_OK)
      {
        tmp = 0x60;
        ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL4, &tmp, 1);
      }

      if (ret != OS04C10_OK)
      {
        ret = OS04C10_ERROR;
      }
      break;

    case OS04C10_COLOR_EFFECT_BW:
      tmp = 0xFF;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_ISP_CONTROL01, &tmp, 1);

      if (ret == OS04C10_OK)
      {
        tmp = 0x18;
        ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL0, &tmp, 1);
      }
      if (ret == OS04C10_OK)
      {
        tmp = 0x80;
        ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL3, &tmp, 1);
      }
      if (ret == OS04C10_OK)
      {
        tmp = 0x80;
        ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL4, &tmp, 1);
      }

      if (ret != OS04C10_OK)
      {
        ret = OS04C10_ERROR;
      }
      break;

    case OS04C10_COLOR_EFFECT_SEPIA:
      tmp = 0xFF;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_ISP_CONTROL01, &tmp, 1);

      if (ret == OS04C10_OK)
      {
        tmp = 0x18;
        ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL0, &tmp, 1);
      }
      if (ret == OS04C10_OK)
      {
        tmp = 0x40;
        ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL3, &tmp, 1);
      }
      if (ret == OS04C10_OK)
      {
        tmp = 0xA0;
        ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL4, &tmp, 1);
      }

      if (ret != OS04C10_OK)
      {
        ret = OS04C10_ERROR;
      }
      break;

    case OS04C10_COLOR_EFFECT_NEGATIVE:
      tmp = 0xFF;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_ISP_CONTROL01, &tmp, 1);

      if (ret == OS04C10_OK)
      {
        tmp = 0x40;
        ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL0, &tmp, 1);
      }
      if (ret != OS04C10_OK)
      {
        ret = OS04C10_ERROR;
      }
      break;

    case OS04C10_COLOR_EFFECT_NONE:
    default :
      tmp = 0x7F;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_ISP_CONTROL01, &tmp, 1);

      if (ret == OS04C10_OK)
      {
        tmp = 0x00;
        ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL0, &tmp, 1);
      }

      if (ret != OS04C10_OK)
      {
        ret = OS04C10_ERROR;
      }

      break;
  }

  return ret;
}

/**
  * @brief  Set the OS04C10 camera Brightness Level.
  * @note   The brightness of OS04C10 could be adjusted. Higher brightness will
  *         make the picture more bright. The side effect of higher brightness
  *         is the picture looks foggy.
  * @param  pObj  pointer to component object
  * @param  Level Value to be configured
  * @retval Component status
  */
int32_t OS04C10_SetBrightness(OS04C10_Object_t *pObj, int32_t Level)
{
  int32_t ret;
  const uint8_t brightness_level[] = {0x40U, 0x30U, 0x20U, 0x10U, 0x00U, 0x10U, 0x20U, 0x30U, 0x40U};
  uint8_t tmp;

  tmp = 0xFF;
  ret = os04c10_write_reg(&pObj->Ctx, OS04C10_ISP_CONTROL01, &tmp, 1);

  if (ret == OS04C10_OK)
  {
    tmp = brightness_level[Level + 4];
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL7, &tmp, 1);
  }
  if (ret == OS04C10_OK)
  {
    tmp = 0x04;
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL0, &tmp, 1);
  }

  if (ret == OS04C10_OK)
  {
    if (Level < 0)
    {
      tmp = 0x01;
      if (os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL8, &tmp, 1) != OS04C10_OK)
      {
        ret = OS04C10_ERROR;
      }
    }
    else
    {
      tmp = 0x09;
      if (os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL8, &tmp, 1) != OS04C10_OK)
      {
        ret = OS04C10_ERROR;
      }
    }
  }

  return ret;
}

/**
  * @brief  Set the OS04C10 camera Saturation Level.
  * @note   The color saturation of OS04C10 could be adjusted. High color saturation
  *         would make the picture looks more vivid, but the side effect is the
  *         bigger noise and not accurate skin color.
  * @param  pObj  pointer to component object
  * @param  Level Value to be configured
  * @retval Component status
  */
int32_t OS04C10_SetSaturation(OS04C10_Object_t *pObj, int32_t Level)
{
  int32_t ret;
  const uint8_t saturation_level[] = {0x00U, 0x10U, 0x20U, 0x30U, 0x80U, 0x70U, 0x60U, 0x50U, 0x40U};
  uint8_t tmp;

  tmp = 0xFF;
  ret = os04c10_write_reg(&pObj->Ctx, OS04C10_ISP_CONTROL01, &tmp, 1);

  if (ret == OS04C10_OK)
  {
    tmp = saturation_level[Level + 4];
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL3, &tmp, 1);
  }
  if (ret == OS04C10_OK)
  {
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL4, &tmp, 1);
  }
  if (ret == OS04C10_OK)
  {
    tmp = 0x02;
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL0, &tmp, 1);
  }

  if (ret == OS04C10_OK)
  {
    tmp = 0x41;
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL8, &tmp, 1);
  }

  if (ret != OS04C10_OK)
  {
    ret = OS04C10_ERROR;
  }

  return ret;
}

/**
  * @brief  Set the OS04C10 camera Contrast Level.
  * @note   The contrast of OS04C10 could be adjusted. Higher contrast will make
  *         the picture sharp. But the side effect is losing dynamic range.
  * @param  pObj  pointer to component object
  * @param  Level Value to be configured
  * @retval Component status
  */
int32_t OS04C10_SetContrast(OS04C10_Object_t *pObj, int32_t Level)
{
  int32_t ret;
  const uint8_t contrast_level[] = {0x10U, 0x14U, 0x18U, 0x1CU, 0x20U, 0x24U, 0x28U, 0x2CU, 0x30U};
  uint8_t tmp;

  tmp = 0xFF;
  ret = os04c10_write_reg(&pObj->Ctx, OS04C10_ISP_CONTROL01, &tmp, 1);

  if (ret == OS04C10_OK)
  {
    tmp = 0x04;
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL0, &tmp, 1);
  }
  if (ret == OS04C10_OK)
  {
    tmp = contrast_level[Level + 4];
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL6, &tmp, 1);
  }
  if (ret == OS04C10_OK)
  {
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL5, &tmp, 1);
  }
  if (ret == OS04C10_OK)
  {
    tmp = 0x41;
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL8, &tmp, 1);
  }

  if (ret != OS04C10_OK)
  {
    ret = OS04C10_ERROR;
  }

  return ret;
}

/**
  * @brief  Set the OS04C10 camera Hue degree.
  * @param  pObj  pointer to component object
  * @param  Level Value to be configured
  * @retval Component status
  */
int32_t OS04C10_SetHueDegree(OS04C10_Object_t *pObj, int32_t Degree)
{
  int32_t ret;
  const uint8_t hue_degree_ctrl1[] = {0x80U, 0x6FU, 0x40U, 0x00U, 0x40U, 0x6FU, 0x80U, 0x6FU, 0x40U, 0x00U, 0x40U,
                                      0x6FU
                                     };
  const uint8_t hue_degree_ctrl2[] = {0x00U, 0x40U, 0x6FU, 0x80U, 0x6FU, 0x40U, 0x00U, 0x40U, 0x6FU, 0x80U, 0x6FU,
                                      0x40U
                                     };
  const uint8_t hue_degree_ctrl8[] = {0x32U, 0x32U, 0x32U, 0x02U, 0x02U, 0x02U, 0x01U, 0x01U, 0x01U, 0x31U, 0x31U,
                                      0x31U
                                     };
  uint8_t tmp;

  tmp = 0xFF;
  ret = os04c10_write_reg(&pObj->Ctx, OS04C10_ISP_CONTROL01, &tmp, 1);

  if (ret == OS04C10_OK)
  {
    tmp = 0x01;
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL0, &tmp, 1);
  }
  if (ret == OS04C10_OK)
  {
    tmp = hue_degree_ctrl1[Degree + 6];
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL1, &tmp, 1);
  }
  if (ret == OS04C10_OK)
  {
    tmp = hue_degree_ctrl2[Degree + 6];
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL2, &tmp, 1);
  }
  if (ret == OS04C10_OK)
  {
    tmp = hue_degree_ctrl8[Degree + 6];
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL8, &tmp, 1);
  }

  if (ret != OS04C10_OK)
  {
    ret = OS04C10_ERROR;
  }

  return ret;
}

/**
  * @brief  Control OS04C10 camera mirror/vflip.
  * @param  pObj  pointer to component object
  * @param  Config To configure mirror, flip, both or none
  * @retval Component status
  */
int32_t OS04C10_MirrorFlipConfig(OS04C10_Object_t *pObj, uint32_t Config)
{
  int32_t ret = OS04C10_OK;
  uint8_t reg3820_val;
  uint8_t reg3716_val;

  switch (Config)
  {
    case OS04C10_MIRROR:
      reg3820_val = 0x80;
      reg3716_val = 0x24;
      break;
    case OS04C10_FLIP:
      reg3820_val = 0xB8;
      reg3716_val = 0x04;
      break;
    case OS04C10_MIRROR_FLIP:
      reg3820_val = 0xB0;
      reg3716_val = 0x04;
      break;
    case OS04C10_MIRROR_FLIP_NONE:
    default:
      reg3820_val = 0x88;
      reg3716_val = 0x24;
      break;
  }

  if (os04c10_write_reg(&pObj->Ctx, 0x3820, &reg3820_val, 1) != OS04C10_OK)
  {
    ret = OS04C10_ERROR;
  }

  if (ret == OS04C10_OK)
  {
    if (os04c10_write_reg(&pObj->Ctx, 0x3716, &reg3716_val, 1) != OS04C10_OK)
    {
      ret = OS04C10_ERROR;
    }
  }

  return ret;
}

/**
  * @brief  Control OS04C10 camera zooming.
  * @param  pObj  pointer to component object
  * @param  Zoom  Zoom to be configured
  * @retval Component status
  */
int32_t OS04C10_ZoomConfig(OS04C10_Object_t *pObj, uint32_t Zoom)
{
  int32_t ret = OS04C10_OK;
  uint32_t res;
  uint32_t zoom;
  uint8_t tmp;

  /* Get camera resolution */
  if (OS04C10_GetResolution(pObj, &res) != OS04C10_OK)
  {
    ret = OS04C10_ERROR;
  }
  else
  {
    zoom = Zoom;

    if (zoom == OS04C10_ZOOM_x1)
    {
      tmp = 0x10;
      if (os04c10_write_reg(&pObj->Ctx, OS04C10_SCALE_CTRL0, &tmp, 1) != OS04C10_OK)
      {
        ret = OS04C10_ERROR;
      }
    }
    else
    {
      switch (res)
      {
        case OS04C10_R320x240:
        case OS04C10_R480x272:
          zoom = zoom >> 1U;
          break;
        case OS04C10_R640x480:
          zoom = zoom >> 2U;
          break;
        default:
          break;
      }

      tmp = 0x00;
      if (os04c10_write_reg(&pObj->Ctx, OS04C10_SCALE_CTRL0, &tmp, 1) != OS04C10_OK)
      {
        ret = OS04C10_ERROR;
      }
      else
      {
        tmp = (uint8_t)zoom;
        if (os04c10_write_reg(&pObj->Ctx, OS04C10_SCALE_CTRL1, &tmp, 1) != OS04C10_OK)
        {
          ret = OS04C10_ERROR;
        }
      }
    }
  }

  return ret;
}

/**
  * @brief  Enable/disable the OS04C10 camera night mode.
  * @param  pObj  pointer to component object
  * @param  Cmd   Enable disable night mode
  * @retval Component status
  */
int32_t OS04C10_NightModeConfig(OS04C10_Object_t *pObj, uint32_t Cmd)
{
  int32_t ret;
  uint8_t tmp = 0;

  if (Cmd == NIGHT_MODE_ENABLE)
  {
    /* Auto Frame Rate: 15fps ~ 3.75fps night mode for 60/50Hz light environment,
    24Mhz clock input,24Mhz PCLK*/
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SC_PLL_CONTRL4, &tmp, 1);
    if (ret == OS04C10_OK)
    {
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SC_PLL_CONTRL5, &tmp, 1);
    }
    if (ret == OS04C10_OK)
    {
      tmp = 0x7C;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_AEC_CTRL00, &tmp, 1);
    }
    if (ret == OS04C10_OK)
    {
      tmp = 0x01;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_AEC_B50_STEP_HIGH, &tmp, 1);
    }
    if (ret == OS04C10_OK)
    {
      tmp = 0x27;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_AEC_B50_STEP_LOW, &tmp, 1);
    }
    if (ret == OS04C10_OK)
    {
      tmp = 0x00;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_AEC_B60_STEP_HIGH, &tmp, 1);
    }
    if (ret == OS04C10_OK)
    {
      tmp = 0xF6;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_AEC_B60_STEP_LOW, &tmp, 1);
    }
    if (ret == OS04C10_OK)
    {
      tmp = 0x04;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_AEC_CTRL0D, &tmp, 1);
    }
    if (ret == OS04C10_OK)
    {
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_AEC_CTRL0E, &tmp, 1);
    }
    if (ret == OS04C10_OK)
    {
      tmp = 0x0B;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_AEC_CTRL02, &tmp, 1);
    }
    if (ret == OS04C10_OK)
    {
      tmp = 0x88;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_AEC_CTRL03, &tmp, 1);
    }
    if (ret == OS04C10_OK)
    {
      tmp = 0x0B;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_AEC_MAX_EXPO_HIGH, &tmp, 1);
    }
    if (ret == OS04C10_OK)
    {
      tmp = 0x88;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_AEC_MAX_EXPO_LOW, &tmp, 1);
    }
    if (ret != OS04C10_OK)
    {
      ret = OS04C10_ERROR;
    }
  }
  else
  {
    if (os04c10_read_reg(&pObj->Ctx, OS04C10_AEC_CTRL00, &tmp, 1) != OS04C10_OK)
    {
      ret = OS04C10_ERROR;
    }
    else
    {
      ret = OS04C10_OK;
      tmp &= 0xFBU;
      /* Set Bit 2 to 0 */
      if (os04c10_write_reg(&pObj->Ctx, OS04C10_AEC_CTRL00, &tmp, 1) != OS04C10_OK)
      {
        ret = OS04C10_ERROR;
      }
    }
  }

  return ret;
}


static int32_t mdBToGainTimes1024(int32_t mdB) 
{
  double dB = mdB / 1000.0; // Convert mdB to dB
  double gain = pow(10, dB / 20.0);
  return (int32_t)(gain * 1024);
}

/**
  * @brief  Set the gain
  * @param  pObj  pointer to component object
  * @param  Gain Gain in mdB
  * @retval Component status
  */
int32_t OS04C10_SetGain(OS04C10_Object_t *pObj, int32_t mdB)
{
  int32_t ret = OS04C10_OK;
  int32_t gain = mdBToGainTimes1024(mdB);
  
  uint32_t input_gain = 0;
  uint16_t gain16 = 0;
  struct regval tGain_reg[4] = 
  {
    {0x3508, 0x00},//long a-gain[13:8]
    {0x3509, 0x80},//long a-gain[7:0]
    {0x350A, 0x00},// d-gain[13:8]
    {0x350B, 0x00},// d-gain[7:0]
  };

  if (gain < 1024) gain = 1024;
  else if (gain >= OS04C10_SENSOR_MAX_GAIN) gain = OS04C10_SENSOR_MAX_GAIN;

  input_gain = gain;
  if(gain < 1024)
      gain = 1024;
  else if(gain >= OS04C10_MAX_A_GAIN)
      gain = OS04C10_MAX_A_GAIN;

  /* A Gain */
  if (gain < 1024) {
      gain = 1024;
  } else if ((gain >= 1024) && (gain < 2048)) {
      gain = (gain>>6)<<6;
  } else if ((gain >=2048) && (gain < 4096)) {
      gain = (gain>>7)<<7;
  } else if ((gain >= 4096) && (gain < 8192)) {
      gain = (gain>>8)<<8;
  } else if ((gain >= 8192) && (gain < OS04C10_MAX_A_GAIN)) {
      gain = (gain>>9)<<9;
  } else {
      gain = OS04C10_MAX_A_GAIN;
  }

  gain16=(uint16_t)(gain>>3);
  tGain_reg[0].val = (gain16>>8)&0x3f;//high bit
  tGain_reg[1].val = gain16&0xff; //low byte

  if(input_gain > OS04C10_MAX_A_GAIN){
      tGain_reg[2].val=(uint16_t)((input_gain * 4) / OS04C10_MAX_A_GAIN) & 0x3F;
      tGain_reg[3].val=(uint16_t)((input_gain * 1024) / OS04C10_MAX_A_GAIN) & 0xFF;
  }
  else{
      uint16_t tmp_dgain = ((input_gain * 1024) / gain);
      tGain_reg[2].val=(uint16_t)((tmp_dgain >> 8) & 0x3F);
      tGain_reg[3].val=(uint16_t)(tmp_dgain & 0xFF);
  }

  if(OS04C10_WriteTable(pObj, tGain_reg, ARRAY_SIZE(tGain_reg)) != OS04C10_OK)
  {
    ret = OS04C10_ERROR;
  }
  return ret;
}

/**
  * @brief  Set the exposure
  * @param  pObj  pointer to component object
  * @param  Exposure Exposure in micro seconds
  * @retval Component status
  */
int32_t OS04C10_SetExposure(OS04C10_Object_t *pObj, int32_t exposure)
{
  int32_t ret = OS04C10_OK;
  uint32_t lines = 0;
  uint8_t hold;
  struct regval tExpo_reg[] = {
    {0x3500, 0x00},//long exp[19,16]
    {0x3501, 0x02},//long exp[15,8]
    {0x3502, 0x00},//long exp[7,0]
  };

  lines = (uint32_t)(exposure / OS04C10_1H_PERIOD_USEC);
  if (lines < 2)
  {
    lines = 2;
  }

  tExpo_reg[0].val = (lines >> 16) & 0x0F;
  tExpo_reg[1].val = (lines >> 8) & 0xFF;
  tExpo_reg[2].val = lines & 0xFF;
  
  hold = 0x00;
  if(os04c10_write_reg(&pObj->Ctx, 0x3208, &hold, 1) != OS04C10_OK)
  {
    ret = OS04C10_ERROR;
  }
  else
  {
    if(OS04C10_WriteTable(pObj, tExpo_reg, ARRAY_SIZE(tExpo_reg)) != OS04C10_OK)
    {
      ret = OS04C10_ERROR;
    }
    else
    {
      hold = 0x10;
      if(os04c10_write_reg(&pObj->Ctx, 0x3208, &hold, 1) != OS04C10_OK)
      {
        ret = OS04C10_ERROR;
      }

      hold = 0xa0;
      if(os04c10_write_reg(&pObj->Ctx, 0x3208, &hold, 1) != OS04C10_OK)
      {
        ret = OS04C10_ERROR;
      }
    }
  }

  return ret;
}

/**
  * @brief  Set the Frequency
  * @param  pObj  pointer to component object
  * @param  frequency in Mhz
  * @retval Component status
  */
int32_t OS04C10_SetFrequency(OS04C10_Object_t *pObj, int32_t frequency)
{
  uint32_t ret = OS04C10_OK;

  // switch (frequency)
  // {
  //   case OS04C10_INCK_74MHZ:
  //     if(OS04C10_WriteTable(pObj, inck_74Mhz_regs, ARRAY_SIZE(inck_74Mhz_regs)) != OS04C10_OK)
  //     {
  //       ret = OS04C10_ERROR;
  //     }
  //     break;
  //   case OS04C10_INCK_27MHZ:
  //     if(OS04C10_WriteTable(pObj, inck_27Mhz_regs, ARRAY_SIZE(inck_27Mhz_regs)) != OS04C10_OK)
  //     {
  //       ret = OS04C10_ERROR;
  //     }
  //     break;
  //   case OS04C10_INCK_24MHZ:
  //     if(OS04C10_WriteTable(pObj, inck_24Mhz_regs, ARRAY_SIZE(inck_24Mhz_regs)) != OS04C10_OK)
  //     {
  //       ret = OS04C10_ERROR;
  //     }
  //     break;
  //   case OS04C10_INCK_18MHZ:
  //     if(OS04C10_WriteTable(pObj, inck_18Mhz_regs, ARRAY_SIZE(inck_18Mhz_regs)) != OS04C10_OK)
  //     {
  //       ret = OS04C10_ERROR;
  //     }
  //     break;
  //   default:
  //     /* OS04C10_INCK_6MHZ */
  //     if(OS04C10_WriteTable(pObj, inck_6Mhz_regs, ARRAY_SIZE(inck_6Mhz_regs)) != OS04C10_OK)
  //     {
  //       ret = OS04C10_ERROR;
  //     }
  //     break;
  // };

  return ret;
}

/**
  * @brief  Set the Framerate
  * @param  pObj  pointer to component object
  * @param  framerate 10, 15, 20, 25 or 30fps
  * @retval Component status
  */
int32_t OS04C10_SetFramerate(OS04C10_Object_t *pObj, int32_t framerate)
{
  uint32_t ret = OS04C10_OK;
  // switch (framerate)
  // {
  //   case 10:
  //     if(OS04C10_WriteTable(pObj, framerate_10fps_regs, ARRAY_SIZE(framerate_10fps_regs)) != OS04C10_OK)
  //     {
  //       ret = OS04C10_ERROR;
  //     }
  //     break;
  //   case 15:
  //     if(OS04C10_WriteTable(pObj, framerate_15fps_regs, ARRAY_SIZE(framerate_15fps_regs)) != OS04C10_OK)
  //     {
  //       ret = OS04C10_ERROR;
  //     }
  //     break;
  //   case 20:
  //     if(OS04C10_WriteTable(pObj, framerate_20fps_regs, ARRAY_SIZE(framerate_20fps_regs)) != OS04C10_OK)
  //     {
  //      ret = OS04C10_ERROR;
  //     }
  //     break;
  //   case 25:
  //     if(OS04C10_WriteTable(pObj, framerate_25fps_regs, ARRAY_SIZE(framerate_25fps_regs)) != OS04C10_OK)
  //     {
  //       ret = OS04C10_ERROR;
  //     }
  //     break;
  //   default:
  //     /* 30fps */
  //     if(OS04C10_WriteTable(pObj, framerate_30fps_regs, ARRAY_SIZE(framerate_30fps_regs)) != OS04C10_OK)
  //     {
  //       ret = OS04C10_ERROR;
  //     }
  //     break;
  // };

  return ret;
}

/**
  * @brief  Configure Embedded Synchronization mode.
  * @param  pObj  pointer to component object
  * @param  pSyncCodes  pointer to Embedded Codes
  * @retval Component status
  */

int32_t OS04C10_EmbeddedSynchroConfig(OS04C10_Object_t *pObj, OS04C10_SyncCodes_t *pSyncCodes)
{
  uint8_t tmp;
  int32_t ret = OS04C10_ERROR;

  /*[7] : SYNC code from reg 0x4732-0x4732, [1]: Enable Clip ,[0]: Enable CCIR656 */
  tmp = 0x83;
  if (os04c10_write_reg(&pObj->Ctx, OS04C10_CCIR656_CTRL00, &tmp, 1) == OS04C10_OK)
  {
    tmp = pSyncCodes->FrameStartCode;
    if (os04c10_write_reg(&pObj->Ctx, OS04C10_CCIR656_FS, &tmp, 1) == OS04C10_OK)
    {
      tmp = pSyncCodes->FrameEndCode;
      if (os04c10_write_reg(&pObj->Ctx, OS04C10_CCIR656_FE, &tmp, 1) != OS04C10_OK)
      {
        return OS04C10_ERROR;
      }
      tmp = pSyncCodes->LineStartCode;
      if (os04c10_write_reg(&pObj->Ctx, OS04C10_CCIR656_LS, &tmp, 1) == OS04C10_OK)
      {
        tmp = pSyncCodes->LineEndCode;
        if (os04c10_write_reg(&pObj->Ctx, OS04C10_CCIR656_LE, &tmp, 1) == OS04C10_OK)
        {
          /*Adding 1 dummy line */
          tmp = 0x01;
          if (os04c10_write_reg(&pObj->Ctx, OS04C10_656_DUMMY_LINE, &tmp, 1) == OS04C10_OK)
          {
            ret = OS04C10_OK;
          }
        }
      }
    }
  }

  /* max clip value[9:8], to avoid SYNC code clipping */
  tmp = 0x2;
  if (ret == OS04C10_OK)
  {
    ret = os04c10_write_reg(&pObj->Ctx, 0x4302, &tmp, 1);
  }
  if (ret == OS04C10_OK)
  {
    ret = os04c10_write_reg(&pObj->Ctx, 0x4306, &tmp, 1);
  }
  if (ret == OS04C10_OK)
  {
    ret = os04c10_write_reg(&pObj->Ctx, 0x430A, &tmp, 1);
  }

  return ret;
}
/**
  * @brief  Enable/disable the OS04C10 color bar mode.
  * @param  pObj  pointer to component object
  * @param  Cmd   Enable disable colorbar
  * @retval Component status
  */
int32_t OS04C10_ColorbarModeConfig(OS04C10_Object_t *pObj, uint32_t Cmd)
{
  int32_t ret;
  uint8_t tmp = 0x40;

  if ((Cmd == COLORBAR_MODE_ENABLE) || (Cmd == COLORBAR_MODE_GRADUALV))
  {
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL4, &tmp, 1);
    if (ret == OS04C10_OK)
    {
      tmp = (Cmd == COLORBAR_MODE_GRADUALV ? 0x8c : 0x80);
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_PRE_ISP_TEST_SETTING1, &tmp, 1);
    }
    if (ret != OS04C10_OK)
    {
      ret = OS04C10_ERROR;
    }
  }
  else
  {
    tmp = 0x10;
    ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SDE_CTRL4, &tmp, 1);
    if (ret == OS04C10_OK)
    {
      tmp = 0x00;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_PRE_ISP_TEST_SETTING1, &tmp, 1);
    }
    if (ret != OS04C10_OK)
    {
      ret = OS04C10_ERROR;
    }
  }

  return ret;
}

/**
  * @brief  Set the camera pixel clock
  * @param  pObj  pointer to component object
  * @param  ClockValue Can be OS04C10_PCLK_48M, OS04C10_PCLK_24M, OS04C10_PCLK_12M, OS04C10_PCLK_9M
  *                    OS04C10_PCLK_8M, OS04C10_PCLK_7M
  * @retval Component status
  */
int32_t OS04C10_SetPCLK(OS04C10_Object_t *pObj, uint32_t ClockValue)
{
  int32_t ret;
  uint8_t tmp;

  switch (ClockValue)
  {
    case OS04C10_PCLK_7M:
      tmp = 0x38;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SC_PLL_CONTRL2, &tmp, 1);
      tmp = 0x16;
      ret += os04c10_write_reg(&pObj->Ctx, OS04C10_SC_PLL_CONTRL3, &tmp, 1);
      break;
    case OS04C10_PCLK_8M:
      tmp = 0x40;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SC_PLL_CONTRL2, &tmp, 1);
      tmp = 0x16;
      ret += os04c10_write_reg(&pObj->Ctx, OS04C10_SC_PLL_CONTRL3, &tmp, 1);
      break;
    case OS04C10_PCLK_9M:
      tmp = 0x60;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SC_PLL_CONTRL2, &tmp, 1);
      tmp = 0x18;
      ret += os04c10_write_reg(&pObj->Ctx, OS04C10_SC_PLL_CONTRL3, &tmp, 1);
      break;
    case OS04C10_PCLK_12M:
      tmp = 0x60;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SC_PLL_CONTRL2, &tmp, 1);
      tmp = 0x16;
      ret += os04c10_write_reg(&pObj->Ctx, OS04C10_SC_PLL_CONTRL3, &tmp, 1);
      break;
    case OS04C10_PCLK_48M:
      tmp = 0x60;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SC_PLL_CONTRL2, &tmp, 1);
      tmp = 0x03;
      ret += os04c10_write_reg(&pObj->Ctx, OS04C10_SC_PLL_CONTRL3, &tmp, 1);
      break;
    case OS04C10_PCLK_24M:
    default:
      tmp = 0x60;
      ret = os04c10_write_reg(&pObj->Ctx, OS04C10_SC_PLL_CONTRL2, &tmp, 1);
      tmp = 0x13;
      ret += os04c10_write_reg(&pObj->Ctx, OS04C10_SC_PLL_CONTRL3, &tmp, 1);
      break;
  }

  if (ret != OS04C10_OK)
  {
    ret = OS04C10_ERROR;
  }

  return ret;
}

/**
  * @brief  Enable DVP(Digital Video Port) Mode: Parallel Data Output
  * @param  pObj  pointer to component object
  * @retval Component status
  */
int OS04C10_EnableDVPMode(OS04C10_Object_t *pObj)
{
  uint32_t index;
  int32_t ret = OS04C10_OK;
  uint8_t tmp;


  static const uint16_t regs[10][2] =
  {
    /* Configure the IO Pad, output FREX/VSYNC/HREF/PCLK/D[9:2]/GPIO0/GPIO1 */
    {OS04C10_PAD_OUTPUT_ENABLE01, 0xFF},
    {OS04C10_PAD_OUTPUT_ENABLE02, 0xF3},
    {0x302e, 0x00},
    /* Unknown DVP control configuration */
    {0x471c, 0x50},
    {OS04C10_MIPI_CONTROL00, 0x58},
    /* Timing configuration */
    {OS04C10_SC_PLL_CONTRL0, 0x18},
    {OS04C10_SC_PLL_CONTRL1, 0x41},
    {OS04C10_SC_PLL_CONTRL2, 0x60},
    {OS04C10_SC_PLL_CONTRL3, 0x13},
    {OS04C10_SYSTEM_ROOT_DIVIDER, 0x01},
  };

  for(index=0; index < sizeof(regs) / 4U ; index++)
  {
    tmp = (uint8_t)regs[index][1];
    if(os04c10_write_reg(&pObj->Ctx, regs[index][0], &tmp, 1) != OS04C10_OK)
    {
      ret = OS04C10_ERROR;
      break;
    }
  }

  return ret;
}

/**
  * @brief  Enable MIPI (Mobile Industry Processor Interface) Mode: Serial port
  * @param  pObj  pointer to component object
  * @retval Component status
  */
int32_t OS04C10_EnableMIPIMode(OS04C10_Object_t *pObj)
{
  int32_t ret = OS04C10_OK;
  uint8_t tmp;
  uint32_t index;

  static const uint16_t regs[14][2] =
  {
    /* PAD settings */
    {OS04C10_PAD_OUTPUT_ENABLE01, 0},
    {OS04C10_PAD_OUTPUT_ENABLE02, 0},
    {0x302e, 0x08},
    /* Pixel clock period */
    {OS04C10_PCLK_PERIOD, 0x23},
    /* Timing configuration */
    {OS04C10_SC_PLL_CONTRL0, 0x18},
    {OS04C10_SC_PLL_CONTRL1, 0x12},
    {OS04C10_SC_PLL_CONTRL2, 0x30},
    {OS04C10_SC_PLL_CONTRL3, 0x13},
    {OS04C10_SYSTEM_ROOT_DIVIDER, 0x01},
    {0x4814, 0x2a},
    {OS04C10_MIPI_CTRL00, 0x24},
    {OS04C10_PAD_OUTPUT_VALUE00, 0x70},
    {OS04C10_MIPI_CONTROL00, 0x45},
    {OS04C10_FRAME_CTRL02, 0x00},
  };

  for(index=0; index < sizeof(regs) / 4U ; index++)
  {
    tmp = (uint8_t)regs[index][1];
    if(os04c10_write_reg(&pObj->Ctx, regs[index][0], &tmp, 1) != OS04C10_OK)
    {
      ret = OS04C10_ERROR;
      break;
    }
  }

  return ret;
}

/**
  * @brief  Set MIPI VirtualChannel
  * @param  pObj  pointer to component object
  * @param  vchannel virtual channel for Mipi Mode
  * @retval Component status
  */
int32_t OS04C10_SetMIPIVirtualChannel(OS04C10_Object_t *pObj, uint32_t vchannel)
{
  int32_t ret = OS04C10_OK;
  uint8_t tmp;

  if (os04c10_read_reg(&pObj->Ctx, 0x4814, &tmp, 1) != OS04C10_OK)
  {
    ret = OS04C10_ERROR;
  }
  else
  {
    tmp &= ~(3 << 6);
    tmp |= (vchannel << 6);
    if (os04c10_write_reg(&pObj->Ctx, 0x4814, &tmp, 1) != OS04C10_OK)
    {
      ret = OS04C10_ERROR;
    }
  }

  return ret;
}

/**
  * @brief  Start camera
  * @param  pObj  pointer to component object
  * @retval Component status
  */
int32_t OS04C10_Start(OS04C10_Object_t *pObj)
{
  uint8_t tmp;

  tmp = 0x1;
  return os04c10_write_reg(&pObj->Ctx, 0x0100, &tmp, 1);
  // return OS04C10_OK;
}

/**
  * @brief  Stop camera
  * @param  pObj  pointer to component object
  * @retval Component status
  */
int32_t OS04C10_Stop(OS04C10_Object_t *pObj)
{
  uint8_t tmp;

  tmp = 0x00;
  return os04c10_write_reg(&pObj->Ctx, 0x0100, &tmp, 1);
  // return OS04C10_OK;
}


/**
  * @}
  */

/** @defgroup OS04C10_Private_Functions Private Functions
  * @{
  */
/**
  * @brief This function provides accurate delay (in milliseconds)
  * @param pObj   pointer to component object
  * @param Delay  specifies the delay time length, in milliseconds
  * @retval OS04C10_OK
  */
static int32_t OS04C10_Delay(OS04C10_Object_t *pObj, uint32_t Delay)
{
  uint32_t tickstart;
  tickstart = pObj->IO.GetTick();
  while ((pObj->IO.GetTick() - tickstart) < Delay)
  {
    pObj->IO.Delay(1);
  }
  return OS04C10_OK;
}

/**
  * @brief  Wrap component ReadReg to Bus Read function
  * @param  handle  Component object handle
  * @param  Reg  The target register address to write
  * @param  pData  The target register value to be written
  * @param  Length  buffer size to be written
  * @retval error status
  */
static int32_t OS04C10_ReadRegWrap(void *handle, uint16_t Reg, uint8_t *pData, uint16_t Length)
{
  OS04C10_Object_t *pObj = (OS04C10_Object_t *)handle;

  return pObj->IO.ReadReg(pObj->IO.Address, Reg, pData, Length);
}

/**
  * @brief  Wrap component WriteReg to Bus Write function
  * @param  handle  Component object handle
  * @param  Reg  The target register address to write
  * @param  pData  The target register value to be written
  * @param  Length  buffer size to be written
  * @retval error status
  */
static int32_t OS04C10_WriteRegWrap(void *handle, uint16_t Reg, uint8_t *pData, uint16_t Length)
{
  OS04C10_Object_t *pObj = (OS04C10_Object_t *)handle;

  return pObj->IO.WriteReg(pObj->IO.Address, Reg, pData, Length);
}

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
