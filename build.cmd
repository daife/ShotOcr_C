@echo off
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT (
  echo MSVC x64 not found. Install Visual Studio Build Tools with Desktop development with C++.
  exit /b 1
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /std:c++17 /O1 /MT /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN src\main.cpp src\service.cpp /Fo:build\ /Fe:build\ShotOcr.exe /link /SUBSYSTEM:WINDOWS /MACHINE:X64 /OPT:REF /OPT:ICF user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib windowscodecs.lib winhttp.lib crypt32.lib winmm.lib msimg32.lib uiautomationcore.lib
exit /b %errorlevel%
