#!/bin/sh
# shellcheck disable=SC2317  # the exit after main (last line) is a guard: main always exits by itself
# Installs or updates the STP/TPW Launcher (tpw-port) on a Mac. It downloads the source and compiles it here.
#
#   curl -fsSL https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.sh | sh
#   curl -fsSL https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.sh | sh -s -- --dev
#
# Options (install.ps1 takes the same ones as -Dev, -Dir and so on): see usage below.
#
# The folder it makes (DIR, by default $HOME/Applications/STP-TPW-Launcher, which no clone of the repository uses and
# whose name has no spaces, which make dislikes):
#   source/          the git clone, with its submodules. It belongs to this script: do not edit it.
#   build/           the launcher in use (build/tpw_launcher)
#   build-next/      a new build being made, or made and waiting to be switched in
#   build-prev/      the build before the last switch, kept until the next one
#   bin/themepark    the themepark command: starts build/tpw_launcher from any folder, and themepark update runs this
#                    script from source/ (see THE THEMEPARK COMMAND)
#   channel.txt      "stable" or "dev": the launcher reads it to know that this script installed it; it records what
#                    is installed and never chooses what an update gets
#   last-build.log   the output of the last build (on a terminal, of the downloads too), with no user name, home folder
#                    or computer name in it
#   .tpw-installer   marks the folder as this script's, so that it never writes into a folder that is somebody else's
# Game files and saves are not in DIR (they are in the launcher's data folder, ~/Library/Application Support/
# STP-TPW Launcher), and this script never touches them.
#
# ONE LINE FOR EVERYONE. The same line installs, and run again it updates. Without --dev it always follows the newest
# release, also on an install made with --dev, which it switches back and says so (the maintainer, 2026-10-25: a player is on main
# only by asking for it each time). --update, the launcher's Update button, does the same: without --dev on that run
# it gets the newest release, whatever channel.txt says, and the launcher never passes --dev. A clone that a player
# made earlier to play from (the README's old way, usually ~/tpw-port) is never touched or used; at the end the player
# is told it is no longer needed.
#
# THE CONTRACT. The launcher's updater runs this script and reads its output, so these never change:
#   STEP n/N some plain words     one line as each step starts
#   RESULT ok VERSION             the last line when it worked, and the exit code is 0 (VERSION: 0.1.0, 0.1.1-dev)
#   RESULT fail SOME REASON       the last line when it did not, and the exit code is not 0
# Exactly one RESULT line is printed, last. Other lines are for people and may change. Options are only ever added.
# These are the lines of plain mode, which is what the updater gets (the output is a pipe). Run from a Terminal window
# the script shows the same run as a progress view instead (see "output on a terminal" below), and --plain asks for the
# lines anyway.
#
# WHY IT BUILDS FROM SOURCE. A program downloaded from the internet is blocked by macOS (Gatekeeper) and Windows
# (SmartScreen) unless its maker signed it with a paid certificate. A program the computer compiled itself is not marked
# as downloaded, so there is nothing to block and nothing to sign. The price is a few minutes of compiling.
#
# WHY NOTHING IS SWITCHED UNTIL THE END (stage, then swap). The launcher in use is DIR/build. A new version is built in
# another folder, DIR/build-next, and is switched in only after it built and answered --version with the right version.
# Whatever goes wrong before that (no internet, a compiler error, a full disk, a power cut) leaves the working launcher
# as it was. The switch itself is two renames. The launcher cannot be replaced while it runs (on Windows a running
# program's folder cannot be renamed), so the updater stages first, then quits and lets --swap-and-launch --wait-pid
# switch and start the new one.
#
# WHY THE DOWNLOADS COME FIRST. The clone, the submodules and the build's own download (a CMake, when the Mac has none)
# all finish before the compile starts. A lost connection is then found within a minute, said in plain words, and costs
# nothing; it is not found after ten minutes of compiling.
#
# THE THEMEPARK COMMAND. Players start the game by typing themepark in any Terminal window. DIR/bin/themepark starts
# DIR/build/tpw_launcher (where the Update button is) and returns at once. Three arguments are different: `themepark
# update` runs this script from DIR/source in the foreground (--update --dir DIR, any further options passed on, so
# `themepark update --dev` follows main), with the progress view in the window, and first refuses when this install's
# launcher is running (the Update button updates a running launcher; this run replaces build/ under it and would start a
# second one); `themepark --version` and `themepark --help` run the launcher in the foreground, so that its answer shows
# in the window. DIR/bin gets on the PATH of new Terminal
# windows through one line in ~/.zprofile (zsh is macOS's shell), and in bash's login file when bash is the player's
# shell (or ~/.bash_profile when there is one), marked with CMD_MARK so that it is found again and never added twice.
# `curl | sh` cannot change the window it runs in, so the command works from the next window on (the first install
# starts the launcher itself). At the end a new login shell of the player's own shell checks that it finds themepark;
# if it does not, the player is given the one line that fixes it (the install has still succeeded). Only an install
# (not --update or --swap-and-launch) writes that line: an update never brings back a line the player removed. A
# themepark command this script did not make is never shadowed: the line is then not written and the player is told
# the full path instead.
#
# THE APPLICATION. Players also open the game from an icon (the maintainer, 2026-10-25): every install, update and swap puts the
# application "STP/TPW Launcher" in /Applications (on disk "STP:TPW Launcher.app": a colon in a file's name is what the
# Finder shows as a slash), so Launchpad, Spotlight, the Finder and the Dock all have it. /Applications belongs to the
# Mac's administrators as a group, so an administrator writes there with no password; for anyone else it goes in the
# player's own ~/Applications, which Launchpad and Spotlight search too, and the player is told. The application is
# small and the install folder is not in it: an Info.plist (the same name and identifier as the launcher's own,
# launcher/macos/Info.plist.in), the icon as an .icns, and as its program a few lines of shell that start
# DIR/build/tpw_launcher in DIR, exactly as the themepark command does (write_app below; the launcher then runs from the
# bundle it keeps beside itself, launcher/app_bundle.h, as it does from themepark). So an update that replaces
# DIR/build changes nothing in the application, and a launcher that is gone (the install folder deleted) gets a plain
# message to run the installer again. The icon is the launcher's own (tpw_launcher --write-icon: the game's icon once a
# game is imported, the launcher's stand-in before; app_icon.h), turned into an .icns by Apple's sips and iconutil,
# which every Mac has. The icon in the Finder and Launchpad is the one of the last install or update; the Dock's while
# the launcher runs is always the current one. The application is written again at every run and is only ever replaced
# when it is this script's (APP_MARK in its program); nothing else in /Applications is touched or deleted. It is made
# by this script on this Mac, never downloaded, so Gatekeeper has nothing to say about it. To remove it, drag it to the
# Trash (it comes back at the next install or update).
#
# SAFETY. No sudo, nothing written outside DIR but that one marked line, the application STP/TPW Launcher in
# /Applications (or ~/Applications) and, when missing, the folder DIR is in (the default's ~/Applications); the launcher
# is started at the end, from DIR/build. The only folders deleted are DIR/build-next and DIR/build-prev, by remove_own
# below, which refuses any other name, an odd install folder and a link, and the scratch folder of the application's
# icon (mktemp's). Nothing is installed on the Mac: Apple's Command Line Tools must be there already, and the build
# fetches a CMake into its own folder when needed.
#
# Test hooks (for the maintainers' tests, not for players): TPW_INSTALL_REPO replaces the repository URL,
# TPW_INSTALL_HOME the folder that holds .zprofile and .bash_profile, TPW_INSTALL_APPS the folder the application goes
# in (in place of /Applications and the fallback), and TPW_INSTALL_STUB=1 puts a stand-in launcher in
# place of the download and the build (the installer's CI tests the themepark command with it), of version
# TPW_INSTALL_STUB_VERSION when set (a newer one stands in for a new release). TPW_INSTALL_SCRUB=FILE runs only the log
# scrubber (devtools/test_install_log_scrub.sh), TPW_INSTALL_HOSTNAME stands in for this Mac's name there.
#
# Everything is inside functions and the last line calls main. That matters for `curl ... | sh`: the shell reads this
# file while it runs, so a half-downloaded file runs nothing, and a program that reads standard input cannot eat the
# rest of the script (every command that could is given </dev/null). Keep the call to main the only statement outside
# a function, on the same line as its exit: the updater may run DIR/source/install.sh, which the checkout replaces
# while the script is still running.

usage() {
    cat <<'EOF'
Installs or updates the STP/TPW Launcher, compiled on this Mac from its source.

  curl -fsSL https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.sh | sh
  curl -fsSL https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.sh | sh -s -- --dev

Options:
  --dev                 follow main (the next version, still being made) instead of the newest release
  --stable              follow the newest release (the default; switches an install made with --dev back)
  --dir PATH            the install folder (default: $HOME/Applications/STP-TPW-Launcher)
  --update              update an existing install without asking anything (to the newest release unless --dev)
  --stage               with --update: build the new version into DIR/build-next and stop there
  --swap-and-launch     switch DIR/build-next in as DIR/build (the old one is kept as DIR/build-prev), then start it
  --wait-pid PID        with --swap-and-launch: first wait for that process (the running launcher) to exit
  --no-launch           do not start the launcher at the end
  --plain               one line per step and the build's whole output, no colour, no redrawing (what a file gets)
  --help                this text

After installing, open STP/TPW Launcher from your Applications folder (Launchpad, Spotlight or the Finder), or open
a new Terminal window and type themepark, from any folder, to play.
Run the same line again, or type themepark update, to update. Both follow the newest release unless --dev is added
each time; so does the launcher's Update button, which never adds --dev.
EOF
}

# ---- output: the lines the launcher reads (plain mode)
#
# Two ways of showing the same run. Plain mode (UI=plain: the output is not a terminal, --plain, NO_COLOR, TERM=dumb or
# a window narrower than 60 columns) prints the lines below and the build's output as it comes: that is what the
# updater, the CI and a log get, and it never changes. On a terminal (UI=tty) the same run is one block that redraws
# in place (the progress view further down): a row per stage, a bar for the build, the build's output only in the log.

STEP_NO=0
STEP_TOTAL=0

step() {
    STEP_NO=$((STEP_NO + 1))
    [ "$UI" = tty ] || printf 'STEP %s/%s %s\n' "$STEP_NO" "$STEP_TOTAL" "$*"
}

# A line for people. On a terminal it is kept and shown under the block at the end (a notice, or what went wrong).
say() {
    if [ "$UI" = tty ]; then
        UI_NOTES="$UI_NOTES$*
"
    else
        printf '%s\n' "$*"
    fi
}

# A line of plain mode only: the progress view shows the same thing in its rows.
info() {
    [ "$UI" = tty ] || printf '%s\n' "$*"
}

# The stop traps: Ctrl+C, the updater's TERM, a closed terminal.
arm_traps() {
    trap 'stopped INT' INT
    trap 'stopped TERM' TERM
    trap 'stopped HUP' HUP
}

disarm_traps() {
    trap - INT TERM HUP
}

die() {
    disarm_traps
    if [ "$UI" = tty ]; then
        ui_fail "$*"
    else
        printf 'RESULT fail %s\n' "$*"
    fi
    exit 1
}

finish_ok() {
    disarm_traps
    if [ "$UI" = tty ]; then
        ui_finish
    else
        printf 'RESULT ok %s\n' "$1"
    fi
    exit 0
}

# shellcheck disable=SC2329  # called by the traps in main, with the signal's name
stopped() {
    # in the middle of a frame: the frame finishes first (ui_draw then calls this again), or the cursor would be on an
    # unknown line
    if [ "$UI_DRAWING" = 1 ]; then
        UI_SIGNAL=$1
        return 0
    fi
    disarm_traps
    if [ "$UI" = tty ]; then
        stop_children
        ui_stopped
        case "$1" in INT) exit 130 ;; TERM) exit 143 ;; *) exit 129 ;; esac
    fi
    printf '\n'
    stop_children
    die "Stopped before it finished. The launcher in use was not changed."
}

