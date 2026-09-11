#include "fsbl_app_common.h"

static common_flash_ops_t g_ops = {0};
static uint8_t g_ops_inited = 0;

int fsbl_app_common_init(common_flash_ops_t *flash_ops)
{
  if (g_ops_inited) return -1;
  if (flash_ops == NULL || flash_ops->flash_read == NULL || flash_ops->flash_write == NULL
      || (flash_ops->flash_erase == NULL) || (flash_ops->crc32 == NULL)) {
    return -2;
  }

  g_ops = *flash_ops;
  g_ops_inited = 1;
  return 0;
}

int fsbl_app_read_sys_clk_config(sys_clk_config_t *config)
{
  if (!g_ops_inited) return -1;
  if (config == NULL) return -2;
  
  if (g_ops.flash_read(SYS_CLK_CONFIG_SAVE_FLASH_BASE, config, sizeof(*config)) != 0) return -3;
  if (g_ops.crc32(config, (sizeof(*config) - sizeof(config->crc32))) != config->crc32) return -4;
  return 0;
}

int fsbl_app_write_sys_clk_config(sys_clk_config_t *config)
{
  if (!g_ops_inited) return -1;
  if (config == NULL) return -2;

  if (g_ops.flash_erase(SYS_CLK_CONFIG_SAVE_FLASH_BASE, SYS_CLK_CONFIG_SAVE_FLASH_SIZE) != 0) return -3;
  config->crc32 = g_ops.crc32(config, (sizeof(*config) - sizeof(config->crc32)));
  if (g_ops.flash_write(SYS_CLK_CONFIG_SAVE_FLASH_BASE, config, sizeof(*config)) != 0) return -4;
  return 0;
}

int fsbl_app_read_quick_snapshot_config(quick_snapshot_config_t *config)
{
  if (!g_ops_inited) return -1;
  if (config == NULL) return -2;

  if (g_ops.flash_read(QUICK_SNAPSHOT_CONFIG_SAVE_FLASH_BASE, config, sizeof(*config)) != 0) return -3;
  if (g_ops.crc32(config, (sizeof(*config) - sizeof(config->crc32))) != config->crc32) return -4;
  return 0;
}

int fsbl_app_write_quick_snapshot_config(quick_snapshot_config_t *config)
{
  if (!g_ops_inited) return -1;
  if (config == NULL) return -2;

  if (g_ops.flash_erase(QUICK_SNAPSHOT_CONFIG_SAVE_FLASH_BASE, QUICK_SNAPSHOT_CONFIG_SAVE_FLASH_SIZE) != 0) return -3;
  config->crc32 = g_ops.crc32(config, (sizeof(*config) - sizeof(config->crc32)));
  if (g_ops.flash_write(QUICK_SNAPSHOT_CONFIG_SAVE_FLASH_BASE, config, sizeof(*config)) != 0) return -4;
  return 0;
}

quick_snapshot_result_t *fsbl_app_get_quick_snapshot_result(void)
{
  quick_snapshot_result_t *result = (quick_snapshot_result_t *)QUICK_SNAPSHOT_RESULT_PSRAM_BASE;

  if (g_ops.crc32(result, (sizeof(*result) - sizeof(result->crc32))) != result->crc32) return NULL;
  return result;
}
