# The steps of origpc-login.yml: does the ORIGINAL PC game's login library reach our online server, and what does it do there?
# TEMPORARY, on the ci-origpc trial branch only.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File .github\origpc\win.ps1 STEP [NAME]
#
# STEP is one of: fetch, extract, hosts, build, probe, run NAME, series, trygame, hold, clean.
# The disc comes from the secrets (TPW_ORIGPC_DISC "URL MD5" if set, else the entries of TPW_FRESH_PC_DISCS "URL MD5;URL MD5"),
# the first one that holds the login library is kept; the addresses are never printed. Only the library (weaauthr.dll) and
# Online.sam's login lines are taken from it, into $env:RUNNER_TEMP\orig, which clean deletes. Everything kept for the run's
# artifact goes to $env:RUNNER_TEMP\origpc-out: the harness logs (message headers only, never the login body), netstat and
# pktmon's packet lines (addresses other than the server's masked) and screenshots. No game file is ever put there, nor the
# account name or password.
#
# Windows PowerShell 5.1 runs it. This file is plain ASCII on purpose.

param([string]$Step = '', [string]$Name = '')

$ErrorActionPreference = 'Continue'
$ProgressPreference = 'SilentlyContinue'

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Temp = $env:RUNNER_TEMP
$Out = Join-Path $Temp 'origpc-out'
$Orig = Join-Path $Temp 'orig'
$Bin = Join-Path $Orig 'bin'
$Dll = Join-Path $Bin 'WEAAUTHR.DLL'
$Exe = Join-Path $Temp 'origpc-tool\origpc_auth.exe'
$Server = $env:SERVER
if (-not $Server) { $Server = '164.92.124.157' }
$Hold = 120
if ($env:HOLD_SECONDS) { $Hold = [int]$env:HOLD_SECONDS }
$GameHost = 'daphne.eagames.co.uk'
[void](New-Item -ItemType Directory -Force -Path $Out, $Orig)

function Fail([string]$msg) {
    Write-Host "::error::$msg"
    exit 1
}

function Write-Text([string]$file, [string]$text) {
    [System.IO.File]::WriteAllText($file, $text, (New-Object System.Text.UTF8Encoding($false)))
}

function Add-Text([string]$file, [string]$text) {
    [System.IO.File]::AppendAllText($file, $text, (New-Object System.Text.UTF8Encoding($false)))
}

function Save-Screen([string]$file) {
    Add-Type -AssemblyName System.Windows.Forms, System.Drawing
    $r = [System.Windows.Forms.SystemInformation]::VirtualScreen
    $b = New-Object System.Drawing.Bitmap $r.Width, $r.Height
    $g = [System.Drawing.Graphics]::FromImage($b)
    try { $g.CopyFromScreen($r.X, $r.Y, 0, 0, $b.Size) } catch { Write-Host "screenshot failed: $_" }
    $b.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $b.Dispose()
    Write-Host "screenshot $([System.IO.Path]::GetFileName($file)) ($($r.Width)x$($r.Height))"
}

# The netstat lines for one remote or local port, with the time.
function Get-PortLines([int]$port) {
    $lines = @(netstat -ano -p TCP | Where-Object { $_ -match ":$port\s" })
    return $lines
}

# Every IPv4 address in netstat and the capture text but the server's and the loopback ones (the runner's own, other peers).
function Hide-Addresses([string]$text) {
    $keep = @('127.0.0.1', '0.0.0.0', $script:Server)
    $ev = [System.Text.RegularExpressions.MatchEvaluator] ({ param($m) if ($keep -contains $m.Value) { $m.Value } else { 'x.x.x.x' } }.GetNewClosure())
    return [regex]::Replace($text, '\b\d{1,3}(\.\d{1,3}){3}\b', $ev)
}

