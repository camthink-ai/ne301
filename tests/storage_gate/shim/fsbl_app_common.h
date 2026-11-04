#ifndef SHIM_FSBL_APP_COMMON_H
#define SHIM_FSBL_APP_COMMON_H
#include <stdint.h>
#define FSBL_APP_SYSCLK_PROFILE_HSE_200MHZ 1U
#define FSBL_APP_SYSCLK_PROFILE_HSE_400MHZ 2U
#define FSBL_APP_SYSCLK_PROFILE_HSI_800MHZ 3U
#define FSBL_APP_SYSCLK_PROFILE_HSE_800MHZ 4U
typedef struct {
    uint32_t sys_clk_profile;
} sys_clk_config_t;
int fsbl_app_read_sys_clk_config(sys_clk_config_t *config);
int fsbl_app_write_sys_clk_config(sys_clk_config_t *config);
#endif
