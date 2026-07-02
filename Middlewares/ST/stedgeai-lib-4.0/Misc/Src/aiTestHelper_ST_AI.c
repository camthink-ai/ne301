/**
 ******************************************************************************
 * @file    aiTestUtility_ST_AI.c
 * @author  MCD/AIS Team
 * @brief   Helper functions for AI test application (ST AI API)
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2023 STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software is licensed under terms that can be found in the LICENSE file in
 * the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

/*
 * Description:
 *  Basic helper functions for AI test applications (embedded API - ST AI)
 *
 * History:
 *  - v1.0 - Initial version
 */

#include <stdio.h>
#include <string.h>

#if !defined(TFLM_RUNTIME) && !defined(NO_STM_AI_RUNTIME)

#include <aiTestHelper_ST_AI.h>
#include <aiTestUtility.h>
#include <stai.h>


void stai_platform_version(void)
{
  stai_runtime_info netrt_info;
  stai_runtime_get_info(&netrt_info);

  LC_PRINT("\r\n");
  LC_PRINT("ST.AI RT\r\n");
  LC_PRINT("--------------------------------------------------\r\n");
  LC_PRINT(" tools version   : v%d.%d.%d\r\n", (int)netrt_info.tools_version.major,
      (int)netrt_info.tools_version.minor,
      (int)netrt_info.tools_version.micro);
  LC_PRINT(" network rt lib  : v%d.%d.%d-%x\r\n", (int)netrt_info.runtime_version.major,
      (int)netrt_info.runtime_version.minor,
      (int)netrt_info.runtime_version.micro,
      (int)netrt_info.runtime_build);
  LC_PRINT("   compiled with : %s\r\n", netrt_info.compiler_desc);
  LC_PRINT("\r\n");
}


void stai_log_err(const stai_return_code err, const char *fct)
{
  if (fct)
    LC_PRINT("E: AI error (%s) - code=0x%08x\r\n", fct, err);
  else
    LC_PRINT("E: AI error - code=0x%08x\r\n", err);
}


static void stai_log_data_type(const stai_format fmt)
{
  stai_format_type type_ = STAI_FORMAT_GET_TYPE(fmt);
  int nb_bits =  (int)STAI_FORMAT_GET_BITS(fmt);

  if (type_ == STAI_FORMAT_TYPE_FLOAT)
    LC_PRINT("float%d", nb_bits);
  else if (type_ == STAI_FORMAT_TYPE_BOOL) {
    LC_PRINT("bool%d", nb_bits);
  } else { /* integer type */
    LC_PRINT("%s%d", STAI_FORMAT_GET_SIGN(fmt)?"i":"u", nb_bits);
    if (nb_bits < 8)
      LC_PRINT(" int32-%db encoding", nb_bits);
    else
      LC_PRINT("(%d.%d)", STAI_FORMAT_GET_IBITS(fmt),
          STAI_FORMAT_GET_FBITS(fmt));
  }
}


void stai_log_tensor(const stai_tensor *tensor)
{

  if (tensor->name)
    LC_PRINT(" name=%s", tensor->name);

  LC_PRINT(" (");
  for (uint32_t i=0; i<tensor->shape.size; i++) {
    LC_PRINT("%ld", tensor->shape.data[i]);
    if (i != tensor->shape.size - 1)
      LC_PRINT(",");
    else
      LC_PRINT(")");
  }

  LC_PRINT(" ");
  stai_log_data_type(tensor->format);
  LC_PRINT(" (fmt=0x%08X)", tensor->format);
  // JMD
  // LC_PRINT(" @0x%08X/%d", tensor->address, tensor->size_bytes);
  LC_PRINT(" @0xNULL/%ld", tensor->size_bytes);

  if (tensor->scale.size) {
    LC_PRINT("\n\r       ");
    LC_PRINT(" scale=%f", (double)tensor->scale.data[0]);
    LC_PRINT(" zeropoint=%d", tensor->zeropoint.data[0]);
  }

}


