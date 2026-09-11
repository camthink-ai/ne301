#
# Copyright 2022-2023 Morse Micro
#
# SPDX-License-Identifier: Apache-2.0
#

# Configure the toolchain
# TOOLCHAIN_DIR := /home/tyson/projects/camthink/ne301/Prerequisites/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi
# TOOLCHAIN_BASE := $(TOOLCHAIN_DIR)/bin/arm-none-eabi-

TOOLCHAIN_BASE := arm-none-eabi-

CC := "$(TOOLCHAIN_BASE)gcc"
CXX := "$(TOOLCHAIN_BASE)g++"
AS := $(CC) -x assembler-with-cpp
OBJCOPY := "$(TOOLCHAIN_BASE)objcopy"
AR := "$(TOOLCHAIN_BASE)ar"
LD := "$(TOOLCHAIN_BASE)ld"

ARCH := armv8-m.main
BFDNAME := elf32-littlearm

TOOLCHAIN_INCLUDES := "$(TOOLCHAIN_DIR)/arm-none-eabi/include"
INCLUDES += $(TOOLCHAIN_INCLUDES)

CFLAGS += -mcpu=cortex-m55 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)"
CSPECS ?= -specs="nano.specs" -lc_nano

# Enable function sections and data sections so unused can be garbage collected
CFLAGS += -ffunction-sections -fdata-sections

# Disable RWX warnings for GNU Binutils 2.39 or later. This is done to keep the linker script
# compatible with older versions. The solution when using newer versions is to add the appropriate
# output section type attributes in the linker script,
# https://sourceware.org/binutils/docs-2.39/ld/Output-Section-Attributes.html.
LINKFLAGS += $(shell $(LD) --no-warn-rwx-segments -v > /dev/null 2>&1 && echo -Wl,--no-warn-rwx-segments)

# Garbage collect sections
LINKFLAGS += -Wl,--gc-sections
