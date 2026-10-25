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
#   bin/themepark    the themepark command: starts build/tpw_launcher from any folder (see THE THEMEPARK COMMAND)
#   channel.txt      "stable" or "dev": the launcher reads it to know that this script installed it; it records what
#                    is installed and never chooses what an update gets
#   last-build.log   the output of the last build, with no user name, home folder or computer name in it
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
# DIR/build/tpw_launcher (where the Update button is) and returns at once. DIR/bin gets on the PATH of new Terminal
# windows through one line in ~/.zprofile (zsh is macOS's shell), and in bash's login file when bash is the player's
# shell (or ~/.bash_profile when there is one), marked with CMD_MARK so that it is found again and never added twice.
# `curl | sh` cannot change the window it runs in, so the command works from the next window on (the first install
# starts the launcher itself). At the end a new login shell of the player's own shell checks that it finds themepark;
# if it does not, the player is given the one line that fixes it (the install has still succeeded). Only an install
# (not --update or --swap-and-launch) writes that line: an update never brings back a line the player removed. A
# themepark command this script did not make is never shadowed: the line is then not written and the player is told
# the full path instead.
#
# SAFETY. No sudo, nothing written outside DIR but that one marked line and, when missing, the folder DIR is in (the
# default's ~/Applications); the launcher is started at the end, from DIR/build. The only folders deleted are
# DIR/build-next and DIR/build-prev, by remove_own below, which refuses any other name, an odd install folder and a
# link. Nothing is installed on the Mac: Apple's Command Line Tools must be there already, and the build fetches a CMake
# into its own folder when needed.
#
# Test hooks (for the maintainers' tests, not for players): TPW_INSTALL_REPO replaces the repository URL,
# TPW_INSTALL_HOME the folder that holds .zprofile and .bash_profile, and TPW_INSTALL_STUB=1 puts a stand-in launcher in
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
  --help                this text

After installing, open a new Terminal window and type themepark to play, from any folder.
Run the same line again to update. It follows the newest release unless --dev is added each time; so does
the launcher's Update button, which never adds --dev.
EOF
}

# ---- output: the lines the launcher reads

STEP_NO=0
STEP_TOTAL=0

step() {
    STEP_NO=$((STEP_NO + 1))
    printf 'STEP %s/%s %s\n' "$STEP_NO" "$STEP_TOTAL" "$*"
}

say() {
    printf '%s\n' "$*"
}

die() {
    trap - INT TERM HUP
    printf 'RESULT fail %s\n' "$*"
    exit 1
}

finish_ok() {
    trap - INT TERM HUP
    printf 'RESULT ok %s\n' "$1"
    exit 0
}

# shellcheck disable=SC2329  # called by the trap in main
stopped() {
    printf '\n'
    stop_children
    die "Stopped before it finished. The launcher in use was not changed."
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
    wait "$!"
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
    say "macOS $macos, $(uname -m)"

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
        say "Missing: $missing."
        say "Apple's Command Line Tools have all of these (git, make, the compiler and Python 3). To get them:"
        say "  1. Run: xcode-select --install"
        say "  2. Click Install in the window that opens, and wait until it finishes."
        say "  3. Run this command again."
        say "Nothing was changed."
        die "Missing: $missing. Run xcode-select --install, wait for it to finish, then run this again."
    fi
    say "Command Line Tools: $(xcode-select -p)"

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
    say "Newest release: $newest"
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
        say "Downloading the source (a clone of $REPO)"
        wait_for git clone --no-checkout "$REPO" "$SRC" </dev/null || no_network "The download of the source did not finish."
    fi

    if [ "$CHANNEL" = dev ]; then
        say "Fetching main"
        wait_for git_src fetch --force --no-tags origin "+refs/heads/main:refs/remotes/origin/main" \
            || no_network "The download of the source did not finish."
        ref="refs/remotes/origin/main"
    else
        say "Fetching $TAG"
        wait_for git_src fetch --force --no-tags origin "+refs/tags/$TAG:refs/tags/$TAG" \
            || no_network "The download of the source did not finish."
        ref="refs/tags/$TAG"
    fi
    COMMIT=$(git_src rev-parse --verify "$ref^{commit}" 2>/dev/null) || die "The source has no $ref."

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
    say "Downloading the submodules"
    wait_for git_src submodule update --init --recursive --force \
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
    say "Source: $COMMIT (version $VERSION)"
}

