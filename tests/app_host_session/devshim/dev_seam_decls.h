#ifndef DEVSHIM_DEV_SEAM_DECLS_H
#define DEVSHIM_DEV_SEAM_DECLS_H

#include "aicam_types.h"

aicam_bool_t json_config_get_ai_1_active(void);
aicam_result_t json_config_sync_ai_pipe_nvs_from_input_size(uint32_t input_width,
                                                            uint32_t input_height);

#endif
