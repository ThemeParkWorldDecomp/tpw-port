@echo off
rem Starts the launcher that build.cmd made (build\tpw_launcher.exe) and returns at once; options go to the launcher.
setlocal
set "TPW_EXE=%~dp0build\tpw_launcher.exe"
if exist "%TPW_EXE%" goto run
echo There is no "%TPW_EXE%" yet. Run build.cmd first.
exit /b 1
:run
start "" "%TPW_EXE%" %*
exit /b 0
