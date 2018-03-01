@echo off
echo Mounting current directory on X:
echo To unmount, use "dokanctl /u X"
ddsfs.exe -f . X:
pause