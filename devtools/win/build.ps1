# Builds the launcher on 64-bit Windows 10 or 11. build.cmd at the top of the tree runs this file with Windows
# PowerShell 5.1 (part of Windows) and -ExecutionPolicy Bypass, so nothing has to be allowed or installed first.
#
#   build.cmd                 builds build\tpw_launcher.exe (run.cmd starts it)
#   build.cmd test            builds everything and runs the tests (ctest)
#   build.cmd clean           deletes the build folder, the fetched tools with it
#   build.cmd help            this list
#   BUILD=<folder>            another build folder (default: build), relative to the top of the tree
#   TOOLS=fetch               fetch every tool even when the PATH has a usable one (the CI does this)
#
# Nothing is installed on the PC. Besides Git (for the clone and its submodules) the build needs a MinGW clang, CMake,
# Ninja and Python 3. Each is taken from where it was fetched earlier, else from the PATH when the version there is
# usable (clang: a MinGW clang for x86-64 not older than the one below; CMake 3.20 or newer; Ninja 1.10 or newer;
# Python 3.10 or newer), else the release below is downloaded into <build>\tools\, checked against its SHA-256 and
# kept there, so `build.cmd clean` removes it with the rest of the build. The PATH is changed for this build only.
#
# This file is plain ASCII on purpose: Windows PowerShell reads a file without a byte order mark as ANSI.

$ErrorActionPreference = 'Continue'   # a native program's stderr must not stop the script; exit codes decide
$ProgressPreference = 'SilentlyContinue'

# The releases fetched when the PATH has no usable tool. The checksums are the publishers': CMake's
# cmake-4.4.4-SHA-256.txt; the GitHub release asset digests of llvm-mingw and Ninja; python.org's file list (and the
# zip's Sigstore bundle) for Python. Ninja's and Python's zips hold their files at the top; the other two hold one
# folder (Top), which becomes <build>\tools\<Name>.
$Pins = @(
    @{ Name = 'llvm-mingw'; Version = '20261006'; Exe = 'bin\clang.exe'; Top = 'llvm-mingw-20261006-ucrt-x86_64'
       Url = 'https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/llvm-mingw-20261006-ucrt-x86_64.zip'
       Sha256 = '317492c456aa27ee607a5919f1d2d38dcdc1112516a24d0bf4b00d078f52d17a'; Size = '180 MB' },
    @{ Name = 'cmake'; Version = '4.4.4'; Exe = 'bin\cmake.exe'; Top = 'cmake-4.4.4-windows-x86_64'
       Url = 'https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-windows-x86_64.zip'
       Sha256 = 'bace36e94b31c68ab6fa295f26dfa11219e0701cf7c94b0284a7d1cb13dac536'; Size = '52 MB' },
    @{ Name = 'ninja'; Version = '1.13.2'; Exe = 'ninja.exe'; Top = ''
       Url = 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip'
       Sha256 = '07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65'; Size = '1 MB' },
    @{ Name = 'python'; Version = '3.14.7'; Exe = 'python.exe'; Top = ''
       Url = 'https://www.python.org/ftp/python/3.14.7/python-3.14.7-embed-amd64.zip'
       Sha256 = 'd297e5ff019966817ad8502465176139f2d3d840fa4ed84b13bed399a6ab1f15'; Size = '12 MB' }
)
$ClangMin = [version]'23.1.3'   # the clang of llvm-mingw 20261006

# Exit codes (the CI and scripts rely on them):
#   0 done; 2 unknown argument; 10 not 64-bit Windows, or no curl.exe/tar.exe to fetch with; 20 a download failed;
#   21 a download did not match its checksum; 22 a download could not be unpacked; 30 configuring failed;
#   40 building failed; 45 a program needs a non-system DLL; 50 a test failed; 60 the build folder could not be removed.

$Root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$Action = 'build'
$BuildArg = 'build'
$FetchAll = $false

function Say([string]$text) { Write-Host "==> $text" }

# Started by a double click in Explorer, the window would close before the message could be read: wait for Enter.
# Explorer starts "cmd.exe /c <file>"; a Command Prompt or PowerShell window does not, so they never wait.
function Test-FromExplorer {
    if ($env:CI) { return $false }
    try {
        $me = Get-CimInstance Win32_Process -Filter "ProcessId=$PID" -ErrorAction Stop
        $cmd = Get-CimInstance Win32_Process -Filter "ProcessId=$($me.ParentProcessId)" -ErrorAction Stop
        if ($null -eq $cmd -or $cmd.Name -ne 'cmd.exe' -or $cmd.CommandLine -notmatch '(?i)\s/c\s') { return $false }
        $top = Get-CimInstance Win32_Process -Filter "ProcessId=$($cmd.ParentProcessId)" -ErrorAction Stop
        return ($null -ne $top -and $top.Name -eq 'explorer.exe')
    } catch { return $false }
}

