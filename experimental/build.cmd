@echo off
REM Build the EARLIER/SUPERSEDED 32-bit pxc2_nt.dll capture shim (reference only;
REM the shipped solution is the GDI PXC200_Support.dll in ..\src).
REM Output: build\pxc2_nt.dll. Pure user-mode; no signing needed to run.
setlocal
if "%VS%"=="" set "VS=C:\Program Files\Microsoft Visual Studio\2022\Community"
call "%VS%\VC\Auxiliary\Build\vcvars32.bat" >nul || (echo ERROR: could not find VS2022 x86 toolchain at "%VS%" & exit /b 1)

if not exist build mkdir build
cl /nologo /EHsc /W3 /O2 /I..\src /DWIN32 /D_WINDOWS /DUNICODE /D_UNICODE ^
   shim.cpp ..\src\capture.cpp ..\src\config.cpp ..\src\convert.cpp ^
   /Fo:build\ ^
   /link /DLL /DEF:pxc2_nt.def /OUT:build\pxc2_nt.dll ^
   mfplat.lib mf.lib mfreadwrite.lib mfuuid.lib ole32.lib shlwapi.lib kernel32.lib || exit /b 1

echo.
echo === exports ===
dumpbin /nologo /exports build\pxc2_nt.dll | findstr /i "OpenLibrary CloseLibrary"
echo === machine (want x86) ===
dumpbin /nologo /headers build\pxc2_nt.dll | findstr /i "machine"
endlocal
