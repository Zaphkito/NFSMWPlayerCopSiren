@echo off
setlocal
cd /d "%~dp0"
where cl >nul 2>nul
if errorlevel 1 (
  echo Open an x86 Native Tools Command Prompt for Visual Studio and run build.cmd.
  exit /b 1
)
if not exist build mkdir build
if errorlevel 1 exit /b 1
cl /nologo /LD /O2 /GS- /Zl /W4 NFSMWPlayerCopSiren.cpp /Fobuild\NFSMWPlayerCopSiren.obj /link /NODEFAULTLIB /ENTRY:DllMain /MACHINE:X86 /DYNAMICBASE /NXCOMPAT kernel32.lib user32.lib /OUT:build\NFSMWPlayerCopSiren.asi /IMPLIB:build\NFSMWPlayerCopSiren.lib
if errorlevel 1 exit /b 1
copy /y NFSMWPlayerCopSiren.ini build\NFSMWPlayerCopSiren.ini >nul
exit /b %errorlevel%
