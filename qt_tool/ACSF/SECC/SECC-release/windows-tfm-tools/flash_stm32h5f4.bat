@echo off
rem STM32H5F4 test/prod flash. Same steps as flash_stm32h5f4.sh.
rem Images come only from the test\ or prod\ folder next to this file.
rem Release is flash_stm32h5f4_release.bat and reads only release\.
rem This file does not set Boot lock, release WRP, or HDP over BL2,
rem and it does not write PRODUCT_STATE=0x72.
rem
rem   flash_stm32h5f4.bat                       choose test or prod
rem   flash_stm32h5f4.bat test                  flash test\
rem   flash_stm32h5f4.bat prod                  flash prod\
rem   flash_stm32h5f4.bat test erase [SN]       mass-erase, then program
rem   flash_stm32h5f4.bat prod unlock-only [SN]
rem
rem Addresses: BL2 0x0C00E000  S 0x0C078000  NS 0x0C0F8000
rem Does not reset when finished.
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
set "MODE=unlock"
set "IMAGE="
set "SN="
rem External image directory. Empty means test\ or prod\ next to this bat.
set "IMGDIR_EXT="
goto :parse_args
:args_done
if not defined IMAGE (
    echo Select image:
    echo   1. test  %~dp0test
    echo   2. prod  %~dp0prod
    set /p CHOICE=Enter 1 or 2: 
    if "!CHOICE!"=="1" set "IMAGE=test"
    if /i "!CHOICE!"=="test" set "IMAGE=test"
    if "!CHOICE!"=="2" set "IMAGE=prod"
    if /i "!CHOICE!"=="prod" set "IMAGE=prod"
)
if not defined IMAGE (
    echo [FAIL] choose test or prod
    set "FAILED_STEP=image"
    set "EXIT_CODE=1"
    goto :finish
)
rem Use the external directory when one was given. Otherwise test\ or prod\ beside this bat.
if defined IMGDIR_EXT (
    set "BL2=!IMGDIR_EXT!\bl2.bin"
    set "BL2HEX=!IMGDIR_EXT!\bl2.hex"
    set "SBIN=!IMGDIR_EXT!\tfm_s_signed.bin"
    set "NSBIN=!IMGDIR_EXT!\tfm_ns_signed.bin"
) else (
    set "BL2=%~dp0!IMAGE!\bl2.bin"
    set "BL2HEX=%~dp0!IMAGE!\bl2.hex"
    set "SBIN=%~dp0!IMAGE!\tfm_s_signed.bin"
    set "NSBIN=%~dp0!IMAGE!\tfm_ns_signed.bin"
)
if not exist "!SBIN!" (
    echo [FAIL] missing !SBIN!
    set "FAILED_STEP=missing tfm_s_signed.bin"
    set "EXIT_CODE=1"
    goto :finish
)
if not exist "!NSBIN!" (
    echo [FAIL] missing !NSBIN!
    set "FAILED_STEP=missing tfm_ns_signed.bin"
    set "EXIT_CODE=1"
    goto :finish
)
rem Flash bl2.hex when it is present. Otherwise flash bl2.bin. Markers come from that file.
if exist "!BL2HEX!" (
    call :decode_bl2_hex
    if errorlevel 1 goto :finish
) else if exist "!BL2!" (
    set "BL2SCAN=!BL2!"
) else (
    echo [FAIL] missing bl2.hex and bl2.bin in the image directory
    set "FAILED_STEP=missing BL2"
    set "EXIT_CODE=1"
    goto :finish
)
call :file_has "!BL2SCAN!" "H5F4BL2"
if errorlevel 1 (
    echo [FAIL] BL2 has no H5F4BL2 marker
    set "FAILED_STEP=H5F4BL2"
    set "EXIT_CODE=1"
    goto :finish
)
call :file_has "!BL2SCAN!" "H5F4SWP2"
if errorlevel 1 (
    echo [FAIL] BL2 has no H5F4SWP2 marker
    set "FAILED_STEP=H5F4SWP2"
    set "EXIT_CODE=1"
    goto :finish
)
call :file_has "!BL2SCAN!" "Unexpected value for write protection"
if not errorlevel 1 (
    echo [FAIL] BL2 is a release image. Use flash_stm32h5f4_release.bat
    set "FAILED_STEP=release BL2"
    set "EXIT_CODE=1"
    goto :finish
)
set "H5F4_SCAN_FILE=!BL2SCAN!"
set "H5F4_SCAN_TEXT=Starting bootloader S-sec="
call :file_has_set
set "SSEC=!ERRORLEVEL!"
if /i "!IMAGE!"=="test" (
    if "!SSEC!"=="1" (
        echo [FAIL] BL2 has no S-sec marker. Build with buildtfm.sh test
        set "FAILED_STEP=S-sec"
        set "EXIT_CODE=1"
        goto :finish
    )
) else (
    if "!SSEC!"=="0" (
        echo [FAIL] BL2 has the test S-sec marker. Refusing to flash it as prod
        set "FAILED_STEP=test BL2"
        set "EXIT_CODE=1"
        goto :finish
    )
)

