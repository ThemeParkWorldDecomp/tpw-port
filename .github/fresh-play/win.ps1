# The steps of fresh-play.yml on Windows: a fresh install of this commit with the players' install line, the game bundle
# imported by the installed launcher and the easy-park plays. TEMPORARY, on the ci-020-fresh branch only. The Windows
# counterpart of unix.sh, step for step.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File .github\fresh-play\win.ps1 STEP [NAME]
#
# STEP is one of: secrets, release, install, version, desktop, fetch, import, play NAME, frames, clean. Every step writes
# what it saw into $env:RUNNER_TEMP\play-out (the run's artifact: outputs, logs and the frames as png). The game's files
# exist only on the runner: the bundle, the data folder and the bmp frames are never kept, and clean deletes them.
#
# Windows PowerShell 5.1 runs it (the players' install line is theirs). This file is plain ASCII on purpose.

param([string]$Step = '', [string]$Name = '')

$ErrorActionPreference = 'Continue'   # a native program's stderr must not stop a step; the checks decide
$ProgressPreference = 'SilentlyContinue'

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Src = [System.IO.Path]::GetFullPath((Join-Path $Here '..\..'))
$Temp = $env:RUNNER_TEMP
$Out = Join-Path $Temp 'play-out'
$Frames = Join-Path $Temp 'tpw-frames'
$Zip = Join-Path $Temp 'tpw-bundle.zip'
$Mirror = Join-Path $Temp 'release'
$Dir = Join-Path $env:LOCALAPPDATA 'STP-TPW-Launcher'
$Build = Join-Path $Dir 'build'
$Launcher = Join-Path $Build 'tpw_launcher.exe'
$Data = Join-Path $env:APPDATA 'STP-TPW Launcher'
$Version = ([string](Get-Content -LiteralPath (Join-Path $Src 'launcher\VERSION.txt') -TotalCount 1)).Trim()
[void](New-Item -ItemType Directory -Force -Path $Out)

function Fail([string]$msg) {
    Write-Host "::error::$msg"
    exit 1
}

function Write-Text([string]$file, [string]$text) {
    [System.IO.File]::WriteAllText($file, $text, (New-Object System.Text.UTF8Encoding($false)))
}

# The data folder's log files (DATA\*.log and DATA\logs\*.log) and their sizes now.
function Get-LogSizes {
    $sizes = @{}
    foreach ($folder in $Data, (Join-Path $Data 'logs')) {
        if (Test-Path -LiteralPath $folder) {
            foreach ($f in Get-ChildItem -LiteralPath $folder -Filter *.log -File) { $sizes[$f.FullName] = $f.Length }
        }
    }
    return $sizes
}

# What the log files gained since the sizes BEFORE (a new file whole), read while the files may still be open.
function Get-NewLogText($before) {
    $text = New-Object System.Text.StringBuilder
    $now = Get-LogSizes
    foreach ($f in ($now.Keys | Sort-Object)) {
        $from = 0
        if ($before.ContainsKey($f)) { $from = [int64]$before[$f] }
        [void]$text.AppendLine("==== $f (from byte $from)")
        try {
            $s = [System.IO.File]::Open($f, 'Open', 'Read', 'ReadWrite, Delete')
            try {
                [void]$s.Seek($from, 'Begin')
                $r = New-Object System.IO.StreamReader($s, (New-Object System.Text.UTF8Encoding($false)))
                [void]$text.Append($r.ReadToEnd())
            } finally { $s.Dispose() }
        } catch { [void]$text.AppendLine("(not readable: $_)") }
    }
    return $text.ToString()
}

# Processes running a program of the install folder's build (tpw_launcher.exe, or the game's Sim Theme Park.exe, which
# the launcher window's Play starts beside it: launcher/game_app.h).
function Get-BuildProcesses {
    $prefix = $Build.TrimEnd('\') + '\'
    return @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
        $p = ''
        try { $p = "$($_.Path)" } catch { $p = '' }
        $p -and $p.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)
    })
}

function Invoke-Git([string[]]$GitArgs) {
    $o = @(& git.exe @GitArgs 2>&1 | ForEach-Object { "$_" })
    if ($LASTEXITCODE -ne 0) { $o | ForEach-Object { Write-Host $_ }; Fail "git $($GitArgs -join ' ') failed" }
    return ,$o
}

