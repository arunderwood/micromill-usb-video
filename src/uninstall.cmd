@echo off
REM Restore the original PXC200_Support.dll in a MicroMill folder.
REM Usage: uninstall.cmd "C:\Program Files (x86)\NWR-ESI\MicroMill"
setlocal
set "DEST=%~1"
if "%DEST%"=="" set "DEST=C:\Program Files (x86)\NWR-ESI\MicroMill"
if not exist "%DEST%\PXC200_Support.dll.pxc-backup" (
  echo ERROR: no PXC200_Support.dll.pxc-backup in "%DEST%" - nothing to restore.
  exit /b 1
)
copy /Y "%DEST%\PXC200_Support.dll.pxc-backup" "%DEST%\PXC200_Support.dll" >nul && echo Restored original PXC200_Support.dll
if exist "%DEST%\pxcshim.ini" ( del "%DEST%\pxcshim.ini" >nul 2>&1 & echo Removed pxcshim.ini )
echo Done.
endlocal
