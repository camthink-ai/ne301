######################################
# Root Makefile - NE301 Project Build System
######################################

# Project configuration
PROJECT_NAME = ne301

# ==============================================
# Version Management (from version.mk)
# ==============================================
include version.mk
include stedgeai.mk

# Directories
BUILD_DIR = build
PKG_SCRIPT_DIR = Script

# Sub-projects
PROJECTS = FSBL Appli Web Model WakeCore
FSBL_DIR = FSBL
APP_DIR = Appli
WEB_DIR = Web
MODEL_DIR = Model
WAKECORE_DIR = WakeCore

# Output file names
FSBL_NAME = $(PROJECT_NAME)_FSBL
APP_NAME = $(PROJECT_NAME)_App
WEB_NAME = $(PROJECT_NAME)_Web
MODEL_NAME = $(PROJECT_NAME)_Model
U0_NAME = $(PROJECT_NAME)_WakeCore

# Flash addresses
FLASH_ADDR_FSBL = 0x70000000
FLASH_ADDR_APP = 0x70100000
FLASH_ADDR_WEB = 0x70400000
FLASH_ADDR_MODEL = 0x70900000
FLASH_ADDR_WAKECORE = 0x8000000

# Flash partition addresses and sizes (from mem_map.h)
FLASH_BASE_ADDR = 0x70000000
FLASH_SECTOR_SIZE = 0x10000
FLASH_ADDR_NVS_BASE = 0x70080000
FLASH_ADDR_NVS_END = 0x7008FFFF
FLASH_ADDR_OTA_BASE = 0x70090000
FLASH_ADDR_OTA_END = 0x70091FFF
FLASH_ADDR_APP1_BASE = 0x70100000
FLASH_ADDR_APP1_END = 0x704FFFFF
FLASH_ADDR_APP2_BASE = 0x70500000
FLASH_ADDR_APP2_END = 0x708FFFFF
FLASH_ADDR_AI_DEFAULT_BASE = 0x70900000
FLASH_ADDR_AI_DEFAULT_END = 0x70DFFFFF
FLASH_ADDR_AI_1_BASE = 0x70E00000
FLASH_ADDR_AI_1_END = 0x712FFFFF
FLASH_ADDR_AI_2_BASE = 0x71300000
FLASH_ADDR_AI_2_END = 0x717FFFFF
FLASH_ADDR_AI_3_BASE = 0x71800000
FLASH_ADDR_AI_3_END = 0x71FFFFFF
FLASH_ADDR_LITTLEFS_BASE = 0x72000000
FLASH_ADDR_LITTLEFS_END = 0x75FFFFFF
FLASH_ADDR_WIFI_FW_BASE = 0x77C00000
FLASH_ADDR_WIFI_FW_END = 0x77FFFFFF


# Parallel build (auto-detect CPU cores)
# MAKEFLAGS += -j$(shell nproc 2>/dev/null || echo 4)
SUBMAKE_J = -j$(shell nproc 2>/dev/null || echo 4)
######################################
# Toolchain Configuration
######################################
PREFIX = arm-none-eabi-

ifdef GCC_PATH
CC = $(GCC_PATH)/$(PREFIX)gcc
AS = $(GCC_PATH)/$(PREFIX)gcc -x assembler-with-cpp
CP = $(GCC_PATH)/$(PREFIX)objcopy
SZ = $(GCC_PATH)/$(PREFIX)size
READELF = $(GCC_PATH)/$(PREFIX)readelf
else
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size
READELF = $(PREFIX)readelf
endif

HEX = $(CP) -O ihex
BIN = $(CP) -O binary

# STM32 tools
PACKER = python $(PKG_SCRIPT_DIR)/ota_packer.py
FLASHER = STM32_Programmer_CLI
SIGNER = STM32_SigningTool_CLI
EL = "$(shell dirname "$(shell which $(FLASHER))" 2>/dev/null)/ExternalLoader/MX66UW1G45G_STM32N6570-DK.stldr"

######################################
# Common Compiler Configuration
######################################
CPU = -mcpu=cortex-m55
MCU_FLAGS = $(CPU) -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
OPT = -g3