void stai_log_io_tensor(const stai_tensor *tensor, uintptr_t address)
{
  if (tensor->name)
    LC_PRINT(" name=%s", tensor->name);

  LC_PRINT(" (");
  for (uint32_t i=0; i<tensor->shape.size; i++) {
    LC_PRINT("%ld", tensor->shape.data[i]);
    if (i != tensor->shape.size - 1)
      LC_PRINT(",");
    else
      LC_PRINT(")");
  }

  LC_PRINT(" ");
  stai_log_data_type(tensor->format);
  LC_PRINT(" (fmt=0x%08X)", tensor->format);
  LC_PRINT(" @0x%08X/%ld", address, tensor->size_bytes);

  if (tensor->scale.size) {
    LC_PRINT("\n\r       ");
    LC_PRINT(" scale=%f", (double)tensor->scale.data[0]);
    LC_PRINT(" zeropoint=%d", tensor->zeropoint.data[0]);
  }
}


static void _stai_log_tensor_data(const stai_tensor *tensor, uintptr_t address)
{
  LC_PRINT(" ");
  stai_log_data_type(tensor->format);
  LC_PRINT(" @0x%08X/%ld", address, tensor->size_bytes);
}


void stai_log_network_info(const stai_network_info* report,
    const tensor_descs *ins,
    const tensor_descs *outs,
    const tensor_descs *acts,
    const tensor_descs *states,
    const tensor_descs *params)
{
  LC_PRINT("Network informations...\r\n");
  LC_PRINT(" model signature : %s\r\n", report->model_signature);
  LC_PRINT(" c-name          : %s\r\n", report->c_model_name);
  LC_PRINT(" c-signature     : %lu\r\n", report->c_model_signature);
  LC_PRINT(" c-datetime      : %s\r\n", report->c_model_datetime);
  LC_PRINT(" c-compile-time  : %s\r\n", report->c_compile_datetime);
  LC_PRINT(" macc            : %s\r\n", uint64ToDecimal(report->n_macc));

  LC_PRINT(" runtime version : %d.%d.%d\r\n",
      report->runtime_version.major,
      report->runtime_version.minor,
      report->runtime_version.micro);

  if (report->flags & STAI_FLAG_INPUTS)
    LC_PRINT(" n_inputs : %u (allocate-inputs)\r\n", report->n_inputs);
  else
    LC_PRINT(" n_inputs : %u\r\n", report->n_inputs);
  for (int i=0; i<report->n_inputs && i<ins->n_tensor ; i++) {
    LC_PRINT("  i[%d] ", i);
    stai_log_io_tensor(&ins->tensors[i], ins->address[i]);
    LC_PRINT("\r\n");
  }

  if (report->flags & STAI_FLAG_OUTPUTS)
    LC_PRINT(" n_outputs : %u (allocate-outputs)\r\n", report->n_outputs);
  else
    LC_PRINT(" n_outputs : %u\r\n", report->n_outputs);
  for (int i=0; i<report->n_outputs && outs->n_tensor; i++) {
    LC_PRINT("  o[%d] ", i);
    stai_log_io_tensor(&outs->tensors[i], outs->address[i]);
    LC_PRINT("\r\n");
  }

  if (report->flags & STAI_FLAG_ACTIVATIONS)
    LC_PRINT(" n_activations : %u (allocate-activations)\r\n", report->n_activations);
  else
    LC_PRINT(" n_activations : %u\r\n", report->n_activations);
  for (int i=0; i<report->n_activations && acts->n_tensor; i++) {
    LC_PRINT("  a[%d] ", i);
    _stai_log_tensor_data(&report->activations[i], acts->address[i]);
    LC_PRINT("\r\n");
  }

  if (report->flags & STAI_FLAG_STATES)
    LC_PRINT(" n_states : %u (allocate-states)\r\n", report->n_states);
  else
    LC_PRINT(" n_states : %u\r\n", report->n_states);
  for (int i=0; i<report->n_states && acts->n_tensor; i++) {
    LC_PRINT("  s[%d] ", i);
    _stai_log_tensor_data(&report->states[i], states->address[i]);
    LC_PRINT("\r\n");
  }

  if (report->flags & STAI_FLAG_WEIGHTS)
    LC_PRINT(" n_weights : %u (allocate-weights)\r\n", report->n_weights);
  else
    LC_PRINT(" n_weights : %u\r\n", report->n_weights);
  for (int i=0; i<report->n_weights; i++) {
    LC_PRINT("  w[%d] ", i);
    _stai_log_tensor_data(&report->weights[i], params->address[i]);
    LC_PRINT("\r\n");
  }
}

#endif /* !TFLM_RUNTIME) */
