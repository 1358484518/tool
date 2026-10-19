@echo off
rem 本文件是 regression.bat 的副本，只给 da_close_tfm_update.bat 调用。
rem 不要改原来的 regression.bat。
rem ****************************************************************************
rem  * STM32H573I-DK TF-M option-byte regression (Windows)
rem  * Wipe protections, erase flash, restore default secure OBs,
rem  * set BOOT_UBE=0xB4 (OEM-iRoT).
rem  *
rem  * Usage:
rem  *   regression.bat              first ST-LINK
rem  *   regression.bat <SN>         specific probe
rem  *
rem  * SPDX-License-Identifier: BSD-3-Clause
rem  ****************************************************************************
setlocal EnableExtensions
set "FAILED_STEP="
set "EXIT_CODE=0"

echo.
echo ============================================================
echo  STM32H573I-DK  regression  (OEM-iRoT)
echo ============================================================
echo.

set "sn_option="
if not "%~1"=="" (
    set "sn_option=sn=%~1"
    echo [info] ST-LINK SN = %~1
) else (
    echo [info] ST-LINK SN not specified, use the first probe
)

echo.
echo [1/8] Locate STM32_Programmer_CLI
if defined STM32_PROGRAMMER_CLI if exist "%STM32_PROGRAMMER_CLI%" (
    for %%I in ("%STM32_PROGRAMMER_CLI%") do set "CUBEPROG=%%~dpI"
    goto :cubeprog_ready
)
set "CUBEPROG="

if exist "D:\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" (
    set "CUBEPROG=D:\STM32CubeProgrammer\bin"
)
if exist "%ProgramFiles%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" (
    set "CUBEPROG=%ProgramFiles%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin"
)
if not defined CUBEPROG if exist "%ProgramFiles(x86)%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" (
    set "CUBEPROG=%ProgramFiles(x86)%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin"
)
:cubeprog_ready
if defined CUBEPROG (
    echo [info] CubeProgrammer bin = %CUBEPROG%
    set "PATH=%CUBEPROG%;%PATH%"
)
where STM32_Programmer_CLI >nul 2>&1
if errorlevel 1 (
    echo [FAIL] step 1/8 : STM32_Programmer_CLI not found
    echo        Install STM32CubeProgrammer, or add its bin directory to PATH.
    set "FAILED_STEP=1/8 Locate STM32_Programmer_CLI"
    set "EXIT_CODE=1"
    goto :finish
)
for /f "delims=" %%I in ('where STM32_Programmer_CLI') do (
    echo [ok]   %%I
    goto :cli_found
)
:cli_found

set "connect=-c port=SWD ap=1 %sn_option% mode=UR"
set "connect_no_reset=-c port=SWD ap=1 %sn_option% mode=HotPlug"
set "product_state=-ob PRODUCT_STATE=0xED TZEN=0xB4"
set "remove_bank1_protect=-ob SECWM1_STRT=127 SECWM1_END=0 WRPSGn1=0xffffffff"
set "remove_bank2_protect=-ob SECWM2_STRT=127 SECWM2_END=0 WRPSGn2=0xffffffff"
set "erase_all=-e all"
set "remove_hdp_protection=-ob HDP1_END=0 HDP2_END=0"
set "default_ob1=-ob SECBOOTADD=0xC0100 HDP1_STRT=1 HDP1_END=0 HDP2_STRT=1 HDP2_END=0 SWAP_BANK=0 SRAM2_RST=0 SRAM2_ECC=0"
set "default_ob2=-ob SECWM2_STRT=0 SECWM2_END=127 SECWM1_STRT=0 SECWM1_END=127"
set "boot_ube=-ob BOOT_UBE=0xB4"

echo.
echo [2/8] PRODUCT_STATE=0xED  TZEN=0xB4 ^(TrustZone ON^)
set "STEP_ID=2/8"
set "STEP_NAME=PRODUCT_STATE / TZEN"
call :run_cli %connect% %product_state%
if errorlevel 1 goto :finish

echo.
echo [3/8] Remove bank1 protection and erase all
set "STEP_ID=3/8"
set "STEP_NAME=Remove bank1 protect + erase"
call :run_cli %connect% %remove_bank1_protect% %erase_all%
if errorlevel 1 goto :finish

echo.
echo [4/8] Remove bank2 protection and erase all
set "STEP_ID=4/8"
set "STEP_NAME=Remove bank2 protect + erase"
call :run_cli %connect% %remove_bank2_protect% %erase_all%
if errorlevel 1 goto :finish

echo.
echo [5/8] Remove HDP protection
set "STEP_ID=5/8"
set "STEP_NAME=Remove HDP"
call :run_cli %connect_no_reset% %remove_hdp_protection%
if errorlevel 1 goto :finish

echo.
echo [6/8] Default OB1 : SECBOOTADD=0xC0100 ^(BL2^), SWAP_BANK=0, SRAM2
set "STEP_ID=6/8"
set "STEP_NAME=Default OB1 SECBOOTADD"
call :run_cli %connect_no_reset% %default_ob1%
if errorlevel 1 goto :finish

echo.
echo [7/8] Default OB2 : bank1+bank2 full secure
set "STEP_ID=7/8"
set "STEP_NAME=Default OB2 SECWM"
call :run_cli %connect_no_reset% %default_ob2%
if errorlevel 1 goto :finish

echo.
echo [8/8] BOOT_UBE=0xB4 ^(OEM-iRoT, boot from user flash BL2^)
echo        0xB4 = OEM-iRoT    0xC3 = ST-iRoT
set "STEP_ID=8/8"
set "STEP_NAME=BOOT_UBE OEM-iRoT"
call :run_cli %connect_no_reset% %boot_ube%
if errorlevel 1 goto :finish

echo.
echo [extra] Read option bytes ^(confirm BOOT_UBE / SECBOOTADD / TZEN^)
set "STEP_ID=extra"
set "STEP_NAME=Display option bytes"
call :run_cli %connect_no_reset% -ob displ
if errorlevel 1 goto :finish

echo.
echo ============================================================
echo  ALL STEPS OK
echo  OEM-iRoT: BOOT_UBE=0xB4  SECBOOTADD=0xC0100  TZEN=0xB4
echo ============================================================
goto :finish

:run_cli
echo ------------------------------------------------------------
echo STEP %STEP_ID%  %STEP_NAME%
echo CMD: STM32_Programmer_CLI %*
echo ------------------------------------------------------------
STM32_Programmer_CLI %*
if errorlevel 1 (
    echo.
    echo [FAIL] step %STEP_ID% : %STEP_NAME%
    echo        command: STM32_Programmer_CLI %*
    set "FAILED_STEP=%STEP_ID% %STEP_NAME%"
    set "EXIT_CODE=1"
    exit /b 1
)
echo [ok]   step %STEP_ID% done
exit /b 0

:finish
echo.
if not "%EXIT_CODE%"=="0" (
    echo ============================================================
    echo  FAILED at: %FAILED_STEP%
    echo ============================================================
)
echo.
rem 只有 da_tfm_close.bat 会设 TFM_OUTER_NO_PAUSE=1。这时把退出码交回去，避免烧录前多停一次键。
rem 单独双击本脚本或 tfm_update.bat 时没有这个变量，仍然 pause。
if /i "%TFM_OUTER_NO_PAUSE%"=="1" (
    endlocal & exit /b %EXIT_CODE%
)
echo Window stays open. Press any key to close.
echo.
pause
endlocal & exit /b %EXIT_CODE%
