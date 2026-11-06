@echo off
rem STM32H573 / H5F4 test and prod flash. English only: UTF-8 inside cmd breaks on GBK Windows.
rem Images come from test\ or prod\ next to this bat unless a directory is given.
rem Release uses flash_stm32h573_release.bat.
rem Addresses: BL2 0x0C00E000, S 0x0C044000, NS 0x0C100000.
rem After the images: SECWM2=[127,0], WRP 0xFFFFFFE3, HDP1=[0,19].
rem H573 uses WRPSGn1. H5F4 uses WRPSG11.
rem Writes SECBOOT_LOCK=0xC3 before other option bytes. Does not write PRODUCT_STATE=0x72.
rem
rem   flash_stm32h573.bat
rem   flash_stm32h573.bat test [all|download|regression] [DIR] [SN]
rem   flash_stm32h573.bat prod --dir D:\bins [SN]
rem SPDX-License-Identifier: BSD-3-Clause
if /i "%~1"=="__RUN__" goto :run
setlocal EnableExtensions
for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "STAMP=%%I"
set "LOGDIR=%~dp0log"
if not exist "%LOGDIR%" mkdir "%LOGDIR%"
set "LOGFILE=%LOGDIR%\flash_%STAMP%.log"
set "LOGBAT=%~f0"
set "LOGARG=%*"
powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand JABsAG8AZwAgAD0AIAAkAGUAbgB2ADoATABPAEcARgBJAEwARQAKACQAYgBhAHQAIAA9ACAAJABlAG4AdgA6AEwATwBHAEIAQQBUAAoAJABhAHIAZwAgAD0AIAAkAGUAbgB2ADoATABPAEcAQQBSAEcACgBpAGYAIAAoAC0AbgBvAHQAIAAkAGwAbwBnACAALQBvAHIAIAAtAG4AbwB0ACAAJABiAGEAdAApACAAewAgAFcAcgBpAHQAZQAtAEgAbwBzAHQAIAAnAEwATwBHAEYASQBMAEUAIABvAHIAIABMAE8ARwBCAEEAVAAgAG0AaQBzAHMAaQBuAGcAJwA7ACAAZQB4AGkAdAAgADEAIAB9AAoAUwBlAHQALQBDAG8AbgB0AGUAbgB0ACAALQBMAGkAdABlAHIAYQBsAFAAYQB0AGgAIAAkAGwAbwBnACAALQBWAGEAbAB1AGUAIAAoACcAPQA9AD0APQAgACcAIAArACAAKABHAGUAdAAtAEQAYQB0AGUAIAAtAEYAbwByAG0AYQB0ACAAJwB5AHkAeQB5AC0ATQBNAC0AZABkACAASABIADoAbQBtADoAcwBzACcAKQAgACsAIAAnACAAPQA9AD0APQAnACkAIAAtAEUAbgBjAG8AZABpAG4AZwAgAEQAZQBmAGEAdQBsAHQACgAkAGMAbQBkAEwAaQBuAGUAIAA9ACAAJwAiACcAIAArACAAJABiAGEAdAAgACsAIAAnACIAIABfAF8AUgBVAE4AXwBfACAAJwAgACsAIAAkAGEAcgBnAAoAJgAgAGMAbQBkAC4AZQB4AGUAIAAvAGMAIAAkAGMAbQBkAEwAaQBuAGUAIAAyAD4AJgAxACAAfAAgAEYAbwByAEUAYQBjAGgALQBPAGIAagBlAGMAdAAgAHsACgAgACAAIAAgACQAdAAgAD0AIABpAGYAIAAoACQAXwAgAC0AaQBzACAAWwBTAHkAcwB0AGUAbQAuAE0AYQBuAGEAZwBlAG0AZQBuAHQALgBBAHUAdABvAG0AYQB0AGkAbwBuAC4ARQByAHIAbwByAFIAZQBjAG8AcgBkAF0AKQAgAHsAIAAkAF8ALgBUAG8AUwB0AHIAaQBuAGcAKAApACAAfQAgAGUAbABzAGUAIAB7ACAAWwBzAHQAcgBpAG4AZwBdACQAXwAgAH0ACgAgACAAIAAgAFcAcgBpAHQAZQAtAEgAbwBzAHQAIAAkAHQACgAgACAAIAAgAEEAZABkAC0AQwBvAG4AdABlAG4AdAAgAC0ATABpAHQAZQByAGEAbABQAGEAdABoACAAJABsAG8AZwAgAC0AVgBhAGwAdQBlACAAJAB0ACAALQBFAG4AYwBvAGQAaQBuAGcAIABEAGUAZgBhAHUAbAB0AAoAfQAKAGkAZgAgACgAJABuAHUAbABsACAALQBlAHEAIAAkAEwAQQBTAFQARQBYAEkAVABDAE8ARABFACkAIAB7ACAAZQB4AGkAdAAgADAAIAB9ACAAZQBsAHMAZQAgAHsAIABlAHgAaQB0ACAAJABMAEEAUwBUAEUAWABJAFQAQwBPAEQARQAgAH0ACgA=
set "RC=%ERRORLEVEL%"
echo.
echo log: %LOGFILE%
endlocal
exit /b %RC%

