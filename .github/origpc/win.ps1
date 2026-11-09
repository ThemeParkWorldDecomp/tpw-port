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

# The scripted input for the real game (the runner is a throwaway VM): move the pointer and click, press a key, type. The game's
# window lands at a different place each run, so clicks are given in its own 640x480 coordinates and placed from its client area
# at the time. Only the dummy account is ever typed; no real password is sent to the field, so a screenshot can never show one.
$script:InputReady = $false
function Ensure-Input {
    if ($script:InputReady) { return }
    Add-Type -Namespace Origpc -Name Input -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, System.UIntPtr e);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, System.UIntPtr e);
public static void Click(int x, int y) {
    SetCursorPos(x, y); System.Threading.Thread.Sleep(90);
    mouse_event(0x0002, 0, 0, 0, System.UIntPtr.Zero); System.Threading.Thread.Sleep(50);
    mouse_event(0x0004, 0, 0, 0, System.UIntPtr.Zero);
}
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)] public struct PT { public int X; public int Y; }
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)] public struct RC { public int L; public int T; public int R; public int B; }
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool ClientToScreen(System.IntPtr h, ref PT p);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool GetClientRect(System.IntPtr h, out RC r);
public static string Client(System.IntPtr h) {
    PT p = new PT(); RC r;
    if (!GetClientRect(h, out r) || !ClientToScreen(h, ref p)) { return "client area unknown"; }
    return "client origin " + p.X + "," + p.Y + " size " + (r.R - r.L) + "x" + (r.B - r.T);
}
public static void Key(byte vk) {
    keybd_event(vk, 0, 0, System.UIntPtr.Zero); System.Threading.Thread.Sleep(50);
    keybd_event(vk, 0, 2, System.UIntPtr.Zero);
}
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr h);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool SetWindowPos(System.IntPtr h, System.IntPtr after, int x, int y, int cx, int cy, uint f);
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern short VkKeyScan(char c);
// Run 4: the runner's own console window sat over the game, so the screenshots missed it and the keys went to the console.
// The game window is put on top (topmost) and given the keyboard. A key press of our own first (F24, which nothing uses; not Alt,
// which would open the window's menu) lets this background process do that.
[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
public static void Focus(System.IntPtr h) {
    SetWindowPos(h, new System.IntPtr(-1), 0, 0, 0, 0, 0x0043);
    if (GetForegroundWindow() == h) { return; }
    Key(0x87);
    SetForegroundWindow(h); System.Threading.Thread.Sleep(100);
}
// A point given in the game's own 640x480 coordinates, clicked wherever the window's client area is now and however it is scaled.
public static string ClickGame(System.IntPtr h, int x, int y) {
    PT p = new PT(); RC r;
    if (!GetClientRect(h, out r) || !ClientToScreen(h, ref p) || r.R <= 0 || r.B <= 0) { return "client area unknown, no click"; }
    int sx = p.X + x * r.R / 640, sy = p.Y + y * r.B / 480;
    Focus(h); Click(sx, sy);
    return "clicked game " + x + "," + y + " at screen " + sx + "," + sy;
}
public static void Type(string s) {
    foreach (char c in s) {
        short v = VkKeyScan(c);
        if (v == -1) { continue; }
        bool shift = (v & 0x100) != 0;
        if (shift) { keybd_event(0x10, 0, 0, System.UIntPtr.Zero); }
        Key((byte)(v & 0xff));
        if (shift) { keybd_event(0x10, 0, 2, System.UIntPtr.Zero); }
        System.Threading.Thread.Sleep(60);
    }
}
'@
    $script:InputReady = $true
}

