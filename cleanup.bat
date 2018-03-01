@echo off
echo Warning: This will remove ALL .dds files in the 
echo current folder and all subfolders.
echo.
echo The following files will be deleted:
where /R . *.dds
echo.
echo If you don't want this, close this window now.
pause
del /s *.dds
pause