call :find_cli
if errorlevel 1 goto :finish
set "CONNECT=-c port=SWD ap=1 mode=UR"
if defined SN set "CONNECT=-c port=SWD ap=1 sn=!SN! mode=UR"

echo.
echo STM32H5F4 test/prod flash  mode=!MODE!
rem Program bl2.hex when it exists. Otherwise program bl2.bin.
if exist "!BL2HEX!" (
    echo BL2  !BL2HEX!  addresses and OTP inside the hex
) else (
    echo BL2  !BL2!  0x0C00E000
)
echo S    !SBIN!   0x0C078000
echo NS   !NSBIN!  0x0C0F8000
echo.

if /i "!MODE!"=="erase" goto :do_erase
call :hdp_overlaps
if errorlevel 3 goto :hdp_on
if errorlevel 2 goto :finish
echo [info] HDP does not cover BL2. Unlock WRP only.
call :unlock_wrp
if errorlevel 1 goto :finish
goto :after_unlock
:hdp_on
echo [info] HDP still covers BL2 at 0x0C00E000.
if /i "!MODE!"=="unlock-only" (
    echo [FAIL] HDP covers BL2. Run: flash_stm32h5f4.bat !IMAGE! erase
    set "FAILED_STEP=HDP covers BL2"
    set "EXIT_CODE=1"
    goto :finish
)
:do_erase
call :mass_erase
if errorlevel 1 goto :finish
:after_unlock
echo [ob] BOOT_UBE=0xB4
set "CLI_ARGS=!CONNECT! -ob BOOT_UBE=0xB4"
call :run_ob
if errorlevel 1 goto :finish

echo [flash] S
call :flash_bin "!SBIN!" 0x0C078000
if errorlevel 1 goto :finish
echo [flash] NS
call :flash_bin "!NSBIN!" 0x0C0F8000
if errorlevel 1 goto :finish
echo [ob] HDP off and WRP clear before BL2
call :unlock_wrp
if errorlevel 1 goto :finish
echo [flash] BL2
rem Prefer bl2.hex. Its addresses cover BL2, OTP at 0x0C028000, and the counter.
if exist "!BL2HEX!" (
    call :flash_hex "!BL2HEX!"
) else (
    call :flash_bin "!BL2!" 0x0C00E000
)
if errorlevel 1 (
    echo [FAIL] BL2 verify failed. If the address is 0x0C010000, HDP still covers BL2.
    echo        Run: flash_stm32h5f4.bat !IMAGE! erase
    goto :finish
)
echo [read] BL2 must contain H5F4BL2
set "DUMP=%TEMP%\h5f4_bl2_onchip.bin"
"%CLI%" !CONNECT! -r 0x0C00E000 0x20000 "!DUMP!"
if errorlevel 1 (
    echo [FAIL] BL2 readback failed
    set "FAILED_STEP=readback BL2"
    set "EXIT_CODE=1"
    goto :finish
)
call :file_has "!DUMP!" "H5F4BL2"
if errorlevel 1 (
    echo [FAIL] on-chip BL2 has no H5F4BL2
    set "FAILED_STEP=on-chip H5F4BL2"
    set "EXIT_CODE=1"
    goto :finish
)
echo.
echo On-chip BL2 is the new image. This script does not reset.
echo UART USART6 PC6/PC7 115200 must show H5F4BL2.
echo This script does not write SECBOOT_LOCK, release WRP, or HDP1 [0, 19].
goto :finish

:find_cli
set "CLI="
where STM32_Programmer_CLI >nul 2>&1 && set "CLI=STM32_Programmer_CLI"
if not defined CLI if exist "D:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" set "CLI=D:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
if not defined CLI if exist "%ProgramFiles%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" set "CLI=%ProgramFiles%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
if not defined CLI if exist "%ProgramFiles(x86)%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" set "CLI=%ProgramFiles(x86)%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
if not defined CLI (
    echo [FAIL] STM32_Programmer_CLI not found
    set "FAILED_STEP=STM32_Programmer_CLI"
    set "EXIT_CODE=1"
    exit /b 1
)
echo [ok] !CLI!
exit /b 0

:run_ob
echo CMD: "%CLI%" !CLI_ARGS!
"%CLI%" !CLI_ARGS!
if errorlevel 1 (
    echo [FAIL] option bytes: !CLI_ARGS!
    set "FAILED_STEP=option bytes"
    set "EXIT_CODE=1"
    exit /b 1
)
exit /b 0

