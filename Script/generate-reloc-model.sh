#!/bin/bash

# ST Edge AI Model Generation and Relocation Script
# This script integrates model conversion to C code and binary generation
# Usage: ./generate-reloc-model.sh [options] or ./generate-reloc-model.sh [model_file] [output_name] [neural_art_config]

set -e  # Exit on any error

# Default configuration
MODEL_FILE="st_yolo_x_nano_480_1.0_0.25_3_int8.tflite"
NETWORK_BIN="network_rel.bin"
NEURAL_ART_CONFIG="default@neural_art_reloc.json"
C_OUT_DIR="st_ai_c"
BIN_OUT_DIR="st_ai_bin"
APPEND_ARGS=""

# Alternative way to specify neural art config via environment variable
if [ -n "$NEURAL_ART_CONFIG_FILE" ]; then
    NEURAL_ART_CONFIG="$NEURAL_ART_CONFIG_FILE"
fi

# Flag for clean operation
CLEAN_ONLY=false
STEDGEAI_BIN=""

# Color output functions
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to clean all generated files and directories
clean_all() {
    log_info "Cleaning all generated files and directories..."
    
    local cleaned_items=0
    
    # Remove build directory
    if [ -d "$C_OUT_DIR" ]; then
        rm -rf "$C_OUT_DIR"
        log_info "Removed build directory: $C_OUT_DIR"
        cleaned_items=$((cleaned_items + 1))
    fi
    
    # Remove output directory
    if [ -d "$BIN_OUT_DIR" ]; then
        rm -rf "$BIN_OUT_DIR"
        log_info "Removed output directory: $BIN_OUT_DIR"
        cleaned_items=$((cleaned_items + 1))
    fi
    
    # Remove common output binary files
    local binary_files=("network_rel.bin" "network.bin" "model.bin" "*.bin")
    for pattern in "${binary_files[@]}"; do
        if ls $pattern 1> /dev/null 2>&1; then
            for file in $pattern; do
                if [ -f "$file" ]; then
                    rm -f "$file"
                    log_info "Removed binary file: $file"
                    cleaned_items=$((cleaned_items + 1))
                fi
            done
        fi
    done
    
    # Remove common generated files
    local generated_files=(
        "network.c"
        "network.h" 
        "network_data.c"
        "network_data.h"
        "network_data_params.c"
        "network_data_params.h"
        "*.log"
        "*.tmp"
    )
    
    for pattern in "${generated_files[@]}"; do
        if ls $pattern 1> /dev/null 2>&1; then
            for file in $pattern; do
                if [ -f "$file" ]; then
                    rm -f "$file"
                    log_info "Removed generated file: $file"
                    cleaned_items=$((cleaned_items + 1))
                fi
            done
        fi
    done
    
    if [ $cleaned_items -eq 0 ]; then
        log_info "No files to clean - workspace is already clean"
    else
        log_success "Cleaned $cleaned_items items successfully"
    fi
    
    log_success "Clean operation completed"
}