# One harness run. NAME is real-name (the game's server name through the hosts entry, held HOLD_SECONDS), real-ip-all-N /
# real-ip-cpu1-N (the address itself, on all CPUs or pinned to CPU 0), real-ip-retry (two attempts) or mock-all / mock-cpu1
# (the stand-in server on 127.0.0.1). Netstat is polled while it runs; the real ones also get a packet capture, of which only
# pktmon's one-line-per-packet text is kept (no packet bytes).
function Invoke-Run([string]$Name) {
    Write-Host "==== run $Name"
    $port = 7598
    $hargs = @('--dll', $Dll, '--log', (Join-Path $Out "harness-$Name.txt"))
    $real = $Name.StartsWith('real')
    if ($Name -eq 'real-name') { $hargs += @('--server', $GameHost, '--seconds', "$Hold") }
    elseif ($Name -eq 'real-ip-retry') { $hargs += @('--server', $Server, '--seconds', '20', '--attempts', '2') }
    elseif ($Name.StartsWith('real-ip-')) { $hargs += @('--server', $Server, '--seconds', '20') }
    elseif ($Name.StartsWith('mock-')) {
        $port = 17598
        $hargs += @('--server', '127.0.0.1', '--port', "$port", '--seconds', '15', '--mock', '1')
    } else { Fail "unknown run $Name" }
    if ($Name -match 'cpu1') { $hargs += @('--affinity', '1') }
    $etl = Join-Path $Temp "pktmon-$Name.etl"
    if ($real) {
        & pktmon.exe filter remove | Out-Null
        & pktmon.exe filter add origpc -t TCP -p 7598 | Out-Null
        & pktmon.exe start --capture --pkt-size 64 --file-name $etl | Out-Null
        Write-Host "pktmon start: exit $LASTEXITCODE"
    }
    $net = Join-Path $Out "netstat-$Name.txt"
    $p = Start-Process -FilePath $Exe -ArgumentList $hargs -PassThru -NoNewWindow `
        -RedirectStandardOutput (Join-Path $Temp "harness-$Name-stdout.txt") -RedirectStandardError (Join-Path $Out "harness-$Name-stderr.txt")
    $null = $p.Handle   # keeps the exit code readable after it ends
    $last = ''
    $states = @{}
    $start = Get-Date
    while (-not $p.HasExited) {
        $lines = @(Get-PortLines $port | ForEach-Object { Hide-Addresses (($_.Trim() -split '\s+') -join ' ') })
        $now = $lines -join "`n"
        if ($now -ne $last) {
            $stamp = '{0,6:N1}s' -f ((Get-Date) - $start).TotalSeconds
            Add-Text $net "---- $stamp`n$now`n"
            Write-Host "netstat $stamp"
            $lines | ForEach-Object { Write-Host "  $_" }
            $last = $now
        }
        foreach ($l in $lines) {
            $f = $l -split ' '
            if ($f.Count -ge 5 -and $f[4] -eq "$($p.Id)") { $states[$f[3]] = $true }
        }
        Start-Sleep -Milliseconds 500
    }
    if ($real) {
        & pktmon.exe stop | Out-Null
        $txt = Join-Path $Temp "pktmon-$Name.txt"
        & pktmon.exe etl2txt $etl --out $txt | Out-Null
        if (Test-Path -LiteralPath $txt) { Write-Text (Join-Path $Out "pktmon-$Name.txt") (Hide-Addresses ([System.IO.File]::ReadAllText($txt))) }
        Remove-Item -LiteralPath $etl, $txt -Force -ErrorAction SilentlyContinue
        & pktmon.exe filter remove | Out-Null
    }
    $summary = @(Get-Content -LiteralPath (Join-Path $Out "harness-$Name.txt") | Where-Object {
        $_ -match 'SUMMARY|CALLBACK|FAIL|WATCHDOG|MOCK a complete|MOCK received|commsThread|CreateThread|socket\(|WSAENOTSOCK|connect\(|send\(|recv\(|returned|==== attempt' })
    $sl = "$Name`: harness exit $($p.ExitCode); its connection states seen in netstat: $(($states.Keys | Sort-Object) -join ', ')"
    Write-Host $sl
    $summary | ForEach-Object { Write-Host "  $_" }
    Add-Text (Join-Path $Out 'summary.txt') ("$sl`n" + (($summary | ForEach-Object { "  $_" }) -join "`n") + "`n")
}