:run
shift
setlocal EnableExtensions EnableDelayedExpansion
echo log: %LOGFILE%
echo.

set "EXIT_CODE=0"
set "FAILED_STEP="
set "IMAGE="
set "MODE=all"
set "SN_ARG="
rem External image directory. Empty means test\ or prod\ next to this bat.
set "IMGDIR_EXT="
set "ADDR_BL2=0x0C00E000"
set "ADDR_S=0x0C044000"
set "ADDR_NS=0x0C100000"
set "MAGIC=3db8f396"

:parse_args
if "%~1"=="" goto :args_done
if /i "%~1"=="test" (
    set "IMAGE=test"
    shift
    goto :parse_args
)
if /i "%~1"=="prod" (
    set "IMAGE=prod"
    shift
    goto :parse_args
)
if /i "%~1"=="release" goto :reject_release
if /i "%~1"=="all" (
    set "MODE=all"
    shift
    goto :parse_args
)
if /i "%~1"=="download" (
    set "MODE=download"
    shift
    goto :parse_args
)
if /i "%~1"=="flash" (
    set "MODE=download"
    shift
    goto :parse_args
)
if /i "%~1"=="update" (
    set "MODE=download"
    shift
    goto :parse_args
)
if /i "%~1"=="regression" (
    set "MODE=regression"
    shift
    goto :parse_args
)
if /i "%~1"=="erase" (
    set "MODE=regression"
    shift
    goto :parse_args
)
if /i "%~1"=="regress" (
    set "MODE=regression"
    shift
    goto :parse_args
)
if /i "%~1"=="-h" goto :usage
if /i "%~1"=="--help" goto :usage
if /i "%~1"=="help" goto :usage
rem --dir takes the next argument as the external image directory.
if /i "%~1"=="--dir" goto :got_dir_flag
if /i "%~1"=="-d" goto :got_dir_flag
rem An existing directory is the external image path. Same filenames as test\ or prod\.
if exist "%~1\" (
    if defined IMGDIR_EXT (
        echo [FAIL] Image directory given twice: %~1
        goto :usage
    )
    set "IMGDIR_EXT=%~f1"
    shift
    goto :parse_args
)
echo %~1| findstr /r /i "^[0-9A-Fa-f][0-9A-Fa-f]*$" >nul
if not errorlevel 1 (
    if defined SN_ARG goto :fail_sn
    set "SN_ARG=%~1"
    shift
    goto :parse_args
)
echo [FAIL] Unknown argument: %~1
goto :usage

:got_dir_flag
if "%~2"=="" (
    echo [FAIL] --dir needs a directory
    goto :usage
)
if not exist "%~2\" (
    echo [FAIL] Image directory not found: %~2
    goto :usage
)
if defined IMGDIR_EXT (
    echo [FAIL] Image directory given twice: %~2
    goto :usage
)
set "IMGDIR_EXT=%~f2"
shift
shift
goto :parse_args

:args_done
if not defined IMAGE goto :prompt_image
goto :have_image

:prompt_image
echo Choose an image:
echo   1. test
echo   2. prod
echo Release images use flash_stm32h573_release.bat
set /p CHOICE=Enter 1 or 2: 
if "%CHOICE%"=="1" set "IMAGE=test"
if /i "%CHOICE%"=="test" set "IMAGE=test"
if "%CHOICE%"=="2" set "IMAGE=prod"
if /i "%CHOICE%"=="prod" set "IMAGE=prod"
if not defined IMAGE (
    echo [FAIL] Enter 1 or 2
    set "FAILED_STEP=choose image"
    goto :fail_stop
)

