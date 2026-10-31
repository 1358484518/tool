@echo off
rem STM32H5F4 release flash. Same steps as flash_stm32h5f4_release.sh.
rem Images come from the release\ folder next to this file unless a directory is given.
rem Does not write PRODUCT_STATE=0x72. Option bytes use Under Reset.
rem
rem   flash_stm32h5f4_release.bat                 regression + program + Boot lock 0xB4
rem   flash_stm32h5f4_release.bat all [SN]
rem   flash_stm32h5f4_release.bat download [DIR] [SN]   program, then lock
rem   flash_stm32h5f4_release.bat --dir D:\bins
rem   flash_stm32h5f4_release.bat regression [SN] mass-erase, SECBOOT_LOCK back to 0xC3
rem
rem After the images:
rem   SECBOOTADD=0xC0100
rem   SECWM1 [0, 123]  SECWM2 [0, 63]
rem   WRPSG11=0xFFFFFFE3  HDP1 [0, 19]  SECBOOT_LOCK=0xB4
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
set "MODE=all"
set "SN="
rem External image directory. Empty means release\ next to this bat.
set "IMGDIR_EXT="
:parse_args
if "%~1"=="" goto :args_done
if /i "%~1"=="-h" goto :usage
if /i "%~1"=="/?" goto :usage
if /i "%~1"=="help" goto :usage
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
if /i "%~1"=="all" (
    set "MODE=all"
    shift
    goto :parse_args
)
if /i "%~1"=="--dir" goto :got_dir_flag
if /i "%~1"=="-d" goto :got_dir_flag
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
    if defined SN (
        echo [FAIL] ST-LINK serial given twice
        goto :usage
    )
    set "SN=%~1"
    shift
    goto :parse_args
)
echo Unknown argument: %~1
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

call :find_cli
if errorlevel 1 goto :finish
set "CONNECT=-c port=SWD ap=1 mode=UR"
if defined SN set "CONNECT=-c port=SWD ap=1 sn=!SN! mode=UR"

echo.
echo STM32H5F4 release  mode=!MODE!
echo.

if /i "!MODE!"=="download" goto :do_download
call :mass_erase
if errorlevel 1 goto :finish
echo [ob] BOOT_UBE=0xB4 SECBOOTADD=0xC0100
set "CLI_ARGS=!CONNECT! -ob BOOT_UBE=0xB4 SECBOOTADD=0xC0100"
call :run_ob
if errorlevel 1 goto :finish
if /i "!MODE!"=="regression" (
    echo Regression done. SECBOOT_LOCK is 0xC3. Images were not programmed.
    goto :finish
)
:do_download
call :need_images
if errorlevel 1 goto :finish
if /i "!MODE!"=="download" (
    call :hdp_overlaps
    if errorlevel 3 (
        echo [FAIL] HDP still covers BL2. Run flash_stm32h5f4_release.bat with no arguments.
        set "FAILED_STEP=HDP covers BL2"
        set "EXIT_CODE=1"
        goto :finish
    )
    if errorlevel 2 goto :finish
)
echo [ob] BOOT_UBE=0xB4 SECBOOTADD=0xC0100
set "CLI_ARGS=!CONNECT! -ob BOOT_UBE=0xB4 SECBOOTADD=0xC0100"
call :run_ob
if errorlevel 1 goto :finish
echo [flash] S  0x0C078000
call :flash_bin "!SBIN!" 0x0C078000
if errorlevel 1 goto :finish
echo [flash] NS 0x0C0F8000
call :flash_bin "!NSBIN!" 0x0C0F8000
if errorlevel 1 goto :finish
echo [flash] BL2
rem Prefer bl2.hex. Its addresses cover BL2, OTP at 0x0C028000, and the counter.
if exist "!BL2HEX!" (
    call :flash_hex "!BL2HEX!"
) else (
    call :flash_bin "!BL2!" 0x0C00E000
)
if errorlevel 1 goto :finish
call :check_magic 0x0C078000 S
if errorlevel 1 goto :finish
call :check_magic 0x0C0F8000 NS
if errorlevel 1 goto :finish
set "DUMP=%TEMP%\h5f4_rel_bl2.bin"
echo [read] BL2 before HDP
"%CLI%" !CONNECT! -u 0x0C00E000 0x20000 "!DUMP!"
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
call :file_has "!DUMP!" "Unexpected value for write protection"
if errorlevel 1 (
    echo [FAIL] on-chip BL2 has no release write-protection check
    set "FAILED_STEP=on-chip release BL2"
    set "EXIT_CODE=1"
    goto :finish
)
set "H5F4_SCAN_FILE=!DUMP!"
set "H5F4_SCAN_TEXT=Starting bootloader S-sec="
call :file_has_set
set "SSEC=!ERRORLEVEL!"
if "!SSEC!"=="0" (
    echo [FAIL] on-chip BL2 has the test S-sec marker
    set "FAILED_STEP=test BL2 on chip"
    set "EXIT_CODE=1"
    goto :finish
)
echo [lock] SECWM1 [0, 123]  SECWM2 [0, 63]
set "CLI_ARGS=!CONNECT! -ob SECWM1_STRT=0 SECWM1_END=123 SECWM2_STRT=0 SECWM2_END=63"
call :run_ob
if errorlevel 1 goto :finish
echo [lock] WRPSG11=0xFFFFFFE3
set "CLI_ARGS=!CONNECT! -ob WRPSG11=0xFFFFFFE3"
call :run_ob
if errorlevel 1 goto :finish
echo [lock] HDP1 [0, 19]
set "CLI_ARGS=!CONNECT! -ob HDP1_STRT=0 HDP1_END=19"
call :run_ob
if errorlevel 1 goto :finish
echo [lock] SECBOOT_LOCK=0xB4
set "CLI_ARGS=!CONNECT! -ob SECBOOT_LOCK=0xB4"
call :run_ob
if errorlevel 1 goto :finish
echo.
echo Release download done. No reset.
echo PRODUCT_STATE is left Open. Release BL2 reset-loops until it is Closed.
echo This script does not write PRODUCT_STATE=0x72.
echo Boot lock is 0xB4. Flash test/prod only after flash_stm32h5f4_release.bat regression.
echo UART USART6 PC6/PC7 115200. Release log is H5F4BL2 only.
goto :finish

