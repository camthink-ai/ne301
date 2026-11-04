
#ifndef LC_STATEBLOB_H
#define LC_STATEBLOB_H

#include <stdint.h>
#include "lc_bus_config.h"

#define LC_ST_BLOB_MAGIC        0x5341434Cu
#define LC_ST_BLOB_SCHEMA       1u
#define LC_ST_BLOB_SCHEMA_GEN   2u
#define LC_ST_BLOB_SCHEMA_ENABLE 3u
#define LC_ST_BLOB_SIZE         152u
#define LC_ST_BLOB_SIZE_GEN     160u
#define LC_ST_BLOB_SIZE_ENABLE  160u
#define LC_ST_BLOB_MAX          512u

#define LC_ST_ENABLE_ENABLED 1u
#define LC_ST_ENABLE_PAUSED  2u

uint32_t lc_st_encode(uint8_t *out, uint32_t cap, const lc_bus_config_t *cfg,
                      uint32_t total_in, uint32_t total_out,
                      uint32_t window_in, uint32_t window_out,
                      uint32_t report_seq);

uint32_t lc_st_encode_gen(uint8_t *out, uint32_t cap, const lc_bus_config_t *cfg,
                          uint32_t total_in, uint32_t total_out,
                          uint32_t window_in, uint32_t window_out,
                          uint32_t report_seq, uint32_t config_generation);

uint32_t lc_st_encode_enable(uint8_t *out, uint32_t cap, const lc_bus_config_t *cfg,
                             uint32_t total_in, uint32_t total_out,
                             uint32_t window_in, uint32_t window_out,
                             uint32_t report_seq, uint32_t config_generation,
                             uint32_t enable_state);

typedef enum {
    LC_ST_OK = 0,
    LC_ST_ERR_SIZE = 1,
    LC_ST_ERR_MAGIC = 2,
    LC_ST_ERR_SCHEMA = 3,
    LC_ST_ERR_CRC = 4,
    LC_ST_ERR_CONTENT = 5
} lc_st_status_t;

lc_st_status_t lc_st_decode(const uint8_t *in, uint32_t len,
                            lc_bus_config_t *cfg,
                            uint32_t *total_in, uint32_t *total_out,
                            uint32_t *window_in, uint32_t *window_out,
                            uint32_t *report_seq);

lc_st_status_t lc_st_decode_gen(const uint8_t *in, uint32_t len,
                                lc_bus_config_t *cfg,
                                uint32_t *total_in, uint32_t *total_out,
                                uint32_t *window_in, uint32_t *window_out,
                                uint32_t *report_seq,
                                uint32_t *config_generation);

lc_st_status_t lc_st_decode_enable(const uint8_t *in, uint32_t len,
                                   lc_bus_config_t *cfg,
                                   uint32_t *total_in, uint32_t *total_out,
                                   uint32_t *window_in, uint32_t *window_out,
                                   uint32_t *report_seq,
                                   uint32_t *config_generation,
                                   uint32_t *enable_state,
                                   uint32_t *source_schema);

#endif
