/**
 ******************************************************************************
 * @file    aiTestHelper.h
 * @author  MCD/AIS Team
 * @brief   Helper functions for AI test application (ST AI API)
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2019,2021 STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software is licensed under terms that can be found in the LICENSE file in
 * the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

#ifndef __AI_TEST_HELPER_ST_AI_H__
#define __AI_TEST_HELPER_ST_AI_H__

#if !defined(TFLM_RUNTIME) && !defined(NO_STM_AI_RUNTIME)

#ifdef __cplusplus
extern "C" {
#endif

#include <stai.h>

typedef struct {
  uint16_t                n_tensor;
  const stai_tensor       *tensors;
  uintptr_t               *address;
} tensor_descs;

void stai_platform_version(void);
void stai_log_network_info(const stai_network_info* report,
    const tensor_descs *ins,
    const tensor_descs *outs,
    const tensor_descs *acts,
    const tensor_descs *states,
    const tensor_descs *params);
void stai_log_err(const stai_return_code err, const char *fct);

#ifdef __cplusplus
}
#endif

#endif /* !TFLM_RUNTIME */

#endif /* __AI_TEST_HELPER_ST_AI_H__ */