# ---- output on a terminal: the progress view
#
# One block, redrawn in place, in a frame of box-drawing characters (rounded corners, a blank line inside at the top
# and the bottom): a title (the name and, once known, the version it installs), a row per stage (a green check when
# done, a spinner while it runs, grey while it waits, a yellow ! for a notice, a red x when it failed), a blank line
# and a footer. The build's row is a bar with the percentage and the file being compiled, read from the log (cmake's
# "[ 58%] Building CXX object ..." lines); until the first percentage is in the log (the configure step) it says what
# the build is doing instead ("configuring the build"), never a 0 % that reads as stuck. The cursor is hidden while the
# block redraws and shown again before anything else is printed. Rows are only ever added, the names are those of the
# design note (tools, source, build, command, start; installed, newest, swap on an update). Without a UTF-8 locale (or
# a shell that counts bytes) the glyphs are ASCII ([x] done, [>] active, [ ] waiting, a frame of + - |). The frame is
# as wide as the window less one column, at most 110; under 72 columns inside it the bar has 10 cells and the time
# left is dropped; under 60 columns there is no frame (and the view is not chosen to begin with). The width is read
# again at every frame: after a resize the block starts afresh below the old one rather than drawing over the wrong
# lines.

UI=plain
UI_SRC=source
UI_ROWS=0
UI_LINES=0
UI_COLS=80
UI_LAST_COLS=0
UI_BOX=0
UI_IN=0
UI_TW=80
UI_RULE=""
UI_RULE_W=-1
UI_STAGE=0
UI_FRAME=0
UI_TICK=""
UI_VERSION=""
UI_FOOTER=""
UI_GOT=""
UI_DRAWING=0
UI_SIGNAL=""
UI_NOTES=""
UI_FAIL_TEXT=""
UI_FAIL_DONE=0
UI_START=0
UI_BUILD_START=0
UI_BUILD_T0=0
UI_BUILD_P0=0

# Chooses the view, once the options are read. Sets up the colours and glyphs of the terminal view.
ui_start() {
    UI=plain
    [ "$PLAIN" = 0 ] || return 0
    [ -t 1 ] || return 0
    [ -z "${NO_COLOR:-}" ] || return 0
    case "${TERM:-}" in '' | dumb) return 0 ;; esac
    ui_cols
    [ "$UI_COLS" -ge 60 ] || return 0
    UI="tty"
    UI_LAST_COLS=$UI_COLS
    ESC=$(printf '\033')
    RST="${ESC}[0m"
    BOLD="${ESC}[1m"
    DIM="${ESC}[38;5;243m"
    GREEN="${ESC}[32m"
    CYAN="${ESC}[36m"
    YELLOW="${ESC}[33m"
    RED="${ESC}[31m"
    CLR="${ESC}[K"
    case "${LC_ALL:-${LC_CTYPE:-${LANG:-}}}" in
        *[Uu][Tt][Ff]-8* | *[Uu][Tt][Ff]8*) ui_glyphs utf ;;
        *) ui_glyphs ascii ;;
    esac
    # a shell that counts bytes rather than characters would put the frame's right edge off by the glyphs: ASCII then
    [ "$UI_UTF" = 0 ] || [ "${#G_DONE}" = 1 ] || ui_glyphs ascii
    # the text column: 4 spaces, the glyph, 2 spaces, the name in 13, a space
    UI_PAD=$((4 + ${#G_DONE} + 2 + 13 + 1))
    UI_IND=""
    _k=0
    while [ "$_k" -lt "$UI_PAD" ]; do UI_IND="$UI_IND "; _k=$((_k + 1)); done
    ui_geom
    UI_START=$(date +%s)
    printf '%s[?25l' "$ESC"
}

# The glyphs of the view: those of the design note (utf), or ASCII stand-ins.
ui_glyphs() {
    if [ "$1" = utf ]; then
        UI_UTF=1 G_DONE='✔' G_PEND='·' G_WARN='!' G_FAIL='✖' B_ON='█' B_OFF='░'
        BX_H='─' BX_V='│' BX_TL='╭' BX_TR='╮' BX_BL='╰' BX_BR='╯'
    else
        UI_UTF=0 G_DONE='[x]' G_PEND='[ ]' G_WARN='[!]' G_FAIL='[x]' B_ON='#' B_OFF='.'
        BX_H='-' BX_V='|' BX_TL='+' BX_TR='+' BX_BL='+' BX_BR='+'
    fi
}

# The frame for the width in UI_COLS: UI_BOX (1 when the block gets one: 60 columns or more), UI_IN (the columns inside
# it), UI_TW (the columns a line of the block may use: inside the frame, or the whole window without one) and UI_RULE
# (its top and bottom edge, without the corners).
ui_geom() {
    if [ "$UI_COLS" -ge 60 ]; then
        UI_BOX=1
        _out=$((UI_COLS - 1))
        [ "$_out" -le 110 ] || _out=110
        UI_IN=$((_out - 2))
        UI_TW=$UI_IN
    else
        UI_BOX=0
        UI_IN=0
        UI_TW=$UI_COLS
    fi
    if [ "$UI_RULE_W" != "$UI_IN" ]; then
        UI_RULE=""
        _k=0
        while [ "$_k" -lt "$UI_IN" ]; do UI_RULE="$UI_RULE$BX_H"; _k=$((_k + 1)); done
        UI_RULE_W=$UI_IN
    fi
}

# The width on the screen of $1, a line of the block, in UI_W: its characters less the escape codes (which all end in
# a letter).
ui_width() {
    _s=$1
    UI_W=0
    while :; do
        case "$_s" in
            *"$ESC"*)
                _h=${_s%%"$ESC"*}
                UI_W=$((UI_W + ${#_h}))
                _s=${_s#*"$ESC"}
                _s=${_s#\[*[mKlhA]} ;;
            *)
                UI_W=$((UI_W + ${#_s}))
                break ;;
        esac
    done
}

# The terminal's width: from the terminal itself (standard input is a pipe under curl | sh), else tput, else 80.
ui_cols() {
    size=$(stty size 2>/dev/null </dev/tty) || size=""
    UI_COLS=${size#* }
    case "$UI_COLS" in '' | *[!0-9]*) UI_COLS=$(tput cols 2>/dev/null) ;; esac
    case "$UI_COLS" in '' | *[!0-9]* | 0) UI_COLS=80 ;; esac
}

# $1 seconds -> DUR ("48 s", "3 min 40 s", "3 min").
fmt_dur() {
    if [ "$1" -lt 60 ]; then
        DUR="$1 s"
    elif [ $(($1 % 60)) = 0 ]; then
        DUR="$(($1 / 60)) min"
    else
        DUR="$(($1 / 60)) min $(($1 % 60)) s"
    fi
}

# $1 with the home folder written ~, for the rows (the log and plain mode keep paths as they are).
tilde() {
    _s=$1
    _o=""
    if [ "${#HOME}" -gt 1 ]; then
        while :; do
            case "$_s" in
                *"$HOME"*) _o="$_o${_s%%"$HOME"*}~"; _s=${_s#*"$HOME"} ;;
                *) break ;;
            esac
        done
    fi
    printf '%s\n' "$_o$_s"
}

# The row named $1: its index in UI_I, or 0 and false when there is none.
ui_find() {
    UI_I=0
    _n=""
    _i=1
    while [ "$_i" -le "$UI_ROWS" ]; do
        eval "_n=\$UI_N_$_i"
        if [ "$_n" = "$1" ]; then
            UI_I=$_i
            return 0
        fi
        _i=$((_i + 1))
    done
    return 1
}

# ui_set NAME STATE TEXT: the row's state (done, active, pending, warn, fail) and text; a new row goes at the end.
ui_set() {
    [ "$UI" = tty ] || return 0
    if ! ui_find "$1"; then
        UI_ROWS=$((UI_ROWS + 1))
        UI_I=$UI_ROWS
        eval "UI_N_$UI_I=\$1"
        eval "UI_P_$UI_I=\$(printf '%-13s' \"\$1\")"
    fi
    eval "UI_S_$UI_I=\$2"
    eval "UI_T_$UI_I=\$3"
    ui_draw
}

ui_active() { ui_set "$1" active "$2"; }
ui_done() { ui_set "$1" "done" "$2"; }
ui_pending() { ui_set "$1" pending "$2"; }
ui_warn() { ui_set "$1" warn "$2"; }

# The state of the row named $1 -> UI_S (empty when there is none).
ui_state() {
    UI_S=""
    [ "$UI" = tty ] || return 0
    ui_find "$1" || return 0
    eval "UI_S=\$UI_S_$UI_I"
}

# The row named $1 is called $2 from now on.
ui_rename() {
    [ "$UI" = tty ] || return 0
    ui_find "$1" || return 0
    eval "UI_N_$UI_I=\$2"
    eval "UI_P_$UI_I=\$(printf '%-13s' \"\$2\")"
}

# The row that runs now: its index in UI_I, or 0.
ui_active_row() {
    UI_I=0
    _i=$UI_ROWS
    while [ "$_i" -ge 1 ]; do
        eval "_s=\$UI_S_$_i"
        if [ "$_s" = active ]; then
            UI_I=$_i
            return 0
        fi
        _i=$((_i - 1))
    done
    return 1
}

ui_spin() {
    if [ "$UI_UTF" = 0 ]; then
        SPIN='[>]'
        return 0
    fi
    case $((UI_FRAME % 10)) in
        0) SPIN='⠋' ;; 1) SPIN='⠙' ;; 2) SPIN='⠹' ;; 3) SPIN='⠸' ;; 4) SPIN='⠼' ;;
        5) SPIN='⠴' ;; 6) SPIN='⠦' ;; 7) SPIN='⠧' ;; 8) SPIN='⠇' ;; *) SPIN='⠏' ;;
    esac
}

# One line of the block, cleared to the right: inside the frame when there is one (padded to its right edge; a line
# wider than the frame, which the layout never makes, pushes the edge rather than being cut).
ui_line() {
    if [ "$UI_BOX" = 1 ]; then
        ui_width "$1"
        _fill=""
        while [ "$UI_W" -lt "$UI_IN" ]; do _fill="$_fill "; UI_W=$((UI_W + 1)); done
        printf '%s%s%s%s%s%s%s%s%s\n' "$DIM" "$BX_V" "$RST" "$1" "$_fill" "$DIM" "$BX_V" "$RST" "$CLR"
    else
        printf '%s%s\n' "$1" "$CLR"
    fi
    UI_LINES=$((UI_LINES + 1))
}

# The frame's top ($1 = top) or bottom edge.
ui_edge() {
    [ "$UI_BOX" = 1 ] || return 0
    if [ "$1" = top ]; then
        printf '%s%s%s%s%s%s\n' "$DIM" "$BX_TL" "$UI_RULE" "$BX_TR" "$RST" "$CLR"
    else
        printf '%s%s%s%s%s%s\n' "$DIM" "$BX_BL" "$UI_RULE" "$BX_BR" "$RST" "$CLR"
    fi
    UI_LINES=$((UI_LINES + 1))
}