COMMON_CFLAGS = $(MCU_FLAGS) $(OPT) -Wall -Werror -fdata-sections -ffunction-sections -fstack-usage -std=gnu11
COMMON_ASFLAGS = $(MCU_FLAGS) $(OPT) -Wall -Werror -fdata-sections -ffunction-sections
COMMON_LDFLAGS = $(MCU_FLAGS) -specs=nano.specs -Wl,--gc-sections -Wl,--no-warn-rwx-segments -Wl,--print-memory-usage -u _printf_float
COMMON_DEFS = -DSTM32N657xx -DUSE_FULL_LL_DRIVER -DUSE_DCACHE -DPWR_USE_3V3 -DBOARD_PSRAM_SIZE=64

# Export to sub-Makefiles
export CC AS CP SZ READELF HEX BIN MCU_FLAGS OPT
export COMMON_CFLAGS COMMON_ASFLAGS COMMON_LDFLAGS COMMON_DEFS
export BUILD_DIR PROJECT_NAME

# Export version info to sub-Makefiles
export VERSION VERSION_MAJOR VERSION_MINOR VERSION_PATCH VERSION_BUILD VERSION_SUFFIX
export FSBL_VERSION APP_VERSION WEB_VERSION MODEL_VERSION WAKECORE_VERSION
export FSBL_EFFECTIVE_SUFFIX APP_EFFECTIVE_SUFFIX WEB_EFFECTIVE_SUFFIX MODEL_EFFECTIVE_SUFFIX WAKECORE_EFFECTIVE_SUFFIX
export GIT_COMMIT GIT_BRANCH BUILD_DATE BUILD_TIME

######################################
# Unified Version String Generation
######################################
# Function to generate version string with suffix: $(call version_string,COMP_VERSION,COMP_EFFECTIVE_SUFFIX)
# Example: $(call version_string,$(APP_VERSION),$(APP_EFFECTIVE_SUFFIX)) -> "1.0.0.945_beta"
version_string = $(1)$(if $(2),_$(2))


# Generate version strings for all components
FSBL_VERSION_STR    := $(call version_string,$(FSBL_VERSION),$(FSBL_EFFECTIVE_SUFFIX))
APP_VERSION_STR     := $(call version_string,$(APP_VERSION),$(APP_EFFECTIVE_SUFFIX))
WEB_VERSION_STR     := $(call version_string,$(WEB_VERSION),$(WEB_EFFECTIVE_SUFFIX))
MODEL_VERSION_STR   := $(call version_string,$(MODEL_VERSION),$(MODEL_EFFECTIVE_SUFFIX))
WAKECORE_VERSION_STR := $(call version_string,$(WAKECORE_VERSION),$(WAKECORE_EFFECTIVE_SUFFIX))

######################################
# Version Header Generation (cross-platform using Python)
######################################
VERSION_HEADER = Custom/Common/Inc/version.h
VERSION_SCRIPT = $(PKG_SCRIPT_DIR)/version_header.py

.PHONY: version-header
version-header:
	@echo "Generating version header..."
	@python $(VERSION_SCRIPT) -o $(VERSION_HEADER) \
		$(if $(VERSION_BUILD),-b $(VERSION_BUILD)) \
		--fsbl-version "$(FSBL_VERSION_STR)"

######################################
# Default Target
######################################
.PHONY: all
all: $(BUILD_DIR) fsbl app web model wakecore
	@echo "========================================="
	@echo "Firmware Build Complete!"
	@echo "  FSBL: $(BUILD_DIR)/$(FSBL_NAME).bin"
	@echo "  App:  $(BUILD_DIR)/$(APP_NAME).bin"
	@echo "  Web:  $(BUILD_DIR)/$(WEB_NAME).bin"
	@echo "  Model: $(BUILD_DIR)/$(MODEL_NAME).bin"
	@echo "  WakeCore(U0): $(BUILD_DIR)/$(U0_NAME).bin"
	@echo "========================================="

######################################
# Generic Build Template
######################################
# Usage: $(call build_project,name,dir,target_name)
define build_project
.PHONY: $(1)
$(1): $(BUILD_DIR) version-header
	@echo "========================================="
	@echo "Building $(1)..."
	@echo "========================================="
	@$$(MAKE) $$(SUBMAKE_J) -C $(2)
	@echo "Copying $(1) output to $$(BUILD_DIR)/"
	@cp $(2)/$$(BUILD_DIR)/$(3).bin $$(BUILD_DIR)/ 2>/dev/null || true
	@echo "$(1) build complete"