# Function to show usage
show_usage() {
    echo "Usage: $0 [OPTIONS] or $0 [model_file] [output_name] [neural_art_config]"
    echo ""
    echo "Generate relocatable model binary from TensorFlow Lite model"
    echo ""
    echo "Options:"
    echo "  -m, --model FILE       TensorFlow Lite model file (default: $MODEL_FILE)"
    echo "  -o, --output FILE      Output binary file name (default: $NETWORK_BIN)"
    echo "  -f, --config CONFIG    Neural art configuration (default: uses STEDGEAI_CORE_DIR path)"
    echo "  -a, --append ARGS      Additional arguments for stedgeai generate command"
    echo "  -c, --clean            Clean all generated files and directories"
    echo "  -h, --help             Show this help message"
    echo ""
    echo "Environment variables:"
    echo "  STEDGEAI_CORE_DIR        Path to ST Edge AI installation directory"
    echo "                           (stedgeai is resolved from this dir, not PATH)"
    echo "  NEURAL_ART_CONFIG_FILE   Alternative way to specify neural art config file"
    echo ""
    echo "Examples:"
    echo "  # Basic usage:"
    echo "  $0 -m my_model.tflite -o my_output.bin -f \"default@./custom_config.json\""
    echo "  $0 --model my_model.tflite --output my_output.bin"
    echo ""
    echo "  # With additional stedgeai arguments:"
    echo "  $0 -m st_yolo_x_nano_480_1.0_0.25_3_int8.tflite -f reloc-ne301@neural_art_reloc.json -o network.bin -a \"--abc xxx --xyz xxx\""
    echo "  $0 -m my_model.tflite -a \"--optimization-level high --memory-layout custom\""
    echo ""
    echo "  # Using positional arguments:"
    echo "  $0 my_model.tflite my_output.bin \"default@./custom_config.json\""
    echo ""
    echo "  # Clean generated files:"
    echo "  $0 -c"
    echo "  $0 --clean"
    echo ""
    echo "  # Using environment variable:"
    echo "  export NEURAL_ART_CONFIG_FILE=\"./my_config.json\""
    echo "  $0 -m my_model.tflite -o my_output.bin"
}