:unlock_wrp
set "CLI_ARGS=!CONNECT! -ob HDP1_STRT=1 HDP1_END=0 HDP2_STRT=1 HDP2_END=0"
call :run_ob
if errorlevel 1 exit /b 1
set "CLI_ARGS=!CONNECT! -ob WRPSG11=0xffffffff WRPSG12=0xffffffff WRPSG21=0xffffffff WRPSG22=0xffffffff"
call :run_ob
exit /b %ERRORLEVEL%

:mass_erase
echo [erase] PRODUCT_STATE=0xED TZEN=0xB4
set "CLI_ARGS=!CONNECT! -ob PRODUCT_STATE=0xED TZEN=0xB4"
call :run_ob
if errorlevel 1 exit /b 1
echo [erase] SECBOOT_LOCK=0xC3
set "CLI_ARGS=!CONNECT! -ob SECBOOT_LOCK=0xC3"
call :run_ob
if errorlevel 1 exit /b 1
echo [erase] bank1 protect off and mass erase
set "CLI_ARGS=!CONNECT! -ob SECWM1_STRT=255 SECWM1_END=0 WRPSG11=0xffffffff WRPSG12=0xffffffff -e all"
call :run_ob
if errorlevel 1 exit /b 1
echo [erase] bank2 protect off and mass erase
set "CLI_ARGS=!CONNECT! -ob SECWM2_STRT=255 SECWM2_END=0 WRPSG21=0xffffffff WRPSG22=0xffffffff -e all"
call :run_ob
if errorlevel 1 exit /b 1
echo [erase] HDP off
set "CLI_ARGS=!CONNECT! -ob HDP1_STRT=1 HDP1_END=0 HDP2_STRT=1 HDP2_END=0"
call :run_ob
if errorlevel 1 exit /b 1
echo [erase] SECBOOTADD=0xC0100
set "CLI_ARGS=!CONNECT! -ob SECBOOTADD=0xC0100 HDP1_STRT=1 HDP1_END=0 HDP2_STRT=1 HDP2_END=0 SWAP_BANK=0 SRAM2_RST=0 SRAM2_ECC=0"
call :run_ob
if errorlevel 1 exit /b 1
echo [erase] watermarks full [0, 255]
set "CLI_ARGS=!CONNECT! -ob SECWM1_STRT=0 SECWM1_END=255 SECWM2_STRT=0 SECWM2_END=255"
call :run_ob
exit /b %ERRORLEVEL%

:hdp_overlaps
set "OBDUMP=%TEMP%\h5f4_ob.txt"
"%CLI%" !CONNECT! -ob displ > "!OBDUMP!" 2>&1
if errorlevel 1 (
    echo [FAIL] could not read option bytes
    set "FAILED_STEP=read option bytes"
    set "EXIT_CODE=1"
    exit /b 2
)
powershell -NoProfile -Command "$t=Get-Content -Raw '!OBDUMP!'; if($t -notmatch 'HDP1_STRT\s*:\s*(0x[0-9A-Fa-f]+)'){exit 2}; $s=[Convert]::ToInt32($Matches[1],16); if($t -notmatch 'HDP1_END\s*:\s*(0x[0-9A-Fa-f]+)'){exit 2}; $e=[Convert]::ToInt32($Matches[1],16); if($s -gt $e){exit 0}; if($s -le 23 -and $e -ge 7){exit 3}; exit 0"
set "HDRC=!ERRORLEVEL!"
if "!HDRC!"=="2" (
    echo [FAIL] could not parse HDP1 from option bytes
    set "FAILED_STEP=parse HDP1"
    set "EXIT_CODE=1"
)
exit /b !HDRC!

rem Decode Intel hex data bytes so marker text can be searched. Sets BL2SCAN.
:decode_bl2_hex
set "BL2SCAN=%TEMP%\h5f4_bl2_from_hex.bin"
powershell -NoProfile -Command "$p=$env:BL2HEX; $o=$env:BL2SCAN; $parts=New-Object System.Collections.Generic.List[byte]; foreach($line in [IO.File]::ReadAllLines($p)){ $l=$line.Trim(); if($l.Length -lt 11 -or $l[0] -ne [char]58){ continue }; $n=[Convert]::ToInt32($l.Substring(1,2),16); $t=[Convert]::ToInt32($l.Substring(7,2),16); if($t -eq 1){ break }; if($t -ne 0){ continue }; for($i=0; $i -lt $n; $i++){ $parts.Add([Convert]::ToByte($l.Substring(9+2*$i,2),16)) } }; [IO.File]::WriteAllBytes($o, $parts.ToArray())"
if errorlevel 1 (
    echo [FAIL] could not decode !BL2HEX!
    set "FAILED_STEP=decode bl2.hex"
    set "EXIT_CODE=1"
    exit /b 1
)
exit /b 0

