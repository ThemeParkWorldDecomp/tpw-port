@echo off
rem Builds the launcher on 64-bit Windows 10 or 11: `build.cmd` makes build\tpw_launcher.exe, `run.cmd` starts it,
rem `build.cmd help` lists the rest. Nothing is installed: the tools the build needs are fetched into build\tools\
rem (pinned versions, SHA-256 checked) unless the PATH has usable ones, and `build.cmd clean` deletes them with the
rem build. The work is done by devtools\win\build.ps1 in Windows PowerShell 5.1, which is part of Windows; it runs
rem with -ExecutionPolicy Bypass for this one run, so no setting has to be changed first.
rem
rem Exit codes: those of build.ps1 (0 done, 2 bad argument, 10 not 64-bit Windows, 20 download, 21 checksum, 22 unpack,
rem 30 configure, 40 build, 50 tests, 60 clean); 10 also when Windows PowerShell is missing.
setlocal
rem A 32-bit Command Prompt sees the 64-bit PowerShell under Sysnative only.
set "TPW_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "TPW_PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
if exist "%TPW_PS%" goto have_ps
echo Windows PowerShell was not found at "%TPW_PS%". This build needs Windows 10 or 11.
exit /b 10
:have_ps
"%TPW_PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0devtools\win\build.ps1" %*
exit /b %ERRORLEVEL%
