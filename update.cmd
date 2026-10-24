@echo off
rem Updates this clone to the newest tpw-port and builds it: git pull, the submodules, build.cmd, then run.cmd starts the
rem launcher. The launcher's About box runs it (Check for updates, then Update), and it can be run by hand. Your saves
rem and checked game files (%APPDATA%\STP-TPW Launcher) are never touched.
rem
rem The optional first argument is the launcher's process id: the script waits (at most 30 seconds) until that launcher
rem has closed, as Windows cannot replace build\tpw_launcher.exe while it runs.
setlocal
cd /d "%~dp0"
if not "%~1"=="" call :wait %~1
rem cmd reads a script while it runs it, from where it left off, and the pull may change this file: so everything from
rem the pull on is one block, which cmd reads whole before it starts. Keep the :failed label, as goto looks for it in the
rem file the pull left.
(
    echo ==^> Getting the newest tpw-port
    git -c core.autocrlf=false pull --ff-only || goto failed
    echo ==^> Updating the submodules
    git -c core.autocrlf=false submodule update --init --recursive || goto failed
    call "%~dp0build.cmd" || goto failed
    call "%~dp0run.cmd" || goto failed
    exit /b 0
)

:wait
set /a "tries=0"
:waitloop
tasklist /FI "PID eq %~1" /NH 2>nul | find "%~1" >nul || exit /b 0
if %tries%==0 echo ==^> Waiting for the launcher to close
set /a "tries+=1"
if %tries% geq 30 exit /b 0
rem A second's pause (timeout refuses to run when the input is not a console).
ping -n 2 127.0.0.1 >nul
goto waitloop

:failed
echo.
echo The update stopped (see above). Your saves and game files (%APPDATA%\STP-TPW Launcher) were not touched.
pause
exit /b 1