function Finish([int]$code) {
    if (Test-FromExplorer) { [void](Read-Host 'Press Enter to close this window') }
    exit $code
}

function Fail([int]$code, [string[]]$lines) {
    foreach ($l in $lines) { Write-Host $l -ForegroundColor Red }
    Finish $code
}

function Show-Help {
    Write-Host 'build.cmd [build|test|clean|help] [BUILD=<folder>] [TOOLS=fetch]'
    Write-Host '  build          build <folder>\tpw_launcher.exe (the default; run.cmd starts build\tpw_launcher.exe)'
    Write-Host '  test           build everything and run the tests'
    Write-Host '  clean          delete the build folder and the tools fetched into it'
    Write-Host '  BUILD=<folder> the build folder (default: build)'
    Write-Host '  TOOLS=fetch    fetch every tool even when the PATH has a usable one'
}

foreach ($a in $args) {
    $s = [string]$a
    if ($s -match '^(?i)(build|test|clean|help)$') { $Action = $s.ToLowerInvariant() }
    elseif ($s -match '^(?i)(-h|--help|/\?|-\?)$') { $Action = 'help' }
    elseif ($s -match '^(?i)BUILD=(.+)$') { $BuildArg = $Matches[1] }
    elseif ($s -match '^(?i)TOOLS=fetch$') { $FetchAll = $true }
    else { Show-Help; Fail 2 @("Unknown argument: $s") }
}
if ($Action -eq 'help') { Show-Help; Finish 0 }

