@echo off
setlocal

for /f "tokens=1* delims==" %%I in ('wmic path win32_pnpentity get caption  /format:list ^| find "Arduino Leonardo"') do (
    call :resetCOM "%%~J"
)

:continue

:: wmic /format:list strips trailing spaces (at least for path win32_pnpentity)
for /f "tokens=1* delims==" %%I in ('wmic path win32_pnpentity get caption  /format:list ^| find "Arduino Leonardo bootloader"') do (
    call :setCOM "%%~J"
)

:: end main batch
goto :EOF

:resetCOM <WMIC_output_line>
:: sets _COM#=line
setlocal
set "str=%~1"
set "num=%str:*(COM=%"
set "num=%num:)=%"
set port=COM%num%
echo %port%
mode %port%: BAUD=1200 parity=N data=8 stop=1
goto :continue

:setCOM <WMIC_output_line>
:: sets _COM#=line
setlocal
set "str=%~1"
set "num=%str:*(COM=%"
set "num=%num:)=%"
set port=COM%num%
echo %port%
goto :flash

:flash
"c:\Program Files (x86)\Arduino\hardware\tools\avr\bin\avrdude.exe" -v -C"c:\Program Files (x86)\Arduino\hardware\tools\avr\etc\avrdude.conf" -patmega32u4 -cavr109 -P%port% -b57600 -D -V -Uflash:w:./Dual_Fueltank_level.ino.leonardo.hex:i
