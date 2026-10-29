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
#   tools\MinGit\    a portable Git (MinGit), fetched here only when the PC has no Git on its PATH (see SAFETY)
#   bin\themepark.cmd the themepark command: starts build\tpw_launcher.exe from any folder, and themepark update runs
#                    this script from source\ (see THE THEMEPARK COMMAND)
#   icon.ico         the launcher's icon, written by build\tpw_launcher.exe --write-icon at every install and switch,
#                    for the Start menu shortcut (see THE START MENU SHORTCUT)
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
#   RESULT ok VERSION             the last line when it worked, and the exit code is 0 (VERSION: 0.1.0, 0.2.0-dev)
#   RESULT fail SOME REASON       the last line when it did not, and the exit code is not 0
# Exactly one RESULT line is printed, last. Other lines are for people and may change. Options are only ever added.
# Run through iex (no file) the script never calls exit, which would close the PowerShell window; it sets
# $LASTEXITCODE instead.
#
# These are the lines of plain mode, which is what the updater gets (the output is a pipe). Run in a console the script
# shows the same run as a progress view instead (see "output in a console" below), and -Plain asks for the lines in a
# console too. The build's whole output goes to DIR\last-build.log either way; plain mode shows it as well.
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
# DIR\bin\themepark.cmd starts DIR\build\tpw_launcher.exe (where the Update button is) and returns at once. Three
# arguments are different: `themepark update` runs this script from DIR\source in the foreground (-Update -Dir DIR, any
# further options passed on, so `themepark update -Dev` follows main), with the progress view in the window, and first
# refuses when a launcher is running (the Update button updates a running launcher; this run would fail at the switch
# and would start a second one); `themepark --version` and `themepark --help` run the launcher in the foreground through
# a pipe (it has no console of its own), so that its answer shows in the window. DIR\bin is
# added to the user's Path (HKCU\Environment, no administrator rights), once, and Windows is told so that new windows
# see it. Through irm | iex the script runs in the player's own window, so DIR\bin goes on that window's Path too and
# themepark works there at once. At the end a new PowerShell, with the Path a new window gets, checks that it finds
# themepark, and if it does not the player is given the one line that fixes it (the install has still succeeded). Only
# an install (not -Update or -SwapAndLaunch) changes the Path: an update never brings back an entry the player removed.
# A themepark command this script did not make is never shadowed: the Path is then left as it was and the player is
# told the full path instead.
#
# THE START MENU SHORTCUT. Players also open the game from an icon, as on a Mac (install.sh puts STP/TPW Launcher in
# Applications). Every install, update and switch (not -Stage) writes the shortcut "STP-TPW Launcher" into the user's
# own Programs folder of the Start menu (%APPDATA%\Microsoft\Windows\Start Menu\Programs, where [Environment]::
# GetFolderPath('Programs') points; no administrator rights, nothing for all users), so the Start menu and its search
# have it. It points at DIR\build\tpw_launcher.exe, started in DIR\build, with the launcher's icon: first the launcher
# writes DIR\icon.ico (--write-icon: the game's own icon once a game is imported, the launcher's stand-in before) and
# the shortcut shows that file; when the launcher cannot write it (a stand-in launcher of a test build), the shortcut
# shows the program's own icon. Between installs the launcher keeps DIR\icon.ico up to date itself (as the Mac's
# application: launcher/installed_icon.h), so the shortcut shows the game's icon as soon as a game is imported, as the
# taskbar does while the launcher runs. A shortcut of that name that points at some other program is left alone and
# the player is told. The shortcut not being written never fails the run (the launcher is in place; the player is
# told). There is no uninstall path: the player deletes the shortcut from the Start menu, and it comes back at the next
# install or update. The step is "Adding STP/TPW Launcher to the Start menu", after the command and before the start;
# the progress view's row is startmenu. There are no application bundles on Windows: the game plays in the launcher's
# own process, and nothing answers the two bundles the Mac launcher writes inside its build folder (Sim Theme Park.app
# and Theme Park World.app; install.sh, THE APPLICATION), so the running check and Remove-Own have nothing more to see.
#
# SAFETY. No administrator rights, nothing written outside DIR but that Path entry and the Start menu shortcut (one
# file, STP-TPW Launcher.lnk, in the user's own Programs folder, THE START MENU SHORTCUT above; the launcher is started
# at the end, from DIR\build). The only folders deleted are DIR\build-next, DIR\build-prev and DIR\tools\MinGit (with
# its .tmp), by Remove-Own below, which refuses any other name, an odd install folder and a link or junction. Nothing is
# installed on the PC: a Git on the PATH is used as it is, and when there is none a portable Git (MinGit, the zip of a
# pinned Git for Windows release, checked against its SHA-256 below) is fetched into DIR\tools\MinGit and run from
# there. No winget, no setup program, no registry key, no Path entry for it; it goes with DIR.
#
# Test hooks (for the maintainers' tests, not for players): TPW_INSTALL_REPO replaces the repository URL, and
# TPW_INSTALL_STUB=1 puts a stand-in launcher in place of the download and the build (the installer's CI tests the
# themepark command with it; Git is still looked for, and fetched when there is none, so the CI tests the fetch with
# it too), of version TPW_INSTALL_STUB_VERSION when set (a newer one stands in for a new release).
# TPW_INSTALL_VIEW=plain, lines or vt forces that view of the output, whatever the output is (the CI runs the progress
# view through a pipe with it). TPW_INSTALL_PROGRAMS names the folder the Start menu shortcut goes in, so that a test
# never writes the Start menu of the account it runs under.
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
    [switch]$Plain,
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
    # the RESULT line is out, what is being installed, and which git runs the git commands (Git: git.exe from the PATH,
    # or the fetched DIR\tools\MinGit\cmd\git.exe; GitFetched: this run fetched it).
    $St = @{ StepNo = 0; StepTotal = 0; Done = $false; Channel = ''; Tag = ''; Commit = ''; Version = ''; Git = 'git.exe'; GitFetched = $false; LogStarted = $false }
    # Every git command gets these: files kept with their line endings as stored (as the build expects), paths longer
    # than 260 characters allowed, and no credential window.
    $GitCfg = @('-c', 'core.autocrlf=false', '-c', 'core.longpaths=true', '-c', 'credential.helper=')
    # The portable Git fetched when the PC has none (Get-MinGit below): MinGit is Git for Windows without the GUI,
    # Perl and Bash (it has sh, which git submodule needs). The 64-bit one runs on Windows on Arm too, emulated, as the
    # build's tools do. To move it on: the newest release's MinGit-VERSION-64-bit.zip, with the size and the SHA-256
    # the release page shows for it.
    $MinGit = @{
        Version = '2.56.0.4'
        Url     = 'https://github.com/git-for-windows/git/releases/download/v2.56.0.windows.4/MinGit-2.56.0.4-64-bit.zip'
        Sha256  = 'e3bf78351fe777fba47ddee5950082f8a98bb0f02604fdc7ba3ee2cb68ffa877'
        Size    = '38 MB'
    }

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
        Write-Host '  -Plain           one line per step and the build''s whole output, no colour, no redrawing (what a file gets)'
        Write-Host '  -Help            this text'
        Write-Host ''
        Write-Host 'After installing, type themepark in any PowerShell window to play, from any folder.'
        Write-Host 'Run the same line again, or type themepark update, to update. Both follow the newest release unless -Dev is'
        Write-Host "added each time; so does the launcher's Update button, which never adds -Dev."
    }

    # ---- output: the lines the launcher reads (plain mode)
    #
    # Two ways of showing the same run. Plain mode ($Ui.Mode plain: the output is not a console, -Plain, NO_COLOR,
    # TERM=dumb or a window narrower than 60 columns) prints the lines below and the build's output as it comes: that is
    # what the updater, the CI and a log get, and it never changes. In a console the same run is one block that redraws
    # in place (the progress view further down): a row per stage, a bar for the build, the build's output only in the
    # log.

    function Step([string]$text) {
        $St.StepNo += 1
        if ($Ui.Mode -eq 'plain') { Write-Host "STEP $($St.StepNo)/$($St.StepTotal) $text" }
    }

    # A line for people. In the progress view it is kept and shown under the block at the end (a notice, or what went
    # wrong).
    function Say([string]$text) {
        if ($Ui.Mode -eq 'plain') { Write-Host $text } else { [void]$Ui.Notes.Add($text) }
    }

    # A line of plain mode only: the progress view shows the same thing in its rows.
    function Info([string]$text) {
        if ($Ui.Mode -eq 'plain') { Write-Host $text }
    }

    # Prints the one RESULT fail line (the progress view: what went wrong, under the block) and unwinds to the end of
    # the block.
    function Fail([string]$reason) {
        if ($Ui.Mode -eq 'plain') { Write-Host "RESULT fail $reason" } else { Show-UiFail $reason }
        $St.Done = $true
        throw 'tpw-install-failed'
    }

    function Finish-Ok([string]$version) {
        if ($Ui.Mode -eq 'plain') { Write-Host "RESULT ok $version" } else { Finish-Ui }
        $St.Done = $true
    }

    # A lost connection, said the same way wherever it shows up.
    function Fail-Network([string]$detail) {
        if ($detail) { Say $detail }
        Say 'The launcher in use was not changed.'
        Fail "Couldn't reach GitHub, try again later."
    }

    # ---- output in a console: the progress view
    #
    # One block, redrawn in place inside a frame of box-drawing characters (rounded corners, grey): a blank line, the
    # title with the version ("STP/TPW Launcher 0.1.0", as soon as the source's commit is known), a row per stage (a
    # green check when done, a spinner while it runs, grey while it waits, a yellow ! for a notice, a red x when it
    # failed), a blank line, a footer and a blank line, with two blank columns inside each side. The build's row says
    # what the build is doing until the compiler reports a percentage (fetching a tool, configuring the build,
    # starting the compiler, compiling the first file), then a bar with the percentage, the time left and the file
    # being compiled, read from build.ps1's output as it comes (ninja's "[12/345] Building CXX object ..." lines). The
    # cursor is hidden while the block redraws and shown again before anything else is printed. Rows are only ever
    # added; the names are those of the design note (tools, source, build, command, start; installed, newest, swap on
    # an update). Two console modes: vt (Windows Terminal, PowerShell 7, or the console host once SetConsoleMode
    # turned its escape codes on) and lines (a console host without them: every finished row on its own line in
    # colour, the running one in Write-Progress, nothing redrawn, no frame, ASCII glyphs; its title is printed once,
    # so the finished rows wait until the version is known and the title can name it). The width is read again at
    # every frame: the frame is one column narrower than the window and at most 110 wide; inside it, under 72
    # columns, the bar is 10 cells and the time left is dropped; under 60 columns there is no frame. After a resize
    # the block starts afresh below the old one rather than drawing over the wrong lines.

    $Ui = @{ Mode = 'plain'; Rows = (New-Object System.Collections.ArrayList); Lines = 0; Cols = 80; LastCols = 0
             Frame = 0; Pad = 20; Version = ''; Footer = ''; Closing = ''; Box = $false; In = 0; TW = 80
             Notes = (New-Object System.Collections.ArrayList); FailText = ''; FailDone = $false
             Start = (Get-Date); BuildStart = (Get-Date); BuildT0 = $null; BuildP0 = 0; Pct = $null; File = ''
             Stage = 0; Words = ''; Held = (New-Object System.Collections.ArrayList); Flush = $false
             Src = 'source'; Got = ''; LastDraw = [DateTime]::MinValue; Header = $false; Progress = ''
             G = @{ done = '[x]'; pending = '[ ]'; warn = '[!]'; fail = '[x]'; on = '#'; off = '.'
                    h = '-'; v = '|'; tl = '+'; tr = '+'; bl = '+'; br = '+' }
             Spin = @('[>]') }
    # The escape codes of vt mode (empty in the other modes, so that the same text serves all of them).
    $C = @{ Esc = [string][char]27; Rst = ''; Bold = ''; Dim = ''; Green = ''; Cyan = ''; Yellow = ''; Red = ''; Clr = '' }

    # The console host's escape codes (ENABLE_VIRTUAL_TERMINAL_PROCESSING), turned on with one SetConsoleMode call.
    # Windows Terminal and PowerShell 7 have them on already. False when the console refuses (an old Windows, or the
    # legacy console).
    function Enable-UiVt {
        try {
            if (-not ('TpwInstall.Con' -as [type])) {
                Add-Type -Namespace TpwInstall -Name Con -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError = true)] public static extern IntPtr GetStdHandle(int nStdHandle);
[DllImport("kernel32.dll", SetLastError = true)] public static extern bool GetConsoleMode(IntPtr hConsoleHandle, out uint lpMode);
[DllImport("kernel32.dll", SetLastError = true)] public static extern bool SetConsoleMode(IntPtr hConsoleHandle, uint dwMode);
'@
            }
            $handle = [TpwInstall.Con]::GetStdHandle(-11)
            $mode = [uint32]0
            if (-not [TpwInstall.Con]::GetConsoleMode($handle, [ref]$mode)) { return $false }
            if (($mode -band 4) -ne 0) { return $true }
            return [bool][TpwInstall.Con]::SetConsoleMode($handle, [uint32]($mode -bor 4))
        } catch { return $false }
    }

    # The console's width in columns: the window's, else 80.
    function Get-UiCols {
        $w = 0
        try { $w = [int]$Host.UI.RawUI.WindowSize.Width } catch {}
        if ($w -le 0) { try { $w = [int][Console]::WindowWidth } catch {} }
        if ($w -le 0) { $w = 80 }
        return $w
    }

    # The frame's width from the console's: one column narrower than the window, at most 110, with two columns of
    # padding inside each side (vt mode, 60 columns or more; otherwise no frame and the text has the whole width).
    # TW is the width the text may use.
    function Set-UiGeometry {
        if ($Ui.Mode -eq 'vt' -and $Ui.Cols -ge 60) {
            $outer = $Ui.Cols - 1
            if ($outer -gt 110) { $outer = 110 }
            $Ui.Box = $true
            $Ui.In = $outer - 2
            $Ui.TW = $Ui.In
        } else {
            $Ui.Box = $false
            $Ui.In = 0
            $Ui.TW = $Ui.Cols
        }
    }

    # The columns $text takes on the screen: its length without the escape codes.
    function Get-UiWidth([string]$text) {
        return ([regex]::Replace($text, '\x1b\[[0-9;?]*[A-Za-z]', '')).Length
    }

    # $text as one line of the frame: a grey bar each side, padded to the frame's inner width.
    function Format-UiBoxed([string]$text) {
        $w = Get-UiWidth $text
        $fill = ''
        if ($w -lt $Ui.In) { $fill = ' ' * ($Ui.In - $w) }
        return $C.Dim + $Ui.G.v + $C.Rst + $text + $fill + $C.Dim + $Ui.G.v + $C.Rst
    }

    # Chooses the view, once the options are read, and sets up the colours and glyphs of the console view.
    function Start-Ui {
        $Ui.Mode = 'plain'
        $want = "$env:TPW_INSTALL_VIEW"
        if ($Plain -or $want -eq 'plain') { return }
        if ($want -ne 'vt' -and $want -ne 'lines') {
            $want = ''
            if ($env:NO_COLOR -or $env:TERM -eq 'dumb') { return }
            try { if ([Console]::IsOutputRedirected) { return } } catch { return }
            if ($Host.Name -ne 'ConsoleHost' -and -not $Host.UI.SupportsVirtualTerminal) { return }
        }
        $Ui.Cols = Get-UiCols
        if (-not $want -and $Ui.Cols -lt 60) { return }
        $vt = $false
        if ($want -eq 'vt') { $vt = $true }
        elseif (-not $want) {
            if ($Host.UI.SupportsVirtualTerminal) { $vt = $true } else { $vt = Enable-UiVt }
        }
        $Ui.LastCols = $Ui.Cols
        $Ui.Start = Get-Date
        if ($vt) {
            $Ui.Mode = 'vt'
            $e = $C.Esc
            $C.Rst = $e + '[0m'; $C.Bold = $e + '[1m'; $C.Dim = $e + '[38;5;243m'; $C.Green = $e + '[32m'
            $C.Cyan = $e + '[36m'; $C.Yellow = $e + '[33m'; $C.Red = $e + '[31m'; $C.Clr = $e + '[K'
            # the glyphs of the design note, which Consolas and Cascadia have (no emoji): a check, a middle dot, an x,
            # the blocks of the bar, the braille spinner, the frame's lines and rounded corners
            $Ui.G = @{ done = [string][char]0x2714; pending = [string][char]0x00B7; warn = '!'
                       fail = [string][char]0x2716; on = [string][char]0x2588; off = [string][char]0x2591
                       h = [string][char]0x2500; v = [string][char]0x2502; tl = [string][char]0x256D
                       tr = [string][char]0x256E; bl = [string][char]0x2570; br = [string][char]0x256F }
            $Ui.Spin = @(@(0x280B, 0x2819, 0x2839, 0x2838, 0x283C, 0x2834, 0x2826, 0x2827, 0x2807, 0x280F) | ForEach-Object { [string][char]$_ })
            Write-Host -NoNewline ($e + '[?25l')
        } else {
            $Ui.Mode = 'lines'
        }
        # the text column: 4 spaces, the glyph, 2 spaces, the name in 13, a space
        $Ui.Pad = 4 + $Ui.G.done.Length + 2 + 13 + 1
        Set-UiGeometry
    }

    # $seconds as "48 s", "3 min 40 s" or "3 min".
    function Format-Duration([int]$seconds) {
        if ($seconds -lt 60) { return "$seconds s" }
        $min = [math]::Floor($seconds / 60)
        if ($seconds % 60 -eq 0) { return "$min min" }
        return "$min min $($seconds % 60) s"
    }

    # The seconds since $from, whole.
    function Get-Elapsed($from) { return [int][math]::Floor(((Get-Date) - $from).TotalSeconds) }

    # $text with the profile folder written ~, for the rows (the log and plain mode keep paths as they are).
    function ConvertTo-Tilde([string]$text) {
        $profileDir = "$env:USERPROFILE"
        if ($profileDir.Length -le 3) { return $text }
        return [regex]::Replace($text, '(?i)' + [regex]::Escape($profileDir.TrimEnd('\')), '~')
    }

    # The row named $name, or $null.
    function Get-UiRow([string]$name) {
        foreach ($row in $Ui.Rows) { if ($row.Name -eq $name) { return $row } }
        return $null
    }

    # Set-UiRow NAME STATE TEXT: the row's state (done, active, pending, warn, fail) and text; a new row goes at the
    # end.
    function Set-UiRow([string]$name, [string]$state, [string]$text) {
        if ($Ui.Mode -eq 'plain') { return }
        $row = Get-UiRow $name
        if ($null -eq $row) {
            $row = @{ Name = $name; State = ''; Text = ''; Shown = '' }
            [void]$Ui.Rows.Add($row)
        }
        $row.State = $state
        $row.Text = $text
        if ($Ui.Mode -eq 'vt') { Write-UiFrame } else { Write-UiRowLine $row }
    }

    function Set-UiActive([string]$name, [string]$text) { Set-UiRow $name 'active' $text }
    function Set-UiDone([string]$name, [string]$text) { Set-UiRow $name 'done' $text }
    function Set-UiPending([string]$name, [string]$text) { Set-UiRow $name 'pending' $text }
    function Set-UiWarn([string]$name, [string]$text) { Set-UiRow $name 'warn' $text }

    # The state of the row named $name ('' when there is none).
    function Get-UiState([string]$name) {
        $row = Get-UiRow $name
        if ($null -eq $row) { return '' }
        return $row.State
    }

    # The row named $name is called $new from now on.
    function Rename-UiRow([string]$name, [string]$new) {
        $row = Get-UiRow $name
        if ($null -ne $row) { $row.Name = $new }
    }

    # The row that runs now (the last one), or $null.
    function Get-UiActiveRow {
        for ($i = $Ui.Rows.Count - 1; $i -ge 0; $i--) {
            if ($Ui.Rows[$i].State -eq 'active') { return $Ui.Rows[$i] }
        }
        return $null
    }

    # $text in lines no wider than $width, split at spaces; a word wider than a line is clipped, so that a line never
    # wraps by itself.
    function Get-UiWrapped([int]$width, [string]$text) {
        if ($width -lt 8) { $width = 8 }
        $lines = New-Object System.Collections.ArrayList
        $line = ''
        foreach ($word in ($text -split ' ')) {
            if (-not $word) { continue }
            if ($word.Length -gt $width) { $word = $word.Substring(0, $width - 3) + '...' }
            if (-not $line) { $line = $word }
            elseif ($line.Length + 1 + $word.Length -le $width) { $line = $line + ' ' + $word }
            else { [void]$lines.Add($line); $line = $word }
        }
        [void]$lines.Add($line)
        return ,$lines
    }

    # A row of vt mode: its lines (the name padded to 13; the text wrapped, or the bar as the build laid it out).
    function Format-UiRow($row) {
        switch ($row.State) {
            'done' { $g = $C.Green + $Ui.G.done + $C.Rst }
            'active' { $g = $C.Cyan + $Ui.Spin[$Ui.Frame % $Ui.Spin.Count] + $C.Rst }
            'pending' { $g = $C.Dim + $Ui.G.pending + $C.Rst }
            'warn' { $g = $C.Yellow + $Ui.G.warn + $C.Rst }
            default { $g = $C.Red + $Ui.G.fail + $C.Rst }
        }
        $name = $row.Name.PadRight(13)
        if ($row.State -eq 'pending') { $first = '    ' + $g + '  ' + $C.Dim + $name + ' '; $more = $C.Dim }
        else { $first = '    ' + $g + '  ' + $C.Bold + $name + $C.Rst + ' '; $more = '' }
        $out = New-Object System.Collections.ArrayList
        if ($row.Text.Contains($C.Esc)) {
            [void]$out.Add($first + $row.Text)
            return ,$out
        }
        $indent = ' ' * $Ui.Pad
        $i = 0
        foreach ($l in (Get-UiWrapped ($Ui.TW - $Ui.Pad - 1) $row.Text)) {
            if ($i -eq 0) { [void]$out.Add($first + $l + $C.Rst) } else { [void]$out.Add($indent + $more + $l + $C.Rst) }
            $i++
        }
        return ,$out
    }

    # Draws the block, over the last frame when there is one (vt mode).
    function Write-UiFrame {
        if ($Ui.Mode -ne 'vt') { return }
        $Ui.Cols = Get-UiCols
        Set-UiGeometry
        $sb = New-Object System.Text.StringBuilder
        if ($Ui.Cols -ne $Ui.LastCols) {
            if ($Ui.Lines -gt 0) { [void]$sb.Append("`r`n") }
            $Ui.Lines = 0
            $Ui.LastCols = $Ui.Cols
        }
        $prev = $Ui.Lines
        if ($prev -gt 0) { [void]$sb.Append($C.Esc + '[' + $prev + 'A' + "`r") }
        $inner = New-Object System.Collections.ArrayList
        $title = '  ' + $C.Bold + 'STP/TPW Launcher'
        if ($Ui.Version) { $title = $title + ' ' + $Ui.Version }
        [void]$inner.Add($title + $C.Rst)
        [void]$inner.Add('')
        foreach ($row in $Ui.Rows) { foreach ($l in (Format-UiRow $row)) { [void]$inner.Add($l) } }
        [void]$inner.Add('')
        foreach ($l in (Get-UiWrapped ($Ui.TW - 3) $Ui.Footer)) { [void]$inner.Add('  ' + $l) }
        $lines = New-Object System.Collections.ArrayList
        if ($Ui.Box) {
            $rule = $Ui.G.h * $Ui.In
            [void]$lines.Add($C.Dim + $Ui.G.tl + $rule + $Ui.G.tr + $C.Rst)
            [void]$lines.Add((Format-UiBoxed ''))
            foreach ($l in $inner) { [void]$lines.Add((Format-UiBoxed $l)) }
            [void]$lines.Add((Format-UiBoxed ''))
            [void]$lines.Add($C.Dim + $Ui.G.bl + $rule + $Ui.G.br + $C.Rst)
        } else {
            $lines = $inner
        }
        foreach ($l in $lines) { [void]$sb.Append($l).Append($C.Clr).Append("`r`n") }
        # a frame shorter than the one before: what is left of the old one is cleared
        if ($lines.Count -lt $prev) {
            for ($k = $lines.Count; $k -lt $prev; $k++) { [void]$sb.Append($C.Clr).Append("`r`n") }
            [void]$sb.Append($C.Esc + '[' + ($prev - $lines.Count) + 'A' + "`r")
        }
        $Ui.Lines = $lines.Count
        $Ui.LastDraw = Get-Date
        Write-Host -NoNewline $sb.ToString()
    }

    # The title, once, before the first line of lines mode.
    function Write-UiHeader {
        if ($Ui.Header) { return }
        $Ui.Header = $true
        $title = 'STP/TPW Launcher'
        if ($Ui.Version) { $title = $title + ' ' + $Ui.Version }
        Write-Host ('  ' + $title) -ForegroundColor White
        Write-Host ''
    }

    function Complete-UiProgress {
        if (-not $Ui.Progress) { return }
        $ProgressPreference = 'Continue'
        try { Write-Progress -Id 1 -Activity $Ui.Progress -Completed } catch {}
        $Ui.Progress = ''
    }

    # The finished rows of lines mode that wait for the version (so that the title, printed once, names it): written
    # now, in order, as they are now.
    function Write-UiHeld {
        if ($Ui.Held.Count -eq 0) { return }
        $held = @($Ui.Held)
        $Ui.Held.Clear()
        foreach ($row in $held) { $row.Shown = ''; Write-UiRowLine $row }
    }

    # A row of lines mode: a finished row (done, warn, fail) on its own line, once, after the version is known (or at
    # the close, whatever is known); the running one in Write-Progress.
    function Write-UiRowLine($row) {
        if ($row.State -eq 'active') {
            if ($Ui.Version -or $Ui.Flush) { Write-UiHeld }
            $ProgressPreference = 'Continue'
            $status = $row.Text
            if (-not $status) { $status = '...' }
            try {
                if ($row.Name -eq 'build' -and $null -ne $Ui.Pct -and $Ui.Pct -gt 0) {
                    Write-Progress -Id 1 -Activity $row.Name -Status $status -PercentComplete $Ui.Pct
                } else {
                    Write-Progress -Id 1 -Activity $row.Name -Status $status
                }
                $Ui.Progress = $row.Name
            } catch {}
            return
        }
        if ($row.State -eq 'pending') { return }
        $key = $row.State + '|' + $row.Text
        if ($row.Shown -eq $key) { return }
        $row.Shown = $key
        if (-not $Ui.Header -and -not $Ui.Version -and -not $Ui.Flush) {
            if (-not $Ui.Held.Contains($row)) { [void]$Ui.Held.Add($row) }
            return
        }
        if ($Ui.Held.Count -gt 0 -and -not $Ui.Held.Contains($row)) { Write-UiHeld }
        Complete-UiProgress
        Write-UiHeader
        switch ($row.State) {
            'done' { $g = $Ui.G.done; $colour = 'Green' }
            'warn' { $g = $Ui.G.warn; $colour = 'Yellow' }
            default { $g = $Ui.G.fail; $colour = 'Red' }
        }
        $i = 0
        foreach ($l in (Get-UiWrapped ($Ui.Cols - $Ui.Pad - 1) $row.Text)) {
            if ($i -eq 0) {
                Write-Host -NoNewline '    '
                Write-Host -NoNewline $g -ForegroundColor $colour
                Write-Host -NoNewline ('  ' + $row.Name.PadRight(13) + ' ') -ForegroundColor White
                Write-Host $l
            } else {
                Write-Host ((' ' * $Ui.Pad) + $l)
            }
            $i++
        }
    }

    # The build row's text. Until the compiler reports a percentage of at least 1 (no bare "0 %"): what the build is
    # doing, in words (fetching a tool, configuring the build, starting the compiler, compiling the first file). Then
    # the bar (20 cells, 10 under 72 columns of text), the percentage, the time left (from the pace so far, once 10
    # points and 5 seconds are done) and the file being compiled, in what is left of the line. Lines mode has no bar
    # of its own (Write-Progress draws one): the percentage, the time left and the file.
    function Format-UiBuild {
        $pct = $Ui.Pct
        if ($null -eq $pct -or $pct -lt 1) {
            if ($null -ne $pct -and $Ui.File -and $Ui.File -notmatch '^linking ') { return 'compiling ' + $Ui.File }
            if ($Ui.Words) { return $Ui.Words }
            if ($Ui.Stage -ge 2) { return 'starting the compiler' }
            if ($Ui.Stage -ge 1) { return 'configuring the build' }
            return 'preparing the build'
        }
        $left = ''
        if ($null -ne $Ui.BuildT0 -and $pct -lt 100) {
            $gain = $pct - $Ui.BuildP0
            $spent = ((Get-Date) - $Ui.BuildT0).TotalSeconds
            if ($gain -ge 10 -and $spent -ge 5) {
                $left = 'about ' + (Format-Duration ([int][math]::Floor($spent * (100 - $pct) / $gain))) + ' left'
            }
        }
        $pc = "$pct".PadLeft(2) + ' %'
        if ($Ui.Mode -ne 'vt') {
            $t = $pc
            if ($left) { $t = $t + '   ' + $left }
            if ($Ui.File) { $t = $t + '   ' + $Ui.File }
            return $t
        }
        $w = 20
        if ($Ui.TW -lt 72) { $w = 10; $left = '' }
        $on = [int][math]::Floor($pct * $w / 100)
        $bar = $C.Cyan + ($Ui.G.on * $on) + $C.Dim + ($Ui.G.off * ($w - $on)) + $C.Rst
        $t = $bar + '  ' + $pc
        $used = $w + 6
        if ($left) { $t = $t + '   ' + $left; $used = $used + 3 + $left.Length }
        $room = $Ui.TW - $Ui.Pad - $used - 4
        if ($Ui.File -and $room -ge 8) {
            $f = $Ui.File
            if ($f.Length -gt $room) { $f = '...' + $f.Substring($f.Length - $room + 3) }
            $t = $t + '   ' + $C.Dim + $f + $C.Rst
        }
        return $t
    }

    # The build's row from a line of build.ps1's output: the percentage (ninja's "[12/345]", or make's "[ 58%]"), the
    # file being compiled or linked, and before the first percentage the stage the build is in (the tools being
    # fetched, by name; cmake configuring; the compiler starting), which only ever advances; redrawn a few times a
    # second at most.
    function Update-UiBuild([string]$line) {
        $changed = $false
        $now = $false
        $rest = $null
        if ($line -match '^\[\s*(\d+)/(\d+)\]\s*(.*)$') {
            $a = [int]$Matches[1]
            $b = [int]$Matches[2]
            $rest = $Matches[3]
            if ($b -gt 0) { $Ui.Pct = [int][math]::Floor($a * 100 / $b) }
            $changed = $true
        } elseif ($line -match '^\[\s*(\d+)%\]\s*(.*)$') {
            $Ui.Pct = [int]$Matches[1]
            $rest = $Matches[2]
            $changed = $true
        } elseif ($line -match '^==> Fetching (\S+)(?: (\S+))?') {
            $Ui.Words = 'fetching ' + $Matches[1]
            if ($Matches[2]) { $Ui.Words = $Ui.Words + ' ' + $Matches[2] }
            $changed = $true; $now = $true
        } elseif ($line -match '^-- (Configuring|Generating) done|^-- Build files have been written|^==> Building|^Scanning dependencies') {
            if ($Ui.Stage -lt 2) { $Ui.Stage = 2; $Ui.Words = ''; $changed = $true; $now = $true }
        } elseif ($line -match '^==> Configuring|^==> Build folder|^-- ') {
            if ($Ui.Stage -lt 1) { $Ui.Stage = 1; $Ui.Words = ''; $changed = $true; $now = $true }
        }
        if ($null -ne $rest) {
            if ($rest -match '^Building .*? object (.*)$') {
                $Ui.File = ($Matches[1] -replace 'CMakeFiles[\\/][^\\/]*\.dir[\\/]', '') -replace '\.(o|obj)$', ''
            } elseif ($rest -match '^Linking \S+ \S+ (.*)$') {
                $Ui.File = 'linking ' + $Matches[1]
            }
        }
        if ($null -ne $Ui.Pct) {
            if ($Ui.Pct -gt 100) { $Ui.Pct = 100 }
            if ($Ui.Pct -ge 1 -and $null -eq $Ui.BuildT0) { $Ui.BuildT0 = Get-Date; $Ui.BuildP0 = $Ui.Pct }
        }
        if (-not $changed) { return }
        # the stage's few changes are drawn at once; the percentage a few times a second
        if (-not $now -and ((Get-Date) - $Ui.LastDraw).TotalMilliseconds -lt 200 -and ($null -eq $Ui.Pct -or $Ui.Pct -lt 100)) { return }
        $Ui.Frame += 1
        Set-UiActive 'build' (Format-UiBuild)
        $Ui.LastDraw = Get-Date
    }

    # A line under the block, in colour ($colour: Green, Yellow, Red, Dim or '').
    function Write-UiText([string]$text, [string]$colour) {
        if (-not $colour) { Write-Host $text; return }
        if ($Ui.Mode -eq 'vt') { Write-Host ($C[$colour] + $text + $C.Rst); return }
        $fg = $colour
        if ($colour -eq 'Dim') { $fg = 'DarkGray' }
        Write-Host $text -ForegroundColor $fg
    }

    # $text in lines no wider than $width where its words allow (a word wider than a line, a path, is kept whole).
    function Get-UiNoteLines([int]$width, [string]$text) {
        $lines = New-Object System.Collections.ArrayList
        $line = ''
        foreach ($word in ($text -split ' ')) {
            if (-not $word) { continue }
            if (-not $line) { $line = $word }
            elseif ($line.Length + 1 + $word.Length -le $width) { $line = $line + ' ' + $word }
            else { [void]$lines.Add($line); $line = $word }
        }
        if ($line) { [void]$lines.Add($line) }
        return ,$lines
    }

    # The notes (the lines Say kept) under the block, in $colour ('' for none): a line that starts with a space (a
    # command, a list item) as it is; a line that does not end a sentence is joined to the next; each paragraph
    # wrapped to the block's text width and indented two spaces, with a blank line only between paragraphs.
    function Write-UiNotes([string]$colour) {
        if ($Ui.Notes.Count -eq 0) { return }
        $items = New-Object System.Collections.ArrayList
        $para = ''
        foreach ($n in (@($Ui.Notes) + @(''))) {
            if ($n -eq '') {
                if ($para) { [void]$items.Add(@{ Text = $para; Raw = $false }); $para = '' }
                if ($items.Count -gt 0 -and $null -ne $items[$items.Count - 1]) { [void]$items.Add($null) }
                continue
            }
            if ($n.StartsWith(' ')) {
                if ($para) { [void]$items.Add(@{ Text = $para; Raw = $false }); $para = '' }
                [void]$items.Add(@{ Text = $n; Raw = $true })
                continue
            }
            if ($para) { $para = $para + ' ' + $n } else { $para = $n }
            if ($n -match '[.:?!"]$') { [void]$items.Add(@{ Text = $para; Raw = $false }); $para = '' }
        }
        while ($items.Count -gt 0 -and $null -eq $items[$items.Count - 1]) { $items.RemoveAt($items.Count - 1) }
        if ($items.Count -eq 0) { return }
        Write-Host ''
        foreach ($it in $items) {
            if ($null -eq $it) { Write-Host ''; continue }
            $text = ConvertTo-Tilde $it.Text
            if ($it.Raw) { Write-UiText ('  ' + $text) $colour; continue }
            foreach ($l in (Get-UiNoteLines ($Ui.TW - 2) $text)) { Write-UiText ('  ' + $l) $colour }
        }
    }

    # The last lines of the log and where it is, after a failure of the downloads or the build. Only once this run
    # started the log ($St.LogStarted, set where the log is truncated): before that the file holds an earlier run's
    # build, which has nothing to do with what went wrong now (a failure before any download, such as no release to
    # install, showed the last successful build's output and asked for a bug report about it).
    function Write-UiLogTail {
        if (-not $St.LogStarted) { return }
        if (-not (Test-Path -LiteralPath $Log -PathType Leaf)) { return }
        $tail = @(Get-Content -LiteralPath $Log -Tail 15 -ErrorAction SilentlyContinue)
        if ($tail.Count -eq 0) { return }
        Write-Host ''
        Write-Host '  The last lines of its output:'
        foreach ($l in $tail) { Write-UiText ('      ' + $l) 'Dim' }
        Write-Host ''
        Write-Host "  The whole output is in $(ConvertTo-Tilde $Log) (it holds nothing personal)."
        Write-Host '  Please attach it to a bug report: https://github.com/ThemeParkWorldDecomp/tpw-port/issues'
        Write-Host '  Run the line again with -Plain to see everything.'
    }

    # The block's last frame, with the cursor back. $state and $text: what the row that was running becomes ('' leaves
    # it as it is).
    function Close-Ui([string]$state, [string]$text) {
        if ($state) {
            $row = Get-UiActiveRow
            if ($null -ne $row) { $row.State = $state; $row.Text = $text }
        }
        if ($Ui.Mode -eq 'vt') {
            if ($Ui.Rows.Count -gt 0 -or $Ui.Lines -gt 0) { Write-UiFrame }
            Write-Host -NoNewline ($C.Esc + '[?25h')
            return
        }
        Complete-UiProgress
        $Ui.Flush = $true
        Write-UiHeld
        foreach ($row in $Ui.Rows) { if ($row.State -ne 'active' -and $row.State -ne 'pending') { Write-UiRowLine $row } }
        Write-UiHeader
        Write-Host ''
        if ($Ui.Footer) { Write-Host ('  ' + $Ui.Footer) }
    }

    # What went wrong: the running row turns red with the first sentence of the reason, then the notes, then the log's
    # last lines when a download or the build failed.
    function Show-UiFail([string]$reason) {
        if ($Ui.FailDone) { Write-UiNotes ''; return }
        $short = $reason
        $cut = $short.IndexOf('. ')
        if ($cut -ge 0) { $short = $short.Substring(0, $cut) }
        $short = $short.TrimEnd('.')
        $row = Get-UiActiveRow
        $name = ''
        if ($null -ne $row) {
            $name = $row.Name
            $text = $Ui.FailText
            if (-not $text) { $text = ConvertTo-Tilde $short }
            Close-Ui 'fail' $text
        } else {
            Close-Ui '' ''
            Write-Host ''
            if ($Ui.Mode -eq 'vt') {
                Write-Host ('    ' + $C.Red + $Ui.G.fail + $C.Rst + '  ' + (ConvertTo-Tilde $short))
            } else {
                Write-Host -NoNewline '    '
                Write-Host -NoNewline $Ui.G.fail -ForegroundColor Red
                Write-Host ('  ' + (ConvertTo-Tilde $short))
            }
        }
        Write-UiNotes ''
        if ($name -eq 'source' -or $name -eq 'build' -or $name -eq 'newest') { Write-UiLogTail }
        Write-Host ''
    }

    # The run worked: the last frame, the notes (yellow) and the closing line (green).
    function Finish-Ui {
        Close-Ui '' ''
        Write-UiNotes 'Yellow'
        Write-Host ''
        Write-UiText ('  ' + (ConvertTo-Tilde $Ui.Closing)) 'Green'
        Write-Host ''
    }

    # Ctrl+C: the block stays as it was and the text goes under it, with the cursor back.
    function Show-UiStopped {
        if ($Ui.Mode -eq 'vt') { Write-Host ($C.Esc + '[?25h' + "`r" + $C.Clr) } else { Complete-UiProgress; $Ui.Flush = $true; Write-UiHeld; Write-Host '' }
        Write-UiText '  Stopped. Nothing is half installed: the new build was in its own staging folder' 'Yellow'
        Write-UiText '  and the old launcher is as it was. Run the line again to continue.' 'Yellow'
    }

    # A git command whose output is for the screen: shown as it comes in plain mode, kept in the log (scrubbed) by the
    # progress view. Its exit code is in $LASTEXITCODE, as for any program.
    function Invoke-Git {
        if ($Ui.Mode -eq 'plain') {
            & $St.Git @GitCfg @args | Out-Host
            return
        }
        $rules = Get-LogRules
        $writer = New-Object System.IO.StreamWriter($Log, $true, (New-Object System.Text.UTF8Encoding($false)))
        try {
            & $St.Git @GitCfg @args 2>&1 | ForEach-Object {
                if ($_ -is [System.Management.Automation.ErrorRecord]) { $line = $_.Exception.Message } else { $line = "$_" }
                $writer.WriteLine((ConvertTo-LogLine $line $rules))
            }
            $code = $LASTEXITCODE
        } finally { $writer.Close() }
        $global:LASTEXITCODE = $code
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
            if (-not $s) { continue }
            # %TEMP% is often spelled with a short name (C:\Users\JOHNDO~1\AppData\Local\Temp); GetFullPath spells it as
            # the install folder was spelled.
            try { $s = [System.IO.Path]::GetFullPath($s) } catch {}
            if ($path.TrimEnd('\') -eq $s.TrimEnd('\')) { return $true }
        }
        return $false
    }

    function Test-Link([string]$path) {
        $item = Get-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
        return ($null -ne $item -and ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint))
    }

    # Deletes DIR\build-next, DIR\build-prev, DIR\tools\MinGit or DIR\tools\MinGit.tmp and nothing else. This is the
    # only place the script deletes a folder.
    function Remove-Own([string]$name) {
        if (@('build-next', 'build-prev', 'tools\MinGit', 'tools\MinGit.tmp') -cnotcontains $name) { Fail "Internal error: this script never deletes `"$name`"." }
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

    # The portable Git, DIR\tools\MinGit\cmd\git.exe: fetched now, unless an earlier run fetched the same pinned zip (the
    # .sha256 file beside it says which). The way build.ps1 fetches its tools: into a .tmp folder, checked against the
    # pinned SHA-256, unpacked with tar.exe, moved into place. Returns the git.exe to run.
    function Get-MinGit {
        $tools = Join-Path $Dir 'tools'
        $gitDir = Join-Path $tools 'MinGit'
        $exe = Join-Path $gitDir 'cmd\git.exe'
        $stamp = Join-Path $gitDir '.sha256'
        if ((Test-Path -LiteralPath $exe -PathType Leaf) -and (Test-Path -LiteralPath $stamp -PathType Leaf) -and
            ([System.IO.File]::ReadAllText($stamp).Trim() -eq $MinGit.Sha256)) {
            Info "Git fetched earlier: $gitDir"
            return $exe
        }
        $tmp = Join-Path $tools 'MinGit.tmp'
        $zip = Join-Path $tmp 'download.zip'
        $unpacked = Join-Path $tmp 'x'
        Remove-Own 'tools\MinGit.tmp'
        Remove-Own 'tools\MinGit'
        # DIR is made here already (step 2 makes it otherwise), marked as this script's. The install folder is $Dir: PowerShell
        # variable names ignore case and a called function sees this one's locals, so no local may be named dir (this one is gitDir).
        try {
            [void](New-Item -ItemType Directory -Force -Path $Dir -ErrorAction Stop)
            Write-Text (Join-Path $Dir '.tpw-installer') ''
            [void](New-Item -ItemType Directory -Force -Path $unpacked -ErrorAction Stop)
        } catch { Fail "Couldn't make the folder $tmp." }
        Info "No Git on this PC. Fetching Git (MinGit $($MinGit.Version), about $($MinGit.Size), once) into $gitDir"
        Set-UiActive 'tools' "fetching Git (MinGit $($MinGit.Version), about $($MinGit.Size), once)"
        $curl = Join-Path $env:SystemRoot 'System32\curl.exe'
        $out = & $curl -fsSL --retry 2 -o $zip $MinGit.Url 2>&1 | ForEach-Object { "$_" }
        if ($LASTEXITCODE -ne 0) {
            $detail = "Could not download $($MinGit.Url) (curl exit $LASTEXITCODE). $(($out -join ' ').Trim())"
            Remove-Own 'tools\MinGit.tmp'
            Fail-Network $detail.Trim()
        }
        $sum = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($sum -ne $MinGit.Sha256) {
            Remove-Own 'tools\MinGit.tmp'
            Say 'The download of Git does not match the checksum in this script, so it was deleted.'
            Say "  expected $($MinGit.Sha256)"
            Say "  got      $sum"
            Say 'Run this again. If it fails the same way, the download has changed: report it, do not use it.'
            Fail 'The download of Git did not match its checksum.'
        }
        $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
        & $tar -xf $zip -C $unpacked 2>&1 | Out-Null
        $tarCode = $LASTEXITCODE
        $moved = $false
        if ($tarCode -eq 0) {
            try { Move-Item -LiteralPath $unpacked -Destination $gitDir -ErrorAction Stop; $moved = $true } catch { $moved = $false }
        }
        Remove-Own 'tools\MinGit.tmp'
        if (-not $moved -or -not (Test-Path -LiteralPath $exe -PathType Leaf)) {
            Remove-Own 'tools\MinGit'
            Fail "Couldn't unpack the download of Git (tar exit $tarCode). Run this again."
        }
        Write-Text $stamp $MinGit.Sha256
        Info "Fetched Git: MinGit $($MinGit.Version) from $($MinGit.Url); its SHA-256 checksum matched"
        Set-UiDone 'tools' "Windows PowerShell found; Git $($MinGit.Version) fetched into tools\MinGit"
        $St.GitFetched = $true
        return $exe
    }

    function Test-Computer {
        if ($env:OS -ne 'Windows_NT') {
            Say 'This installer is for Windows. On a Mac, use install.sh (see the README).'
            Fail 'This installer is for Windows only.'
        }
        if (-not [Environment]::Is64BitOperatingSystem) { Fail 'This needs 64-bit Windows 10 or 11.' }
        $os = [Environment]::OSVersion.Version
        Info "Windows $($os.Major).$($os.Minor) build $($os.Build), $env:PROCESSOR_ARCHITECTURE"

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
        if ($missing.Count) {
            $Ui.FailText = "not installed: $($missing -join ', ')"
            Say "Missing: $($missing -join ', ')."
            foreach ($h in $how) { Say "  $h" }
            Say 'Then run this command again. Nothing was changed.'
            Fail "Missing: $($missing -join ', '). See the lines above for how to get it, then run this again."
        }
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

        # Git: the one on the PATH, or else the portable one in DIR\tools\MinGit, fetched now when it is not there
        # (nothing is installed; see SAFETY). With TPW_INSTALL_STUB nothing is cloned, but Git is still looked for and
        # fetched when missing, so that the installer's CI tests the fetch with the stub.
        if ($null -ne (Get-Command git.exe -CommandType Application -ErrorAction SilentlyContinue)) {
            $St.Git = 'git.exe'
            $gitVersion = (& git.exe --version 2>$null | Out-String).Trim()
            Info "Git: $gitVersion"
        } else {
            $St.Git = Get-MinGit
            $gitVersion = (& $St.Git --version 2>$null | Out-String).Trim()
            if ($LASTEXITCODE -ne 0 -or -not $gitVersion) {
                Fail "The fetched Git in $(Join-Path $Dir 'tools\MinGit') does not run. Delete that folder and run this again."
            }
            Info "Git: $gitVersion (MinGit, in $(Join-Path $Dir 'tools\MinGit'); nothing is installed on the PC)"
        }
    }

    # ---- 2. the downloads

    # Which release or commit to install. Sets $St.Tag (stable only) and checks GitHub answers, before anything is
    # made.
    function Select-Target {
        if ($St.Channel -eq 'dev') {
            $St.Tag = ''
            $out = & $St.Git @GitCfg ls-remote $Repo refs/heads/main 2>&1 | ForEach-Object { "$_" }
            if ($LASTEXITCODE -ne 0) { Fail-Network ($out -join "`n") }
            if (-not $out) { Fail 'The repository has no main branch.' }
            return
        }
        $out = & $St.Git @GitCfg ls-remote --tags --refs $Repo 2>&1 | ForEach-Object { "$_" }
        if ($LASTEXITCODE -ne 0) { Fail-Network ($out -join "`n") }
        # Releases are the tags vX.Y.Z. The highest by number, not by letters (v0.0.10 comes after v0.0.9).
        $versions = @()
        foreach ($line in $out) {
            if ($line -match 'refs/tags/v(\d+)\.(\d+)\.(\d+)$') { $versions += [version]"$($Matches[1]).$($Matches[2]).$($Matches[3])" }
        }
        if ($versions.Count -eq 0) {
            if ($St.Channel -eq 'stable' -and (Read-Channel) -eq 'dev') {
                # a dev install, run without -Dev before any release exists: it stays on main, and that is no bug
                $old = Get-Info (Join-Path $Dir 'build\install-info.txt') 'version'
                if (-not $old) { $old = 'unknown' }
                Set-UiWarn 'installed' "this install follows main ($old); there is no release to switch it to yet"
                Say 'No release has been published yet. This install follows main (the development version); to update it, run:'
                Say '  themepark update -Dev'
                if ($St.GitFetched) { Say "Nothing else was changed (Git was fetched into $(Join-Path $Dir 'tools\MinGit'))." } else { Say 'Nothing was changed.' }
                Fail 'No release has been published yet. This install follows main; run themepark update -Dev to update it.'
            }
            Say 'No release has been published yet, so there is nothing to install by default.'
            Say 'To try the version that is being made (it can have bugs), run this with -Dev:'
            Say '  & ([scriptblock]::Create((irm https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.ps1))) -Dev'
            if ($St.GitFetched) { Say "Nothing else was changed (Git was fetched into $(Join-Path $Dir 'tools\MinGit'))." } else { Say 'Nothing was changed.' }
            Fail 'No release has been published yet. Use -Dev to install the development version.'
        }
        $newest = ($versions | Sort-Object | Select-Object -Last 1)
        $St.Tag = "v$newest"
        Info "Newest release: $newest"
        $Ui.Version = "$newest"
    }

    # Clones or fetches the source and checks out the wanted commit, submodules included. Sets $St.Commit and
    # $St.Version.
    function Get-Source {
        if (Test-Path -LiteralPath (Join-Path $Src '.git')) {
            Invoke-Git -C $Src remote set-url origin $Repo
            if ($LASTEXITCODE -ne 0) { Fail "Couldn't use the source folder $Src." }
        } elseif (Test-Path -LiteralPath $Src) {
            Say "$Src exists but is not a git clone, so this script cannot use it. Move it away and run this again."
            Fail "$Src is in the way. Move it away and run this again."
        } else {
            Info "Downloading the source (a clone of $Repo)"
            Set-UiActive $Ui.Src 'downloading tpw-port'
            # -c on clone keeps the settings in the clone, for every later git command too.
            Invoke-Git clone -c core.autocrlf=false -c core.longpaths=true --no-checkout $Repo $Src
            if ($LASTEXITCODE -ne 0) { Fail-Network 'The download of the source did not finish.' }
        }

        if ($St.Channel -eq 'dev') {
            Info 'Fetching main'
            Set-UiActive $Ui.Src 'fetching main'
            Invoke-Git -C $Src fetch --force --no-tags origin '+refs/heads/main:refs/remotes/origin/main'
            if ($LASTEXITCODE -ne 0) { Fail-Network 'The download of the source did not finish.' }
            $ref = 'refs/remotes/origin/main'
        } else {
            Info "Fetching $($St.Tag)"
            Set-UiActive $Ui.Src "fetching $($St.Tag)"
            Invoke-Git -C $Src fetch --force --no-tags origin "+refs/tags/$($St.Tag):refs/tags/$($St.Tag)"
            if ($LASTEXITCODE -ne 0) { Fail-Network 'The download of the source did not finish.' }
            $ref = "refs/tags/$($St.Tag)"
        }
        $commit = (& $St.Git @GitCfg -C $Src rev-parse --verify "$ref^{commit}" 2>$null | Out-String).Trim()
        if ($LASTEXITCODE -ne 0 -or -not $commit) { Fail "The source has no $ref." }
        $St.Commit = $commit
        # The view's title names the version as soon as the commit is known (the checkout and the submodules take a
        # while); $St.Version itself is read from the checkout below, and a version that does not parse is left to
        # that check.
        if ($Ui.Mode -ne 'plain') {
            $early = ''
            try { $early = ([string](& $St.Git @GitCfg -C $Src show "${commit}:launcher/VERSION.txt" 2>$null | Select-Object -First 1)).Trim() } catch {}
            if ($early -match '^\d+\.\d+\.\d+(-dev)?$') { $Ui.Version = $early }
        }

        # The source is this script's own. A change somebody made in it would be lost by the checkout: stop instead. A
        # clone not checked out yet has no index (and git would call every file deleted). Submodules are left out of the
        # check: they differ from the pinned commits between a checkout and their update.
        if (Test-Path -LiteralPath (Join-Path $Src '.git\index')) {
            $changes = (& $St.Git @GitCfg -C $Src status --porcelain --ignore-submodules=all 2>$null | Out-String).Trim()
            if ($changes) {
                Say "$Src has changes that this script did not make, and switching versions would overwrite them."
                Say 'Move that folder away (or the changes out of it) and run this again.'
                Fail "The source folder has changes of its own: $Src"
            }
        }
        Invoke-Git -C $Src checkout -q --detach $commit
        if ($LASTEXITCODE -ne 0) { Fail "Couldn't switch the source to $commit." }
        Invoke-Git -C $Src submodule sync -q --recursive
        if ($LASTEXITCODE -ne 0) { Fail "Couldn't read the source's submodules." }
        Info 'Downloading the submodules'
        Set-UiActive $Ui.Src 'downloading the submodules'
        Invoke-Git -C $Src submodule update --init --recursive --force
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
        Info "Source: $commit (version $version)"
        $Ui.Version = $version
    }

    # Makes an empty DIR\build-next and copies in the tools the build in use fetched (build.ps1 keeps each with its
    # checksum and fetches it again only when the pin changed).
    function New-Next {
        Remove-Own 'build-next'
        [void](New-Item -ItemType Directory -Force -Path $Next)
        $oldTools = Join-Path $Dir 'build\tools'
        if (Test-Path -LiteralPath $oldTools) {
            Info 'Copying the build tools of the launcher in use'
            Set-UiActive $Ui.Src 'copying the build tools of the launcher in use'
            & robocopy.exe $oldTools (Join-Path $Next 'tools') /E /NFL /NDL /NJH /NJS /NP | Out-Null
            if ($LASTEXITCODE -ge 8) { Say 'Copying them did not work; the build fetches them again.' }
            if ($Ui.Mode -ne 'plain') { Set-UiDone $Ui.Src $Ui.Got }   # the row gets its text back
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

    # devtools\win\build.ps1, its output on the screen (plain mode) or in the build's row (the progress view), and in
    # DIR\last-build.log either way. TOOLS=fetch: the pinned tools, the same as the release builds, whatever this PC's
    # PATH has. Its exit codes: 0 done, 10 no curl.exe or tar.exe, 20 a download failed, 21 a download did not match its
    # checksum, 22 unpacking failed, 30 configure, 40 build, 45 a DLL.
    function Invoke-Build {
        $buildPs1 = Join-Path $Src 'devtools\win\build.ps1'
        $rules = Get-LogRules
        # the progress view's log holds the downloads' output too; the build's goes after it
        $writer = New-Object System.IO.StreamWriter($Log, ($Ui.Mode -ne 'plain'), (New-Object System.Text.UTF8Encoding($false)))
        $St.LogStarted = $true
        # CI=1: build.ps1 downloads without a progress bar, which would be one long line here.
        $oldCI = $env:CI
        $env:CI = '1'
        $Ui.BuildStart = Get-Date
        $Ui.BuildT0 = $null
        $Ui.Pct = $null
        $Ui.File = ''
        $Ui.Stage = 0
        $Ui.Words = ''
        if ($Ui.Mode -ne 'plain') { Set-UiActive 'build' (Format-UiBuild) }
        try {
            & (Get-WindowsPowerShell) -NoLogo -NoProfile -ExecutionPolicy Bypass -File $buildPs1 "BUILD=$Next" 'TOOLS=fetch' 2>&1 | ForEach-Object {
                if ($_ -is [System.Management.Automation.ErrorRecord]) { $line = $_.Exception.Message } else { $line = "$_" }
                if ($Ui.Mode -eq 'plain') { Write-Host $line } else { Update-UiBuild $line }
                $writer.WriteLine((ConvertTo-LogLine $line $rules))
            }
            $code = $LASTEXITCODE
        } finally {
            $writer.Close()
            $env:CI = $oldCI
        }
        $spent = Format-Duration (Get-Elapsed $Ui.BuildStart)
        if ($code -eq 0) {
            Set-UiDone 'build' "done in $spent"
            return
        }
        if ($code -eq 20) { Fail-Network 'A download of the build tools did not finish.' }
        if ($Ui.Mode -ne 'plain') {
            Close-Ui 'fail' "failed after $spent"
            if (Test-Path -LiteralPath (Join-Path $Dir 'build\tpw_launcher.exe')) {
                Write-Host ''
                Write-Host "  The launcher in use ($(Get-Info (Join-Path $Dir 'build\install-info.txt') 'version')) is untouched and still works."
            }
            Write-UiLogTail
            $Ui.FailDone = $true
        }
        Info "The build failed (build.ps1 exit $code). Its output is above and in $Log."
        Info "The launcher in use ($(Join-Path $Dir 'build')) was not changed."
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
        Info $first
        try {
            Write-Text (Join-Path $Next 'install-info.txt') "version=$($St.Version)`nchannel=$($St.Channel)`ncommit=$($St.Commit)`n"
        } catch { Fail "Couldn't write $(Join-Path $Next 'install-info.txt')." }
    }

    # ---- 5. the switch

    # Moves a folder, trying again for a few seconds: Windows refuses while a virus scan or the search indexer still has a
    # new file in it open.
    function Move-Folder([string]$from, [string]$to) {
        $tries = 0
        while ($true) {
            try { Move-Item -LiteralPath $from -Destination $to -ErrorAction Stop; return }
            catch {
                $tries++
                if ($tries -ge 10) { Say "Windows said: $($_.Exception.Message)"; throw }
                Start-Sleep -Milliseconds 500
            }
        }
    }

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
            try { Move-Folder $build $prev }
            catch { Fail "Couldn't move the old launcher aside. Close it and try again. Nothing was switched." }
            $hadBuild = $true
        }
        try { Move-Folder $Next $build }
        catch {
            if ($hadBuild) { try { Move-Folder $prev $build } catch {} }
            Fail "Couldn't move the new launcher into place. The old one was put back."
        }
        Write-Channel $newChannel
        $St.Version = $newVersion
        if ($hadBuild) {
            Info "Switched to version $newVersion. The one before is kept in $prev."
            Set-UiDone 'swap' "switched to $newVersion; the one before is kept in build-prev"
        } else {
            Info "Installed version $newVersion."
            if ((Get-UiState 'swap') -eq 'active') { Set-UiDone 'swap' "installed version $newVersion" }
        }
    }

    function Start-Launcher {
        $exe = Join-Path $Dir 'build\tpw_launcher.exe'
        Info "Starting $exe"
        Set-UiActive 'start' 'opening the launcher'
        try {
            Start-Process -FilePath $exe -WorkingDirectory $Dir -ErrorAction Stop | Out-Null
            Set-UiDone 'start' 'the launcher is open'
        } catch {
            Say "Couldn't start the launcher. Start it yourself: $exe"
            Set-UiWarn 'start' 'the launcher did not start (see below)'
        }
    }

    # ---- the test build (TPW_INSTALL_STUB=1, for the installer's CI only)

    # Instead of the build: a stand-in tpw_launcher.exe, compiled with the C# compiler of the .NET Framework that comes
    # with Windows. Like the real one it has no console and answers --version; run from DIR\build (by themepark), it adds
    # a line to DIR\stub-launch.txt, so a test can see that themepark started it.
    function New-StubLauncher {
        [void](New-Item -ItemType Directory -Force -Path $Next)
        Info 'Test build: a stand-in launcher.'
        Set-UiActive 'build' 'a test build: a stand-in launcher'
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
        if (args.Length > 0 && args[0] == "--write-icon") return 1;   // the icon is the real launcher's: the shortcut shows the program's own
        string here = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
        if (Path.GetFileName(here) == "build") {
            File.AppendAllText(Path.Combine(Path.GetDirectoryName(here), "stub-launch.txt"),
                "STP/TPW Launcher v$v args=" + string.Join(" ", args) + " cwd=" + Environment.CurrentDirectory + "\r\n");
        }
        return 0;
    }
}
"@
        if ($Ui.Mode -eq 'plain') {
            & $csc /nologo /target:winexe "/out:$(Join-Path $Next 'tpw_launcher.exe')" $source | Out-Host
        } else {
            & $csc /nologo /target:winexe "/out:$(Join-Path $Next 'tpw_launcher.exe')" $source | Out-Null
        }
        if ($LASTEXITCODE -ne 0) { Fail 'Test build: the stand-in launcher did not compile.' }
        Remove-Item -LiteralPath $source -Force -ErrorAction SilentlyContinue
        Set-UiDone 'build' 'a stand-in launcher (test build)'
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
            'rem The STP/TPW Launcher of this install (the folder above this one), from any folder:',
            'rem   themepark             opens the launcher and returns at once',
            'rem   themepark update      updates this install to the newest release (themepark update -Dev: to main) in this',
            'rem                         window, by running the installer that came with it (source\install.ps1 -Update)',
            'rem   themepark --version   prints the launcher''s version in this window (--help: its options)',
            'rem Anything else is passed on to the launcher, which is opened detached. The installer (install.ps1) writes',
            'rem this file. To remove the command, delete the install folder and remove its bin folder from your Path',
            'rem (Settings, search for "environment variables for your account").',
            'setlocal',
            'set "TPW_DIR=%~dp0.."',
            'if /i "%~1"=="update" goto update',
            'if not exist "%TPW_DIR%\build\tpw_launcher.exe" goto missing',
            'if /i "%~1"=="--version" goto show',
            'if /i "%~1"=="--help" goto show',
            'if /i "%~1"=="-h" goto show',
            'start "" /d "%TPW_DIR%" "%TPW_DIR%\build\tpw_launcher.exe" %*',
            'exit /b 0',
            ':show',
            'rem the launcher has no console: its output reaches this window only through a pipe',
            '"%TPW_DIR%\build\tpw_launcher.exe" %* 2>&1 | find /v ""',
            'exit /b %errorlevel%',
            ':update',
            'if not exist "%TPW_DIR%\source\install.ps1" goto noinstaller',
            'rem the installer replaces build\ and opens the new launcher: one that is running must be quit first (its own',
            'rem Update button updates it while it runs)',
            'tasklist /fi "imagename eq tpw_launcher.exe" /nh 2>nul | findstr /i /c:"tpw_launcher.exe" >nul',
            'if not errorlevel 1 goto running',
            'set "TPW_ARGS="',
            ':more',
            'shift',
            'if "%~1"=="" goto run',
            'set "TPW_ARGS=%TPW_ARGS% %1"',
            'goto more',
            ':run',
            'powershell -NoProfile -ExecutionPolicy Bypass -File "%TPW_DIR%\source\install.ps1" -Update -Dir "%TPW_DIR%" %TPW_ARGS%',
            'exit /b %errorlevel%',
            ':missing',
            'echo themepark: there is no launcher in "%TPW_DIR%\build". Run the installer again.',
            'exit /b 1',
            ':noinstaller',
            'echo themepark: there is no installer in "%TPW_DIR%\source". Run the install line from the README again.',
            'exit /b 1',
            ':running',
            'echo themepark: the launcher is running. Quit it, or press its Update button, then run themepark update again.',
            'exit /b 1'
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
        Info "Added $bin to your Path."
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
        # The new window compares the command it finds with ours and answers with one word: its output file is in the
        # console's code page, which cannot spell every folder name (the command itself is UTF-16).
        $expected = (Join-Path $Dir 'bin\themepark.cmd') -replace "'", "''"
        $check = "`$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + " +
                 "[Environment]::GetEnvironmentVariable('Path', 'User'); " +
                 "`$c = Get-Command themepark -ErrorAction SilentlyContinue | Select-Object -First 1; " +
                 "if (`$c -and `$c.Path -ieq '$expected') { [Console]::Out.WriteLine('ours') } " +
                 "elseif (`$c) { [Console]::Out.WriteLine('other') } else { [Console]::Out.WriteLine('none') }"
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
        return ($found -eq 'ours')
    }

    function Wait-Exit([int]$id) {
        $p = Get-Process -Id $id -ErrorAction SilentlyContinue
        if ($null -eq $p) { return }
        if (-not $p.WaitForExit(300000)) { Fail 'The launcher is still running after 5 minutes. Nothing was switched.' }
    }

    # ---- the Start menu shortcut (THE START MENU SHORTCUT above)

    $ShortcutName = 'STP-TPW Launcher.lnk'

    # The folder the shortcut goes in: TPW_INSTALL_PROGRAMS (a test), else the user's own Programs folder of the Start
    # menu.
    function Get-ProgramsFolder {
        if ($env:TPW_INSTALL_PROGRAMS) { return $env:TPW_INSTALL_PROGRAMS }
        return [Environment]::GetFolderPath('Programs')
    }

    # The shortcut's icon: DIR\icon.ico, written by the launcher in use (again at every run, so that it is the game's
    # icon once a game is imported). The launcher has no console; its exit code says whether it wrote the file. '' when
    # it did not (the stand-in launcher of a test build): the shortcut then shows the program's own icon.
    function Write-ShortcutIcon {
        $exe = Join-Path $Dir 'build\tpw_launcher.exe'
        $icon = Join-Path $Dir 'icon.ico'
        Remove-Item -LiteralPath $icon -Force -ErrorAction SilentlyContinue
        try {
            $p = Start-Process -FilePath $exe -ArgumentList @('--write-icon', "`"$icon`"") -WorkingDirectory $Dir -NoNewWindow -PassThru -ErrorAction Stop
            $null = $p.Handle
            if (-not $p.WaitForExit(60000)) { try { $p.Kill() } catch {}; Info 'The launcher did not write its icon in time.'; return '' }
            if ($p.ExitCode -ne 0) { Info "The launcher did not write its icon (exit code $($p.ExitCode)); the shortcut shows the program's own."; return '' }
        } catch {
            Info "The launcher did not write its icon ($_); the shortcut shows the program's own."
            return ''
        }
        if (-not (Test-Path -LiteralPath $icon)) { Info 'The launcher wrote no icon file; the shortcut shows the program''s own.'; return '' }
        return $icon
    }

    # Writes the shortcut (again at every install, update and switch, when DIR\build has the launcher). The path of the
    # shortcut it wrote, or '' with the reason said: a shortcut of that name that points at another program, or a
    # folder it cannot write. Never fails the run.
    function Write-Shortcut {
        $exe = Join-Path $Dir 'build\tpw_launcher.exe'
        $programs = Get-ProgramsFolder
        if (-not $programs) { Say 'Windows did not say where the Start menu is, so STP/TPW Launcher was not added to it.'; return '' }
        $lnk = Join-Path $programs $ShortcutName
        $iconFile = Write-ShortcutIcon
        try {
            [void](New-Item -ItemType Directory -Force -Path $programs -ErrorAction Stop)
            $shell = New-Object -ComObject WScript.Shell
            $shortcut = $shell.CreateShortcut($lnk)   # loads an existing one, so its target can be looked at
            $target = [string]$shortcut.TargetPath
            if ((Test-Path -LiteralPath $lnk) -and $target -and ([System.IO.Path]::GetFileName($target) -ine 'tpw_launcher.exe')) {
                Say "$lnk points at $target, not at the launcher, so it is left alone."
                return ''
            }
            $shortcut.TargetPath = $exe
            $shortcut.WorkingDirectory = Join-Path $Dir 'build'
            $shortcut.Description = 'STP/TPW Launcher'
            if ($iconFile) { $shortcut.IconLocation = "$iconFile,0" } else { $shortcut.IconLocation = "$exe,0" }
            $shortcut.Save()
        } catch {
            Say "Couldn't write the Start menu shortcut $lnk ($("$_" -replace '\s+', ' ')). The launcher is in place all the same."
            return ''
        }
        Info "Start menu shortcut: $lnk"
        return $lnk
    }

    # The step that writes the shortcut: its row and its lines around Write-Shortcut. The shortcut's path, or ''.
    function Add-Shortcut {
        Set-UiActive 'startmenu' 'adding STP/TPW Launcher to the Start menu'
        $lnk = Write-Shortcut
        if ($lnk) {
            Info 'STP/TPW Launcher is in the Start menu: open it from there, or find it with Search.'
            Set-UiDone 'startmenu' 'STP/TPW Launcher, in the Start menu'
        } else {
            Set-UiWarn 'startmenu' 'STP/TPW Launcher was not added (see below)'
        }
        return $lnk
    }

    # ---- the three ways to run: swap, install or update

    function Invoke-Swap {
        if (-not (Test-Path -LiteralPath (Join-Path $Dir '.tpw-installer')) -and -not (Test-Path -LiteralPath (Join-Path $Dir 'channel.txt'))) {
            Fail "There is no install in $Dir."
        }
        $St.StepTotal = 2   # the switch and the shortcut
        if ($WaitPid) { $St.StepTotal += 1 }
        if (-not $NoLaunch) { $St.StepTotal += 1 }
        $Ui.Footer = 'Your games, saves and settings are not touched by an update.'
        if ($WaitPid) {
            Step 'Waiting for the launcher to close'
            Set-UiActive 'launcher' 'waiting for the launcher to close'
            Wait-Exit ([int]$WaitPid)
            Set-UiDone 'launcher' 'closed'
        }
        Step 'Switching to the new version'
        Set-UiActive 'swap' 'switching to the new version'
        if (Test-Path -LiteralPath (Join-Path $Next 'install-info.txt')) {
            Switch-Build
            [void](Write-Command)
        } else {
            Say "No new version is waiting in $Next, so there is nothing to switch."
            if (-not (Test-Path -LiteralPath (Join-Path $Dir 'build\tpw_launcher.exe'))) { Fail "There is no launcher in $(Join-Path $Dir 'build')." }
            $St.Version = Get-Info (Join-Path $Dir 'build\install-info.txt') 'version'
            Set-UiDone 'swap' 'nothing was waiting in build-next'
        }
        Step 'Adding STP/TPW Launcher to the Start menu'
        [void](Add-Shortcut)
        if (-not $NoLaunch) {
            Step 'Starting the launcher'
            Start-Launcher
        }
        $v = $St.Version
        if (-not $v) { $v = 'unknown' }
        $Ui.Closing = "Done in $(Format-Duration (Get-Elapsed $Ui.Start)). The launcher is version $v."
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
        # The progress view's rows. An install in place gets an "installed" row (and a swap row later) instead of the
        # tools row, as the design note draws them.
        $hadBuild = Test-Path -LiteralPath (Join-Path $Dir 'build\tpw_launcher.exe')
        $oldVersion = ''
        if ($hadBuild) { $oldVersion = Get-Info (Join-Path $Dir 'build\install-info.txt') 'version' }
        $oldShown = $oldVersion
        if (-not $oldShown) { $oldShown = 'unknown' }
        if ($hadBuild) {
            $Ui.Footer = 'Your games, saves and settings are not touched by an update.'
            Set-UiActive 'installed' "version $oldShown found"
        } else {
            $Ui.Footer = "All of this stays inside $(ConvertTo-Tilde $Dir)."
            Set-UiActive 'tools' 'looking for Git and Windows PowerShell'   # a fetch of Git, if needed, is shown by Get-MinGit
            Set-UiPending $Ui.Src ''
            Set-UiPending 'build' ''
            if (-not $Update) { Set-UiPending 'command' 'themepark' }
            if (-not $Stage) { Set-UiPending 'startmenu' 'STP/TPW Launcher' }
            if (-not $Stage -and -not $NoLaunch) { Set-UiPending 'start' '' }
        }
        # A dev install run without -Dev: the row says so at once; the plain line comes once a release is known
        # (Select-Target: with none, the install stays on main, and "it now follows the newest release" would be wrong).
        $switching = $St.Channel -eq 'stable' -and (Read-Channel) -eq 'dev'
        if ($switching) {
            Set-UiWarn 'installed' "this install follows main ($oldShown). Without -Dev the line switches it to the newest release. Add -Dev to stay on main."
        }

        # check, download, build, verify; then switch, the shortcut and the start unless -Stage
        $St.StepTotal = 4
        if (-not $Stage) { $St.StepTotal += 2 }   # the switch and the shortcut
        if (-not $Stage -and -not $NoLaunch) { $St.StepTotal += 1 }

        Step 'Checking this computer'
        Test-Computer
        if (-not $hadBuild -and -not $St.GitFetched) { Set-UiDone 'tools' 'Git and Windows PowerShell found' }

        Step "Downloading the source"
        Set-UiActive $Ui.Src 'looking for the newest version'
        if (-not $Stub) { Select-Target }
        try {
            [void](New-Item -ItemType Directory -Force -Path $Dir -ErrorAction Stop)
            Write-Text (Join-Path $Dir '.tpw-installer') ''
        } catch { Fail "Couldn't make the folder $Dir." }
        if ($Stub) {
            Info 'Test build: nothing is downloaded.'
            Set-UiActive $Ui.Src 'a test build: nothing is downloaded'
            $St.Version = '0.0.0-dev'
            if ($env:TPW_INSTALL_STUB_VERSION) { $St.Version = $env:TPW_INSTALL_STUB_VERSION }
            $St.Commit = "stub-$($St.Version)"
            $Ui.Version = $St.Version
        } else {
            # the log holds the downloads' output too in the progress view
            if ($Ui.Mode -ne 'plain') {
                try { Write-Text $Log '' } catch { Fail "Couldn't write $Log." }
                $St.LogStarted = $true
            }
            Get-Source
        }
        if ($switching) { Info 'This install followed main (-Dev); it now follows the newest release. Add -Dev to stay on main.' }

        $buildInfo = Join-Path $Dir 'build\install-info.txt'
        $nextInfo = Join-Path $Next 'install-info.txt'
        $current = (Test-Path -LiteralPath (Join-Path $Dir 'build\tpw_launcher.exe')) -and ((Get-Info $buildInfo 'commit') -eq $St.Commit)
        $ready = (-not $current) -and ((Get-Info $nextInfo 'commit') -eq $St.Commit) -and
                 ((Get-Info $nextInfo 'channel') -eq $St.Channel) -and (Test-Path -LiteralPath (Join-Path $Next 'tpw_launcher.exe'))

        # The progress view, now that the version is known: an install in place is current (two rows and done), or
        # gets the rows of an update; a fresh install's source row is done.
        if ($Ui.Mode -ne 'plain') {
            $shortCommit = $St.Commit
            if ($shortCommit.Length -gt 10) { $shortCommit = $shortCommit.Substring(0, 10) }
            if ($St.Channel -eq 'dev') {
                $got = "tpw-port main downloaded ($($St.Version), $shortCommit)"
                $newest = "main is at $($St.Version), which is installed"
            } else {
                $got = "tpw-port $($St.Version) downloaded"
                $newest = "$($St.Version) is the newest release"
            }
            $Ui.Got = $got
            $warned = (Get-UiState 'installed') -eq 'warn'
            if ($hadBuild -and $current) {
                $Ui.Footer = ''   # nothing happens to the install, so nothing to promise about it
                $shownVersion = $oldVersion
                if (-not $shownVersion) { $shownVersion = $St.Version }
                if (-not $warned) { Set-UiDone 'installed' "$(ConvertTo-Tilde $Dir), version $shownVersion" }
                Rename-UiRow $Ui.Src 'newest'
                Set-UiDone 'newest' $newest
            } elseif ($hadBuild) {
                if (-not $warned) { Set-UiDone 'installed' "version $oldShown found, updating to $($St.Version)" }
                Set-UiDone $Ui.Src $got
                Set-UiPending 'build' ''
                Set-UiPending 'swap' 'the new launcher replaces the old one when the build is done'
                if (-not $Update) { Set-UiPending 'command' 'themepark' }
                if (-not $Stage) { Set-UiPending 'startmenu' 'STP/TPW Launcher' }
                if (-not $Stage -and -not $NoLaunch) { Set-UiPending 'start' '' }
            } else {
                Set-UiDone $Ui.Src $got
            }
        }

        if ($current) {
            Info "Already up to date: version $($St.Version)."
            # A build staged earlier is of another commit now: drop it, so that a later -SwapAndLaunch cannot switch to it.
            Remove-Own 'build-next'
            Step 'Building the launcher'
            Info 'Nothing to build.'
            Step 'Checking the new launcher'
            Info 'Nothing to check.'
            if ((Read-Channel) -ne $St.Channel) {
                Write-Channel $St.Channel
                $lines = @(Get-Content -LiteralPath $buildInfo | ForEach-Object { if ($_ -like 'channel=*') { "channel=$($St.Channel)" } else { $_ } })
                Write-Text $buildInfo (($lines -join "`n") + "`n")
            }
        } elseif ($ready) {
            Info "This version was built earlier and is waiting in $Next."
            Step 'Building the launcher'
            Info 'Nothing to build.'
            Set-UiDone 'build' 'built earlier, waiting in build-next'
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
            if (-not $current) {
                Info "The new version is ready in $Next. Nothing was switched."
                $Ui.Closing = "The new version is ready in $Next. Nothing was switched."
            } else {
                $Ui.Closing = "Nothing to do: version $($St.Version) is installed."
            }
            Finish-Ok $St.Version
            return
        }
        Step 'Switching to the new version'
        if (-not $current) {
            if ($hadBuild) { Set-UiActive 'swap' 'switching to the new version' }
            Switch-Build
        } else {
            Info 'Nothing to switch.'
        }
        # THE THEMEPARK COMMAND: the file at every switch, the Path entry only on an install
        $play = "Start it with: $(Join-Path $Dir 'build\tpw_launcher.exe')"
        $everyWindow = 'From now on, type themepark in any new PowerShell window to play. It works from any folder.'
        $commandRow = 'themepark is not set up (see below)'
        if (-not $Update -and -not $current) { Set-UiActive 'command' 'adding the themepark command' }
        if (Write-Command) {
            $play = "To play later, run: $(Join-Path $Dir 'bin\themepark.cmd')"
            $commandRow = "themepark, from $(Join-Path $Dir 'bin') (see below)"
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
                    $commandRow = 'themepark, in any new PowerShell window'
                } else {
                    # not a failure of the install: the launcher is in place, only the shortcut needs this
                    $bin = (Join-Path $Dir 'bin') -replace "'", "''"
                    Say 'A new PowerShell window does not find themepark yet.'
                    Say 'To fix it, run this line once in PowerShell, then open a new window:'
                    Say "  [Environment]::SetEnvironmentVariable('Path', [Environment]::GetEnvironmentVariable('Path', 'User') + ';$bin', 'User')"
                    $commandRow = 'themepark, once the line below is run'
                }
            }
        }
        if ($Update -or $current) {
        } elseif ($commandRow -eq 'themepark, in any new PowerShell window') {
            Set-UiDone 'command' $commandRow
        } else {
            Set-UiWarn 'command' $commandRow
        }
        # THE START MENU SHORTCUT: written again at every install, update and switch
        Step 'Adding STP/TPW Launcher to the Start menu'
        $shortcut = Add-Shortcut
        if (-not $NoLaunch) {
            Step 'Starting the launcher'
            Start-Launcher
        }
        Info $play
        if (-not $Update) { Show-OldCopies }
        $closing = $play
        if ($play -eq $everyWindow) { $closing = 'Type themepark in a new PowerShell window to play.' }
        if ($shortcut -and $play -eq $everyWindow) {
            $closing = 'Open STP/TPW Launcher from the Start menu, or type themepark in a new PowerShell window, to play.'
        } elseif ($shortcut) {
            $closing = "Open STP/TPW Launcher from the Start menu to play. $play"
        }
        if ($current) { $Ui.Closing = "Nothing to do. $closing" }
        else { $Ui.Closing = "Done in $(Format-Duration (Get-Elapsed $Ui.Start)). $closing" }
        Finish-Ok $St.Version
    }

    # ---- main

    try {
        if ($Help) { Show-Usage; $St.Done = $true; return 0 }
        if ($Rest) { Fail "Unknown option $($Rest -join ' ') (-Help lists them)." }
        Start-Ui
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
            $unexpected = "Unexpected error: $("$_" -replace '\s+', ' ')"
            if ($Ui.Mode -eq 'plain') { Write-Host "RESULT fail $unexpected" } else { Show-UiFail $unexpected }
            $St.Done = $true
        }
        return 1
    } finally {
        # Ctrl+C runs this and nothing else: the one RESULT line still comes (the progress view: the Stopped text).
        if (-not $St.Done) {
            if ($Ui.Mode -eq 'plain') { Write-Host 'RESULT fail Stopped before it finished. The launcher in use was not changed.' }
            else { Show-UiStopped }
        }
    }
})[-1]

if ($null -eq $TpwInstallCode) { $TpwInstallCode = 1 }
$global:LASTEXITCODE = [int]$TpwInstallCode
if ($TpwInstallFromFile) { exit $TpwInstallCode }
Remove-Variable TpwInstallCode, TpwInstallFromFile -ErrorAction SilentlyContinue
