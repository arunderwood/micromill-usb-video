@echo off
REM Build the 32-bit GDI PXC200_Support.dll replacement (the shipped solution).
REM Output: build\PXC200_Support.dll. Pure user-mode; no driver, no signing.
REM
REM Override the VS install location if yours differs:
REM   set VS=C:\Program Files\Microsoft Visual Studio\2022\Professional
setlocal
if "%VS%"=="" set "VS=C:\Program Files\Microsoft Visual Studio\2022\Community"
call "%VS%\VC\Auxiliary\Build\vcvars32.bat" >nul || (echo ERROR: could not find VS2022 x86 toolchain at "%VS%" & exit /b 1)

if not exist build mkdir build
cl /nologo /EHsc /W3 /O2 /DWIN32 /D_WINDOWS /DUNICODE /D_UNICODE ^
   pxcsupport.cpp capture.cpp config.cpp convert.cpp ^
   /Fo:build\ ^
   /link /DLL /DEF:pxcsupport.def /OUT:build\PXC200_Support.dll ^
   mfplat.lib mf.lib mfreadwrite.lib mfuuid.lib strmiids.lib ole32.lib shlwapi.lib ^
   gdi32.lib user32.lib kernel32.lib || exit /b 1

echo.
echo === exports ===
dumpbin /nologo /exports build\PXC200_Support.dll | findstr /i "PXC_"
echo === machine (want x86) ===
dumpbin /nologo /headers build\PXC200_Support.dll | findstr /i "machine"
endlocal