:have_image
rem Use the external directory when one was given. Otherwise test\ or prod\ beside this bat.
if defined IMGDIR_EXT (
    set "IMGDIR=!IMGDIR_EXT!\"
) else (
    set "IMGDIR=%~dp0!IMAGE!\"
)
set "BL2=!IMGDIR!bl2.bin"
set "BL2HEX=!IMGDIR!bl2.hex"
set "SBIN=!IMGDIR!tfm_s_signed.bin"
set "NSBIN=!IMGDIR!tfm_ns_signed.bin"
echo Image: !IMAGE!
echo Dir:   !IMGDIR!
echo Mode:  !MODE!
echo.

if /i "!MODE!"=="regression" goto :after_markers
if not exist "!SBIN!" goto :fail_no_s
if not exist "!NSBIN!" goto :fail_no_ns
rem Flash bl2.hex when present. Otherwise flash bl2.bin. Markers come from that file.
if exist "!BL2HEX!" (
    call :decode_bl2_hex
    if errorlevel 1 goto :fail_stop
) else if exist "!BL2!" (
    set "BL2SCAN=!BL2!"
) else (
    goto :fail_no_bl2
)
set "H5F4_SCAN_FILE=!BL2SCAN!"
set "H5F4_SCAN_TEXT=set wrp1"
call :file_has_set
set "HAS_WRP=!ERRORLEVEL!"
if not "!HAS_WRP!"=="0" goto :fail_not_dev
set "H5F4_SCAN_TEXT=Starting bootloader"
call :file_has_set
set "HAS_SSEC=!ERRORLEVEL!"
if /i "!IMAGE!"=="test" (
    if not "!HAS_SSEC!"=="0" goto :fail_not_test
)
if /i "!IMAGE!"=="prod" (
    if "!HAS_SSEC!"=="0" goto :fail_not_prod
)
echo [ok]   BL2 markers match !IMAGE!
echo.

:after_markers
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
if defined CUBEPROG set "PATH=%CUBEPROG%;%PATH%"
where STM32_Programmer_CLI >nul 2>&1
if errorlevel 1 goto :fail_no_cli
echo [ok]   STM32_Programmer_CLI
echo.

set "CONNECT_UR=-c port=SWD ap=1 mode=UR"
set "CONNECT_HP=-c port=SWD ap=1 mode=HotPlug"
if defined SN_ARG set "CONNECT_UR=-c port=SWD ap=1 sn=%SN_ARG% mode=UR"
if defined SN_ARG set "CONNECT_HP=-c port=SWD ap=1 sn=%SN_ARG% mode=HotPlug"
if defined SN_ARG echo [info] ST-LINK SN=%SN_ARG%

set "CHIP_H5F4=0"
set "CHIPLOG=%TEMP%\stm32_chip_id.txt"
STM32_Programmer_CLI %CONNECT_HP% > "%CHIPLOG%" 2>&1
findstr /i /c:"0x47A" /c:"STM32H5F4" "%CHIPLOG%" >nul
if not errorlevel 1 set "CHIP_H5F4=1"
del /q "%CHIPLOG%" >nul 2>&1
if "%CHIP_H5F4%"=="1" (
    echo [info] STM32H5F4, write protection is WRPSG11
) else (
    echo [info] Treat as H573, write protection is WRPSGn1
)
echo.

if /i "!MODE!"=="download" goto :do_download

echo [1] SECBOOT_LOCK=0xC3
echo     Disable. A release image leaves 0xB4, and dev BL2 reset-loops while it stays.
echo     BOOT_UBE and SECBOOTADD cannot change until this is cleared.
echo     If TrustZone is off, SECBOOT_LOCK reads 0x0. Enable TZEN, then write 0xC3.
echo ------------------------------------------------------------
call :write_secboot_unlock
if errorlevel 1 goto :fail_boot_lock
echo [ok]   SECBOOT_LOCK=0xC3
echo.

