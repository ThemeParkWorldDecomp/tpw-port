# The steps of fresh-play.yml on Windows: a fresh install of this commit with the players' install line, the game bundle
# imported by the installed launcher and the easy-park plays. TEMPORARY, on the ci-030-fresh branch only. The Windows
# counterpart of unix.sh, step for step.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File .github\fresh-play\win.ps1 STEP [NAME]
#
# STEP is one of: secrets, release, install, version, desktop, nomesa, fetch, fetchpc, import, importpc, play NAME, upgrade, scroll, frames, clean. Every step writes
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
# Makes this step a player whose Windows user name is First Last WORD: Windows' folders and the install move under it.
function Set-Name([string]$which, [bool]$copyData) {
    $word = "Jos$([char]0xE9)"
    if ($which -eq 'cjk') { $word = "$([char]0x65E5)$([char]0x672C)" }
    $base = "C:\Users\First Last $word"
    $roaming = Join-Path $base 'AppData\Roaming'; $local = Join-Path $base 'AppData\Local'
    [void](New-Item -ItemType Directory -Force -Path $roaming, $local)
    $newBuild = Join-Path $local 'STP-TPW-Launcher\build'
    if (-not (Test-Path -LiteralPath $newBuild)) {
        [void](New-Item -ItemType Directory -Force -Path (Split-Path -Parent $newBuild))
        Copy-Item -LiteralPath $script:Build -Destination $newBuild -Recurse
    }
    $newData = Join-Path $roaming 'STP-TPW Launcher'
    if ($copyData -and -not (Test-Path -LiteralPath $newData)) { Copy-Item -LiteralPath $script:Data -Destination $newData -Recurse }
    $shell = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer'
    foreach ($key in @("$shell\User Shell Folders", "$shell\Shell Folders")) {
        Set-ItemProperty -LiteralPath $key -Name 'AppData' -Value $roaming
        Set-ItemProperty -LiteralPath $key -Name 'Local AppData' -Value $local
    }
    $env:APPDATA = $roaming; $env:LOCALAPPDATA = $local; $env:USERPROFILE = $base
    $script:Build = $newBuild; $script:Launcher = Join-Path $newBuild 'tpw_launcher.exe'; $script:Data = $newData
}