# Function to parse command line arguments
parse_arguments() {
    # Check if first argument starts with dash (option format)
    if [[ $# -gt 0 && "$1" =~ ^- ]]; then
        # Parse options format
        while [[ $# -gt 0 ]]; do
            case $1 in
                -m|--model)
                    if [ -z "$2" ]; then
                        log_error "Option $1 requires an argument"
                        show_usage
                        exit 1
                    fi
                    MODEL_FILE="$2"
                    shift 2
                    ;;
                -o|--output)
                    if [ -z "$2" ]; then
                        log_error "Option $1 requires an argument"
                        show_usage
                        exit 1
                    fi
                    NETWORK_BIN="$2"
                    shift 2
                    ;;
                -f|--config)
                    if [ -z "$2" ]; then
                        log_error "Option $1 requires an argument"
                        show_usage
                        exit 1
                    fi
                    NEURAL_ART_CONFIG="$2"
                    shift 2
                    ;;
                -a|--append)
                    if [ -z "$2" ]; then
                        log_error "Option $1 requires an argument"
                        show_usage
                        exit 1
                    fi
                    APPEND_ARGS="$2"
                    shift 2
                    ;;
                -c|--clean)
                    CLEAN_ONLY=true
                    shift
                    ;;
                -h|--help)
                    show_usage
                    exit 0
                    ;;
                *)
                    log_error "Unknown option: $1"
                    show_usage
                    exit 1
                    ;;
            esac
        done
    else
        # Parse positional arguments format (backward compatibility)
        if [ $# -ge 1 ] && [ "$1" != "-h" ] && [ "$1" != "--help" ]; then
            MODEL_FILE="$1"
        fi
        if [ $# -ge 2 ]; then
            NETWORK_BIN="$2"
        fi
        if [ $# -ge 3 ]; then
            NEURAL_ART_CONFIG="$3"
        fi
        if [ $# -gt 3 ]; then
            log_warning "Extra arguments ignored: ${@:4}"
        fi
    fi
}

# Resolve stedgeai from STEDGEAI_CORE_DIR so an older PATH entry cannot win.
resolve_stedgeai_bin() {
    local candidates=()

    if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
        candidates=(
            "$STEDGEAI_CORE_DIR/Utilities/windows/stedgeai.exe"
            "$STEDGEAI_CORE_DIR/Utilities/windows/stedgeai"
        )
    else
        candidates=(
            "$STEDGEAI_CORE_DIR/Utilities/linux/stedgeai"
            "$STEDGEAI_CORE_DIR/Utilities/windows/stedgeai.exe"
        )
    fi

    for candidate in "${candidates[@]}"; do
        if [ -f "$candidate" ]; then
            STEDGEAI_BIN="$candidate"
            export PATH="$(cd "$(dirname "$candidate")" && pwd):$PATH"
            return 0
        fi
    done

    if command -v stedgeai &> /dev/null; then
        STEDGEAI_BIN="stedgeai"
        log_warning "stedgeai not found under STEDGEAI_CORE_DIR; falling back to PATH"
        return 0
    fi

    return 1
}

# Function to check if required tools and environment variables are available
check_prerequisites() {
    log_info "Checking prerequisites..."
    
    # Check if STEDGEAI_CORE_DIR is set
    if [ -z "$STEDGEAI_CORE_DIR" ]; then
        log_error "STEDGEAI_CORE_DIR environment variable is not set"
        log_info "Please set STEDGEAI_CORE_DIR to your ST Edge AI installation directory"
        exit 1
    fi

    if ! resolve_stedgeai_bin; then
        log_error "stedgeai not found under STEDGEAI_CORE_DIR: $STEDGEAI_CORE_DIR"
        log_info "Expected: $STEDGEAI_CORE_DIR/Utilities/windows/stedgeai.exe (Windows)"
        exit 1
    fi

    log_info "Using stedgeai: $STEDGEAI_BIN"
    "$STEDGEAI_BIN" --version 2>/dev/null | head -1 || true

    if ! python "$(dirname "$0")/check_stedgeai_toolchain.py"; then
        exit 1
    fi
    
    # Check if python is available
    if ! command -v python &> /dev/null; then
        log_error "python command not found"
        exit 1
    fi
    
    # Check if model file exists
    if [ ! -f "$MODEL_FILE" ]; then
        log_error "Model file '$MODEL_FILE' not found in current directory"
        log_info "Please ensure the model file is present: $MODEL_FILE"
        exit 1
    fi
    
    # Neural art config will be validated by stedgeai tool itself
    
    # Check if NPU driver script exists
    NPU_DRIVER_SCRIPT="$STEDGEAI_CORE_DIR/scripts/N6_reloc/npu_driver.py"
    if [ ! -f "$NPU_DRIVER_SCRIPT" ]; then
        log_error "NPU driver script not found: $NPU_DRIVER_SCRIPT"
        exit 1
    fi
    
    log_success "All prerequisites checked successfully"
}

# Function to clean up previous builds
cleanup_previous_builds() {
    log_info "Cleaning up previous builds..."
    
    if [ -d "$C_OUT_DIR" ]; then
        rm -rf "$C_OUT_DIR"
        log_info "Removed previous build directory: $C_OUT_DIR"
    fi
    
    if [ -d "$BIN_OUT_DIR" ]; then
        rm -rf "$BIN_OUT_DIR"
        log_info "Removed previous output directory: $BIN_OUT_DIR"
    fi
    
    # Remove any existing network_rel.bin in current directory
    if [ -f "$NETWORK_BIN" ]; then
        rm -f "$NETWORK_BIN"
        log_info "Removed existing $NETWORK_BIN"
    fi
}

# Function to generate C code from model
generate_c_code() {
    log_info "Step 1: Converting model to C code..."
    log_info "Model file: $MODEL_FILE"
    log_info "Target: STM32N6"
    log_info "Output directory: $C_OUT_DIR"
    
    # Run stedgeai generate command
    log_info "Neural art config: $NEURAL_ART_CONFIG"
    if [ -n "$APPEND_ARGS" ]; then
        log_info "Additional arguments: $APPEND_ARGS"
    fi
    
    # Build and execute the stedgeai generate command
    eval "\"$STEDGEAI_BIN\" generate -m \"./$MODEL_FILE\" --target stm32n6 --no-workspace --st-neural-art \"$NEURAL_ART_CONFIG\" --output \"$C_OUT_DIR\" $APPEND_ARGS"
    
    if [ $? -eq 0 ]; then
        log_success "Model successfully converted to C code"
    else
        log_error "Failed to convert model to C code"
        exit 1
    fi
    
    # Check if network.c was generated
    NETWORK_C_FILE="$C_OUT_DIR/network.c"
    if [ ! -f "$NETWORK_C_FILE" ]; then
        log_error "network.c file not found in build directory"
        log_info "Expected location: $NETWORK_C_FILE"
        exit 1
    fi
    
    log_info "Generated files in $C_OUT_DIR:"
    ls -la "$C_OUT_DIR/" | head -10
}

# Function to convert C code to binary
convert_to_binary() {
    log_info "Step 2: Converting C code to network_rel.bin..."
    
    NETWORK_C_FILE="$C_OUT_DIR/network.c"
    
    # Set up Python path for ST Edge AI utilities
    if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
        export PYTHONPATH="$STEDGEAI_CORE_DIR/Utilities/windows/Lib/site-packages"
    else
        export PYTHONPATH="$STEDGEAI_CORE_DIR/Utilities/linux/Lib/site-packages"
    fi
    
    # Run npu_driver.py to convert C code to binary
    python "$STEDGEAI_CORE_DIR/scripts/N6_reloc/npu_driver.py" \
        -i "$NETWORK_C_FILE" \
        --output "$BIN_OUT_DIR"
    
    if [ $? -eq 0 ]; then
        log_success "C code successfully converted to binary"
    else
        log_error "Failed to convert C code to binary"
        exit 1
    fi
    
    # Look for the generated binary file
    GENERATED_BIN=$(find "$BIN_OUT_DIR" -name "network_rel.bin" -type f | head -1)
    
    if [ -z "$GENERATED_BIN" ]; then
        log_error "No .bin file found in output directory"
        log_info "Output directory contents:"
        ls -la "$BIN_OUT_DIR/"
        exit 1
    fi
    
    # Copy the binary to the expected location
    cp "$GENERATED_BIN" "./$NETWORK_BIN"
    log_success "Binary file copied to: $NETWORK_BIN"
    
    # Display file information
    log_info "Generated binary file information:"
    ls -lh "$NETWORK_BIN"
}

# Function to display summary
display_summary() {
    log_success "=== Model Generation Complete ==="
    echo ""
    log_info "Input model: $MODEL_FILE"
    log_info "Generated C code: $C_OUT_DIR/network.c"
    log_info "Final binary: $NETWORK_BIN"
    echo ""
    
    if [ -f "$NETWORK_BIN" ]; then
        BINARY_SIZE=$(stat -c%s "$NETWORK_BIN" 2>/dev/null || stat -f%z "$NETWORK_BIN" 2>/dev/null || echo "unknown")
        log_info "Binary size: $BINARY_SIZE bytes"
    fi
    
    log_info "Build artifacts:"
    log_info "  - C code and headers: $C_OUT_DIR/"
    log_info "  - Intermediate files: $BIN_OUT_DIR/"
    log_info "  - Final binary: $NETWORK_BIN"
    echo ""
    log_success "Ready to deploy: $NETWORK_BIN"
}


# Main execution
main() {
    # Show help if no arguments provided
    if [ $# -eq 0 ]; then
        show_usage
        exit 0
    fi
    
    # Parse command line arguments first
    parse_arguments "$@"
    
    # Handle clean operation
    if [ "$CLEAN_ONLY" = true ]; then
        log_info "ST Edge AI Model Generation Script - Clean Mode"
        log_info "=============================================="
        echo ""
        clean_all
        exit 0
    fi
    
    log_info "ST Edge AI Model Generation Script"
    log_info "=================================="
    log_info "Model: $MODEL_FILE"
    log_info "Output: $NETWORK_BIN"
    log_info "Neural Art Config: $NEURAL_ART_CONFIG"
    if [ -n "$APPEND_ARGS" ]; then
        log_info "Additional Args: $APPEND_ARGS"
    fi
    echo ""
    
    # Check prerequisites
    check_prerequisites
    
    # Clean up previous builds
    cleanup_previous_builds
    
    # Step 1: Generate C code from model
    generate_c_code
    
    # Step 2: Convert C code to binary
    convert_to_binary
    
    # Display summary
    display_summary
    
    log_success "Script completed successfully!"
}

# Handle script interruption
trap 'log_error "Script interrupted"; exit 1' INT TERM

# Execute main function with all arguments
main "$@"
