# =============================================================================
# stedgeai.mk - ST Edge AI / NPU runtime variant selection
# =============================================================================
# Select toolchain + vendored runtime at build time. Application code is shared;
# firmware must be built per variant (NPU runtime is statically linked).
#
# Usage:
#   make all                        # default STEDGEAI_VARIANT=4.0
#   make all STEDGEAI_VARIANT=4.0
#   make app STEDGEAI_VARIANT=2.2
#
# Model packages must be generated with the matching stedgeai tool chain.
# Set STEDGEAI_CORE_DIR to the corresponding ST Edge AI installation.
# =============================================================================

STEDGEAI_VARIANT ?= 4.0

ifeq ($(STEDGEAI_VARIANT),2.2)
  STEDGEAI_LIB_DIR      := Middlewares/ST/stedgeai-lib-2.2
  STEDGEAI_NPU_DEVICE   := STM32N6XX
  STEDGEAI_BIT          := 2
  NETWORK_RUNTIME_LIB   := NetworkRuntime1020_CM55_GCC.a
  MODEL_STEDGEAI_VERSION := v2.2
  STEDGEAI_EXTRA_SOURCES := \
    Npu/ll_aton/ecloader.c \
    Npu/ll_aton/ll_aton_osal_zephyr.c
else ifeq ($(STEDGEAI_VARIANT),4.0)
  STEDGEAI_LIB_DIR      := Middlewares/ST/stedgeai-lib-4.0
  STEDGEAI_NPU_DEVICE   := STM32N6xx
  STEDGEAI_BIT          := 4
  NETWORK_RUNTIME_LIB   := NetworkRuntime1200_CM55_GCC.a
  MODEL_STEDGEAI_VERSION := v4.0.1
  STEDGEAI_EXTRA_SOURCES := \
    Npu/ll_aton/ll_aton_stai_internal.c
else ifeq ($(STEDGEAI_VARIANT),3.0)
  STEDGEAI_LIB_DIR      := Middlewares/ST/stedgeai-lib-3.0
  STEDGEAI_NPU_DEVICE   := STM32N6xx
  STEDGEAI_BIT          := 3
  NETWORK_RUNTIME_LIB   := NetworkRuntime1100_CM55_GCC.a
  MODEL_STEDGEAI_VERSION := v3.0.0
  STEDGEAI_EXTRA_SOURCES :=
else
  $(error Unsupported STEDGEAI_VARIANT=$(STEDGEAI_VARIANT). Use 2.2, 3.0, or 4.0.)
endif

ifndef MODEL_VERSION
  MODEL_VERSION := $(STEDGEAI_BIT).$(MODEL_VERSION_OVERRIDE)
endif

STEDGEAI_LIB_ROOT := ../$(STEDGEAI_LIB_DIR)

STEDGEAI_C_SOURCES := \
  Npu/Devices/$(STEDGEAI_NPU_DEVICE)/mcu_cache.c \
  Npu/Devices/$(STEDGEAI_NPU_DEVICE)/npu_cache.c \
  Npu/ll_aton/ll_aton.c \
  Npu/ll_aton/ll_aton_cipher.c \
  Npu/ll_aton/ll_aton_util.c \
  Npu/ll_aton/ll_aton_dbgtrc.c \
  Npu/ll_aton/ll_aton_debug.c \
  Npu/ll_aton/ll_aton_lib.c \
  Npu/ll_aton/ll_aton_lib_sw_operators.c \
  Npu/ll_aton/ll_aton_osal_freertos.c \
  Npu/ll_aton/ll_aton_osal_threadx.c \
  Npu/ll_aton/ll_aton_profiler.c \
  Npu/ll_aton/ll_aton_reloc_network.c \
  Npu/ll_aton/ll_aton_rt_main.c \
  Npu/ll_aton/ll_aton_runtime.c \
  Npu/ll_aton/ll_sw_float.c \
  Npu/ll_aton/ll_sw_integer.c \
  $(STEDGEAI_EXTRA_SOURCES)

STEDGEAI_C_SOURCE_PATHS := $(addprefix $(STEDGEAI_LIB_ROOT)/,$(STEDGEAI_C_SOURCES))
STEDGEAI_LIBDIR := $(STEDGEAI_LIB_ROOT)/Lib/GCC/ARMCortexM55

export STEDGEAI_VARIANT STEDGEAI_LIB_DIR STEDGEAI_NPU_DEVICE STEDGEAI_BIT
export NETWORK_RUNTIME_LIB MODEL_STEDGEAI_VERSION MODEL_VERSION
export STEDGEAI_LIB_ROOT STEDGEAI_C_SOURCE_PATHS STEDGEAI_LIBDIR
