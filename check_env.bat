@echo off
REM ######################################
REM # Quick Tools Check Script (Windows)
REM ######################################
REM # Checks if all required tools are installed

echo =========================================
echo NE301 Required Tools Check
echo =========================================
echo.

REM Function to check command
set MISSING=0
set OPTIONAL_MISSING=0

echo Essential Tools:
echo ----------------
call :check_tool "arm-none-eabi-gcc" "ARM GCC Compiler"
call :check_tool "arm-none-eabi-objcopy" "ARM Objcopy"
call :check_tool "make" "GNU Make"

echo.
echo Build Tools:
echo ------------
call :check_tool "python" "Python 3"
call :check_tool "node" "Node.js"
call :check_tool "pnpm" "pnpm"

echo.
echo STM32 Tools:
echo ------------
call :check_optional "STM32_Programmer_CLI" "STM32 Programmer"
call :check_optional "STM32_SigningTool_CLI" "STM32 Signing Tool"

echo.
echo AI Model Tools:
echo ---------------
call :check_optional "stedgeai" "ST Edge AI"

REM Check STEDGEAI_CORE_DIR
if defined STEDGEAI_CORE_DIR (
    echo [OK] STEDGEAI_CORE_DIR = %STEDGEAI_CORE_DIR%
) else (
    echo [!!] STEDGEAI_CORE_DIR - NOT SET
)

echo.
echo =========================================

if %MISSING% EQU 0 (
    echo Result: Essential tools complete! [OK]
    echo.
    if %OPTIONAL_MISSING% GTR 0 (
        echo Note: %OPTIONAL_MISSING% optional tool^(s^) missing
        echo.
        echo Basic build available:
        echo   make              # Build firmware ^(FSBL + App^)
        echo   make web          # Build web ^(no stedgeai needed^)
        echo.
        echo Optional features:
        where STM32_Programmer_CLI >nul 2>&1 || echo   - Flash: Install STM32CubeProgrammer
        where stedgeai >nul 2>&1 || echo   - AI Model: Install ST Edge AI ^(stedgeai^)
    ) else (
        echo You can now run:
        echo   make              # Build firmware
        echo   make web          # Build web
        echo   make model        # Build AI model
        echo   make flash        # Flash to device
    )
) else (
    echo Result: %MISSING% essential tool^(s^) missing [ERROR]
    echo.
    echo Run setup script:
    echo   setup.bat         ^(Windows^)
    echo   ./setup.sh        ^(Git Bash^)
    echo.
    echo Or see: SETUP.md for manual installation
)

echo =========================================
goto :eof

REM Function to check essential tool
:check_tool
set tool=%~1
set name=%~2
where %tool% >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [OK] %name%
    %tool% --version 2>&1 | findstr /n "^" | findstr "^1:"
) else (
    echo [!!] %name% - NOT FOUND
    set /a MISSING+=1
)
goto :eof

REM Function to check optional tool
:check_optional
set tool=%~1
set name=%~2
where %tool% >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [OK] %name%
    %tool% --version 2>&1 | findstr /n "^" | findstr "^1:"
) else (
    echo [!!] %name% - NOT FOUND
    set /a OPTIONAL_MISSING+=1
)
goto :eof

