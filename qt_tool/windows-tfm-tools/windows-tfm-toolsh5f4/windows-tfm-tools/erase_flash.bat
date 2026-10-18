@echo off
rem ****************************************************************************
rem  STM32H5F4 one-click user-flash erase (Windows)
rem  Unlocks WRP/SECWM, mass-erases both banks, tries to drop HDP.
rem  Does not restore TF-M watermarks or download images.
rem
rem  Usage:
rem    erase_flash.bat                 ST-Link, first probe
rem    erase_flash.bat <SN>
rem    erase_flash.bat stlink [SN]
rem    erase_flash.bat jlink [SN]
rem ****************************************************************************
if not defined H5F4_INNER (
    set "H5F4_INNER=1"
    cmd /c ""%~f0" %*"
    echo.
    echo Window stays open. Press any key to close.
    pause
    exit /b
)
setlocal EnableExtensions
set "FAILED_STEP="
set "EXIT_CODE=0"
set "PORT=SWD"
set "SN_ARG="

if /i "%~1"=="-h" goto :usage
if /i "%~1"=="/?" goto :usage
if /i "%~1"=="stlink" (
    set "PORT=SWD"
    if not "%~2"=="" set "SN_ARG=%~2"
) else if /i "%~1"=="jlink" (
    set "PORT=JLINK"
    if not "%~2"=="" set "SN_ARG=%~2"
) else if not "%~1"=="" (
    set "SN_ARG=%~1"
)

call "%~dp0h5f4_env.bat"

echo.
echo ============================================================
echo  STM32H5F4  ERASE FLASH
echo  rev:  %H5F4_REV%
echo  This wipes ALL user Flash ^(4 MB^). Option bytes stay Open/TZEN.
echo ============================================================
echo.

echo [1/6] Locate STM32_Programmer_CLI
where STM32_Programmer_CLI >nul 2>&1
if errorlevel 1 (
    echo [FAIL] STM32_Programmer_CLI not found
    echo        Install STM32CubeProgrammer and add its bin directory to PATH.
    set "FAILED_STEP=1/6 Locate STM32_Programmer_CLI"
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
    echo [info] using first probe ^(port=%PORT%^)
)

set "connect=-c port=%PORT% ap=1 %sn_option% mode=UR"
set "connect_no_reset=-c port=%PORT% ap=1 %sn_option% mode=HotPlug"
echo [info] connect = %connect%

echo.
echo [2/6] PRODUCT_STATE=0xED  TZEN=0xB4 ^(keep TrustZone ON, chip Open^)
set "STEP_ID=2/6"
set "STEP_NAME=PRODUCT_STATE / TZEN"
set "CLI_ARGS=%connect% -ob %H5F4_PRODUCT_STATE%"
call "%~dp0h5f4_run_cli.bat"
if errorlevel 1 goto :cli_fail

echo.
echo [3/6] Unlock bank1 WRP/SECWM and mass-erase
set "STEP_ID=3/6"
set "STEP_NAME=bank1 unlock + erase all"
set "CLI_ARGS=%connect% -ob SECWM1_STRT=255 SECWM1_END=0 WRPSG11=0xffffffff WRPSG12=0xffffffff -e all"
call "%~dp0h5f4_run_cli.bat"
if errorlevel 1 goto :cli_fail

echo.
echo [4/6] Unlock bank2 WRP/SECWM and mass-erase
set "STEP_ID=4/6"
set "STEP_NAME=bank2 unlock + erase all"
set "CLI_ARGS=%connect% -ob SECWM2_STRT=255 SECWM2_END=0 WRPSG21=0xffffffff WRPSG22=0xffffffff -e all"
call "%~dp0h5f4_run_cli.bat"
if errorlevel 1 goto :cli_fail

echo.
echo [5/6] Try to disable HDP ^(may still show STRT^<=END until old BL2 is gone^)
echo ------------------------------------------------------------
echo CMD: STM32_Programmer_CLI %connect_no_reset% -ob %H5F4_HDP_OFF%
echo ------------------------------------------------------------
STM32_Programmer_CLI %connect_no_reset% -ob %H5F4_HDP_OFF%
if errorlevel 1 (
    echo [warn] HDP -ob failed; mass-erase already ran
) else (
    echo [ok]   HDP off requested
)

echo.
echo [6/6] Leave SECWM open so the empty chip can be programmed again
set "STEP_ID=6/6"
set "STEP_NAME=SECWM open"
set "CLI_ARGS=%connect_no_reset% -ob %H5F4_SECWM_OPEN%"
call "%~dp0h5f4_run_cli.bat"
if errorlevel 1 goto :cli_fail

echo.
echo [extra] Option bytes after erase
set "STEP_ID=extra"
set "STEP_NAME=Display option bytes"
set "CLI_ARGS=%connect_no_reset% -ob displ"
call "%~dp0h5f4_run_cli.bat"
if errorlevel 1 goto :cli_fail

echo.
echo [reset]
STM32_Programmer_CLI %connect% -hardRst
echo.
echo ============================================================
echo  FLASH ERASE OK
echo  User Flash is blank. This is not a TF-M download.
echo  Next: tfm_update.bat
echo ============================================================
goto :finish

:usage
echo Usage: erase_flash.bat [stlink^|jlink] [SN]
echo Default is ST-Link:  -c port=SWD ap=1
echo Unlocks WRPSG11/12/21/22 and SECWM, then STM32_Programmer_CLI -e all.
pause
exit /b 0

:cli_fail
set "FAILED_STEP=%STEP_ID% %STEP_NAME%"
set "EXIT_CODE=1"
goto :finish

:finish
echo.
if not "%EXIT_CODE%"=="0" (
    echo ============================================================
    echo  FAILED at: %FAILED_STEP%
    echo  If HDP still covers Flash, use CubeProgrammer GUI:
    echo    Option Bytes -^> Reset MCU to Factory Settings
    echo ============================================================
)
if /i "%TFM_SKIP_PAUSE%"=="1" endlocal & exit /b %EXIT_CODE%
if defined H5F4_INNER endlocal & exit /b %EXIT_CODE%
echo Window stays open. Press any key to close.
pause
endlocal & exit /b %EXIT_CODE%
