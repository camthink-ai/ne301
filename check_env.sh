#!/bin/bash
######################################
# Quick Tools Check Script
######################################
# Checks if all required tools are installed

echo "========================================="
echo "NE301 Required Tools Check"
echo "========================================="
echo ""

# Function to check command
check_tool() {
    local tool=$1
    local name=$2
    
    if command -v "$tool" &> /dev/null; then
        echo "[OK] $name"
        $tool --version 2>&1 | head -n 1
        return 0
    else
        echo "[!!] $name - NOT FOUND"
        return 1
    fi
}

# Check essential tools
echo "Essential Tools:"
echo "----------------"
check_tool "arm-none-eabi-gcc" "ARM GCC Compiler"
check_tool "arm-none-eabi-objcopy" "ARM Objcopy"
check_tool "make" "GNU Make"

echo ""
echo "Build Tools:"
echo "------------"
check_tool "python" "Python 3"
check_tool "node" "Node.js"
check_tool "pnpm" "pnpm"

echo ""
echo "STM32 Tools:"
echo "------------"
check_tool "STM32_Programmer_CLI" "STM32 Programmer"
check_tool "STM32_SigningTool_CLI" "STM32 Signing Tool"

echo ""
echo "AI Model Tools:"
echo "---------------"
check_tool "stedgeai" "ST Edge AI"

# Check STEDGEAI_CORE_DIR
if [ -n "$STEDGEAI_CORE_DIR" ]; then
    echo "[OK] STEDGEAI_CORE_DIR = $STEDGEAI_CORE_DIR"
else
    echo "[!!] STEDGEAI_CORE_DIR - NOT SET"
fi

echo ""
echo "========================================="

# Count missing tools
MISSING=0
for tool in arm-none-eabi-gcc make python node pnpm; do
    if ! command -v "$tool" &> /dev/null; then
        ((MISSING++))
    fi
done

# Optional tools (don't count as missing for basic build)
OPTIONAL_MISSING=0
for tool in STM32_Programmer_CLI STM32_SigningTool_CLI stedgeai; do
    if ! command -v "$tool" &> /dev/null; then
        ((OPTIONAL_MISSING++))
    fi
done

if [ $MISSING -eq 0 ]; then
    echo "Result: Essential tools complete! ✓"
    echo ""
    if [ $OPTIONAL_MISSING -gt 0 ]; then
        echo "Note: $OPTIONAL_MISSING optional tool(s) missing"
        echo ""
        echo "Basic build available:"
        echo "  make              # Build firmware (FSBL + App)"
        echo "  make web          # Build web (no stedgeai needed)"
        echo ""
        echo "Optional features:"
        if ! command -v STM32_Programmer_CLI &> /dev/null; then
            echo "  - Flash: Install STM32CubeProgrammer"
        fi
        if ! command -v stedgeai &> /dev/null; then
            echo "  - AI Model: Install ST Edge AI (stedgeai)"
        fi
    else
        echo "You can now run:"
        echo "  make              # Build firmware"
        echo "  make web          # Build web"
        echo "  make model        # Build AI model"
        echo "  make flash        # Flash to device"
    fi
else
    echo "Result: $MISSING essential tool(s) missing ✗"
    echo ""
    echo "Run setup script:"
    echo "  ./setup.sh        (Linux/macOS/Git Bash)"
    echo "  setup.bat         (Windows)"
    echo ""
    echo "Or see: SETUP.md for manual installation"
fi

echo "========================================="