# A row's text in lines no wider than $3, the first after $1, the others after the indent (and $2 before each, for a
# grey row). The text is split at spaces; a word wider than a line is clipped, so that a line never wraps by itself.
ui_wrap() {
    _pre=$1
    _cpre=$2
    _w=$3
    _text=$4
    _ind=${5:-$UI_IND}
    _line=""
    _first=1
    set -f
    for _wd in $_text; do
        if [ "${#_wd}" -gt "$_w" ]; then
            _wd=$(printf '%s\n' "$_wd" | awk -v n="$_w" '{ print substr($0, 1, n - 3) "..." }')
        fi
        if [ -z "$_line" ]; then
            _line=$_wd
        elif [ $((${#_line} + 1 + ${#_wd})) -le "$_w" ]; then
            _line="$_line $_wd"
        else
            ui_wrap_line
            _line=$_wd
        fi
    done
    set +f
    ui_wrap_line
}

ui_wrap_line() {
    if [ "$_first" = 1 ]; then
        ui_line "$_pre$_line$RST"
        _first=0
    else
        ui_line "$_ind$_cpre$_line$RST"
    fi
}

# ui_row STATE NAME TEXT (the name padded to 13)
ui_row() {
    case "$1" in
        done) _g="${GREEN}${G_DONE}${RST}" ;;
        active) ui_spin; _g="${CYAN}${SPIN}${RST}" ;;
        pending) _g="${DIM}${G_PEND}${RST}" ;;
        warn) _g="${YELLOW}${G_WARN}${RST}" ;;
        *) _g="${RED}${G_FAIL}${RST}" ;;
    esac
    if [ "$1" = pending ]; then
        _p="    $_g  $DIM$2 "
        _c=$DIM
    else
        _p="    $_g  $BOLD$2$RST "
        _c=""
    fi
    case "$3" in
        *"$ESC"*) ui_line "$_p$3" ;;   # the bar: laid out by ui_build_tick to fit the line
        *) ui_wrap "$_p" "$_c" $((UI_TW - UI_PAD - 1)) "$3" ;;
    esac
}

# Draws the block, over the last frame when there is one.
ui_draw() {
    [ "$UI" = tty ] || return 0
    UI_DRAWING=1
    ui_cols
    if [ "$UI_COLS" != "$UI_LAST_COLS" ]; then
        [ "$UI_LINES" = 0 ] || printf '\n'
        UI_LINES=0
        UI_LAST_COLS=$UI_COLS
    fi
    ui_geom
    _prev=$UI_LINES
    [ "$_prev" = 0 ] || printf '%s[%sA\r' "$ESC" "$_prev"
    UI_LINES=0
    ui_edge top
    [ "$UI_BOX" = 1 ] && ui_line ""
    ui_line "  ${BOLD}STP/TPW Launcher${UI_VERSION:+ $UI_VERSION}${RST}"
    ui_line ""
    _r=1
    while [ "$_r" -le "$UI_ROWS" ]; do
        eval "_n=\$UI_P_$_r"
        eval "_s=\$UI_S_$_r"
        eval "_t=\$UI_T_$_r"
        ui_row "$_s" "$_n" "$_t"
        _r=$((_r + 1))
    done
    ui_line ""
    ui_wrap "  " "" $((UI_TW - 3)) "$UI_FOOTER" "  "
    [ "$UI_BOX" = 1 ] && ui_line ""
    ui_edge bottom
    # a frame shorter than the one before: what is left of the old one is cleared
    if [ "$UI_LINES" -lt "$_prev" ]; then
        _k=$UI_LINES
        while [ "$_k" -lt "$_prev" ]; do printf '%s\n' "$CLR"; _k=$((_k + 1)); done
        printf '%s[%sA\r' "$ESC" "$((_prev - UI_LINES))"
    fi
    UI_DRAWING=0
    if [ -n "$UI_SIGNAL" ]; then
        _sig=$UI_SIGNAL
        UI_SIGNAL=""
        stopped "$_sig"
    fi
}

# The bar for $1 percent -> BAR (and its width in cells -> BAR_W).
ui_bar() {
    BAR_W=20
    [ "$UI_TW" -ge 72 ] || BAR_W=10
    _f=$(($1 * BAR_W / 100))
    BAR=$CYAN
    _k=0
    while [ "$_k" -lt "$_f" ]; do BAR="$BAR$B_ON"; _k=$((_k + 1)); done
    BAR="$BAR$DIM"
    while [ "$_k" -lt "$BAR_W" ]; do BAR="$BAR$B_OFF"; _k=$((_k + 1)); done
    BAR="$BAR$RST"
}