# Makes an empty DIR/build-next and fetches the build's tools into it, so that a lost connection shows up now. A CMake
# the current build fetched is copied over first (the same pinned one, unless the Makefile's pin changed, and then the
# build fetches the new one).
# make TARGET in the source, into build-next, its output on the screen (as it is) and in the log (with nothing personal
# in it, see scrub_tee), its exit code in $RC.
# shellcheck disable=SC2329  # called through wait_for
make_logged() {
    { make -C "$SRC" BUILD="$NEXT" "$1" </dev/null 2>&1; echo $? >"$RC"; } | scrub_tee "$LOG"
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
    : >"$LOG"
    say "Getting the build's tools (CMake, if this Mac has none)"
    wait_for make_logged find-cmake
    if [ "$(cat "$RC")" != 0 ]; then
        if grep -q "Could not download CMake" "$LOG"; then
            no_network "The download of CMake did not finish."
        fi
        die "Getting the build's tools failed. The output is above and in $LOG."
    fi
}

# ---- 3. the build

build_next() {
    say "Compiling into $NEXT. The first time takes a few minutes."
    wait_for make_logged launcher
    if [ "$(cat "$RC")" != 0 ]; then
        say "The build failed. Its output is above and in $LOG."
        say "The launcher in use ($DIR/build) was not changed."
        die "The build failed. The launcher in use was not changed. The output is in $LOG."
    fi
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
    say "$first"
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
        say "Switched to version $VERSION. The one before is kept in $DIR/build-prev."
    else
        say "Installed version $VERSION."
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
    printf '%s\n' "$copies" | while IFS= read -r c; do
        say "An older copy of the launcher is in $c."
    done
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
    say "Starting $DIR/build/tpw_launcher"
    (cd "$DIR" && nohup "$DIR/build/tpw_launcher" >/dev/null 2>&1 </dev/null &) \
        || say "Couldn't start the launcher. Start it yourself: $DIR/build/tpw_launcher"
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
# Starts the STP/TPW Launcher of this install (the folder above this one) and returns at once. The installer
# (install.sh) writes this file. To remove the command, delete the install folder and the line in ~/.zprofile (and
# ~/.bash_profile) that ends with the comment above.
dir=$(cd "$(dirname "$0")/.." && pwd) || exit 1
if [ ! -x "$dir/build/tpw_launcher" ]; then
    echo "themepark: there is no launcher in $dir/build. Run the installer again." >&2
    exit 1
fi
cd "$dir" && nohup "$dir/build/tpw_launcher" "$@" >/dev/null 2>&1 </dev/null &
EOF
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
        say "Added to $profile: $line"
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
    say "Changed in $profile: $line"
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
    say "Test build: nothing is downloaded."
    mkdir -p "$DIR" || die "Couldn't make the folder $DIR."
    : >"$DIR/.tpw-installer" || die "Couldn't write in $DIR."
    VERSION=${TPW_INSTALL_STUB_VERSION:-0.0.0-dev}
    COMMIT=stub-$VERSION
}

stub_build() {
    remove_own build-next
    mkdir -p "$NEXT" || die "Couldn't make $NEXT."
    say "Test build: a stand-in launcher."
    cat >"$NEXT/tpw_launcher" <<EOF || die "Couldn't write the stand-in launcher."
#!/bin/sh
if [ "\$1" = --version ]; then printf 'STP/TPW Launcher v$VERSION\nbuild: stub\n'; fi
here=\$(cd "\$(dirname "\$0")" && pwd)
if [ "\$(basename "\$here")" = build ]; then
    printf 'STP/TPW Launcher v$VERSION args=%s cwd=%s\n' "\$*" "\$PWD" >>"\$here/../stub-launch.txt"
fi
EOF
    chmod 755 "$NEXT/tpw_launcher"
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
    STEP_TOTAL=1
    [ -z "$WAIT_PID" ] || STEP_TOTAL=$((STEP_TOTAL + 1))
    [ "$LAUNCH" = 0 ] || STEP_TOTAL=$((STEP_TOTAL + 1))
    if [ -n "$WAIT_PID" ]; then
        step "Waiting for the launcher to close"
        wait_for_exit "$WAIT_PID"
    fi
    step "Switching to the new version"
    if [ -f "$NEXT/install-info.txt" ]; then
        do_swap
        write_command || :
    else
        say "No new version is waiting in $NEXT, so there is nothing to switch."
        [ -x "$DIR/build/tpw_launcher" ] || die "There is no launcher in $DIR/build."
        VERSION=$(info_get "$DIR/build/install-info.txt" version)
    fi
    if [ "$LAUNCH" = 1 ]; then
        step "Starting the launcher"
        launch
    fi
    finish_ok "${VERSION:-unknown}"
}

