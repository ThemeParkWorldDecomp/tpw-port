# The broad installer cases for Windows (installer-broad.yml runs them; they also run on a PC by hand, from a checkout:
#   powershell -NoProfile -ExecutionPolicy Bypass -File .github\installer-ci\win.ps1 CASE...
# with no CASE, every case but real and public). The Windows counterpart of mac.sh, case for case as far as Windows allows.
#
# Most cases run install.ps1 for real (clone, tags, submodule, build, check, switch, themepark command) against a local
# fake release: a git repository made here, with tags vX.Y.Z, a devtools\win\build.ps1 that builds a stand-in launcher in
# a moment (or hangs, or fails, when a case asks), and a submodule. TPW_INSTALL_REPO points the script at it. Every case
# has its own empty profile folder: USERPROFILE, LOCALAPPDATA and APPDATA point into it, so the runner's real profile is
# never written to. (Two things cannot be redirected: the user's Path in the registry, which the installer edits for real
# and which this script puts back at the end, and the Desktop and Documents folders, which Windows resolves from the real
# profile; the legacy case says so when it cannot plant an old clone there.)
#
# The script runs the installer the two ways players and the launcher do: in this process from its text, as
# `irm ... | iex` does (Invoke-Line), and as a file in a new Windows PowerShell, as the launcher's updater does
# (Invoke-File). Stopping a run in the middle ends the whole process tree (taskkill /T), as the updater does.
#
# Cases: fresh legacy rerun updater dev tags refusals paths missing nogit buildfail interrupt interrupt_first tui
#   real     one real build, from a local tag of the CI checkout (needs TPW_CI_SOURCE: the checkout to take)
#   public   the one-liner a player runs, against the newest real release (manual workflow only)
#   shots    the same install, then pictures of the screen: PowerShell in front, themepark typed, the launcher's window
#            in front and alone, its window's icon and the .exe's (screenshots.yml; needs TPW_CI_SOURCE, keeps them in
#            TPW_CI_SHOTS)
#
# Windows PowerShell 5.1 (every Windows 10 and 11 has it) is enough; PowerShell 7 runs it too. This file is plain ASCII on
# purpose (Windows PowerShell reads a file without a byte order mark as ANSI): names with non-ASCII letters are built
# from character codes.

[CmdletBinding(PositionalBinding = $false)]
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Cases)

$ErrorActionPreference = 'Continue'   # a native program's stderr must not stop a case; the checks decide
$ProgressPreference = 'SilentlyContinue'

if ($env:OS -ne 'Windows_NT') { Write-Host 'This is for Windows.'; exit 2 }

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$InstallPs1 = $env:INSTALL_PS1
if (-not $InstallPs1) { $InstallPs1 = [System.IO.Path]::GetFullPath((Join-Path $Here '..\..\install.ps1')) }
if (-not (Test-Path -LiteralPath $InstallPs1 -PathType Leaf)) { Write-Host "There is no $InstallPs1."; exit 2 }
$Script = Get-Content -Raw -LiteralPath $InstallPs1

$Work = $env:TPW_CI_WORK
if (-not $Work) {
    $base = $env:RUNNER_TEMP
    if (-not $base) { $base = $env:TEMP }
    $Work = Join-Path $base ('tpw-broad-' + [System.IO.Path]::GetRandomFileName().Replace('.', ''))
}
[void](New-Item -ItemType Directory -Force -Path $Work)
$Work = (Resolve-Path -LiteralPath $Work).ProviderPath

# A local clone and submodule by path (git 2.38.1 and newer refuse the file protocol in submodules otherwise), and no
# password window ever.
$env:GIT_CONFIG_COUNT = '1'
$env:GIT_CONFIG_KEY_0 = 'protocol.file.allow'
$env:GIT_CONFIG_VALUE_0 = 'always'
$env:GIT_TERMINAL_PROMPT = '0'
$env:GCM_INTERACTIVE = 'never'

# What every case starts from, and what is put back at the end.
$Base = @{
    USERPROFILE = $env:USERPROFILE; LOCALAPPDATA = $env:LOCALAPPDATA; APPDATA = $env:APPDATA
    Path = $env:Path; SystemRoot = $env:SystemRoot; Location = (Get-Location -PSProvider FileSystem).ProviderPath
}
$CmdMark = 'tpw-port installer: the themepark command'

# The state of the case that is running (set by New-Case).
$script:Case = ''
$script:C = ''        # the case's folder
$script:H = ''        # its profile folder (USERPROFILE)
$script:Fake = ''     # the fake release repository
$script:SubRepo = ''  # its submodule
$script:Dir = ''      # the default install folder
$script:Data = ''     # the launcher's data folder
$script:RC = 0        # the last run's exit code
$script:Quiet = $false

# ---- checks

function Bad([string]$msg) { throw "tpw-broad: [$script:Case] $msg" }

function Note([string]$msg) { Write-Host "-- [$script:Case] $msg" }

function Expect-Eq([string]$what, $got, $want) {
    if ("$got" -ne "$want") { Bad "${what}: got `"$got`", wanted `"$want`"" }
}

# A line of the output exactly equal to TEXT.
function Expect-Line($out, [string]$text) {
    if (-not (@($out) -ceq $text)) { Bad "the output has no line `"$text`"" }
}

# TEXT somewhere in a line of the output.
function Expect-Text($out, [string]$text) {
    if (-not @(@($out) | Where-Object { "$_".Contains($text) })) { Bad "the output has no `"$text`"" }
}

function Expect-NoText($out, [string]$text) {
    if (@(@($out) | Where-Object { "$_".Contains($text) })) { Bad "the output has `"$text`"" }
}

# STEP n/N lines count up from 1, N stays the same, and the last one is N (the launcher's updater reads them).
function Test-Steps($out) {
    $n = 0; $total = 0; $last = 0; $wrong = $false
    foreach ($line in @($out)) {
        if ("$line" -match '^STEP (\d+)/(\d+) ') {
            $n += 1
            if ([int]$Matches[1] -ne $n) { $wrong = $true }
            if ($total -ne 0 -and [int]$Matches[2] -ne $total) { $wrong = $true }
            $total = [int]$Matches[2]
            $last = [int]$Matches[1]
        }
    }
    if ($n -eq 0 -or $wrong -or $last -ne $total) { Bad 'the STEP lines are not 1/N to N/N' }
}

# After a run: it exited with 0 and printed exactly one RESULT line, the last, "RESULT ok VERSION".
function Assert-Ok($out, [string]$version) {
    if ($script:RC -ne 0) { Bad "the install exited with $script:RC, not 0" }
    $lines = @($out)
    if ($lines.Count -eq 0) { Bad 'the run printed nothing' }
    Expect-Eq 'the last line' $lines[-1] "RESULT ok $version"
    Expect-Eq 'the number of RESULT lines' @($lines | Where-Object { "$_" -like 'RESULT *' }).Count 1
    Test-Steps $lines
}

# After a run: a failure, exit not 0, exactly one RESULT line, the last, "RESULT fail" matching PATTERN (a regex).
function Assert-Fail($out, [string]$pattern) {
    if ($script:RC -eq 0) { Bad 'the run exited with 0, but had to fail' }
    $lines = @($out)
    if ($lines.Count -eq 0) { Bad 'the run printed nothing' }
    $last = "$($lines[-1])"
    if ($last -notlike 'RESULT fail *') { Bad "the last line is `"$last`", not a RESULT fail line" }
    if ($last -notmatch $pattern) { Bad "RESULT fail `"$last`" does not match $pattern" }
    Expect-Eq 'the number of RESULT lines' @($lines | Where-Object { "$_" -like 'RESULT *' }).Count 1
}

# ---- one case's world

# New case NAME: an empty profile folder (HOME, default under the case's folder), the fake release, a clean environment.
function New-Case([string]$name, [string]$homeDir = '') {
    $script:Case = $name
    $script:C = Join-Path $Work $name
    [void](New-Item -ItemType Directory -Force -Path $script:C)
    if (-not $homeDir) { $homeDir = Join-Path $script:C 'home' }
    [void](New-Item -ItemType Directory -Force -Path $homeDir)
    $script:H = (Resolve-Path -LiteralPath $homeDir).ProviderPath
    $env:USERPROFILE = $script:H
    $env:LOCALAPPDATA = Join-Path $script:H 'AppData\Local'
    $env:APPDATA = Join-Path $script:H 'AppData\Roaming'
    [void](New-Item -ItemType Directory -Force -Path $env:LOCALAPPDATA, $env:APPDATA)
    $env:Path = $Base.Path
    $env:SystemRoot = $Base.SystemRoot
    $script:Fake = Join-Path $script:C 'fake'
    $script:SubRepo = Join-Path $script:C 'sub'
    $env:TPW_INSTALL_REPO = $script:Fake
    # the Start menu shortcut goes here, never in the Start menu of the account the tests run under
    $script:Programs = Join-Path $script:C 'programs'
    $env:TPW_INSTALL_PROGRAMS = $script:Programs
    Remove-Item -Path Env:TPW_INSTALL_STUB, Env:TPW_INSTALL_STUB_VERSION, Env:TPW_FAKE_BUILD -ErrorAction SilentlyContinue
    $env:TPW_FAKE_SLEEP = '604'
    $script:Dir = Join-Path $env:LOCALAPPDATA 'STP-TPW-Launcher'
    $script:Data = Join-Path $env:APPDATA 'STP-TPW Launcher'
    $script:Quiet = $false
    Set-Location -LiteralPath $script:H
    Write-Host "== case $name`: profile folder $script:H"
}

function Restore-Base {
    $env:USERPROFILE = $Base.USERPROFILE
    $env:LOCALAPPDATA = $Base.LOCALAPPDATA
    $env:APPDATA = $Base.APPDATA
    $env:Path = $Base.Path
    $env:SystemRoot = $Base.SystemRoot
    Remove-Item -Path Env:TPW_INSTALL_REPO, Env:TPW_INSTALL_PROGRAMS, Env:TPW_INSTALL_STUB, Env:TPW_INSTALL_STUB_VERSION, Env:TPW_FAKE_BUILD, Env:TPW_INSTALL_VIEW -ErrorAction SilentlyContinue
    Set-Location -LiteralPath $Base.Location
}

# The output of a run, all of it, or (Quiet: a real build prints thousands of lines) its STEP and RESULT lines and the
# last ones, unless it failed.
function Show-Run($out) {
    $lines = @($out)
    if ($script:Quiet -and $script:RC -eq 0) {
        $lines | Where-Object { "$_" -match '^(STEP|RESULT) ' } | ForEach-Object { Write-Host $_ }
        Write-Host '... the last lines of the output:'
        $lines | Select-Object -Last 25 | ForEach-Object { Write-Host $_ }
    } elseif ($script:Quiet) {
        $lines | Select-Object -Last 400 | ForEach-Object { Write-Host $_ }
    } else {
        $lines | ForEach-Object { Write-Host $_ }
    }
    Write-Host "[exit $script:RC]"
}

# The line a player runs: the script's text in this process, from the current folder, as `irm ... | iex` runs it.
# OPTIONS is a hashtable of its parameters (@{ NoLaunch = $true; Dir = 'D:\x' }). Returns the lines it printed.
function Invoke-Line([hashtable]$Options, [string]$Text = '') {
    if ($null -eq $Options) { $Options = @{} }
    if (-not $Text) { $Text = $Script }
    $global:LASTEXITCODE = 0
    $out = @(& ([scriptblock]::Create($Text)) @Options 6>&1 | ForEach-Object { "$_" })
    $script:RC = $LASTEXITCODE
    Show-Run $out
    return ,$out
}

# The script as a file in a new Windows PowerShell, as the launcher's updater runs it. ARGS are its arguments as typed.
function Invoke-File([string[]]$FileArgs, [string]$ScriptPath = '') {
    if (-not $ScriptPath) { $ScriptPath = $InstallPs1 }
    if ($null -eq $FileArgs) { $FileArgs = @() }
    $global:LASTEXITCODE = 0
    $out = @(& powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $ScriptPath @FileArgs 2>&1 | ForEach-Object { "$_" })
    $script:RC = $LASTEXITCODE
    Show-Run $out
    return ,$out
}

function ConvertTo-CommandArgument([string]$a) {
    if ($a -match '[\s"]') { return '"' + $a.Replace('"', '\"') + '"' }
    return $a
}

# ---- the fake release

function Write-Text([string]$file, [string]$text) {
    [System.IO.File]::WriteAllText($file, $text, (New-Object System.Text.UTF8Encoding($false)))
}

function Invoke-Git([string]$repo, [string[]]$GitArgs) {
    $out = @(& git.exe -C $repo -c user.name=ci -c user.email=ci@example.invalid -c commit.gpgsign=false -c tag.gpgsign=false -c core.autocrlf=false @GitArgs 2>&1 | ForEach-Object { "$_" })
    if ($LASTEXITCODE -ne 0) {
        $out | ForEach-Object { Write-Host $_ }
        Bad "git $($GitArgs -join ' ') failed in $repo"
    }
    return ,$out
}

function Invoke-GitF([string[]]$GitArgs) { return ,(Invoke-Git $script:Fake $GitArgs) }

function Get-FakeCommit([string]$ref) { return "$((Invoke-GitF @('rev-parse', "$ref^{commit}"))[0])".Trim() }

function Set-FakeVersion([string]$version, [string]$message) {
    Write-Text (Join-Path $script:Fake 'launcher\VERSION.txt') "$version`n"
    [void](Invoke-GitF @('add', 'launcher/VERSION.txt'))
    [void](Invoke-GitF @('commit', '-q', '-m', $message))
}

# A release: a commit that says VERSION and the tag vVERSION on it, then main goes on as the next -dev. Tags are
# annotated unless -Lightweight.
function New-FakeRelease([string]$version, [switch]$Lightweight) {
    Set-FakeVersion $version "Release $version"
    if ($Lightweight) { [void](Invoke-GitF @('tag', "v$version")) }
    else { [void](Invoke-GitF @('tag', '-a', "v$version", '-m', "Release $version")) }
    $parts = $version.Split('.')
    $next = "$($parts[0]).$($parts[1]).$([int]$parts[2] + 1)-dev"
    Set-FakeVersion $next "Start $next"
}

# A change on main that is not a release.
function Add-FakeDevChange {
    Add-Content -LiteralPath (Join-Path $script:Fake 'launcher\dev-notes.txt') -Value (Get-Date -Format o)
    [void](Invoke-GitF @('add', 'launcher/dev-notes.txt'))
    [void](Invoke-GitF @('commit', '-q', '-m', 'A change on main'))
}

# The stand-in build of the fake release, as devtools\win\build.ps1 (install.ps1 runs it with BUILD=<folder> TOOLS=fetch
# in a new Windows PowerShell and reads its exit code: 0 done, 40 a build error).
$FakeBuildPs1 = @'
# The stand-in build of the fake release (win.ps1 writes it): makes BUILD\tpw_launcher.exe, a small program that answers
# --version like the launcher and, run from a folder named build (by themepark), notes its start in the folder above.
# TPW_FAKE_BUILD=fail fails like a compiler, =slow hangs after half a build (TPW_FAKE_SLEEP seconds), =empty makes
# nothing, =wrongversion makes a launcher of another version.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$top = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$build = 'build'
foreach ($a in $args) { if ("$a" -match '^BUILD=(.+)$') { $build = $Matches[1] } }
if (-not [System.IO.Path]::IsPathRooted($build)) { $build = Join-Path $top $build }
$v = ([string](Get-Content -LiteralPath (Join-Path $top 'launcher\VERSION.txt') -TotalCount 1)).Trim()
$mode = $env:TPW_FAKE_BUILD
if (-not $mode) { $mode = 'ok' }
Write-Host '==> a stand-in for fetching the build tools'
Write-Host "==> building version $v ($mode)"
# as the real build's output names the profile folder and the account (install.ps1 scrubs them in DIR\last-build.log)
Write-Host "==> profile $env:USERPROFILE user=$env:USERNAME"
[void](New-Item -ItemType Directory -Force -Path $build)
if ($mode -eq 'slow') {
    [void](New-Item -ItemType Directory -Force -Path (Join-Path $build 'half'))
    Set-Content -LiteralPath (Join-Path $build 'half\file') -Value 'half a build'
    $seconds = 600
    if ($env:TPW_FAKE_SLEEP) { $seconds = [int]$env:TPW_FAKE_SLEEP }
    Start-Sleep -Seconds $seconds
}
if ($mode -eq 'fail') { Write-Host 'fake compiler error: expected ;'; exit 40 }
if ($mode -eq 'empty') { exit 0 }
if ($mode -eq 'wrongversion') { $v = '9.9.9' }
$csc = @('FrameworkArm64', 'Framework64', 'Framework') |
    ForEach-Object { Join-Path $env:SystemRoot "Microsoft.NET\$_\v4.0.30319\csc.exe" } |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $csc) { Write-Host 'There is no C# compiler (csc.exe) of the .NET Framework.'; exit 40 }
$source = Join-Path $build 'stub.cs'
$code = @"
using System;
using System.IO;
static class Stub {
    static int Main(string[] args) {
        if (args.Length > 0 && args[0] == "--write-icon") return 1;   // the icon is the real launcher's: the shortcut gets none
        if (args.Length > 0 && args[0] == "--version") { Console.WriteLine("STP/TPW Launcher v$v"); Console.WriteLine("build: fake"); }
        string here = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
        if (Path.GetFileName(here) == "build") {
            File.AppendAllText(Path.Combine(Path.GetDirectoryName(here), "stub-launch.txt"),
                "STP/TPW Launcher v$v args=" + string.Join(" ", args) + " cwd=" + Environment.CurrentDirectory + "\r\n");
        }
        return 0;
    }
}
"@
[System.IO.File]::WriteAllText($source, $code, (New-Object System.Text.UTF8Encoding($false)))
& $csc /nologo /target:winexe "/out:$(Join-Path $build 'tpw_launcher.exe')" $source
if ($LASTEXITCODE -ne 0) { exit 40 }
Remove-Item -LiteralPath $source -Force
exit 0
'@

function Initialize-Fake {
    & git.exe init -q -b main $script:SubRepo 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) { Bad 'git init of the submodule failed' }
    Write-Text (Join-Path $script:SubRepo 'README') "stand-in for the translation`n"
    [void](Invoke-Git $script:SubRepo @('add', 'README'))
    [void](Invoke-Git $script:SubRepo @('commit', '-q', '-m', 'tpw-decomp'))
    & git.exe init -q -b main $script:Fake 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) { Bad 'git init of the fake release failed' }
    [void](New-Item -ItemType Directory -Force -Path (Join-Path $script:Fake 'launcher'), (Join-Path $script:Fake 'devtools\win'))
    Write-Text (Join-Path $script:Fake 'devtools\win\build.ps1') $FakeBuildPs1
    # the installer is in the repository, as in tpw-port: the launcher's updater runs source\install.ps1
    Write-Text (Join-Path $script:Fake 'install.ps1') $Script
    [void](Invoke-GitF @('add', '-A'))
    [void](Invoke-GitF @('submodule', 'add', '-q', $script:SubRepo, 'tpw-decomp'))
    Set-FakeVersion '0.0.1-dev' 'The first commit'
}

# An old clone built the README's old way: a recursive clone of the fake release, with build\tpw_launcher.exe.
function New-OldClone([string]$clone) {
    [void](New-Item -ItemType Directory -Force -Path (Split-Path -Parent $clone))
    & git.exe clone -q --recurse-submodules $script:Fake $clone 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) { Bad "the old clone at $clone could not be made" }
    & powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $clone 'devtools\win\build.ps1') 'BUILD=build' 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath (Join-Path $clone 'build\tpw_launcher.exe'))) { Bad 'the old clone did not build' }
}