# Runs EXE with ARGS (no spaces in any of them), its output and errors into OUT and ERR, waiting at most SECONDS.
# Returns the exit code, or -1 when it was still running (killed).
function Invoke-Program([string]$exe, [string[]]$a, [string]$so, [string]$se, [int]$seconds) {
    $p = Start-Process -FilePath $exe -ArgumentList $a -WorkingDirectory $Build -NoNewWindow -PassThru `
        -RedirectStandardOutput $so -RedirectStandardError $se
    $null = $p.Handle   # keeps the process handle, so ExitCode is there after the wait
    if (-not $p.WaitForExit($seconds * 1000)) { try { $p.Kill() } catch {}; return -1 }
    return $p.ExitCode
}

# Test desktop settings for the steps that start the launcher: Mesa llvmpipe for OpenGL (the desktop step puts it
# beside the program), and SDL's software renderer for the launcher's own 2D window (SDL's GPU renderer fails inside the
# hosted VM's Mesa: TGSI SAMPLE). Neither is in the install or changes a player's graphics driver.
function Use-TestDesktop {
    $env:GALLIUM_DRIVER = 'llvmpipe'
    $env:SDL_RENDER_DRIVER = 'software'
}

switch ($Step) {
    # Fails: one of the three secrets is not set (they are the repository's Actions secrets, set by the maintainer).
    'secrets' {
        $missing = @()
        if (-not $env:BUNDLE_URL) { $missing += 'TPW_FRESH_BUNDLE_URL' }
        if (-not $env:BUNDLE_SIZE) { $missing += 'TPW_FRESH_BUNDLE_SIZE' }
        if (-not $env:BUNDLE_MD5) { $missing += 'TPW_FRESH_BUNDLE_MD5' }
        if ($missing.Count -gt 0) { Fail "the bundle's secrets are not set: $($missing -join ' ') (Settings, Secrets and variables, Actions)" }
        Write-Host "the bundle's three secrets are set"
    }

    # A local release of this checkout (unix.sh, release): a copy whose only release tag is vVERSION on the checked-out
    # commit, and bare copies of its submodules beside it, each with a branch on the pinned commit. Nothing is pushed.
    'release' {
        if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') { Fail "launcher\VERSION.txt says $Version, not a release" }
        [void](New-Item -ItemType Directory -Force -Path $Mirror)
        $repo = Join-Path $Mirror 'tpw-port'
        [void](Invoke-Git @('clone', '-q', $Src, $repo))
        foreach ($t in (Invoke-Git @('-C', $repo, 'tag', '-l'))) { if ($t) { [void](Invoke-Git @('-C', $repo, 'tag', '-d', $t)) } }
        $head = (Invoke-Git @('-C', $Src, 'rev-parse', 'HEAD'))[0].Trim()
        [void](Invoke-Git @('-C', $repo, 'tag', "v$Version", $head))
        foreach ($line in (Invoke-Git @('-C', $Src, 'config', '-f', '.gitmodules', '--get-regexp', '\.path$'))) {
            $key, $path = $line -split ' ', 2
            $url = (Invoke-Git @('-C', $Src, 'config', '-f', '.gitmodules', '--get', ($key -replace '\.path$', '.url')))[0].Trim()
            if (-not $url.StartsWith('../')) { Fail "the submodule $path has the address $url" }
            $bare = Join-Path $Mirror $url.Substring(3)
            [void](Invoke-Git @('clone', '-q', '--bare', (Join-Path $Src $path), $bare))
            $pinned = (Invoke-Git @('-C', (Join-Path $Src $path), 'rev-parse', 'HEAD'))[0].Trim()
            [void](Invoke-Git @('-C', $bare, 'branch', '-f', 'ci-pin', $pinned))
            Write-Host "submodule ${path}: $pinned"
        }
        $tags = @(Invoke-Git @('ls-remote', '--tags', $repo))
        $tags | ForEach-Object { Write-Host $_ }
        Write-Text (Join-Path $Out 'release-tags.txt') (($tags -join "`n") + "`n")
        if ($tags.Count -ne 1) { Fail 'the local release has other tags' }
        Write-Host "v$Version is $head"
    }

    # The players' line, irm ... | iex, runs install.ps1's text in the window, with no file. This runs the local
    # release's install.ps1 the same way, as a script block from its text, with -NoLaunch (the launcher is started by
    # the steps below instead), on its stable channel. Fails: the install folder or the data folder is already there
    # (not a fresh runner), the exit code is not 0, or the last line is not "RESULT ok VERSION".
    'install' {
        if (Test-Path -LiteralPath $Dir) { Fail "an install folder is already there: $Dir" }
        if (Test-Path -LiteralPath $Data) { Fail "a data folder is already there: $Data" }
        $env:TPW_INSTALL_REPO = Join-Path $Mirror 'tpw-port'
        # git 2.38.1 and newer refuse a local submodule's file protocol unless allowed (installer-ci/win.ps1 does the same)
        $env:GIT_CONFIG_COUNT = '1'
        $env:GIT_CONFIG_KEY_0 = 'protocol.file.allow'
        $env:GIT_CONFIG_VALUE_0 = 'always'
        $env:GIT_TERMINAL_PROMPT = '0'
        $text = Get-Content -Raw -LiteralPath (Join-Path $env:TPW_INSTALL_REPO 'install.ps1')
        Write-Host "& ([scriptblock]::Create((Get-Content -Raw $env:TPW_INSTALL_REPO\install.ps1))) -NoLaunch"
        $start = Get-Date
        $global:LASTEXITCODE = 0
        $o = @(& ([scriptblock]::Create($text)) -NoLaunch 6>&1 | ForEach-Object { $s = "$_"; Write-Host $s; $s })
        $rc = $LASTEXITCODE
        Write-Host "the install took $([int]((Get-Date) - $start).TotalSeconds) s, exit $rc"
        Write-Text (Join-Path $Out 'install.txt') (($o -join "`n") + "`n")
        $log = Join-Path $Dir 'last-build.log'
        if (Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination (Join-Path $Out 'last-build.log') }
        $last = ''
        if ($o.Count -gt 0) { $last = $o[-1] }
        if ($rc -ne 0) { Fail "the install exited with ${rc}: $last" }
        if ($last -cne "RESULT ok $Version") { Fail "the install's last line is `"$last`", not `"RESULT ok $Version`"" }
        $channel = ([string](Get-Content -LiteralPath (Join-Path $Dir 'channel.txt') -TotalCount 1)).Trim()
        if ($channel -cne 'stable') { Fail "channel.txt says $channel" }
        if (-not (Test-Path -LiteralPath $Launcher)) { Fail "no launcher at $Launcher" }
        $commit = (Invoke-Git @('-C', (Join-Path $Dir 'source'), 'rev-parse', 'HEAD'))[0].Trim()
        Write-Host "installed: $Launcher, channel stable, source $commit"
    }

    # The installed launcher's --version, then themepark --version in a new Windows PowerShell and a new PowerShell 7
    # window (the user's Path as a new window reads it from the registry). Fails: the first line is not
    # "STP/TPW Launcher vVERSION", or a new window does not find our themepark or gets another answer.
    'version' {
        $so = Join-Path $Out 'version.txt'
        $rc = Invoke-Program $Launcher @('--version') $so (Join-Path $Out 'version-err.txt') 60
        Get-Content -LiteralPath $so | ForEach-Object { Write-Host $_ }
        if ($rc -ne 0) { Fail "--version exited with $rc" }
        $first = ([string](Get-Content -LiteralPath $so -TotalCount 1)).Trim()
        if ($first -cne "STP/TPW Launcher v$Version") { Fail "--version says `"$first`"" }
        $expected = (Join-Path $Dir 'bin\themepark.cmd') -replace "'", "''"
        $check = @"
`$ProgressPreference = 'SilentlyContinue'
`$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
`$c = Get-Command themepark -ErrorAction SilentlyContinue | Select-Object -First 1
if (`$null -eq `$c) { [Console]::Out.WriteLine('themepark: not found'); exit 3 }
if (`$c.Path -ine '$expected') { [Console]::Out.WriteLine('themepark: another one, ' + `$c.Path); exit 4 }
[Console]::Out.WriteLine('themepark: ours')
themepark --version
exit `$LASTEXITCODE
"@
        $encoded = [Convert]::ToBase64String([System.Text.Encoding]::Unicode.GetBytes($check))
        $shells = @('powershell.exe')
        if (Get-Command pwsh.exe -ErrorAction SilentlyContinue) { $shells += 'pwsh.exe' }
        $all = @()
        foreach ($shell in $shells) {
            Push-Location -LiteralPath $env:USERPROFILE
            $global:LASTEXITCODE = 0
            $o = @(& $shell -NoLogo -NoProfile -NonInteractive -EncodedCommand $encoded 2>&1 | ForEach-Object { "$_" })
            $code = $LASTEXITCODE
            Pop-Location
            $o | ForEach-Object { Write-Host "  [$shell] $_" }
            $all += @("[$shell exit $code]") + $o
            if ($code -ne 0) { Fail "themepark --version in a new $shell window exited with $code" }
            if (-not ($o -ceq 'themepark: ours')) { Fail "a new $shell window does not find $expected" }
            if (-not ($o -ceq "STP/TPW Launcher v$Version")) { Fail "themepark --version in a new $shell window did not say v$Version" }
        }
        Write-Text (Join-Path $Out 'themepark-version.txt') (($all -join "`n") + "`n")
        Write-Host "the installed launcher says v$Version, and themepark --version in a new $($shells -join ' and a new ') window too"
    }

    # The test desktop, which a player's PC has and the runner does not: a pinned Mesa (mesa-dist-win 26.2.4, its
    # checksum below) whose llvmpipe gives the game its OpenGL. Its DLLs go beside the installed program, after the
    # install, so that the install ran on the runner as it is. Fails: the fetch, the checksum or the unpacking.
    'desktop' {
        $archive = Join-Path $Temp 'mesa.7z'
        & curl.exe --fail --location --retry 3 --silent --show-error --output $archive `
            https://github.com/pal1000/mesa-dist-win/releases/download/26.2.4/mesa3d-26.2.4-release-msvc.7z
        if ($LASTEXITCODE -ne 0) { Fail "Mesa: curl.exe exited with $LASTEXITCODE" }
        # mesa-dist-win 26.2.4, msvc build:
        $want = '351fc8c8b695878ffb3eaa044b3ead08672a48b1a045e3c3e3975811df0f6695'
        $got = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($got -ne $want) { Fail 'Mesa: the checksum does not match' }
        $mesa = Join-Path $Temp 'mesa'
        & 7z x $archive "-o$mesa" -y | Out-Null
        if ($LASTEXITCODE -ne 0) { Fail "Mesa: 7z exited with $LASTEXITCODE" }
        Copy-Item -Path (Join-Path $mesa 'x64\*.dll') -Destination $Build
        Get-ChildItem -LiteralPath $Build -Filter *.dll | ForEach-Object { Write-Host "  $($_.Name)" }
    }

    # The bundle, fetched fresh by this run. Fails: curl.exe gives up after three tries or the server answers an error,
    # the file is not the bundle's size, or its MD5 is not the bundle's checksum.
    'fetch' {
        & curl.exe --fail --location --retry 3 --retry-delay 10 --silent --show-error --output $Zip `
            --write-out 'fetched %{size_download} bytes in %{time_total} s (%{speed_download} bytes/s)\n' $env:BUNDLE_URL
        if ($LASTEXITCODE -ne 0) { Fail "the bundle could not be fetched (curl.exe exit $LASTEXITCODE)" }
        $size = (Get-Item -LiteralPath $Zip).Length
        $want = ($env:BUNDLE_SIZE -replace '\s', '')
        if ("$size" -ne $want) { Fail "the bundle is $size bytes, not $want" }
        $got = (Get-FileHash -LiteralPath $Zip -Algorithm MD5).Hash.ToLowerInvariant()
        if ($got -ne ($env:BUNDLE_MD5 -replace '\s', '').ToLowerInvariant()) { Fail "the bundle's checksum does not match (a damaged fetch or another file)" }
        Write-Host "the bundle: $size bytes, its checksum matches"
    }

    # The bundle imported by the installed launcher's --import, which is the drop's import (unix.sh, import), into the
    # launcher's own data folder, fresh on this runner. Fails: the launcher exits other than 0 or runs past 15 minutes.
    'import' {
        Use-TestDesktop
        $so = Join-Path $Out 'import.txt'; $se = Join-Path $Out 'import-err.txt'
        $start = Get-Date
        $rc = Invoke-Program $Launcher @('--import', $Zip) $so $se 900
        Get-Content -LiteralPath $so, $se | ForEach-Object { Write-Host $_ }
        Write-Host "the import took $([int]((Get-Date) - $start).TotalSeconds) s, exit $rc"
        if ($rc -ne 0) { Fail "tpw_launcher.exe --import exited with $rc" }
        if (-not (Select-String -LiteralPath $so, $se -SimpleMatch 'import: into the data folder' -Quiet)) { Fail 'the import did not name its data folder' }
        Select-String -LiteralPath $so, $se -SimpleMatch 'import: into the data folder' | ForEach-Object { Write-Host $_.Line }
        if (-not (Test-Path -LiteralPath $Data)) { Fail "the data folder $Data is not there after the import" }
    }

    # One play through the easy-park recipe, as unix.sh's play (the same NAMEs, clicks and checks).
    'play' {
        Use-TestDesktop
        $fdir = Join-Path $Frames $Name
        [void](New-Item -ItemType Directory -Force -Path $fdir)
        $before = Get-LogSizes
        switch ($Name) {
            'us' { $startArgs = @('--play', 'us') }
            'eur' { $startArgs = @('--play', 'eur') }
            'gold' { $startArgs = @('--window-click', '205,373@30') }
            'us-vulkan' { $startArgs = @('--play', 'us', '--renderer', 'vulkan') }
            default { Fail "play wants us, eur, gold or us-vulkan: $Name" }
        }
        $recipe = @('--click', '663,60@200', '--type', 'Tess@230', '--click', '709,283@300', '--click', '71,588@800',
            '--click', '130,707@2300', '--click', '173,596@2500', '--click', '252,176@2700', '--click', '598,100@2850')
        $launch = $startArgs + @('--mute', '--no-movies', '--windowed', '--display', '0', '--aspect', '4:3',
            '--resolution', '1024x768') + $recipe + @('--frames', '3300', '--dump-frames', $fdir, '--dump-every', '150')
        Write-Host "$Launcher $($launch -join ' ')"
        $so = Join-Path $Out "play-$Name.txt"; $se = Join-Path $Out "play-$Name-err.txt"
        $t0 = Get-Date
        $rc = Invoke-Program $Launcher $launch $so $se 900
        $late = ($rc -eq -1)
        # The launcher window's Play starts the game as a program of its own, Sim Theme Park.exe, and the launcher
        # exits: the run goes on while a program of the build runs, within the same 15 minutes.
        while (-not $late -and (Get-BuildProcesses).Count -gt 0) {
            Start-Sleep -Seconds 10
            if (((Get-Date) - $t0).TotalSeconds -ge 900) { $late = $true }
        }
        $left = Get-BuildProcesses
        if ($left.Count -gt 0) {
            Write-Host "still running, killed: $(($left | ForEach-Object { "$($_.ProcessName) $($_.Id)" }) -join ', ')"
            $left | ForEach-Object { try { $_.Kill() } catch {} }
            Start-Sleep -Seconds 3
        }
        $secs = [int]((Get-Date) - $t0).TotalSeconds
        $text = ((Get-Content -LiteralPath $so, $se) -join "`n") + "`n" + (Get-NewLogText $before)
        Write-Text (Join-Path $Out "text-$Name.txt") $text
        Get-Content -LiteralPath $so, $se | Select-Object -Last 25 | ForEach-Object { Write-Host $_ }
        $bmps = @(Get-ChildItem -LiteralPath $fdir -Filter 'frame-*.bmp' | Sort-Object Name)
        $lastFrame = 'none'
        if ($bmps.Count -gt 0) { $lastFrame = $bmps[-1].Name }
        $lines = $text -split "`r?`n"
        $why = @()
        if ($late) { $why += 'still running after 15 minutes (killed)' }
        if ($rc -ne 0) { $why += "the launcher exited with $rc" }
        $crash = @($lines | Where-Object { $_ -match 'launcher: crashed|exit: crashed|exit: abort' })
        if ($crash.Count -gt 0) { $crash | ForEach-Object { Write-Host $_ }; $why += 'a crash' }
        $bad = @($lines | Where-Object { $_ -match 'exit: code [1-9]' })
        if ($bad.Count -gt 0) { $bad | ForEach-Object { Write-Host $_ }; $why += 'an exit code other than 0 in the log' }
        if (-not ($lines | Where-Object { $_ -match 'exit: code 0' })) { $why += 'no "exit: code 0" line in the log' }
        $stand = ''
        foreach ($l in $lines) { if ($l -match 'launcher: ([0-9]+ of [0-9]+ stand-ins were reached)') { $stand = $Matches[1] } }
        if (-not $stand) { $why += 'no "0 of N stand-ins were reached" line' }
        elseif (-not $stand.StartsWith('0 of ')) { $why += $stand }
        if ($bmps.Count -eq 0) { $why += 'no frames' }
        if ($Name -eq 'gold') {
            if (-not ($lines | Where-Object { $_ -like '*play: Sim Theme Park, the US edition*' })) {
                $why += 'the game that started was not the US edition (the click missed the US tile''s Play?)'
            }
            if (-not ($lines | Where-Object { $_ -match 'Gold rides: on, [0-9]+ rides overlaid from assets/gold$' })) {
                $why += 'no whole "Gold rides: on" line'
            }
        }
        $lines | Where-Object { $_ -match '^.{0,40}(play: |Gold rides: |renderer|OpenGL|Vulkan|vulkan:|opengl:)' } |
            Sort-Object -Unique | Select-Object -First 20 | ForEach-Object { Write-Host $_ }
        $summary = "${Name}: exit $rc, $stand, $($bmps.Count) frames (last $lastFrame), $secs s"
        if ($why.Count -gt 0) {
            Write-Text (Join-Path $Out "result-$Name.txt") "FAIL $summary`n"
            Fail "--play ${Name}: $($why -join '; ') ($summary)"
        }
        Write-Text (Join-Path $Out "result-$Name.txt") "PASS $summary`n"
        Write-Host "PASS $summary"
    }

    # The frames as png (python's standard library only, as on Linux and macOS), and the data folder's logs (the run
    # logs, no game file) into play-out\data-logs. Fails: never.
    'frames' {
        $keep = Join-Path $Out 'data-logs'
        foreach ($f in (Get-LogSizes).Keys) {
            [void](New-Item -ItemType Directory -Force -Path $keep)
            try { Copy-Item -LiteralPath $f -Destination $keep -ErrorAction Stop } catch { Write-Host "not kept: $f ($_)" }
        }
        if (-not (Test-Path -LiteralPath $Frames)) { exit 0 }
        & python -I (Join-Path $Here 'bmp2png.py') $Frames (Join-Path $Out 'frames')
        if ($LASTEXITCODE -ne 0) { Write-Host 'some frames were not converted' }
        exit 0
    }

    # The bundle, the data folder (the imported game files) and the bmp frames, deleted whatever happened. The install
    # folder holds no game file. Fails: never (a file still open is said, and the runner is discarded anyway).
    'clean' {
        Get-BuildProcesses | ForEach-Object { try { $_.Kill() } catch {} }
        foreach ($path in $Zip, $Data, $Frames, $Mirror, (Join-Path $Temp 'mesa.7z'), (Join-Path $Temp 'mesa')) {
            if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction Continue }
        }
        Get-ChildItem -LiteralPath $Temp -Force | Select-Object -ExpandProperty Name
        if (-not (Test-Path -LiteralPath $Zip) -and -not (Test-Path -LiteralPath $Data)) { Write-Host 'the bundle and the data folder are deleted' }
        exit 0
    }

    default { Write-Host "::error::unknown step: $Step"; exit 2 }
}
exit 0