endef

# Generate build targets (all use unified template)
$(eval $(call build_project,fsbl,$(FSBL_DIR),$(FSBL_NAME)))
$(eval $(call build_project,app,$(APP_DIR),$(APP_NAME)))
$(eval $(call build_project,web,$(WEB_DIR),$(WEB_NAME)))
$(eval $(call build_project,model,$(MODEL_DIR),$(MODEL_NAME)))
$(eval $(call build_project,wakecore,$(WAKECORE_DIR),$(U0_NAME)))

######################################
# Generic Sign Template
######################################
define sign_project
.PHONY: sign-$(1)
sign-$(1): $(1)
	@echo "Signing $(1)..."
	@$$(SIGNER) -s -bin $$(BUILD_DIR)/$(2).bin -nk -t $(3) -hv 2.3 -o $$(BUILD_DIR)/$(2)_signed.bin
	@echo "$(1) signed"
endef

# Generate sign targets
$(eval $(call sign_project,fsbl,$(FSBL_NAME),fsbl))
$(eval $(call sign_project,app,$(APP_NAME),ssbl))

.PHONY: sign
sign: $(foreach proj,fsbl app,sign-$(proj))

######################################
# package bin Template
######################################
# Usage: $(call pkg_project,name,dep,target_name,type,comp_name,version,version_str,description,suffix_arg)
# Parameters:
#   $(1) = project name (fsbl, app, web, model)
#   $(2) = dependency target
#   $(3) = target binary name (without .bin)
#   $(4) = package type (fsbl, app, web, ai_model)
#   $(5) = component name (NE301_FSBL, NE301_APP, etc.)
#   $(6) = version number (without suffix, for -v parameter)
#   $(7) = full version string (with suffix, for output filename)
#   $(8) = suffix value (empty if no suffix)
#   $(9) = description
define pkg_project
.PHONY: pkg-$(1)
pkg-$(1): $(2)
	@echo "Creating package for $(1)..."
	@$$(PACKER) $$(BUILD_DIR)/$(3).bin -o $$(BUILD_DIR)/$(3)_v$(7)_pkg.bin -t $(4) -n $(5) -v $(6)  $(if $(8),-s $(8)) -d $(9)
	@echo "$(1) package created: $(3)_v$(7)_pkg.bin"
endef

# Generate package targets using unified version strings
$(eval $(call pkg_project,fsbl,sign-fsbl,$(FSBL_NAME)_signed,fsbl,NE301_FSBL,$(FSBL_VERSION),$(FSBL_VERSION_STR),$(FSBL_EFFECTIVE_SUFFIX),"NE301 First Stage Boot Loader"))
$(eval $(call pkg_project,app,sign-app,$(APP_NAME)_signed,app,NE301_APP,$(APP_VERSION),$(APP_VERSION_STR),$(APP_EFFECTIVE_SUFFIX),"NE301 Main Application"))
$(eval $(call pkg_project,web,web,$(WEB_NAME),web,NE301_WEB,$(WEB_VERSION),$(WEB_VERSION_STR),$(WEB_EFFECTIVE_SUFFIX),"NE301 Web User Interface"))
$(eval $(call pkg_project,model,model,$(MODEL_NAME),ai_model,NE301_MODEL,$(MODEL_VERSION),$(MODEL_VERSION_STR),$(MODEL_EFFECTIVE_SUFFIX),"NE301 AI Model"))

.PHONY: pkg
pkg: $(foreach proj, fsbl app web model,pkg-$(proj))
	@echo "========================================="
	@echo "Package Complete!"
	@echo "========================================="

