@echo off
REM Install the GDI video shim into a MicroMill folder (backs up the stock DLL).
REM Usage: install.cmd "C:\Program Files (x86)\NWR-ESI\MicroMill"
REM
REM Replaces PXC200_Support.dll with the shim. The original is backed up as
REM PXC200_Support.dll.pxc-backup; revert anytime with uninstall.cmd.
REM NOTE: run build.cmd first, and run this from an elevated prompt if the
REM target is under Program Files.
setlocal
set "DEST=%~1"
if "%DEST%"=="" set "DEST=C:\Program Files (x86)\NWR-ESI\MicroMill"
if not exist "%DEST%\PXC200_Support.dll" (
  echo ERROR: no PXC200_Support.dll in "%DEST%" - is that the MicroMill folder?
  exit /b 1
)
if not exist "build\PXC200_Support.dll" ( echo ERROR: build\PXC200_Support.dll missing - run build.cmd first & exit /b 1 )

if not exist "%DEST%\PXC200_Support.dll.pxc-backup" (
  copy "%DEST%\PXC200_Support.dll" "%DEST%\PXC200_Support.dll.pxc-backup" >nul
  echo Backed up original -^> PXC200_Support.dll.pxc-backup
) else (
  echo Backup already exists (keeping it).
)
copy /Y "build\PXC200_Support.dll" "%DEST%\PXC200_Support.dll" >nul && echo Installed shim -^> "%DEST%\PXC200_Support.dll"
if exist "..\pxcshim.ini" ( copy /Y "..\pxcshim.ini" "%DEST%\pxcshim.ini" >nul & echo Copied pxcshim.ini )
echo Done. Revert with uninstall.cmd "%DEST%".
endlocal