# Where the build is, from the log: UI_PCT (the last progress line's percentage; empty before the first one), UI_FILE
# (the source file being compiled, or "linking tpw_launcher") and UI_STAGE, what the build is doing before the
# compiler's first progress line (0 nothing yet, 1 cmake configures, 2 the compiler is starting), which only advances.
ui_build_status() {
    _p=$(tail -n 80 "$LOG" 2>/dev/null | LC_ALL=C awk '
        /^\[ *[0-9]+%\]/ {
            p = $0; sub(/^\[ */, "", p); sub(/%\].*/, "", p)
            t = $0; sub(/^\[[^]]*\] */, "", t)
            if (t ~ /^Building .* object /) {
                sub(/^Building .* object /, "", t); sub(/CMakeFiles\/[^\/]*\.dir\//, "", t); sub(/\.o$/, "", t); f = t
            } else if (t ~ /^Linking /) {
                sub(/^Linking [^ ]* [^ ]* /, "", t); f = "linking " t
            }
            pct = p + 0
        }
        /^==> Configuring/ || /^-- / { if (s < 1) s = 1 }
        /^-- (Configuring|Generating) done/ || /^-- Build files have been written/ || /^==> Building/ || /^Scanning dependencies/ { if (s < 2) s = 2 }
        END { print pct "|" (s + 0) "|" f }')
    UI_PCT=${_p%%|*}
    _p=${_p#*|}
    _s=${_p%%|*}
    UI_FILE=${_p#*|}
    case "$UI_PCT" in *[!0-9]*) UI_PCT="" ;; esac
    [ -z "$UI_PCT" ] || [ "$UI_PCT" -le 100 ] || UI_PCT=100
    [ "$_s" -le "$UI_STAGE" ] 2>/dev/null || UI_STAGE=$_s
}

# The build's row at every frame. Until the compiler's first percentage is in the log: the words of what the build is
# doing (a "0 %" that stays through the configure step reads as stuck), no bar, no time left. Then the bar, the
# percentage, the time left (from the pace so far, once 10 points are done) and the file being compiled, in what is
# left of the line.
# shellcheck disable=SC2329  # called through UI_TICK
ui_build_tick() {
    ui_build_status
    if [ -z "$UI_PCT" ] || [ "$UI_PCT" = 0 ]; then
        if [ -n "$UI_PCT" ] && [ -n "$UI_FILE" ]; then
            _t="compiling $UI_FILE"
        else
            case "$UI_STAGE" in
                0) _t="preparing the build" ;;
                1) _t="configuring the build" ;;
                *) _t="starting the compiler" ;;
            esac
        fi
        ui_set build active "$_t"
        return 0
    fi
    _now=$(date +%s)
    if [ "$UI_BUILD_T0" = 0 ]; then
        UI_BUILD_T0=$_now
        UI_BUILD_P0=$UI_PCT
    fi
    ui_bar "$UI_PCT"
    if [ "$UI_PCT" -lt 10 ]; then _pc=" $UI_PCT"; else _pc=$UI_PCT; fi
    _t="$BAR  $_pc %"
    _used=$((BAR_W + 6))
    _gain=$((UI_PCT - UI_BUILD_P0))
    if [ "$UI_TW" -ge 72 ] && [ "$_gain" -ge 10 ] && [ $((_now - UI_BUILD_T0)) -ge 5 ] && [ "$UI_PCT" -lt 100 ]; then
        fmt_dur $(((_now - UI_BUILD_T0) * (100 - UI_PCT) / _gain))
        _t="$_t   about $DUR left"
        _used=$((_used + 3 + 6 + ${#DUR} + 5))
    fi
    _room=$((UI_TW - UI_PAD - _used - 4))
    if [ -n "$UI_FILE" ] && [ "$_room" -ge 8 ]; then
        _f=$UI_FILE
        if [ "${#_f}" -gt "$_room" ]; then
            _f=$(printf '%s\n' "$_f" | awk -v n="$_room" '{ print "..." substr($0, length($0) - n + 4) }')
        fi
        _t="$_t   ${DIM}$_f${RST}"
    fi
    ui_set build active "$_t"
}

# Waits for the background job $1, redrawing the block a few times a second (the spinner, the build's bar).
ui_wait() {
    while kill -0 "$1" 2>/dev/null; do
        UI_FRAME=$((UI_FRAME + 1))
        if [ -n "$UI_TICK" ]; then $UI_TICK; else ui_draw; fi
        sleep 0.25
    done
    wait "$1"
}

# Prints the notes (the lines say kept) under the block, in $1.
# The notes (the lines say kept) under the block, in $1 (a colour, or ""): a line that starts with a space (a command,
# a list item) as it is; a line that does not end a sentence is joined to the next; each paragraph wrapped to the
# block's text width and indented two spaces, with a blank line only between paragraphs (a word wider than a line, a
# path, is kept whole).
ui_notes() {
    [ -n "$UI_NOTES" ] || return 0
    printf '\n'
    printf '%s\n' "$UI_NOTES" | {
        _para=""
        _gap=0
        _any=0
        while IFS= read -r _l; do
            case "$_l" in
                "") ui_note_flush "$1"; [ "$_any" = 0 ] || _gap=1; continue ;;
                " "*) ui_note_flush "$1"; _para=$_l; ui_note_flush "$1" raw; continue ;;
            esac
            _l=$(tilde "$_l")
            _para="${_para:+$_para }$_l"
            case "$_l" in *[.:?!\"]) ui_note_flush "$1" ;; esac
        done
    }
}

# ui_note_flush COLOUR [raw]: the paragraph gathered so far, if any, wrapped (or as it is when raw).
ui_note_flush() {
    [ -n "$_para" ] || return 0
    [ "$_gap" = 0 ] || printf '\n'
    _gap=0
    _any=1
    if [ "$2" = raw ]; then
        printf '  %s%s%s\n' "$1" "$(tilde "$_para")" "$RST"
    else
        _w=$((UI_TW - 2))
        _line=""
        set -f
        for _wd in $_para; do
            if [ -z "$_line" ]; then
                _line=$_wd
            elif [ $((${#_line} + 1 + ${#_wd})) -le "$_w" ]; then
                _line="$_line $_wd"
            else
                printf '  %s%s%s\n' "$1" "$_line" "$RST"
                _line=$_wd
            fi
        done
        set +f
        [ -z "$_line" ] || printf '  %s%s%s\n' "$1" "$_line" "$RST"
    fi
    _para=""
}

# The last lines of the log and where it is, after a failure of the downloads or the build.
ui_log_tail() {
    [ -s "$LOG" ] || return 0
    printf '\n  The last lines of its output:\n'
    tail -n 15 "$LOG" | while IFS= read -r _l; do
        printf '      %s%s%s\n' "$DIM" "$_l" "$RST"
    done
    printf '\n  The whole output is in %s (it holds nothing personal).\n' "$(tilde "$LOG")"
    printf '  Please attach it to a bug report: https://github.com/ThemeParkWorldDecomp/tpw-port/issues\n'
    printf '  Run the line again with --plain to see everything.\n'
}

# The block's last frame, with the cursor back. $1: the state and text of the row that was running (none: left as is).
ui_close() {
    if [ -n "$1" ] && ui_active_row; then
        eval "UI_S_$UI_I=\$1"
        eval "UI_T_$UI_I=\$2"
    fi
    if [ "$UI_ROWS" -gt 0 ] || [ "$UI_LINES" -gt 0 ]; then
        ui_draw
    fi
    printf '%s[?25h' "$ESC"
}

# What went wrong: the running row turns red with the first sentence of the reason, then the notes, then the log's
# last lines when a download or the build failed.
ui_fail() {
    if [ "$UI_FAIL_DONE" = 1 ]; then
        ui_notes ""
        return 0
    fi
    _reason=$1
    _short=${_reason%%. *}
    _short=${_short%.}
    if ui_active_row; then
        eval "_name=\$UI_N_$UI_I"
        ui_close fail "${UI_FAIL_TEXT:-$(tilde "$_short")}"
    else
        _name=""
        ui_close
        printf '\n    %s%s%s  %s\n' "$RED" "$G_FAIL" "$RST" "$(tilde "$_short")"
    fi
    ui_notes ""
    case "$_name" in source | build | newest) ui_log_tail ;; esac
    printf '\n'
}

# The run worked: the last frame, the notes (yellow) and the closing line (green).
ui_finish() {
    ui_close
    ui_notes "$YELLOW"
    printf '\n  %s%s%s\n\n' "$GREEN" "$(tilde "$UI_CLOSING")" "$RST"
}

# shellcheck disable=SC2329  # called by stopped
ui_stopped() {
    # The block stays as it was and the text goes under it. (No last frame: a Ctrl+C that lands while a line of the
    # frame is being written makes the shell write the end of that line twice, so the block can be a line taller than
    # drawn, and moving up to redraw it would miss by one.)
    printf '%s[?25h\r%s\n' "$ESC" "$CLR"
    printf '  %sStopped. Nothing is half installed: the new build was in its own staging folder%s\n' "$YELLOW" "$RST"
    printf '  %sand the old launcher is as it was. Run the line again to continue.%s\n\n' "$YELLOW" "$RST"
}

# Ends everything this script started that still runs (a download, the build with its compilers): the updater stops
# only the script itself, and Ctrl+C reaches only the commands in the foreground.
# shellcheck disable=SC2329  # called by stopped
stop_children() {
    kids=$(ps -A -o pid= -o ppid= 2>/dev/null | awk -v top="$$" '
        { parent[$1] = $2 }
        END {
            do {
                n = 0
                for (p in parent) if (!(p in mine) && (parent[p] == top || parent[p] in mine)) { mine[p] = 1; n++ }
            } while (n)
            for (p in mine) print p
        }')
    # shellcheck disable=SC2086  # one number per word
    [ -z "$kids" ] || kill -TERM $kids 2>/dev/null
}

# Runs a long command (a download, the build) in the background and waits for it. A stop (the updater's TERM, Ctrl+C)
# then runs stopped at once, which ends the command, instead of only after the command finished by itself.
wait_for() {
    "$@" &
    if [ "$UI" = tty ]; then
        ui_wait "$!"
    else
        wait "$!"
    fi
}

# A download, with its output on the screen in plain mode and only in the log in the progress view.
fetch_logged() {
    if [ "$UI" = tty ]; then
        wait_for logged "$@"
    else
        wait_for "$@"
    fi
}

# Runs a command with its output appended to the log, scrubbed, and nothing on the screen; its exit code in $RC and
# as the result.
# shellcheck disable=SC2329  # called through wait_for
logged() {
    { "$@" </dev/null 2>&1; echo $? >"$RC"; } | scrub_tee "$LOG" >/dev/null
    [ "$(cat "$RC")" = 0 ]
}

# A lost connection, said the same way wherever it shows up.
no_network() {
    say "$*"
    say "The launcher in use was not changed."
    die "Couldn't reach GitHub, try again later."
}

# ---- paths

# True when $1 must never be the install folder.
unsafe_path() {
    case "$1" in
        "" | / | . | .. | */.. | */../* | ./* | */./* | *"
"*) return 0 ;;
    esac
    [ "$1" = "$HOME" ] && return 0
    case "$1" in
        /usr | /bin | /sbin | /etc | /var | /tmp | /opt | /private | /System | /Library | /Applications | /Users | /Volumes | /cores | /dev | /home)
            return 0 ;;
    esac
    return 1
}

# Deletes DIR/build-next or DIR/build-prev and nothing else. This is the only place the script deletes a folder.
remove_own() {
    case "$1" in
        build-next | build-prev) ;;
        *) die "Internal error: this script never deletes \"$1\"." ;;
    esac
    unsafe_path "$DIR" && die "Internal error: refusing to delete inside \"$DIR\"."
    target="$DIR/$1"
    if [ -L "$target" ]; then
        die "$target is a link. This script does not delete links. Remove it yourself and run this again."
    fi
    [ -e "$target" ] || return 0
    [ -d "$target" ] || die "$target is not a folder. Remove it yourself and run this again."
    rm -rf -- "$target" || die "Couldn't remove $target. Close anything using it and try again."
}

# The value of KEY in a file of KEY=value lines (install-info.txt in a build folder).
info_get() {
    [ -f "$1" ] || return 0
    sed -n "s/^$2=//p" "$1" | head -n 1 | tr -d '\r'
}

read_channel() {
    sed -n '1p' "$DIR/channel.txt" 2>/dev/null | tr -d '\r'
}

write_channel() {
    # shellcheck disable=SC2015  # either failing is the failure
    printf '%s\n' "$1" >"$DIR/channel.txt.tmp" && mv -f "$DIR/channel.txt.tmp" "$DIR/channel.txt" \
        || die "Couldn't write $DIR/channel.txt."
}

# ---- 1. this computer

check_computer() {
    if [ "$(uname -s)" != Darwin ]; then
        say "This installer is for macOS. On Windows, use install.ps1 (see the README)."
        die "This installer is for macOS only."
    fi
    case "$(uname -m)" in
        arm64 | x86_64) ;;
        *) die "This needs a Mac with Apple Silicon or an Intel processor." ;;
    esac
    macos=$(sw_vers -productVersion 2>/dev/null || echo 0)
    case "${macos%%.*}" in
        '' | *[!0-9]*) macos_major=0 ;;
        *) macos_major=${macos%%.*} ;;
    esac
    if [ "$macos_major" -lt 11 ]; then
        say "This needs macOS 11 or newer. This Mac has macOS $macos."
        die "macOS 11 or newer is needed."
    fi
    info "macOS $macos, $(uname -m)"

    # Apple's Command Line Tools bring git, make, the compiler and Python 3. Ask xcode-select first: with the tools
    # missing, running git or make would pop up Apple's installer window in the middle of this script.
    missing=""
    if ! xcode-select -p >/dev/null 2>&1 || ! xcrun --find clang >/dev/null 2>&1; then
        missing="Apple's Command Line Tools"
    else
        for tool in git make python3 curl; do
            command -v "$tool" >/dev/null 2>&1 || missing="${missing:+$missing, }$tool"
        done
    fi
    if [ -n "$missing" ]; then
        UI_FAIL_TEXT="not installed: $missing"
        say "Missing: $missing."
        say "Apple's Command Line Tools have all of these (git, make, the compiler and Python 3). To get them:"
        say "  1. Run: xcode-select --install"
        say "  2. Click Install in the window that opens, and wait until it finishes."
        say "  3. Run this command again."
        say "Nothing was changed."
        die "Missing: $missing. Run xcode-select --install, wait for it to finish, then run this again."
    fi
    info "Command Line Tools: $(xcode-select -p)"

    # The source is about 150 MB and a build folder about 150 MB (more with a fetched CMake), and an update keeps three
    # build folders for a while (build, build-next, build-prev).
    probe="$DIR"
    while [ ! -d "$probe" ] && [ "$probe" != / ]; do probe=$(dirname "$probe"); done
    free_kb=$(df -Pk "$probe" 2>/dev/null | awk 'NR == 2 { print $4 }')
    case "$free_kb" in '' | *[!0-9]*) free_kb="" ;; esac
    if [ -n "$free_kb" ] && [ "$free_kb" -lt 1000000 ]; then
        say "There is only $((free_kb / 1024)) MB free where $DIR goes. The install needs about 1 GB free."
        say "Nothing was changed."
        die "Not enough free space on the disk (about 1 GB is needed)."
    fi
}

# ---- 2. the downloads

# Which release or commit to install. Sets TAG (stable only) and checks GitHub answers, before anything is made.
pick_target() {
    if [ "$CHANNEL" = dev ]; then
        TAG=""
        out=$(git ls-remote "$REPO" refs/heads/main 2>&1 </dev/null) || no_network "$out"
        [ -n "$out" ] || die "The repository has no main branch."
        return 0
    fi
    out=$(git ls-remote --tags --refs "$REPO" 2>&1 </dev/null) || no_network "$out"
    # Releases are the tags vX.Y.Z. The highest by number, not by letters (v0.0.10 comes after v0.0.9).
    newest=$(printf '%s\n' "$out" \
        | awk '{ n = $2; sub(/^refs\/tags\//, "", n); if (n ~ /^v[0-9]+\.[0-9]+\.[0-9]+$/) print substr(n, 2) }' \
        | sort -t. -k1,1n -k2,2n -k3,3n | tail -n 1)
    if [ -z "$newest" ]; then
        say "No release has been published yet, so there is nothing to install by default."
        say "To try the version that is being made (it can have bugs), run the same command with --dev:"
        say "  curl -fsSL https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.sh | sh -s -- --dev"
        say "Nothing was changed."
        die "No release has been published yet. Use --dev to install the development version."
    fi
    TAG="v$newest"
    info "Newest release: $newest"
    UI_VERSION=$newest
}

git_src() {
    git -C "$SRC" "$@" </dev/null
}

# Clones or fetches the source and checks out the wanted commit, submodules included. Sets COMMIT and VERSION.
get_source() {
    if [ -d "$SRC/.git" ]; then
        git_src remote set-url origin "$REPO" || die "Couldn't use the source folder $SRC."
    elif [ -e "$SRC" ] || [ -L "$SRC" ]; then
        say "$SRC exists but is not a git clone, so this script cannot use it. Move it away and run this again."
        die "$SRC is in the way. Move it away and run this again."
    else
        info "Downloading the source (a clone of $REPO)"
        ui_active "$UI_SRC" "downloading tpw-port"
        fetch_logged git clone --no-checkout "$REPO" "$SRC" </dev/null || no_network "The download of the source did not finish."
    fi

    if [ "$CHANNEL" = dev ]; then
        info "Fetching main"
        ui_active "$UI_SRC" "fetching main"
        fetch_logged git_src fetch --force --no-tags origin "+refs/heads/main:refs/remotes/origin/main" \
            || no_network "The download of the source did not finish."
        ref="refs/remotes/origin/main"
    else
        info "Fetching $TAG"
        ui_active "$UI_SRC" "fetching $TAG"
        fetch_logged git_src fetch --force --no-tags origin "+refs/tags/$TAG:refs/tags/$TAG" \
            || no_network "The download of the source did not finish."
        ref="refs/tags/$TAG"
    fi
    COMMIT=$(git_src rev-parse --verify "$ref^{commit}" 2>/dev/null) || die "The source has no $ref."
    # the view's title names the version as soon as the commit is known (the checkout and the submodules take a while);
    # VERSION itself is read from the checkout below, and a version that does not parse is left to that check
    if [ "$UI" = tty ]; then
        _v=$(git_src show "$COMMIT:launcher/VERSION.txt" 2>/dev/null | sed -n '1p' | tr -d '\r')
        case "$_v" in [0-9]*.[0-9]*.[0-9]* | [0-9]*.[0-9]*.[0-9]*-dev) UI_VERSION=$_v ;; esac
    fi

    # The source is this script's own. A change somebody made in it would be lost by the checkout: stop instead. A
    # clone not checked out yet has no index (and git would call every file deleted). Submodules are left out of the
    # check: they differ from the pinned commits between a checkout and their update.
    if [ -f "$SRC/.git/index" ] && [ -n "$(git_src status --porcelain --ignore-submodules=all 2>/dev/null)" ]; then
        say "$SRC has changes that this script did not make, and switching versions would overwrite them."
        say "Move that folder away (or the changes out of it) and run this again."
        die "The source folder has changes of its own: $SRC"
    fi
    git_src checkout -q --detach "$COMMIT" || die "Couldn't switch the source to $COMMIT."
    git_src submodule sync -q --recursive || die "Couldn't read the source's submodules."
    info "Downloading the submodules"
    ui_active "$UI_SRC" "downloading the submodules"
    fetch_logged git_src submodule update --init --recursive --force \
        || no_network "The download of the submodules did not finish."

    VERSION=$(sed -n '1p' "$SRC/launcher/VERSION.txt" 2>/dev/null | tr -d '\r')
    case "$VERSION" in
        [0-9]*.[0-9]*.[0-9]* | [0-9]*.[0-9]*.[0-9]*-dev) ;;
        *) die "The source does not name its version (launcher/VERSION.txt)." ;;
    esac
    # A release names its version in its tag too, and release.yml refuses a tag that does not match.
    if [ "$CHANNEL" = stable ] && [ "v$VERSION" != "$TAG" ]; then
        die "The release $TAG says it is version $VERSION. Not installing it."
    fi
    info "Source: $COMMIT (version $VERSION)"
    UI_VERSION=$VERSION
}

# Makes an empty DIR/build-next and fetches the build's tools into it, so that a lost connection shows up now. A CMake
# the current build fetched is copied over first (the same pinned one, unless the Makefile's pin changed, and then the
# build fetches the new one).
# make TARGET in the source, into build-next, its output on the screen (as it is) and in the log (with nothing personal
# in it, see scrub_tee), its exit code in $RC.
# shellcheck disable=SC2329  # called through wait_for
make_logged() {
    if [ "$UI" = tty ]; then
        { make -C "$SRC" BUILD="$NEXT" "$1" </dev/null 2>&1; echo $? >"$RC"; } | scrub_tee "$LOG" >/dev/null
    else
        { make -C "$SRC" BUILD="$NEXT" "$1" </dev/null 2>&1; echo $? >"$RC"; } | scrub_tee "$LOG"
    fi
}

# Reads the build's output on stdin: it goes unchanged to the screen, and a copy with nothing personal in it is
# appended to the log $1 (the log is what a player is asked to send; the screen is theirs). The home folder becomes ~,
# any /Users/NAME ~ or /Users/<user>, a volume's name <volume>, /var/folders/xx/yyyy <id>, the account name <user> (only
# after user=, owner: and the like, so a name that is a common word is not changed elsewhere) and this Mac's name
# <computer> (4 letters or more). Scrubbing again changes nothing. It mirrors platform/log_scrub.h.
scrub_tee() {
    TPW_S_LOG=$1 \
        TPW_S_HOMES="$(printf '%s\n%s\n' "$HOME" "$(cd "$HOME" 2>/dev/null && pwd -P)")" \
        TPW_S_USER="${USER:-$(id -un 2>/dev/null)}" \
        TPW_S_HOST="${TPW_INSTALL_HOSTNAME:-$(hostname 2>/dev/null)}" \
        TPW_S_VOLUMES="$(ls /Volumes 2>/dev/null)" \
        LC_ALL=C awk '
function isname(c) { return c ~ /[A-Za-z0-9_.-]/ || c >= "\200" }
function isword(c) { return c ~ /[A-Za-z0-9_-]/ || c >= "\200" }
# find in s becomes rep. Mode 0, a path: nothing name-like may follow. Mode 1, a name: no word character on either
# side. ci: ignore case.
function lit(s, find, rep, mode, ci,   out, hay, need, i, n, before, after) {
    n = length(find)
    if (n == 0) return s
    hay = ci ? tolower(s) : s
    need = ci ? tolower(find) : find
    out = ""
    while ((i = index(hay, need)) > 0) {
        before = i > 1 ? substr(hay, i - 1, 1) : ""
        after = substr(hay, i + n, 1)
        if (mode == 0 ? (after == "" || !isname(after)) : ((before == "" || !isword(before)) && (after == "" || !isword(after)))) {
            out = out substr(s, 1, i - 1) rep
        } else {
            out = out substr(s, 1, i + n - 1)
        }
        s = substr(s, i + n)
        hay = substr(hay, i + n)
    }
    return out s
}
function generic(s,   out, name, pre, rep) {
    out = ""
    while (match(s, /\/Users\/[^\/ \t"\047<>]+/)) {
        name = substr(s, RSTART + 7, RLENGTH - 7)
        pre = RSTART > 1 ? substr(s, RSTART - 1, 1) : ""
        if (name == "Shared" || name == "Public" || name == "Default" || name == "All") rep = substr(s, RSTART, RLENGTH)
        else if (RSTART == 1 || pre ~ /[ \t"\047=:(]/) rep = "~"
        else rep = "/Users/<user>"
        out = out substr(s, 1, RSTART - 1) rep
        s = substr(s, RSTART + RLENGTH)
    }
    return out s
}
function account(s,   out, hay, i, n, before, after, key) {
    n = length(U)
    if (n == 0) return s
    out = ""
    hay = tolower(s)
    while ((i = index(hay, tolower(U))) > 0) {
        before = substr(hay, 1, i - 1)
        after = substr(hay, i + n, 1)
        if (before ~ /(user|username|user name|login|logname|owner|account)[ \t]*[=:][ \t]*$/ && (after == "" || !isword(after))) {
            out = out substr(s, 1, i - 1) "<user>"
        } else {
            out = out substr(s, 1, i + n - 1)
        }
        s = substr(s, i + n)
        hay = substr(hay, i + n)
    }
    return out s
}
function every(s, re, rep,   out) {
    out = ""
    while (match(s, re)) {
        out = out substr(s, 1, RSTART - 1) rep
        s = substr(s, RSTART + RLENGTH)
    }
    return out s
}
function scrub(s,   k) {
    for (k = 1; k <= NH; k++) s = lit(s, H[k], "~", 0, 0)
    for (k = 1; k <= NV; k++) s = lit(s, "/Volumes/" V[k], "/Volumes/<volume>", 0, 0)
    s = generic(s)
    s = every(s, "/Volumes/[^/ \t\"\047<>]+", "/Volumes/<volume>")
    s = every(s, "/var/folders/[^/ \t\"\047<>]+/[^/ \t\"\047<>]+", "/var/folders/<id>")
    if (length(C) >= 4) s = lit(s, C, "<computer>", 1, length(C) >= 6)
    return account(s)
}
BEGIN {
    NH = split(ENVIRON["TPW_S_HOMES"], H, "\n")
    NV = split(ENVIRON["TPW_S_VOLUMES"], V, "\n")
    for (k = 1; k <= NH; k++) if (length(H[k]) < 2 || H[k] == "/") H[k] = ""
    U = ENVIRON["TPW_S_USER"]
    C = ENVIRON["TPW_S_HOST"]
    sub(/\..*/, "", C)
    out = ENVIRON["TPW_S_LOG"]
}
{
    print $0
    fflush()
    print scrub($0) >> out
    fflush(out)
}'
}

prepare_next() {
    remove_own build-next
    mkdir -p "$NEXT" || die "Couldn't make $NEXT."
    if [ -d "$DIR/build/cmake" ]; then
        cp -R "$DIR/build/cmake" "$NEXT/cmake" 2>/dev/null || true
    fi
    [ "$UI" = tty ] || : >"$LOG"   # the progress view started the log at the downloads
    info "Getting the build's tools (CMake, if this Mac has none)"
    ui_active "$UI_SRC" "getting the build's tools (CMake, if this Mac has none)"
    wait_for make_logged find-cmake
    if [ "$(cat "$RC")" != 0 ]; then
        if grep -q "Could not download CMake" "$LOG"; then
            no_network "The download of CMake did not finish."
        fi
        die "Getting the build's tools failed. The output is above and in $LOG."
    fi
    [ "$UI" = tty ] && ui_done "$UI_SRC" "$UI_GOT"   # the row gets its text back
    return 0
}

# ---- 3. the build

build_next() {
    info "Compiling into $NEXT. The first time takes a few minutes."
    UI_BUILD_START=$(date +%s)
    UI_STAGE=0
    UI_TICK=ui_build_tick
    [ "$UI" = tty ] && ui_build_tick
    wait_for make_logged launcher
    UI_TICK=""
    fmt_dur $(($(date +%s) - UI_BUILD_START))
    if [ "$(cat "$RC")" != 0 ]; then
        if [ "$UI" = tty ]; then
            ui_close fail "failed after $DUR"
            if [ -x "$DIR/build/tpw_launcher" ]; then
                printf '\n  The launcher in use (%s) is untouched and still works.\n' "$(info_get "$DIR/build/install-info.txt" version)"
            fi
            ui_log_tail
            UI_FAIL_DONE=1
        fi
        info "The build failed. Its output is above and in $LOG."
        info "The launcher in use ($DIR/build) was not changed."
        die "The build failed. The launcher in use was not changed. The output is in $LOG."
    fi
    ui_done build "done in $DUR"
}

# ---- 4. the check

# The first line of a launcher's --version: "STP/TPW Launcher v0.1.0".
version_line() {
    "$1" --version </dev/null 2>&1 | sed -n '1p' | tr -d '\r'
}

verify_next() {
    [ -x "$NEXT/tpw_launcher" ] || die "The build finished but made no tpw_launcher. The launcher in use was not changed."
    first=$(version_line "$NEXT/tpw_launcher")
    case "$first" in
        *" v$VERSION") ;;
        *)
            say "The new launcher says \"$first\", not version $VERSION."
            die "The new launcher did not answer with version $VERSION. The launcher in use was not changed."
            ;;
    esac
    info "$first"
    {
        printf 'version=%s\n' "$VERSION"
        printf 'channel=%s\n' "$CHANNEL"
        printf 'commit=%s\n' "$COMMIT"
    } >"$NEXT/install-info.txt" || die "Couldn't write $NEXT/install-info.txt."
}

# ---- 5. the switch

do_swap() {
    [ -f "$NEXT/install-info.txt" ] || die "There is no finished new build in $NEXT to switch to."
    new_version=$(info_get "$NEXT/install-info.txt" version)
    new_channel=$(info_get "$NEXT/install-info.txt" channel)
    case "$new_channel" in stable | dev) ;; *) die "$NEXT/install-info.txt names no channel. Nothing was switched." ;; esac
    first=$(version_line "$NEXT/tpw_launcher")
    case "$first" in
        *" v$new_version") ;;
        *) die "The new build in $NEXT does not answer with version $new_version. Nothing was switched." ;;
    esac
    if [ -L "$DIR/build" ] || { [ -e "$DIR/build" ] && [ ! -d "$DIR/build" ]; }; then
        die "$DIR/build is not a folder. Nothing was switched."
    fi
    remove_own build-prev
    had_build=0
    if [ -d "$DIR/build" ]; then
        mv "$DIR/build" "$DIR/build-prev" \
            || die "Couldn't move the old launcher aside. Close it and try again. Nothing was switched."
        had_build=1
    fi
    if ! mv "$NEXT" "$DIR/build"; then
        [ -d "$DIR/build-prev" ] && mv "$DIR/build-prev" "$DIR/build"
        die "Couldn't move the new launcher into place. The old one was put back."
    fi
    write_channel "$new_channel"
    VERSION=$new_version
    if [ "$had_build" = 1 ]; then
        info "Switched to version $VERSION. The one before is kept in $DIR/build-prev."
        ui_done swap "switched to $VERSION; the one before is kept in build-prev"
    else
        info "Installed version $VERSION."
        ui_state swap
        [ "$UI_S" = active ] && ui_done swap "installed version $VERSION"
    fi
}

