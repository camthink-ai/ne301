#ifndef SHIM_STM32N6XX_HAL_H
#define SHIM_STM32N6XX_HAL_H

#include <stdint.h>
#include <stddef.h>

#define HAL_OK 0u
#define HASH ((void *)0x40000000UL)
#define HASH_ALGOSELECTION_SHA256 0x00000000u
#define HASH_ALGOSELECTION_SHA224 0x00000001u
#define HASH_BYTE_SWAP 0x00000002u

typedef struct {
    uint32_t Algorithm;
    uint32_t DataType;
} HASH_InitTypeDef;

typedef struct {
    void *Instance;
    HASH_InitTypeDef Init;
    uint32_t state[8];
    uint8_t block[64];
    uint32_t block_len;
    uint64_t total_len;
} HASH_HandleTypeDef;

int HAL_HASH_Init(HASH_HandleTypeDef *hhash);
int HAL_HASH_DeInit(HASH_HandleTypeDef *hhash);
int HAL_HASH_Suspend(HASH_HandleTypeDef *hhash, uint8_t *regs);
int HAL_HASH_Resume(HASH_HandleTypeDef *hhash, const uint8_t *regs);
int HAL_HASH_Accumulate(HASH_HandleTypeDef *hhash, uint8_t *data, size_t len, uint32_t timeout);
int HAL_HASH_AccumulateLast(HASH_HandleTypeDef *hhash, uint8_t *data, size_t len, uint8_t *out, uint32_t timeout);
void __HAL_RCC_HASH_CLK_ENABLE(void);
void __disable_irq(void);
void __enable_irq(void);
void hal_hash_shim_arm_fail(void);
void hal_hash_shim_disarm_fail(void);

#endif
