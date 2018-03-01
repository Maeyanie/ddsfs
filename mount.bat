@echo off
echo Mounting current directory on X:
ddsfs.exe . X:
echo.
echo To unmount, use "dokanctl /u X"
pause