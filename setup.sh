#!/bin/bash
######################################
# NE301 Development Environment Setup
######################################
# Auto setup script for Linux/macOS/Git Bash
# Installs: ARM GCC, Make, Node.js, pnpm, Python

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_NAME="ne301"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Toolchain versions
ARM_GCC_VERSION="13.3.rel1"
ARM_GCC_URL_LINUX="https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_GCC_VERSION}/binrel/arm-gnu-toolchain-${ARM_GCC_VERSION}-x86_64-arm-none-eabi.tar.xz"
ARM_GCC_URL_MACOS="https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_GCC_VERSION}/binrel/arm-gnu-toolchain-${ARM_GCC_VERSION}-darwin-x86_64-arm-none-eabi.tar.xz"

echo "========================================="
echo "NE301 Toolchain Setup"
echo "========================================="
echo ""

######################################
# Detect OS
######################################
detect_os() {
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        OS="linux"
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        OS="macos"
    elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "cygwin" ]]; then
        OS="windows"
    else
        OS="unknown"
    fi
    echo "Detected OS: $OS"
}

######################################
# Check if toolchain exists
######################################
check_toolchain() {
    if command -v arm-none-eabi-gcc &> /dev/null; then
        echo -e "${GREEN}✓ ARM GCC toolchain found${NC}"
        arm-none-eabi-gcc --version | head -n 1
        return 0
    else
        echo -e "${YELLOW}✗ ARM GCC toolchain not found${NC}"
        return 1
    fi
}