######################################
# Pack to HEX
######################################
.PHONY: pack-hex
pack-hex: wakecore
	@echo "========================================="
	@echo "Packing firmware to HEX files..."
	@echo "========================================="
	@python $(PKG_SCRIPT_DIR)/pack_to_hex.py
	@echo ""
	@echo "Packing WakeCore..."
	@python $(PKG_SCRIPT_DIR)/pack_to_hex.py --wakecore || echo "Warning: WakeCore not found, skipping..."
	@echo "========================================="
	@echo "HEX files created:"
	@echo "  - $(BUILD_DIR)/ne301_Main.hex (Main firmware, without WiFi)"
	@echo "  - $(BUILD_DIR)/ne301_Main_WiFi.hex (Main firmware, with WiFi)"
	@echo "  - $(BUILD_DIR)/ne301_WakeCore.hex (WakeCore)"
	@echo "========================================="

.PHONY: pack-hex-wakecore
pack-hex-wakecore: wakecore
	@echo "========================================="
	@echo "Packing WakeCore to HEX file..."
	@echo "========================================="
	@python $(PKG_SCRIPT_DIR)/pack_to_hex.py --wakecore
	@echo "========================================="
	@echo "HEX file created: $(BUILD_DIR)/ne301_WakeCore.hex"
	@echo "========================================="

######################################
# Generic Flash Template
######################################
define flash_project
.PHONY: flash-$(1)
flash-$(1): $(2)
	@echo "Flashing $(1) to $(3)..."
	@$$(FLASHER) -c port=SWD mode=HOTPLUG -el $$(EL) -hardRst -w $$(BUILD_DIR)/$(4) $(3)
	@echo "$(1) flash complete"
endef

# Generate flash targets (directly use versioned PKG files)
# Function to generate PKG filename: $(call pkg_filename,COMP_NAME,COMP_VERSION_STR)
pkg_filename = $(1)_v$(2)_pkg.bin

$(eval $(call flash_project,fsbl,sign-fsbl,$(FLASH_ADDR_FSBL),$(FSBL_NAME)_signed.bin))
$(eval $(call flash_project,app,pkg-app,$(FLASH_ADDR_APP),$(call pkg_filename,$(APP_NAME)_signed,$(APP_VERSION_STR))))
$(eval $(call flash_project,web,pkg-web,$(FLASH_ADDR_WEB),$(call pkg_filename,$(WEB_NAME),$(WEB_VERSION_STR))))
$(eval $(call flash_project,model,pkg-model,$(FLASH_ADDR_MODEL),$(call pkg_filename,$(MODEL_NAME),$(MODEL_VERSION_STR))))

# Flash WakeCore without signing/packaging (STM32U0: no ExternalLoader)
.PHONY: flash-wakecore
flash-wakecore: wakecore
	@$(MAKE) -C $(WAKECORE_DIR) flash

.PHONY: flash
flash: $(foreach proj,fsbl app web model,flash-$(proj)) erase-ota 
	@echo "========================================="
	@echo "Flash all to device Complete!"
	@echo "========================================="

######################################
# Generic Erase Template
######################################
define erase_partition
.PHONY: erase-$(1)
erase-$(1):
	@echo "Erasing partition '$(1)' at base $(2) end $(3)..."
	@SECTOR_START=$$$$((( $(2) - $(FLASH_BASE_ADDR) ) / $(FLASH_SECTOR_SIZE) )); \
	SECTOR_END=$$$$((( $(3) - $(FLASH_BASE_ADDR) ) / $(FLASH_SECTOR_SIZE) )); \
	echo "SECTOR_START: $$$$SECTOR_START"; \
	echo "SECTOR_END: $$$$SECTOR_END"; \
	$$(FLASHER) -c port=SWD mode=HOTPLUG -el $$(EL) -hardRst -e [$$$$SECTOR_START $$$$SECTOR_END]
	@echo "Partition '$(1)' erased."	
endef


