# Installs or updates the STP/TPW Launcher (tpw-port) on Windows. It downloads the source and compiles it here.
#
#   irm https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.ps1 | iex
#   & ([scriptblock]::Create((irm https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.ps1))) -Dev
#
# The second line is how to pass options through a download. The launcher's updater runs the file itself:
#   powershell -NoProfile -ExecutionPolicy Bypass -File install.ps1 -Update -Stage
#
# Options (install.sh on a Mac takes the same ones as --dev, --dir and so on):
#   -Dev              follow main (the next version, still being made) instead of the newest release
#   -Stable           follow the newest release (the default; switches an install made with -Dev back)
#   -Dir PATH         the install folder (default: %LOCALAPPDATA%\STP-TPW-Launcher, which no clone uses)
#   -Update           update an existing install without asking anything (to the newest release unless -Dev)
#   -Stage            with -Update: build the new version into DIR\build-next and stop there
#   -SwapAndLaunch    switch DIR\build-next in as DIR\build (the old one is kept as DIR\build-prev), then start it
#   -WaitPid PID      with -SwapAndLaunch: first wait for that process (the running launcher) to exit
#   -NoLaunch         do not start the launcher at the end
#   -Help             this text
#
# The folder it makes (DIR):
#   source\          the git clone, with its submodules. It belongs to this script: do not edit it.
#   build\           the launcher in use (build\tpw_launcher.exe), with the build's tools in build\tools
#   build-next\      a new build being made, or made and waiting to be switched in
#   build-prev\      the build before the last switch, kept until the next one
#   bin\themepark.cmd the themepark command: starts build\tpw_launcher.exe from any folder (see THE THEMEPARK COMMAND)
#   channel.txt      "stable" or "dev": the launcher reads it to know that this script installed it; it records what
#                    is installed and never chooses what an update gets
#   last-build.log   the output of the last build, with no user name, profile folder or PC name in it
#   .tpw-installer   marks the folder as this script's, so that it never writes into a folder that is somebody else's
# Game files and saves are not in DIR (they are in the launcher's data folder, %APPDATA%\STP-TPW Launcher), and this
# script never touches them.
#
# ONE LINE FOR EVERYONE. The same line installs, and run again it updates. Without -Dev it always follows the newest
# release, also on an install made with -Dev, which it switches back and says so (the maintainer, 2026-10-25: a player is on main
# only by asking for it each time). -Update, the launcher's Update button, does the same: without -Dev on that run it
# gets the newest release, whatever channel.txt says, and the launcher never passes -Dev. A clone that a player made
# earlier to play from (the README's old way, usually %USERPROFILE%\tpw-port) is never touched or used; at the end the
# player is told it is no longer needed.
#
# THE CONTRACT. The launcher's updater runs this script and reads its output, so these never change:
#   STEP n/N some plain words     one line as each step starts
#   RESULT ok VERSION             the last line when it worked, and the exit code is 0 (VERSION: 0.1.0, 0.1.1-dev)
#   RESULT fail SOME REASON       the last line when it did not, and the exit code is not 0
# Exactly one RESULT line is printed, last. Other lines are for people and may change. Options are only ever added.
# Run through iex (no file) the script never calls exit, which would close the PowerShell window; it sets
# $LASTEXITCODE instead.
#
# WHY IT BUILDS FROM SOURCE. A program downloaded from the internet is blocked by Windows (SmartScreen) and macOS
# (Gatekeeper) unless its maker signed it with a paid certificate. A program the PC compiled itself is not marked as
# downloaded, so there is nothing to block and nothing to sign. The price is a few minutes of compiling.
#
# WHY NOTHING IS SWITCHED UNTIL THE END (stage, then swap). The launcher in use is DIR\build. A new version is built in
# another folder, DIR\build-next, and is switched in only after it built and answered --version with the right version.
# Whatever goes wrong before that (no internet, a compiler error, a full disk, a power cut) leaves the working launcher
# as it was. The switch itself is two renames. Windows cannot rename the folder of a program that is running, so the
# updater stages first, then quits and lets -SwapAndLaunch -WaitPid switch and start the new one.
#
# WHY THE DOWNLOADS COME FIRST. The clone and the submodules finish before the build starts, and the build
# (devtools\win\build.ps1) fetches all its tools (the compiler, CMake, Ninja, Python, about 250 MB the first time) before
# it compiles anything. A lost connection is then found early, said in plain words, and costs nothing. The tools of the
# build in use are copied into the new build folder first, so an update downloads them again only when their pinned
# versions changed.
#
# THE THEMEPARK COMMAND. Players start the game by typing themepark in any PowerShell or Command Prompt window.
# DIR\bin\themepark.cmd starts DIR\build\tpw_launcher.exe (where the Update button is) and returns at once. DIR\bin is
# added to the user's Path (HKCU\Environment, no administrator rights), once, and Windows is told so that new windows
# see it. Through irm | iex the script runs in the player's own window, so DIR\bin goes on that window's Path too and
# themepark works there at once. At the end a new PowerShell, with the Path a new window gets, checks that it finds
# themepark, and if it does not the player is given the one line that fixes it (the install has still succeeded). Only
# an install (not -Update or -SwapAndLaunch) changes the Path: an update never brings back an entry the player removed.
# A themepark command this script did not make is never shadowed: the Path is then left as it was and the player is
# told the full path instead.
#
# SAFETY. No administrator rights, nothing written outside DIR but that Path entry (the launcher is started at the end,
# from DIR\build). The only folders deleted are DIR\build-next and DIR\build-prev, by Remove-Own below, which refuses
# any other name, an odd install folder and a link or junction. Nothing is installed on the PC: Git for Windows must be
# there already.
#
# Test hooks (for the maintainers' tests, not for players): TPW_INSTALL_REPO replaces the repository URL, and
# TPW_INSTALL_STUB=1 puts a stand-in launcher in place of the download and the build (the installer's CI tests the
# themepark command with it), of version TPW_INSTALL_STUB_VERSION when set (a newer one stands in for a new release).
#
# This file is plain ASCII on purpose: Windows PowerShell reads a file without a byte order mark as ANSI.

[CmdletBinding(PositionalBinding = $false)]
param(
    [switch]$Dev,
    [switch]$Stable,
    [string]$Dir = '',
    [switch]$Update,
    [switch]$Stage,
    [switch]$SwapAndLaunch,
    [string]$WaitPid = '',
    [switch]$NoLaunch,
    [switch]$Help,
    # anything else, refused below with a RESULT line
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest
)

# Run as a file (the updater, or a saved copy) the exit code is set with exit. Run through iex or a scriptblock there is
# no file, and exit would close the window.
$TpwInstallFromFile = -not [string]::IsNullOrEmpty($MyInvocation.MyCommand.Path)