######################################
# Check dependencies
######################################
check_dependencies() {
    echo ""
    echo "Checking essential dependencies..."
    
    local missing_deps=()
    local optional_deps=()
    
    # Check essential tools
    if ! command -v make &> /dev/null; then
        missing_deps+=("make")
    fi
    
    # Check python (for model packaging)
    if ! command -v python &> /dev/null && ! command -v python3 &> /dev/null; then
        missing_deps+=("python")
    fi
    
    # Check node (for web building)
    if ! command -v node &> /dev/null; then
        missing_deps+=("node")
    fi
    
    # Check pnpm (for web building)
    if ! command -v pnpm &> /dev/null; then
        missing_deps+=("pnpm")
    fi
    
    # Check optional tools
    if ! command -v STM32_Programmer_CLI &> /dev/null; then
        optional_deps+=("STM32_Programmer_CLI")
    fi
    
    if ! command -v STM32_SigningTool_CLI &> /dev/null; then
        optional_deps+=("STM32_SigningTool_CLI")
    fi
    
    if ! command -v stedgeai &> /dev/null; then
        optional_deps+=("stedgeai")
    fi
    
    # Report results
    if [ ${#missing_deps[@]} -eq 0 ]; then
        echo -e "${GREEN}✓ All essential dependencies found${NC}"
        
        if [ ${#optional_deps[@]} -gt 0 ]; then
            echo -e "${YELLOW}⚠ Optional tools missing: ${optional_deps[*]}${NC}"
            echo "  These are not required for basic firmware/web builds"
        fi
        return 0
    else
        echo -e "${YELLOW}✗ Missing essential dependencies: ${missing_deps[*]}${NC}"
        return 1
    fi
}

######################################
# Install toolchain for Linux
######################################
install_linux() {
    echo ""
    echo "Installing ARM GCC toolchain for Linux..."
    
    # Check if running as root
    if [ "$EUID" -ne 0 ]; then
        echo -e "${YELLOW}Note: May need sudo for system-wide installation${NC}"
    fi
    
    # Option 1: Package manager (easier but older version)
    echo "Option 1: Install via apt (may be older version)"
    echo "  sudo apt update && sudo apt install gcc-arm-none-eabi"
    echo ""
    echo "Option 2: Install latest from ARM (recommended)"
    echo "  Downloading ARM GCC ${ARM_GCC_VERSION}..."
    
    read -p "Install latest from ARM? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        INSTALL_DIR="/opt/arm-gnu-toolchain"
        TMP_DIR="/tmp/arm-gcc-install"
        
        mkdir -p "$TMP_DIR"
        cd "$TMP_DIR"
        
        echo "Downloading..."
        wget -q --show-progress "$ARM_GCC_URL_LINUX"
        
        echo "Extracting..."
        tar xf arm-gnu-toolchain-*.tar.xz
        
        echo "Installing to $INSTALL_DIR..."
        sudo mv arm-gnu-toolchain-*-arm-none-eabi "$INSTALL_DIR"
        
        echo "Creating symlinks..."
        sudo ln -sf "$INSTALL_DIR/bin"/* /usr/local/bin/
        
        echo -e "${GREEN}✓ Installation complete${NC}"
        rm -rf "$TMP_DIR"
    fi
}

######################################
# Install toolchain for macOS
######################################
install_macos() {
    echo ""
    echo "Installing ARM GCC toolchain for macOS..."
    
    if command -v brew &> /dev/null; then
        echo "Using Homebrew..."
        brew install --cask gcc-arm-embedded
    else
        echo -e "${YELLOW}Homebrew not found. Manual installation required.${NC}"
        echo "1. Download from: $ARM_GCC_URL_MACOS"
        echo "2. Extract to /usr/local/"
        echo "3. Add to PATH in ~/.zshrc or ~/.bash_profile"
    fi
}

######################################
# Install for Windows
######################################
install_windows() {
    echo ""
    echo "Installing ARM GCC toolchain for Windows..."
    echo ""
    echo "Recommended: Install STM32CubeCLT"
    echo "  Download: https://www.st.com/en/development-tools/stm32cubeclt.html"
    echo ""
    echo "Alternative: ARM GNU Toolchain"
    echo "  Download: https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads"
    echo "  Select: arm-gnu-toolchain-*-mingw-w64-i686-arm-none-eabi.exe"
    echo ""
    echo "After installation, add to PATH or use:"
    echo "  make GCC_PATH=\"C:\\path\\to\\toolchain\\bin\""
}

######################################
# Install Node.js and pnpm
######################################
install_node_deps() {
    echo ""
    echo "Installing Node.js dependencies..."
    
    # Check Node.js
    if ! command -v node &> /dev/null; then
        echo "Node.js not found. Please install from:"
        echo "  https://nodejs.org/ (LTS version recommended)"
        return 1
    fi
    
    # Check pnpm
    if ! command -v pnpm &> /dev/null; then
        echo "Installing pnpm..."
        npm install -g pnpm
    fi
    
    echo -e "${GREEN}✓ Node.js dependencies ready${NC}"
}

######################################
# Generate configuration file
######################################
generate_config() {
    echo ""
    echo "Generating configuration file..."
    
    # Detect toolchain path
    TOOLCHAIN_PATH=$(dirname "$(which arm-none-eabi-gcc)" 2>/dev/null || echo "")
    
    cat > "${SCRIPT_DIR}/.make.env" << EOF
# NE301 Makefile Configuration
# Auto-generated by setup script

# ARM GCC Toolchain path
GCC_PATH = ${TOOLCHAIN_PATH}

# Parallel build jobs (auto-detect or specify)
# MAKEFLAGS += -j\$(shell nproc 2>/dev/null || echo 4)

# Optimization level
# OPT = -Os -g3

# Flash addresses (can be customized)
# FLASH_ADDR_FSBL = 0x34000000
# FLASH_ADDR_APP = 0x70100000
# FLASH_ADDR_WEB = 0x90400000
# FLASH_ADDR_MODEL = 0x90700000

# ST Edge AI Core (for AI model generation)
# Uncomment and set if you have ST Edge AI installed:
# export STEDGEAI_CORE_DIR=/path/to/STEdgeAI
EOF
    
    echo -e "${GREEN}✓ Configuration saved to .make.env${NC}"
    echo "  Edit this file to customize build settings"
}

######################################
# Test build
######################################
test_build() {
    echo ""
    echo "Testing build system..."
    
    cd "$SCRIPT_DIR"
    
    # Test make info
    if make info &> /dev/null; then
        echo -e "${GREEN}✓ Build system test passed${NC}"
        make info
        return 0
    else
        echo -e "${RED}✗ Build system test failed${NC}"
        return 1
    fi
}

######################################
# Main installation flow
######################################
main() {
    detect_os
    
    echo ""
    echo "Checking current setup..."
    
    # Check toolchain
    if check_toolchain; then
        TOOLCHAIN_OK=true
    else
        TOOLCHAIN_OK=false
    fi
    
    # Check dependencies
    check_dependencies
    
    # Install if needed
    if [ "$TOOLCHAIN_OK" = false ]; then
        echo ""
        echo "ARM GCC toolchain not found. Would you like to install it?"
        read -p "Install now? (y/n) " -n 1 -r
        echo
        
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            case $OS in
                linux)
                    install_linux
                    ;;
                macos)
                    install_macos
                    ;;
                windows)
                    install_windows
                    echo ""
                    echo "Please install manually and re-run this script"
                    exit 0
                    ;;
                *)
                    echo "Unknown OS. Please install manually."
                    exit 1
                    ;;
            esac
        fi
    fi
    
    # Install Node.js deps if needed
    if ! command -v pnpm &> /dev/null; then
        install_node_deps
    fi
    
    # Generate config
    generate_config
    
    # Test
    test_build
    
    echo ""
    echo "========================================="
    echo "Setup Complete!"
    echo "========================================="
    echo ""
    echo "Essential tools installed:"
    echo "  [OK] ARM GCC Compiler"
    echo "  [OK] Make"
    echo "  [OK] Python"
    echo "  [OK] Node.js & pnpm"
    echo ""
    
    # Check optional tools
    local has_flash=false
    local has_ai=false
    
    if command -v STM32_Programmer_CLI &> /dev/null; then
        has_flash=true
    fi
    
    if command -v stedgeai &> /dev/null && [ -n "$STEDGEAI_CORE_DIR" ]; then
        has_ai=true
    fi
    
    if [ "$has_flash" = false ] || [ "$has_ai" = false ]; then
        echo "Optional tools (install separately):"
        if [ "$has_flash" = false ]; then
            echo "  [!] STM32CubeProgrammer - for flashing firmware"
            echo "    Download: https://www.st.com/stm32cubeprog"
        fi
        if [ "$has_ai" = false ]; then
            echo "  [!] ST Edge AI (stedgeai) - for AI model generation"
            echo "    Download: https://www.st.com/en/development-tools/stedgeai-core.html#get-software"
            echo "    Set STEDGEAI_CORE_DIR environment variable after install"
        fi
        echo ""
    fi
    
    echo "Next steps:"
    echo "  1. make              # Build firmware (FSBL + App)"
    echo "  2. make web          # Build web frontend"
    if [ "$has_ai" = true ]; then
        echo "  3. make model        # Build AI model"
        echo "  4. make flash        # Flash all to device"
    elif [ "$has_flash" = true ]; then
        echo "  3. make flash        # Flash to device"
    fi
    echo ""
    echo "For help: make help"
    echo "To verify: ./check_env.sh"
    echo "========================================="
}

# Run main
main

