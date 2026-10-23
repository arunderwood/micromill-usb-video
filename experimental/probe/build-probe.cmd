@echo off
REM Build the Media Foundation capture probe (x64 console diagnostic tool).
setlocal
if "%VS%"=="" set "VS=C:\Program Files\Microsoft Visual Studio\2022\Community"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo ERROR: could not find VS2022 x64 toolchain at "%VS%" & exit /b 1)
if not exist build mkdir build
cl /nologo /EHsc /W3 /O2 /DUNICODE /D_UNICODE mfprobe.cpp ^
   /Fe:build\mfprobe.exe /Fo:build\ ^
   /link mfplat.lib mf.lib mfreadwrite.lib mfuuid.lib ole32.lib shlwapi.lib
endlocal