# Everything happens in this block, so that its functions and variables do not stay behind in the window after iex. Its
# last output is the exit code.
$TpwInstallCode = @(& {
    $ErrorActionPreference = 'Continue'   # a native program's stderr must not stop the script; exit codes decide
    $ProgressPreference = 'SilentlyContinue'

    $Repo = 'https://github.com/ThemeParkWorldDecomp/tpw-port.git'
    if ($env:TPW_INSTALL_REPO) { $Repo = $env:TPW_INSTALL_REPO }
    $Stub = [bool]$env:TPW_INSTALL_STUB
    # A wrong address must fail, never wait for a password nobody sees, in a terminal or a window.
    $env:GIT_TERMINAL_PROMPT = '0'
    $env:GCM_INTERACTIVE = 'never'

    # The state the functions share (a hashtable, so that a function's change is seen by all): the step counter, whether
    # the RESULT line is out, and what is being installed.
    $St = @{ StepNo = 0; StepTotal = 0; Done = $false; Channel = ''; Tag = ''; Commit = ''; Version = '' }
    # Every git command gets these: files kept with their line endings as stored (as the build expects), paths longer
    # than 260 characters allowed, and no credential window.
    $GitCfg = @('-c', 'core.autocrlf=false', '-c', 'core.longpaths=true', '-c', 'credential.helper=')

    function Show-Usage {
        Write-Host 'Installs or updates the STP/TPW Launcher, compiled on this PC from its source.'
        Write-Host ''
        Write-Host '  irm https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.ps1 | iex'
        Write-Host '  & ([scriptblock]::Create((irm https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.ps1))) -Dev'
        Write-Host ''
        Write-Host 'Options:'
        Write-Host '  -Dev             follow main (the next version, still being made) instead of the newest release'
        Write-Host '  -Stable          follow the newest release (the default; switches an install made with -Dev back)'
        Write-Host '  -Dir PATH        the install folder (default: %LOCALAPPDATA%\STP-TPW-Launcher)'
        Write-Host '  -Update          update an existing install without asking anything (to the newest release unless -Dev)'
        Write-Host '  -Stage           with -Update: build the new version into DIR\build-next and stop there'
        Write-Host '  -SwapAndLaunch   switch DIR\build-next in as DIR\build (the old one is kept as DIR\build-prev), then start it'
        Write-Host '  -WaitPid PID     with -SwapAndLaunch: first wait for that process (the running launcher) to exit'
        Write-Host '  -NoLaunch        do not start the launcher at the end'
        Write-Host '  -Help            this text'
        Write-Host ''
        Write-Host 'After installing, type themepark in any PowerShell window to play, from any folder.'
        Write-Host 'Run the same line again to update. It follows the newest release unless -Dev is added each time; so does'
        Write-Host "the launcher's Update button, which never adds -Dev."
    }

    # ---- output: the lines the launcher reads

    function Step([string]$text) {
        $St.StepNo += 1
        Write-Host "STEP $($St.StepNo)/$($St.StepTotal) $text"
    }

    function Say([string]$text) { Write-Host $text }

    # Prints the one RESULT fail line and unwinds to the end of the block.
    function Fail([string]$reason) {
        Write-Host "RESULT fail $reason"
        $St.Done = $true
        throw 'tpw-install-failed'
    }

    function Finish-Ok([string]$version) {
        Write-Host "RESULT ok $version"
        $St.Done = $true
    }

    # A lost connection, said the same way wherever it shows up.
    function Fail-Network([string]$detail) {
        if ($detail) { Say $detail }
        Say 'The launcher in use was not changed.'
        Fail "Couldn't reach GitHub, try again later."
    }

    # ---- paths

    # True when $path must never be the install folder.
    function Test-UnsafePath([string]$path) {
        if ([string]::IsNullOrWhiteSpace($path)) { return $true }
        $root = [System.IO.Path]::GetPathRoot($path)
        if ([string]::IsNullOrEmpty($root) -or $path.TrimEnd('\') -eq $root.TrimEnd('\')) { return $true }
        $special = @($env:USERPROFILE, $env:LOCALAPPDATA, $env:APPDATA, $env:SystemRoot, $env:ProgramFiles,
                     ${env:ProgramFiles(x86)}, $env:ProgramData, $env:TEMP, $env:PUBLIC, (Split-Path $env:USERPROFILE))
        foreach ($s in $special) {
            if ($s -and $path.TrimEnd('\') -eq $s.TrimEnd('\')) { return $true }
        }
        return $false
    }

    function Test-Link([string]$path) {
        $item = Get-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
        return ($null -ne $item -and ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint))
    }

    # Deletes DIR\build-next or DIR\build-prev and nothing else. This is the only place the script deletes a folder.
    function Remove-Own([string]$name) {
        if ($name -ne 'build-next' -and $name -ne 'build-prev') { Fail "Internal error: this script never deletes `"$name`"." }
        if (Test-UnsafePath $Dir) { Fail "Internal error: refusing to delete inside `"$Dir`"." }
        $target = Join-Path $Dir $name
        if (-not (Test-Path -LiteralPath $target)) { return }
        if (Test-Link $target) { Fail "$target is a link. This script does not delete links. Remove it yourself and run this again." }
        if (-not (Test-Path -LiteralPath $target -PathType Container)) { Fail "$target is not a folder. Remove it yourself and run this again." }
        # cmd's rd copes with deeper paths than Remove-Item does in Windows PowerShell.
        & cmd.exe /d /c rd /s /q $target | Out-Null
        if (Test-Path -LiteralPath $target) { Fail "Couldn't remove $target. Close anything using it and try again." }
    }

    # The value of $key in a file of key=value lines (install-info.txt in a build folder).
    function Get-Info([string]$file, [string]$key) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { return '' }
        foreach ($line in (Get-Content -LiteralPath $file)) {
            if ($line.StartsWith("$key=")) { return $line.Substring($key.Length + 1).Trim() }
        }
        return ''
    }

    function Write-Text([string]$file, [string]$text) {
        [System.IO.File]::WriteAllText($file, $text, (New-Object System.Text.UTF8Encoding($false)))
    }

    function Read-Channel {
        $file = Join-Path $Dir 'channel.txt'
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { return '' }
        return ([string](Get-Content -LiteralPath $file -TotalCount 1)).Trim()
    }

    function Write-Channel([string]$channel) {
        $tmp = Join-Path $Dir 'channel.txt.tmp'
        try {
            Write-Text $tmp "$channel`n"
            Move-Item -LiteralPath $tmp -Destination (Join-Path $Dir 'channel.txt') -Force -ErrorAction Stop
        } catch { Fail "Couldn't write $(Join-Path $Dir 'channel.txt')." }
    }

    # Windows PowerShell 5.1, which runs the build (devtools\win\build.ps1). A 32-bit window sees the 64-bit one under
    # Sysnative only.
    function Get-WindowsPowerShell {
        $ps = Join-Path $env:SystemRoot 'Sysnative\WindowsPowerShell\v1.0\powershell.exe'
        if (Test-Path -LiteralPath $ps) { return $ps }
        return (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe')
    }

    # ---- 1. this computer

    function Test-Computer {
        if ($env:OS -ne 'Windows_NT') {
            Say 'This installer is for Windows. On a Mac, use install.sh (see the README).'
            Fail 'This installer is for Windows only.'
        }
        if (-not [Environment]::Is64BitOperatingSystem) { Fail 'This needs 64-bit Windows 10 or 11.' }
        $os = [Environment]::OSVersion.Version
        Say "Windows $($os.Major).$($os.Minor) build $($os.Build), $env:PROCESSOR_ARCHITECTURE"

        $missing = @()
        $how = @()
        # curl.exe and tar.exe (the build's downloads) came with Windows 10 version 1803, build 17134.
        if ($os.Major -lt 10 -or $os.Build -lt 17134) {
            $missing += 'Windows 10 version 1803 or newer'
            $how += 'Windows 10 version 1803 or newer, or Windows 11: update Windows (Settings, Windows Update).'
        } else {
            foreach ($tool in @('curl.exe', 'tar.exe')) {
                if (-not (Test-Path -LiteralPath (Join-Path $env:SystemRoot "System32\$tool"))) {
                    $missing += $tool
                    $how += "${tool}: part of Windows 10 version 1803 and newer. Update Windows (Settings, Windows Update)."
                }
            }
        }
        if (-not (Test-Path -LiteralPath (Get-WindowsPowerShell))) {
            $missing += 'Windows PowerShell'
            $how += 'Windows PowerShell: part of Windows 10 and 11. This Windows has it removed; it cannot build the launcher.'
        }
        if ($null -eq (Get-Command git.exe -CommandType Application -ErrorAction SilentlyContinue)) {
            $missing += 'Git'
            $how += 'Git: run  winget install --id Git.Git -e  (or get it from https://git-scm.com/download/win), then open a new PowerShell window.'
        }
        if ($missing.Count) {
            Say "Missing: $($missing -join ', ')."
            foreach ($h in $how) { Say "  $h" }
            Say 'Then run this command again. Nothing was changed.'
            Fail "Missing: $($missing -join ', '). See the lines above for how to get it, then run this again."
        }
        $gitVersion = (& git.exe --version 2>$null | Out-String).Trim()
        Say "Git: $gitVersion"

        # Estimates (the README says 2.5 GB for a clone and its build): the source is about 150 MB and a build folder
        # with its tools (the compiler is the biggest) about 2 GB. An update makes a second build folder beside the one
        # in use, and the old build-prev goes only at the switch.
        try {
            $probe = $Dir
            while (-not (Test-Path -LiteralPath $probe) -and (Split-Path $probe)) { $probe = Split-Path $probe }
            $drive = New-Object System.IO.DriveInfo([System.IO.Path]::GetPathRoot($probe))
            $freeMB = [int64]($drive.AvailableFreeSpace / 1MB)
            $needGB = 3
            if (Test-Path -LiteralPath (Join-Path $Dir 'build')) { $needGB = 5 }
            if ($freeMB -lt $needGB * 1000) {
                Say "There is only $freeMB MB free where $Dir goes. This needs about $needGB GB free."
                Say 'Nothing was changed.'
                Fail "Not enough free space on the disk (about $needGB GB are needed)."
            }
        } catch {
            if ($St.Done) { throw }   # the Fail above; any other error only means the free space is unknown
        }
    }

    # ---- 2. the downloads

    # Which release or commit to install. Sets $St.Tag (stable only) and checks GitHub answers, before anything is
    # made.
    function Select-Target {
        if ($St.Channel -eq 'dev') {
            $St.Tag = ''
            $out = & git.exe @GitCfg ls-remote $Repo refs/heads/main 2>&1 | ForEach-Object { "$_" }
            if ($LASTEXITCODE -ne 0) { Fail-Network ($out -join "`n") }
            if (-not $out) { Fail 'The repository has no main branch.' }
            return
        }
        $out = & git.exe @GitCfg ls-remote --tags --refs $Repo 2>&1 | ForEach-Object { "$_" }
        if ($LASTEXITCODE -ne 0) { Fail-Network ($out -join "`n") }
        # Releases are the tags vX.Y.Z. The highest by number, not by letters (v0.0.10 comes after v0.0.9).
        $versions = @()
        foreach ($line in $out) {
            if ($line -match 'refs/tags/v(\d+)\.(\d+)\.(\d+)$') { $versions += [version]"$($Matches[1]).$($Matches[2]).$($Matches[3])" }
        }
        if ($versions.Count -eq 0) {
            Say 'No release has been published yet, so there is nothing to install by default.'
            Say 'To try the version that is being made (it can have bugs), run this with -Dev:'
            Say '  & ([scriptblock]::Create((irm https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.ps1))) -Dev'
            Say 'Nothing was changed.'
            Fail 'No release has been published yet. Use -Dev to install the development version.'
        }
        $newest = ($versions | Sort-Object | Select-Object -Last 1)
        $St.Tag = "v$newest"
        Say "Newest release: $newest"
    }

    # Clones or fetches the source and checks out the wanted commit, submodules included. Sets $St.Commit and
    # $St.Version.
    function Get-Source {
        if (Test-Path -LiteralPath (Join-Path $Src '.git')) {
            & git.exe @GitCfg -C $Src remote set-url origin $Repo | Out-Host
            if ($LASTEXITCODE -ne 0) { Fail "Couldn't use the source folder $Src." }
        } elseif (Test-Path -LiteralPath $Src) {
            Say "$Src exists but is not a git clone, so this script cannot use it. Move it away and run this again."
            Fail "$Src is in the way. Move it away and run this again."
        } else {
            Say "Downloading the source (a clone of $Repo)"
            # -c on clone keeps the settings in the clone, for every later git command too.
            & git.exe @GitCfg clone -c core.autocrlf=false -c core.longpaths=true --no-checkout $Repo $Src | Out-Host
            if ($LASTEXITCODE -ne 0) { Fail-Network 'The download of the source did not finish.' }
        }

        if ($St.Channel -eq 'dev') {
            Say 'Fetching main'
            & git.exe @GitCfg -C $Src fetch --force --no-tags origin '+refs/heads/main:refs/remotes/origin/main' | Out-Host
            if ($LASTEXITCODE -ne 0) { Fail-Network 'The download of the source did not finish.' }
            $ref = 'refs/remotes/origin/main'
        } else {
            Say "Fetching $($St.Tag)"
            & git.exe @GitCfg -C $Src fetch --force --no-tags origin "+refs/tags/$($St.Tag):refs/tags/$($St.Tag)" | Out-Host
            if ($LASTEXITCODE -ne 0) { Fail-Network 'The download of the source did not finish.' }
            $ref = "refs/tags/$($St.Tag)"
        }
        $commit = (& git.exe @GitCfg -C $Src rev-parse --verify "$ref^{commit}" 2>$null | Out-String).Trim()
        if ($LASTEXITCODE -ne 0 -or -not $commit) { Fail "The source has no $ref." }
        $St.Commit = $commit

        # The source is this script's own. A change somebody made in it would be lost by the checkout: stop instead. A
        # clone not checked out yet has no index (and git would call every file deleted). Submodules are left out of the
        # check: they differ from the pinned commits between a checkout and their update.
        if (Test-Path -LiteralPath (Join-Path $Src '.git\index')) {
            $changes = (& git.exe @GitCfg -C $Src status --porcelain --ignore-submodules=all 2>$null | Out-String).Trim()
            if ($changes) {
                Say "$Src has changes that this script did not make, and switching versions would overwrite them."
                Say 'Move that folder away (or the changes out of it) and run this again.'
                Fail "The source folder has changes of its own: $Src"
            }
        }
        & git.exe @GitCfg -C $Src checkout -q --detach $commit | Out-Host
        if ($LASTEXITCODE -ne 0) { Fail "Couldn't switch the source to $commit." }
        & git.exe @GitCfg -C $Src submodule sync -q --recursive | Out-Host
        if ($LASTEXITCODE -ne 0) { Fail "Couldn't read the source's submodules." }
        Say 'Downloading the submodules'
        & git.exe @GitCfg -C $Src submodule update --init --recursive --force | Out-Host
        if ($LASTEXITCODE -ne 0) { Fail-Network 'The download of the submodules did not finish.' }

        $versionFile = Join-Path $Src 'launcher\VERSION.txt'
        $version = ''
        if (Test-Path -LiteralPath $versionFile) { $version = ([string](Get-Content -LiteralPath $versionFile -TotalCount 1)).Trim() }
        if ($version -notmatch '^\d+\.\d+\.\d+(-dev)?$') { Fail 'The source does not name its version (launcher\VERSION.txt).' }
        # A release names its version in its tag too, and release.yml refuses a tag that does not match.
        if ($St.Channel -eq 'stable' -and "v$version" -ne $St.Tag) {
            Fail "The release $($St.Tag) says it is version $version. Not installing it."
        }
        $St.Version = $version
        Say "Source: $commit (version $version)"
    }

    # Makes an empty DIR\build-next and copies in the tools the build in use fetched (build.ps1 keeps each with its
    # checksum and fetches it again only when the pin changed).
    function New-Next {
        Remove-Own 'build-next'
        [void](New-Item -ItemType Directory -Force -Path $Next)
        $oldTools = Join-Path $Dir 'build\tools'
        if (Test-Path -LiteralPath $oldTools) {
            Say 'Copying the build tools of the launcher in use'
            & robocopy.exe $oldTools (Join-Path $Next 'tools') /E /NFL /NDL /NJH /NJS /NP | Out-Null
            if ($LASTEXITCODE -ge 8) { Say 'Copying them did not work; the build fetches them again.' }
        }
    }

    # ---- 3. the build

    # What the build's output may keep in DIR\last-build.log (the screen shows it as it is): nothing that names the player
    # or the PC. The profile folder (and a C:\Users\NAME of any other drive or spelling) becomes %USERPROFILE%, a
    # Windows SID <sid>, the PC's name <computer> (4 letters or more) and the account name <user>, only after user=,
    # owner: and the like, so that a name that is a common word is not changed elsewhere. Scrubbing again changes
    # nothing. It mirrors platform\log_scrub.h. Each rule is (pattern, replacement); the pattern is built in its own
    # parentheses, because PowerShell's comma binds tighter than +.
    function Get-LogRules {
        $rules = New-Object System.Collections.ArrayList
        $word = '(?![\w.-])'
        $profileDir = $env:USERPROFILE
        if ($profileDir -and $profileDir.Length -ge 4) {
            $spelled = [regex]::Escape($profileDir.TrimEnd('\', '/')).Replace('\\', '[\\/]')
            [void]$rules.Add(@(('(?i)' + $spelled + $word), '%USERPROFILE%'))
        }
        [void]$rules.Add(@('(?i)[A-Za-z]:[\\/]Users[\\/](?!(?:Public|Default|Shared|All Users|Default User)(?:[\\/]|$|[\s"''<>]))[^\\/\s"''<>%]+', '%USERPROFILE%'))
        [void]$rules.Add(@('S-1-5-21(?:-\d+){3,5}', '<sid>'))
        $pc = $env:COMPUTERNAME
        if ($pc -and $pc.Length -ge 4) {
            $case = ''
            if ($pc.Length -ge 6) { $case = '(?i)' }
            [void]$rules.Add(@(($case + '(?<![\w-])' + [regex]::Escape($pc) + '(?![\w-])'), '<computer>'))
        }
        $user = $env:USERNAME
        if ($user) {
            [void]$rules.Add(@(('(?i)\b(user|username|user name|login|logname|owner|account)(\s*[=:]\s*)' + [regex]::Escape($user) + '(?![\w-])'), '${1}${2}<user>'))
        }
        return ,$rules
    }

    function ConvertTo-LogLine([string]$line, $rules) {
        foreach ($rule in $rules) { $line = [regex]::Replace($line, $rule[0], $rule[1]) }
        return $line
    }

    # devtools\win\build.ps1, its output on the screen and in DIR\last-build.log. TOOLS=fetch: the pinned tools, the same
    # as the release builds, whatever this PC's PATH has. Its exit codes: 0 done, 10 no curl.exe or tar.exe, 20 a
    # download failed, 21 a download did not match its checksum, 22 unpacking failed, 30 configure, 40 build, 45 a DLL.
    function Invoke-Build {
        $buildPs1 = Join-Path $Src 'devtools\win\build.ps1'
        $rules = Get-LogRules
        $writer = New-Object System.IO.StreamWriter($Log, $false, (New-Object System.Text.UTF8Encoding($false)))
        # CI=1: build.ps1 downloads without a progress bar, which would be one long line here.
        $oldCI = $env:CI
        $env:CI = '1'
        try {
            & (Get-WindowsPowerShell) -NoLogo -NoProfile -ExecutionPolicy Bypass -File $buildPs1 "BUILD=$Next" 'TOOLS=fetch' 2>&1 | ForEach-Object {
                if ($_ -is [System.Management.Automation.ErrorRecord]) { $line = $_.Exception.Message } else { $line = "$_" }
                Write-Host $line
                $writer.WriteLine((ConvertTo-LogLine $line $rules))
            }
            $code = $LASTEXITCODE
        } finally {
            $writer.Close()
            $env:CI = $oldCI
        }
        if ($code -eq 0) { return }
        if ($code -eq 20) { Fail-Network 'A download of the build tools did not finish.' }
        Say "The build failed (build.ps1 exit $code). Its output is above and in $Log."
        Say "The launcher in use ($(Join-Path $Dir 'build')) was not changed."
        if ($code -eq 21) { Fail 'A download of the build tools did not match its checksum. The launcher in use was not changed.' }
        Fail "The build failed. The launcher in use was not changed. The output is in $Log."
    }

    # ---- 4. the check

    # The first line of a launcher's --version ("STP/TPW Launcher v0.1.0"). The launcher has no console, so its output
    # goes through a file, in the build folder.
    function Get-VersionLine([string]$folder) {
        $exe = Join-Path $folder 'tpw_launcher.exe'
        $out = Join-Path $folder 'version-check.txt'
        if (-not (Test-Path -LiteralPath $exe)) { return '' }
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

    function Test-Next {
        if (-not (Test-Path -LiteralPath (Join-Path $Next 'tpw_launcher.exe'))) {
            Fail 'The build finished but made no tpw_launcher.exe. The launcher in use was not changed.'
        }
        $first = Get-VersionLine $Next
        if (-not $first.EndsWith(" v$($St.Version)")) {
            Say "The new launcher says `"$first`", not version $($St.Version)."
            Fail "The new launcher did not answer with version $($St.Version). The launcher in use was not changed."
        }
        Say $first
        try {
            Write-Text (Join-Path $Next 'install-info.txt') "version=$($St.Version)`nchannel=$($St.Channel)`ncommit=$($St.Commit)`n"
        } catch { Fail "Couldn't write $(Join-Path $Next 'install-info.txt')." }
    }

    # ---- 5. the switch

    function Switch-Build {
        $info = Join-Path $Next 'install-info.txt'
        if (-not (Test-Path -LiteralPath $info)) { Fail "There is no finished new build in $Next to switch to." }
        $newVersion = Get-Info $info 'version'
        $newChannel = Get-Info $info 'channel'
        if ($newChannel -ne 'stable' -and $newChannel -ne 'dev') { Fail "$info names no channel. Nothing was switched." }
        $first = Get-VersionLine $Next
        if (-not $first.EndsWith(" v$newVersion")) {
            Fail "The new build in $Next does not answer with version $newVersion. Nothing was switched."
        }
        $build = Join-Path $Dir 'build'
        $prev = Join-Path $Dir 'build-prev'
        if ((Test-Path -LiteralPath $build) -and ((Test-Link $build) -or -not (Test-Path -LiteralPath $build -PathType Container))) {
            Fail "$build is not a folder. Nothing was switched."
        }
        Remove-Own 'build-prev'
        $hadBuild = $false
        if (Test-Path -LiteralPath $build) {
            try { Move-Item -LiteralPath $build -Destination $prev -ErrorAction Stop }
            catch { Fail "Couldn't move the old launcher aside. Close it and try again. Nothing was switched." }
            $hadBuild = $true
        }
        try { Move-Item -LiteralPath $Next -Destination $build -ErrorAction Stop }
        catch {
            if ($hadBuild) { try { Move-Item -LiteralPath $prev -Destination $build -ErrorAction Stop } catch {} }
            Fail "Couldn't move the new launcher into place. The old one was put back."
        }
        Write-Channel $newChannel
        $St.Version = $newVersion
        if ($hadBuild) { Say "Switched to version $newVersion. The one before is kept in $prev." }
        else { Say "Installed version $newVersion." }
    }

    function Start-Launcher {
        $exe = Join-Path $Dir 'build\tpw_launcher.exe'
        Say "Starting $exe"
        try { Start-Process -FilePath $exe -WorkingDirectory $Dir -ErrorAction Stop | Out-Null }
        catch { Say "Couldn't start the launcher. Start it yourself: $exe" }
    }

    # ---- the test build (TPW_INSTALL_STUB=1, for the installer's CI only)

    # Instead of the build: a stand-in tpw_launcher.exe, compiled with the C# compiler of the .NET Framework that comes
    # with Windows. Like the real one it has no console and answers --version; run from DIR\build (by themepark), it adds
    # a line to DIR\stub-launch.txt, so a test can see that themepark started it.
    function New-StubLauncher {
        [void](New-Item -ItemType Directory -Force -Path $Next)
        Say 'Test build: a stand-in launcher.'
        $csc = @('FrameworkArm64', 'Framework64', 'Framework') |
            ForEach-Object { Join-Path $env:SystemRoot "Microsoft.NET\$_\v4.0.30319\csc.exe" } |
            Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
        if (-not $csc) { Fail 'Test build: there is no C# compiler (csc.exe) of the .NET Framework.' }
        $v = $St.Version
        $source = Join-Path $Next 'stub.cs'
        Write-Text $source @"
using System;
using System.IO;
static class Stub {
    static int Main(string[] args) {
        if (args.Length > 0 && args[0] == "--version") { Console.WriteLine("STP/TPW Launcher v$v"); Console.WriteLine("build: stub"); }
        string here = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
        if (Path.GetFileName(here) == "build") {
            File.AppendAllText(Path.Combine(Path.GetDirectoryName(here), "stub-launch.txt"),
                "STP/TPW Launcher v$v args=" + string.Join(" ", args) + " cwd=" + Environment.CurrentDirectory + "\r\n");
        }
        return 0;
    }
}
"@
        & $csc /nologo /target:winexe "/out:$(Join-Path $Next 'tpw_launcher.exe')" $source | Out-Host
        if ($LASTEXITCODE -ne 0) { Fail 'Test build: the stand-in launcher did not compile.' }
        Remove-Item -LiteralPath $source -Force -ErrorAction SilentlyContinue
    }

    # ---- the themepark command

    $CmdMark = 'tpw-port installer: the themepark command'

    # Writes DIR\bin\themepark.cmd (again at every install and switch: it is this script's, in DIR). It finds the
    # install from its own place (%~dp0), so it holds no path. No ( ) blocks: a ) in the folder's name would end one.
    function Write-Command {
        $cmd = Join-Path $Dir 'bin\themepark.cmd'
        if ((Test-Path -LiteralPath $cmd) -and -not (Select-String -LiteralPath $cmd -SimpleMatch $CmdMark -Quiet)) {
            Say "$cmd was not made by this installer, so it is left alone."
            return $false
        }
        $text = (@(
            '@echo off',
            "rem $CmdMark",
            'rem Starts the STP/TPW Launcher of this install (the folder above this one) and returns at once. The installer',
            'rem (install.ps1) writes this file. To remove the command, delete the install folder and remove its bin folder',
            'rem from your Path (Settings, search for "environment variables for your account").',
            'setlocal',
            'set "TPW_DIR=%~dp0.."',
            'if exist "%TPW_DIR%\build\tpw_launcher.exe" goto start',
            'echo themepark: there is no launcher in "%TPW_DIR%\build". Run the installer again.',
            'exit /b 1',
            ':start',
            'start "" /d "%TPW_DIR%" "%TPW_DIR%\build\tpw_launcher.exe" %*'
        ) -join "`r`n") + "`r`n"
        try {
            [void](New-Item -ItemType Directory -Force -Path (Join-Path $Dir 'bin') -ErrorAction Stop)
            [System.IO.File]::WriteAllText($cmd, $text, [System.Text.Encoding]::ASCII)
        } catch {
            Say "Couldn't write $cmd."
            return $false
        }
        return $true
    }

    # The user's Path as stored (%USERPROFILE% left as it is), and its kind.
    function Get-UserPath($key) {
        $value = [string]$key.GetValue('Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        $kind = [Microsoft.Win32.RegistryValueKind]::ExpandString
        if ($null -ne $key.GetValue('Path')) { $kind = $key.GetValueKind('Path') }
        return @($value, $kind)
    }

    function Test-SameFolder([string]$entry, [string]$folder) {
        return ([Environment]::ExpandEnvironmentVariables($entry).TrimEnd('\') -ieq $folder.TrimEnd('\'))
    }

    # True when the user's Path has DIR\bin (an update says how to play from it).
    function Test-UserPath {
        $bin = Join-Path $Dir 'bin'
        try {
            $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment')
            if ($null -eq $key) { return $false }
            try { $value = (Get-UserPath $key)[0] } finally { $key.Close() }
        } catch { return $false }
        foreach ($e in ($value -split ';')) { if ($e -and (Test-SameFolder $e $bin)) { return $true } }
        return $false
    }

    # Tells the open programs (Explorer above all, which starts the new windows) that the environment changed.
    function Send-EnvironmentChange {
        try {
            if (-not ('TpwInstall.Env' -as [type])) {
                Add-Type -Namespace TpwInstall -Name Env -MemberDefinition @'
[DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint msg, UIntPtr wParam, string lParam, uint flags, uint timeout, out UIntPtr result);
'@
            }
            $result = [UIntPtr]::Zero
            # HWND_BROADCAST, WM_SETTINGCHANGE "Environment", SMTO_ABORTIFHUNG, 5 seconds per window at most
            [void][TpwInstall.Env]::SendMessageTimeout([IntPtr]0xffff, 0x1A, [UIntPtr]::Zero, 'Environment', 2, 5000, [ref]$result)
        } catch {
            Say 'Windows was not told about the new Path: sign out and in again if themepark is not found.'
        }
    }

    # Adds DIR\bin to the user's Path, once. An entry of another install made by this script (its bin\themepark.cmd has
    # the mark) is taken out: the newest install takes the command over. Fails (and says why) when it cannot.
    function Add-UserPath {
        $bin = Join-Path $Dir 'bin'
        if ($bin.Contains(';') -or $bin.Contains('%')) {
            Say "The install folder's name has a ; or a %, which the Path cannot hold, so themepark was not added."
            return $false
        }
        $other = Get-Command themepark -CommandType Application, ExternalScript -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $other -and -not (Test-SameFolder (Split-Path $other.Path) $bin) -and
            -not (Select-String -LiteralPath $other.Path -SimpleMatch $CmdMark -Quiet -ErrorAction SilentlyContinue)) {
            Say "There is already a themepark command ($($other.Path)) that this installer did not make, so it is left alone."
            return $false
        }
        try {
            $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Environment')
            try {
                $value, $kind = Get-UserPath $key
                $kept = @()
                $have = $false
                $dropped = $false
                foreach ($e in ($value -split ';')) {
                    if (-not $e) { continue }
                    if (Test-SameFolder $e $bin) {
                        if ($have) { $dropped = $true } else { $kept += $e }
                        $have = $true
                        continue
                    }
                    $ours = $false
                    try {
                        $theirs = Join-Path ([Environment]::ExpandEnvironmentVariables($e)) 'themepark.cmd'
                        $ours = (Test-Path -LiteralPath $theirs -PathType Leaf) -and
                                (Select-String -LiteralPath $theirs -SimpleMatch $CmdMark -Quiet -ErrorAction SilentlyContinue)
                    } catch {}   # an entry that is no path name at all is kept as it is
                    if ($ours) {
                        $dropped = $true
                        continue
                    }
                    $kept += $e
                }
                if ($have -and -not $dropped) { return $true }
                if (-not $have) { $kept += $bin }
                if ($kind -ne [Microsoft.Win32.RegistryValueKind]::String) { $kind = [Microsoft.Win32.RegistryValueKind]::ExpandString }
                $key.SetValue('Path', ($kept -join ';'), $kind)
            } finally { $key.Close() }
        } catch {
            Say "Couldn't add $bin to your Path."
            return $false
        }
        Say "Added $bin to your Path."
        Send-EnvironmentChange
        return $true
    }

    # Puts DIR\bin on the Path of this PowerShell window too (irm | iex runs in the player's own window), so themepark
    # works here at once.
    function Add-SessionPath {
        $bin = Join-Path $Dir 'bin'
        foreach ($e in ($env:Path -split ';')) { if ($e -and (Test-SameFolder $e $bin)) { return } }
        $env:Path = $env:Path.TrimEnd(';') + ';' + $bin
    }

    # The self-check: does themepark work in a new window? A new Windows PowerShell, without a profile and with the Path
    # a new window gets (the machine's and the user's, from the registry), is asked where themepark is. 30 seconds at
    # most.
    function Test-NewWindow {
        $out = Join-Path $Dir 'themepark-check.txt'
        $check = "`$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + " +
                 "[Environment]::GetEnvironmentVariable('Path', 'User'); " +
                 "Get-Command themepark -ErrorAction SilentlyContinue | Select-Object -First 1 | ForEach-Object { [Console]::Out.WriteLine(`$_.Path) }"
        $encoded = [Convert]::ToBase64String([System.Text.Encoding]::Unicode.GetBytes($check))
        $found = ''
        try {
            $p = Start-Process -FilePath (Get-WindowsPowerShell) -ArgumentList '-NoLogo', '-NoProfile', '-NonInteractive', '-EncodedCommand', $encoded `
                -WorkingDirectory $env:SystemRoot -NoNewWindow -PassThru -RedirectStandardOutput $out -ErrorAction Stop
            $null = $p.Handle
            if (-not $p.WaitForExit(30000)) { try { $p.Kill() } catch {} }
            if (Test-Path -LiteralPath $out) { $found = ([string](Get-Content -LiteralPath $out -TotalCount 1)).Trim() }
        } catch {}
        Remove-Item -LiteralPath $out -Force -ErrorAction SilentlyContinue
        return ($found -and ((Split-Path $found -Leaf) -ieq 'themepark.cmd') -and (Test-SameFolder (Split-Path $found) (Join-Path $Dir 'bin')))
    }

    function Wait-Exit([int]$id) {
        $p = Get-Process -Id $id -ErrorAction SilentlyContinue
        if ($null -eq $p) { return }
        if (-not $p.WaitForExit(300000)) { Fail 'The launcher is still running after 5 minutes. Nothing was switched.' }
    }

    # ---- the three ways to run: swap, install or update

    function Invoke-Swap {
        if (-not (Test-Path -LiteralPath (Join-Path $Dir '.tpw-installer')) -and -not (Test-Path -LiteralPath (Join-Path $Dir 'channel.txt'))) {
            Fail "There is no install in $Dir."
        }
        $St.StepTotal = 1
        if ($WaitPid) { $St.StepTotal += 1 }
        if (-not $NoLaunch) { $St.StepTotal += 1 }
        if ($WaitPid) {
            Step 'Waiting for the launcher to close'
            Wait-Exit ([int]$WaitPid)
        }
        Step 'Switching to the new version'
        if (Test-Path -LiteralPath (Join-Path $Next 'install-info.txt')) {
            Switch-Build
            [void](Write-Command)
        } else {
            Say "No new version is waiting in $Next, so there is nothing to switch."
            if (-not (Test-Path -LiteralPath (Join-Path $Dir 'build\tpw_launcher.exe'))) { Fail "There is no launcher in $(Join-Path $Dir 'build')." }
            $St.Version = Get-Info (Join-Path $Dir 'build\install-info.txt') 'version'
        }
        if (-not $NoLaunch) {
            Step 'Starting the launcher'
            Start-Launcher
        }
        $v = $St.Version
        if (-not $v) { $v = 'unknown' }
        Finish-Ok $v
    }

    # The folders a player may have played from before this script: a clone made the README's old way (in the user's
    # folder or one of its usual folders, or the current folder or its parent), or an install of this script in another
    # folder. Each once, never this install or a folder in it.
    function Get-OldCopies {
        $cwd = (Get-Location -PSProvider FileSystem).ProviderPath
        $parents = @($env:USERPROFILE, [Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('MyDocuments'))
        if ($env:USERPROFILE) { $parents += Join-Path $env:USERPROFILE 'Downloads' }
        $places = @($parents | Where-Object { $_ } | ForEach-Object { Join-Path $_ 'tpw-port' }) + @($cwd, (Split-Path -Parent $cwd))
        $seen = @{}
        foreach ($c in $places) {
            if (-not $c) { continue }
            $isClone = (Test-Path -LiteralPath (Join-Path $c '.git')) -and
                       (Test-Path -LiteralPath (Join-Path $c 'launcher\VERSION.txt') -PathType Leaf)
            if (-not $isClone -and -not (Test-Path -LiteralPath (Join-Path $c '.tpw-installer'))) { continue }
            $full = [System.IO.Path]::GetFullPath($c).TrimEnd('\')
            if ($full -eq $Dir -or $full.StartsWith("$Dir\", [StringComparison]::OrdinalIgnoreCase)) { continue }
            if ($seen.ContainsKey($full.ToLowerInvariant())) { continue }
            $seen[$full.ToLowerInvariant()] = $true
            $full
        }
    }

    # Tells the player about the folders Get-OldCopies finds. Nothing in them is read, changed or deleted.
    function Show-OldCopies {
        $copies = @(Get-OldCopies)
        if ($copies.Count -eq 0) { return }
        Say ''
        foreach ($c in $copies) { Say "An older copy of the launcher is in $c." }
        if ($copies.Count -gt 1) {
            Say 'If you only used them to play, they are no longer needed: this install replaces them, and you can delete'
            Say 'those folders. Your games, saves and settings are not in them.'
        } else {
            Say 'If you only used it to play, it is no longer needed: this install replaces it, and you can delete that'
            Say 'folder. Your games, saves and settings are not in it.'
        }
        Say "They are in $(Join-Path $env:APPDATA 'STP-TPW Launcher'), and this install uses them as they are."
    }

    function Invoke-Install {
        if ($Update) {
            if (-not (Test-Path -LiteralPath (Join-Path $Dir 'channel.txt')) -or -not (Test-Path -LiteralPath (Join-Path $Src '.git'))) {
                Fail "There is no install in $Dir to update."
            }
        }
        # Only -Dev on this very run gives main; -Update included, channel.txt only records what is installed (ONE LINE
        # FOR EVERYONE).
        if (-not $St.Channel) { $St.Channel = 'stable' }
        if ($St.Channel -eq 'stable' -and (Read-Channel) -eq 'dev') {
            Say 'This install followed main (-Dev); it now follows the newest release. Add -Dev to stay on main.'
        }

        # check, download, build, verify; then switch and start unless -Stage
        $St.StepTotal = 4
        if (-not $Stage) { $St.StepTotal += 1 }
        if (-not $Stage -and -not $NoLaunch) { $St.StepTotal += 1 }

        Step 'Checking this computer'
        Test-Computer

        Step "Downloading the source"
        if (-not $Stub) { Select-Target }
        try {
            [void](New-Item -ItemType Directory -Force -Path $Dir -ErrorAction Stop)
            Write-Text (Join-Path $Dir '.tpw-installer') ''
        } catch { Fail "Couldn't make the folder $Dir." }
        if ($Stub) {
            Say 'Test build: nothing is downloaded.'
            $St.Version = '0.0.0-dev'
            if ($env:TPW_INSTALL_STUB_VERSION) { $St.Version = $env:TPW_INSTALL_STUB_VERSION }
            $St.Commit = "stub-$($St.Version)"
        } else {
            Get-Source
        }

        $buildInfo = Join-Path $Dir 'build\install-info.txt'
        $nextInfo = Join-Path $Next 'install-info.txt'
        $current = (Test-Path -LiteralPath (Join-Path $Dir 'build\tpw_launcher.exe')) -and ((Get-Info $buildInfo 'commit') -eq $St.Commit)
        $ready = (-not $current) -and ((Get-Info $nextInfo 'commit') -eq $St.Commit) -and
                 ((Get-Info $nextInfo 'channel') -eq $St.Channel) -and (Test-Path -LiteralPath (Join-Path $Next 'tpw_launcher.exe'))

        if ($current) {
            Say "Already up to date: version $($St.Version)."
            # A build staged earlier is of another commit now: drop it, so that a later -SwapAndLaunch cannot switch to it.
            Remove-Own 'build-next'
            Step 'Building the launcher'
            Say 'Nothing to build.'
            Step 'Checking the new launcher'
            Say 'Nothing to check.'
            if ((Read-Channel) -ne $St.Channel) {
                Write-Channel $St.Channel
                $lines = @(Get-Content -LiteralPath $buildInfo | ForEach-Object { if ($_ -like 'channel=*') { "channel=$($St.Channel)" } else { $_ } })
                Write-Text $buildInfo (($lines -join "`n") + "`n")
            }
        } elseif ($ready) {
            Say "This version was built earlier and is waiting in $Next."
            Step 'Building the launcher'
            Say 'Nothing to build.'
            Step 'Checking the new launcher'
            Test-Next
        } elseif ($Stub) {
            Remove-Own 'build-next'
            Step 'Getting the build tools and building the launcher (a few minutes)'
            New-StubLauncher
            Step 'Checking the new launcher'
            Test-Next
        } else {
            New-Next
            Step 'Getting the build tools and building the launcher (a few minutes)'
            Invoke-Build
            Step 'Checking the new launcher'
            Test-Next
        }

        if ($Stage) {
            if (-not $current) { Say "The new version is ready in $Next. Nothing was switched." }
            Finish-Ok $St.Version
            return
        }
        Step 'Switching to the new version'
        if (-not $current) { Switch-Build } else { Say 'Nothing to switch.' }
        # THE THEMEPARK COMMAND: the file at every switch, the Path entry only on an install
        $play = "Start it with: $(Join-Path $Dir 'build\tpw_launcher.exe')"
        $everyWindow = 'From now on, type themepark in any new PowerShell window to play. It works from any folder.'
        if (Write-Command) {
            $play = "To play later, run: $(Join-Path $Dir 'bin\themepark.cmd')"
            if ($Update) {
                if (Test-UserPath) { $play = $everyWindow }
            } elseif (Add-UserPath) {
                # Run as a file, the script's Path ends with it; through iex it is the player's window.
                Add-SessionPath
                if (-not $TpwInstallFromFile) {
                    $everyWindow = 'From now on, type themepark in this window or any new PowerShell window to play. It works from any folder.'
                }
                if (Test-NewWindow) {
                    $play = $everyWindow
                } else {
                    # not a failure of the install: the launcher is in place, only the shortcut needs this
                    $bin = (Join-Path $Dir 'bin') -replace "'", "''"
                    Say 'A new PowerShell window does not find themepark yet.'
                    Say 'To fix it, run this line once in PowerShell, then open a new window:'
                    Say "  [Environment]::SetEnvironmentVariable('Path', [Environment]::GetEnvironmentVariable('Path', 'User') + ';$bin', 'User')"
                }
            }
        }
        if (-not $NoLaunch) {
            Step 'Starting the launcher'
            Start-Launcher
        }
        Say $play
        if (-not $Update) { Show-OldCopies }
        Finish-Ok $St.Version
    }

    # ---- main

    try {
        if ($Help) { Show-Usage; $St.Done = $true; return 0 }
        if ($Rest) { Fail "Unknown option $($Rest -join ' ') (-Help lists them)." }
        if ($Stage -and -not $Update) { Fail '-Stage goes with -Update.' }
        if ($SwapAndLaunch -and $Update) { Fail '-SwapAndLaunch does not go with -Update.' }
        if ($WaitPid -and -not $SwapAndLaunch) { Fail '-WaitPid goes with -SwapAndLaunch.' }
        if ($WaitPid -and $WaitPid -notmatch '^[1-9][0-9]{0,9}$') { Fail '-WaitPid needs a process number.' }
        $St.Channel = ''
        if ($Dev) { $St.Channel = 'dev' }
        if ($Stable) { $St.Channel = 'stable' }

        if (-not $Dir) { $Dir = Join-Path $env:LOCALAPPDATA 'STP-TPW-Launcher' }
        # A relative folder is taken from the current one; GetFullPath takes out . and .. (Combine keeps a full path).
        $Dir = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine((Get-Location -PSProvider FileSystem).ProviderPath, $Dir)).TrimEnd('\')
        if (Test-UnsafePath $Dir) { Fail "Not using `"$Dir`" as the install folder. Choose another with -Dir." }
        $Src = Join-Path $Dir 'source'
        $Next = Join-Path $Dir 'build-next'
        $Log = Join-Path $Dir 'last-build.log'

        # A folder that exists, has things in it and is not this script's is somebody else's (a clone to work in, for
        # example, which has its own build\). Never write into it.
        if ((Test-Path -LiteralPath $Dir -PathType Container) -and
            -not (Test-Path -LiteralPath (Join-Path $Dir '.tpw-installer')) -and
            -not (Test-Path -LiteralPath (Join-Path $Dir 'channel.txt')) -and
            $null -ne (Get-ChildItem -LiteralPath $Dir -Force -ErrorAction SilentlyContinue | Select-Object -First 1)) {
            Say "$Dir exists and was not made by this installer, so it is left alone."
            Say "Choose another place with -Dir, for example: -Dir `"$Dir-installed`""
            Fail "$Dir is not empty and not an install made by this script. Choose another folder with -Dir."
        }

        if ($SwapAndLaunch) { Invoke-Swap } else { Invoke-Install }
        return 0
    } catch {
        if ("$_" -ne 'tpw-install-failed') {
            Write-Host "RESULT fail Unexpected error: $("$_" -replace '\s+', ' ')"
            $St.Done = $true
        }
        return 1
    } finally {
        # Ctrl+C runs this and nothing else: the one RESULT line still comes.
        if (-not $St.Done) { Write-Host 'RESULT fail Stopped before it finished. The launcher in use was not changed.' }
    }
})[-1]

if ($null -eq $TpwInstallCode) { $TpwInstallCode = 1 }
$global:LASTEXITCODE = [int]$TpwInstallCode
if ($TpwInstallFromFile) { exit $TpwInstallCode }
Remove-Variable TpwInstallCode, TpwInstallFromFile -ErrorAction SilentlyContinue
