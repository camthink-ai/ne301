#ifndef RCC_IC_AUTO_H
#define RCC_IC_AUTO_H

#include "stm32n6xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

void RCC_IC_FillDCMIPP_PLL3_IC17_IC18(RCC_PeriphCLKInitTypeDef *pcfg);
void RCC_IC_FillSPI2_PLL_IC8(RCC_PeriphCLKInitTypeDef *pcfg);
void RCC_IC_FillSPI4_PLL_IC9(RCC_PeriphCLKInitTypeDef *pcfg);
void RCC_IC_FillSPI6_PLL_IC9(RCC_PeriphCLKInitTypeDef *pcfg);

#ifdef __cplusplus
}
#endif

#endif /* RCC_IC_AUTO_H */
