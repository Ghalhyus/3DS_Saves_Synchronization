@echo off
rem Compile PokeBankSync.3dsx avec devkitPro (installe dans C:\devkitPro). Arguments passes a make : compiler.cmd clean
set DEVKITPRO=/opt/devkitpro
set DEVKITARM=/opt/devkitpro/devkitARM
C:\devkitPro\msys2\usr\bin\bash.exe -lc "cd \"$(cygpath -u '%~dp0.')\" && make %*"