switch ($Step) {
    'fetch' {
        $entries = @()
        if ($env:ORIGPC_DISC) { $entries += $env:ORIGPC_DISC }
        if ($env:PC_DISCS) { $entries += ($env:PC_DISCS -split ';') }
        if ($entries.Count -eq 0) { Fail 'neither TPW_ORIGPC_DISC nor TPW_FRESH_PC_DISCS is set' }
        $i = 0
        foreach ($entry in $entries) {
            $i++
            if (-not $entry.Trim()) { continue }
            $u, $m = $entry.Trim() -split ' ', 2
            $disc = Join-Path $Orig "disc$i$([System.IO.Path]::GetExtension($u))"
            & curl.exe --fail --location --retry 3 --retry-delay 10 --silent --show-error --output $disc `
                --write-out "disc $i`: %{size_download} bytes in %{time_total} s\n" $u
            if ($LASTEXITCODE -ne 0) { Write-Host "disc $i could not be fetched (curl.exe exit $LASTEXITCODE)"; continue }
            if ($m) {
                $got = (Get-FileHash -LiteralPath $disc -Algorithm MD5).Hash.ToLowerInvariant()
                if ($got -ne $m.Trim().ToLowerInvariant()) { Remove-Item -LiteralPath $disc -Force; Fail "disc $i's checksum does not match" }
                Write-Host "disc $i`: checksum matches"
            }
            $list = @(& 7z.exe l -ba $disc 2>$null)
            $has = @($list | Where-Object { $_ -match '(^|[\\/ ])WEAAUTHR\.DLL$' }).Count -gt 0
            Write-Host "disc $i`: holds the login library: $has"
            if ($has) {
                Write-Text (Join-Path $Orig 'disc.txt') $disc
                exit 0
            }
            Remove-Item -LiteralPath $disc -Force
        }
        Fail 'no disc in the secrets holds WEAAUTHR.DLL (an original PC disc or install is needed)'
    }

    'extract' {
        $disc = (Get-Content -LiteralPath (Join-Path $Orig 'disc.txt') -TotalCount 1).Trim()
        [void](New-Item -ItemType Directory -Force -Path $Bin)
        & 7z.exe e -y "-o$Bin" $disc 'WEAAUTHR.DLL' 'DATA\ONLINE.SAM' 'DATA\ONLINEOV.SAM' | Out-Null
        if (-not (Test-Path -LiteralPath $Dll)) { Fail 'WEAAUTHR.DLL did not come out of the disc' }
        Write-Host "WEAAUTHR.DLL: $((Get-Item -LiteralPath $Dll).Length) bytes, md5 $((Get-FileHash -LiteralPath $Dll -Algorithm MD5).Hash.ToLowerInvariant())"
        $sam = Join-Path $Bin 'ONLINE.SAM'
        if (Test-Path -LiteralPath $sam) {
            Write-Host 'the disc Online.sam login lines:'
            Get-Content -LiteralPath $sam | Where-Object { $_ -match 'LoginServer|LoginPort' } | ForEach-Object { Write-Host "  $($_.Trim())" }
        }
        $ov = Join-Path $Bin 'ONLINEOV.SAM'
        if (Test-Path -LiteralPath $ov) {
            $set = @(Get-Content -LiteralPath $ov | Where-Object { $_.Trim() -and -not $_.Trim().StartsWith('#') })
            Write-Host "the disc onlineoverride.sam sets $($set.Count) lines"
        }
    }

    # The players' way to point the game at our server: the game's own server name, mapped to it in the hosts file.
    'hosts' {
        $hosts = Join-Path $env:SystemRoot 'System32\drivers\etc\hosts'
        Add-Content -LiteralPath $hosts -Value "`r`n$Server $GameHost" -Encoding ASCII
        & ipconfig.exe /flushdns | Out-Null
        $r = [System.Net.Dns]::GetHostAddresses($GameHost) | ForEach-Object { $_.ToString() }
        Write-Host "$GameHost resolves to: $($r -join ', ')"
        if ($r -notcontains $Server) { Fail "$GameHost does not resolve to the server" }
    }

    'build' {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        $vs = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
        if (-not $vs) { Fail 'no Visual Studio with the C++ tools' }
        $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvarsall.bat'
        $tool = Split-Path -Parent $Exe
        [void](New-Item -ItemType Directory -Force -Path $tool)
        $cmd = Join-Path $tool 'build.cmd'
        $text = "@echo off`r`ncall `"$vcvars`" x86 >nul || exit /b 1`r`ncd /d `"$tool`"`r`n" +
            "cl /nologo /EHsc /O1 /MT /W3 /Fe:origpc_auth.exe `"$(Join-Path $Here 'origpc_auth.cpp')`" ws2_32.lib || exit /b 1`r`n"
        [System.IO.File]::WriteAllText($cmd, $text, (New-Object System.Text.ASCIIEncoding))
        & cmd.exe /c $cmd
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $Exe)) { Fail 'the harness did not build' }
        Write-Host "built $Exe"
    }

    # What the server does with a plain connection: it is expected to send nothing until it has a request.
    'probe' {
        $t = Test-NetConnection -ComputerName $Server -Port 7598 -WarningAction SilentlyContinue
        Write-Host "Test-NetConnection $Server 7598: TcpTestSucceeded = $($t.TcpTestSucceeded)"
        $c = New-Object System.Net.Sockets.TcpClient
        try {
            $c.Connect($Server, 7598)
            $s = $c.GetStream()
            $start = Get-Date
            $n = 0
            while (((Get-Date) - $start).TotalSeconds -lt 10) {
                if ($s.DataAvailable) { $buf = New-Object byte[] 64; $n += $s.Read($buf, 0, 64) }
                Start-Sleep -Milliseconds 100
            }
            $line = "a plain connection to $Server`:7598 got $n bytes from the server in 10 s without sending anything"
        } catch { $line = "a plain connection to $Server`:7598 failed: $_" } finally { $c.Close() }
        Write-Host $line
        Write-Text (Join-Path $Out 'probe.txt') "$line`nTcpTestSucceeded $($t.TcpTestSucceeded)`nlogical processors $([Environment]::ProcessorCount)`n"
    }

    'run' { Invoke-Run $Name }

    # The race check: the login on all CPUs and pinned to one, three times each, one run with a second attempt (a second
    # Accept), and the stand-in server both ways. Each run is its own process, so each one's first socket is a cold one.
    'series' {
        foreach ($n in 'real-ip-all-1', 'real-ip-cpu1-1', 'real-ip-all-2', 'real-ip-cpu1-2', 'real-ip-all-3', 'real-ip-cpu1-3',
            'real-ip-retry', 'mock-all', 'mock-cpu1') { Invoke-Run $n }
        Write-Host '---- the series in short'
        Get-Content -LiteralPath (Join-Path $Out 'summary.txt') | Where-Object { $_ -match '^\S|SUMMARY' } | ForEach-Object { Write-Host $_ }
    }

    # The real game, as far as it gets on this machine: the disc mounted and its TP.EXE started, screenshots, then stopped.
    'trygame' {
        $disc = (Get-Content -LiteralPath (Join-Path $Orig 'disc.txt') -TotalCount 1).Trim()
        $iso = $disc
        if ([System.IO.Path]::GetExtension($disc) -ne '.iso') {
            $iso = [System.IO.Path]::ChangeExtension($disc, '.iso')
            Move-Item -LiteralPath $disc -Destination $iso -Force
            Write-Text (Join-Path $Orig 'disc.txt') $iso
        }
        $m = Mount-DiskImage -ImagePath $iso -PassThru
        $letter = ($m | Get-Volume).DriveLetter
        if (-not $letter) { Fail 'the disc did not mount' }
        Write-Host "mounted on $letter`:"
        $exe = "$letter`:\TP.EXE"
        Save-Screen (Join-Path $Out 'game-0-before.png')
        # The game's (and SafeDisc's) OutputDebugString text, the way DebugView reads it.
        $dbw = Start-Process -FilePath $Exe -ArgumentList @('--dbwin', '40', '--log', (Join-Path $Out 'dbwin-game.txt')) -PassThru -NoNewWindow `
            -RedirectStandardOutput (Join-Path $Temp 'dbwin-stdout.txt')
        Start-Sleep -Seconds 1
        $g = $null
        try { $g = Start-Process -FilePath $exe -WorkingDirectory "$letter`:\" -PassThru -ErrorAction Stop; $null = $g.Handle }
        catch { Write-Host "TP.EXE did not start: $_" }
        # Only TP.EXE and what it started (by parent) are looked at and stopped: no other process on the runner.
        function Get-GameTree {
            if ($null -eq $g) { return @() }
            $all = @(Get-CimInstance Win32_Process)
            $ids = @($g.Id)
            do {
                $n = $ids.Count
                $ids = @($ids + @($all | Where-Object { $ids -contains $_.ParentProcessId } | ForEach-Object { $_.ProcessId }) | Select-Object -Unique)
            } while ($ids.Count -ne $n)
            return @($all | Where-Object { $ids -contains $_.ProcessId })
        }
        $at = 0
        foreach ($t in 5, 15, 30) {
            Start-Sleep -Seconds ($t - $at)
            $at = $t
            Save-Screen (Join-Path $Out "game-1-at-${t}s.png")
            $tree = @(Get-GameTree)
            $state = '-'
            if ($null -ne $g) { $state = "exited: $($g.HasExited)"; if ($g.HasExited) { $state += " (code $($g.ExitCode))" } }
            Write-Host "at $t s: TP.EXE $state; its processes: $(($tree | ForEach-Object { "$($_.Name)[$($_.ProcessId)]" }) -join ', ')"
            foreach ($q in $tree) {
                $w = Get-Process -Id $q.ProcessId -ErrorAction SilentlyContinue
                if ($w -and $w.MainWindowTitle) { Write-Host "  window of $($q.Name): '$($w.MainWindowTitle)'" }
            }
        }
        $ev = @(Get-WinEvent -FilterHashtable @{ LogName = 'System'; StartTime = (Get-Date).AddMinutes(-3) } -ErrorAction SilentlyContinue |
            Where-Object { $_.Message -match 'secdrv|SafeDisc|driver' } | Select-Object -First 5)
        $ev | ForEach-Object { Write-Host "  system event $($_.Id): $($_.Message.Split("`n")[0])" }
        $sd = Get-Service -Name secdrv -ErrorAction SilentlyContinue
        Write-Host "secdrv service: $(if ($sd) { $sd.Status } else { 'not installed' })"
        foreach ($q in @(Get-GameTree)) { Write-Host "stopping $($q.Name)[$($q.ProcessId)]"; Stop-Process -Id $q.ProcessId -Force -ErrorAction SilentlyContinue }
        Save-Screen (Join-Path $Out 'game-2-after.png')
        if (-not $dbw.WaitForExit(20000)) { Stop-Process -Id $dbw.Id -Force -ErrorAction SilentlyContinue }
        Get-Content -LiteralPath (Join-Path $Out 'dbwin-game.txt') -ErrorAction SilentlyContinue | Select-Object -First 40 | ForEach-Object { Write-Host "  $_" }
        Dismount-DiskImage -ImagePath $iso | Out-Null
    }

    # Keeps the runner (and nothing else) waiting, for a look at the server while it stands.
    'hold' {
        $min = 0
        if ($env:KEEP_OPEN_MINUTES) { $min = [int]$env:KEEP_OPEN_MINUTES }
        if ($min -gt 0) { Write-Host "holding the runner for $min minutes"; Start-Sleep -Seconds ($min * 60) }
    }

    'clean' {
        if (Test-Path -LiteralPath (Join-Path $Orig 'disc.txt')) {
            $disc = (Get-Content -LiteralPath (Join-Path $Orig 'disc.txt') -TotalCount 1).Trim()
            Dismount-DiskImage -ImagePath $disc -ErrorAction SilentlyContinue | Out-Null
        }
        Remove-Item -LiteralPath $Orig -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $Orig) { Fail "$Orig is still there" }
        $bad = @(Get-ChildItem -LiteralPath $Out -Recurse -File | Where-Object { $_.Extension -notmatch '^\.(txt|png)$' })
        if ($bad.Count) { $bad | Remove-Item -Force; Fail 'a file other than text or a screenshot was in the artifact folder' }
        Write-Host 'the game files are deleted'
        # The artifact is public: the account name and password must not be in it (the harness never writes them; this checks).
        foreach ($secret in @($env:ORIGPC_NAME, $env:ORIGPC_PASS)) {
            if (-not $secret -or $secret.Length -lt 3) { continue }
            foreach ($f in @(Get-ChildItem -LiteralPath $Out -Recurse -File -Filter '*.txt')) {
                $text = [System.IO.File]::ReadAllText($f.FullName)
                if ($text.Contains($secret)) {
                    Write-Text $f.FullName ($text.Replace($secret, '<redacted>'))
                    Write-Host "::warning::a secret was found in $($f.Name) and is now replaced"
                }
            }
        }
    }

    default { Fail "unknown step '$Step'" }
}
exit 0
