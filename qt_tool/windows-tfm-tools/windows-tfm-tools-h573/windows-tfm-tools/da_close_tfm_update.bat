@echo off
rem ??????? tfm_update.bat ?????????? da_tfm_close.bat ??????
rem ??????? da_close_regression.bat???????????? tfm_update.bat??
rem ****************************************************************************
rem  * STM32H573I-DK TF-M flash after regression (Windows)
rem  *
rem  * 1) Run da_close_regression.bat (option bytes + erase + OEM-iRoT)
rem  * 2) If present in current dir (or this script's dir), download:
rem  *      bl2.bin                 preferred, written at 0x0C00E000
rem  *      bl2.hex                 only if no bin; addresses inside the file
rem  *      tfm_s_signed.hex/.bin   S 512 KB @ 0x0C044000 (preferred)
rem  *      tfm_ns_signed.bin       NS 1 MB  @ 0x0C100000 (Bank2)
rem  *      tfm_s_ns_signed.hex     fallback only: S+NS concatenated with no
rem  *                              Bank1 gap, so NS lands at the wrong offset.
rem  *
rem  * Usage:
rem  *   tfm_update.bat
rem  *   tfm_update.bat <ST-LINK SN>
rem  *
rem  * SPDX-License-Identifier: BSD-3-Clause
rem  ****************************************************************************
setlocal EnableExtensions EnableDelayedExpansion

set "EXIT_CODE=0"
set "FAILED_STEP="
set "FLASHED=0"
set "SN_ARG="
if not "%~1"=="" set "SN_ARG=%~1"

rem H573 flash map (secure alias 0x0C00_0000)
set "ADDR_BL2=0x0C00E000"
set "ADDR_S=0x0C044000"
set "ADDR_NS=0x0C100000"

echo.
echo ============================================================
echo  STM32H573I-DK  TF-M UPDATE
echo  cwd: %CD%
echo ============================================================
echo.

if not exist "%~dp0da_close_regression.bat" (
    echo [FAIL] da_close_regression.bat not found next to this script
    set "FAILED_STEP=locate da_close_regression.bat"
    set "EXIT_CODE=1"
    goto :finish
)

echo [1] Run da_close_regression.bat
echo ------------------------------------------------------------
set "TFM_SKIP_PAUSE=1"
if defined SN_ARG (
    call "%~dp0da_close_regression.bat" %SN_ARG%
) else (
    call "%~dp0da_close_regression.bat"
)
set "TFM_SKIP_PAUSE="
if errorlevel 1 (
    echo.
    echo [FAIL] da_close_regression.bat failed, skip download
    set "FAILED_STEP=da_close_regression.bat"
    set "EXIT_CODE=1"
    goto :finish
)
echo [ok]   regression finished
echo.

echo [2] Locate STM32_Programmer_CLI
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
if defined CUBEPROG set "PATH=%CUBEPROG%;%PATH%"
where STM32_Programmer_CLI >nul 2>&1
if errorlevel 1 (
    echo [FAIL] STM32_Programmer_CLI not found
    set "FAILED_STEP=locate STM32_Programmer_CLI"
    set "EXIT_CODE=1"
    goto :finish
)
echo [ok]   STM32_Programmer_CLI ready
echo.

rem Keep Under Reset for connect+download (same as previous tfm_update.bat).
set "CONNECT_UR=-c port=SWD ap=1 mode=UR"
set "CONNECT_HP=-c port=SWD ap=1 mode=UR"
if defined SN_ARG (
    set "CONNECT_UR=-c port=SWD ap=1 sn=%SN_ARG% mode=UR"
    set "CONNECT_HP=-c port=SWD ap=1 sn=%SN_ARG% mode=UR"
)

echo [3] Scan images in current directory
set "FOUND_ANY=0"
call :note_file bl2.bin            "BL2  %ADDR_BL2%"
call :note_file bl2.hex            "BL2  hex, used only when bl2.bin is absent"
call :note_file tfm_s_signed.hex   "S    %ADDR_S%    (hex uses file addresses)"
call :note_file tfm_s_signed.bin   "S    %ADDR_S%"
call :note_file tfm_ns_signed.bin  "NS   %ADDR_NS%"
call :note_file tfm_s_ns_signed.hex "fallback S+NS, NS offset is wrong"
echo.

if defined TFM_NS_IMAGE set "FOUND_ANY=1"
if defined TFM_BL2_IMAGE set "FOUND_ANY=1"
if "%FOUND_ANY%"=="0" (
    echo [FAIL] no bl2.bin / bl2.hex / tfm_s_signed / tfm_ns_signed.bin in:
    echo        %CD%
    echo        %~dp0
    set "FAILED_STEP=no image files"
    set "EXIT_CODE=1"
    goto :finish
)

echo [4] Download images that exist
echo.

call :find_file tfm_s_signed.hex
if errorlevel 1 goto :s_bin
call :flash_hex "!FILE!" S-signed
if errorlevel 1 goto :finish
goto :after_s

:s_bin
call :find_file tfm_s_signed.bin
if errorlevel 1 goto :s_fallback
call :flash_bin "!FILE!" %ADDR_S% S-signed
if errorlevel 1 goto :finish
goto :after_s

:s_fallback
call :find_file tfm_s_ns_signed.hex
if errorlevel 1 goto :after_s
echo [warn] tfm_s_ns_signed.hex has no Bank1 gap; NS in this file is not at %ADDR_NS%
call :flash_hex "!FILE!" S-NS-signed-fallback
if errorlevel 1 goto :finish

:after_s
rem TFM_NS_IMAGE ?????????????I???? tfm_ns_signed.bin????????? ADDR_NS??
if not defined TFM_NS_IMAGE goto :ns_default
if not exist "%TFM_NS_IMAGE%" (
    echo [FAIL] TFM_NS_IMAGE not found: %TFM_NS_IMAGE%
    set "FAILED_STEP=TFM_NS_IMAGE"
    set "EXIT_CODE=1"
    goto :finish
)
set "FILE=%TFM_NS_IMAGE%"
call :flash_bin "%FILE%" %ADDR_NS% NS-signed
if errorlevel 1 goto :finish
goto :after_ns
:ns_default
call :find_file tfm_ns_signed.bin
if errorlevel 1 goto :after_ns
call :flash_bin "!FILE!" %ADDR_NS% NS-signed
if errorlevel 1 goto :finish
:after_ns

rem TFM_BL2_IMAGE replaces the search below. A hex keeps the addresses in the file.
rem A bin is written at ADDR_BL2. Empty prefers bl2.bin, then bl2.hex.
if not defined TFM_BL2_IMAGE goto :bl2_default
if not exist "%TFM_BL2_IMAGE%" (
    echo [FAIL] TFM_BL2_IMAGE not found: %TFM_BL2_IMAGE%
    set "FAILED_STEP=TFM_BL2_IMAGE"
    set "EXIT_CODE=1"
    goto :finish
)
for %%I in ("%TFM_BL2_IMAGE%") do set "BL2_EXT=%%~xI"
if /i "!BL2_EXT!"==".hex" (
    call :flash_hex "%TFM_BL2_IMAGE%" BL2
) else (
    call :flash_bin "%TFM_BL2_IMAGE%" %ADDR_BL2% BL2
)
if errorlevel 1 goto :finish
goto :after_bl2
:bl2_default
set "BL2_FILE="
set "BL2_HEX="
for %%D in ("%CD%" "%~dp0.") do (
    if not defined BL2_FILE if exist "%%~fD\bl2.bin" set "BL2_FILE=%%~fD\bl2.bin"
)
for %%D in ("%CD%" "%~dp0.") do (
    if not defined BL2_FILE if not defined BL2_HEX if exist "%%~fD\bl2.hex" set "BL2_HEX=%%~fD\bl2.hex"
)
for %%D in ("%~dp0build_s" "%~dp0build_s\bin" "%~dp0build_s\api_ns\bin" "%~dp0build_ns" "%~dp0build_ns\bin") do (
    if not defined BL2_FILE if not defined BL2_HEX if exist "%%~fD\bl2.bin" set "BL2_FILE=%%~fD\bl2.bin"
)
for %%D in ("%~dp0build_s" "%~dp0build_s\bin" "%~dp0build_s\api_ns\bin" "%~dp0build_ns" "%~dp0build_ns\bin") do (
    if not defined BL2_FILE if not defined BL2_HEX if exist "%%~fD\bl2.hex" set "BL2_HEX=%%~fD\bl2.hex"
)
if defined BL2_FILE goto :bl2_flash_bin
if not defined BL2_HEX goto :after_bl2
call :flash_hex "%BL2_HEX%" BL2
if errorlevel 1 goto :finish
goto :after_bl2
:bl2_flash_bin
call :flash_bin "%BL2_FILE%" %ADDR_BL2% BL2
if errorlevel 1 goto :finish
:after_bl2

echo [5] Reset MCU
echo ------------------------------------------------------------
echo CMD: STM32_Programmer_CLI %CONNECT_UR% -hardRst
echo ------------------------------------------------------------
STM32_Programmer_CLI %CONNECT_UR% -hardRst
if errorlevel 1 goto :rst_fail
echo [ok]   reset done
echo.

echo ============================================================
echo  ALL STEPS OK  ^(%FLASHED% file^(s^) downloaded^)
echo ============================================================
goto :finish

:rst_fail
echo [FAIL] reset failed
set "FAILED_STEP=hardRst"
set "EXIT_CODE=1"
goto :finish

:note_file
call :find_file %~1
if errorlevel 1 (
    echo        skip   %~1
    exit /b 0
)
echo        FOUND  %~1                 -^> %~2
set "FOUND_ANY=1"
exit /b 0

:find_file
set "FILE="
if exist "%CD%\%~1" (
    set "FILE=%CD%\%~1"
    exit /b 0
)
if exist "%~dp0%~1" (
    set "FILE=%~dp0%~1"
    exit /b 0
)
exit /b 1

rem CubeProgrammer on Windows treats -d "file.bin" as extension .bin" (invalid).
rem cd into the folder and pass only the filename, unquoted.
:flash_hex
set "STEP_PATH=%~1"
set "STEP_DESC=%~2"
set "STEP_NAME=%~nx1"
if not exist "%STEP_PATH%" (
    echo [FAIL] missing %STEP_PATH%
    set "FAILED_STEP=missing %STEP_NAME%"
    set "EXIT_CODE=1"
    exit /b 1
)
echo ------------------------------------------------------------
echo DOWNLOAD  %STEP_DESC%  [%STEP_NAME%]
echo FILE: %STEP_PATH%
echo CMD:  STM32_Programmer_CLI %CONNECT_HP% -d %STEP_NAME% -v
echo        cwd %~dp1
echo ------------------------------------------------------------
pushd "%~dp1"
if errorlevel 1 (
    echo [FAIL] cannot cd to %~dp1
    set "FAILED_STEP=cd %STEP_NAME%"
    set "EXIT_CODE=1"
    exit /b 1
)
STM32_Programmer_CLI %CONNECT_HP% -d %STEP_NAME% -v
set "DLRC=!ERRORLEVEL!"
popd
if not "!DLRC!"=="0" (
    echo [FAIL] download %STEP_NAME%
    set "FAILED_STEP=download %STEP_NAME%"
    set "EXIT_CODE=1"
    exit /b 1
)
echo [ok]   %STEP_NAME% downloaded
echo.
set /a FLASHED+=1
exit /b 0

:flash_bin
set "STEP_PATH=%~1"
set "STEP_ADDR=%~2"
set "STEP_DESC=%~3"
set "STEP_NAME=%~nx1"
if not exist "%STEP_PATH%" (
    echo [FAIL] missing %STEP_PATH%
    set "FAILED_STEP=missing %STEP_NAME%"
    set "EXIT_CODE=1"
    exit /b 1
)
echo ------------------------------------------------------------
echo DOWNLOAD  %STEP_DESC%  [%STEP_NAME%]
echo FILE: %STEP_PATH%
echo ADDR: %STEP_ADDR%
echo CMD:  STM32_Programmer_CLI %CONNECT_HP% -d %STEP_NAME% %STEP_ADDR% -v
echo        cwd %~dp1
echo ------------------------------------------------------------
pushd "%~dp1"
if errorlevel 1 (
    echo [FAIL] cannot cd to %~dp1
    set "FAILED_STEP=cd %STEP_NAME%"
    set "EXIT_CODE=1"
    exit /b 1
)
STM32_Programmer_CLI %CONNECT_HP% -d %STEP_NAME% %STEP_ADDR% -v
set "DLRC=!ERRORLEVEL!"
popd
if not "!DLRC!"=="0" (
    echo [FAIL] download %STEP_NAME%
    set "FAILED_STEP=download %STEP_NAME%"
    set "EXIT_CODE=1"
    exit /b 1
)
echo [ok]   %STEP_NAME% downloaded @ %STEP_ADDR%
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
echo.
rem ???? da_tfm_close.bat ???? TFM_OUTER_NO_PAUSE=1?????????? pause??????????????
rem ???????????????????????????? pause?????????????????
if /i "%TFM_OUTER_NO_PAUSE%"=="1" (
    endlocal & exit /b %EXIT_CODE%
)
echo Window stays open. Press any key to close.
echo.
pause
endlocal & exit /b %EXIT_CODE%