echo [2] Regression. This mass-erases the device.
echo     SECBOOTADD=0xC0100 boots BL2 at 0x0C010000
echo ------------------------------------------------------------
echo PRODUCT_STATE=0xED  TZEN=0xB4
STM32_Programmer_CLI %CONNECT_UR% -ob PRODUCT_STATE=0xED TZEN=0xB4
if errorlevel 1 goto :fail_regress
echo Remove bank1 protection and erase all
if "%CHIP_H5F4%"=="1" (
    STM32_Programmer_CLI %CONNECT_UR% -ob SECWM1_STRT=127 SECWM1_END=0 WRPSG11=0xffffffff WRPSG12=0xffffffff -e all
) else (
    STM32_Programmer_CLI %CONNECT_UR% -ob SECWM1_STRT=127 SECWM1_END=0 WRPSGn1=0xffffffff -e all
)
if errorlevel 1 goto :fail_regress
echo Remove bank2 protection and erase all
if "%CHIP_H5F4%"=="1" (
    STM32_Programmer_CLI %CONNECT_UR% -ob SECWM2_STRT=127 SECWM2_END=0 WRPSG21=0xffffffff WRPSG22=0xffffffff -e all
) else (
    STM32_Programmer_CLI %CONNECT_UR% -ob SECWM2_STRT=127 SECWM2_END=0 WRPSGn2=0xffffffff -e all
)
if errorlevel 1 goto :fail_regress
echo Remove HDP
STM32_Programmer_CLI %CONNECT_HP% -ob HDP1_END=0 HDP2_END=0
if errorlevel 1 goto :fail_regress
echo SECBOOTADD=0xC0100
STM32_Programmer_CLI %CONNECT_HP% -ob SECBOOTADD=0xC0100 HDP1_STRT=1 HDP1_END=0 HDP2_STRT=1 HDP2_END=0 SWAP_BANK=0 SRAM2_RST=0 SRAM2_ECC=0
if errorlevel 1 goto :fail_regress
echo SECWM bank1 and bank2
STM32_Programmer_CLI %CONNECT_HP% -ob SECWM2_STRT=0 SECWM2_END=127 SECWM1_STRT=0 SECWM1_END=127
if errorlevel 1 goto :fail_regress
echo BOOT_UBE=0xB4
STM32_Programmer_CLI %CONNECT_HP% -ob BOOT_UBE=0xB4
if errorlevel 1 goto :fail_boot_ube
echo [ok]   regression done
echo.
if /i "!MODE!"=="regression" goto :finish

:do_download
echo [1] SECBOOT_LOCK=0xC3
echo     Disable. A release image leaves 0xB4, and dev BL2 reset-loops while it stays.
echo     If TrustZone is off, SECBOOT_LOCK reads 0x0. Enable TZEN, then write 0xC3.
echo ------------------------------------------------------------
call :write_secboot_unlock
if errorlevel 1 goto :fail_boot_lock
echo [ok]   SECBOOT_LOCK=0xC3
echo.

echo [3] Program S
call :flash_bin "!SBIN!" %ADDR_S% S
if errorlevel 1 goto :fail_stop
echo [4] Program NS
call :flash_bin "!NSBIN!" %ADDR_NS% NS
if errorlevel 1 goto :fail_stop
echo [5] Program BL2
if exist "!BL2HEX!" (
    call :flash_hex "!BL2HEX!" BL2
) else (
    call :flash_bin "!BL2!" %ADDR_BL2% BL2
)
if errorlevel 1 goto :fail_stop

echo [6] Read back MCUboot magic
call :check_magic %ADDR_S% S
if errorlevel 1 goto :fail_stop
call :check_magic %ADDR_NS% NS
if errorlevel 1 goto :fail_stop

echo [7] SECWM2=[127,0]  HDP1=[0,19]  WRP=0xFFFFFFE3
echo     A 0 bit means protected. BL2 expects group 0x1C.
echo     Apply this after the images. HDP and WRP cover BL2.
echo ------------------------------------------------------------
if "%CHIP_H5F4%"=="1" (
    echo     H5F4 WRPSG11=0xFFFFFFE3
    STM32_Programmer_CLI %CONNECT_HP% -ob SECWM2_STRT=127 SECWM2_END=0 WRPSG11=0xFFFFFFE3 HDP1_STRT=0 HDP1_END=19
) else (
    echo     H573 WRPSGn1=0xFFFFFFE3
    STM32_Programmer_CLI %CONNECT_HP% -ob SECWM2_STRT=127 SECWM2_END=0 WRPSGn1=0xFFFFFFE3 HDP1_STRT=0 HDP1_END=19
)
if errorlevel 1 goto :fail_protect
echo [ok]   watermark, write protection, hide protection
echo.
echo Done. No reset. SECBOOT_LOCK=0xC3. PRODUCT_STATE=0x72 was not written.
echo UART 115200 8N1.
echo log: %LOGFILE%
goto :finish

