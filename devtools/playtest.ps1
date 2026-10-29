# playtest.ps1: the local playtest on Windows (the Mac's is devtools/playtest.sh; read its header for the whole idea).
# Run from the repository's root, after build.cmd:
#   powershell -ExecutionPolicy Bypass -File devtools\playtest.ps1 [-Build build] [-Editions 'us eur gold'] [-Frames 3300]
#                                                                  [-Import PATH,PATH] [-Keep DIR]
# On your own copy of the retail discs' files (the US Mac CD, the European PC CD and the Gold bonus CD, in the launcher's data
# folder) it runs the verifier, which must show 100% for US, European and Gold or nothing is played, then plays each edition
# through the easy-park recipe (Gold from the launcher window's Play) and prints one result line per edition.
# The data folder is named by the environment variable TPW_PLAYTEST_DATA, or by the first line of the git-ignored file
# .playtest-data in the repository's root. The location is a local setting, never committed, never put in a pull request.
# With neither set the script says so and exits 0 having tested nothing: a skip is not a pass.
# The recipe is the Mac's halved, at 1024 x 768 (4:3), in a window on screen 0 (the same as the tested play of 2026-10-23
# on the Windows runners, whose only OpenGL had 1024-pixel textures). Exit code: 0 all passed (or skipped), 1 the verifier
# is below 100% or an edition failed, 2 bad options or no launcher.
param(
  [string]$Build = 'build',
  [string]$Editions = 'us eur gold',
  [int]$Frames = 3300,
  [string[]]$Import = @(),
  [string]$Keep = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$data = $env:TPW_PLAYTEST_DATA
$local = Join-Path $root '.playtest-data'
if (-not $data -and (Test-Path -LiteralPath $local)) {
  $data = Get-Content -LiteralPath $local | Where-Object { $_.Trim() -ne '' -and $_.Trim() -notlike '#*' } | Select-Object -First 1
}
if (-not $data) {
  Write-Output 'playtest: SKIPPED, nothing was tested. Set TPW_PLAYTEST_DATA (or put the path on the first line of the'
  Write-Output 'playtest: git-ignored file .playtest-data in the repository''s root) to the launcher''s data folder holding your'
  Write-Output 'playtest: own US, European and Gold game files. See CONTRIBUTING.md, "Before a pull request".'
  exit 0
}
if (-not (Test-Path -LiteralPath $data -PathType Container)) { Write-Error "playtest: the data folder does not exist: $data"; exit 2 }
if ($Frames -lt 2900) { Write-Error "playtest: -Frames $Frames ends the run before the Park Information panel (2900 or more)"; exit 2 }
$eds = -split $Editions
foreach ($e in $eds) { if ($e -notin 'us', 'eur', 'gold') { Write-Error "playtest: editions are us, eur and gold: $e"; exit 2 } }
$launcher = Join-Path $Build 'tpw_launcher.exe'
if (-not (Test-Path -LiteralPath $launcher)) { Write-Error "playtest: no launcher at $launcher; build it first (build.cmd), or name the build folder with -Build"; exit 2 }

$work = Join-Path ([System.IO.Path]::GetTempPath()) ('tpw-playtest-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force -Path $work | Out-Null
if ($Keep) { New-Item -ItemType Directory -Force -Path $Keep | Out-Null }

# Runs the launcher with the given arguments, output to two files; returns the exit code (-1 after the time limit).
function Invoke-Launcher([string[]]$LauncherArgs, [string]$Out, [string]$Err, [int]$Seconds) {
  $p = Start-Process -FilePath $launcher -ArgumentList $LauncherArgs -NoNewWindow -PassThru `
       -RedirectStandardOutput $Out -RedirectStandardError $Err
  $null = $p.Handle   # keeps the process handle, so ExitCode is there after the wait
  if (-not $p.WaitForExit($Seconds * 1000)) { $p.Kill(); return -1 }
  return $p.ExitCode
}

try {
  $version = (& $launcher --version 2>$null) -join ' '
  Write-Output "playtest: $version"

  # 1. The verifier: --import with the given paths, or with an empty folder (nothing to import), prints the status of each
  # edition in the data folder ("import: us: unlocked, 770 of 770 required files").
  $nothing = Join-Path $work 'nothing'
  New-Item -ItemType Directory -Force -Path $nothing | Out-Null
  $paths = if ($Import.Count -gt 0) { $Import } else { @($nothing) }
  $vOut = Join-Path $work 'verify-out.txt'; $vErr = Join-Path $work 'verify-err.txt'
  $null = Invoke-Launcher (@('--data', $data, '--import') + $paths) $vOut $vErr 1800
  $verify = @(Get-Content -LiteralPath $vOut, $vErr -ErrorAction SilentlyContinue)
  if ($Keep) { $verify | Set-Content -LiteralPath (Join-Path $Keep 'verify.txt') }
  $line = @(); $short = @()
  foreach ($pair in @(@('us', 'US'), @('eur', 'EUR'), @('gold', 'Gold'))) {
    $m = $verify | Select-String -Pattern ("import: $($pair[0]): [^,]*, (\d+) of (\d+) required files") | Select-Object -Last 1
    if ($m) {
      $have = [int]$m.Matches[0].Groups[1].Value; $need = [int]$m.Matches[0].Groups[2].Value
      $line += "$($pair[1]) $have/$need"
      if ($have -ne $need -or $need -eq 0) { $short += $pair[1] }
    } else { $line += "$($pair[1]) ?/?"; $short += $pair[1] }
  }
  if ($short.Count -gt 0) {
    Write-Output "playtest: verifier: $($line -join ' '), NOT 100% (not complete: $($short -join ' ')). Refusing to play."
    $verify | Select-String -Pattern 'import: (error|us:|eur:|gold:)' | ForEach-Object { Write-Output "playtest:   $($_.Line)" }
    exit 1
  }
  Write-Output "playtest: verifier: $($line -join ' '), 100% for US, European and Gold"

  # 2. The plays: the easy-park recipe at 1024 x 768 (lobby, new player, the first park's gate, the park's buttons, Park
  # Information; the clicks are at passes up to 2850).
  $recipe = '--click', '663,60@200', '--type', 'Tess@230', '--click', '709,283@300', '--click', '71,588@800',
            '--click', '130,707@2300', '--click', '173,596@2500', '--click', '252,176@2700', '--click', '598,100@2850'
  $env:TPW_VSYNC_EVERY_REFRESH = '1'   # the recipe's clicks are at pass numbers (platform/window.cpp)
  $fails = 0
  foreach ($e in $eds) {
    $label = @{ us = 'US'; eur = 'EUR'; gold = 'Gold' }[$e]
    $fdir = Join-Path $work "frames-$e"; New-Item -ItemType Directory -Force -Path $fdir | Out-Null
    $common = @('--data', $data, '--mute', '--no-movies', '--windowed', '--display', '0', '--aspect', '4:3', '--resolution', '1024x768')
    $tail = @('--frames', "$Frames", '--dump-frames', $fdir, '--dump-every', '150')
    # Gold: the launcher's window (no --play) and a click on the US tile's Play (window point 205,373 of its 800 x 560; the European tile's is 595,373),
    # which starts the game with the Gold rides switched on when all three editions are ready.
    $mode = if ($e -eq 'gold') { @('--window-click', '205,373@30') } else { @('--play', $e) }
    $so = Join-Path $work "play-$e-out.txt"; $se = Join-Path $work "play-$e-err.txt"
    $start = Get-Date
    $rc = Invoke-Launcher ($common + $mode + $recipe + $tail) $so $se 900
    $secs = [int]((Get-Date) - $start).TotalSeconds
    # The launcher's output and the data folder's run logs written since this run started.
    $texts = @($so, $se) + @(Get-ChildItem -LiteralPath (Join-Path $data 'logs') -Filter *.log -ErrorAction SilentlyContinue |
                             Where-Object { $_.LastWriteTime -ge $start } | ForEach-Object { $_.FullName })
    $all = @(Get-Content -LiteralPath $texts -ErrorAction SilentlyContinue)
    $why = @()
    if ($rc -eq -1) { $why += 'force-quit at the time limit' } elseif ($rc -ne 0) { $why += "exit $rc" }
    $stand = $all | Select-String -Pattern 'launcher: (0 of \d+) stand-ins were reached' | Select-Object -Last 1
    if (-not $stand) { $why += 'no "0 of N stand-ins were reached" line' }
    $n = @(Get-ChildItem -LiteralPath $fdir -Filter *.bmp -ErrorAction SilentlyContinue).Count
    if ($n -eq 0) { $why += 'no frames' }
    if ($e -eq 'gold') {
      if (-not ($all | Select-String -SimpleMatch 'play: Sim Theme Park, the US edition' -Quiet)) {
        $why += 'the game that started was not the US edition (the click missed the US tile''s Play?)'
      }
      if (-not ($all | Select-String -Pattern 'Gold rides: on, \d+ rides overlaid from assets/gold$' -Quiet)) { $why += 'no whole "Gold rides: on" line' }
    }
    if ($why.Count -eq 0) {
      Write-Output "playtest: ${label}: PASS (exit 0, $($stand.Matches[0].Groups[1].Value) stand-ins, $n frames, $secs s)"
    } else {
      Write-Output "playtest: ${label}: FAIL ($($why -join '; '))"
      $all | Select-Object -Last 8 | ForEach-Object { Write-Output "playtest:   $_" }
      $fails++
    }
    if ($Keep) {
      $dest = Join-Path $Keep $e; New-Item -ItemType Directory -Force -Path $dest | Out-Null
      Copy-Item -Path (Join-Path $fdir '*.bmp') -Destination $dest -ErrorAction SilentlyContinue
      $all | Set-Content -LiteralPath (Join-Path $dest 'output.txt')
    }
  }
  if ($fails -eq 0) { Write-Output 'playtest: all passed'; exit 0 }
  Write-Output "playtest: $fails edition(s) failed"; exit 1
} finally {
  Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
