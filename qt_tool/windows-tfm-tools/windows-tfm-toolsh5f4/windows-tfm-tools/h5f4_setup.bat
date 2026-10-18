@echo off
rem Shared Windows setup: CubeProgrammer PATH, Python, connect strings.
rem Image search is h5f4_find_images.bat (bin first, else hex -> bin).
rem Linear only: no CALL :label / GOTO :eof (that can close tfm_update.bat).
call "%~dp0h5f4_env.bat"
set "H5F4_SETUP_RC=0"
set "PYCMD="

if not defined H5F4_PORT set "H5F4_PORT=SWD"
set "H5F4_SN_OPT="
if defined H5F4_SN set "H5F4_SN_OPT=sn=%H5F4_SN%"

where STM32_Programmer_CLI >nul 2>&1
if errorlevel 1 (
    echo [FAIL] STM32_Programmer_CLI not found
    echo        Install STM32CubeProgrammer and add its bin directory to PATH.
    set "H5F4_SETUP_RC=1"
    exit /b 1
)
for /f "delims=" %%I in ('where STM32_Programmer_CLI') do (
    echo [ok]   STM32_Programmer_CLI = %%I
    goto :cli_found
)
:cli_found

set "PYCMD="
python -c "import sys; raise SystemExit(0 if sys.version_info[0]>=3 else 1)" >nul 2>&1
if not errorlevel 1 set "PYCMD=python"
if not defined PYCMD python3 -c "import sys; raise SystemExit(0 if sys.version_info[0]>=3 else 1)" >nul 2>&1
if not defined PYCMD if not errorlevel 1 set "PYCMD=python3"
if not defined PYCMD py -3 -c "import sys; raise SystemExit(0 if sys.version_info[0]>=3 else 1)" >nul 2>&1
if not defined PYCMD if not errorlevel 1 set "PYCMD=py -3"
if defined PYCMD (
    echo [ok]   python = %PYCMD%
) else (
    echo [info] Python 3 not on PATH ^(only needed to convert .hex -^> .bin^)
)

set "H5F4_CONNECT=-c port=%H5F4_PORT% ap=1 %H5F4_SN_OPT% mode=UR"
set "H5F4_CONNECT_HP=-c port=%H5F4_PORT% ap=1 %H5F4_SN_OPT% mode=HotPlug"
echo [info] connect = %H5F4_CONNECT%
exit /b 0