# The folders a player may have played from before this script: a clone made the README's old way (in the home folder,
# or one of its usual folders, or the folder this runs in or its parent), or an install of this script in another
# folder. Prints each once, never this install or a folder in it.
old_copies() {
    here=$(cd "$DIR" 2>/dev/null && pwd -P) || return 0
    seen=""
    for c in "$HOME/tpw-port" "$HOME/Downloads/tpw-port" "$HOME/Desktop/tpw-port" "$HOME/Documents/tpw-port" \
        "$PWD" "$(dirname "$PWD")"; do
        { [ -e "$c/.git" ] && [ -f "$c/launcher/VERSION.txt" ]; } || [ -e "$c/.tpw-installer" ] || continue
        real=$(cd "$c" 2>/dev/null && pwd -P) || continue
        case "$real" in "$here" | "$here"/*) continue ;; esac
        case "$seen" in *"
$real
"*) continue ;; esac
        seen="$seen
$real
"
        printf '%s\n' "$real"
    done
}

# Tells the player about the folders old_copies finds. Nothing in them is read, changed or deleted.
tell_old_copies() {
    copies=$(old_copies)
    [ -n "$copies" ] || return 0
    say ""
    # not a pipe: the progress view keeps say's lines in a variable, which a pipe's subshell would lose
    while IFS= read -r c; do
        say "An older copy of the launcher is in $c."
    done <<EOF
$copies
EOF
    if [ "$(printf '%s\n' "$copies" | wc -l)" -gt 1 ]; then
        say "If you only used them to play, they are no longer needed: this install replaces them, and you can delete"
        say "those folders. Your games, saves and settings are not in them."
    else
        say "If you only used it to play, it is no longer needed: this install replaces it, and you can delete that"
        say "folder. Your games, saves and settings are not in it."
    fi
    say "They are in ~/Library/Application Support/STP-TPW Launcher, and this install uses them as they are."
}

launch() {
    info "Starting $DIR/build/tpw_launcher"
    ui_active start "opening the launcher"
    if (cd "$DIR" && nohup "$DIR/build/tpw_launcher" >/dev/null 2>&1 </dev/null &); then
        ui_done start "the launcher is open"
    else
        say "Couldn't start the launcher. Start it yourself: $DIR/build/tpw_launcher"
        ui_warn start "the launcher did not start (see below)"
    fi
}

# ---- the themepark command

CMD_MARK="# tpw-port installer: the themepark command"

# Writes DIR/bin/themepark (again at every install and switch: it is this script's, in DIR). It finds the install from
# its own place, so it holds no path. Fails (and says why) when it cannot.
write_command() {
    cmd="$DIR/bin/themepark"
    if [ -e "$cmd" ] && ! grep -qF "$CMD_MARK" "$cmd" 2>/dev/null; then
        say "$cmd was not made by this installer, so it is left alone."
        return 1
    fi
    if ! { mkdir -p "$DIR/bin" && write_command_file "$cmd.tmp" && chmod 755 "$cmd.tmp" && mv -f "$cmd.tmp" "$cmd"; }; then
        say "Couldn't write $cmd."
        return 1
    fi
}

write_command_file() {
    cat >"$1" <<'EOF'
#!/bin/sh
# tpw-port installer: the themepark command
# The STP/TPW Launcher of this install (the folder above this one), from any folder:
#   themepark             opens the launcher and returns at once
#   themepark update      updates this install to the newest release (themepark update --dev: to main) in this window,
#                         by running the installer that came with it (source/install.sh --update), then opens it
#   themepark --version   prints the launcher's version in this window (--help: its options)
# Anything else is passed on to the launcher, which is opened detached. The installer (install.sh) writes this file.
# To remove the command, delete the install folder and the line in ~/.zprofile (and ~/.bash_profile) that ends with
# the comment above.
dir=$(cd "$(dirname "$0")/.." && pwd) || exit 1
launcher="$dir/build/tpw_launcher"
cd "$dir" || exit 1
if [ "$1" = update ]; then
    shift
    if [ ! -f "$dir/source/install.sh" ]; then
        echo "themepark: there is no installer in $dir/source. Run the install line from the README again." >&2
        exit 1
    fi
    # the installer replaces build/ and opens the new launcher: one that is running must be quit first (its own
    # Update button updates it while it runs)
    running=$(ps -A -ww -o pid= -o command= | while read -r pid command; do
        case "$command" in ("$launcher" | "$launcher "*) printf '%s ' "$pid" ;; esac
    done)
    if [ -n "$running" ]; then
        echo "themepark: the launcher is running (process ${running% })." >&2
        echo "Quit it, or press its Update button, then run themepark update again." >&2
        exit 1
    fi
    exec /bin/sh "$dir/source/install.sh" --update --dir "$dir" "$@"
fi
if [ ! -x "$launcher" ]; then
    echo "themepark: there is no launcher in $dir/build. Run the installer again." >&2
    exit 1
fi
case "$1" in
    --version | --help | -h) exec "$launcher" "$@" ;;
esac
nohup "$launcher" "$@" >/dev/null 2>&1 </dev/null &
EOF
}

# ---- the application (THE APPLICATION above)

APP_NAME="STP:TPW Launcher.app"
APP_MARK="# tpw-port installer: the STP/TPW Launcher application"
APP_DIR=""   # the folder write_app put the application in (empty until it has)

# The folder the application goes in: TPW_INSTALL_APPS (a test), else /Applications when this user may write there (an
# administrator), else the player's own ~/Applications, made when missing. Prints it, or nothing when there is none.
apps_folder() {
    if [ -n "${TPW_INSTALL_APPS:-}" ]; then
        mkdir -p "$TPW_INSTALL_APPS" 2>/dev/null || return 1
        printf '%s\n' "$TPW_INSTALL_APPS"
    elif [ -d /Applications ] && [ -w /Applications ]; then
        printf '%s\n' /Applications
    else
        mkdir -p "$HOME/Applications" 2>/dev/null || return 1
        [ -w "$HOME/Applications" ] || return 1
        printf '%s\n' "$HOME/Applications"
    fi
}

# The application's icon, as the .icns $2, made in the scratch folder $1: the launcher writes its icon as a PNG
# (tpw_launcher --write-icon, app_icon.h), sips scales it to the ten sizes an .icns holds and iconutil packs them.
# Returns 1 when any of that is missing (a stand-in launcher in a test): the application is then made without an icon.
make_app_icon() {
    png="$1/icon.png"
    set="$1/icon.iconset"
    command -v sips >/dev/null 2>&1 && command -v iconutil >/dev/null 2>&1 || return 1
    "$DIR/build/tpw_launcher" --write-icon "$png" >/dev/null 2>&1 </dev/null && [ -s "$png" ] || return 1
    mkdir -p "$set" || return 1
    for size in 16 32 128 256 512; do
        sips -z "$size" "$size" "$png" --out "$set/icon_${size}x${size}.png" >/dev/null 2>&1 </dev/null || return 1
        sips -z "$((size * 2))" "$((size * 2))" "$png" --out "$set/icon_${size}x${size}@2x.png" >/dev/null 2>&1 </dev/null \
            || return 1
    done
    iconutil -c icns "$set" -o "$2" >/dev/null 2>&1 </dev/null && [ -s "$2" ]
}

# Writes the application into apps_folder (again at every install, update and switch, when DIR/build has the launcher).
# Never asks for a password: says what it could not do and returns 1. APP_DIR is the folder it went in.
write_app() {
    APP_DIR=""
    apps=$(apps_folder) || apps=""
    if [ -z "$apps" ]; then
        say "There is no Applications folder this user can write to, so STP/TPW Launcher was not added to one."
        return 1
    fi
    app="$apps/$APP_NAME"
    exe="$app/Contents/MacOS/STP-TPW-Launcher"
    # another program of the same name is never replaced; one of ours, or a half-written one with no program, is
    if [ -e "$app" ] && ! grep -qF "$APP_MARK" "$exe" 2>/dev/null && [ -n "$(ls -A "$app/Contents/MacOS" 2>/dev/null)" ]; then
        say "$app was not made by this installer, so it is left alone."
        return 1
    fi
    if [ -e "$app" ] && [ ! -d "$app" ]; then
        say "$app is not a folder, so it is left alone."
        return 1
    fi
    work=$(mktemp -d "${TMPDIR:-/tmp}/tpw-app.XXXXXX") || work=""
    if [ -z "$work" ]; then
        say "Couldn't make a scratch folder for the application's icon."
        return 1
    fi
    icns="$work/icon.icns"
    icon=0
    make_app_icon "$work" "$icns" && icon=1
    ok=1
    mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources" || ok=0
    [ "$ok" = 0 ] || { write_app_program "$exe.tmp" && chmod 755 "$exe.tmp" && mv -f "$exe.tmp" "$exe"; } || ok=0
    [ "$ok" = 0 ] || { write_app_plist "$app/Contents/Info.plist.tmp" "$icon" && mv -f "$app/Contents/Info.plist.tmp" "$app/Contents/Info.plist"; } || ok=0
    [ "$ok" = 0 ] || { printf 'APPL????' >"$app/Contents/PkgInfo.tmp" && mv -f "$app/Contents/PkgInfo.tmp" "$app/Contents/PkgInfo"; } || ok=0
    if [ "$ok" = 1 ]; then
        if [ "$icon" = 1 ]; then
            mv -f "$icns" "$app/Contents/Resources/icon.icns" || ok=0
        else
            rm -f "$app/Contents/Resources/icon.icns"
        fi
    fi
    case "$work" in */tpw-app.??????) rm -rf "$work" ;; esac
    if [ "$ok" = 0 ]; then
        say "Couldn't write $app."
        return 1
    fi
    touch "$app"   # a changed folder: the Finder and Launchpad read the bundle again
    APP_DIR=$apps
}

