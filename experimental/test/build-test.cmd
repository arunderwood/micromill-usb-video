@echo off
REM Build the 32-bit shim test harness (matches the 32-bit pxc2_nt.dll shim).
setlocal
if "%VS%"=="" set "VS=C:\Program Files\Microsoft Visual Studio\2022\Community"
call "%VS%\VC\Auxiliary\Build\vcvars32.bat" >nul || (echo ERROR: could not find VS2022 x86 toolchain at "%VS%" & exit /b 1)
if not exist build mkdir build
cl /nologo /EHsc /W3 /O2 shimtest.cpp /Fe:build\shimtest.exe /Fo:build\ || exit /b 1
echo Built build\shimtest.exe
endlocal
