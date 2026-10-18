@echo off
rem ****************************************************************************
rem  STM32H5F4 TF-M flash after regression (Windows, ST-Link)
rem
rem  Default:
rem    1) erase_flash.bat      mass-erase user flash
rem    2) regression.bat       option bytes (WRPSG11, SECWM, BOOT_UBE)
rem    3) find .bin in cwd / this folder, else build_s and build_ns here
rem       if no .bin, convert matching .hex to .bin
rem    4) download BL2/S/NS to 0x0C00E000 / 0x0C078000 / 0x0C0F8000
rem
rem  Usage:
rem    tfm_update.bat                 erase + option bytes + download
rem    tfm_update.bat images-only     skip erase/regression; only program images
rem    tfm_update.bat [images-only] [ST-LINK SN]
rem ****************************************************************************
if not defined H5F4_INNER (
    set "H5F4_INNER=1"
    cmd /c ""%~f0" %*"
    echo.
    echo Window stays open. Press any key to close.
    pause
    exit /b
)
setlocal EnableExtensions EnableDelayedExpansion

set "EXIT_CODE=0"
set "FAILED_STEP="
set "FLASHED=0"
set "DO_REG=1"
set "H5F4_PORT=SWD"
set "H5F4_SN="
set "SKIP_NS=0"
call "%~dp0h5f4_env.bat"

if /i "%~1"=="-h" goto :usage
if /i "%~1"=="/?" goto :usage

:parse_args
if "%~1"=="" goto :args_done
if /i "%~1"=="images-only" (
    set "DO_REG=0"
    shift
    goto :parse_args
)
if /i "%~1"=="skip-erase" (
    set "DO_REG=0"
    shift
    goto :parse_args
)
set "H5F4_SN=%~1"
shift
goto :parse_args
:args_done

echo.
echo ============================================================
echo  STM32H5F4  TF-M UPDATE  ^(ST-Link, 0x0C alias^)
echo  rev:  %H5F4_REV%
echo  cwd:  %CD%
echo ============================================================
echo.

call "%~dp0h5f4_setup.bat"
if errorlevel 1 (
    set "FAILED_STEP=STM32_Programmer_CLI"
    set "EXIT_CODE=1"
    goto :finish
)

echo.
echo Flash map ^(secure alias^):
echo   BL2  %H5F4_ADDR_BL2_S%
echo   S    %H5F4_ADDR_S_S%
echo   NS   %H5F4_ADDR_NS_S%   ^(H573 was 0x0C088000, do not use^)
echo.

if "%DO_REG%"=="1" (
    echo [1] erase_flash.bat ^(unlock WRP/SECWM, -e all^)
    echo ------------------------------------------------------------
    set "TFM_SKIP_PAUSE=1"
    if defined H5F4_SN (
        call "%~dp0erase_flash.bat" %H5F4_SN%
    ) else (
        call "%~dp0erase_flash.bat"
    )
    if errorlevel 1 (
        echo [FAIL] erase_flash.bat failed
        set "FAILED_STEP=erase_flash.bat"
        set "EXIT_CODE=1"
        set "TFM_SKIP_PAUSE="
        goto :finish
    )
    echo [ok]   flash erased
    echo.
    echo [2] regression.bat ^(option bytes: WRPSG11, SECWM 0-255, BOOT_UBE^)
    echo ------------------------------------------------------------
    if defined H5F4_SN (
        call "%~dp0regression.bat" %H5F4_SN%
    ) else (
        call "%~dp0regression.bat"
    )
    set "TFM_SKIP_PAUSE="
    if errorlevel 1 (
        echo [FAIL] regression.bat failed, skip download
        set "FAILED_STEP=regression.bat"
        set "EXIT_CODE=1"
        goto :finish
    )
    echo [ok]   option bytes programmed
    echo.
) else (
    echo [1] skip erase + regression ^(images-only^)
    echo.
)

echo [3] Find images
echo ------------------------------------------------------------
call "%~dp0h5f4_find_images.bat"
if errorlevel 1 (
    set "FAILED_STEP=find images"
    set "EXIT_CODE=1"
    goto :finish
)
echo.

set "connect=%H5F4_CONNECT%"

echo [4] Download images
echo.