# The application's program: a few lines of shell that start DIR/build/tpw_launcher in DIR, as the themepark command
# does (write_command_file), detached: an exec'd launcher stays behind the other windows, a detached one comes to the
# front (tried both on 2026-10-26). DIR is written in (an application in /Applications cannot find the install from its own
# place). A missing launcher (the install folder deleted) gets a message, not a silent nothing.
write_app_program() {
    # DIR between single quotes: a ' in it becomes '\'' (the installer refuses ", $, ` and \ in it)
    quoted=$(printf '%s\n' "$DIR" | sed "s/'/'\\\\''/g")
    cat >"$1" <<EOF
#!/bin/sh
$APP_MARK
# Starts the STP/TPW Launcher installed in the folder named below, as the themepark command does. The installer
# (install.sh) writes this application again at every install and update. To remove it, drag it to the Trash.
dir='$quoted'
if [ ! -x "\$dir/build/tpw_launcher" ]; then
    osascript -e "display alert \\"STP/TPW Launcher is not installed any more\\" message \\"The launcher was in \$dir. Run the installer again to put it back.\\" as critical" >/dev/null 2>&1
    exit 1
fi
cd "\$dir" && nohup "\$dir/build/tpw_launcher" >/dev/null 2>&1 </dev/null &
EOF
}

# The application's Info.plist: the launcher's own name and identifier (launcher/macos/Info.plist.in), the version
# installed (its numbers: 0.1.1-dev is 0.1.1), the icon when $2 is 1.
write_app_plist() {
    numbers=${VERSION%-dev}
    case "$numbers" in [0-9]*.[0-9]*.[0-9]*) ;; *) numbers=0.0.0 ;; esac
    icon_lines=""
    [ "$2" = 1 ] && icon_lines="
	<key>CFBundleIconFile</key>
	<string>icon</string>"
    cat >"$1" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
	<string>en</string>
	<key>CFBundleExecutable</key>
	<string>STP-TPW-Launcher</string>
	<key>CFBundleIdentifier</key>
	<string>org.tpw-port.launcher</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>STP/TPW Launcher</string>
	<key>CFBundleDisplayName</key>
	<string>STP/TPW Launcher</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>$numbers</string>
	<key>CFBundleVersion</key>
	<string>$numbers</string>$icon_lines
	<key>LSApplicationCategoryType</key>
	<string>public.app-category.games</string>
	<key>LSMinimumSystemVersion</key>
	<string>11.0</string>
	<key>NSHighResolutionCapable</key>
	<true/>