if ([System.IO.Path]::IsPathRooted($BuildArg)) { $Build = [System.IO.Path]::GetFullPath($BuildArg) }
else { $Build = [System.IO.Path]::GetFullPath((Join-Path $Root $BuildArg)) }
$Build = $Build.TrimEnd('\')
$Tools = Join-Path $Build 'tools'
$Logs = Join-Path $Build 'logs'

# Deletes a folder with cmd's rd, which copes with deeper paths than Remove-Item does in Windows PowerShell.
function Remove-Folder([string]$path) {
    if (Test-Path -LiteralPath $path) { & cmd.exe /d /c rd /s /q $path | Out-Null }
    return -not (Test-Path -LiteralPath $path)
}

if ($Action -eq 'clean') {
    $unsafe = @([System.IO.Path]::GetPathRoot($Build).TrimEnd('\'), $Root.TrimEnd('\'), $env:USERPROFILE, $env:SystemRoot)
    if ($unsafe -contains $Build -or $Root.StartsWith("$Build\", [StringComparison]::OrdinalIgnoreCase)) {
        Fail 60 @("Not removing BUILD=$BuildArg ($Build).")
    }
    Say "Removing $Build (the launcher's data in %APPDATA%, your checked game files and saves, is not touched)"
    if (-not (Remove-Folder $Build)) { Fail 60 @("Could not remove $Build. Close any program using it and run build.cmd clean again.") }
    Finish 0
}

# 64-bit Windows only: the toolchain and the launcher are x86-64. Windows 11 on Arm runs them under its x64 emulation.
if ($env:OS -ne 'Windows_NT' -or -not [Environment]::Is64BitOperatingSystem) {
    Fail 10 @('This build is for 64-bit Windows 10 or 11 only.')
}
$os = [Environment]::OSVersion.Version
Say "64-bit Windows $($os.Major).$($os.Minor) build $($os.Build), $env:PROCESSOR_ARCHITECTURE"
if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64' -or $env:PROCESSOR_ARCHITEW6432 -eq 'ARM64') {
    Say 'Windows on Arm: the x86-64 tools and the launcher run under Windows 11 x64 emulation'
}

# --- The tools -------------------------------------------------------------------------------------------------------

function Get-VersionOf([string]$exe, [string]$arg, [string]$pattern) {
    try { $out = (& $exe $arg 2>$null | Out-String) } catch { return $null }
    if ($out -match $pattern) { return [version]$Matches[1] }
    return $null
}

# The first program of that name on the PATH whose version is usable, or $null.
function Find-OnPath([string[]]$names, [string]$arg, [string]$pattern, [version]$min) {
    foreach ($n in $names) {
        foreach ($c in @(Get-Command $n -CommandType Application -All -ErrorAction SilentlyContinue)) {
            $v = Get-VersionOf $c.Path $arg $pattern
            if ($null -ne $v -and $v -ge $min) { return @{ Path = $c.Path; Version = $v } }
        }
    }
    return $null
}

# clang on the PATH counts only when it is a MinGW clang for x86-64 (Target x86_64-w64-windows-gnu, as llvm-mingw's
# and MSYS2's are; not the LLVM installer's MSVC one), has clang++ beside it and is not older than ClangMin.
function Find-ClangOnPath {
    foreach ($c in @(Get-Command clang -CommandType Application -All -ErrorAction SilentlyContinue)) {
        try { $out = (& $c.Path --version 2>$null | Out-String) } catch { continue }
        if ($out -notmatch '(?m)^Target:\s*x86_64-w64-(windows-gnu|mingw32)') { continue }
        if ($out -notmatch 'clang version (\d+\.\d+\.\d+)') { continue }
        $v = [version]$Matches[1]
        $cxx = Join-Path (Split-Path $c.Path) 'clang++.exe'
        if ($v -ge $ClangMin -and (Test-Path -LiteralPath $cxx)) { return @{ Path = $c.Path; Version = $v } }
    }
    return $null
}

$Curl = Join-Path $env:SystemRoot 'System32\curl.exe'
$Tar = Join-Path $env:SystemRoot 'System32\tar.exe'

# Fetches one pinned tool into <build>\tools\<Name> unless it is there already with the same checksum (the .sha256
# file beside it records the checksum it was fetched with). Returns the tool's program.
function Get-Pinned($pin) {
    $dir = Join-Path $Tools $pin.Name
    $exe = Join-Path $dir $pin.Exe
    $stamp = Join-Path $dir '.sha256'
    if (-not (Test-Path -LiteralPath $Curl) -or -not (Test-Path -LiteralPath $Tar)) {
        Fail 10 @("Fetching $($pin.Name) needs curl.exe and tar.exe, part of Windows 10 version 1803 and newer.",
                  'Update Windows, or put the tools on the PATH yourself (port README, "Building by hand").')
    }
    $tmp = Join-Path $Tools "$($pin.Name).tmp"
    $zip = Join-Path $tmp 'download.zip'
    $unpacked = Join-Path $tmp 'x'
    [void](Remove-Folder $tmp)
    [void](Remove-Folder $dir)
    [void](New-Item -ItemType Directory -Force -Path $unpacked)
    Say "Fetching $($pin.Name) $($pin.Version) into $dir (about $($pin.Size), once)"
    $quiet = @('--progress-bar')
    if ($env:CI) { $quiet = @('-sS') }
    & $Curl -fL --retry 2 @quiet -o $zip $pin.Url
    if ($LASTEXITCODE -ne 0) {
        [void](Remove-Folder $tmp)
        Fail 20 @("Could not download $($pin.Url) (curl exit $LASTEXITCODE).",
                  'Check the internet connection and run build.cmd again.')
    }
    $sum = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($sum -ne $pin.Sha256) {
        [void](Remove-Folder $tmp)
        Fail 21 @("The download of $($pin.Name) does not match the checksum in devtools\win\build.ps1, so it was deleted.",
                  "  expected $($pin.Sha256)", "  got      $sum",
                  'Run build.cmd again. If it fails the same way, the download has changed: report it, do not use it.')
    }
    & $Tar -xf $zip -C $unpacked
    $tarCode = $LASTEXITCODE
    $from = $unpacked
    if ($pin.Top) { $from = Join-Path $unpacked $pin.Top }
    $moved = $false
    if ($tarCode -eq 0 -and (Test-Path -LiteralPath $from)) {
        try { Move-Item -LiteralPath $from -Destination $dir -ErrorAction Stop; $moved = $true } catch { $moved = $false }
    }
    [void](Remove-Folder $tmp)
    if (-not $moved -or -not (Test-Path -LiteralPath $exe)) {
        [void](Remove-Folder $dir)
        Fail 22 @("Could not unpack the $($pin.Name) download (tar exit $tarCode). Run build.cmd again.")
    }
    [System.IO.File]::WriteAllText($stamp, $pin.Sha256)
    Say "Fetched $($pin.Name) $($pin.Version) from $($pin.Url); its SHA-256 checksum matched"
    return $exe
}

function Test-Fetched($pin) {
    $dir = Join-Path $Tools $pin.Name
    $stamp = Join-Path $dir '.sha256'
    if (-not (Test-Path -LiteralPath (Join-Path $dir $pin.Exe)) -or -not (Test-Path -LiteralPath $stamp)) { return $false }
    return ((Get-Content -LiteralPath $stamp -TotalCount 1) -eq $pin.Sha256)
}

# Where each tool comes from: fetched earlier (kept, so that a build folder keeps its tools), else the PATH when usable
# and TOOLS=fetch was not given, else fetched now.
function Select-Tool($pin, $onPath) {
    $exe = Join-Path (Join-Path $Tools $pin.Name) $pin.Exe
    if (Test-Fetched $pin) {
        Say "$($pin.Name) $($pin.Version): $exe (fetched earlier)"
        return $exe
    }
    if (-not $FetchAll -and $null -ne $onPath) {
        Say "$($pin.Name) $($onPath.Version): $($onPath.Path) (on the PATH)"
        return $onPath.Path
    }
    return (Get-Pinned $pin)
}

[void](New-Item -ItemType Directory -Force -Path $Tools)
$pinOf = @{}
foreach ($p in $Pins) { $pinOf[$p.Name] = $p }

$onPath = @{ 'llvm-mingw' = $null; 'cmake' = $null; 'ninja' = $null; 'python' = $null }
if (-not $FetchAll) {
    $onPath['llvm-mingw'] = Find-ClangOnPath
    $onPath['cmake'] = Find-OnPath @('cmake') '--version' 'cmake version (\d+\.\d+(\.\d+)?)' ([version]'3.20')
    $onPath['ninja'] = Find-OnPath @('ninja') '--version' '^(\d+\.\d+(\.\d+)?)' ([version]'1.10')
    # The Microsoft Store's python.exe stand-in prints no version, so it is never chosen.
    $onPath['python'] = Find-OnPath @('python', 'python3') '--version' 'Python (\d+\.\d+(\.\d+)?)' ([version]'3.10')
}
$Clang = Select-Tool $pinOf['llvm-mingw'] $onPath['llvm-mingw']
$CMake = Select-Tool $pinOf['cmake'] $onPath['cmake']
$Ninja = Select-Tool $pinOf['ninja'] $onPath['ninja']
$Python = Select-Tool $pinOf['python'] $onPath['python']
$ClangBin = Split-Path $Clang
$ClangXX = Join-Path $ClangBin 'clang++.exe'
$CTest = Join-Path (Split-Path $CMake) 'ctest.exe'

# For this build only: the toolchain's folder first (its linker, nm, windres and DLLs), then CMake's and Ninja's.
$env:PATH = "$ClangBin;$(Split-Path $CMake);$(Split-Path $Ninja);$env:PATH"

# --- The build -------------------------------------------------------------------------------------------------------

# Runs one step, its output on the screen and in <build>\logs\<name>.log. Returns the program's exit code.
function Invoke-Logged([string]$name, [string]$exe, [string[]]$argv) {
    [void](New-Item -ItemType Directory -Force -Path $Logs)
    $log = Join-Path $Logs "$name.log"
    $writer = New-Object System.IO.StreamWriter($log, $false, (New-Object System.Text.UTF8Encoding($false)))
    try {
        $writer.WriteLine("> $exe $($argv -join ' ')")
        & $exe @argv 2>&1 | ForEach-Object {
            if ($_ -is [System.Management.Automation.ErrorRecord]) { $line = $_.Exception.Message } else { $line = "$_" }
            Write-Host $line
            $writer.WriteLine($line)
        }
        $code = $LASTEXITCODE
    } finally { $writer.Close() }
    return $code
}

function Step([string]$name, [int]$failCode, [string]$exe, [string[]]$argv) {
    $code = Invoke-Logged $name $exe $argv
    if ($code -ne 0) {
        Fail $failCode @("The $name step failed (exit $code). Its log is $(Join-Path $Logs "$name.log").",
                         'If this build folder was configured with other tools, start again with: build.cmd clean')
    }
}

function Slash([string]$path) { return $path.Replace('\', '/') }

# A standalone exe must run without the build tools on PATH. The port links its runtimes statically; check the
# actual PE imports as well, so a toolchain change cannot silently add libc++.dll or another redistributable.
function Test-Imports([string]$program) {
    $readobj = Join-Path $ClangBin 'llvm-readobj.exe'
    if (-not (Test-Path -LiteralPath $readobj)) {
        Fail 45 @("The compiler has no llvm-readobj.exe beside it: $ClangBin", 'Run build.cmd TOOLS=fetch to use the checked toolchain.')
    }
    # --coff-imports excludes the export table's own DLL Name (the executable's filename).
    $output = & $readobj --coff-imports $program 2>&1
    if ($LASTEXITCODE -ne 0) { Fail 45 @("Could not inspect the DLL imports of $program.", ($output | Out-String)) }
    $dlls = @($output | ForEach-Object { if ("$_" -match '^\s*Name:\s*(\S+)') { $Matches[1].ToLowerInvariant() } } | Sort-Object -Unique)
    if ($dlls.Count -eq 0) { Fail 45 @("No DLL import table found in $program.") }
    $system = @('advapi32.dll', 'avrt.dll', 'bcrypt.dll', 'cfgmgr32.dll', 'comctl32.dll', 'comdlg32.dll',
        'crypt32.dll', 'd3d11.dll', 'd3d12.dll', 'dinput8.dll', 'dwmapi.dll', 'dwrite.dll', 'dxgi.dll',
        'gdi32.dll', 'glu32.dll', 'hid.dll', 'imm32.dll', 'kernel32.dll', 'msvcrt.dll', 'ntdll.dll',
        'ole32.dll', 'oleaut32.dll', 'opengl32.dll', 'powrprof.dll', 'psapi.dll', 'rpcrt4.dll',
        'secur32.dll', 'setupapi.dll', 'shcore.dll', 'shell32.dll', 'shlwapi.dll', 'ucrtbase.dll',
        'user32.dll', 'userenv.dll', 'usp10.dll', 'version.dll', 'winmm.dll', 'winspool.drv',
        'wintrust.dll', 'ws2_32.dll', 'wtsapi32.dll', 'xinput1_4.dll')
    $record = @("$program imports:") + @($dlls | ForEach-Object { "  $_" })
    $record | ForEach-Object { Write-Host $_ }
    Add-Content -LiteralPath (Join-Path $Logs 'imports.log') -Value $record
    $extra = @($dlls | Where-Object { $_ -notin $system -and $_ -notmatch '^(api|ext)-ms-win-[a-z0-9-]+\.dll$' })
    if ($extra.Count) {
        Fail 45 @("$program needs DLLs that are not part of Windows: $($extra -join ', ').",
                  'The build must link these libraries statically. Report build\logs\imports.log; do not download DLLs from other sites.')
    }
}

Set-Location -LiteralPath $Root
if ((Test-Path -LiteralPath (Join-Path $Build 'CMakeCache.txt')) -and (Test-Path -LiteralPath (Join-Path $Build 'build.ninja'))) {
    Say "Build folder $Build is configured"
} else {
    Say "Configuring the build in $Build (the first build takes a while)"
    Step 'configure' 30 $CMake @('-S', (Slash $Root), '-B', (Slash $Build), '-G', 'Ninja',
        "-DCMAKE_MAKE_PROGRAM=$(Slash $Ninja)", "-DCMAKE_C_COMPILER=$(Slash $Clang)",
        "-DCMAKE_CXX_COMPILER=$(Slash $ClangXX)", "-DPython3_EXECUTABLE=$(Slash $Python)")
}

Say 'Building the launcher'
Step 'build' 40 $CMake @('--build', (Slash $Build), '--target', 'tpw_launcher')
$Exe = Join-Path $Build 'tpw_launcher.exe'
Test-Imports $Exe

if ($Action -eq 'test') {
    Say 'Building the tests'
    Step 'build-tests' 40 $CMake @('--build', (Slash $Build))
    # CTest's command list covers tests in subdirectories too; tools fetched under build\tools are never inspected.
    $testList = & $CTest --test-dir $Build --show-only=json-v1 | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0) { Fail 50 @('Could not read the CTest program list.') }
    foreach ($program in @($testList.tests | ForEach-Object { $_.command[0] } | Sort-Object -Unique)) {
        if ($program -and $program -like '*.exe') { Test-Imports $program }
    }
    Say 'Running the tests'
    Step 'test' 50 $CTest @('--test-dir', (Slash $Build), '--output-on-failure')
}

Say "Done. The launcher is $Exe"
if ($Build -eq (Join-Path $Root 'build')) { Say 'Start it with: .\run.cmd (or double-click build\tpw_launcher.exe)' }
else { Say "Start it with: `"$Exe`"" }
Finish 0