if defined TFM_S_SIGNED (
    call :flash_image "%TFM_S_SIGNED%" %H5F4_ADDR_S_S% "S signed"
    if errorlevel 1 goto :finish
) else (
    call :flash_image "%TFM_S_NS_SIGNED%" %H5F4_ADDR_S_S% "S+NS signed"
    if errorlevel 1 goto :finish
    set "SKIP_NS=1"
)

if "%SKIP_NS%"=="1" (
    echo [info] skip NS, already in concatenated S+NS
) else (
    if not defined TFM_NS_SIGNED (
        echo [FAIL] tfm_ns_signed.bin not found
        set "FAILED_STEP=no NS image"
        set "EXIT_CODE=1"
        goto :finish
    )
    call :flash_image "%TFM_NS_SIGNED%" %H5F4_ADDR_NS_S% "NS signed"
    if errorlevel 1 goto :finish
)

echo [5] Unlock HDP and WRP before BL2
echo ------------------------------------------------------------
echo CMD: STM32_Programmer_CLI %connect% -ob %H5F4_HDP_OFF%
STM32_Programmer_CLI %connect% -ob %H5F4_HDP_OFF%
echo CMD: STM32_Programmer_CLI %connect% -ob %H5F4_WRP%
STM32_Programmer_CLI %connect% -ob %H5F4_WRP%
if errorlevel 1 (
    echo [FAIL] could not clear WRPSG11/12/21/22
    set "FAILED_STEP=unlock WRP"
    set "EXIT_CODE=1"
    goto :finish
)
echo.

call :flash_image "%TFM_BL2%" %H5F4_ADDR_BL2_S% "BL2"
if errorlevel 1 goto :finish

echo [6] Reset MCU
echo ------------------------------------------------------------
echo CMD: STM32_Programmer_CLI %connect% -hardRst
STM32_Programmer_CLI %connect% -hardRst
if errorlevel 1 (
    echo [FAIL] reset failed
    set "FAILED_STEP=hardRst"
    set "EXIT_CODE=1"
    goto :finish
)
echo [ok]   reset done
echo.

echo ============================================================
echo  ALL STEPS OK  ^(%FLASHED% file(s) downloaded^)
echo  UART 115200 USART6 PC6/PC7 must show H5F4BL2
echo  If you only see Starting bootloader without H5F4BL2:
echo    run tfm_update.bat again ^(not images-only^) to mass-erase HDP
echo ============================================================
goto :finish

:usage
echo Usage: tfm_update.bat [images-only] [ST-LINK SN]
echo Default: erase_flash, regression option bytes, then flash cwd/bin or hex.
pause
exit /b 0

:flash_image
set "STEP_PATH=%~1"
set "STEP_ADDR=%~2"
set "STEP_DESC=%~3"
set "STEP_NAME=%~nx1"
echo ------------------------------------------------------------
echo DOWNLOAD  %STEP_DESC%  [%STEP_NAME%]
echo FILE: %STEP_PATH%
set "EXT=%~x1"
if /i "%EXT%"==".hex" (
    echo CMD:  STM32_Programmer_CLI %connect% -d "%STEP_PATH%" -v
    STM32_Programmer_CLI %connect% -d "%STEP_PATH%" -v
) else (
    echo ADDR: %STEP_ADDR%
    echo CMD:  STM32_Programmer_CLI %connect% -d "%STEP_PATH%" %STEP_ADDR% -v
    STM32_Programmer_CLI %connect% -d "%STEP_PATH%" %STEP_ADDR% -v
)
if errorlevel 1 (
    echo [FAIL] download %STEP_NAME%
    echo        If BL2 verify fails at 0x0C010000, HDP still covers BL2.
    echo        Re-run without images-only so regression.bat mass-erases.
    set "FAILED_STEP=download %STEP_NAME%"
    set "EXIT_CODE=1"
    exit /b 1
)
echo [ok]   %STEP_NAME% downloaded
echo.
set /a FLASHED+=1
exit /b 0

:finish
echo.
if not "%EXIT_CODE%"=="0" (
    echo ============================================================
    echo  FAILED at: %FAILED_STEP%
    echo ============================================================
)
if defined H5F4_INNER endlocal & exit /b %EXIT_CODE%
echo Window stays open. Press any key to close.
pause
endlocal & exit /b %EXIT_CODE%
