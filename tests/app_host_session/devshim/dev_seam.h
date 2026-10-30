#ifndef DEVSHIM_DEV_SEAM_H
#define DEVSHIM_DEV_SEAM_H

#include <stdint.h>
#include "video_ai_node.h"

void dev_seam_reset(void);
nn_model_info_t *dev_seam_model_info(void);
void dev_seam_set_state(nn_state_t state);
void dev_seam_set_info_rc(int rc);
void dev_seam_set_load_rc(int rc);
void dev_seam_set_unload_rc(int rc);
uint32_t dev_seam_load_calls(void);
uintptr_t dev_seam_last_load_ptr(void);
uint32_t dev_seam_active_switch_calls(void);
video_node_t *dev_seam_ai_node(void);

#endif
