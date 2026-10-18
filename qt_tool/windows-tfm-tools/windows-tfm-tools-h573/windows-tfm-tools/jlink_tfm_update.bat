@echo off
rem ****************************************************************************
rem  * STM32H573I-DK TF-M flash after J-Link regression (Windows)
rem  *
rem  * J-Link programs the 0x08000000 flash window. Hex files that use the
rem  * secure alias 0x0Cxxxxxx are remapped (0x0C - 0x04000000 = 0x08).
rem  * Hex files are converted with jlink_hex_ns_alias.py (Python).
rem  *
rem  * Prefer separate .bin (NS is entire Bank2, not packed after S):
rem  *   tfm_s_signed.bin       0x08044000
rem  *   tfm_ns_signed.bin      0x08100000
rem  *   bl2.bin                0x0800E000
rem  *   tfm_s_ns_signed.bin    fallback only; concatenated NS has the wrong offset
rem  *
rem  * SPDX-License-Identifier: BSD-3-Clause
rem  ****************************************************************************
setlocal EnableExtensions EnableDelayedExpansion

set "EXIT_CODE=0"
set "FAILED_STEP="
set "FLASHED=0"
set "SCRIPT_REV=cube-jlink-20260825f"
set "SN_ARG="

if /i "%~1"=="-h" goto :usage
if /i "%~1"=="/?" goto :usage
if not "%~1"=="" set "SN_ARG=%~1"

set "sn_option="
if defined SN_ARG set "sn_option=sn=%SN_ARG%"

rem NS flash alias (J-Link). Secure alias is NS + 0x04000000.
set "ADDR_BL2=0x0800E000"
set "ADDR_S=0x08044000"
set "ADDR_NS=0x08100000"

echo.
echo ============================================================
echo  STM32H573I-DK  jlink_tfm_update.bat
echo  rev:  %SCRIPT_REV%
echo  file: %~f0
echo  cwd:  %CD%
echo  J-Link: program 0x08 alias as .bin, no 0x0C hex, SECWM open while flashing
echo ============================================================
echo.

set "REG_BAT="
if exist "%~dp0jlink_regression.bat" set "REG_BAT=%~dp0jlink_regression.bat"
if not defined REG_BAT if exist "%~dp0regression.bat" set "REG_BAT=%~dp0regression.bat"
if not defined REG_BAT (
    echo [FAIL] jlink_regression.bat not found next to this script
    set "FAILED_STEP=locate jlink_regression.bat"
    set "EXIT_CODE=1"
    goto :finish
)
echo [info] regression = %REG_BAT%

echo [1] Locate STM32_Programmer_CLI
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
    set "FAILED_STEP=locate STM32_Programmer_CLI"
    set "EXIT_CODE=1"
    goto :finish
)
echo [ok]   STM32_Programmer_CLI ready
echo [info] connect = -c port=JLINK ap=1 %sn_option% mode=UR
echo.

echo [2] Run J-Link regression
echo ------------------------------------------------------------
set "TFM_SKIP_PAUSE=1"
if defined SN_ARG (
    call "%REG_BAT%" jlink %SN_ARG%
) else (
    call "%REG_BAT%" jlink
)
set "TFM_SKIP_PAUSE="
if errorlevel 1 (
    echo.
    echo [FAIL] regression failed, skip download
    set "FAILED_STEP=jlink_regression.bat"
    set "EXIT_CODE=1"
    goto :finish
)
echo [ok]   regression finished
echo.

set "connect=-c port=JLINK ap=1 %sn_option% mode=UR"
set "connect_no_reset=-c port=JLINK ap=1 %sn_option% mode=HotPlug"

echo [2b] Open SECWM ^(J-Link cannot program fully-secure 0x0C flash^)
echo CMD: STM32_Programmer_CLI %connect_no_reset% -ob SECWM1_STRT=127 SECWM1_END=0 SECWM2_STRT=127 SECWM2_END=0
STM32_Programmer_CLI %connect_no_reset% -ob SECWM1_STRT=127 SECWM1_END=0 SECWM2_STRT=127 SECWM2_END=0
if errorlevel 1 (
    echo [FAIL] could not open SECWM
    set "FAILED_STEP=open SECWM"
    set "EXIT_CODE=1"
    goto :finish
)
echo.

