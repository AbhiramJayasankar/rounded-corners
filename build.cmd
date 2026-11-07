@echo off
rem Build RoundedCorners.exe with MSVC, no C runtime.
rem Run from an "x64 Native Tools Command Prompt", or set VCVARS to your vcvars64.bat.
setlocal
where cl >nul 2>nul && goto build
if not defined VCVARS set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || exit /b 1
:build
cd /d "%~dp0"
rc /nologo RoundedCorners.rc || exit /b 1
cl /nologo /c /O1 /Os /GS- /Gs999999 /W4 /WX /Zl /DUNICODE /D_UNICODE RoundedCorners.c || exit /b 1
link /nologo /SUBSYSTEM:WINDOWS /ENTRY:Start /NODEFAULTLIB /MANIFEST:NO /DEBUG:NONE /OPT:REF /OPT:ICF ^
  /OUT:RoundedCorners.exe RoundedCorners.obj RoundedCorners.res ^
  kernel32.lib user32.lib gdi32.lib shell32.lib comctl32.lib || exit /b 1
del RoundedCorners.obj RoundedCorners.res
echo Built RoundedCorners.exe