run_install() {
    if [ "$UPDATE" = 1 ]; then
        { [ -e "$DIR/channel.txt" ] && [ -d "$SRC/.git" ]; } || die "There is no install in $DIR to update."
    fi
    # Only --dev on this very run gives main; --update included, channel.txt only records what is installed (ONE LINE
    # FOR EVERYONE).
    [ -n "$CHANNEL" ] || CHANNEL=stable
    if [ "$CHANNEL" = stable ] && [ "$(read_channel)" = dev ]; then
        say "This install followed main (--dev); it now follows the newest release. Add --dev to stay on main."
    fi

    # check, download, build, verify; then switch and start unless --stage
    STEP_TOTAL=4
    [ "$STAGE" = 1 ] || STEP_TOTAL=$((STEP_TOTAL + 1))
    [ "$STAGE" = 1 ] || [ "$LAUNCH" = 0 ] || STEP_TOTAL=$((STEP_TOTAL + 1))

    step "Checking this computer"
    check_computer

    step "Downloading the source and the build's tools"
    if [ -n "${TPW_INSTALL_STUB:-}" ]; then
        stub_source
    else
        pick_target
        mkdir -p "$DIR" || die "Couldn't make the folder $DIR."
        : >"$DIR/.tpw-installer" || die "Couldn't write in $DIR."
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

    if [ "$current" = 1 ]; then
        say "Already up to date: version $VERSION."
        # A build staged earlier is of another commit now: drop it, so that a later --swap-and-launch cannot switch to it.
        remove_own build-next
        step "Building the launcher"
        say "Nothing to build."
        step "Checking the new launcher"
        say "Nothing to check."
        if [ "$(read_channel)" != "$CHANNEL" ]; then
            write_channel "$CHANNEL"
            sed "s/^channel=.*/channel=$CHANNEL/" "$DIR/build/install-info.txt" >"$DIR/build/install-info.tmp" \
                && mv -f "$DIR/build/install-info.tmp" "$DIR/build/install-info.txt"
        fi
    elif [ "$ready" = 1 ]; then
        say "This version was built earlier and is waiting in $NEXT."
        step "Building the launcher"
        say "Nothing to build."
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
            say "The new version is ready in $NEXT. Nothing was switched."
        fi
        finish_ok "$VERSION"
    fi
    step "Switching to the new version"
    if [ "$current" = 0 ]; then
        do_swap
    else
        say "Nothing to switch."
    fi
    # THE THEMEPARK COMMAND: the file at every switch, the PATH line only on an install
    play="Start it with: $DIR/build/tpw_launcher"
    every_window="From now on, type themepark in any new Terminal window to play. It works from any folder."
    if write_command; then
        play="To play later, run: $DIR/bin/themepark"
        if [ "$UPDATE" = 1 ]; then
            has_path_line && play=$every_window
        elif setup_path; then
            if new_shell_finds_command; then
                play=$every_window
            else
                # not a failure of the install: the launcher is in place, only the shortcut needs this
                say "A new Terminal window does not find themepark yet (your shell is ${SHELL:-unknown})."
                say "To fix it, run this line once, then open a new Terminal window:"
                say "  $(fix_line)"
            fi
        fi
    fi
    if [ "$LAUNCH" = 1 ]; then
        step "Starting the launcher"
        launch
    fi
    say "$play"
    [ "$UPDATE" = 1 ] || tell_old_copies
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
    GIT_TERMINAL_PROMPT=0   # a wrong address must fail, never wait for a password nobody sees
    export GIT_TERMINAL_PROMPT
    trap stopped INT TERM HUP

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
            -h | --help)
                usage
                exit 0
                ;;
            *) die "Unknown option $1 (--help lists them)." ;;
        esac
        shift
    done
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