echo [3] Scan images
set "FOUND_ANY=0"
call :note_file tfm_s_signed.bin      "S     %ADDR_S%"
call :note_file tfm_s_ns_signed.bin   "S+NS  %ADDR_S%"
call :note_file tfm_s_ns_signed.hex   "S+NS  hex, remap 0x0C-^>0x08"
call :note_file tfm_ns_signed.bin     "NS    %ADDR_NS%"
call :note_file bl2.bin               "BL2   %ADDR_BL2%"
call :note_file bl2.hex               "BL2   hex, remap 0x0C-^>0x08"
echo.

if "%FOUND_ANY%"=="0" (
    echo [FAIL] no TF-M images in:
    echo        %CD%
    echo        %~dp0
    set "FAILED_STEP=no image files"
    set "EXIT_CODE=1"
    goto :finish
)

echo [4] Download
echo.

call :find_file tfm_s_signed.bin
if errorlevel 1 goto :j_s_ns_bin
call :flash_bin "!FILE!" %ADDR_S% S-signed
if errorlevel 1 goto :finish
goto :after_s

:j_s_ns_bin
call :find_file tfm_s_ns_signed.bin
if errorlevel 1 goto :j_s_ns_hex
echo [warn] tfm_s_ns_signed.bin has no Bank1 gap; still flash tfm_ns_signed.bin if present
call :flash_bin "!FILE!" %ADDR_S% S-NS-signed-fallback
if errorlevel 1 goto :finish
goto :after_s

:j_s_ns_hex
call :find_file tfm_s_ns_signed.hex
if errorlevel 1 goto :j_no_s
echo [warn] tfm_s_ns_signed.hex has no Bank1 gap; still flash tfm_ns_signed.bin if present
call :flash_hex "!FILE!" S-NS-signed-fallback
if errorlevel 1 goto :finish
goto :after_s
:j_no_s
echo [info] no S / S+NS image
:after_s

call :find_file tfm_ns_signed.bin
if errorlevel 1 goto :j_after_ns
call :flash_bin "!FILE!" %ADDR_NS% NS-signed
if errorlevel 1 goto :finish
:j_after_ns

call :find_file bl2.bin
if errorlevel 1 goto :j_bl2_hex
call :flash_bin "!FILE!" %ADDR_BL2% BL2
if errorlevel 1 goto :finish
goto :after_bl2
:j_bl2_hex
call :find_file bl2.hex
if errorlevel 1 goto :after_bl2
call :flash_hex "!FILE!" BL2
if errorlevel 1 goto :finish
:after_bl2

if "%FLASHED%"=="0" (
    echo [FAIL] nothing downloaded
    set "FAILED_STEP=nothing downloaded"
    set "EXIT_CODE=1"
    goto :finish
)

echo [4b] Restore full-bank SECWM
echo CMD: STM32_Programmer_CLI %connect_no_reset% -ob SECWM1_STRT=0 SECWM1_END=127 SECWM2_STRT=0 SECWM2_END=127
STM32_Programmer_CLI %connect_no_reset% -ob SECWM1_STRT=0 SECWM1_END=127 SECWM2_STRT=0 SECWM2_END=127
if errorlevel 1 (
    echo [FAIL] restore SECWM
    set "FAILED_STEP=restore SECWM"
    set "EXIT_CODE=1"
    goto :finish
)
echo [ok]   SECWM restored
echo.

echo [5] Reset MCU
echo ------------------------------------------------------------
echo CMD: STM32_Programmer_CLI %connect% -hardRst
echo ------------------------------------------------------------
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
echo  ALL STEPS OK  ^(%FLASHED% file(s) downloaded^)  port=JLINK
echo ============================================================
goto :finish

:usage
echo Usage: jlink_tfm_update.bat [J-Link SN]
echo J-Link uses 0x08 flash alias. Hex 0x0C addresses are remapped.
pause
exit /b 0

:note_file
call :find_file %~1
if not errorlevel 1 (
    echo        FOUND  %~1    %~2
    set "FOUND_ANY=1"
) else (
    echo        skip   %~1
)
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