</dict>
</plist>
EOF
}

# The step that writes the application: its row and its lines around write_app.
add_app() {
    ui_active application "adding STP/TPW Launcher to Applications"
    if write_app; then
        info "STP/TPW Launcher is in $APP_DIR: open it from Launchpad, Spotlight or the Finder."
        if [ "$APP_DIR" = "$HOME/Applications" ] && [ -z "${TPW_INSTALL_APPS:-}" ]; then
            say "This user may not write in /Applications, so STP/TPW Launcher went into your own Applications folder,"
            say "$APP_DIR. Launchpad and Spotlight find it there too."
        fi
        ui_done application "STP/TPW Launcher, in $(tilde "$APP_DIR")"
    else
        ui_warn application "STP/TPW Launcher was not added (see below)"
    fi
}

# Makes sure PROFILE has LINE once, as its only line with CMD_MARK: added at the end when there is none, put in place
# of the first and the others dropped when they differ (an install in another folder takes the command over).
put_line() {
    profile=$1
    line=$2
    if [ -e "$profile" ] && [ ! -f "$profile" ]; then
        say "$profile is not a file, so it was not changed."
        return 1
    fi
    if [ ! -f "$profile" ] || ! grep -qF "$CMD_MARK" "$profile"; then
        # the line starts a line of its own even when the file does not end with a newline
        if [ -s "$profile" ] && [ -n "$(tail -c 1 "$profile")" ]; then
            printf '\n' >>"$profile" || return 1
        fi
        printf '%s\n' "$line" >>"$profile" || return 1
        info "Added to $profile: $line"
        return 0
    fi
    if [ "$(grep -cF "$CMD_MARK" "$profile")" = 1 ] && grep -qxF "$line" "$profile"; then
        return 0
    fi
    # Rewritten in place (not replaced by another file), so that a ~/.zprofile that is a link stays one.
    kept=$(TPW_MARK=$CMD_MARK TPW_LINE=$line awk '
        index($0, ENVIRON["TPW_MARK"]) { if (!done) print ENVIRON["TPW_LINE"]; done = 1; next }
        { print }' "$profile") || return 1
    printf '%s\n' "$kept" >"$profile" || return 1
    info "Changed in $profile: $line"
}

# The PATH line of THE THEMEPARK COMMAND for this DIR.
path_line() {
    # shellcheck disable=SC2016  # $PATH is for the profile to expand
    printf 'export PATH="$PATH:%s/bin"  %s\n' "$DIR" "$CMD_MARK"
}

# The file a bash login shell (a new Terminal window, when bash is the player's shell) reads: the first of these that
# exists, as bash looks for them, or a new ~/.bash_profile.
bash_login_file() {
    for name in .bash_profile .bash_login .profile; do
        if [ -f "$1/$name" ]; then
            printf '%s\n' "$1/$name"
            return 0
        fi
    done
    printf '%s\n' "$1/.bash_profile"
}

# True when a profile has this install's PATH line (an update says how to play from it).
has_path_line() {
    home=${TPW_INSTALL_HOME:-$HOME}
    for profile in "$home/.zprofile" "$(bash_login_file "$home")"; do
        grep -qxF "$(path_line)" "$profile" 2>/dev/null && return 0
    done
    return 1
}

# Writes the PATH line of THE THEMEPARK COMMAND. Fails (and says why) when it cannot add it.
setup_path() {
    case "$DIR" in
        *[\"\$\`\\]*)
            say "The install folder's name has a character a shell line cannot hold, so themepark was not added."
            return 1
            ;;
    esac
    other=$(command -v themepark 2>/dev/null)
    if [ -n "$other" ] && [ "$other" != "$DIR/bin/themepark" ] && ! grep -qF "$CMD_MARK" "$other" 2>/dev/null; then
        say "There is already a themepark command ($other) that this installer did not make, so it is left alone."
        return 1
    fi
    line=$(path_line)
    home=${TPW_INSTALL_HOME:-$HOME}
    # zsh (macOS's shell) always. bash's login file when bash is the player's shell, or ~/.bash_profile when it exists
    # anyway. ~/.bashrc is not one: a new Terminal window starts a login shell, which does not read it.
    put_line "$home/.zprofile" "$line" || return 1
    case "${SHELL:-}" in
        */bash) put_line "$(bash_login_file "$home")" "$line" || return 1 ;;
        *) if [ -f "$home/.bash_profile" ]; then put_line "$home/.bash_profile" "$line" || return 1; fi ;;
    esac
}

# The self-check: does themepark work in a new Terminal window? A login shell of the player's own shell, started the way
# Terminal starts one (its profile read, a bare environment), is asked where themepark is. 20 seconds at most, in case a
# profile waits for something.
new_shell_finds_command() {
    shell=${SHELL:-/bin/zsh}
    [ -x "$shell" ] || return 1
    found="$DIR/.themepark-check"
    env -i HOME="${TPW_INSTALL_HOME:-$HOME}" USER="${USER:-}" LOGNAME="${LOGNAME:-}" SHELL="$shell" TERM=dumb \
        PATH=/usr/bin:/bin:/usr/sbin:/sbin "$shell" -l -c 'command -v themepark' </dev/null >"$found" 2>/dev/null &
    check_pid=$!
    waited=0
    while kill -0 "$check_pid" 2>/dev/null; do
        if [ "$waited" -ge 20 ]; then
            kill "$check_pid" 2>/dev/null
            break
        fi
        sleep 1
        waited=$((waited + 1))
    done
    wait "$check_pid" 2>/dev/null
    grep -qxF "$DIR/bin/themepark" "$found" 2>/dev/null
    ok=$?
    rm -f "$found"
    return "$ok"
}

# The one line that puts DIR/bin on the PATH of the player's shell, for a player to run when the self-check failed.
fix_line() {
    quoted=$(printf '%s\n' "$DIR/bin" | sed "s/'/'\\\\''/g")
    case "${SHELL:-}" in
        */fish) printf "fish_add_path '%s'\n" "$quoted" ;;
        */bash) printf "echo 'export PATH=\"\$PATH:%s\"' >> %s\n" "$quoted" "$(bash_login_file "$HOME" | sed "s|^$HOME|~|")" ;;
        */zsh) printf "echo 'export PATH=\"\$PATH:%s\"' >> \"\${ZDOTDIR:-\$HOME}/.zprofile\"\n" "$quoted" ;;
        *) printf "echo 'export PATH=\"\$PATH:%s\"' >> ~/.profile\n" "$quoted" ;;
    esac
}

# ---- the test build (TPW_INSTALL_STUB=1, for the installer's CI only)

# Instead of the download and the build: a stand-in launcher that answers --version as the real one does and, run from
# DIR/build (by themepark), adds a line to DIR/stub-launch.txt, so a test can see that themepark started it.
stub_source() {
    info "Test build: nothing is downloaded."
    ui_active "$UI_SRC" "a test build: nothing is downloaded"
    mkdir -p "$DIR" || die "Couldn't make the folder $DIR."
    : >"$DIR/.tpw-installer" || die "Couldn't write in $DIR."
    VERSION=${TPW_INSTALL_STUB_VERSION:-0.0.0-dev}
    COMMIT=stub-$VERSION
    UI_VERSION=$VERSION
}

stub_build() {
    remove_own build-next
    mkdir -p "$NEXT" || die "Couldn't make $NEXT."
    info "Test build: a stand-in launcher."
    ui_active build "a test build: a stand-in launcher"
    cat >"$NEXT/tpw_launcher" <<EOF || die "Couldn't write the stand-in launcher."
#!/bin/sh
if [ "\$1" = --version ]; then printf 'STP/TPW Launcher v$VERSION\nbuild: stub\n'; fi
if [ "\$1" = --write-icon ]; then exit 1; fi   # the icon is the real launcher's: the application gets none
here=\$(cd "\$(dirname "\$0")" && pwd)
if [ "\$(basename "\$here")" = build ]; then
    printf 'STP/TPW Launcher v$VERSION args=%s cwd=%s\n' "\$*" "\$PWD" >>"\$here/../stub-launch.txt"
fi
EOF
    chmod 755 "$NEXT/tpw_launcher"
    ui_done build "a stand-in launcher (test build)"
}

wait_for_exit() {
    waited=0
    while kill -0 "$1" 2>/dev/null; do
        if [ "$waited" -ge 300 ]; then
            die "The launcher is still running after 5 minutes. Nothing was switched."
        fi
        sleep 1
        waited=$((waited + 1))
    done
}

# ---- the three ways to run: swap, install or update

run_swap() {
    [ -e "$DIR/.tpw-installer" ] || [ -e "$DIR/channel.txt" ] || die "There is no install in $DIR."
    STEP_TOTAL=2   # the switch and the application
    [ -z "$WAIT_PID" ] || STEP_TOTAL=$((STEP_TOTAL + 1))
    [ "$LAUNCH" = 0 ] || STEP_TOTAL=$((STEP_TOTAL + 1))
    UI_FOOTER="Your games, saves and settings are not touched by an update."
    if [ -n "$WAIT_PID" ]; then
        step "Waiting for the launcher to close"
        ui_active launcher "waiting for the launcher to close"
        wait_for_exit "$WAIT_PID"
        ui_done launcher "closed"
    fi
    step "Switching to the new version"
    ui_active swap "switching to the new version"
    if [ -f "$NEXT/install-info.txt" ]; then
        do_swap
        write_command || :
    else
        say "No new version is waiting in $NEXT, so there is nothing to switch."
        [ -x "$DIR/build/tpw_launcher" ] || die "There is no launcher in $DIR/build."
        VERSION=$(info_get "$DIR/build/install-info.txt" version)
        ui_done swap "nothing was waiting in build-next"
    fi
    step "Adding STP/TPW Launcher to Applications"
    add_app
    if [ "$LAUNCH" = 1 ]; then
        step "Starting the launcher"
        launch
    fi
    fmt_dur $(($(date +%s) - UI_START))
    UI_CLOSING="Done in $DUR. The launcher is version ${VERSION:-unknown}."
    finish_ok "${VERSION:-unknown}"
}