rem Decode Intel hex data bytes so marker text can be searched. Sets BL2SCAN.
:decode_bl2_hex
set "BL2SCAN=%TEMP%\h573_bl2_from_hex.bin"
powershell -NoProfile -Command "$p=$env:BL2HEX; $o=$env:BL2SCAN; $parts=New-Object System.Collections.Generic.List[byte]; foreach($line in [IO.File]::ReadAllLines($p)){ $l=$line.Trim(); if($l.Length -lt 11 -or $l[0] -ne [char]58){ continue }; $n=[Convert]::ToInt32($l.Substring(1,2),16); $t=[Convert]::ToInt32($l.Substring(7,2),16); if($t -eq 1){ break }; if($t -ne 0){ continue }; for($i=0; $i -lt $n; $i++){ $parts.Add([Convert]::ToByte($l.Substring(9+2*$i,2),16)) } }; [IO.File]::WriteAllBytes($o, $parts.ToArray())"
if errorlevel 1 (
    echo [FAIL] could not decode !BL2HEX!
    set "FAILED_STEP=decode bl2.hex"
    set "EXIT_CODE=1"
    exit /b 1
)
exit /b 0

:file_has
set "H5F4_SCAN_FILE=%~1"
set "H5F4_SCAN_TEXT=%~2"
:file_has_set
powershell -NoProfile -Command "exit ([int](-not [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($env:H5F4_SCAN_FILE)).Contains($env:H5F4_SCAN_TEXT)))"
exit /b

:flash_bin
set "STEP_ADDR=%~2"
set "STEP_DESC=%~3"
set "STEP_NAME=%~nx1"
echo ------------------------------------------------------------
echo Program %STEP_DESC%  %STEP_NAME%  %STEP_ADDR%
echo ------------------------------------------------------------
pushd "%~dp1"
STM32_Programmer_CLI %CONNECT_HP% -d %STEP_NAME% %STEP_ADDR% -v
set "DLRC=!ERRORLEVEL!"
popd
if not "!DLRC!"=="0" goto :flash_bin_fail
echo [ok]   %STEP_NAME%
echo.
exit /b 0
:flash_bin_fail
echo [FAIL] Program %STEP_NAME% failed.
set "FAILED_STEP=download %STEP_NAME%"
set "EXIT_CODE=1"
exit /b 1

:flash_hex
set "STEP_DESC=%~2"
set "STEP_NAME=%~nx1"
echo ------------------------------------------------------------
echo Program %STEP_DESC%  %STEP_NAME%
echo ------------------------------------------------------------
pushd "%~dp1"
STM32_Programmer_CLI %CONNECT_HP% -d %STEP_NAME% -v
set "DLRC=!ERRORLEVEL!"
popd
if not "!DLRC!"=="0" goto :flash_hex_fail
echo [ok]   %STEP_NAME%
echo.
exit /b 0
:flash_hex_fail
echo [FAIL] Program %STEP_NAME% failed.
set "FAILED_STEP=download %STEP_NAME%"
set "EXIT_CODE=1"
exit /b 1

:check_magic
set "RB_ADDR=%~1"
set "RB_NAME=%~2"
set "RB_FILE=%TEMP%\tfm_magic_%RB_NAME%.bin"
if exist "%RB_FILE%" del /f /q "%RB_FILE%"
STM32_Programmer_CLI %CONNECT_HP% -u %RB_ADDR% 0x10 "%RB_FILE%"
if errorlevel 1 goto :magic_read_fail
set "GOT="
for /f "usebackq delims=" %%M in (`powershell -NoProfile -Command "$b=[IO.File]::ReadAllBytes('%RB_FILE%'); '{0:x2}{1:x2}{2:x2}{3:x2}' -f $b[0],$b[1],$b[2],$b[3]"`) do set "GOT=%%M"
del /f /q "%RB_FILE%" >nul 2>&1
echo        %RB_NAME% first 4 bytes: !GOT!
if /I not "!GOT!"=="%MAGIC%" goto :magic_bad
echo [ok]   %RB_NAME% magic
exit /b 0
:magic_read_fail
echo [FAIL] Readback %RB_NAME% @ %RB_ADDR% failed.
set "FAILED_STEP=readback %RB_NAME%"
set "EXIT_CODE=1"
exit /b 1
:magic_bad
echo [FAIL] %RB_NAME% has no MCUboot magic (expected %MAGIC%).
set "FAILED_STEP=magic %RB_NAME%"
set "EXIT_CODE=1"
exit /b 1