:need_images
rem Use the external directory when one was given. Otherwise release\ beside this bat.
if defined IMGDIR_EXT (
    set "BL2=!IMGDIR_EXT!\bl2.bin"
    set "BL2HEX=!IMGDIR_EXT!\bl2.hex"
    set "SBIN=!IMGDIR_EXT!\tfm_s_signed.bin"
    set "NSBIN=!IMGDIR_EXT!\tfm_ns_signed.bin"
) else (
    set "BL2=%~dp0release\bl2.bin"
    set "BL2HEX=%~dp0release\bl2.hex"
    set "SBIN=%~dp0release\tfm_s_signed.bin"
    set "NSBIN=%~dp0release\tfm_ns_signed.bin"
)
if not exist "!SBIN!" (
    echo [FAIL] missing !SBIN!
    set "FAILED_STEP=missing tfm_s_signed.bin"
    set "EXIT_CODE=1"
    exit /b 1
)
if not exist "!NSBIN!" (
    echo [FAIL] missing !NSBIN!
    set "FAILED_STEP=missing tfm_ns_signed.bin"
    set "EXIT_CODE=1"
    exit /b 1
)
rem Flash bl2.hex when it is present. Otherwise flash bl2.bin. Markers come from that file.
if exist "!BL2HEX!" (
    call :decode_bl2_hex
    if errorlevel 1 exit /b 1
) else if exist "!BL2!" (
    set "BL2SCAN=!BL2!"
) else (
    echo [FAIL] missing bl2.hex and bl2.bin in the image directory
    set "FAILED_STEP=missing BL2"
    set "EXIT_CODE=1"
    exit /b 1
)
call :file_has "!BL2SCAN!" "H5F4BL2"
if errorlevel 1 (
    echo [FAIL] BL2 has no H5F4BL2 marker
    set "FAILED_STEP=H5F4BL2"
    set "EXIT_CODE=1"
    exit /b 1
)
call :file_has "!BL2SCAN!" "Unexpected value for write protection"
if errorlevel 1 (
    echo [FAIL] BL2 has no release write-protection check. Build with buildtfm.sh release.
    set "FAILED_STEP=not release BL2"
    set "EXIT_CODE=1"
    exit /b 1
)
set "H5F4_SCAN_FILE=!BL2SCAN!"
set "H5F4_SCAN_TEXT=Starting bootloader S-sec="
call :file_has_set
set "SSEC=!ERRORLEVEL!"
if "!SSEC!"=="0" (
    echo [FAIL] BL2 has the test S-sec marker
    set "FAILED_STEP=test BL2"
    set "EXIT_CODE=1"
    exit /b 1
)
if exist "!BL2HEX!" (
    echo [ok] !BL2HEX!
) else (
    echo [ok] !BL2!
)
exit /b 0

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