# One real-game run. TAG names its files; AFFINITY, when nonzero, pins the process (and so its comms thread) to those CPUs.
# The game's own OutputDebugString (its comms thread's Started/Ended, and any SafeDisc text) is read by the harness in --dbwin
# mode; netstat on 7598 is polled, masked, while it runs. The timeline presses Escape to skip the intro movies, takes
# screenshots, and (once the online button's position is known from a screenshot) clicks through to the login and Accept with
# the dummy account. A first run that only reaches the title screen and posts screenshots is enough; keep_open_minutes gives
# time to read them and fill in the positions.
# Every TP.EXE from the copied game (C:\TPW): only the game this script started, never another process.
function Get-GameCopies { @(Get-CimInstance Win32_Process -Filter "Name = 'TP.exe'" -ErrorAction SilentlyContinue | Where-Object { $_.ExecutablePath -like 'C:\TPW\*' }) }

# SHIM is the compatibility layer list for this child process only. It returns how many seconds the game's window was seen alive
# (0 if no window ever came up).
function Invoke-Game([string]$Tag, [int]$Affinity, [string]$Game, [string[]]$GameArgs = @(), [string]$Shim = 'DWM8And16BitMitigation 16BitColor') {
    Write-Host "==== game run $Tag (affinity $Affinity; arguments '$($GameArgs -join ' ')'; layers '$Shim')"
    Ensure-Input
    # Run 3: a crashed game stayed listed for a minute (Windows Error Reporting), and the next run's TP.EXE ended at once with
    # no window. Wait for the last one to be gone first.
    $deadline = (Get-Date).AddSeconds(90)
    while (@(Get-GameCopies).Count -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2 }
    $log = Join-Path $Out "game-$Tag.txt"
    Write-Text $log "game run $Tag at $(Get-Date -Format o); affinity $Affinity; arguments '$($GameArgs -join ' ')'; cwd $Game`n"
    $left = @(Get-GameCopies)
    if ($left.Count) { Add-Text $log "an earlier TP.EXE is still there: $(($left | ForEach-Object { $_.ProcessId }) -join ', ')`n" }
    $gameExe = Join-Path $Game 'TP.EXE'
    $dbwLog = Join-Path $Out "dbwin-$Tag.txt"
    # The dbwin reader is the harness (module-level $Exe), not the game.
    $dbw = Start-Process -FilePath $Exe -ArgumentList @('--dbwin', '80', '--log', $dbwLog) -PassThru -NoNewWindow `
        -RedirectStandardOutput (Join-Path $Temp "dbwin-$Tag-stdout.txt")
    Start-Sleep -Milliseconds 700
    # Run 1 (no shims): both windowed runs crashed (0xc0000005) right after the game logged the desktop's 32-bit pixel format,
    # and fullscreen exited with code 1. A 1999 DirectDraw game expects a 16-bit display, so it runs under Windows' own
    # compatibility layers for that (reduced colour, and DirectDraw's 8/16-bit mitigation), set only for this child process.
    $run = Get-Date
    # A crash writes a minidump on the runner only (never in the artifact); its stack, as addresses, goes in the game log.
    $dumps = Join-Path $Temp 'tpdumps'
    New-Item -ItemType Directory -Force -Path $dumps | Out-Null
    $wer = 'HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\TP.EXE'
    New-Item -Path $wer -Force | Out-Null
    New-ItemProperty -Path $wer -Name DumpFolder -Value $dumps -PropertyType ExpandString -Force | Out-Null
    New-ItemProperty -Path $wer -Name DumpType -Value 1 -PropertyType DWord -Force | Out-Null
    New-ItemProperty -Path $wer -Name DumpCount -Value 5 -PropertyType DWord -Force | Out-Null
    $env:__COMPAT_LAYER = $Shim
    Add-Text $log "compatibility layers: '$Shim'; ddraw.dll beside TP.EXE: $(Test-Path -LiteralPath (Join-Path $Game 'ddraw.dll'))`n"
    $g = $null
    $sp = @{ FilePath = $gameExe; WorkingDirectory = $Game; PassThru = $true; ErrorAction = 'Stop' }
    if ($GameArgs.Count) { $sp.ArgumentList = $GameArgs }
    try { $g = Start-Process @sp; $null = $g.Handle }
    catch { Add-Text $log "TP.EXE did not start: $_`n"; Write-Host "TP.EXE did not start: $_" }
    Remove-Item Env:\__COMPAT_LAYER -ErrorAction SilentlyContinue
    if ($g -and $Affinity -gt 0) {
        try { $g.ProcessorAffinity = [IntPtr]$Affinity; Add-Text $log ("affinity set to 0x{0:x}`n" -f $Affinity) }
        catch { Add-Text $log "could not set affinity: $_`n" }
    }
    # Only TP.EXE and what it starts (by parent) are watched and stopped: no other process on the runner.
    $watch = $g
    function Get-GameTree {
        if ($null -eq $watch) { return @() }
        $all = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue)
        $ids = @($watch.Id)
        do {
            $n = $ids.Count
            $ids = @($ids + @($all | Where-Object { $ids -contains $_.ParentProcessId } | ForEach-Object { $_.ProcessId }) | Select-Object -Unique)
        } while ($ids.Count -ne $n)
        return @($all | Where-Object { $ids -contains $_.ProcessId })
    }
    # time (s) -> what to do. Keys skip the EA and intro movies (Escape is virtual-key 0x1B, Return 0x0D). The clicks are in the
    # game's own 640x480 coordinates (the JP front end is navigated by position) and are filled in from the screenshots; the
    # typed text is only ever the dummy account (origpc_auth.cpp's defaults), never a secret.
    $keyAt = @{ 3 = 0x1B; 5 = 0x1B; 8 = 0x1B; 11 = 0x0D }
    $clickAt = @{}
    $typeAt = @{}
    $shotAt = @(4, 6, 8, 10, 12, 14, 16, 18, 20, 23, 26, 30, 35, 40, 45, 50, 55, 60, 70, 80, 90)
    $net = Join-Path $Out "game-netstat-$Tag.txt"
    $lastNet = ''
    $sawWindow = $false
    $alive = 0
    $hwnd = [IntPtr]::Zero
    $start = Get-Date
    for ($t = 1; $t -le 92; $t++) {
        Start-Sleep -Seconds 1
        $elapsed = [int]((Get-Date) - $start).TotalSeconds
        if ($hwnd -eq [IntPtr]::Zero -and $null -ne $g -and -not $g.HasExited) {
            $g.Refresh()
            if ($g.MainWindowHandle -ne [IntPtr]::Zero) { $hwnd = $g.MainWindowHandle; Add-Text $log "t${t}s game window found`n" }
        }
        if ($hwnd -ne [IntPtr]::Zero -and ($keyAt.ContainsKey($t) -or $shotAt -contains $t)) { try { [Origpc.Input]::Focus($hwnd) } catch {} }
        if ($keyAt.ContainsKey($t)) {
            try { [Origpc.Input]::Key([byte]$keyAt[$t]); Add-Text $log "t${t}s key 0x$('{0:x}' -f $keyAt[$t])`n" } catch {}
        }
        if ($clickAt.ContainsKey($t) -and $hwnd -ne [IntPtr]::Zero) {
            try { Add-Text $log "t${t}s $([Origpc.Input]::ClickGame($hwnd, $clickAt[$t][0], $clickAt[$t][1]))`n" } catch {}
        }
        if ($typeAt.ContainsKey($t)) {
            try { [Origpc.Input]::Type([string]$typeAt[$t]); Add-Text $log "t${t}s typed $(([string]$typeAt[$t]).Length) characters`n" } catch {}
        }
        if ($shotAt -contains $t) { Save-Screen (Join-Path $Out "game-$Tag-${t}s.png") }
        $lines = @(Get-PortLines 7598 | ForEach-Object { Hide-Addresses (($_.Trim() -split '\s+') -join ' ') })
        $now = $lines -join "`n"
        if ($now -ne $lastNet) {
            Add-Text $net ("---- {0,4}s`n{1}`n" -f $elapsed, $now)
            if ($now) { Write-Host "t${t}s netstat 7598:"; $lines | ForEach-Object { Write-Host "  $_" } }
            $lastNet = $now
        }
        if ($t % 7 -eq 0 -or $shotAt -contains $t) {
            $tree = @(Get-GameTree)
            foreach ($q in $tree) {
                $w = Get-Process -Id $q.ProcessId -ErrorAction SilentlyContinue
                if ($w -and $w.MainWindowTitle) {
                    $sawWindow = $true
                    $alive = $t
                    $titleLine = "t${t}s window of $($q.Name): '$($w.MainWindowTitle)', $([Origpc.Input]::Client($w.MainWindowHandle))"
                    Add-Text $log "$titleLine`n"; Write-Host "  $titleLine"
                }
            }
            $state = if ($null -ne $g -and $g.HasExited) { "exited code $($g.ExitCode)" } else { 'running' }
            Add-Text $log "t${t}s TP.EXE $state; tree: $(($tree | ForEach-Object { "$($_.Name)[$($_.ProcessId)]" }) -join ', ')`n"
            if ($null -ne $g -and $g.HasExited -and $tree.Count -eq 0) { Write-Host "  TP.EXE $state, nothing left; ending this run"; break }
        }
    }
    Save-Screen (Join-Path $Out "game-$Tag-final.png")
    $exitInfo = if ($null -eq $g) { 'never started' } elseif ($g.HasExited) { "exited code $($g.ExitCode) (0x$('{0:x}' -f $g.ExitCode))" } else { 'still running' }
    Add-Text $log "result: TP.EXE $exitInfo; a window was seen: $sawWindow`n"
    Write-Host "game run $Tag`: TP.EXE $exitInfo; window seen: $sawWindow"
    # A crash's faulting module, offset and exception code, from Windows Error Reporting's Application log entry.
    Start-Sleep -Seconds 2
    $crash = @(Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = 1000, 1001, 1005; StartTime = $run } -ErrorAction SilentlyContinue |
        Where-Object { $_.Message -match 'TP\.EXE' } | Select-Object -First 3)
    foreach ($e in $crash) {
        $text = ($e.Message -split "`r?`n" | Where-Object { $_.Trim() } | Select-Object -First 12) -join ' | '
        Add-Text $log "event $($e.Id) $($e.ProviderName): $text`n"
        Write-Host "  event $($e.Id): $text"
    }
    Start-Sleep -Seconds 3
    $cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe'
    foreach ($dmp in @(Get-ChildItem -LiteralPath $dumps -Filter '*.dmp' -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -ge $run })) {
        if (Test-Path -LiteralPath $cdb) {
            $st = @(& $cdb -z $dmp.FullName -c '.ecxr; r; kv 40; lm; q' 2>&1 | ForEach-Object { "$_" } |
                Where-Object { $_ -match '^(eax|eip|cs|ChildEBP|[0-9a-f]{8} |\s*[0-9a-f]{2} [0-9a-f]{8} )' -or $_ -match '!|\+0x' })
            Add-Text $log "crash stack from $($dmp.Name):`n$(($st | Select-Object -First 90) -join "`n")`n"
            Write-Host "  crash stack:"; $st | Select-Object -First 40 | ForEach-Object { Write-Host "    $_" }
        } else { Add-Text $log "minidump written but cdb.exe is not on this runner`n" }
        Remove-Item -LiteralPath $dmp.FullName -Force -ErrorAction SilentlyContinue
    }
    foreach ($q in @(Get-GameTree)) { Stop-Process -Id $q.ProcessId -Force -ErrorAction SilentlyContinue }
    foreach ($q in @(Get-GameCopies)) { Wait-Process -Id $q.ProcessId -Timeout 60 -ErrorAction SilentlyContinue }
    if (-not $dbw.WaitForExit(25000)) { Stop-Process -Id $dbw.Id -Force -ErrorAction SilentlyContinue }
    $dbt = @(Get-Content -LiteralPath $dbwLog -ErrorAction SilentlyContinue)
    $comms = @($dbt | Where-Object { $_ -match 'commsThread|Started|Ended|WSAE|login|Login|connect|Connect' })
    if ($comms.Count) { Write-Host "  dbwin lines of note:"; $comms | Select-Object -First 30 | ForEach-Object { Write-Host "    $_" } }
    Add-Text (Join-Path $Out 'summary.txt') "game $Tag`: $exitInfo; window seen $sawWindow, last at ${alive}s; dbwin lines of note: $($comms.Count)`n"
    return $alive
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

    # The real game, as far as it gets on this machine. An unprotected pressing (no TP.ICD, e.g. the JP EA Best Selections disc)
    # is copied to a writable folder and its TP.EXE is run for real: windowed 640x480 software rendering (dialog.tcf), the CD
    # kept mounted for the disc check (volume label TPWORLD), the language forced to Japanese, screenshots taken, input scripted,
    # the comms thread and netstat watched, and a second run pinned to one CPU. A SafeDisc disc (TP.ICD present) cannot run on a
    # modern Windows, so it is only noted; the login library harness (the series step) covers it. No game file is kept.
    'trygame' {
        $disc = (Get-Content -LiteralPath (Join-Path $Orig 'disc.txt') -TotalCount 1).Trim()
        $iso = $disc
        if ([System.IO.Path]::GetExtension($disc) -ne '.iso') {
            $iso = [System.IO.Path]::ChangeExtension($disc, '.iso')
            Move-Item -LiteralPath $disc -Destination $iso -Force
            Write-Text (Join-Path $Orig 'disc.txt') $iso
        }
        $m = Mount-DiskImage -ImagePath $iso -PassThru
        $vol = $m | Get-Volume
        $letter = $vol.DriveLetter
        if (-not $letter) { Fail 'the disc did not mount' }
        $label = $vol.FileSystemLabel
        Write-Host "mounted on $letter`: (volume label '$label')"
        $src = "$letter`:"
        $icd = Test-Path -LiteralPath "$src\TP.ICD"
        $summaryFile = Join-Path $Out 'trygame.txt'
        Write-Text $summaryFile "trygame at $(Get-Date -Format o)`nmounted $letter`: label '$label'; TP.ICD (SafeDisc) present: $icd`n"
        Write-Host "disc has TP.ICD (SafeDisc): $icd"

        if ($icd) {
            Add-Text $summaryFile "the disc is SafeDisc-protected, so TP.EXE cannot run on this Windows; the login library harness covers it`n"
            Save-Screen (Join-Path $Out 'game-safedisc-desktop.png')
            Dismount-DiskImage -ImagePath $iso | Out-Null
            break
        }

        # Unprotected pressing: copy the whole game to a writable folder (TP.EXE writes saves and dialog.tcf beside itself).
        $game = 'C:\TPW'
        Write-Host "copying the game to $game"
        & robocopy.exe $src $game /E /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
        if (-not (Test-Path -LiteralPath (Join-Path $game 'TP.EXE'))) { Dismount-DiskImage -ImagePath $iso | Out-Null; Fail 'TP.EXE was not copied to the runner' }
        Add-Text $summaryFile "copied $((Get-ChildItem -LiteralPath $game -Recurse -File | Measure-Object -Sum Length).Sum) bytes to $game`n"

        # Japanese: the game reads its language from the registry (default 0x809 English). 0x11 is Japanese. It is 32-bit, so the
        # real key on a 64-bit Windows is under WOW6432Node; the plain and HKCU paths are set too for safety.
        $rel = 'Bullfrog Productions Ltd\Theme Park World'
        foreach ($root in 'HKLM:\SOFTWARE\WOW6432Node', 'HKLM:\SOFTWARE', 'HKCU:\SOFTWARE') {
            $key = Join-Path $root $rel
            try {
                New-Item -Path $key -Force | Out-Null
                New-ItemProperty -Path $key -Name 'Language' -Value 0x11 -PropertyType DWord -Force | Out-Null
                Add-Text $summaryFile "registry $key Language=0x11`n"
            } catch { Add-Text $summaryFile "registry $key could not be set: $_`n" }
        }

        # dialog.tcf: a dev config the game reads from its working directory. 28 bytes of little-endian u32:
        #   version 2, gameOptions 0x000c0e14 (the default 0xc0e15 minus bit 0 FULLSCREEN, so windowed 640x480), dialogOptions
        #   0x00000840 (0x800 show the front end + 0x40 force the software renderer), 0, CURRENTTheme 1, 0, 0.
        $tcf = [byte[]](0x02,0x00,0x00,0x00, 0x14,0x0e,0x0c,0x00, 0x40,0x08,0x00,0x00, 0x00,0x00,0x00,0x00,
                        0x01,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00)
        [System.IO.File]::WriteAllBytes((Join-Path $game 'dialog.tcf'), $tcf)
        Add-Text $summaryFile "wrote dialog.tcf (windowed 640x480, software renderer, front end on)`n"

        # Sound: the workflow installs VB-CABLE before this step; the game needs the audio service and an output device.
        foreach ($svc in 'AudioEndpointBuilder', 'Audiosrv') {
            try { Set-Service -Name $svc -StartupType Automatic; Start-Service -Name $svc } catch { Add-Text $summaryFile "could not start $svc`: $_`n" }
            Add-Text $summaryFile "service $svc`: $((Get-Service -Name $svc -ErrorAction SilentlyContinue).Status)`n"
        }
        $snd = @(Get-CimInstance Win32_SoundDevice -ErrorAction SilentlyContinue | ForEach-Object { "$($_.Name) ($($_.Status))" })
        Add-Text $summaryFile "sound devices: $(if ($snd.Count) { $snd -join '; ' } else { 'none' })`n"
        Write-Host "sound devices: $($snd -join '; ')"

        Save-Screen (Join-Path $Out 'game-before.png')
        # Runs 3 and 4: the title screen came up, then at 8-14 s the process heap was found corrupt, in Windows' own DirectDraw
        # (its 16-bit emulation on this GPU-less machine: Surface IsLost -> reset of all DirectDraw objects -> heap allocation, with
        # d3d10warp freeing on another thread). -flmouse (no pointer thread) and one CPU did not change it. So these runs put
        # cnc-ddraw (a widely used open DirectDraw replacement for old games, drawn here with GDI) beside the unchanged TP.EXE.
        # It is fetched from its own release and its checksum checked; it is not a game file and is removed with the copy.
        $cncZip = Join-Path $Temp 'cnc-ddraw-7.1.0.0.zip'
        $cncSha = '0b13ab89a64c9918189b1dadd449ef6ed3cb3b7b19cabd96d8adbd95505bb908'
        & curl.exe --fail --location --retry 3 --retry-delay 10 --silent --show-error --output $cncZip `
            'https://github.com/FunkyFr3sh/cnc-ddraw/releases/download/v7.1.0.0/cnc-ddraw.zip'
        $cncOk = $false
        if ($LASTEXITCODE -eq 0 -and (Get-FileHash -LiteralPath $cncZip -Algorithm SHA256).Hash.ToLowerInvariant() -eq $cncSha) {
            & 7z.exe e -y "-o$game" $cncZip 'ddraw.dll' | Out-Null
            $cncOk = Test-Path -LiteralPath (Join-Path $game 'ddraw.dll')
        }
        Add-Text $summaryFile "cnc-ddraw 7.1.0.0 fetched, checked and placed: $cncOk`n"
        Write-Host "cnc-ddraw placed: $cncOk"
        # Its settings: in a window, GDI drawing (no GPU here), and no CPU pinning of its own (singlecpu would pin the whole game to
        # CPU 0 and so hide the race this test is about); nothing is saved back.
        $ini = "[ddraw]`r`nwindowed=true`r`nfullscreen=false`r`nborder=true`r`nrenderer=gdi`r`nsinglecpu=false`r`nsavesettings=0`r`n" +
            "no_compat_warning=true`r`n"
        [System.IO.File]::WriteAllText((Join-Path $game 'ddraw.ini'), $ini, (New-Object System.Text.ASCIIEncoding))
        # The game's own fullscreen mode (dialog.tcf gameOptions 0x000c0e15), which cnc-ddraw shows in a window.
        $tcfFull = [byte[]]$tcf.Clone(); $tcfFull[4] = 0x15
        $best = 'cnc'
        if ($cncOk) {
            $winAlive = [int]@(Invoke-Game 'cnc' 0 $game @() '16BitColor')[-1]
            [System.IO.File]::WriteAllBytes((Join-Path $game 'dialog.tcf'), $tcfFull)
            $fullAlive = [int]@(Invoke-Game 'cnc-full' 0 $game @() '16BitColor')[-1]
            # The pinned run uses whichever of the two lived longer.
            if ($winAlive -ge $fullAlive) { [System.IO.File]::WriteAllBytes((Join-Path $game 'dialog.tcf'), $tcf) } else { $best = 'cnc-full' }
            Add-Text $summaryFile "cnc-ddraw: windowed alive ${winAlive}s, fullscreen alive ${fullAlive}s; the pinned run uses $best`n"
            # The race check on the real game: the whole process (and so its comms thread) pinned to CPU 0.
            Invoke-Game "$best-cpu1" 1 $game @() '16BitColor' | Out-Null
        }
        Remove-Item -LiteralPath (Join-Path $game 'ddraw.dll'), (Join-Path $game 'ddraw.ini') -Force -ErrorAction SilentlyContinue
        [System.IO.File]::WriteAllBytes((Join-Path $game 'dialog.tcf'), $tcf)
        # Windows' own DirectDraw again, with full page heap on TP.EXE: the heap corruption then faults at the write that does it,
        # so the crash stack names the code (the game's or Windows') that overruns. The setting is removed right after.
        $ifeo = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\TP.EXE'
        New-Item -Path $ifeo -Force | Out-Null
        New-ItemProperty -Path $ifeo -Name GlobalFlag -Value 0x02000000 -PropertyType DWord -Force | Out-Null
        New-ItemProperty -Path $ifeo -Name PageHeapFlags -Value 3 -PropertyType DWord -Force | Out-Null
        Invoke-Game 'pageheap' 0 $game | Out-Null
        Remove-Item -Path $ifeo -Recurse -Force -ErrorAction SilentlyContinue

        $sd = Get-Service -Name secdrv -ErrorAction SilentlyContinue
        Add-Text $summaryFile "secdrv service: $(if ($sd) { $sd.Status } else { 'not installed' })`n"
        Save-Screen (Join-Path $Out 'game-after.png')
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
        # The copied real game (trygame): no game file is ever kept or uploaded.
        if (Test-Path -LiteralPath 'C:\TPW') {
            # Run 3: a crashed TP.EXE from the copy still held its files. Stop the copy's own processes, then retry.
            foreach ($q in @(Get-GameCopies)) { Stop-Process -Id $q.ProcessId -Force -ErrorAction SilentlyContinue; Wait-Process -Id $q.ProcessId -Timeout 60 -ErrorAction SilentlyContinue }
            for ($i = 0; $i -lt 6 -and (Test-Path -LiteralPath 'C:\TPW'); $i++) {
                Remove-Item -LiteralPath 'C:\TPW' -Recurse -Force -ErrorAction SilentlyContinue
                if (Test-Path -LiteralPath 'C:\TPW') { Start-Sleep -Seconds 10 }
            }
            if (Test-Path -LiteralPath 'C:\TPW') { Fail 'C:\TPW is still there' }
            Write-Host 'the copied real game (C:\TPW) is deleted'
        }
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