:reject_release
echo [FAIL] This script programs test or prod only.
echo        Use flash_stm32h573_release.bat for release.
set "FAILED_STEP=release"
goto :fail_stop

:fail_not_dev
echo [FAIL] !BL2! has no "set wrp1".
echo        That file is not a test or prod BL2. Use flash_stm32h573_release.bat for release.
set "FAILED_STEP=BL2 marker"
goto :fail_stop

:fail_not_test
echo [FAIL] !BL2! has no "Starting bootloader".
echo        A test BL2 is built with INFO logs. This file looks like prod.
set "FAILED_STEP=test marker"
goto :fail_stop

:fail_not_prod
echo [FAIL] !BL2! contains "Starting bootloader".
echo        Prod is built with ERROR logs. Put the test image in test\.
set "FAILED_STEP=prod marker"
goto :fail_stop

:fail_sn
echo [FAIL] ST-LINK serial number given twice.
set "FAILED_STEP=serial"
goto :fail_stop

:fail_no_cli
echo [FAIL] STM32_Programmer_CLI.exe was not found.
set "FAILED_STEP=locate STM32_Programmer_CLI"
goto :fail_stop

:fail_no_s
echo [FAIL] Missing !SBIN!
echo        Run buildtfm.sh !IMAGE! first.
set "FAILED_STEP=missing tfm_s_signed.bin"
goto :fail_stop

:fail_no_ns
echo [FAIL] Missing !NSBIN!
set "FAILED_STEP=missing tfm_ns_signed.bin"
goto :fail_stop

:fail_no_bl2
echo [FAIL] Missing bl2.hex and bl2.bin
set "FAILED_STEP=missing BL2"
goto :fail_stop

rem If TrustZone is off, SECBOOT_LOCK reads 0x0. Enable TZEN=0xB4, then write 0xC3.
:write_secboot_unlock
STM32_Programmer_CLI %CONNECT_HP% -ob SECBOOT_LOCK=0xC3
if not errorlevel 1 exit /b 0
echo [info] SECBOOT_LOCK stayed 0x0. Enable TZEN=0xB4 and retry.
STM32_Programmer_CLI %CONNECT_HP% -ob TZEN=0xB4
if errorlevel 1 exit /b 1
STM32_Programmer_CLI %CONNECT_HP% -ob SECBOOT_LOCK=0xC3
exit /b %errorlevel%

:fail_regress
echo [FAIL] Option-byte regression failed.
set "FAILED_STEP=regression"
goto :fail_stop

:fail_boot_ube
echo [FAIL] Writing BOOT_UBE=0xB4 failed.
set "FAILED_STEP=BOOT_UBE"
goto :fail_stop

:fail_boot_lock
echo [FAIL] Writing SECBOOT_LOCK=0xC3 failed.
set "FAILED_STEP=SECBOOT_LOCK"
goto :fail_stop

:fail_protect
echo [FAIL] Writing SECWM2 / WRP / HDP1 failed.
set "FAILED_STEP=SECWM2 WRP HDP"
goto :fail_stop

:usage
echo.
echo flash_stm32h573.bat                              choose test or prod
echo flash_stm32h573.bat test [all^|download^|regression] [DIR] [SN]
echo flash_stm32h573.bat prod --dir D:\bins [SN]
echo No DIR uses test\ or prod\ next to this bat. DIR must contain the same bin names.
echo Release: flash_stm32h573_release.bat
echo.
set "FAILED_STEP=usage"
goto :fail_stop

:fail_stop
echo.
echo ============================================================
echo  Failed: %FAILED_STEP%
echo  log: %LOGFILE%
echo ============================================================
echo.
pause
goto :eof

:finish
echo.
pause
goto :eof
