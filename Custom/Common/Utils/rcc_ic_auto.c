#include "rcc_ic_auto.h"
#include "stdio.h"

/* IC output divider range per IS_RCC_ICCLKDIVIDER (stm32n6xx_hal_rcc.h) */
#define RCC_IC_DIV_MIN  1U
#define RCC_IC_DIV_MAX  256U

/* Legacy targets from CPU_CLK_USE_* tables (camera.c): ~300 MHz DCMIPP tap, ~20 MHz CSI tap */
#define RCC_IC_DCMIPP_IC17_TARGET_HZ  300000000UL
#define RCC_IC_DCMIPP_IC18_TARGET_HZ   20000000UL

/* SPI kernel clock targets (spi.c): SPI2 IC8 → 80 MHz from PLL1 */
#define RCC_IC_SPI2_PLL1_TARGET_HZ      80000000UL
#define RCC_IC_SPI4_PLL1_TARGET_HZ     100000000UL
#define RCC_IC_SPI6_PLL1_TARGET_HZ     RCC_IC_SPI4_PLL1_TARGET_HZ

static uint32_t rcc_ic_best_div(uint64_t pll_hz, uint32_t target_hz)
{
  uint32_t best_d = RCC_IC_DIV_MIN;
  uint64_t best_err = (uint64_t)-1;

  if (pll_hz < 1000ULL || target_hz < 1000U)
    return RCC_IC_DIV_MIN;

  for (uint32_t d = RCC_IC_DIV_MIN; d <= RCC_IC_DIV_MAX; d++)
  {
    uint64_t out = pll_hz / (uint64_t)d;
    uint64_t err = (out > (uint64_t)target_hz) ? (out - (uint64_t)target_hz)
                                                 : ((uint64_t)target_hz - out);
    if (err < best_err)
    {
      best_err = err;
      best_d = d;
    }
  }
  return best_d;
}

void RCC_IC_FillDCMIPP_PLL3_IC17_IC18(RCC_PeriphCLKInitTypeDef *pcfg)
{
  uint32_t pll3 = HAL_RCCEx_GetPLL3CLKFreq();
  uint32_t div17;
  uint32_t div18;

  if (pll3 == 0U || pll3 == RCC_PERIPH_FREQUENCY_NO)
  {
    div17 = 3U;
    div18 = 45U;
  }
  else
  {
    div17 = rcc_ic_best_div((uint64_t)pll3, RCC_IC_DCMIPP_IC17_TARGET_HZ);
    div18 = rcc_ic_best_div((uint64_t)pll3, RCC_IC_DCMIPP_IC18_TARGET_HZ);
  }

  pcfg->PeriphClockSelection |= (RCC_PERIPHCLK_DCMIPP | RCC_PERIPHCLK_CSI);
  pcfg->DcmippClockSelection = RCC_DCMIPPCLKSOURCE_IC17;
  pcfg->ICSelection[RCC_IC17].ClockSelection = RCC_ICCLKSOURCE_PLL3;
  pcfg->ICSelection[RCC_IC17].ClockDivider = div17;
  pcfg->ICSelection[RCC_IC18].ClockSelection = RCC_ICCLKSOURCE_PLL3;
  pcfg->ICSelection[RCC_IC18].ClockDivider = div18;
}

void RCC_IC_FillSPI2_PLL_IC8(RCC_PeriphCLKInitTypeDef *pcfg)
{
  uint32_t pll1 = HAL_RCCEx_GetPLL1CLKFreq();
  uint32_t div = 10U;

  if (pll1 != 0U && pll1 != RCC_PERIPH_FREQUENCY_NO)
    div = rcc_ic_best_div((uint64_t)pll1, RCC_IC_SPI2_PLL1_TARGET_HZ);

  pcfg->PeriphClockSelection |= RCC_PERIPHCLK_SPI2;
  pcfg->Spi2ClockSelection = RCC_SPI2CLKSOURCE_IC8;
  pcfg->ICSelection[RCC_IC8].ClockSelection = RCC_ICCLKSOURCE_PLL1;
  pcfg->ICSelection[RCC_IC8].ClockDivider = div;
}

void RCC_IC_FillSPI4_PLL_IC9(RCC_PeriphCLKInitTypeDef *pcfg)
{
  uint32_t pll1 = HAL_RCCEx_GetPLL1CLKFreq();
  uint32_t div = 8U;

  if (pll1 != 0U && pll1 != RCC_PERIPH_FREQUENCY_NO)
    div = rcc_ic_best_div((uint64_t)pll1, RCC_IC_SPI4_PLL1_TARGET_HZ);

  pcfg->PeriphClockSelection |= RCC_PERIPHCLK_SPI4;
  pcfg->Spi4ClockSelection = RCC_SPI4CLKSOURCE_IC9;
  pcfg->ICSelection[RCC_IC9].ClockSelection = RCC_ICCLKSOURCE_PLL1;
  pcfg->ICSelection[RCC_IC9].ClockDivider = div;
}

void RCC_IC_FillSPI6_PLL_IC9(RCC_PeriphCLKInitTypeDef *pcfg)
{
  uint32_t pll1 = HAL_RCCEx_GetPLL1CLKFreq();
  uint32_t div = 8U;

  if (pll1 != 0U && pll1 != RCC_PERIPH_FREQUENCY_NO)
    div = rcc_ic_best_div((uint64_t)pll1, RCC_IC_SPI6_PLL1_TARGET_HZ);

  pcfg->PeriphClockSelection |= RCC_PERIPHCLK_SPI6;
  pcfg->Spi6ClockSelection = RCC_SPI6CLKSOURCE_IC9;
  pcfg->ICSelection[RCC_IC9].ClockSelection = RCC_ICCLKSOURCE_PLL1;
  pcfg->ICSelection[RCC_IC9].ClockDivider = div;
}