# What must hold under such a name: the log in the real folder, and no second First Last folder beside the two.
function Get-NameFaults {
    $faults = @()
    if (-not (Test-Path -LiteralPath (Join-Path $script:Data 'logs\launcher.log'))) { $faults += "no launcher.log in the real folder $($script:Data)" }
    $good = @("First Last Jos$([char]0xE9)", "First Last $([char]0x65E5)$([char]0x672C)")
    foreach ($d in Get-ChildItem -LiteralPath 'C:\Users' -Directory) {
        if ($d.Name -like 'First Last*' -and $good -notcontains $d.Name) { $faults += "a second folder: $($d.Name)" }
    }
    return $faults
}

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
        if ($env:OLD_COMMIT) { $head = $env:OLD_COMMIT; $Version = '0.2.0' }
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
        if ($env:OLD_COMMIT) {
            $Version = '0.2.0'
            $text = (& git.exe -C $env:TPW_INSTALL_REPO show "$($env:OLD_COMMIT):install.ps1") -join "`n"
        }
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
        # The harder conditions: NAME is CONDITION-EDITION, started as a player starts it (the window's Play).
        $pick = $Name; $variant = ''; $extra = @()
        if ($Name -match '^(d3d|gl20|gl14|accent|cjk|gdi|pc|pcaccent|pccjk|parkaccent|parkcjk|nodev|nomute|nodriver|pcnodev|pcnomute|ctlnomute|ctldriver|arrive)-(us|eur)$') { $variant = $Matches[1]; $pick = "$($Matches[2])-default" }
        switch ($variant) {
            # SDL's default renderer. Mesa's d3d10warp.dll stays out of the build: beside the program it takes the place of
            # Windows' own, and dies on TGSI SAMPLE. A player's install has none of Mesa's files.
            'd3d' {
                Remove-Item Env:SDL_RENDER_DRIVER
                Remove-Item -LiteralPath (Join-Path $Build 'd3d10warp.dll') -Force -ErrorAction SilentlyContinue
            }
            # No working sound device: the program's own switch for the open step, an SDL driver of no such name for the step
            # before it, and nomute leaves the machine's real audio to answer (a runner may have no device at all).
            { $_ -eq 'nodev' -or $_ -eq 'pcnodev' } { $env:TPW_SOUND_NO_DEVICE = '1' }
            { $_ -eq 'nodriver' -or $_ -eq 'ctldriver' } { $env:SDL_AUDIO_DRIVER = 'nonesuch' }
            'arrive' { $env:TPW_SOUND_NO_DEVICE = 'after:20'; $env:SDL_AUDIO_DRIVER = 'dummy' }
            'gl20' { $env:MESA_GL_VERSION_OVERRIDE = '2.0' }
            'gl14' {
                $env:MESA_GL_VERSION_OVERRIDE = '1.4'
                $env:MESA_EXTENSION_OVERRIDE = '-GL_EXT_framebuffer_object -GL_ARB_framebuffer_object -GL_EXT_framebuffer_blit'
            }
            'gdi' { Remove-Item Env:GALLIUM_DRIVER; Remove-Item Env:SDL_RENDER_DRIVER }
            { $_ -eq 'accent' -or $_ -eq 'cjk' } { Set-Name $variant $true }
            { $_ -eq 'pcaccent' -or $_ -eq 'parkaccent' } { Set-Name 'accent' $false }
            { $_ -eq 'pccjk' -or $_ -eq 'parkcjk' } { Set-Name 'cjk' $false }
        }
        $fdir = Join-Path $Frames $Name
        [void](New-Item -ItemType Directory -Force -Path $fdir)
        $before = Get-LogSizes
        switch ($pick) {
            'us' { $startArgs = @('--play', 'us') }
            'eur' { $startArgs = @('--play', 'eur') }
            'gold' { $startArgs = @('--window-click', '205,373@30') }
            'us-vulkan' { $startArgs = @('--play', 'us', '--renderer', 'vulkan') }
            'us-default' { $startArgs = @('--window-click', '205,373@30') }
            'eur-default' { $startArgs = @('--window-click', '595,373@30') }
            default { Fail "play wants us, eur, gold or us-vulkan: $Name" }
        }
        $recipe = @('--click', '663,60@200', '--type', 'Tess@230', '--click', '709,283@300', '--click', '71,588@800',
            '--click', '130,707@2300', '--click', '173,596@2500', '--click', '252,176@2700', '--click', '598,100@2850')
        $launch = $startArgs + @('--mute', '--no-movies', '--windowed', '--display', '0', '--aspect', '4:3',
            '--resolution', '1024x768') + $recipe + @('--frames', '3300', '--dump-frames', $fdir, '--dump-every', '150')
        # As a player starts it: the launcher window's Play, its own size, the films on, no clicks.
        if ($pick -like '*-default') {
            $count = '1500'
            # Without the Gold CD the Play is the tile's own (launcher_ui.cpp, tile-play-*), clicked as a player clicks it.
            if ($variant -in @('pc', 'pcaccent', 'pccjk', 'pcnodev', 'pcnomute')) {
                $count = '3000'
                if ($pick -eq 'us-default') { $startArgs = @('--window-click', '226,325@30') } else { $startArgs = @('--window-click', '486,325@30') }
            }
            $launch = $startArgs + $extra + @('--mute', '--frames', $count, '--dump-frames', $fdir, '--dump-every', '150')
        }
        # Into a park under the name: the Windows disc by name, then the easy-park recipe.
        if ($variant -like 'park*') {
            $launch = @('--play', (($pick -replace '-default', '') + '-pc'), '--mute', '--no-movies', '--windowed', '--display', '0', '--aspect', '4:3',
                '--resolution', '1024x768') + $recipe + @('--frames', '3300', '--dump-frames', $fdir, '--dump-every', '150')
        }
        # The Windows disc by name, no click: reaches the game on 0.3.0 as well, whose window's Play cannot start a Windows disc.
        if ($variant -like 'ctl*') {
            $launch = @('--play', (($pick -replace '-default', '') + '-pc'), '--no-movies', '--windowed', '--display', '0', '--aspect', '4:3',
                '--resolution', '1024x768', '--frames', '1500', '--dump-frames', $fdir, '--dump-every', '150')
        }
        if ($variant -match 'nodev|nomute|nodriver|arrive') { $launch = @($launch | Where-Object { $_ -ne '--mute' }) }
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
        $pages = @($lines | Where-Object { $_ -match 'process code page [0-9]+' })
        $pages | Select-Object -First 4 | ForEach-Object { Write-Host $_ }
        # 0.2.0 has no code page line: in the update job, the plays before the update are on it.
        $onOld = $env:OLD_COMMIT -and $pick -notlike '*-default'
        if (-not $onOld) {
            if ($pages.Count -lt 1) { $why += 'no code page line in the log' }
            if (@($pages | Where-Object { $_ -notmatch 'process code page 65001' }).Count -gt 0) { $why += 'a code page other than 65001' }
            if ($pick -like '*-default' -and $variant -notlike 'park*' -and $variant -notlike 'ctl*' -and $pages.Count -lt 2) { $why += 'the launcher and the game did not both log a code page' }
        }
        if ($variant -match 'accent|cjk') { $why += @(Get-NameFaults) }
        $lines | Where-Object { $_ -match 'sound: ' } | Select-Object -First 6 | ForEach-Object { Write-Host $_ }
        if ($variant -match 'nodev' -and -not ($lines | Where-Object { $_ -like '*sound: no audio device*' })) { $why += 'no "no audio device" line: the failure was not exercised' }
        if ($variant -eq 'arrive' -and -not ($lines | Where-Object { $_ -like '*sound: no audio device*' })) { $why += 'no "no audio device" line' }
        if ($variant -eq 'arrive' -and -not ($lines | Where-Object { $_ -like '*sound: a sound device has opened*' })) { $why += 'the device that came was never opened' }
        if ($variant -match 'nodriver|ctldriver' -and -not ($lines | Where-Object { $_ -like '*sound: SDL audio failed to start*' })) { $why += 'no "SDL audio failed to start" line: the failure was not exercised' }
        if ($Name -eq 'gold') {
            if (-not ($lines | Where-Object { $_ -like '*play: Sim Theme Park, the US edition*' })) {
                $why += 'the game that started was not the US edition (the click missed the US tile''s Play?)'
            }
            if (-not ($lines | Where-Object { $_ -match 'Gold rides: on, [0-9]+ rides overlaid from assets/gold$' })) {
                $why += 'no whole "Gold rides: on" line'
            }
        }
        if ($pick -eq 'us-default' -and -not ($lines | Where-Object { $_ -like '*play: Sim Theme Park, the US edition*' })) { $why += 'the US edition did not start' }
        if ($pick -eq 'eur-default' -and -not ($lines | Where-Object { $_ -like '*play: Theme Park World*' })) { $why += 'the European edition did not start' }
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

    # This machine's own graphics: Mesa's files out of the build, for the gdi plays.
    'nomesa' {
        $mesa = Join-Path $Temp 'mesa\x64'
        foreach ($d in Get-ChildItem -LiteralPath $mesa -Filter *.dll) { Remove-Item -LiteralPath (Join-Path $Build $d.Name) -Force -ErrorAction SilentlyContinue }
        Get-ChildItem -LiteralPath $Build -Filter *.dll | ForEach-Object { Write-Host "  $($_.Name)" }
    }

    # The Windows discs, from the secret PC_DISCS ("URL MD5;URL MD5;..."): fetched under plain names, then imported one by
    # one. The addresses are never printed.
    'fetchpc' {
        $pc = Join-Path $Temp 'pc'
        [void](New-Item -ItemType Directory -Force -Path $pc)
        $i = 0
        foreach ($entry in ($env:PC_DISCS -split ';')) {
            $i++
            $u, $m = $entry.Trim() -split ' ', 2
            $disc = Join-Path $pc "disc$i$([System.IO.Path]::GetExtension($u))"
            & curl.exe --fail --location --retry 3 --retry-delay 10 --silent --show-error --output $disc `
                --write-out "disc $i`: %{size_download} bytes in %{time_total} s\n" $u
            if ($LASTEXITCODE -ne 0) { Fail "disc $i could not be fetched (curl.exe exit $LASTEXITCODE)" }
            $got = (Get-FileHash -LiteralPath $disc -Algorithm MD5).Hash.ToLowerInvariant()
            if ($got -ne $m.Trim().ToLowerInvariant()) { Fail "disc $i's checksum does not match" }
        }
    }
    # A fresh import of the Windows discs under a real user name (Name is accent or cjk). Fails: the discs do not land in the
    # real folder, or a second folder appears.
    'importname' {
        Use-TestDesktop
        Set-Name $Name $false
        foreach ($disc in Get-ChildItem -LiteralPath (Join-Path $Temp 'pc') -File | Sort-Object Name) {
            $so = Join-Path $Out "import-$Name-$($disc.BaseName).txt"; $se = Join-Path $Out "import-$Name-$($disc.BaseName)-err.txt"
            $rc = Invoke-Program $Launcher @('--import', $disc.FullName) $so $se 900
            Write-Host "$($disc.Name): exit $rc"
            Get-Content -LiteralPath $so, $se | Select-Object -Last 6 | ForEach-Object { Write-Host $_ }
            # --import exits 1 unless US, European and Gold are all ready, which Windows discs alone never make them.
            # The plays after this prove the discs landed.
            if ($rc -ne 0 -and $rc -ne 1) { Fail "the import of $($disc.Name) under $Name exited with $rc" }
        }
        $faults = @(Get-NameFaults)
        if ($faults.Count -gt 0) { Fail ($faults -join '; ') }
        Write-Host "imported under $Name into the real folder"
    }

    'importpc' {
        Use-TestDesktop
        foreach ($disc in Get-ChildItem -LiteralPath (Join-Path $Temp 'pc') -File | Sort-Object Name) {
            $so = Join-Path $Out "import-$($disc.BaseName).txt"; $se = Join-Path $Out "import-$($disc.BaseName)-err.txt"
            $rc = Invoke-Program $Launcher @('--import', $disc.FullName) $so $se 900
            Write-Host "$($disc.Name): exit $rc"
            Get-Content -LiteralPath $so, $se | Select-Object -Last 15 | ForEach-Object { Write-Host $_ }
        }
        if (-not (Test-Path -LiteralPath $Data)) { Fail "the data folder $Data is not there after the imports" }
    }

    # A player's update: the local release gains v0.3.0 on this checkout, and the line runs over the install of 0.2.0.
    # Fails: the exit code is not 0, or the last line is not "RESULT ok VERSION".
    'upgrade' {
        $mesa = Join-Path $Temp 'mesa\x64'
        if (Test-Path -LiteralPath $mesa) {
            foreach ($d in Get-ChildItem -LiteralPath $mesa -Filter *.dll) { Remove-Item -LiteralPath (Join-Path $Build $d.Name) -Force -ErrorAction SilentlyContinue }
        }
        $repo = Join-Path $Mirror 'tpw-port'
        $head = (Invoke-Git @('-C', $Src, 'rev-parse', 'HEAD'))[0].Trim()
        [void](Invoke-Git @('-C', $repo, 'fetch', '-q', $Src, $head))
        [void](Invoke-Git @('-C', $repo, 'tag', "v$Version", $head))
        $env:TPW_INSTALL_REPO = $repo
        $env:GIT_CONFIG_COUNT = '1'
        $env:GIT_CONFIG_KEY_0 = 'protocol.file.allow'
        $env:GIT_CONFIG_VALUE_0 = 'always'
        $env:GIT_TERMINAL_PROMPT = '0'
        $text = Get-Content -Raw -LiteralPath (Join-Path $Src 'install.ps1')
        $global:LASTEXITCODE = 0
        $o = @(& ([scriptblock]::Create($text)) -NoLaunch 6>&1 | ForEach-Object { $s = "$_"; Write-Host $s; $s })
        $rc = $LASTEXITCODE
        Write-Text (Join-Path $Out 'upgrade.txt') (($o -join "`n") + "`n")
        $last = ''
        if ($o.Count -gt 0) { $last = $o[-1] }
        if ($rc -ne 0) { Fail "the update exited with ${rc}: $last" }
        if ($last -cne "RESULT ok $Version") { Fail "the update's last line is `"$last`", not `"RESULT ok $Version`"" }
        Write-Host "updated to $Version"
    }

    # A real mouse on the launcher's Settings box: pictures of the window before and after the wheel, and after a drag on
    # the scroll bar, kept in play-out. Fails: the wheel changes nothing.
    'scroll' {
        Use-TestDesktop
        Add-Type -AssemblyName System.Drawing
        Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class FreshMouse {
    [StructLayout(LayoutKind.Sequential)] public struct P { public int X; public int Y; }
    [StructLayout(LayoutKind.Sequential)] public struct R { public int L; public int T; public int Rt; public int B; }
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, int d, UIntPtr e);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref P p);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out R r);
}
"@
        $p = Start-Process -FilePath $Launcher -ArgumentList @('--mute', '--window-click', '630,52@30') -WorkingDirectory $Build -PassThru
        $h = [IntPtr]::Zero
        for ($i = 0; $i -lt 60 -and $h -eq [IntPtr]::Zero; $i++) { Start-Sleep -Seconds 1; $p.Refresh(); $h = $p.MainWindowHandle }
        if ($h -eq [IntPtr]::Zero) { Fail 'the launcher has no window' }
        Start-Sleep -Seconds 8
        [void][FreshMouse]::SetForegroundWindow($h)
        Start-Sleep -Seconds 1
        $o = New-Object FreshMouse+P
        [void][FreshMouse]::ClientToScreen($h, [ref]$o)
        $c = New-Object FreshMouse+R
        [void][FreshMouse]::GetClientRect($h, [ref]$c)
        Write-Host "the window's inside: $($c.Rt) x $($c.B) at $($o.X),$($o.Y)"
        function Save-Shot([string]$n) {
            $b = New-Object System.Drawing.Bitmap $c.Rt, $c.B
            $g = [System.Drawing.Graphics]::FromImage($b)
            $g.CopyFromScreen($o.X, $o.Y, 0, 0, $b.Size)
            $f = Join-Path $Out "scroll-$n.png"
            $b.Save($f, [System.Drawing.Imaging.ImageFormat]::Png)
            $g.Dispose(); $b.Dispose()
            return (Get-FileHash -LiteralPath $f -Algorithm MD5).Hash
        }
        function Set-Wheel([int]$n) { for ($i = 0; $i -lt [Math]::Abs($n); $i++) { [FreshMouse]::mouse_event(0x0800, 0, 0, 120 * [Math]::Sign($n), [UIntPtr]::Zero); Start-Sleep -Milliseconds 100 }; Start-Sleep -Seconds 1 }
        [void][FreshMouse]::SetCursorPos($o.X + 90, $o.Y + 300)
        Start-Sleep -Seconds 1
        $a = Save-Shot 'a-open'
        Start-Sleep -Seconds 2
        $idle = Save-Shot 'a2-idle'
        Set-Wheel -5
        $b = Save-Shot 'b-wheel-down'
        Set-Wheel 5
        $d = Save-Shot 'c-wheel-up'
        [void][FreshMouse]::SetCursorPos($o.X + 709, $o.Y + 115)
        Start-Sleep -Milliseconds 300
        [FreshMouse]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
        for ($y = 115; $y -le 400; $y += 15) { [void][FreshMouse]::SetCursorPos($o.X + 709, $o.Y + $y); Start-Sleep -Milliseconds 50 }
        [FreshMouse]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Seconds 1
        $e = Save-Shot 'd-bar-drag'
        try { $p.Kill() } catch {}
        Write-Host "two shots with no input match: $($a -eq $idle); wheel down changed the window: $($a -ne $b); wheel back up matches the start: $($a -eq $d); the bar's drag changed it: $($d -ne $e)"
        Write-Text (Join-Path $Out 'result-scroll.txt') "two shots with no input match: $($a -eq $idle)`nwheel down changed the window: $($a -ne $b)`nwheel back up matches the start: $($a -eq $d)`nthe bar's drag changed it: $($d -ne $e)`n"
        if ($a -eq $b) { Fail 'the wheel changed nothing in the Settings box' }
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
        foreach ($path in $Zip, $Data, $Frames, $Mirror, (Join-Path $Temp 'pc'), "C:\Users\First Last Jos$([char]0xE9)", "C:\Users\First Last $([char]0x65E5)$([char]0x672C)", (Join-Path $Temp 'mesa.7z'), (Join-Path $Temp 'mesa')) {
            if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction Continue }
        }
        Get-ChildItem -LiteralPath $Temp -Force | Select-Object -ExpandProperty Name
        if (-not (Test-Path -LiteralPath $Zip) -and -not (Test-Path -LiteralPath $Data)) { Write-Host 'the bundle and the data folder are deleted' }
        exit 0
    }

    default { Write-Host "::error::unknown step: $Step"; exit 2 }
}
exit 0