:remap_hex
set "HEX_BIN=%TEMP%\tfm_jlink_%~n1.bin"
set "HEX_ADDR_FILE=%HEX_BIN%.addr"
echo [info] hex -^> bin on 0x08 alias ^(python^)
echo        in  %~2
echo        out %HEX_BIN%
if not exist "%~dp0jlink_hex_ns_alias.py" (
    echo [FAIL] missing %~dp0jlink_hex_ns_alias.py
    set "FAILED_STEP=remap %~1"
    set "EXIT_CODE=1"
    exit /b 1
)
set "PY="
where python >nul 2>&1 && set "PY=python"
if not defined PY where python3 >nul 2>&1 && set "PY=python3"
if not defined PY where py >nul 2>&1 && set "PY=py -3"
if not defined PY (
    echo [FAIL] Python not found. Add python to PATH, or copy tfm_s_ns_signed.bin and skip hex.
    set "FAILED_STEP=python not found"
    set "EXIT_CODE=1"
    exit /b 1
)
echo [info] %PY% "%~dp0jlink_hex_ns_alias.py"
%PY% "%~dp0jlink_hex_ns_alias.py" -InFile "%~2" -OutFile "%HEX_BIN%"
if errorlevel 1 (
    echo [FAIL] hex to bin failed
    set "FAILED_STEP=remap %~1"
    set "EXIT_CODE=1"
    exit /b 1
)
if not exist "%HEX_BIN%" (
    echo [FAIL] bin not created
    set "FAILED_STEP=remap %~1"
    set "EXIT_CODE=1"
    exit /b 1
)
set "HEX_LOAD="
if exist "%HEX_ADDR_FILE%" set /p HEX_LOAD=<"%HEX_ADDR_FILE%"
echo [info] LOAD %HEX_LOAD%
echo %HEX_LOAD% | findstr /i /c:"0x0C" >nul
if not errorlevel 1 (
    echo [FAIL] converted address still 0x0C, will not program secure alias
    set "FAILED_STEP=remap still 0x0C"
    set "EXIT_CODE=1"
    exit /b 1
)
exit /b 0

:flash_hex
set "STEP_PATH=%~1"
set "STEP_DESC=%~2"
set "STEP_NAME=%~nx1"
call :remap_hex "%STEP_NAME%" "%STEP_PATH%"
if errorlevel 1 exit /b 1
for %%I in ("%HEX_BIN%") do set "HEX_NAME=%%~nxI"
echo ------------------------------------------------------------
echo DOWNLOAD  %STEP_DESC%  [%STEP_NAME%]
echo FILE: %HEX_BIN%
echo ADDR: %HEX_LOAD%   ^(must be 0x08..., never 0x0C...^)
echo CMD:  STM32_Programmer_CLI %connect% -d !HEX_NAME! %HEX_LOAD%
echo ------------------------------------------------------------
pushd "%TEMP%"
STM32_Programmer_CLI %connect% -d !HEX_NAME! %HEX_LOAD% > "%TEMP%\tfm_jlink_dl.txt" 2>&1
set "DLRC=!ERRORLEVEL!"
popd
call :check_download
exit /b %ERRORLEVEL%

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
echo CMD:  STM32_Programmer_CLI %connect% -d %STEP_NAME% %STEP_ADDR%
echo        cwd %~dp1
echo ------------------------------------------------------------
pushd "%~dp1"
if errorlevel 1 (
    echo [FAIL] cannot cd to %~dp1
    set "FAILED_STEP=cd %STEP_NAME%"
    set "EXIT_CODE=1"
    exit /b 1
)
STM32_Programmer_CLI %connect% -d %STEP_NAME% %STEP_ADDR% > "%TEMP%\tfm_jlink_dl.txt" 2>&1
set "DLRC=!ERRORLEVEL!"
popd
call :check_download
exit /b %ERRORLEVEL%

:check_download
type "%TEMP%\tfm_jlink_dl.txt"
findstr /c:"0x0C044000" /c:"0x0C00E000" /c:"0x0C0C4000" /c:"0x0C100000" "%TEMP%\tfm_jlink_dl.txt" >nul
if not errorlevel 1 (
    echo.
    echo [FAIL] CubeProgrammer still used 0x0C alias. This is the old hex path.
    echo        Need jlink_tfm_update.bat rev cube-jlink-20260825f and jlink_hex_ns_alias.py
    set "FAILED_STEP=download %STEP_NAME% still 0x0C"
    set "EXIT_CODE=1"
    exit /b 1
)
findstr /i /c:"Data mismatch" /c:"verification failed" /c:"Error: Download" /c:"Error: failed" /c:"No debug probe" /c:"Library not found" "%TEMP%\tfm_jlink_dl.txt" >nul
if not errorlevel 1 (
    echo.
    echo [FAIL] download %STEP_NAME%  ^(CubeProgrammer reported Error^)
    set "FAILED_STEP=download %STEP_NAME%"
    set "EXIT_CODE=1"
    exit /b 1
)
if not "%DLRC%"=="0" (
    echo.
    echo [FAIL] download %STEP_NAME%  exit=%DLRC%
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
) else (
    echo ============================================================
    echo  jlink_tfm_update Done
    echo ============================================================
)
echo.
echo Window stays open. Press any key to close.
pause
endlocal & exit /b %EXIT_CODE%
