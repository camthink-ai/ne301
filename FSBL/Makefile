######################################
# FSBL Makefile
######################################
# Inherits common configuration from root Makefile (via export)

######################################
# Helper Functions
######################################
rwildcard=$(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

######################################
# Target Configuration
######################################
TARGET = $(PROJECT_NAME)_FSBL

# Local build directory
BUILD_DIR = build

######################################
# Source Files
######################################
# C source files - Core (from .project)
C_SOURCES =
C_SOURCES += Core/Src/main.c
C_SOURCES += Core/Src/boot.c
C_SOURCES += Core/Src/adc.c
C_SOURCES += Core/Src/gpdma.c
C_SOURCES += Core/Src/gpio.c
C_SOURCES += Core/Src/stm32n6xx_hal_msp.c
C_SOURCES += Core/Src/stm32n6xx_it.c
C_SOURCES += Core/Src/sysmem.c
C_SOURCES += Core/Src/system_stm32n6xx_fsbl.c
C_SOURCES += Core/Src/tim.c
C_SOURCES += Core/Src/usart.c
C_SOURCES += Core/Src/iwdg.c
C_SOURCES += Core/Src/wwdg.c
C_SOURCES += Core/Src/xspim.c

# GCC support files (from .project linked resources)
C_SOURCES += ../Gcc/Src/console.c
C_SOURCES += ../Gcc/Src/fast_memcpy.c
C_SOURCES += ../Gcc/Src/syscalls.c

# Custom files (from .project)
C_SOURCES += ../Custom/Common/Utils/generic_math.c
C_SOURCES += ../Custom/Core/System/ota_header.c
C_SOURCES += ../Custom/Core/System/upgrade_manager.c

# HAL drivers (from .project)
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_adc.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_adc_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_bsec.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_cortex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dma.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_dma_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_exti.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_gpio.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_icache.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_iwdg.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pwr.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_pwr_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rcc.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_rcc_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_tim.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_tim_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_uart.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_uart_ex.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_wwdg.c
C_SOURCES += ../Drivers/STM32N6xx_HAL_Driver/Src/stm32n6xx_hal_xspi.c

# Assembly source files
ASM_SOURCES =
ASM_SOURCES += Core/Startup/startup_stm32n657l0hxq_fsbl.s

######################################
# C Compiler Defines (from .cproject lines 63-68)
######################################
C_DEFS = $(COMMON_DEFS)
C_DEFS += -DDEBUG
C_DEFS += -DUSE_HAL_DRIVER
C_DEFS += -DTX_INCLUDE_USER_DEFINE_FILE
C_DEFS += -DTX_SINGLE_MODE_SECURE=1
C_DEFS += -DBOOT_IN_PSRAM

######################################
# Assembly Defines (from .cproject lines 36-40)
######################################
AS_DEFS =
AS_DEFS += -DDEBUG
AS_DEFS += -DTX_INCLUDE_USER_DEFINE_FILE
AS_DEFS += -DTX_SINGLE_MODE_SECURE=1
AS_DEFS += -DTX_LOW_POWER

######################################
# Assembly Include Paths (from .cproject lines 42-48)
######################################
AS_INCLUDES =
AS_INCLUDES += -ICore/Inc
AS_INCLUDES += -I../Drivers/STM32N6xx_HAL_Driver/Inc
AS_INCLUDES += -I../Drivers/CMSIS/Device/ST/STM32N6xx/Include
AS_INCLUDES += -I../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy
AS_INCLUDES += -I../Middlewares/ST/threadx/common/inc
AS_INCLUDES += -I../Middlewares/ST/threadx/ports/cortex_M55/gnu/inc
AS_INCLUDES += -I../Drivers/CMSIS/Include

######################################
# C Include Paths (from .cproject lines 70-80)
######################################
C_INCLUDES =
C_INCLUDES += -ICore/Inc
C_INCLUDES += -I../Drivers/STM32N6xx_HAL_Driver/Inc
C_INCLUDES += -I../Drivers/CMSIS/Device/ST/STM32N6xx/Include
C_INCLUDES += -I../Drivers/STM32N6xx_HAL_Driver/Inc/Legacy
C_INCLUDES += -I../Middlewares/ST/threadx/common/inc
C_INCLUDES += -I../Middlewares/ST/threadx/ports/cortex_M55/gnu/inc
C_INCLUDES += -I../Drivers/CMSIS/Include
C_INCLUDES += -I../Custom/Common/Inc
C_INCLUDES += -I../Custom/Core/System
C_INCLUDES += -I../Custom/Common/Utils

######################################
# Compiler and Linker Flags
######################################
# C compiler flags (inherits COMMON_CFLAGS)
CFLAGS = $(COMMON_CFLAGS) $(C_DEFS) $(C_INCLUDES)
# Generate dependency information
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"
# CMSE support for FSBL
CFLAGS += -mcmse

# Assembler flags (inherits COMMON_ASFLAGS)
ASFLAGS = $(COMMON_ASFLAGS) $(AS_DEFS) $(AS_INCLUDES)
# CMSE support for FSBL
ASFLAGS += -mcmse

######################################
# Linker Configuration
######################################
# Linker script
LDSCRIPT = STM32N657L0HXQ_AXISRAM2_fsbl.ld

# Libraries
LIBS = -lc -lm -lnosys
LIBDIR =

# Linker flags (inherits COMMON_LDFLAGS from root)
LDFLAGS = $(COMMON_LDFLAGS) -T$(LDSCRIPT)
LDFLAGS += $(LIBDIR)
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref
# CMSE support
LDFLAGS += -mcmse
# Link libraries
LDFLAGS += -Wl,--start-group $(LIBS) -Wl,--end-group

######################################
# Build Targets
######################################
.PHONY: all
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin

######################################
# Build Rules
######################################
# Object files (filter out ../ from paths)
OBJECTS = $(addprefix $(BUILD_DIR)/, $(subst ../,,$(C_SOURCES:.c=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/, $(subst ../,,$(ASM_SOURCES:.s=.o)))

# Pre-create all build directories (avoid race conditions in parallel builds)
.PHONY: build-dirs
build-dirs: $(BUILD_DIR)
	@mkdir -p $(sort $(dir $(OBJECTS)))

# Make all objects depend on directories
$(OBJECTS): | build-dirs

# Compile C source files
$(BUILD_DIR)/%.o: %.c Makefile
	@echo "CC $<"
	@$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: ../%.c Makefile
	@echo "CC $<"
	@$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

# Compile assembly source files (.s)
$(BUILD_DIR)/%.o: %.s Makefile
	@echo "AS $<"
	@$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.o: ../%.s Makefile
	@echo "AS $<"
	@$(AS) -c $(ASFLAGS) $< -o $@

# Object list file
$(BUILD_DIR)/$(TARGET).list: $(OBJECTS)
	@echo "Generating object list..."
	@$(file > $@,$(OBJECTS))

# Link ELF file
$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile $(BUILD_DIR)/$(TARGET).list
	@echo "LD $@"
	@$(CC) @$(BUILD_DIR)/$(TARGET).list $(LDFLAGS) -o $@
	@$(SZ) $@

# Generate BIN file
$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	@echo "BIN $@"
	@$(BIN) $< $@

# Create build directory
$(BUILD_DIR):
	@mkdir -p $@

######################################
# Clean
######################################
.PHONY: clean
clean:
	@echo "Cleaning FSBL build files..."
	@rm -rf $(BUILD_DIR)

######################################
# Dependencies
######################################
-include $(call rwildcard,$(BUILD_DIR),*.d)

######################################
# Debug Information
######################################
.PHONY: info
info:
	@echo "FSBL Makefile Configuration:"
	@echo "  TARGET      = $(TARGET)"
	@echo "  BUILD_DIR   = $(BUILD_DIR)"
	@echo "  C_SOURCES   = $(words $(C_SOURCES)) files"
	@echo "  ASM_SOURCES = $(words $(ASM_SOURCES)) files"
	@echo "  LDSCRIPT    = $(LDSCRIPT)"