# Generate erase targets for all partitions
$(eval $(call erase_partition,nvs,$(FLASH_ADDR_NVS_BASE),$(FLASH_ADDR_NVS_END)))
$(eval $(call erase_partition,ota,$(FLASH_ADDR_OTA_BASE),$(FLASH_ADDR_OTA_END)))
$(eval $(call erase_partition,app1,$(FLASH_ADDR_APP1_BASE),$(FLASH_ADDR_APP1_END)))
$(eval $(call erase_partition,app2,$(FLASH_ADDR_APP2_BASE),$(FLASH_ADDR_APP2_END)))
$(eval $(call erase_partition,ai-default,$(FLASH_ADDR_AI_DEFAULT_BASE),$(FLASH_ADDR_AI_DEFAULT_END)))
$(eval $(call erase_partition,ai-1,$(FLASH_ADDR_AI_1_BASE),$(FLASH_ADDR_AI_1_END)))
$(eval $(call erase_partition,ai-2,$(FLASH_ADDR_AI_2_BASE),$(FLASH_ADDR_AI_2_END)))
$(eval $(call erase_partition,ai-3,$(FLASH_ADDR_AI_3_BASE),$(FLASH_ADDR_AI_3_END)))
$(eval $(call erase_partition,littlefs,$(FLASH_ADDR_LITTLEFS_BASE),$(FLASH_ADDR_LITTLEFS_END)))

.PHONY: erase-all
erase-all:
	@echo "========================================="
	@echo "Erasing all partitions (except FSBL)..."
	@echo "========================================="
	@$(MAKE) erase-nvs erase-ota erase-app1 erase-app2 erase-ai-default erase-ai-1 erase-ai-2 erase-ai-3 erase-littlefs
	@echo "========================================="
	@echo "All partitions erased!"
	@echo "========================================="

.PHONY: erase-chip
erase-chip:
	@echo "========================================="
	@echo "WARNING: Erasing entire chip!"
	@echo "========================================="
	@$(FLASHER) -c port=SWD mode=HOTPLUG -el $(EL) -e all
	@echo "Chip erase complete"

######################################
# Generic Clean Template
######################################
define clean_project
.PHONY: clean-$(1)
clean-$(1):
	@echo "Cleaning $(1)..."
	@$$(MAKE) -C $(2) clean
endef

# Generate clean targets
$(eval $(call clean_project,fsbl,$(FSBL_DIR)))
$(eval $(call clean_project,app,$(APP_DIR)))
$(eval $(call clean_project,web,$(WEB_DIR)))
$(eval $(call clean_project,model,$(MODEL_DIR)))
$(eval $(call clean_project,wakecore,$(WAKECORE_DIR)))

.PHONY: clean
clean: $(foreach proj,fsbl app web model wakecore,clean-$(proj)) clean-bin
	@echo "========================================="
	@echo "Clean Complete!"
	@echo "========================================="

