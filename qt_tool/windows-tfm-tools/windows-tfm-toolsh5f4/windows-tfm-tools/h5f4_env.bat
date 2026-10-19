@echo off
rem STM32H5F4 flash map, option-byte names, and CubeProgrammer PATH.
rem Linear script only: do not use internal subroutines. cmd.exe can pop
rem the parent's call stack (tfm_update.bat then exits before pause).
rem Do not use H573 values: WRPSGn1, SECWM ...=127, NS slot 0x0C088000.
set "H5F4_REV=h5f4-p256-prod-images"
set "H5F4_ADDR_BL2_S=0x0C00E000"
set "H5F4_ADDR_S_S=0x0C078000"
set "H5F4_ADDR_NS_S=0x0C0F8000"
set "H5F4_ADDR_BL2_NS=0x0800E000"
set "H5F4_ADDR_S_NS=0x08078000"
set "H5F4_ADDR_NS_NS=0x080F8000"
set "H5F4_SECWM_OPEN=SECWM1_STRT=255 SECWM1_END=0 SECWM2_STRT=255 SECWM2_END=0"
set "H5F4_SECWM_FULL=SECWM1_STRT=0 SECWM1_END=255 SECWM2_STRT=0 SECWM2_END=255"
set "H5F4_WRP=WRPSG11=0xffffffff WRPSG12=0xffffffff WRPSG21=0xffffffff WRPSG22=0xffffffff"
set "H5F4_HDP_OFF=HDP1_STRT=1 HDP1_END=0 HDP2_STRT=1 HDP2_END=0"
set "H5F4_OB1=SECBOOTADD=0xC0100 HDP1_STRT=1 HDP1_END=0 HDP2_STRT=1 HDP2_END=0 SWAP_BANK=0 SRAM2_RST=0 SRAM2_ECC=0"
set "H5F4_BOOT_UBE=BOOT_UBE=0xB4"
set "H5F4_PRODUCT_STATE=PRODUCT_STATE=0xED TZEN=0xB4"

if defined H5F4_PATH_DONE exit /b 0

set "H5F4_PF86=%ProgramFiles(x86)%"

if defined STM32_PROGRAMMER_CLI if exist "%STM32_PROGRAMMER_CLI%" (
    for %%I in ("%STM32_PROGRAMMER_CLI%") do set "PATH=%%~dpI;%PATH%"
    goto :h5f4_cube_done
)
if exist "D:\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" set "PATH=D:\STM32CubeProgrammer\bin;%PATH%"
if exist "%ProgramFiles%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" set "PATH=%ProgramFiles%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin;%PATH%"
if exist "%H5F4_PF86%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" set "PATH=%H5F4_PF86%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin;%PATH%"
if exist "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" set "PATH=C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin;%PATH%"
if exist "C:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" set "PATH=C:\ST\STM32CubeProgrammer\bin;%PATH%"
if exist "%ProgramFiles%\STMicroelectronics\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" set "PATH=%ProgramFiles%\STMicroelectronics\STM32CubeProgrammer\bin;%PATH%"
if exist "%LOCALAPPDATA%\Programs\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" set "PATH=%LOCALAPPDATA%\Programs\STM32CubeProgrammer\bin;%PATH%"
:h5f4_cube_done
for /d %%D in ("%ProgramFiles%\SEGGER\JLink*") do if exist "%%~D\JLinkARM.dll" set "PATH=%%~D;%PATH%"

set "H5F4_PATH_DONE=1"
exit /b 0