run_install() {
    if [ "$UPDATE" = 1 ]; then
        { [ -e "$DIR/channel.txt" ] && [ -d "$SRC/.git" ]; } || die "There is no install in $DIR to update."
    fi
    # Only --dev on this very run gives main; --update included, channel.txt only records what is installed (ONE LINE
    # FOR EVERYONE).
    [ -n "$CHANNEL" ] || CHANNEL=stable
    # The progress view's rows. An install in place gets an "installed" row (and a swap row later) instead of the
    # tools row, as the design note draws them.
    had_build=0
    old_version=""
    if [ -x "$DIR/build/tpw_launcher" ]; then
        had_build=1
        old_version=$(info_get "$DIR/build/install-info.txt" version)
    fi
    if [ "$had_build" = 1 ]; then
        UI_FOOTER="Your games, saves and settings are not touched by an update."
        ui_active installed "version ${old_version:-unknown} found"
    else
        UI_FOOTER="All of this stays inside $(tilde "$DIR")."
        ui_active tools "looking for the Xcode command line tools"
        ui_pending "$UI_SRC" ""
        ui_pending build ""
        [ "$UPDATE" = 1 ] || ui_pending command "themepark"
        [ "$STAGE" = 1 ] || ui_pending application "STP/TPW Launcher"
        [ "$STAGE" = 1 ] || [ "$LAUNCH" = 0 ] || ui_pending start ""
    fi
    if [ "$CHANNEL" = stable ] && [ "$(read_channel)" = dev ]; then
        say "This install followed main (--dev); it now follows the newest release. Add --dev to stay on main."
        if [ "$UI" = tty ]; then
            UI_NOTES=""
            ui_warn installed "this install follows main (${old_version:-unknown}). Without --dev the line switches it to the newest release. Add --dev to stay on main."
        fi
    fi

    # check, download, build, verify; then switch and start unless --stage
    STEP_TOTAL=4
    [ "$STAGE" = 1 ] || STEP_TOTAL=$((STEP_TOTAL + 2))   # the switch and the application
    [ "$STAGE" = 1 ] || [ "$LAUNCH" = 0 ] || STEP_TOTAL=$((STEP_TOTAL + 1))

    step "Checking this computer"
    check_computer
    [ "$had_build" = 1 ] || ui_done tools "Xcode command line tools found"

    step "Downloading the source and the build's tools"
    ui_active "$UI_SRC" "looking for the newest version"
    if [ -n "${TPW_INSTALL_STUB:-}" ]; then
        stub_source
    else
        pick_target
        mkdir -p "$DIR" || die "Couldn't make the folder $DIR."
        : >"$DIR/.tpw-installer" || die "Couldn't write in $DIR."
        [ "$UI" = tty ] && : >"$LOG"   # the log holds the downloads' output too in the progress view
        get_source
    fi

    built=$(info_get "$DIR/build/install-info.txt" commit)
    staged=$(info_get "$NEXT/install-info.txt" commit)
    staged_channel=$(info_get "$NEXT/install-info.txt" channel)
    current=0
    if [ -x "$DIR/build/tpw_launcher" ] && [ "$built" = "$COMMIT" ]; then
        current=1
    fi
    ready=0
    if [ "$current" = 0 ] && [ "$staged" = "$COMMIT" ] && [ "$staged_channel" = "$CHANNEL" ] \
        && [ -x "$NEXT/tpw_launcher" ]; then
        ready=1
    fi

    # The progress view, now that the version is known: an install in place is current (two rows and done), or gets
    # the rows of an update; a fresh install's source row is done.
    if [ "$UI" = tty ]; then
        if [ "$CHANNEL" = dev ]; then
            got="tpw-port main downloaded ($VERSION, $(printf '%.10s' "$COMMIT"))"
            newest="main is at $VERSION, which is installed"
        else
            got="tpw-port $VERSION downloaded"
            newest="$VERSION is the newest release"
        fi
        UI_GOT=$got
        ui_state installed
        if [ "$had_build" = 1 ] && [ "$current" = 1 ]; then
            UI_FOOTER=""   # nothing happens to the install, so nothing to promise about it
            [ "$UI_S" = warn ] || ui_done installed "$(tilde "$DIR"), version ${old_version:-$VERSION}"
            ui_rename "$UI_SRC" newest
            ui_done newest "$newest"
        elif [ "$had_build" = 1 ]; then
            [ "$UI_S" = warn ] || ui_done installed "version ${old_version:-unknown} found, updating to $VERSION"
            ui_done "$UI_SRC" "$got"
            ui_pending build ""
            ui_pending swap "the new launcher replaces the old one when the build is done"
            [ "$UPDATE" = 1 ] || ui_pending command "themepark"
            [ "$STAGE" = 1 ] || ui_pending application "STP/TPW Launcher"
            [ "$STAGE" = 1 ] || [ "$LAUNCH" = 0 ] || ui_pending start ""
        else
            ui_done "$UI_SRC" "$got"
        fi
    fi

    if [ "$current" = 1 ]; then
        info "Already up to date: version $VERSION."
        # A build staged earlier is of another commit now: drop it, so that a later --swap-and-launch cannot switch to it.
        remove_own build-next
        step "Building the launcher"
        info "Nothing to build."
        step "Checking the new launcher"
        info "Nothing to check."
        if [ "$(read_channel)" != "$CHANNEL" ]; then
            write_channel "$CHANNEL"
            sed "s/^channel=.*/channel=$CHANNEL/" "$DIR/build/install-info.txt" >"$DIR/build/install-info.tmp" \
                && mv -f "$DIR/build/install-info.tmp" "$DIR/build/install-info.txt"
        fi
    elif [ "$ready" = 1 ]; then
        info "This version was built earlier and is waiting in $NEXT."
        step "Building the launcher"
        info "Nothing to build."
        ui_done build "built earlier, waiting in build-next"
        step "Checking the new launcher"
        verify_next
    elif [ -n "${TPW_INSTALL_STUB:-}" ]; then
        step "Building the launcher (a few minutes)"
        stub_build
        step "Checking the new launcher"
        verify_next
    else
        prepare_next
        step "Building the launcher (a few minutes)"
        build_next
        step "Checking the new launcher"
        verify_next
    fi

    if [ "$STAGE" = 1 ]; then
        if [ "$current" = 0 ]; then
            info "The new version is ready in $NEXT. Nothing was switched."
            UI_CLOSING="The new version is ready in $NEXT. Nothing was switched."
        else
            UI_CLOSING="Nothing to do: version $VERSION is installed."
        fi
        finish_ok "$VERSION"
    fi
    step "Switching to the new version"
    if [ "$current" = 0 ]; then
        [ "$had_build" = 0 ] || ui_active swap "switching to the new version"
        do_swap
    else
        info "Nothing to switch."
    fi
    # THE THEMEPARK COMMAND: the file at every switch, the PATH line only on an install
    play="Start it with: $DIR/build/tpw_launcher"
    every_window="From now on, type themepark in any new Terminal window to play. It works from any folder."
    command_row="themepark is not set up (see below)"
    [ "$UPDATE" = 1 ] || [ "$current" = 1 ] || ui_active command "adding the themepark command"
    if write_command; then
        play="To play later, run: $DIR/bin/themepark"
        command_row="themepark, from $DIR/bin (see below)"
        if [ "$UPDATE" = 1 ]; then
            has_path_line && play=$every_window
        elif setup_path; then
            if new_shell_finds_command; then
                play=$every_window
                command_row="themepark, in any new Terminal window"
            else
                # not a failure of the install: the launcher is in place, only the shortcut needs this
                say "A new Terminal window does not find themepark yet (your shell is ${SHELL:-unknown})."
                say "To fix it, run this line once, then open a new Terminal window:"
                say "  $(fix_line)"
                command_row="themepark, once the line below is run"
            fi
        fi
    fi
    if [ "$UPDATE" = 1 ] || [ "$current" = 1 ]; then
        :
    elif [ "$command_row" = "themepark, in any new Terminal window" ]; then
        ui_done command "$command_row"
    else
        ui_warn command "$command_row"
    fi
    # THE APPLICATION: written again at every install, update and switch
    step "Adding STP/TPW Launcher to Applications"
    add_app
    if [ "$LAUNCH" = 1 ]; then
        step "Starting the launcher"
        launch
    fi
    info "$play"
    [ "$UPDATE" = 1 ] || tell_old_copies
    closing=$play
    [ "$play" != "$every_window" ] || closing="Type themepark in a new Terminal window to play."
    if [ -n "$APP_DIR" ] && [ "$play" = "$every_window" ]; then
        closing="Open STP/TPW Launcher from Applications, or type themepark in a new Terminal window, to play."
    elif [ -n "$APP_DIR" ]; then
        closing="Open STP/TPW Launcher from Applications to play. $play"
    fi
    if [ "$current" = 1 ]; then
        UI_CLOSING="Nothing to do. $closing"
    else
        fmt_dur $(($(date +%s) - UI_START))
        UI_CLOSING="Done in $DUR. $closing"
    fi
    finish_ok "$VERSION"
}

# ---- main

main() {
    # Test hook: the text on standard input, scrubbed, is appended to the file TPW_INSTALL_SCRUB (and shown as it is).
    if [ -n "${TPW_INSTALL_SCRUB:-}" ]; then
        scrub_tee "$TPW_INSTALL_SCRUB"
        return $?
    fi
    REPO=${TPW_INSTALL_REPO:-https://github.com/ThemeParkWorldDecomp/tpw-port.git}
    DIR=""
    CHANNEL=""
    UPDATE=0
    STAGE=0
    SWAP=0
    WAIT_PID=""
    LAUNCH=1
    PLAIN=0
    GIT_TERMINAL_PROMPT=0   # a wrong address must fail, never wait for a password nobody sees
    export GIT_TERMINAL_PROMPT
    arm_traps

    while [ $# -gt 0 ]; do
        case "$1" in
            --dev) CHANNEL=dev ;;
            --stable) CHANNEL=stable ;;
            --dir)
                [ $# -ge 2 ] || die "--dir needs a folder."
                DIR=$2
                shift
                ;;
            --dir=*) DIR=${1#--dir=} ;;
            --update) UPDATE=1 ;;
            --stage) STAGE=1 ;;
            --swap-and-launch) SWAP=1 ;;
            --wait-pid)
                [ $# -ge 2 ] || die "--wait-pid needs a process number."
                WAIT_PID=$2
                shift
                ;;
            --wait-pid=*) WAIT_PID=${1#--wait-pid=} ;;
            --no-launch) LAUNCH=0 ;;
            --plain) PLAIN=1 ;;
            -h | --help)
                usage
                exit 0
                ;;
            *) die "Unknown option $1 (--help lists them)." ;;
        esac
        shift
    done
    ui_start
    [ "$STAGE" = 0 ] || [ "$UPDATE" = 1 ] || die "--stage goes with --update."
    [ "$SWAP" = 0 ] || [ "$UPDATE" = 0 ] || die "--swap-and-launch does not go with --update."
    [ -z "$WAIT_PID" ] || [ "$SWAP" = 1 ] || die "--wait-pid goes with --swap-and-launch."
    case "$WAIT_PID" in *[!0-9]*) die "--wait-pid needs a process number." ;; esac

    [ -n "$DIR" ] || DIR="$HOME/Applications/STP-TPW-Launcher"
    # --dir . and --dir ./name mean the current folder and a folder in it; "$HOME/." is the home folder, which is refused
    while :; do
        case "$DIR" in ./?*) DIR=${DIR#./} ;; *) break ;; esac
    done
    case "$DIR" in /*) ;; *) DIR="$PWD/$DIR" ;; esac
    while :; do
        case "$DIR" in
            ?*/) DIR=${DIR%/} ;;
            ?*/.) DIR=${DIR%/.} ;;
            *) break ;;
        esac
    done
    # (a line break in the name must not split the one RESULT line)
    unsafe_path "$DIR" && die "Not using \"$(printf '%s' "$DIR" | tr '\n' ' ')\" as the install folder. Choose another with --dir."
    # make and the shell lines of the Makefile cannot hold these in a folder name: the build would fail after the download
    case "$DIR" in
        *[\"\$\`\\]*) die "Not using \"$DIR\" as the install folder: a double quote, \$, backtick or backslash in its name breaks the build. Choose another with --dir." ;;
    esac

    SRC="$DIR/source"
    NEXT="$DIR/build-next"
    LOG="$DIR/last-build.log"
    RC="$DIR/.last-build-status"

    # A folder that exists, has things in it and is not this script's is somebody else's (a clone to work in, for
    # example, which has its own build/). Never write into it.
    if [ -d "$DIR" ] && [ ! -e "$DIR/.tpw-installer" ] && [ ! -e "$DIR/channel.txt" ] \
        && [ -n "$(ls -A "$DIR" 2>/dev/null)" ]; then
        say "$DIR exists and was not made by this installer, so it is left alone."
        say "Choose another place with --dir, for example: --dir \"$DIR-installed\""
        die "$DIR is not empty and not an install made by this script. Choose another folder with --dir."
    fi

    if [ "$SWAP" = 1 ]; then
        run_swap
    else
        run_install
    fi
}

main "$@"; exit $?