.PHONY: clean-bin
clean-bin:
	@echo "Cleaning $(BUILD_DIR)..."
	@rm -rf $(BUILD_DIR)/*

.PHONY: distclean
distclean: clean
	@echo "Deep cleaning..."
	@rm -rf $(BUILD_DIR)

######################################
# Convenience Targets
######################################
.PHONY: rebuild
rebuild: clean all

.PHONY: rebuild-fsbl
rebuild-fsbl: clean-fsbl fsbl

.PHONY: rebuild-app
rebuild-app: clean-app app

.PHONY: rebuild-web
rebuild-web: clean-web web

.PHONY: rebuild-model
rebuild-model: clean-model model

######################################
# Info & Help
######################################
.PHONY: info
info:
	@echo "========================================="
	@echo "NE301 Build Configuration"
	@echo "========================================="
	@echo "Project:         $(PROJECT_NAME)"
	@echo "MCU:             Cortex-M55 (FPv5-D16 hard)"
	@echo "Optimization:    $(OPT)"
	@echo "Parallel:        $(shell nproc 2>/dev/null || echo 4) cores"
	@echo ""
	@echo "Directories:"
	@echo "  Output:        $(BUILD_DIR)/"
	@echo "  FSBL:          $(FSBL_DIR)/"
	@echo "  App:           $(APP_DIR)/"
	@echo "  Web:           $(WEB_DIR)/"
	@echo "  Model:         $(MODEL_DIR)/"
	@echo "  WakeCore(U0):  $(WAKECORE_DIR)/"
	@echo ""
	@echo "Flash Addresses:"
	@echo "  FSBL:          $(FLASH_ADDR_FSBL)"
	@echo "  App:           $(FLASH_ADDR_APP)"
	@echo "  Web:           $(FLASH_ADDR_WEB)"
	@echo "  Model:         $(FLASH_ADDR_MODEL)"
	@echo ""
	@echo "Flash Partitions:"
	@echo "  NVS:           $(FLASH_ADDR_NVS) (64KB)"
	@echo "  OTA:           $(FLASH_ADDR_OTA) (8KB)"
	@echo "  APP1:          $(FLASH_ADDR_APP1) (4MB)"
	@echo "  APP2:          $(FLASH_ADDR_APP2) (4MB)"
	@echo "  AI_Default:    $(FLASH_ADDR_AI_DEFAULT) (5MB)"
	@echo "  AI_1:          $(FLASH_ADDR_AI_1) (5MB)"
	@echo "  AI_2:          $(FLASH_ADDR_AI_2) (5MB)"
	@echo "  AI_3:          $(FLASH_ADDR_AI_3) (8MB)"
	@echo "  LittleFS:      $(FLASH_ADDR_LITTLEFS) (64MB)"
	@echo "========================================="
	@$(CC) --version | head -n 1 2>/dev/null || echo "Toolchain not found"
	@echo "========================================="

.PHONY: help
help:
	@echo "========================================="
	@echo "NE301 Build System"
	@echo "========================================="
	@echo ""
	@echo "Build:    make [fsbl|app|web|model|all]"
	@echo "          STEDGEAI_VARIANT=2.2|3.0|4.0  (default: $(STEDGEAI_VARIANT))"
	@echo "          make wakecore   # Build STM32U0 WakeCore"
	@echo "Sign:     make sign[-fsbl|-app]"
	@echo "Flash:    make flash[-fsbl|-app|-web|-model|-wakecore]"
	@echo "Package:  make pkg[-fsbl|-app|-web|-model]"
	@echo "Pack HEX: make pack-hex  # Pack all firmware (Main, Main+WiFi, WakeCore) to HEX files"
	@echo "          make pack-hex-wakecore  # Pack WakeCore to separate HEX file only"
	@echo "Erase:    make erase-[nvs|ota|app1|app2|ai-default|ai-1|ai-2|ai-3|littlefs]"
	@echo "          make erase-all  # Erase all partitions (except FSBL)"
	@echo "          make erase-chip           # Erase entire chip (WARNING!)"
	@echo "Clean:    make clean[-fsbl|-app|-web|-model]"
	@echo "Rebuild:  make rebuild[-fsbl|-app|-web|-model]"
	@echo ""
	@echo "Examples:"
	@echo "  make              # Build all"
	@echo "  make fsbl         # Build FSBL only"
	@echo "  make app          # Build App only"
	@echo "  make web          # Build Web"
	@echo "  make model        # Build AI model"
	@echo "  make wakecore     # Build STM32U0 WakeCore"
	@echo "  make flash        # Flash all to device"
	@echo "  make pkg          # Package all for OTA"
	@echo "  make pack-hex     # Pack all firmware (Main, Main+WiFi, WakeCore) to HEX files"
	@echo "  make pack-hex-wakecore  # Pack WakeCore to separate HEX file only"
	@echo "  make sign         # Sign all for app and fsbl"
	@echo "  make sign-fsbl    # Sign FSBL only"
	@echo "  make pkg-fsbl 	   # Package signed FSBL only"
	@echo "  make pkg-app      # Package signed APP only"
	@echo "  make flash-fsbl   # Flash signed FSBL to device"
	@echo "  make flash-app    # Flash signed APP to device"
	@echo "  make flash-wakecore   # Flash WakeCore to U0 (0x8000000)"
	@echo "  make erase-nvs    # Erase NVS partition"
	@echo "  make erase-app1   # Erase APP1 partition"
	@echo "  make erase-all    # Erase all partitions"
	@echo "  make erase-chip   # Erase entire chip (WARNING!)"
	@echo "  make clean        # Clean all build files"
	@echo "  make distclean    # Deep clean (including dependencies)"
	@echo "  make -j20         # Parallel build with 20 jobs"
	@echo ""
	@echo "Tips:"
	@echo "  - Parallel build is auto-enabled (detect CPU cores)"
	@echo "  - Use erase commands carefully to avoid data loss"
	@echo ""
	@echo "Use 'make info' for configuration details"
	@echo "========================================="

######################################
# Directory Creation
######################################
$(BUILD_DIR):
	@mkdir -p $@

