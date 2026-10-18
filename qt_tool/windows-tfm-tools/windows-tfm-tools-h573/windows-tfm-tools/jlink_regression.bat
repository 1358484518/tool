@echo off
rem ****************************************************************************
rem  * STM32H573I-DK TF-M option-byte regression (Windows)
rem  * Default probe: J-Link via CubeProgrammer  -c port=JLINK
rem  * Onboard ST-LINK only if first argument is stlink.
rem  *
rem  * SPDX-License-Identifier: BSD-3-Clause
rem  ****************************************************************************
setlocal EnableExtensions
set "FAILED_STEP="
set "EXIT_CODE=0"
set "SCRIPT_REV=cube-jlink-20260825d"
set "PORT=JLINK"
set "SN_ARG="

if /i "%~1"=="stlink" (
    set "PORT=SWD"
    if not "%~2"=="" set "SN_ARG=%~2"
) else if /i "%~1"=="jlink" (
    set "PORT=JLINK"
    if not "%~2"=="" set "SN_ARG=%~2"
) else if /i "%~1"=="-h" (
    goto :usage
) else if /i "%~1"=="/?" (
    goto :usage
) else if not "%~1"=="" (
    set "SN_ARG=%~1"
)

echo.
echo ============================================================
echo  STM32H573I-DK  jlink_regression.bat  (OEM-iRoT)
echo  rev:  %SCRIPT_REV%
echo  file: %~f0
echo ============================================================
echo.

echo [1/8] Locate STM32_Programmer_CLI
set "CUBEPROG="
if exist "D:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" (
    set "CUBEPROG=D:\ST\STM32CubeProgrammer\bin"
)
if not defined CUBEPROG if exist "%ProgramFiles%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" (
    set "CUBEPROG=%ProgramFiles%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin"
)
if not defined CUBEPROG if exist "%ProgramFiles(x86)%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" (
    set "CUBEPROG=%ProgramFiles(x86)%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin"
)
if defined CUBEPROG (
    echo [info] CubeProgrammer bin = %CUBEPROG%
    set "PATH=%CUBEPROG%;%PATH%"
)
for /d %%D in ("%ProgramFiles%\SEGGER\JLink*") do (
    if exist "%%~D\JLinkARM.dll" set "PATH=%%~D;%PATH%"
)
where STM32_Programmer_CLI >nul 2>&1
if errorlevel 1 (
    echo [FAIL] STM32_Programmer_CLI not found
    set "FAILED_STEP=1/8 Locate STM32_Programmer_CLI"
    set "EXIT_CODE=1"
    goto :finish
)
for /f "delims=" %%I in ('where STM32_Programmer_CLI') do (
    echo [ok]   %%I
    goto :cli_found
)
:cli_found

set "sn_option="
if defined SN_ARG (
    set "sn_option=sn=%SN_ARG%"
    echo [info] probe SN = %SN_ARG%
) else (
    echo [info] using first J-Link ^(port=%PORT%^)
)
echo [info] connect = -c port=%PORT% ap=1 %sn_option% mode=UR

set "connect=-c port=%PORT% ap=1 %sn_option% mode=UR"
set "connect_no_reset=-c port=%PORT% ap=1 %sn_option% mode=HotPlug"
set "product_state=-ob PRODUCT_STATE=0xED TZEN=0xB4"
set "remove_bank1_protect=-ob SECWM1_STRT=127 SECWM1_END=0 WRPSGn1=0xffffffff"
set "remove_bank2_protect=-ob SECWM2_STRT=127 SECWM2_END=0 WRPSGn2=0xffffffff"
set "erase_all=-e all"
set "remove_hdp_protection=-ob HDP1_END=0 HDP2_END=0"
set "default_ob1=-ob SECBOOTADD=0xC0100 HDP1_STRT=1 HDP1_END=0 HDP2_STRT=1 HDP2_END=0 SWAP_BANK=0 SRAM2_RST=0 SRAM2_ECC=0"
set "default_ob2=-ob SECWM1_STRT=127 SECWM1_END=0 SECWM2_STRT=127 SECWM2_END=0"
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
echo [6/8] Default OB1 : SECBOOTADD=0xC0100 ^(BL2^)
set "STEP_ID=6/8"
set "STEP_NAME=Default OB1 SECBOOTADD"
call :run_cli %connect_no_reset% %default_ob1%
if errorlevel 1 goto :finish

echo.
echo [7/8] Leave SECWM open so J-Link can program 0x08 flash
set "STEP_ID=7/8"
set "STEP_NAME=SECWM open for J-Link"
call :run_cli %connect_no_reset% %default_ob2%
if errorlevel 1 goto :finish

echo.
echo [8/8] BOOT_UBE=0xB4 ^(OEM-iRoT^)
set "STEP_ID=8/8"
set "STEP_NAME=BOOT_UBE OEM-iRoT"
call :run_cli %connect_no_reset% %boot_ube%
if errorlevel 1 goto :finish

echo.
echo [extra] Read option bytes
set "STEP_ID=extra"
set "STEP_NAME=Display option bytes"
call :run_cli %connect_no_reset% -ob displ
if errorlevel 1 goto :finish

echo.
echo ============================================================
echo  ALL STEPS OK   port=%PORT%
echo  OEM-iRoT: BOOT_UBE=0xB4  SECBOOTADD=0xC0100  TZEN=0xB4
echo ============================================================
goto :finish

:usage
echo Usage: regression.bat [jlink^|stlink] [SN]
echo Default is J-Link:  -c port=JLINK ap=1
pause
exit /b 0

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
) else (
    echo ============================================================
    echo  regression Done
    echo ============================================================
)
echo.
if /i "%TFM_SKIP_PAUSE%"=="1" (
    endlocal & exit /b %EXIT_CODE%
)
echo Window stays open. Press any key to close.
pause
endlocal & exit /b %EXIT_CODE%
