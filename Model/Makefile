######################################
# Model Makefile
######################################
# Inherits configuration from root Makefile (via export)

ifndef STEDGEAI_VARIANT
include ../stedgeai.mk
endif

# Project configuration
TARGET = $(PROJECT_NAME)_Model
BUILD_DIR = build

# Model source directories
WEIGHTS_DIR = weights
MPOOL_DIR = mpools
RELOC_CONFIG = yolov8_od@neural_art_reloc.json

# Scripts
SCRIPT_DIR = ../Script
GEN_RELOC = $(SCRIPT_DIR)/generate-reloc-model.sh
PACKAGER = $(SCRIPT_DIR)/model_packager.py
CHECK_STEDGEAI = python $(SCRIPT_DIR)/check_stedgeai_toolchain.py

# Model files (example: YOLOv8)
MODEL_NAME = yolov8n_256_quant_pc_uf_od_coco-person-st
MODEL_TFLITE = $(WEIGHTS_DIR)/$(MODEL_NAME).tflite
MODEL_JSON = $(WEIGHTS_DIR)/$(MODEL_NAME).json

# Intermediate files
NETWORK_RELOC = $(BUILD_DIR)/network_rel.bin

######################################
# Default Target
######################################
.PHONY: all
all: $(BUILD_DIR)/$(TARGET).bin

######################################
# Build Targets
######################################
$(BUILD_DIR):
	@mkdir -p $@

# Step 1: Generate relocated model
$(NETWORK_RELOC): $(MODEL_TFLITE) | $(BUILD_DIR)
	@echo "Checking ST Edge AI toolchain (STEDGEAI_VARIANT=$(STEDGEAI_VARIANT))..."
	@$(CHECK_STEDGEAI)
	@echo "Generating relocated model..."
	@bash $(GEN_RELOC) \
		-m $(MODEL_TFLITE) \
		-f $(RELOC_CONFIG) \
		-o $@
	@echo "Relocated model: $@"

# Step 2: Package model with metadata
$(BUILD_DIR)/$(TARGET).bin: $(NETWORK_RELOC) $(MODEL_JSON) | $(BUILD_DIR)
	@echo "Packaging model..."
	@python $(PACKAGER) create \
		--model $(NETWORK_RELOC) \
		--config $(MODEL_JSON) \
		--output $@
	@echo "Model package complete: $@"

######################################
# Development Targets
######################################
.PHONY: reloc
reloc: $(NETWORK_RELOC)
	@echo "Relocated model ready"

.PHONY: list-models
list-models:
	@echo "Available models in $(WEIGHTS_DIR):"
	@ls -1 $(WEIGHTS_DIR)/*.tflite 2>/dev/null || echo "  No .tflite models found"

######################################
# Clean Targets
######################################
.PHONY: clean
clean:
	@echo "Cleaning Model build files..."
	@bash $(GEN_RELOC) -c
	@rm -rf $(BUILD_DIR)

######################################
# Info
######################################
.PHONY: info
info:
	@echo "Model Configuration:"
	@echo "  TARGET      = $(TARGET)"
	@echo "  BUILD_DIR   = $(BUILD_DIR)"
	@echo "  MODEL       = $(MODEL_NAME)"
	@echo "  TFLITE      = $(MODEL_TFLITE)"
	@echo "  CONFIG      = $(MODEL_JSON)"

######################################
# Help
######################################
.PHONY: help
help:
	@echo "========================================="
	@echo "Model Build System"
	@echo "========================================="
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build model package [default]"
	@echo "  reloc        - Generate relocated model only"
	@echo "  list-models  - List available models"
	@echo "  clean        - Clean build files"
	@echo "  info         - Show configuration"
	@echo ""
	@echo "To use different model:"
	@echo "  Edit MODEL_TFLITE and MODEL_JSON variables"
	@echo "  in this Makefile"
	@echo ""
	@echo "Example models:"
	@echo "  - model_yolov8_uf_od_coco"
	@echo "  - model_yolov8_uf_od_person"
	@echo "  - model_yolox_uf_od_person"
	@echo "========================================="
