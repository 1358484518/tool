@echo off
rem ****************************************************************************
rem  STM32H5F4 TF-M option-byte regression (Windows, ST-Link)
rem  Wipe WRP/SECWM, mass-erase, restore full-bank secure OBs, BOOT_UBE=OEM-iRoT.
rem
rem  Usage:
rem    regression.bat              first ST-LINK
rem    regression.bat <SN>         specific probe
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
set "H5F4_PORT=SWD"
set "H5F4_SN="
if /i "%~1"=="-h" goto :usage
if /i "%~1"=="/?" goto :usage
if not "%~1"=="" set "H5F4_SN=%~1"

echo.
echo ============================================================
echo  STM32H5F4  regression  (OEM-iRoT, ST-Link)
echo ============================================================
echo.

call "%~dp0h5f4_env.bat"

echo [1/8] Locate STM32_Programmer_CLI
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
if defined H5F4_SN (
    set "sn_option=sn=%H5F4_SN%"
    echo [info] ST-LINK SN = %H5F4_SN%
) else (
    echo [info] ST-LINK SN not specified, use the first probe
)

set "connect=-c port=SWD ap=1 %sn_option% mode=UR"
set "connect_no_reset=-c port=SWD ap=1 %sn_option% mode=HotPlug"
set "product_state=-ob %H5F4_PRODUCT_STATE%"
set "remove_bank1_protect=-ob SECWM1_STRT=255 SECWM1_END=0 WRPSG11=0xffffffff WRPSG12=0xffffffff"
set "remove_bank2_protect=-ob SECWM2_STRT=255 SECWM2_END=0 WRPSG21=0xffffffff WRPSG22=0xffffffff"
set "erase_all=-e all"
set "remove_hdp_protection=-ob %H5F4_HDP_OFF%"
set "default_ob1=-ob %H5F4_OB1%"
set "default_ob2=-ob %H5F4_SECWM_FULL%"
set "boot_ube=-ob %H5F4_BOOT_UBE%"

echo.
echo [2/8] PRODUCT_STATE=0xED  TZEN=0xB4 ^(TrustZone ON^)
set "STEP_ID=2/8"
set "STEP_NAME=PRODUCT_STATE / TZEN"
set "CLI_ARGS=%connect% %product_state%"
call "%~dp0h5f4_run_cli.bat"
if errorlevel 1 goto :cli_fail

echo.
echo [3/8] Remove bank1 protection and erase all
set "STEP_ID=3/8"
set "STEP_NAME=Remove bank1 protect + erase"
set "CLI_ARGS=%connect% %remove_bank1_protect% %erase_all%"
call "%~dp0h5f4_run_cli.bat"
if errorlevel 1 goto :cli_fail

echo.
echo [4/8] Remove bank2 protection and erase all
set "STEP_ID=4/8"
set "STEP_NAME=Remove bank2 protect + erase"
set "CLI_ARGS=%connect% %remove_bank2_protect% %erase_all%"
call "%~dp0h5f4_run_cli.bat"
if errorlevel 1 goto :cli_fail

echo.
echo [5/8] Remove HDP protection
set "STEP_ID=5/8"
set "STEP_NAME=Remove HDP"
set "CLI_ARGS=%connect_no_reset% %remove_hdp_protection%"
call "%~dp0h5f4_run_cli.bat"
if errorlevel 1 goto :cli_fail

echo.
echo [6/8] Default OB1 : SECBOOTADD=0xC0100 ^(BL2^)
set "STEP_ID=6/8"
set "STEP_NAME=Default OB1 SECBOOTADD"
set "CLI_ARGS=%connect_no_reset% %default_ob1%"
call "%~dp0h5f4_run_cli.bat"
if errorlevel 1 goto :cli_fail

echo.
echo [7/8] Default OB2 : bank1+bank2 full secure ^(SECWM 0-255^)
set "STEP_ID=7/8"
set "STEP_NAME=Default OB2 SECWM"
set "CLI_ARGS=%connect_no_reset% %default_ob2%"
call "%~dp0h5f4_run_cli.bat"
if errorlevel 1 goto :cli_fail

echo.
echo [8/8] BOOT_UBE=0xB4 ^(OEM-iRoT, boot from user flash BL2^)
set "STEP_ID=8/8"
set "STEP_NAME=BOOT_UBE OEM-iRoT"
set "CLI_ARGS=%connect_no_reset% %boot_ube%"
call "%~dp0h5f4_run_cli.bat"
if errorlevel 1 goto :cli_fail

echo.
echo [extra] Read option bytes
set "STEP_ID=extra"
set "STEP_NAME=Display option bytes"
set "CLI_ARGS=%connect_no_reset% -ob displ"
call "%~dp0h5f4_run_cli.bat"
if errorlevel 1 goto :cli_fail

echo.
echo ============================================================
echo  ALL STEPS OK
echo  STM32H5F4 OEM-iRoT: BOOT_UBE=0xB4  SECBOOTADD=0xC0100  TZEN=0xB4
echo  WRP names WRPSG11/12/21/22   SECWM 0-255  ^(not H573 WRPSGn1 / 127^)
echo ============================================================
goto :finish

:usage
echo Usage: regression.bat [ST-LINK SN]
echo H5F4 option bytes: WRPSG11/12/21/22, SECWM STRT/END 0-255.
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
    echo ============================================================
)
if /i "%TFM_SKIP_PAUSE%"=="1" endlocal & exit /b %EXIT_CODE%
if defined H5F4_INNER endlocal & exit /b %EXIT_CODE%
echo Window stays open. Press any key to close.
pause
endlocal & exit /b %EXIT_CODE%