:mass_erase
echo [regression] PRODUCT_STATE=0xED TZEN=0xB4
set "CLI_ARGS=!CONNECT! -ob PRODUCT_STATE=0xED TZEN=0xB4"
call :run_ob
if errorlevel 1 exit /b 1
echo [regression] SECBOOT_LOCK=0xC3
set "CLI_ARGS=!CONNECT! -ob SECBOOT_LOCK=0xC3"
call :run_ob
if errorlevel 1 exit /b 1
echo [regression] bank1 protect off and mass erase
set "CLI_ARGS=!CONNECT! -ob SECWM1_STRT=255 SECWM1_END=0 WRPSG11=0xffffffff WRPSG12=0xffffffff -e all"
call :run_ob
if errorlevel 1 exit /b 1
echo [regression] bank2 protect off and mass erase
set "CLI_ARGS=!CONNECT! -ob SECWM2_STRT=255 SECWM2_END=0 WRPSG21=0xffffffff WRPSG22=0xffffffff -e all"
call :run_ob
if errorlevel 1 exit /b 1
echo [regression] HDP off
set "CLI_ARGS=!CONNECT! -ob HDP1_STRT=1 HDP1_END=0 HDP2_STRT=1 HDP2_END=0"
call :run_ob
if errorlevel 1 exit /b 1
echo [regression] SECBOOTADD=0xC0100
set "CLI_ARGS=!CONNECT! -ob SECBOOTADD=0xC0100 HDP1_STRT=1 HDP1_END=0 HDP2_STRT=1 HDP2_END=0 SWAP_BANK=0 SRAM2_RST=0 SRAM2_ECC=0"
call :run_ob
if errorlevel 1 exit /b 1
echo [regression] watermarks full [0, 255] until the release lock
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

:check_magic
set "RB=%TEMP%\h5f4_magic_%~2.bin"
"%CLI%" !CONNECT! -u %~1 0x10 "!RB!"
if errorlevel 1 (
    echo [FAIL] readback %~2
    set "FAILED_STEP=readback %~2"
    set "EXIT_CODE=1"
    exit /b 1
)
set "GOT="
for /f "usebackq delims=" %%M in (`powershell -NoProfile -Command "$b=[IO.File]::ReadAllBytes('!RB!'); '{0:x2}{1:x2}{2:x2}{3:x2}' -f $b[0],$b[1],$b[2],$b[3]"`) do set "GOT=%%M"
echo        %~2 first 4 bytes: !GOT!
if /I not "!GOT!"=="3db8f396" (
    echo [FAIL] %~2 has no MCUboot magic
    set "FAILED_STEP=magic %~2"
    set "EXIT_CODE=1"
    exit /b 1
)
echo [ok] %~2 magic
exit /b 0

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

:usage
echo Usage:
echo   flash_stm32h5f4_release.bat                 regression + program + lock
echo   flash_stm32h5f4_release.bat download [DIR] [SN]
echo   flash_stm32h5f4_release.bat --dir D:\bins
echo   flash_stm32h5f4_release.bat regression [SN]
echo No DIR uses release\ next to this bat. DIR must contain the same bin names.
echo Does not write PRODUCT_STATE=0x72.
echo BL2 0x0C00E000  S 0x0C078000  NS 0x0C0F8000
if defined H5F4_INNER endlocal & exit /b 0
exit /b 0

:finish
echo.
if not "!EXIT_CODE!"=="0" (
    echo FAILED at: !FAILED_STEP!
)
if defined H5F4_INNER endlocal & exit /b %EXIT_CODE%
endlocal & exit /b %EXIT_CODE%
