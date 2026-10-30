
#ifndef LC_CFGCODEC_H
#define LC_CFGCODEC_H

#include <stdint.h>
#include "lc_bus_config.h"

#define LC_CFG_MAGIC        0x3343434Cu
#define LC_CFG_SCHEMA       1u
#define LC_CFG_CAND_SIZE    144u

typedef enum {
    LC_CFG_OK = 0,
    LC_CFG_ERR_STATE_BLOB = 1,
    LC_CFG_ERR_SIZE = 2,
    LC_CFG_ERR_MAGIC = 3,
    LC_CFG_ERR_SCHEMA = 4,
    LC_CFG_ERR_FLAGS = 5,
    LC_CFG_ERR_NAME = 6,
    LC_CFG_ERR_CONTENT = 7
} lc_cfg_status_t;

uint32_t lc_cfg_encode(uint8_t *out, uint32_t cap, const lc_bus_config_t *cfg);

lc_cfg_status_t lc_cfg_decode(const uint8_t *in, uint32_t len, lc_bus_config_t *out);

const char *lc_cfg_status_name(lc_cfg_status_t st);

#endif