:flash_bin
echo CMD: "%CLI%" !CONNECT! -d "%~1" %~2 -v
"%CLI%" !CONNECT! -d "%~1" %~2 -v
if errorlevel 1 (
    echo [FAIL] download %~nx1
    set "FAILED_STEP=download %~nx1"
    set "EXIT_CODE=1"
    exit /b 1
)
echo [ok] %~nx1
exit /b 0

rem Program an Intel hex. Addresses are inside the file, so no start address is passed.
:flash_hex
echo CMD: "%CLI%" !CONNECT! -d "%~1" -v
"%CLI%" !CONNECT! -d "%~1" -v
if errorlevel 1 (
    echo [FAIL] download %~nx1
    set "FAILED_STEP=download %~nx1"
    set "EXIT_CODE=1"
    exit /b 1
)
echo [ok] %~nx1
exit /b 0

:file_has
rem Line search stops at the first NUL. H5F4BL2 in bl2.bin is after those NULs.
set "H5F4_SCAN_FILE=%~1"
set "H5F4_SCAN_TEXT=%~2"
:file_has_set
powershell -NoProfile -Command "exit ([int](-not [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($env:H5F4_SCAN_FILE)).Contains($env:H5F4_SCAN_TEXT)))"
exit /b !ERRORLEVEL!

:parse_args
if "%~1"=="" goto :args_done
if /i "%~1"=="-h" goto :usage
if /i "%~1"=="/?" goto :usage
if /i "%~1"=="help" goto :usage
if /i "%~1"=="test" goto :got_test
if /i "%~1"=="prod" goto :got_prod
if /i "%~1"=="erase" goto :got_erase
if /i "%~1"=="unlock" goto :got_unlock
if /i "%~1"=="unlock-only" goto :got_unlock_only
if /i "%~1"=="release" goto :reject_release
rem --dir takes the next argument as the external image directory.
if /i "%~1"=="--dir" goto :got_dir_flag
if /i "%~1"=="-d" goto :got_dir_flag
rem An existing directory is the external image path. Same filenames as test\ or prod\.
if exist "%~1\" (
    if defined IMGDIR_EXT (
        echo [FAIL] Image directory given twice: %~1
        set "FAILED_STEP=directory"
        set "EXIT_CODE=1"
        goto :finish
    )
    set "IMGDIR_EXT=%~f1"
    shift
    goto :parse_args
)
if not defined SN goto :got_sn
echo Unknown argument: %~1
set "FAILED_STEP=argument"
set "EXIT_CODE=1"
goto :finish
:got_test
set "IMAGE=test"
shift
goto :parse_args
:got_prod
set "IMAGE=prod"
shift
goto :parse_args
:got_erase
set "MODE=erase"
shift
goto :parse_args
:got_unlock
set "MODE=unlock"
shift
goto :parse_args
:got_unlock_only
set "MODE=unlock-only"
shift
goto :parse_args
:got_dir_flag
if "%~2"=="" (
    echo [FAIL] --dir needs a directory
    set "FAILED_STEP=directory"
    set "EXIT_CODE=1"
    goto :finish
)
if not exist "%~2\" (
    echo [FAIL] Image directory not found: %~2
    set "FAILED_STEP=directory"
    set "EXIT_CODE=1"
    goto :finish
)
if defined IMGDIR_EXT (
    echo [FAIL] Image directory given twice: %~2
    set "FAILED_STEP=directory"
    set "EXIT_CODE=1"
    goto :finish
)
set "IMGDIR_EXT=%~f2"
shift
shift
goto :parse_args
:got_sn
set "SN=%~1"
shift
goto :parse_args
:reject_release
echo [FAIL] Release images use flash_stm32h5f4_release.bat. This script flashes test or prod.
set "FAILED_STEP=release"
set "EXIT_CODE=1"
goto :finish

:usage
echo Usage:
echo   flash_stm32h5f4.bat                       choose test or prod
echo   flash_stm32h5f4.bat test [erase^|unlock-only] [DIR] [SN]
echo   flash_stm32h5f4.bat prod --dir D:\bins [SN]
echo No DIR uses test\ or prod\ next to this file. DIR must contain the same bin names.
echo Release: flash_stm32h5f4_release.bat
echo Does not write PRODUCT_STATE=0x72.
if defined H5F4_INNER endlocal & exit /b 0
exit /b 0

:finish
echo.
if not "!EXIT_CODE!"=="0" (
    echo FAILED at: !FAILED_STEP!
)
if defined H5F4_INNER endlocal & exit /b %EXIT_CODE%
endlocal & exit /b %EXIT_CODE%