# ---- looking at what is there

# Everything about a folder: each entry's name, and each file's size and SHA-256. No folder times: NTFS updates the copy of
# a folder's times in its parent's listing lazily, so they can change when only something was read.
function Get-Snapshot([string]$folder) {
    if (-not (Test-Path -LiteralPath $folder)) { return '(missing)' }
    $lines = @(Get-ChildItem -LiteralPath $folder -Recurse -Force | Sort-Object FullName | ForEach-Object {
        if ($_.PSIsContainer) { "$($_.FullName)\" }
        else { "$($_.FullName) $($_.Length) $((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash)" }
    })
    return ($lines -join "`n")
}

function Assert-Same([string]$what, [string]$before, [string]$after) {
    if (-not [string]::Equals($before, $after)) { Bad "$what was changed. Before:`n$before`nAfter:`n$after" }
}

# The names in the profile folder, for "nothing was made" checks. Windows PowerShell's own startup cache
# (AppData\Local\Microsoft\Windows\PowerShell, written by any new powershell.exe) is left out.
function Get-HomeListing {
    $names = @(Get-ChildItem -LiteralPath $script:H -Recurse -Force -Name |
        Where-Object { -not $_.StartsWith('AppData\Local\Microsoft', [StringComparison]::OrdinalIgnoreCase) } | Sort-Object)
    return ($names -join "`n")
}

function Add-SeedData {
    $game = Join-Path $script:Data 'games\us\Theme Park World'
    [void](New-Item -ItemType Directory -Force -Path $game, (Join-Path $script:Data 'saves\T5L48'), (Join-Path $script:Data 'packs'), (Join-Path $script:Data 'logs'))
    Write-Text (Join-Path $script:Data 'launcher.ini') "[launcher]`nfullscreen=0`nvolume=7`nlanguage=en`n"
    $rng = New-Object System.Random(7)
    $bytes = New-Object byte[] 300000
    $rng.NextBytes($bytes)
    [System.IO.File]::WriteAllBytes((Join-Path $game 'Data.wad'), $bytes)
    $bytes = New-Object byte[] 4096
    $rng.NextBytes($bytes)
    [System.IO.File]::WriteAllBytes((Join-Path $script:Data 'saves\T5L48\park1.sav'), $bytes)
    Write-Text (Join-Path $script:Data 'logs\launcher.log') "a log line`n"
}

# How many lines of DIR\stub-launch.txt hold TEXT (none before the stand-in first writes it).
function Get-LaunchCount([string]$dir, [string]$text) {
    $log = Join-Path $dir 'stub-launch.txt'
    if (-not (Test-Path -LiteralPath $log)) { return 0 }
    return @(Get-Content -LiteralPath $log | Where-Object { "$_".Contains($text) }).Count
}

# The stand-in is started detached and can take seconds to write its line: wait for it, 30 seconds at most.
function Wait-LaunchCount([string]$dir, [string]$text, [int]$moreThan) {
    foreach ($i in 1..60) {
        if ((Get-LaunchCount $dir $text) -gt $moreThan) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

# The first line of a launcher's --version ("STP/TPW Launcher v0.1.0"); it has no console, so through a file.
function Get-VersionLine([string]$folder) {
    $exe = Join-Path $folder 'tpw_launcher.exe'
    if (-not (Test-Path -LiteralPath $exe)) { return '' }
    $out = Join-Path $script:C ('version-' + [System.IO.Path]::GetRandomFileName().Replace('.', '') + '.txt')
    try {
        $p = Start-Process -FilePath $exe -ArgumentList '--version' -WorkingDirectory $folder -NoNewWindow -PassThru `
            -RedirectStandardOutput $out -ErrorAction Stop
        $null = $p.Handle
        if (-not $p.WaitForExit(60000)) { try { $p.Kill() } catch {}; return '' }
    } catch { return '' }
    if (-not (Test-Path -LiteralPath $out)) { return '' }
    $first = ([string](Get-Content -LiteralPath $out -TotalCount 1)).Trim()
    Remove-Item -LiteralPath $out -Force -ErrorAction SilentlyContinue
    return $first
}

# The folder DIR holds this version's launcher in build\, and says so.
function Assert-Build([string]$dir, [string]$version, [string]$channel) {
    Expect-Eq "the launcher in $dir\build" (Get-VersionLine (Join-Path $dir 'build')) "STP/TPW Launcher v$version"
    $infoFile = Join-Path $dir 'build\install-info.txt'
    if (-not (Test-Path -LiteralPath $infoFile)) { Bad "$infoFile is missing" }
    $info = @(Get-Content -LiteralPath $infoFile)
    if (-not ($info -ceq "version=$version")) { Bad "$infoFile has no version=$version" }
    if (-not ($info -ceq "channel=$channel")) { Bad "$infoFile has no channel=$channel" }
    $channelFile = Join-Path $dir 'channel.txt'
    if (-not (Test-Path -LiteralPath $channelFile)) { Bad "$channelFile is missing" }
    Expect-Eq 'channel.txt' ([string](Get-Content -LiteralPath $channelFile -TotalCount 1)).Trim() $channel
}

# The Start menu shortcut (install.ps1, THE START MENU SHORTCUT): in the case's Programs folder, pointing at DIR's
# launcher, started in DIR\build. -Icon: DIR\icon.ico, written by the launcher, is its icon and a .ico file (a real
# build); without, the stand-in wrote none and the shortcut shows the program's own.
function Assert-Shortcut([string]$dir, [switch]$Icon) {
    $lnk = Join-Path $script:Programs 'STP-TPW Launcher.lnk'
    if (-not (Test-Path -LiteralPath $lnk -PathType Leaf)) { Bad "the Start menu shortcut $lnk is missing" }
    $sc = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
    $exe = Join-Path $dir 'build\tpw_launcher.exe'
    Expect-Eq "the shortcut's target" ([string]$sc.TargetPath) $exe
    Expect-Eq "the shortcut's folder" ([string]$sc.WorkingDirectory) (Join-Path $dir 'build')
    Expect-Eq "the shortcut's description" ([string]$sc.Description) 'STP/TPW Launcher'
    $ico = Join-Path $dir 'icon.ico'
    if ($Icon) {
        Expect-Eq "the shortcut's icon" ([string]$sc.IconLocation) "$ico,0"
        if (-not (Test-Path -LiteralPath $ico -PathType Leaf)) { Bad "the launcher wrote no $ico" }
        $bytes = [System.IO.File]::ReadAllBytes($ico)
        if ($bytes.Length -lt 22 -or $bytes[0] -ne 0 -or $bytes[1] -ne 0 -or $bytes[2] -ne 1 -or $bytes[3] -ne 0 -or $bytes[4] -lt 1) {
            Bad "$ico is not an icon file"
        }
    } else {
        Expect-Eq "the shortcut's icon" ([string]$sc.IconLocation) "$exe,0"
        if (Test-Path -LiteralPath $ico) { Bad "the stand-in launcher wrote $ico" }
    }
    if (@(Get-ChildItem -LiteralPath $script:Programs -Force).Count -ne 1) { Bad "more than the shortcut is in $script:Programs" }
}

# The user's Path as stored, and its kind (the installer edits it for real; see Restore-UserPath).
function Get-UserPathRaw {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment')
    if ($null -eq $key) { return @('', $null) }
    try {
        $value = [string]$key.GetValue('Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        $kind = $null
        if ($null -ne $key.GetValue('Path')) { $kind = $key.GetValueKind('Path') }
        return @($value, $kind)
    } finally { $key.Close() }
}

function Get-UserPathEntries {
    $value = [Environment]::GetEnvironmentVariable('Path', 'User')
    if (-not $value) { return @() }
    return @($value -split ';' | Where-Object { $_ })
}

function Test-OurCommand([string]$folder) {
    try {
        $cmd = Join-Path $folder 'themepark.cmd'
        return ((Test-Path -LiteralPath $cmd -PathType Leaf) -and (Select-String -LiteralPath $cmd -SimpleMatch $CmdMark -Quiet))
    } catch { return $false }
}

# The user's Path has DIR\bin once, and no other install's bin (the newest install takes the command over).
function Assert-UserPathOnce([string]$dir) {
    $bin = Join-Path $dir 'bin'
    $entries = @(Get-UserPathEntries)
    $ours = @($entries | Where-Object { $_.TrimEnd('\') -ieq $bin })
    if ($ours.Count -ne 1) { Bad "the user's Path has $bin $($ours.Count) times, not once: $($entries -join ';')" }
    foreach ($e in $entries) {
        if ($e.TrimEnd('\') -ieq $bin) { continue }
        if (Test-OurCommand $e) { Bad "the user's Path still has another install's $e" }
    }
}

function Assert-UserPathNone([string]$dir) {
    $bin = Join-Path $dir 'bin'
    $ours = @(Get-UserPathEntries | Where-Object { $_.TrimEnd('\') -ieq $bin })
    if ($ours.Count -ne 0) { Bad "the user's Path has $bin, but the install did not happen" }
}

# A new PowerShell window: without a profile and with the Path a new window gets (the machine's and the user's, from the
# registry). It types LINE (themepark --version unless given); the command must be DIR\bin\themepark.cmd (compared in
# that window, so that no non-ASCII letter of the folder's name crosses a pipe). Returns what LINE printed, with its
# exit code in RC (themepark runs the launcher, or the installer for themepark update, in the foreground, so that it is
# all there). In Windows PowerShell, and in PowerShell 7 when the PC has it.
function Invoke-NewWindow([string]$dir, [string]$shell, [string]$line = 'themepark --version') {
    $expected = (Join-Path $dir 'bin\themepark.cmd') -replace "'", "''"
    $check = @"
`$ProgressPreference = 'SilentlyContinue'
`$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
`$c = Get-Command themepark -ErrorAction SilentlyContinue | Select-Object -First 1
if (`$null -eq `$c) { [Console]::Out.WriteLine('themepark: not found'); exit 3 }
if (`$c.Path -ine '$expected') { [Console]::Out.WriteLine('themepark: another one, not ours'); exit 4 }
[Console]::Out.WriteLine('themepark: ours')
$line
exit `$LASTEXITCODE
"@
    $encoded = [Convert]::ToBase64String([System.Text.Encoding]::Unicode.GetBytes($check))
    $global:LASTEXITCODE = 0
    $out = @(& $shell -NoLogo -NoProfile -NonInteractive -EncodedCommand $encoded 2>&1 | ForEach-Object { "$_" })
    $code = $LASTEXITCODE
    $out | ForEach-Object { Write-Host "  [$shell] $_" }
    Write-Host "  [$shell exit $code]"
    if ($code -eq 3) { Bad "a new $shell window does not find themepark" }
    if ($code -eq 4) { Bad "a new $shell window finds another themepark, not $dir\bin\themepark.cmd" }
    $script:RC = $code
    $at = [Array]::IndexOf($out, 'themepark: ours')
    if ($at -lt 0) { Bad "a new $shell window did not get to $line" }
    return ,@($out | Select-Object -Skip ($at + 1))
}

function Get-NewWindowShells {
    $shells = @('powershell.exe')
    if (Get-Command pwsh.exe -ErrorAction SilentlyContinue) { $shells += 'pwsh.exe' }
    return $shells
}

# themepark --version in a new window must print the version line of VERSION, exit with 0, and have started the stand-in
# launcher of that version with --version.
function Assert-NewWindow([string]$dir, [string]$version) {
    $want = "STP/TPW Launcher v$version args=--version"
    foreach ($shell in Get-NewWindowShells) {
        $seen = Get-LaunchCount $dir $want
        $out = Invoke-NewWindow $dir $shell
        if ($script:RC -ne 0) { Bad "themepark --version in a new $shell window exited with $script:RC" }
        if (-not ($out -ceq "STP/TPW Launcher v$version")) { Bad "themepark --version in a new $shell window did not print the version line of $version" }
        if (-not (Wait-LaunchCount $dir $want $seen)) { Bad "themepark --version in a new $shell window did not start version $version" }
    }
}

# The real launcher: themepark runs it in the foreground through a pipe (it has no console of its own), so this checks
# the command, the exit code, its version and build lines, and that the launcher ends by itself.
function Assert-NewWindowReal([string]$dir) {
    foreach ($shell in Get-NewWindowShells) {
        $out = Invoke-NewWindow $dir $shell
        if ($script:RC -ne 0) { Bad "themepark --version in a new $shell window exited with $script:RC" }
        if (-not ($out -like 'STP/TPW Launcher v*')) { Bad "themepark --version in a new $shell window did not print the version line" }
        if (-not ($out -like 'build: *')) { Bad "themepark --version in a new $shell window did not print the build line" }
        $exe = Join-Path $dir 'build\tpw_launcher.exe'
        $still = $true
        foreach ($i in 1..30) {
            $running = @(Get-Process -Name tpw_launcher -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $exe })
            if ($running.Count -eq 0) { $still = $false; break }
            Start-Sleep -Seconds 1
        }
        if ($still) { Bad 'the launcher started by themepark --version is still running after 30 seconds' }
    }
}

# Processes still running the fake build of DIR (a stopped install must leave none, as the updater kills the tree).
function Get-FakeBuildProcesses([string]$dir) {
    $marker = Join-Path $dir 'source\devtools\win\build.ps1'
    return @(Get-CimInstance Win32_Process -Filter "Name = 'powershell.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -and $_.CommandLine.ToLowerInvariant().Contains($marker.ToLowerInvariant()) })
}

function Assert-NoFakeBuildLeft([string]$dir) {
    foreach ($i in 1..20) {
        if ((Get-FakeBuildProcesses $dir).Count -eq 0) { return }
        Start-Sleep -Milliseconds 500
    }
    Get-FakeBuildProcesses $dir | ForEach-Object { Write-Host "still running: $($_.ProcessId) $($_.CommandLine)" }
    Bad 'a process of the stopped build is still running'
}

# install.ps1 as a file in a new Windows PowerShell, in the background, its build hanging (TPW_FAKE_BUILD=slow). Returns
# the process once the build is under way (DIR\build-next\half\file exists).
function Start-SlowInstall([string]$ScriptPath, [string[]]$FileArgs, [string]$OutFile) {
    $list = @('-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', (ConvertTo-CommandArgument $ScriptPath))
    foreach ($a in $FileArgs) { $list += ConvertTo-CommandArgument $a }
    $env:TPW_FAKE_BUILD = 'slow'
    $p = Start-Process -FilePath 'powershell.exe' -ArgumentList $list -WorkingDirectory (Get-Location -PSProvider FileSystem).ProviderPath `
        -NoNewWindow -PassThru -RedirectStandardOutput $OutFile
    $null = $p.Handle
    Remove-Item -Path Env:TPW_FAKE_BUILD -ErrorAction SilentlyContinue
    foreach ($i in 1..120) {
        if (Test-Path -LiteralPath (Join-Path $script:Dir 'build-next\half\file')) { return $p }
        if ($p.HasExited) { break }
        Start-Sleep -Milliseconds 500
    }
    if (Test-Path -LiteralPath $OutFile) { Get-Content -LiteralPath $OutFile | ForEach-Object { Write-Host $_ } }
    & taskkill.exe /PID $p.Id /T /F 2>&1 | Out-Null
    Bad 'the build did not start'
}

# Ends the install and everything it started, as the launcher's updater does (a job object or taskkill /T there).
function Stop-Tree($p) {
    & taskkill.exe /PID $p.Id /T /F 2>&1 | ForEach-Object { Write-Host "  $_" }
    try { [void]$p.WaitForExit(30000) } catch {}
}

# ---- cases

# A fresh PC: no release yet (a plain message, nothing made), then the first release.
function Invoke-Case_fresh {
    New-Case 'fresh'
    Initialize-Fake
    $before = Get-HomeListing

    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Fail $out 'No release has been published yet'
    Expect-Text $out 'Nothing was changed.'
    Assert-Same 'the profile folder (a run with no release)' $before (Get-HomeListing)

    New-FakeRelease '0.0.1'
    # exactly as a player runs it, no options: the launcher is started at the end
    $out = Invoke-Line @{}
    Assert-Ok $out '0.0.1'
    Expect-Text $out 'Newest release: 0.0.1'
    Expect-Text $out 'From now on, type themepark in this window or any new PowerShell window to play.'
    Expect-Text $out 'STP/TPW Launcher is in the Start menu: open it from there, or find it with Search.'
    Assert-Build $script:Dir '0.0.1' 'stable'
    Assert-Shortcut $script:Dir
    $tagCommit = Get-FakeCommit 'v0.0.1'
    $srcHead = "$(& git.exe -C (Join-Path $script:Dir 'source') rev-parse HEAD 2>&1)".Trim()
    Expect-Eq "the source's commit" $srcHead $tagCommit
    $info = @(Get-Content -LiteralPath (Join-Path $script:Dir 'build\install-info.txt'))
    if (-not ($info -ceq "commit=$tagCommit")) { Bad "install-info.txt has no commit=$tagCommit" }
    if (-not (Test-Path -LiteralPath (Join-Path $script:Dir 'source\tpw-decomp\README'))) { Bad 'the submodule was not downloaded' }
    foreach ($f in @('.tpw-installer', 'last-build.log', 'bin\themepark.cmd')) {
        if (-not (Test-Path -LiteralPath (Join-Path $script:Dir $f))) { Bad "$script:Dir\$f is missing" }
    }
    # the build's output: the screen shows it as it is; last-build.log has the profile folder and the account scrubbed
    Expect-Text $out "==> profile $script:H user=$env:USERNAME"
    $buildLog = [System.IO.File]::ReadAllText((Join-Path $script:Dir 'last-build.log'))
    if ($buildLog -notmatch '(?m)^==> profile %USERPROFILE% user=<user>\r?$') { Bad "last-build.log did not scrub the profile line:`n$buildLog" }
    if ($buildLog.IndexOf($script:H, [StringComparison]::OrdinalIgnoreCase) -ge 0) { Bad "last-build.log names the profile folder $script:H" }
    Assert-UserPathOnce $script:Dir
    # the launcher the line started (from DIR, detached)
    if (-not (Wait-LaunchCount $script:Dir "STP/TPW Launcher v0.0.1 args= cwd=$script:Dir" 0)) { Bad 'the install did not start the launcher' }
    # nothing in the profile folder but the install (AppData\Local\STP-TPW-Launcher): no data folder, nothing else
    # (Windows PowerShell itself keeps a startup cache under AppData\Local\Microsoft, which is not the installer's)
    if (Test-Path -LiteralPath $script:Data) { Bad "the install made the launcher's data folder" }
    $top = @(Get-ChildItem -LiteralPath $script:H -Force -Name | Sort-Object) -join ' '
    Expect-Eq 'the profile folder' $top 'AppData'
    $local = @(Get-ChildItem -LiteralPath $env:LOCALAPPDATA -Force -Name | Where-Object { $_ -ne 'Microsoft' } | Sort-Object) -join ' '
    Expect-Eq 'AppData\Local' $local 'STP-TPW-Launcher'
    # the same window: irm | iex put DIR\bin on this window's Path
    $same = Get-Command themepark -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $same -or $same.Path -ine (Join-Path $script:Dir 'bin\themepark.cmd')) { Bad 'themepark is not on the Path of the window the line ran in' }
    Assert-NewWindow $script:Dir '0.0.1'
}

# An old README-style clone (tpw-port main, recursed submodules, built) with a data folder: the install leaves both
# alone and names the clone. Desktop and Documents are where Windows says they are (the real profile's, usually), so
# the install can only be expected to name a clone there when they resolve into the case's profile folder.
function Invoke-Case_legacy {
    foreach ($where in @('home', 'Downloads', 'Desktop', 'Documents', 'cwd')) {
        New-Case "legacy-$where"
        Initialize-Fake
        New-FakeRelease '0.0.1'
        New-FakeRelease '0.0.2'
        $named = $true
        switch ($where) {
            'home' { $clone = Join-Path $script:H 'tpw-port' }
            'cwd' { $clone = Join-Path $script:H 'projects\tpw-port' }
            'Downloads' { $clone = Join-Path $script:H 'Downloads\tpw-port' }
            'Desktop' {
                $folder = [Environment]::GetFolderPath('Desktop')
                if (-not $folder -or -not $folder.StartsWith($script:H, [StringComparison]::OrdinalIgnoreCase)) {
                    Write-Host "::notice::[legacy-Desktop] Windows resolves the Desktop folder to $folder, outside the case's profile folder, so a clone there cannot be planted; the clone goes to $script:H\Desktop and is not expected to be named."
                    $folder = Join-Path $script:H 'Desktop'
                    $named = $false
                }
                $clone = Join-Path $folder 'tpw-port'
            }
            'Documents' {
                $folder = [Environment]::GetFolderPath('MyDocuments')
                if (-not $folder -or -not $folder.StartsWith($script:H, [StringComparison]::OrdinalIgnoreCase)) {
                    Write-Host "::notice::[legacy-Documents] Windows resolves the Documents folder to $folder, outside the case's profile folder, so a clone there cannot be planted; the clone goes to $script:H\Documents and is not expected to be named."
                    $folder = Join-Path $script:H 'Documents'
                    $named = $false
                }
                $clone = Join-Path $folder 'tpw-port'
            }
        }
        New-OldClone $clone
        Add-SeedData
        $cloneBefore = Get-Snapshot $clone
        $dataBefore = Get-Snapshot $script:Data
        if ($where -eq 'cwd') { Set-Location -LiteralPath $clone }
        $out = Invoke-Line @{ NoLaunch = $true }
        Assert-Ok $out '0.0.2'
        if ($named) {
            Expect-Line $out "An older copy of the launcher is in $clone."
            Expect-Text $out 'Your games, saves and settings are not in it.'
        }
        Assert-Same 'the old clone' $cloneBefore (Get-Snapshot $clone)
        Assert-Same 'the data folder' $dataBefore (Get-Snapshot $script:Data)
        if (Test-Path -LiteralPath (Join-Path $clone 'stub-launch.txt')) { Bad "the old clone's launcher was started" }
        Assert-Build $script:Dir '0.0.2' 'stable'
        Assert-UserPathOnce $script:Dir
        Assert-NewWindow $script:Dir '0.0.2'
        # the same line again, and a newer release: the clone and the data stay as they are
        New-FakeRelease '0.0.3'
        $out = Invoke-Line @{ NoLaunch = $true }
        Assert-Ok $out '0.0.3'
        if ($named) { Expect-Line $out "An older copy of the launcher is in $clone." }
        Assert-Same 'the old clone (after the update)' $cloneBefore (Get-Snapshot $clone)
        Assert-Same 'the data folder (after the update)' $dataBefore (Get-Snapshot $script:Data)
        if ($where -eq 'home') {
            # the install never goes into the old clone, though it is the folder the README used
            $out = Invoke-Line @{ NoLaunch = $true; Dir = $clone }
            Assert-Fail $out 'is not empty and not an install'
            Assert-Same 'the old clone (after a refused install)' $cloneBefore (Get-Snapshot $clone)
        }
        Set-Location -LiteralPath $script:H
    }
    # several old copies at once, each named once
    New-Case 'legacy-all'
    Initialize-Fake
    New-FakeRelease '0.0.1'
    $clones = @((Join-Path $script:H 'tpw-port'), (Join-Path $script:H 'Downloads\tpw-port'))
    foreach ($clone in $clones) {
        [void](New-Item -ItemType Directory -Force -Path (Split-Path -Parent $clone))
        & git.exe clone -q --recurse-submodules $script:Fake $clone 2>&1 | Out-Host
    }
    Add-SeedData
    $snaps = @{}
    foreach ($clone in $clones) { $snaps[$clone] = Get-Snapshot $clone }
    $dataBefore = Get-Snapshot $script:Data
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out '0.0.1'
    foreach ($clone in $clones) { Expect-Line $out "An older copy of the launcher is in $clone." }
    Expect-Text $out 'they are no longer needed'
    foreach ($clone in $clones) { Assert-Same "the old clone $clone" $snaps[$clone] (Get-Snapshot $clone) }
    Assert-Same 'the data folder' $dataBefore (Get-Snapshot $script:Data)
}

# Run again with nothing new (nothing changes), with a newer release (updates).
function Invoke-Case_rerun {
    New-Case 'rerun'
    Initialize-Fake
    New-FakeRelease '0.0.1' -Lightweight
    Add-SeedData
    $dataBefore = Get-Snapshot $script:Data
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out '0.0.1'
    Assert-Shortcut $script:Dir
    $buildBefore = Get-Snapshot (Join-Path $script:Dir 'build')
    $pathBefore = (Get-UserPathRaw)[0]
    # the Start menu shortcut, deleted by the player, comes back at a run with nothing new
    $lnk = Join-Path $script:Programs 'STP-TPW Launcher.lnk'
    Remove-Item -LiteralPath $lnk -Force
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out '0.0.1'
    Expect-Text $out 'Already up to date: version 0.0.1.'
    Assert-Shortcut $script:Dir
    $lnkBefore = (Get-Item -LiteralPath $lnk).LastWriteTimeUtc
    Start-Sleep -Seconds 2
    Assert-Same 'the build (a run with nothing new)' $buildBefore (Get-Snapshot (Join-Path $script:Dir 'build'))
    Expect-Eq "the user's Path after a run with nothing new" (Get-UserPathRaw)[0] $pathBefore
    if (Test-Path -LiteralPath (Join-Path $script:Dir 'build-prev')) { Bad 'a run with nothing new made a build-prev' }
    if (Test-Path -LiteralPath (Join-Path $script:Dir 'build-next')) { Bad 'a run with nothing new left a build-next' }
    Assert-NewWindow $script:Dir '0.0.1'

    # a newer release (annotated this time): it updates, keeps the one before
    New-FakeRelease '0.0.2'
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out '0.0.2'
    Expect-Text $out 'Switched to version 0.0.2. The one before is kept in'
    Assert-Build $script:Dir '0.0.2' 'stable'
    $prev = @(Get-Content -LiteralPath (Join-Path $script:Dir 'build-prev\install-info.txt'))
    if (-not ($prev -ceq 'version=0.0.1')) { Bad 'build-prev is not version 0.0.1' }
    Assert-UserPathOnce $script:Dir
    Assert-NewWindow $script:Dir '0.0.2'
    Assert-Shortcut $script:Dir
    if ((Get-Item -LiteralPath $lnk).LastWriteTimeUtc -le $lnkBefore) { Bad 'the update did not write the Start menu shortcut again' }
    # the launcher the update starts (no -NoLaunch): from DIR, as the new version
    New-FakeRelease '0.0.3'
    $out = Invoke-Line @{}
    Assert-Ok $out '0.0.3'
    if (-not (Wait-LaunchCount $script:Dir "STP/TPW Launcher v0.0.3 args= cwd=$script:Dir" 0)) { Bad 'the update did not start the new launcher' }
    Assert-Same 'the data folder' $dataBefore (Get-Snapshot $script:Data)
}

# The launcher's Update button: -Update -Stage, then -SwapAndLaunch -WaitPid, as install.ps1 in source\ runs them
# (powershell.exe -File, from DIR).
function Invoke-Case_updater {
    New-Case 'updater'
    Initialize-Fake
    New-FakeRelease '0.0.1'
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out '0.0.1'
    Add-SeedData
    $dataBefore = Get-Snapshot $script:Data
    $pathBefore = (Get-UserPathRaw)[0]
    $installer = Join-Path $script:Dir 'source\install.ps1'
    Set-Location -LiteralPath $script:Dir
    # a stage leaves the Start menu shortcut alone (the launcher in use is not changed)
    $lnk = Join-Path $script:Programs 'STP-TPW Launcher.lnk'
    Assert-Shortcut $script:Dir
    $lnkBefore = (Get-Item -LiteralPath $lnk).LastWriteTimeUtc
    # nothing new: nothing staged
    $out = Invoke-File @('-Update', '-Stage', '-Dir', $script:Dir) $installer
    Assert-Ok $out '0.0.1'
    if (Test-Path -LiteralPath (Join-Path $script:Dir 'build-next')) { Bad 'a stage with nothing new left a build-next' }
    if ((Get-Item -LiteralPath $lnk).LastWriteTimeUtc -ne $lnkBefore) { Bad 'a stage wrote the Start menu shortcut' }
    # a newer release: staged, built and checked, but the launcher in use is not changed
    New-FakeRelease '0.0.2'
    $out = Invoke-File @('-Update', '-Stage', '-Dir', $script:Dir) $installer
    Assert-Ok $out '0.0.2'
    Expect-Eq 'the STEP lines of a stage' @($out | Where-Object { "$_" -like 'STEP *' }).Count 4
    Assert-Build $script:Dir '0.0.1' 'stable'
    $next = @(Get-Content -LiteralPath (Join-Path $script:Dir 'build-next\install-info.txt'))
    if (-not ($next -ceq 'version=0.0.2')) { Bad 'build-next is not version 0.0.2' }
    Assert-NewWindow $script:Dir '0.0.1'
    # a newer one still replaces the staged one; the same one again finds it waiting
    New-FakeRelease '0.0.3'
    $out = Invoke-File @('-Update', '-Stage', '-Dir', $script:Dir) $installer
    Assert-Ok $out '0.0.3'
    $next = @(Get-Content -LiteralPath (Join-Path $script:Dir 'build-next\install-info.txt'))
    if (-not ($next -ceq 'version=0.0.3')) { Bad 'build-next is not version 0.0.3' }
    $out = Invoke-File @('-Update', '-Stage', '-Dir', $script:Dir) $installer
    Assert-Ok $out '0.0.3'
    Expect-Text $out 'was built earlier and is waiting'
    # the swap waits for the launcher (here a process of 3 seconds) to exit
    $sleeper = Start-Process -FilePath 'powershell.exe' -ArgumentList '-NoLogo', '-NoProfile', '-NonInteractive', '-Command', 'Start-Sleep -Seconds 3' -NoNewWindow -PassThru
    $null = $sleeper.Handle
    $t0 = Get-Date
    $out = Invoke-File @('-SwapAndLaunch', '-WaitPid', "$($sleeper.Id)", '-Dir', $script:Dir) $installer
    $waited = ((Get-Date) - $t0).TotalSeconds
    Assert-Ok $out '0.0.3'
    if ($waited -lt 2) { Bad "the swap did not wait for the launcher to exit ($waited seconds)" }
    Assert-Build $script:Dir '0.0.3' 'stable'
    Assert-Shortcut $script:Dir
    if ((Get-Item -LiteralPath $lnk).LastWriteTimeUtc -le $lnkBefore) { Bad 'the swap did not write the Start menu shortcut again' }
    $prev = @(Get-Content -LiteralPath (Join-Path $script:Dir 'build-prev\install-info.txt'))
    if (-not ($prev -ceq 'version=0.0.1')) { Bad 'build-prev is not version 0.0.1' }
    if (Test-Path -LiteralPath (Join-Path $script:Dir 'build-next')) { Bad 'the swap left a build-next' }
    if (-not (Wait-LaunchCount $script:Dir "STP/TPW Launcher v0.0.3 args= cwd=$script:Dir" 0)) { Bad 'the swap did not start the new launcher' }
    Assert-NewWindow $script:Dir '0.0.3'
    # nothing waiting: the swap only starts the launcher in use
    $out = Invoke-File @('-SwapAndLaunch', '-NoLaunch', '-Dir', $script:Dir) $installer
    Assert-Ok $out '0.0.3'
    # an update never touches the Path or the data
    Expect-Eq "the user's Path after the updater" (Get-UserPathRaw)[0] $pathBefore
    Assert-Same 'the data folder' $dataBefore (Get-Snapshot $script:Data)
    # a source folder with a change of the player's own: stop, nothing switched
    New-FakeRelease '0.0.4'
    Add-Content -LiteralPath (Join-Path $script:Dir 'source\devtools\win\build.ps1') -Value '# mine'
    $out = Invoke-File @('-Update', '-Stage', '-Dir', $script:Dir) $installer
    Assert-Fail $out 'The source folder has changes of its own'
    Assert-Build $script:Dir '0.0.3' 'stable'
    & git.exe -C (Join-Path $script:Dir 'source') checkout -q -- devtools/win/build.ps1 2>&1 | Out-Host
    $out = Invoke-File @('-Update', '-Dir', $script:Dir, '-NoLaunch') $installer
    Assert-Ok $out '0.0.4'
    Assert-Build $script:Dir '0.0.4' 'stable'
    # themepark update, typed in a new window: the same -Update, run by the command from source\ in the foreground
    New-FakeRelease '0.0.5'
    $out = Invoke-NewWindow $script:Dir 'powershell.exe' 'themepark update -NoLaunch'
    Assert-Ok $out '0.0.5'
    Assert-Build $script:Dir '0.0.5' 'stable'
    Assert-NewWindow $script:Dir '0.0.5'
    # refused while a launcher runs (a copy of ping.exe named tpw_launcher.exe, pinging for 30 seconds, stands for it)
    $standIn = Join-Path $script:C 'tpw_launcher.exe'
    Copy-Item -LiteralPath (Join-Path $env:SystemRoot 'System32\ping.exe') -Destination $standIn
    $pinger = Start-Process -FilePath $standIn -ArgumentList '-n', '31', '127.0.0.1' -NoNewWindow -PassThru `
        -RedirectStandardOutput (Join-Path $script:C 'ping.txt')
    $null = $pinger.Handle
    try {
        $out = Invoke-NewWindow $script:Dir 'powershell.exe' 'themepark update -NoLaunch'
    } finally {
        Stop-Process -Id $pinger.Id -Force -ErrorAction SilentlyContinue
    }
    if ($script:RC -eq 0) { Bad 'themepark update ran while the launcher was running' }
    Expect-Text $out 'themepark: the launcher is running'
    if ($out -like 'STEP *') { Bad 'themepark update started the installer while the launcher was running' }
    Assert-Build $script:Dir '0.0.5' 'stable'
    # its options go to the installer: themepark update -Dev moves the install to main
    $out = Invoke-NewWindow $script:Dir 'powershell.exe' 'themepark update -Dev -NoLaunch'
    Assert-Ok $out '0.0.6-dev'
    Assert-Build $script:Dir '0.0.6-dev' 'dev'
}

# -Dev follows main; the plain line and -Update (which never passes -Dev) move the install to the newest release.
function Invoke-Case_dev {
    New-Case 'dev'
    Initialize-Fake
    New-FakeRelease '0.0.1'
    $out = Invoke-Line @{ Dev = $true; NoLaunch = $true }
    Assert-Ok $out '0.0.2-dev'
    Assert-Build $script:Dir '0.0.2-dev' 'dev'
    $info = @(Get-Content -LiteralPath (Join-Path $script:Dir 'build\install-info.txt'))
    if (-not ($info -ceq "commit=$(Get-FakeCommit 'main')")) { Bad 'the dev install is not at main' }
    Expect-NoText $out 'This install followed main'
    Assert-NewWindow $script:Dir '0.0.2-dev'
    # the plain line: the newest release, saying so
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out '0.0.1'
    Expect-Line $out 'This install followed main (-Dev); it now follows the newest release. Add -Dev to stay on main.'
    Assert-Build $script:Dir '0.0.1' 'stable'
    Assert-UserPathOnce $script:Dir
    Assert-NewWindow $script:Dir '0.0.1'
    # -Dev again: back on main, and no such message
    $out = Invoke-Line @{ Dev = $true; NoLaunch = $true }
    Assert-Ok $out '0.0.2-dev'
    Expect-NoText $out 'This install followed main'
    Assert-Build $script:Dir '0.0.2-dev' 'dev'
    # -Dev with nothing new on main, then with a change on main
    $out = Invoke-Line @{ Dev = $true; NoLaunch = $true }
    Assert-Ok $out '0.0.2-dev'
    Expect-Text $out 'Already up to date: version 0.0.2-dev.'
    Add-FakeDevChange
    $out = Invoke-Line @{ Dev = $true; NoLaunch = $true }
    Assert-Ok $out '0.0.2-dev'
    $info = @(Get-Content -LiteralPath (Join-Path $script:Dir 'build\install-info.txt'))
    if (-not ($info -ceq "commit=$(Get-FakeCommit 'main')")) { Bad 'the dev install is not at the new main' }
    Expect-NoText $out 'Already up to date'
    # -Update, as the Update button runs it, from a dev install
    Set-Location -LiteralPath $script:Dir
    $out = Invoke-File @('-Update', '-Dir', $script:Dir, '-NoLaunch') (Join-Path $script:Dir 'source\install.ps1')
    Assert-Ok $out '0.0.1'
    Expect-Line $out 'This install followed main (-Dev); it now follows the newest release. Add -Dev to stay on main.'
    Assert-Build $script:Dir '0.0.1' 'stable'
    Set-Location -LiteralPath $script:H
    # -Stable says the same as nothing
    $out = Invoke-Line @{ Stable = $true; NoLaunch = $true }
    Assert-Ok $out '0.0.1'
    Expect-Text $out 'Already up to date'
    # a dev install on a PC with no release at all: the plain line and -Update say that it stays on main and how to
    # update it, and never that it now follows a release
    New-Case 'dev_only'
    Initialize-Fake
    $out = Invoke-Line @{ Dev = $true; NoLaunch = $true }
    Assert-Ok $out '0.0.1-dev'
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Fail $out 'No release has been published yet\. This install follows main; run themepark update -Dev to update it\.'
    Expect-Text $out 'themepark update -Dev'
    Expect-NoText $out 'it now follows the newest release'
    Assert-Build $script:Dir '0.0.1-dev' 'dev'
    Set-Location -LiteralPath $script:Dir
    $out = Invoke-File @('-Update', '-Dir', $script:Dir, '-NoLaunch') (Join-Path $script:Dir 'source\install.ps1')
    Assert-Fail $out 'No release has been published yet\. This install follows main; run themepark update -Dev'
    Expect-NoText $out 'it now follows the newest release'
    # the same in the progress view (themepark update in a window): the log of the dev install's build is still there,
    # and a run that failed before it started a log of its own must not show that one and ask for a bug report about it
    if (-not (Get-Item -LiteralPath (Join-Path $script:Dir 'last-build.log')).Length) { Bad 'the dev install left no last-build.log for this check' }
    $env:TPW_INSTALL_VIEW = 'vt'
    $out = Invoke-File @('-Update', '-Dir', $script:Dir, '-NoLaunch') (Join-Path $script:Dir 'source\install.ps1')
    Remove-Item -Path Env:TPW_INSTALL_VIEW -ErrorAction SilentlyContinue
    Set-Location -LiteralPath $script:H
    if ($script:RC -eq 0) { Bad '-Update with no release returned 0 in the progress view' }
    Expect-Text $out 'No release has been published yet'
    Expect-Text $out 'themepark update -Dev'
    Expect-NoText $out 'The last lines of its output'
    Expect-NoText $out 'bug report'
    Expect-NoText $out '==> building version'
    if (@($out | Where-Object { "$_" -match '^(STEP|RESULT) ' })) { Bad 'the progress view printed a line of plain mode' }
    Assert-Build $script:Dir '0.0.1-dev' 'dev'
}

# Which tags are releases: vX.Y.Z by number (v0.0.10 after v0.0.9), nothing else, and a tag must name its version.
function Invoke-Case_tags {
    New-Case 'tags'
    Initialize-Fake
    New-FakeRelease '0.0.9'
    foreach ($t in @('v1.0.0-rc1', 'release-9.9.9', 'v2', '9.9.9')) { [void](Invoke-GitF @('tag', $t)) }
    New-FakeRelease '0.0.10'
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out '0.0.10'
    Expect-Text $out 'Newest release: 0.0.10'
    # a tag whose version file says something else is not installed (and the older release is not either)
    [void](Invoke-GitF @('tag', 'v0.0.11'))
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Fail $out 'The release v0.0.11 says it is version 0.0.11-dev'
    Assert-Build $script:Dir '0.0.10' 'stable'
    # a tag on a tree whose version file is not a version
    New-Case 'tags_bad'
    Initialize-Fake
    Set-FakeVersion 'not a version' 'Broken'
    [void](Invoke-GitF @('tag', 'v0.0.1'))
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Fail $out 'does not name its version'
    if (Test-Path -LiteralPath (Join-Path $script:Dir 'build')) { Bad 'a build was made from a source with no version' }
}

# Refusals: each says why in words, ends with one RESULT fail line, exits not 0 and leaves what is there alone.
function Invoke-Case_refusals {
    New-Case 'refusals'
    Initialize-Fake
    New-FakeRelease '0.0.1'
    $before = Get-HomeListing
    # GitHub (here the repository) cannot be reached
    $env:TPW_INSTALL_REPO = Join-Path $script:C 'does-not-exist'
    $out = Invoke-Line @{ NoLaunch = $true }
    $env:TPW_INSTALL_REPO = $script:Fake
    Assert-Fail $out "Couldn't reach GitHub"
    Assert-Same 'the profile folder (the repository unreachable)' $before (Get-HomeListing)
    # a folder that is somebody else's
    $foreign = Join-Path $script:C 'foreign'
    [void](New-Item -ItemType Directory -Force -Path $foreign)
    Write-Text (Join-Path $foreign 'file.txt') "mine`n"
    $foreignBefore = Get-Snapshot $foreign
    $out = Invoke-Line @{ NoLaunch = $true; Dir = $foreign }
    Assert-Fail $out 'is not empty and not an install'
    Assert-Same 'a foreign folder' $foreignBefore (Get-Snapshot $foreign)
    # folders that must never be the install folder
    foreach ($d in @($script:H, $env:LOCALAPPDATA, $env:APPDATA, 'C:\', $env:SystemRoot, $env:TEMP, (Split-Path -Parent $script:H), $env:ProgramFiles)) {
        $out = Invoke-Line @{ NoLaunch = $true; Dir = $d }
        Assert-Fail $out 'Not using'
    }
    $out = Invoke-File @('nonsense', '-NoLaunch')
    Assert-Fail $out 'Unknown option'
    $out = Invoke-Line @{ Update = $true; NoLaunch = $true }
    Assert-Fail $out 'There is no install in'
    $out = Invoke-Line @{ Stage = $true }
    Assert-Fail $out '-Stage goes with -Update'
    $out = Invoke-Line @{ SwapAndLaunch = $true; NoLaunch = $true }
    Assert-Fail $out 'There is no install in'
    $out = Invoke-Line @{ WaitPid = 'x'; SwapAndLaunch = $true; NoLaunch = $true }
    Assert-Fail $out 'needs a process number'
    Assert-Same 'the profile folder (after the refusals)' $before (Get-HomeListing)
    # an install whose build-next is a junction is not deleted or switched
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out '0.0.1'
    New-FakeRelease '0.0.2'
    $elsewhere = Join-Path $script:C 'elsewhere'
    [void](New-Item -ItemType Directory -Force -Path $elsewhere)
    & cmd.exe /d /c mklink /J (Join-Path $script:Dir 'build-next') $elsewhere 2>&1 | Out-Host
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Fail $out 'is a link'
    Assert-Build $script:Dir '0.0.1' 'stable'
    if (-not (Test-Path -LiteralPath $elsewhere)) { Bad "the junction's target was deleted" }
    & cmd.exe /d /c rmdir (Join-Path $script:Dir 'build-next') 2>&1 | Out-Host
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out '0.0.2'
    # -Help
    $out = Invoke-Line @{ Help = $true }
    if ($script:RC -ne 0) { Bad "-Help exited with $script:RC" }
    Expect-Text $out '-Dir PATH'
}

# Folder names with spaces, apostrophes, non-ASCII letters and shell characters, for the profile folder and the install
# folder. Names the Path cannot hold (; and %) still install, without the command on the Path, and say so.
function Invoke-Case_paths {
    $odd = "Jo's PC " + [char]0xE9 + ' ' + [char]0xFC + ' ' + [char]0x65E5 + [char]0x672C
    New-Case 'paths_home' (Join-Path $Work "paths_home\$odd\home dir")
    Initialize-Fake
    New-FakeRelease '0.0.1'
    Add-SeedData
    $clone = Join-Path $script:H 'Downloads\tpw-port'
    New-OldClone $clone
    $cloneBefore = Get-Snapshot $clone
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out '0.0.1'
    Expect-Line $out "An older copy of the launcher is in $clone."
    # the installer's own new-window check must pass for a name with non-ASCII letters too
    Expect-Text $out 'From now on, type themepark in this window or any new PowerShell window to play.'
    Assert-Build $script:Dir '0.0.1' 'stable'
    Assert-UserPathOnce $script:Dir
    Assert-NewWindow $script:Dir '0.0.1'
    Assert-Same 'the old clone' $cloneBefore (Get-Snapshot $clone)
    # install folders with odd names
    New-Case 'paths_dir'
    Initialize-Fake
    New-FakeRelease '0.0.1'
    $n = 0
    $names = @(("tpw port's " + [char]0xE9), ([string][char]0x65E5 + [char]0x672C + [char]0x8A9E + ' ' + [char]0xFC), 'a (b) & c', 'hash#tag', 'comma, and more')
    foreach ($name in $names) {
        $n += 1
        $d = Join-Path $script:C $name
        $out = Invoke-Line @{ NoLaunch = $true; Dir = $d }
        Assert-Ok $out '0.0.1'
        Assert-Build $d '0.0.1' 'stable'
        Assert-UserPathOnce $d
        Assert-NewWindow $d '0.0.1'
    }
    # a ; or a % cannot be on the Path: the install works, the command is not added, and the player is told
    foreach ($name in @('semi;colon', 'percent %USERPROFILE% x')) {
        $d = Join-Path $script:C $name
        $out = Invoke-Line @{ NoLaunch = $true; Dir = $d }
        Assert-Ok $out '0.0.1'
        Expect-Text $out 'which the Path cannot hold'
        Assert-Build $d '0.0.1' 'stable'
        Assert-UserPathNone $d
    }
    # relative, with a trailing backslash, with .\
    $rel = Join-Path $script:C 'rel'
    [void](New-Item -ItemType Directory -Force -Path $rel)
    Set-Location -LiteralPath $rel
    $out = Invoke-Line @{ NoLaunch = $true; Dir = 'sub dir\' }
    Assert-Ok $out '0.0.1'
    Assert-Build (Join-Path $rel 'sub dir') '0.0.1' 'stable'
    $out = Invoke-Line @{ NoLaunch = $true; Dir = '.\dot dir' }
    Assert-Ok $out '0.0.1'
    Assert-Build (Join-Path $rel 'dot dir') '0.0.1' 'stable'
    $rel2 = Join-Path $script:C 'rel2'
    [void](New-Item -ItemType Directory -Force -Path $rel2)
    Set-Location -LiteralPath $rel2
    $out = Invoke-Line @{ NoLaunch = $true; Dir = '.' }
    Assert-Ok $out '0.0.1'
    Assert-Build $rel2 '0.0.1' 'stable'
    Set-Location -LiteralPath $script:H
}

# Tools the PC lacks: a plain message that lists them, nothing made, exit not 0. (A PC without Git lacks nothing: the
# installer fetches one; the nogit case.)
function Invoke-Case_missing {
    New-Case 'missing'
    Initialize-Fake
    New-FakeRelease '0.0.1'
    $before = Get-HomeListing
    # a Windows without curl.exe, tar.exe and Windows PowerShell (an empty folder stands in for the Windows folder)
    $fakeRoot = Join-Path $script:C 'fake-windows'
    [void](New-Item -ItemType Directory -Force -Path $fakeRoot)
    $env:SystemRoot = $fakeRoot
    $out = Invoke-Line @{ NoLaunch = $true }
    $env:SystemRoot = $Base.SystemRoot
    Assert-Fail $out '^RESULT fail Missing: curl.exe, tar.exe, Windows PowerShell'
    Expect-Text $out 'Update Windows'
    Expect-Text $out 'Nothing was changed.'
    Assert-Same 'the profile folder (Windows tools missing)' $before (Get-HomeListing)
    if (Test-Path -LiteralPath $script:Dir) { Bad 'a run with tools missing made the install folder' }
    # the missing tools, then they are there: the next run installs
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out '0.0.1'
    # with an install in place, a missing tool leaves it working
    New-FakeRelease '0.0.2'
    $env:SystemRoot = $fakeRoot
    $out = Invoke-Line @{ NoLaunch = $true }
    $env:SystemRoot = $Base.SystemRoot
    Assert-Fail $out '^RESULT fail Missing: curl.exe, tar.exe, Windows PowerShell'
    Assert-Build $script:Dir '0.0.1' 'stable'
    Assert-NewWindow $script:Dir '0.0.1'
}

# A PC without Git: the installer fetches MinGit into DIR\tools\MinGit (checked against the SHA-256 pinned in
# install.ps1), clones, checks out the submodule and builds with it, and puts nothing on the user's Path for it. A later
# run and the launcher's updater, still without Git, use the fetched one without fetching again; with its checksum file
# gone it is fetched again.
function Invoke-Case_nogit {
    New-Case 'nogit'
    Initialize-Fake
    New-FakeRelease '0.0.1'
    $noGit = "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\WindowsPowerShell\v1.0"
    $minGit = Join-Path $script:Dir 'tools\MinGit'
    $env:Path = $noGit
    if (Get-Command git.exe -CommandType Application -ErrorAction SilentlyContinue) { Bad 'git.exe is still found with Git off the Path' }
    $out = Invoke-Line @{ NoLaunch = $true }
    $env:Path = $Base.Path
    Assert-Ok $out '0.0.1'
    Expect-Text $out 'Fetching Git (MinGit'
    Expect-Text $out 'its SHA-256 checksum matched'
    Expect-Text $out 'Git: git version'
    Expect-NoText $out 'winget'
    foreach ($f in @('cmd\git.exe', '.sha256')) {
        if (-not (Test-Path -LiteralPath (Join-Path $minGit $f))) { Bad "$minGit\$f is missing" }
    }
    if (Test-Path -LiteralPath (Join-Path $script:Dir 'tools\MinGit.tmp')) { Bad 'the fetch left tools\MinGit.tmp behind' }
    Assert-Build $script:Dir '0.0.1' 'stable'
    $tagCommit = Get-FakeCommit 'v0.0.1'
    $srcHead = "$(& (Join-Path $minGit 'cmd\git.exe') -C (Join-Path $script:Dir 'source') rev-parse HEAD 2>&1)".Trim()
    Expect-Eq "the source's commit (read with the fetched Git)" $srcHead $tagCommit
    if (-not (Test-Path -LiteralPath (Join-Path $script:Dir 'source\tpw-decomp\README'))) { Bad 'the submodule was not downloaded with the fetched Git' }
    Assert-UserPathOnce $script:Dir
    foreach ($e in Get-UserPathEntries) { if ("$e" -like '*MinGit*') { Bad "the user's Path got an entry for the fetched Git: $e" } }
    Assert-NewWindow $script:Dir '0.0.1'
    $snap = Get-Snapshot $minGit
    # the line again, still without Git: the fetched one is used, not fetched again
    New-FakeRelease '0.0.2'
    $env:Path = $noGit
    $out = Invoke-Line @{ NoLaunch = $true }
    $env:Path = $Base.Path
    Assert-Ok $out '0.0.2'
    Expect-NoText $out 'Fetching Git'
    Expect-Text $out 'Git fetched earlier'
    Assert-Same 'the fetched Git' $snap (Get-Snapshot $minGit)
    Assert-Build $script:Dir '0.0.2' 'stable'
    # the launcher's updater (the file, in a new Windows PowerShell), still without Git
    New-FakeRelease '0.0.3'
    $installer = Join-Path $script:Dir 'source\install.ps1'
    $env:Path = $noGit
    $out = Invoke-File @('-Update', '-Stage', '-Dir', $script:Dir) $installer
    $env:Path = $Base.Path
    Assert-Ok $out '0.0.3'
    Expect-NoText $out 'Fetching Git'
    Expect-Text $out 'Git fetched earlier'
    $next = @(Get-Content -LiteralPath (Join-Path $script:Dir 'build-next\install-info.txt'))
    if (-not ($next -ceq 'version=0.0.3')) { Bad 'build-next is not version 0.0.3' }
    Assert-Build $script:Dir '0.0.2' 'stable'
    Assert-Same 'the fetched Git after the updater' $snap (Get-Snapshot $minGit)
    # without its checksum file the fetched Git is fetched again, and the install goes on
    Remove-Item -LiteralPath (Join-Path $minGit '.sha256') -Force
    $env:Path = $noGit
    $out = Invoke-Line @{ NoLaunch = $true }
    $env:Path = $Base.Path
    Assert-Ok $out '0.0.3'
    Expect-Text $out 'Fetching Git (MinGit'
    Assert-Same 'the fetched Git after a second fetch' $snap (Get-Snapshot $minGit)
    Assert-Build $script:Dir '0.0.3' 'stable'
    Assert-NewWindow $script:Dir '0.0.3'
}

# A build that fails: no launcher, no command, no Path entry on a first install; the working one stays on an update.
function Invoke-Case_buildfail {
    New-Case 'buildfail'
    Initialize-Fake
    New-FakeRelease '0.0.1'
    $env:TPW_FAKE_BUILD = 'fail'
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Fail $out 'The build failed'
    Expect-Text $out 'fake compiler error'
    if (Test-Path -LiteralPath (Join-Path $script:Dir 'build')) { Bad 'a failed first build left a build folder' }
    if (Test-Path -LiteralPath (Join-Path $script:Dir 'bin')) { Bad 'a failed first build left the themepark command' }
    if (Test-Path -LiteralPath (Join-Path $script:Dir 'channel.txt')) { Bad 'a failed first build left a channel.txt' }
    Assert-UserPathNone $script:Dir
    # a build that ends well but made nothing, or a launcher of another version: refused before anything is switched
    $env:TPW_FAKE_BUILD = 'empty'
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Fail $out 'made no tpw_launcher.exe'
    $env:TPW_FAKE_BUILD = 'wrongversion'
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Fail $out 'did not answer with version 0.0.1'
    if (Test-Path -LiteralPath (Join-Path $script:Dir 'build')) { Bad 'a wrong build was switched in' }
    Assert-UserPathNone $script:Dir
    Remove-Item -Path Env:TPW_FAKE_BUILD -ErrorAction SilentlyContinue
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out '0.0.1'
    Assert-NewWindow $script:Dir '0.0.1'
    New-FakeRelease '0.0.2'
    $env:TPW_FAKE_BUILD = 'fail'
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Fail $out 'The build failed'
    Expect-Text $out 'The launcher in use'
    Assert-Build $script:Dir '0.0.1' 'stable'
    Assert-NewWindow $script:Dir '0.0.1'
    # the updater's stage fails the same way, with the launcher in use as it was
    Set-Location -LiteralPath $script:Dir
    $out = Invoke-File @('-Update', '-Stage', '-Dir', $script:Dir) (Join-Path $script:Dir 'source\install.ps1')
    Assert-Fail $out 'The build failed'
    Assert-Build $script:Dir '0.0.1' 'stable'
    Set-Location -LiteralPath $script:H
    Remove-Item -Path Env:TPW_FAKE_BUILD -ErrorAction SilentlyContinue
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out '0.0.2'
    Assert-Build $script:Dir '0.0.2' 'stable'
}

# Stopped in the middle of the build, the way the updater stops it (the whole process tree ended) or a power cut would: a
# half build-next is left, the launcher in use works, and the next run recovers. (Ctrl+C's RESULT line needs a console
# and is not tested here.)
function Invoke-Case_interrupt {
    New-Case 'interrupt'
    Initialize-Fake
    New-FakeRelease '0.0.1'
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out '0.0.1'
    Add-SeedData
    $dataBefore = Get-Snapshot $script:Data

    Note 'the tree ended during the build of 0.0.2'
    New-FakeRelease '0.0.2'
    $p = Start-SlowInstall $InstallPs1 @('-NoLaunch', '-Dir', $script:Dir) (Join-Path $script:C 'o2.txt')
    Stop-Tree $p
    Get-Content -LiteralPath (Join-Path $script:C 'o2.txt') | ForEach-Object { Write-Host $_ }
    Assert-NoFakeBuildLeft $script:Dir
    if (-not (Test-Path -LiteralPath (Join-Path $script:Dir 'build-next'))) { Bad 'no half build was left to recover from' }
    Assert-Build $script:Dir '0.0.1' 'stable'
    Assert-NewWindow $script:Dir '0.0.1'
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out '0.0.2'
    Assert-Build $script:Dir '0.0.2' 'stable'
    if (Test-Path -LiteralPath (Join-Path $script:Dir 'build-next\half')) { Bad 'the half build is still in build-next' }
    Assert-UserPathOnce $script:Dir
    Assert-NewWindow $script:Dir '0.0.2'

    Note "the tree ended during the updater's stage of 0.0.3"
    New-FakeRelease '0.0.3'
    Set-Location -LiteralPath $script:Dir
    $p = Start-SlowInstall (Join-Path $script:Dir 'source\install.ps1') @('-Update', '-Stage', '-Dir', $script:Dir) (Join-Path $script:C 'o3.txt')
    Stop-Tree $p
    Get-Content -LiteralPath (Join-Path $script:C 'o3.txt') | ForEach-Object { Write-Host $_ }
    Assert-NoFakeBuildLeft $script:Dir
    Assert-Build $script:Dir '0.0.2' 'stable'
    # a swap with a half-made build-next must not switch to it
    $out = Invoke-File @('-SwapAndLaunch', '-NoLaunch', '-Dir', $script:Dir) (Join-Path $script:Dir 'source\install.ps1')
    Assert-Build $script:Dir '0.0.2' 'stable'
    Set-Location -LiteralPath $script:H
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out '0.0.3'
    Assert-Build $script:Dir '0.0.3' 'stable'
    Assert-Same 'the data folder' $dataBefore (Get-Snapshot $script:Data)
}

# A first install stopped in the build: nothing in place, and the next run installs.
function Invoke-Case_interrupt_first {
    New-Case 'interrupt_first'
    Initialize-Fake
    New-FakeRelease '0.0.1'
    $p = Start-SlowInstall $InstallPs1 @('-NoLaunch', '-Dir', $script:Dir) (Join-Path $script:C 'o1.txt')
    Stop-Tree $p
    Get-Content -LiteralPath (Join-Path $script:C 'o1.txt') | ForEach-Object { Write-Host $_ }
    Assert-NoFakeBuildLeft $script:Dir
    if (Test-Path -LiteralPath (Join-Path $script:Dir 'build')) { Bad 'a build folder of a stopped first install' }
    if (Test-Path -LiteralPath (Join-Path $script:Dir 'bin')) { Bad 'a themepark command of a stopped first install' }
    if (Test-Path -LiteralPath (Join-Path $script:Dir 'channel.txt')) { Bad 'a channel.txt of a stopped first install' }
    Assert-UserPathNone $script:Dir
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out '0.0.1'
    Assert-Build $script:Dir '0.0.1' 'stable'
    Assert-UserPathOnce $script:Dir
    Assert-NewWindow $script:Dir '0.0.1'
}

# A local release of the CI checkout for the case: TPW_CI_SOURCE is the checkout (a git working tree of tpw-port). Takes
# a copy of it into the case's folder, says its launcher\VERSION.txt is release VERSION and tags that; the installer is
# pointed at it (TPW_INSTALL_REPO).
function New-RealRelease([string]$version) {
    if (-not $env:TPW_CI_SOURCE) { Bad 'TPW_CI_SOURCE (the checkout to build) is not set' }
    $source = $env:TPW_CI_SOURCE
    $repo = Join-Path $script:C 'release-repo'
    & git.exe clone -q $source $repo 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) { Bad 'cannot copy the checkout' }
    [void](Invoke-Git $repo @('checkout', '-q', '-B', 'main'))
    Write-Text (Join-Path $repo 'launcher\VERSION.txt') "$version`n"
    [void](Invoke-Git $repo @('add', 'launcher/VERSION.txt'))
    [void](Invoke-Git $repo @('commit', '-q', '-m', "Release $version"))
    [void](Invoke-Git $repo @('tag', "v$version"))
    # The submodules' addresses are relative (../tpw-decomp), so they resolve beside the release repository: bare copies
    # of the checkout's submodules there, each with a branch on the pinned commit (no download from GitHub).
    $modules = @(& git.exe -C $source config -f .gitmodules --get-regexp '\.path$' 2>&1 | ForEach-Object { "$_" })
    if ($LASTEXITCODE -ne 0 -or $modules.Count -eq 0) { Bad 'the checkout has no .gitmodules' }
    foreach ($line in $modules) {
        $key, $path = $line -split ' ', 2
        $url = "$(& git.exe -C $source config -f .gitmodules --get ($key -replace '\.path$', '.url') 2>&1)".Trim()
        if (-not $url.StartsWith('../')) { Bad "the submodule $path has the address $url, not a relative one" }
        $bare = Join-Path $script:C $url.Substring(3)
        & git.exe clone -q --bare (Join-Path $source $path) $bare 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) { Bad "cannot copy the submodule $path" }
        $pinned = "$(& git.exe -C (Join-Path $source $path) rev-parse HEAD 2>&1)".Trim()
        & git.exe -C $bare branch -f ci-pin $pinned 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) { Bad "cannot pin the submodule $path" }
    }
    $env:TPW_INSTALL_REPO = $repo
}

# The real build, from a local tag in the CI checkout: TPW_CI_SOURCE is the checkout (a git working tree of tpw-port, with
# its submodules). Takes a copy of it, says its launcher\VERSION.txt is a release (TPW_CI_VERSION, default 0.0.99) and tags
# that. The run is the one a player makes, with Git on the Path and nothing else of the toolchain (build.ps1 fetches its
# own), in a profile folder with a space, an apostrophe and a non-ASCII letter.
function Invoke-Case_real {
    $version = $env:TPW_CI_VERSION
    if (-not $version) { $version = '0.0.99' }
    New-Case 'real' (Join-Path $Work ("real\Jo's PC " + [char]0xE9 + '\home'))
    $script:Quiet = $true
    Add-SeedData
    $clone = Join-Path $script:H 'tpw-port'
    [void](New-Item -ItemType Directory -Force -Path (Join-Path $clone '.git'), (Join-Path $clone 'launcher'), (Join-Path $clone 'build'))
    Write-Text (Join-Path $clone 'launcher\VERSION.txt') "0.0.2`n"
    Write-Text (Join-Path $clone 'build\tpw_launcher.exe') "an old launcher`n"
    $cloneBefore = Get-Snapshot $clone
    $dataBefore = Get-Snapshot $script:Data
    New-RealRelease $version
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out $version
    Assert-Build $script:Dir $version 'stable'
    Assert-Shortcut $script:Dir -Icon
    $first = Get-VersionLine (Join-Path $script:Dir 'build')
    Write-Host "the launcher says: $first"
    Expect-Line $out "An older copy of the launcher is in $clone."
    Assert-UserPathOnce $script:Dir
    Assert-NewWindowReal $script:Dir
    # again: nothing to do
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out $version
    Expect-Text $out 'Already up to date'
    Assert-Same 'the old clone' $cloneBefore (Get-Snapshot $clone)
    Assert-Same 'the data folder' $dataBefore (Get-Snapshot $script:Data)
}

# ---- the screenshots (screenshots.yml)

function Add-ShotApi {
    if ('TpwShots.Front' -as [type]) { return }
    Add-Type -AssemblyName System.Drawing, System.Windows.Forms
    Add-Type -Namespace TpwShots -Name Front -MemberDefinition @'
[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
[DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder text, int count);
[DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam, uint flags, uint timeout, out IntPtr result);
'@
}

# The window in front, as Windows sees it (GetForegroundWindow): "name=NAME pid=PID exe=PATH title=TITLE".
function Get-FrontApp {
    Add-ShotApi
    $h = [TpwShots.Front]::GetForegroundWindow()
    $procId = [uint32]0
    [void][TpwShots.Front]::GetWindowThreadProcessId($h, [ref]$procId)
    $p = Get-Process -Id $procId -ErrorAction SilentlyContinue
    $sb = New-Object System.Text.StringBuilder 512
    [void][TpwShots.Front]::GetWindowText($h, $sb, 512)
    $exe = ''
    if ($p) { try { $exe = "$($p.Path)" } catch { $exe = '' } }
    return "name=$(if ($p) { $p.ProcessName } else { 'none' }) pid=$procId exe=$exe title=$($sb.ToString())"
}

# The whole screen (every monitor) as FILE (PNG), a second from now; says its size, or that the capture failed.
function Save-Shot([string]$file) {
    Add-ShotApi
    Start-Sleep -Seconds 1
    try {
        $r = [System.Windows.Forms.SystemInformation]::VirtualScreen
        $bmp = New-Object System.Drawing.Bitmap $r.Width, $r.Height
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
        $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
        $g.Dispose()
        $bmp.Dispose()
        Note "$(Split-Path -Leaf $file): $($r.Width)x$($r.Height) pixels"
    } catch {
        Note "$(Split-Path -Leaf $file): the capture failed: $_"
    }
}

# A visible Windows PowerShell window, without a profile and with the Path a new window gets (the machine's and the
# user's, from the registry), cleared, showing its prompt; LINE, when given, runs in it after the prompt is shown.
# Returns the process.
function Start-ShotWindow([string]$line = '') {
    $setup = "`$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')`nClear-Host`n"
    if ($line) { $setup += "Write-Host 'PS> $line'`n$line`n" }
    $encoded = [Convert]::ToBase64String([System.Text.Encoding]::Unicode.GetBytes($setup))
    $p = Start-Process -FilePath powershell.exe -ArgumentList @('-NoLogo', '-NoProfile', '-NoExit', '-EncodedCommand', $encoded) -PassThru
    Start-Sleep -Seconds 3
    try { [void][TpwShots.Front]::SetForegroundWindow($p.MainWindowHandle) } catch { }
    Start-Sleep -Seconds 1
    return $p
}

# The part of the screen outside the main screen's working area (the taskbar) as FILE (PNG).
function Save-TaskbarShot([string]$file) {
    Add-ShotApi
    try {
        $screen = [System.Windows.Forms.Screen]::PrimaryScreen
        $b = $screen.Bounds
        $w = $screen.WorkingArea
        $r = New-Object System.Drawing.Rectangle $b.Left, $w.Bottom, $b.Width, ($b.Bottom - $w.Bottom)
        if ($r.Height -le 0) { $r = New-Object System.Drawing.Rectangle $b.Left, $b.Top, $b.Width, ($w.Top - $b.Top) }
        if ($r.Height -le 0) { throw 'the working area is the whole screen (no taskbar found at the top or the bottom)' }
        $bmp = New-Object System.Drawing.Bitmap $r.Width, $r.Height
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
        $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
        $g.Dispose()
        $bmp.Dispose()
        Note "$(Split-Path -Leaf $file): $($r.Width)x$($r.Height) pixels at $($r.Left),$($r.Top)"
    } catch {
        Note "$(Split-Path -Leaf $file): the capture failed: $_"
    }
}

# The big icon WINDOW carries (WM_GETICON, ICON_BIG, as Alt+Tab and the taskbar ask it) as FILE (PNG); WHAT names it.
function Save-WindowIcon([IntPtr]$window, [string]$file, [string]$what) {
    try {
        $hIcon = [IntPtr]::Zero
        $one = New-Object IntPtr 1
        # 0x7F is WM_GETICON, 1 is ICON_BIG, 2 is SMTO_ABORTIFHUNG
        [void][TpwShots.Front]::SendMessageTimeout($window, 0x7F, $one, [IntPtr]::Zero, 2, 3000, [ref]$hIcon)
        if ($hIcon -eq [IntPtr]::Zero) { throw 'the window carries no big icon (WM_GETICON gave nothing)' }
        $icon = [System.Drawing.Icon]::FromHandle($hIcon)
        $icon.ToBitmap().Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
        Note "the icon of ${what}: $($icon.Width)x$($icon.Height) pixels"
    } catch {
        Note "no icon from ${what}: $_"
    }
}

function Get-OurLauncher([string]$exe) {
    return @(Get-Process -Name tpw_launcher -ErrorAction SilentlyContinue | Where-Object { try { $_.Path -ieq $exe } catch { $false } })
}

# Pictures of the launcher installed for real (New-RealRelease, the install line with -NoLaunch), for the eye: a
# PowerShell window in front, then themepark typed in it as a player types it (SendKeys; when nothing starts that way,
# a new window runs the line itself), then the launcher's window, which must have come to the front (the focus check:
# the foreground window after the command must belong to the launcher of this install, else the case fails, after the
# pictures are taken), then the launcher alone, the icon its window carries and the icon of tpw_launcher.exe. Then the
# game's own process without a game (tpw_launcher --game-app-test us: "Sim Theme Park.exe" beside tpw_launcher.exe,
# started as the window's Play starts it, with a bare window named after the game): the screen with the game's window
# and the taskbar, the taskbar alone, the Task Manager, the icons of the game's window and of Sim Theme Park.exe, what
# Windows says of the process (game-process.txt), and the launcher's window back after it ("Back to the launcher after
# playing" on); the case fails when the game's process, its name or the launcher's return is not as it should be. The
# pictures, the focus check's lines and the launcher's and the game's log lines go to TPW_CI_SHOTS (default: the case's
# shots folder).
# screenshots.yml keeps that folder as the run's artifact.
function Invoke-Case_shots {
    $version = $env:TPW_CI_VERSION
    if (-not $version) { $version = '0.0.99' }
    New-Case 'shots'
    $script:Quiet = $true
    $shots = $env:TPW_CI_SHOTS
    if (-not $shots) { $shots = Join-Path $script:C 'shots' }
    [void](New-Item -ItemType Directory -Force -Path $shots)
    $focus = Join-Path $shots 'focus-check.txt'
    [void](New-Item -ItemType File -Force -Path $focus)
    New-RealRelease $version
    $out = Invoke-Line @{ NoLaunch = $true }
    Assert-Ok $out $version
    Assert-Build $script:Dir $version 'stable'
    Assert-UserPathOnce $script:Dir
    $exe = Join-Path $script:Dir 'build\tpw_launcher.exe'
    Add-ShotApi
    $r = [System.Windows.Forms.SystemInformation]::VirtualScreen
    Note "screen: $($r.Width)x$($r.Height) at $($r.Left),$($r.Top); logged-in desktop: $([Environment]::UserInteractive)"
    # 1. a PowerShell window in front, with the Path a new window gets (themepark is on it)
    $win = Start-ShotWindow
    $before = Get-FrontApp
    Note "in front before: $before"
    Add-Content -LiteralPath $focus -Value "before: $before" -Encoding ASCII
    Save-Shot (Join-Path $shots '1-before-powershell-in-front.png')
    # 2. themepark, typed; a few seconds later the launcher's window is up and must be the foreground window
    $typed = $true
    try {
        [void][TpwShots.Front]::SetForegroundWindow($win.MainWindowHandle)
        Start-Sleep -Milliseconds 500
        [System.Windows.Forms.SendKeys]::SendWait('themepark{ENTER}')
    } catch {
        Note "typing into the window failed: $_"
        $typed = $false
    }
    $started = $false
    if ($typed) {
        foreach ($i in 1..10) {
            if ((Get-OurLauncher $exe).Count -gt 0) { $started = $true; break }
            Start-Sleep -Seconds 1
        }
        if (-not $started) { Note 'nothing started from the typed line; a new window runs it instead'; $typed = $false }
    }
    if (-not $typed) {
        Stop-Process -Id $win.Id -Force -ErrorAction SilentlyContinue
        $win = Start-ShotWindow 'themepark'
    }
    Start-Sleep -Seconds 8
    $after = Get-FrontApp
    Note "in front after: $after"
    Add-Content -LiteralPath $focus -Value "after: $after" -Encoding ASCII
    Save-Shot (Join-Path $shots '2-after-themepark.png')
    # 3. the launcher alone (the PowerShell window closed), then its icons: the one its window carries (the title bar's
    # and the taskbar's, the launcher sets it when it starts; asked of the window with WM_GETICON, ICON_BIG), and the one
    # the Explorer shows for tpw_launcher.exe (the file's own)
    Stop-Process -Id $win.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 3
    Save-Shot (Join-Path $shots '3-launcher-alone.png')
    try {
        $launcherWindows = @(Get-OurLauncher $exe | Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero })
        if ($launcherWindows.Count -eq 0) { throw 'the launcher has no main window' }
        $hIcon = [IntPtr]::Zero
        $one = New-Object IntPtr 1
        # 0x7F is WM_GETICON, 1 is ICON_BIG, 2 is SMTO_ABORTIFHUNG
        [void][TpwShots.Front]::SendMessageTimeout($launcherWindows[0].MainWindowHandle, 0x7F, $one, [IntPtr]::Zero, 2, 3000, [ref]$hIcon)
        if ($hIcon -eq [IntPtr]::Zero) { throw 'the window carries no big icon (WM_GETICON gave nothing)' }
        $windowIcon = [System.Drawing.Icon]::FromHandle($hIcon)
        $windowIcon.ToBitmap().Save((Join-Path $shots '4-launcher-window-icon.png'), [System.Drawing.Imaging.ImageFormat]::Png)
        Note "the icon of the launcher's window: $($windowIcon.Width)x$($windowIcon.Height) pixels"
    } catch {
        Note "no icon from the launcher's window: $_"
    }
    try {
        $icon = [System.Drawing.Icon]::ExtractAssociatedIcon($exe)
        $icon.ToBitmap().Save((Join-Path $shots '5-launcher-exe-icon.png'), [System.Drawing.Imaging.ImageFormat]::Png)
        Note "the icon of tpw_launcher.exe: $($icon.Width)x$($icon.Height) pixels"
    } catch {
        Note "no icon from tpw_launcher.exe: $_"
    }
    # the launcher's log: its version line, its icon, its screens and its window
    Start-Sleep -Seconds 2
    Get-OurLauncher $exe | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
    # (the one log file, logs\launcher.log: every run adds to it under its own "==== run N | ..." line)
    $log = Join-Path $script:Data 'logs\launcher.log'
    if (Test-Path -LiteralPath $log) {
        Copy-Item -LiteralPath $log -Destination (Join-Path $shots 'launcher.log')
        $lines = @(Get-Content -LiteralPath $log)
        $picked = @($lines | Where-Object { $_ -match '^==== run ' } | Select-Object -Last 1) + @($lines | Where-Object { $_ -match 'app icon:|app name|platform: (display|window|fit)|^launcher: ' })
        Set-Content -LiteralPath (Join-Path $shots 'launcher-log-lines.txt') -Value $picked -Encoding ASCII
        Note "the launcher's log, $($log):"
        $picked | ForEach-Object { Write-Host $_ }
    } else {
        Note 'the launcher wrote no log file'
    }
    $gameProblems = @(Save-GameProcessShots $exe $shots)
    Add-Content -LiteralPath $focus -Value "typed in the window: $(if ($typed) { 'yes' } else { 'no (a new window ran the line itself)' })" -Encoding ASCII
    # the focus check: the foreground window's program is this install's launcher
    $frontExe = ($after -replace '^.* exe=', '') -replace ' title=.*$', ''
    if ($frontExe -ieq $exe) {
        Add-Content -LiteralPath $focus -Value "result: ok (the foreground window is $frontExe)" -Encoding ASCII
        Get-Content -LiteralPath $focus | ForEach-Object { Write-Host $_ }
    } else {
        Add-Content -LiteralPath $focus -Value "result: FAIL (the foreground window is $($after -replace '^name=', ''), not $exe)" -Encoding ASCII
        Get-Content -LiteralPath $focus | ForEach-Object { Write-Host $_ }
        Bad "the launcher's window did not come to the front after themepark: in front was $($after -replace '^name=', '')"
    }
    if ($gameProblems.Count -gt 0) { Bad "the game's own process: $($gameProblems -join '; ')" }
}

# The game's own process (launcher/game_app.h; port_CONTRIBUTING.md, "The game's own process") for the pictures, with no
# game: tpw_launcher --game-app-test us, started as Explorer starts a program (no console: the launcher and the game's
# process both write what they print into the one log file, logs\launcher.log, each under its own run header line).
# Returns what was not as it should be (empty when all was).
function Save-GameProcessShots([string]$exe, [string]$shots) {
    $problems = @()
    $gameExe = Join-Path (Split-Path -Parent $exe) 'Sim Theme Park.exe'
    $info = Join-Path $shots 'game-process.txt'
    # "Back to the launcher after playing" on, as a player sets it in the Settings box
    $ini = Join-Path $script:Data 'launcher.ini'
    if (Test-Path -LiteralPath $ini) { Add-Content -LiteralPath $ini -Value 'back_to_launcher=1' -Encoding ASCII }
    else { Set-Content -LiteralPath $ini -Value 'back_to_launcher=1' -Encoding ASCII }
    # the runs the log file holds already, so only this test's are looked at
    $launcherLog = Join-Path $script:Data 'logs\launcher.log'
    $runsBefore = 0
    if (Test-Path -LiteralPath $launcherLog) {
        $runsBefore = @(Get-Content -LiteralPath $launcherLog | Where-Object { $_ -match '^==== run \d+ \|' }).Count
    }
    $t0 = Get-Date
    $test = Start-Process -FilePath $exe -ArgumentList @('--game-app-test', 'us', '--frames', '900') -WorkingDirectory (Split-Path -Parent $exe) -PassThru
    $game = $null
    foreach ($i in 1..40) {
        $game = @(Get-Process -Name 'Sim Theme Park' -ErrorAction SilentlyContinue | Where-Object { try { $_.Path -ieq $gameExe } catch { $false } } |
                  Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero }) | Select-Object -First 1
        if ($game) { break }
        Start-Sleep -Milliseconds 500
    }
    if (-not $game) {
        $problems += "no Sim Theme Park.exe process with a window within 20 s"
        Note "the game's process did not show a window (the launcher's process: $(if ($test.HasExited) { "exited with $($test.ExitCode)" } else { 'still running' }))"
    } else {
        Note "the game's process is up $([int]((Get-Date) - $t0).TotalMilliseconds) ms after the launcher started"
        try { [void][TpwShots.Front]::SetForegroundWindow($game.MainWindowHandle) } catch { }
        Start-Sleep -Seconds 2
        Save-Shot (Join-Path $shots '6-game-window-and-taskbar.png')
        Save-TaskbarShot (Join-Path $shots '7-taskbar.png')
        Save-WindowIcon $game.MainWindowHandle (Join-Path $shots '8-game-window-icon.png') "the game's window"
        try {
            $icon = [System.Drawing.Icon]::ExtractAssociatedIcon($gameExe)
            $icon.ToBitmap().Save((Join-Path $shots '9-game-exe-icon.png'), [System.Drawing.Imaging.ImageFormat]::Png)
            Note "the icon of Sim Theme Park.exe: $($icon.Width)x$($icon.Height) pixels"
        } catch {
            Note "no icon from Sim Theme Park.exe: $_"
        }
        $game.Refresh()
        $version = $game.MainModule.FileVersionInfo
        $launcherVersion = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($exe)
        $lines = @(
            "process name (the Details tab): $($game.ProcessName).exe",
            "description (the Processes tab): $($game.Description)",
            "program: $($game.Path)",
            "window title: $($game.MainWindowTitle)",
            "version resource: FileDescription=$($version.FileDescription) ProductName=$($version.ProductName) OriginalFilename=$($version.OriginalFilename) FileVersion=$($version.FileVersion)",
            "the launcher's: FileDescription=$($launcherVersion.FileDescription) OriginalFilename=$($launcherVersion.OriginalFilename) FileVersion=$($launcherVersion.FileVersion)",
            "sizes: tpw_launcher.exe $((Get-Item -LiteralPath $exe).Length) bytes, Sim Theme Park.exe $((Get-Item -LiteralPath $gameExe).Length) bytes",
            "launcher process: $(if ($test.HasExited) { "exited with $($test.ExitCode)" } else { 'still running' })")
        Set-Content -LiteralPath $info -Value $lines -Encoding UTF8
        $lines | ForEach-Object { Note $_ }
        if ($game.Description -ne 'Sim Theme Park') { $problems += "the Processes tab would show `"$($game.Description)`", not Sim Theme Park" }
        if ($game.MainWindowTitle -notmatch '^Sim Theme Park') { $problems += "the game's window is titled `"$($game.MainWindowTitle)`"" }
        if ($version.OriginalFilename -ne 'Sim Theme Park.exe') { $problems += "OriginalFilename is `"$($version.OriginalFilename)`"" }
        # the Task Manager as a player opens it (Ctrl+Shift+Esc), with the game running
        try {
            $tm = Start-Process -FilePath taskmgr.exe -PassThru
            Start-Sleep -Seconds 5
            Save-Shot (Join-Path $shots '10-task-manager.png')
            Stop-Process -Id $tm.Id -Force -ErrorAction SilentlyContinue
        } catch {
            Note "no Task Manager: $_"
        }
    }
    # the launcher's own process exits once the game's window is up; the game's ends after its passes, and the
    # launcher's window opens again
    [void]$test.WaitForExit(35000)
    $back = $false
    foreach ($i in 1..60) {
        if (@(Get-OurLauncher $exe | Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero }).Count -gt 0 -and
            @(Get-Process -Name 'Sim Theme Park' -ErrorAction SilentlyContinue).Count -eq 0) { $back = $true; break }
        Start-Sleep -Milliseconds 500
    }
    if ($back) {
        Note "the launcher's window is back after the game, $([int]((Get-Date) - $t0).TotalSeconds) s after the start"
        Start-Sleep -Seconds 2
        Save-Shot (Join-Path $shots '11-back-to-the-launcher.png')
        Add-Content -LiteralPath $info -Value 'back to the launcher: yes' -Encoding UTF8
    } else {
        $problems += "the launcher's window did not come back after the game"
        Add-Content -LiteralPath $info -Value 'back to the launcher: no' -Encoding UTF8
    }
    if (-not $test.HasExited) { $problems += "the launcher's process that started the game was still running" }
    elseif ($test.ExitCode -ne 0) { $problems += "the launcher's process that started the game exited with $($test.ExitCode)" }
    Start-Sleep -Seconds 2
    Get-OurLauncher $exe | Stop-Process -Force -ErrorAction SilentlyContinue
    Get-Process -Name 'Sim Theme Park' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
    # the log: the runs this test added to the one log file (the launcher's that started the game, the game's, and the
    # launcher's that opened again after it), with what each printed to standard error in its own run
    $picked = @()
    $runsAdded = 0
    $gameRuns = 0
    $legacy = @(Get-ChildItem -LiteralPath (Join-Path $script:Data 'logs') -Filter 'launcher-*.log' -ErrorAction SilentlyContinue) +
              @(@('launcher.log', 'game.log') | ForEach-Object { Join-Path $script:Data $_ } | Where-Object { Test-Path -LiteralPath $_ })
    if ($legacy.Count -gt 0) { $problems += "old log files beside the one log file: $(($legacy | ForEach-Object { "$_" }) -join ', ')" }
    if (Test-Path -LiteralPath $launcherLog) {
        Copy-Item -LiteralPath $launcherLog -Destination (Join-Path $shots 'test-launcher.log')
        $run = 0
        foreach ($line in @(Get-Content -LiteralPath $launcherLog)) {
            if ($line -match '^==== run \d+ \|') {
                $run++
                if ($run -gt $runsBefore) {
                    $runsAdded++
                    if ($line -match 'the game''s own process') { $gameRuns++ }
                    $picked += "== $line"
                }
                continue
            }
            if ($run -gt $runsBefore -and $line -match '^args: |game app|app id|taskbar|game program|app bundle|app icon|app name|opened again|^play: |^exit: ') { $picked += $line }
        }
    }
    if ($runsAdded -eq 0) { $picked += "(no run in $launcherLog from this test)" }
    elseif ($runsAdded -lt 3) { $problems += "the log file holds $runsAdded runs from this test, not 3 (the launcher's, the game's, the launcher's again)" }
    if ($runsAdded -gt 0 -and $gameRuns -eq 0) { $problems += "no run header of the game's own process in the log file" }
    Set-Content -LiteralPath (Join-Path $shots 'game-process-log-lines.txt') -Value $picked -Encoding UTF8
    $picked | ForEach-Object { Write-Host $_ }
    if (-not ($picked -match 'game app test: Sim Theme Park started as ')) { $problems += 'no "game app test: ... started as" line in the logs' }
    if (-not ($picked -match 'app id: org\.tpw-port\.launcher\.stp')) { $problems += 'no "app id:" line in the logs' }
    if (-not ($picked -match 'taskbar: the game''s window is Sim Theme Park''s')) { $problems += 'no "taskbar:" line in the logs' }
    return $problems
}

# The one-liner a player runs, against the newest real release (or main with PUBLIC_ARGS=-Dev), in an empty profile
# folder that holds an old clone and a data folder.
function Invoke-Case_public {
    New-Case 'public'
    $script:Quiet = $true
    Remove-Item -Path Env:TPW_INSTALL_REPO -ErrorAction SilentlyContinue
    $options = @{}
    $channel = 'stable'
    foreach ($a in @("$env:PUBLIC_ARGS" -split ' ' | Where-Object { $_ })) {
        if ($a -ieq '-Dev') { $options.Dev = $true; $channel = 'dev' }
        elseif ($a -ieq '-Stable') { $options.Stable = $true }
        else { Bad "PUBLIC_ARGS: $a is not an option this case knows (-Dev, -Stable)" }
    }
    Add-SeedData
    $clone = Join-Path $script:H 'Downloads\tpw-port'
    [void](New-Item -ItemType Directory -Force -Path (Join-Path $clone '.git'), (Join-Path $clone 'launcher'))
    Write-Text (Join-Path $clone 'launcher\VERSION.txt') "0.0.2`n"
    $cloneBefore = Get-Snapshot $clone
    # What the player owns (saves, game data, packs) is snapshotted now, before the first run: the real launcher the line
    # starts writes into the data folder (launcher.ini, logs\launcher.log, assets\.staging), so the whole
    # folder is snapshotted later, once that launcher has been stopped, and only the player's own files are held to
    # "unchanged since before the install".
    $ownedDirs = @('saves', 'games', 'packs')
    $ownedBefore = @($ownedDirs | ForEach-Object { Get-Snapshot (Join-Path $script:Data $_) }) -join "`n"
    $url = 'https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.ps1'
    $remote = Invoke-RestMethod -Uri $url
    if (-not "$remote".Contains('RESULT')) { Bad "the download of $url is not the installer" }
    # the line as a player runs it (no -NoLaunch: the launcher is started at the end)
    $out = Invoke-Line $options "$remote"
    Assert-Ok $out ("$($out[-1])" -replace '^RESULT ok ', '')
    $version = "$($out[-1])" -replace '^RESULT ok ', ''
    Assert-Build $script:Dir $version $channel
    Expect-Line $out "An older copy of the launcher is in $clone."
    Assert-UserPathOnce $script:Dir
    # the line started the real launcher; give it a moment, then end it
    Start-Sleep -Seconds 5
    $exe = Join-Path $script:Dir 'build\tpw_launcher.exe'
    $running = @(Get-Process -Name tpw_launcher -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $exe })
    if ($running.Count -gt 0) { Note 'the launcher is running'; $running | Stop-Process -Force -ErrorAction SilentlyContinue }
    else { Write-Host '::warning::[public] the launcher the line started was not running after 5 seconds' }
    Assert-NewWindowReal $script:Dir
    # the launchers have ended (the one the line started, the one of themepark --version): from here the data folder is
    # only the update's to change, and a rerun must leave it as it is
    $dataBefore = Get-Snapshot $script:Data
    $again = @{ NoLaunch = $true } + $options
    $out = Invoke-Line $again "$remote"
    Assert-Ok $out $version
    Expect-Text $out 'Already up to date'
    Assert-Same 'the old clone' $cloneBefore (Get-Snapshot $clone)
    Assert-Same 'the data folder' $dataBefore (Get-Snapshot $script:Data)
    Assert-Same 'the saves, game data and packs' $ownedBefore (@($ownedDirs | ForEach-Object { Get-Snapshot (Join-Path $script:Data $_) }) -join "`n")
}

# ---- main

$userPathAtStart = Get-UserPathRaw

# The installer edits the user's Path for real; put it back as it was when this script started.
function Restore-UserPath {
    $now = Get-UserPathRaw
    if ([string]::Equals($now[0], $userPathAtStart[0]) -and "$($now[1])" -eq "$($userPathAtStart[1])") { return }
    try {
        $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Environment')
        try {
            if ($null -eq $userPathAtStart[1]) { $key.DeleteValue('Path', $false) }
            else { $key.SetValue('Path', $userPathAtStart[0], $userPathAtStart[1]) }
        } finally { $key.Close() }
        Write-Host "The user's Path was put back as it was."
    } catch { Write-Host "::warning::the user's Path could not be put back: $_" }
}

# The progress view (what a console gets instead of the STEP/RESULT lines), forced through the pipe with
# TPW_INSTALL_VIEW: the vt view (escape codes, redrawn in place) and the lines view (one line per finished row); a
# build that fails shows the log's last lines and names -Plain. Nothing of plain mode's lines, the build's output only
# in the log, scrubbed.
function Invoke-Case_tui {
    New-Case 'tui'
    Initialize-Fake
    New-FakeRelease '0.0.1'
    foreach ($view in @('vt', 'lines')) {
        $env:TPW_INSTALL_VIEW = $view
        $dir = Join-Path $script:C "install-$view"
        $out = Invoke-Line @{ NoLaunch = $true; Dir = $dir }
        Expect-Eq "the exit code ($view view)" $script:RC 0
        # both views name the version in the title (the lines view holds its finished rows until it is known)
        Expect-Text $out 'STP/TPW Launcher 0.0.1'
        Expect-Text $out 'tpw-port 0.0.1 downloaded'
        Expect-Text $out 'Done in'
        if (@($out | Where-Object { "$_" -match '^(STEP|RESULT) ' })) { Bad "the $view view printed a line of plain mode" }
        Expect-NoText $out '==> profile'
        $log = Join-Path $dir 'last-build.log'
        if (-not (Test-Path -LiteralPath $log)) { Bad "the $view view left no last-build.log" }
        Expect-Text (Get-Content -LiteralPath $log) '==> profile %USERPROFILE% user=<user>'
        if ($view -eq 'lines') { Expect-Text $out 'Git and Windows PowerShell found' }
        if (-not (Test-Path -LiteralPath (Join-Path $dir 'build\tpw_launcher.exe'))) { Bad "the $view view made no launcher" }
    }
    $env:TPW_INSTALL_VIEW = 'vt'
    $env:TPW_FAKE_BUILD = 'fail'
    $dir = Join-Path $script:C 'install-fail'
    $out = Invoke-Line @{ NoLaunch = $true; Dir = $dir }
    if ($script:RC -eq 0) { Bad 'a failed build in the vt view returned 0' }
    Expect-Text $out 'failed after'
    Expect-Text $out 'fake compiler error'
    Expect-Text $out 'Run the line again with -Plain'
    if (@($out | Where-Object { "$_" -match '^(STEP|RESULT) ' })) { Bad 'the vt view printed a line of plain mode after a failed build' }
    if (Test-Path -LiteralPath (Join-Path $dir 'build')) { Bad 'a failed first build left a build folder' }
    Remove-Item -Path Env:TPW_FAKE_BUILD, Env:TPW_INSTALL_VIEW -ErrorAction SilentlyContinue
}

$all = @('fresh', 'legacy', 'rerun', 'updater', 'dev', 'tags', 'refusals', 'paths', 'missing', 'nogit', 'buildfail', 'interrupt', 'interrupt_first', 'tui')
if (-not $Cases -or $Cases.Count -eq 0) { $Cases = $all }
$failed = @()
foreach ($c in $Cases) {
    Write-Host "::group::case $c"
    $t0 = Get-Date
    $ok = $true
    try {
        & "Invoke-Case_$c"
    } catch {
        $ok = $false
        Write-Host "::error::$_"
        if ($_.ScriptStackTrace) { Write-Host $_.ScriptStackTrace }
    } finally {
        Restore-Base
    }
    $seconds = [int]((Get-Date) - $t0).TotalSeconds
    Write-Host '::endgroup::'
    if ($ok) { Write-Host "case $c passed ($seconds seconds)" }
    else { $failed += $c; Write-Host "::error::case $c failed ($seconds seconds)" }
}
Restore-UserPath
if ($failed.Count) {
    Write-Host "::error::failed cases: $($failed -join ' ')"
    exit 1
}
Write-Host "all cases passed: $($Cases -join ' ')"
exit 0
