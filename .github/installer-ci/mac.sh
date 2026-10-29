#!/bin/bash
# shellcheck disable=SC2034  # SUBREPO, QUIET and PROFILE are read by common.sh's functions
# The broad installer cases for a Mac (installer-broad.yml runs them; they also run on a Mac by hand:
#   sh .github/installer-ci/mac.sh CASE...      with no CASE, every case but real and public).
#
# Most cases run install.sh for real (clone, tags, submodule, make, check, switch, themepark command) against a local
# fake release: a git repository made here, with tags vX.Y.Z, a Makefile that builds a stand-in launcher in a moment (or
# hangs, or fails, when a case asks), and a submodule. TPW_INSTALL_REPO points the script at it. Every case has its own
# empty home folder (HOME and TPW_INSTALL_HOME), so the real profile is never touched.
#
# Cases: fresh shells legacy rerun updater dev tags refusals paths missing buildfail interrupt interrupt_first
#   real     one real build, from a local tag of the CI checkout (needs TPW_CI_SOURCE: the checkout to take)
#   public   the one-liner a player runs, against the newest real release (manual workflow only)
# The checks, the fake release and the cases a Mac and Linux run alike are in common.sh, beside this file.
# Bash 3.2 (the Mac's own) is enough.

set -u

HERE=$(cd "$(dirname "$0")" && pwd)
INSTALL_SH=${INSTALL_SH:-$HERE/../../install.sh}
WORK=${TPW_CI_WORK:-$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/tpw-broad.XXXXXX")}
mkdir -p "$WORK"
CASE=""

if [ "$(uname -s)" != Darwin ]; then echo "This is for a Mac."; exit 2; fi

# a local file:// style clone and submodule (git 2.38.1 and newer refuse the file protocol in submodules otherwise)
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=protocol.file.allow GIT_CONFIG_VALUE_0=always
export GIT_TERMINAL_PROMPT=0

# shellcheck source=common.sh
. "$HERE/common.sh"

# ---- one case's world

# New case NAME: an empty home folder (HOME_DIR, default under the case's folder), the player's shell, the fake release.
new_case() { # NAME [HOME_DIR]
    CASE=$1
    C=$WORK/$1
    mkdir -p "$C"
    H=${2:-$C/home}
    mkdir -p "$H"
    H=$(cd "$H" && pwd -P)
    HOME=$H TPW_INSTALL_HOME=$H SHELL=/bin/zsh
    export HOME TPW_INSTALL_HOME SHELL
    FAKE=$C/fake
    SUBREPO=$C/sub
    TPW_INSTALL_REPO=$FAKE
    TPW_INSTALL_APPS=$C/apps   # stands for /Applications: the application goes here, never in the runner's own
    export TPW_INSTALL_APPS
    export TPW_INSTALL_REPO
    unset TPW_INSTALL_STUB TPW_INSTALL_STUB_VERSION TPW_FAKE_BUILD ZDOTDIR
    TPW_FAKE_SLEEP=604
    export TPW_FAKE_SLEEP
    DIR=$H/Applications/STP-TPW-Launcher
    DATA="$H/Library/Application Support/STP-TPW Launcher"
    PROFILE=.zprofile
    cd "$H" || exit 1
    echo "== case $CASE: home $H"
}

# The script as a file, run in a terminal window: script(1) gives it a tty, so it shows the progress view (the frame,
# redrawn in place, with the escape codes kept in OUT). An 80-column window, UTF-8.
run_tty() { # OUT SCRIPT ARGS...
    local out=$1 script=$2
    shift 2
    TERM=xterm-256color LC_ALL=en_US.UTF-8 script -q "$out" sh "$script" "$@" >/dev/null 2>&1 </dev/null
    RC=$?
    show_run "$out"
}

# ---- looking at what is there

# Everything about a folder: each entry's name, kind and permissions, size, modification time and link target, and each file's SHA-256.
snap() { # FOLDER
    (cd "$1" && {
        find . -exec stat -f '%N %Sp %z %m %Y' {} + | sort
        find . -type f -exec shasum -a 256 {} + | sort -k2
    })
}

seed_data() {
    mkdir -p "$DATA/games/us/Theme Park World" "$DATA/saves/T5L48" "$DATA/packs" "$DATA/logs"
    printf '[launcher]\nfullscreen=0\nvolume=7\nlanguage=en\n' >"$DATA/launcher.ini"
    head -c 300000 /dev/urandom >"$DATA/games/us/Theme Park World/Data.wad"
    head -c 4096 /dev/urandom >"$DATA/saves/T5L48/park1.sav"
    printf 'a log line\n' >"$DATA/logs/launcher.log"
}

# A new Terminal window of the player's shell: a login shell with a bare environment. Types themepark --version from
# another folder: the version line must show in the window (themepark runs the launcher in the foreground for it), and
# the stand-in launcher must have been started with --version, as version VERSION.
check_new_shell() { # DIR VERSION [SHELL]
    local dir=$1 v=$2 shell=${3:-${SHELL:-/bin/zsh}} want seen out
    want="STP/TPW Launcher v$v args=--version"
    seen=$(launch_count "$dir" "$want")
    out=$(cd / && env -i HOME="$HOME" USER="${USER:-}" TERM=dumb PATH=/usr/bin:/bin:/usr/sbin:/sbin "$shell" -l -c \
        'command -v themepark; themepark --version; echo "exit=$?"' </dev/null 2>&1)
    echo "$out"
    case "$out" in "$dir/bin/themepark"*) ;; *) bad "a new $shell window finds themepark at \"${out%%$'\n'*}\", not $dir/bin/themepark" ;; esac
    case "$out" in *"exit=0") ;; *) bad "themepark --version in a new $shell window did not exit with 0" ;; esac
    case "$out" in *"STP/TPW Launcher v$v"*) ;; *) bad "themepark --version in a new $shell window did not print the version line of $v" ;; esac
    wait_for_count "$dir" "$want" "$seen" || bad "themepark --version in a new $shell window did not start version $v"
}

# themepark ARGS... typed in a new Terminal window (a login shell with a bare environment, plus what the fake build
# needs), from another folder: the output in OUT, the exit code in RC. For themepark update, which runs the installer in
# source/ in the window.
run_themepark() { # OUT ARGS...
    local out=$1
    shift
    (cd / && env -i HOME="$HOME" USER="${USER:-}" TERM=dumb PATH=/usr/bin:/bin:/usr/sbin:/sbin \
        TPW_INSTALL_HOME="$TPW_INSTALL_HOME" TPW_INSTALL_APPS="$TPW_INSTALL_APPS" TPW_INSTALL_REPO="$TPW_INSTALL_REPO" \
        TPW_FAKE_SLEEP="$TPW_FAKE_SLEEP" GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=protocol.file.allow \
        GIT_CONFIG_VALUE_0=always GIT_TERMINAL_PROMPT=0 "${SHELL:-/bin/zsh}" -l -c 'themepark "$@"' themepark "$@") \
        >"$out" 2>&1 </dev/null
    RC=$?
    show_run "$out"
}

# The application in the folder that stands for /Applications (TPW_INSTALL_APPS): named for the Finder, its Info.plist
# names STP/TPW Launcher and carries the version's numbers, its program is the installer's and starts the launcher of DIR
# (the stand-in notes "args= cwd=DIR" once more). ICON=1: the icon is there too (a real build).
check_app() { # DIR VERSION [ICON]
    local app="$TPW_INSTALL_APPS/STP:TPW Launcher.app" exe before
    exe="$app/Contents/MacOS/STP-TPW-Launcher"
    [ -d "$app" ] || bad "$app is missing"
    grep -qF '<string>STP/TPW Launcher</string>' "$app/Contents/Info.plist" || bad "the application's Info.plist does not name STP/TPW Launcher"
    grep -qF "<string>${2%-dev}</string>" "$app/Contents/Info.plist" || bad "the application's Info.plist does not carry version ${2%-dev}"
    grep -qF '<string>org.tpw-port.launcher</string>' "$app/Contents/Info.plist" || bad "the application's Info.plist has not the launcher's identifier"
    expect_in "$exe" '# tpw-port installer: the STP/TPW Launcher application'
    [ -x "$exe" ] || bad "the application's program is not executable"
    expect_eq "PkgInfo" "$(cat "$app/Contents/PkgInfo")" 'APPL????'
    if [ "${3:-0}" = 1 ]; then
        [ -s "$app/Contents/Resources/icon.icns" ] || bad "the application has no icon"
        grep -qF '<string>icon</string>' "$app/Contents/Info.plist" || bad "the application's Info.plist names no icon"
    else
        before=$(launch_count "$1" "STP/TPW Launcher v$2 args= cwd=$1")
        "$exe" || bad "the application's program did not run"
        wait_for_count "$1" "STP/TPW Launcher v$2 args= cwd=$1" "$before" || bad "the application did not start the launcher of $1"
    fi
}

# A PATH that holds only links to the commands the installer needs, minus the ones named: a Mac without them.
tool_farm() { # FOLDER OMITTED...
    local farm=$1 t skip o
    shift
    mkdir -p "$farm"
    for t in sh uname sw_vers xcode-select xcrun git make python3 curl df awk sed tr head tail sort cat ls dirname basename \
        mkdir rm mv cp ps kill sleep wc grep env tee date stat find printf cmp touch chmod ln pwd tar shasum; do
        skip=0
        for o in "$@"; do [ "$o" = "$t" ] && skip=1; done
        [ "$skip" = 1 ] && continue
        p=$(command -v "$t") && ln -s "$p" "$farm/$t"
    done
}

# ---- cases

# A fresh machine: no release yet (a plain message, nothing made), then the first release.
case_fresh() {
    new_case fresh
    fake_init
    ls -A "$H" >"$C/home-before.txt"

    run_line "$C/out0" --no-launch
    fail_run "$C/out0" 'No release has been published yet'
    expect_text "$C/out0" 'Nothing was changed.'
    ls -A "$H" >"$C/home-after.txt"
    cmp -s "$C/home-before.txt" "$C/home-after.txt" || bad "a run with no release made something in the home folder"

    fake_release 0.0.1
    # exactly as a player runs it, no options: the launcher is started at the end
    run_line "$C/out1"
    ok_run "$C/out1" 0.0.1
    expect_text "$C/out1" 'Newest release: 0.0.1'
    expect_text "$C/out1" 'From now on, type themepark in any new Terminal window to play.'
    check_build "$DIR" 0.0.1 stable
    check_app "$DIR" 0.0.1
    [ "$(git -C "$DIR/source" rev-parse HEAD)" = "$(git -C "$FAKE" rev-parse 'v0.0.1^{commit}')" ] || bad "the source is not at the tag's commit"
    expect_in "$DIR/build/install-info.txt" "commit=$(git -C "$FAKE" rev-parse 'v0.0.1^{commit}')"
    [ -f "$DIR/source/tpw-decomp/README" ] || bad "the submodule was not downloaded"
    for f in .tpw-installer last-build.log bin/themepark; do [ -e "$DIR/$f" ] || bad "$DIR/$f is missing"; done
    [ -x "$DIR/bin/themepark" ] || bad "bin/themepark is not executable"
    expect_eq "the PATH lines in ~/.zprofile" "$(marked_lines "$H/.zprofile")" 1
    # the launcher the line started (from DIR, detached)
    wait_for_count "$DIR" "STP/TPW Launcher v0.0.1 args= cwd=$DIR" 0 || bad "the install did not start the launcher"
    # nothing outside the install but the PATH line and ~/Applications
    expect_eq "the home folder" "$(ls -A "$H" | tr '\n' ' ')" ".zprofile Applications "
    [ ! -e "$DATA" ] || bad "the install made the launcher's data folder"
    check_new_shell "$DIR" 0.0.1
}

# The player's shell and profile files, each in an empty home.
case_shells() {
    local variant path0=$PATH
    for variant in zsh bash bash_profile_less bash_login zdotdir symlink nonewline foreign takeover; do
        PATH=$path0
        new_case "shells-$variant"
        fake_init
        fake_release 0.0.1
        case "$variant" in
            zsh) ;;
            bash)
                SHELL=/bin/bash
                ;;
            bash_profile_less)
                SHELL=/bin/bash
                printf '# my profile\n' >"$H/.profile"
                ;;
            bash_login)
                # bash reads the first of .bash_profile, .bash_login, .profile that exists
                SHELL=/bin/bash
                printf '# login\n' >"$H/.bash_login"
                printf '# profile\n' >"$H/.profile"
                ;;
            zdotdir)
                mkdir -p "$H/zdot"
                printf 'export ZDOTDIR="$HOME/zdot"\n' >"$H/.zshenv"
                ;;
            symlink)
                mkdir -p "$H/dotfiles"
                printf 'export EDITOR=vi\n' >"$H/dotfiles/zprofile"
                ln -s dotfiles/zprofile "$H/.zprofile"
                ;;
            nonewline)
                printf 'export EDITOR=vi' >"$H/.zprofile"
                ;;
            foreign)
                mkdir -p "$C/other"
                printf '#!/bin/sh\necho not ours\n' >"$C/other/themepark"
                chmod 755 "$C/other/themepark"
                PATH="$C/other:$PATH"
                ;;
            takeover) ;;
        esac
        export SHELL
        run_line "$C/out" --no-launch
        ok_run "$C/out" 0.0.1
        check_build "$DIR" 0.0.1 stable
        case "$variant" in
            zsh)
                expect_eq ".zprofile lines" "$(marked_lines "$H/.zprofile")" 1
                [ ! -e "$H/.bash_profile" ] || bad "zsh as the shell made a .bash_profile"
                check_new_shell "$DIR" 0.0.1 /bin/zsh
                ;;
            bash)
                expect_eq ".bash_profile lines" "$(marked_lines "$H/.bash_profile")" 1
                expect_eq ".zprofile lines" "$(marked_lines "$H/.zprofile")" 1
                check_new_shell "$DIR" 0.0.1 /bin/bash
                ;;
            bash_profile_less)
                expect_eq ".profile lines" "$(marked_lines "$H/.profile")" 1
                [ ! -e "$H/.bash_profile" ] || bad "a .bash_profile was made though .profile exists"
                expect_in "$H/.profile" '# my profile'
                check_new_shell "$DIR" 0.0.1 /bin/bash
                ;;
            bash_login)
                expect_eq ".bash_login lines" "$(marked_lines "$H/.bash_login")" 1
                expect_eq ".profile lines" "$(marked_lines "$H/.profile")" 0
                check_new_shell "$DIR" 0.0.1 /bin/bash
                ;;
            zdotdir)
                # a new window reads ~/zdot/.zprofile, not ~/.zprofile: the check says so and gives the line that fixes it
                expect_text "$C/out" 'A new Terminal window does not find themepark yet'
                fix=$(grep -A1 'run this line once' "$C/out" | tail -n 1 | sed 's/^  //')
                note "the fix line: $fix"
                env -i HOME="$H" TERM=dumb PATH=/usr/bin:/bin:/usr/sbin:/sbin /bin/zsh -c "$fix" || bad "the fix line failed"
                expect_text "$H/zdot/.zprofile" "$DIR/bin"
                check_new_shell "$DIR" 0.0.1 /bin/zsh
                ;;
            symlink)
                [ -L "$H/.zprofile" ] || bad ".zprofile stopped being a link"
                expect_eq "the dotfiles copy" "$(marked_lines "$H/dotfiles/zprofile")" 1
                expect_in "$H/dotfiles/zprofile" 'export EDITOR=vi'
                check_new_shell "$DIR" 0.0.1 /bin/zsh
                ;;
            nonewline)
                expect_in "$H/.zprofile" 'export EDITOR=vi'
                expect_eq ".zprofile lines" "$(marked_lines "$H/.zprofile")" 1
                check_new_shell "$DIR" 0.0.1 /bin/zsh
                ;;
            foreign)
                # a themepark this script did not make is not shadowed: no PATH line, and the player is told
                expect_text "$C/out" 'that this installer did not make, so it is left alone'
                [ ! -e "$H/.zprofile" ] || bad "a PATH line was written beside a foreign themepark"
                ;;
            takeover)
                # an install in another folder takes the command over: one marked line, now the new folder's
                run_line "$C/out2" --no-launch --dir "$C/second install"
                ok_run "$C/out2" 0.0.1
                expect_eq ".zprofile lines" "$(marked_lines "$H/.zprofile")" 1
                expect_text "$H/.zprofile" "$C/second install/bin"
                expect_no_text "$H/.zprofile" "$DIR/bin"
                check_new_shell "$C/second install" 0.0.1 /bin/zsh
                ;;
        esac
        # run again: the line is never doubled
        run_line "$C/outb" --no-launch
        case "$variant" in
            foreign) ;;
            *) ok_run "$C/outb" 0.0.1 ;;
        esac
    done
}

# The launcher's Update button: --update --stage, then --swap-and-launch --wait-pid, as install.sh in source/ runs them.
case_updater() {
    new_case updater
    fake_init
    fake_release 0.0.1
    run_line "$C/out1" --no-launch
    ok_run "$C/out1" 0.0.1
    seed_data
    snap "$DATA" >"$C/data-before.txt"
    cp "$H/.zprofile" "$C/zprofile-before"
    cd "$DIR" || exit 1
    # nothing new: nothing staged
    run_file "$C/u0" "$DIR/source/install.sh" --update --stage --dir "$DIR"
    ok_run "$C/u0" 0.0.1
    [ ! -e "$DIR/build-next" ] || bad "a stage with nothing new left a build-next"
    # a newer release: staged, built and checked, but the launcher in use is not changed
    fake_release 0.0.2
    run_file "$C/u1" "$DIR/source/install.sh" --update --stage --dir "$DIR"
    ok_run "$C/u1" 0.0.2
    expect_eq "STEP lines of a stage" "$(grep -c '^STEP ' "$C/u1")" 4
    check_build "$DIR" 0.0.1 stable
    expect_in "$DIR/build-next/install-info.txt" "version=0.0.2"
    check_new_shell "$DIR" 0.0.1
    # a newer one still replaces the staged one; the same one again finds it waiting
    fake_release 0.0.3
    run_file "$C/u2" "$DIR/source/install.sh" --update --stage --dir "$DIR"
    ok_run "$C/u2" 0.0.3
    expect_in "$DIR/build-next/install-info.txt" "version=0.0.3"
    run_file "$C/u3" "$DIR/source/install.sh" --update --stage --dir "$DIR"
    ok_run "$C/u3" 0.0.3
    expect_text "$C/u3" 'was built earlier and is waiting'
    # the swap waits for the launcher (a process of 3 seconds) to exit, and then for a game its Play started (a process
    # of 6 seconds whose command line is the game's own application in build/, launcher/game_app.h)
    sleep 3 &
    waitpid=$!
    bash -c 'exec -a "$1" sleep 6' bash "$DIR/build/Sim Theme Park.app/Contents/MacOS/Sim Theme Park" &
    gamepid=$!
    start=$(date +%s)
    run_file "$C/u4" "$DIR/source/install.sh" --swap-and-launch --wait-pid "$waitpid" --dir "$DIR"
    end=$(date +%s)
    ok_run "$C/u4" 0.0.3
    [ $((end - start)) -ge 5 ] || bad "the swap did not wait for the launcher and the game to exit ($((end - start)) seconds)"
    kill -0 "$gamepid" 2>/dev/null && bad "the swap went ahead while the game's process was running"
    wait "$gamepid" 2>/dev/null
    check_build "$DIR" 0.0.3 stable
    expect_in "$DIR/build-prev/install-info.txt" "version=0.0.1"
    [ ! -e "$DIR/build-next" ] || bad "the swap left a build-next"
    wait_for_count "$DIR" "STP/TPW Launcher v0.0.3 args= cwd=$DIR" 0 || bad "the swap did not start the new launcher"
    check_new_shell "$DIR" 0.0.3
    # nothing waiting: the swap only starts the launcher in use
    run_file "$C/u5" "$DIR/source/install.sh" --swap-and-launch --no-launch --dir "$DIR"
    ok_run "$C/u5" 0.0.3
    # an update never touches the PATH line, the data or the profile
    cmp -s "$C/zprofile-before" "$H/.zprofile" || bad "the updater changed ~/.zprofile"
    snap "$DATA" >"$C/data-after.txt"
    cmp -s "$C/data-before.txt" "$C/data-after.txt" || bad "the data folder was changed"
    # a source folder with a change of the player's own: stop, nothing switched
    fake_release 0.0.4
    printf 'mine\n' >>"$DIR/source/Makefile"
    run_file "$C/u6" "$DIR/source/install.sh" --update --stage --dir "$DIR"
    fail_run "$C/u6" 'The source folder has changes of its own'
    check_build "$DIR" 0.0.3 stable
    git -C "$DIR/source" checkout -q -- Makefile
    run_file "$C/u7" "$DIR/source/install.sh" --update --dir "$DIR" --no-launch
    ok_run "$C/u7" 0.0.4
    check_build "$DIR" 0.0.4 stable
    # themepark update, typed in a new window: the same --update, run by the command from source/ in the foreground
    fake_release 0.0.5
    run_themepark "$C/u8" update --no-launch
    ok_run "$C/u8" 0.0.5
    check_build "$DIR" 0.0.5 stable
    check_new_shell "$DIR" 0.0.5
    # refused while this install's launcher runs (a process whose command line is the launcher's path stands for it)
    bash -c 'exec -a "$1" sleep 30' bash "$DIR/build/tpw_launcher" &
    fakepid=$!
    run_themepark "$C/u9" update --no-launch
    kill "$fakepid" 2>/dev/null
    wait "$fakepid" 2>/dev/null
    [ "$RC" != 0 ] || bad "themepark update ran while the launcher was running"
    expect_text "$C/u9" "themepark: the launcher is running (process $fakepid)"
    grep -q '^STEP ' "$C/u9" && bad "themepark update started the installer while the launcher was running"
    check_build "$DIR" 0.0.5 stable
    # the same for a launcher running from the bundle it keeps beside itself (launcher/app_bundle.h) and for a game
    # its Play started, which runs from the game's own application in build/ (launcher/game_app.h)
    bash -c 'exec -a "$1" sleep 30' bash "$DIR/build/STP:TPW Launcher.app/Contents/MacOS/tpw_launcher" &
    fakepid=$!
    run_themepark "$C/u9b" update --no-launch
    kill "$fakepid" 2>/dev/null
    wait "$fakepid" 2>/dev/null
    [ "$RC" != 0 ] || bad "themepark update ran while the launcher was running from its bundle"
    expect_text "$C/u9b" "themepark: the launcher is running (process $fakepid)"
    bash -c 'exec -a "$1" sleep 30' bash "$DIR/build/Theme Park World.app/Contents/MacOS/Theme Park World" &
    fakepid=$!
    run_themepark "$C/u9c" update --no-launch
    kill "$fakepid" 2>/dev/null
    wait "$fakepid" 2>/dev/null
    [ "$RC" != 0 ] || bad "themepark update ran while a game was running"
    expect_text "$C/u9c" "themepark: a game started from the launcher is running (process $fakepid)"
    grep -q '^STEP ' "$C/u9c" && bad "themepark update started the installer while a game was running"
    check_build "$DIR" 0.0.5 stable
    # its options go to the installer: themepark update --dev moves the install to main
    run_themepark "$C/u10" update --dev --no-launch
    ok_run "$C/u10" 0.0.6-dev
    check_build "$DIR" 0.0.6-dev dev
}

# Folder names with spaces, apostrophes, non-ASCII letters and shell characters, for the home folder, the install folder
# and the repository.
case_paths() {
    local odd="Jo's Mac é ü 日本"
    new_case paths_home "$WORK/paths_home/$odd/home dir"
    FAKE="$C/fa ke's repo é"
    SUBREPO="$C/su b's é"
    TPW_INSTALL_REPO=$FAKE
    export TPW_INSTALL_REPO
    fake_init
    fake_release 0.0.1
    seed_data
    mkdir -p "$H/Downloads"
    git clone -q --recurse-submodules "$FAKE" "$H/Downloads/tpw-port"
    snap "$H/Downloads/tpw-port" >"$C/clone-before.txt"
    run_line "$C/o1" --no-launch
    ok_run "$C/o1" 0.0.1
    expect_in "$C/o1" "An older copy of the launcher is in $H/Downloads/tpw-port."
    check_build "$DIR" 0.0.1 stable
    expect_eq ".zprofile lines" "$(marked_lines "$H/.zprofile")" 1
    check_new_shell "$DIR" 0.0.1
    snap "$H/Downloads/tpw-port" >"$C/clone-after.txt"
    cmp -s "$C/clone-before.txt" "$C/clone-after.txt" || bad "the old clone was changed"
    # the home folder is also where the other install folders go
    new_case paths_dir
    fake_init
    fake_release 0.0.1
    local n=0 d
    for d in "$C/tpw port's é" "$C/日本語 ü" "$C/a (b) & c" "$C/semi;colon" "$C/percent %USERPROFILE% x" "$C/hash#tag" "$C/  two  spaces"; do
        n=$((n + 1))
        run_line "$C/o2-$n" --no-launch --dir "$d"
        ok_run "$C/o2-$n" 0.0.1
        check_build "$d" 0.0.1 stable
        check_new_shell "$d" 0.0.1
    done
    # a character the Makefile cannot hold in a folder name: refused before anything is made, in words
    for d in "$C/dollar \$HOME" "$C/quote \"q\"" "$C/back\`tick" "$C/back\\slash"; do
        n=$((n + 1))
        run_line "$C/o2-$n" --no-launch --dir "$d"
        fail_run "$C/o2-$n" 'breaks the build'
        [ ! -e "$d" ] || bad "$d was made though it was refused"
    done
    # relative, with a trailing slash, with =
    mkdir -p "$C/rel"
    cd "$C/rel" || exit 1
    run_line "$C/o3" --no-launch --dir "sub dir///"
    ok_run "$C/o3" 0.0.1
    check_build "$C/rel/sub dir" 0.0.1 stable
    run_line "$C/o4" --no-launch "--dir=eq dir"
    ok_run "$C/o4" 0.0.1
    check_build "$C/rel/eq dir" 0.0.1 stable
    run_line "$C/o4b" --no-launch --dir "./dot dir/"
    ok_run "$C/o4b" 0.0.1
    check_build "$C/rel/dot dir" 0.0.1 stable
    mkdir -p "$C/rel2"
    cd "$C/rel2" || exit 1
    run_line "$C/o4c" --no-launch --dir .
    ok_run "$C/o4c" 0.0.1
    check_build "$C/rel2" 0.0.1 stable
    # the default folder, with a ~/Applications that is missing, then one that is a link
    cd "$H" || exit 1
    mkdir -p "$C/real-apps"
    ln -s "$C/real-apps" "$H/Applications"
    run_line "$C/o5" --no-launch
    ok_run "$C/o5" 0.0.1
    [ -x "$C/real-apps/STP-TPW-Launcher/build/tpw_launcher" ] || bad "an Applications link was not followed"
}

# Tools the Mac lacks: a plain message that lists them, nothing made, exit not 0.
case_missing() {
    local what
    for what in git "python3 curl" "make" "git make python3 curl" clt; do
        new_case "missing-${what// /-}"
        fake_init
        fake_release 0.0.1
        farm=$C/farm
        ls -A "$H" >"$C/home-before.txt"
        if [ "$what" = clt ]; then
            mkdir -p "$C/no-developer-dir"
            tool_farm "$farm"
            DEVELOPER_DIR="$C/no-developer-dir" PATH="$farm" run_line "$C/out" --no-launch
            fail_run "$C/out" "Missing: Apple's Command Line Tools"
            expect_text "$C/out" 'xcode-select --install'
        else
            # shellcheck disable=SC2086  # one name per word
            tool_farm "$farm" $what
            PATH="$farm" run_line "$C/out" --no-launch
            fail_run "$C/out" '^RESULT fail Missing: '
            for t in $what; do expect_text "$C/out" "$t"; done
            expect_text "$C/out" 'Missing: '
            expect_text "$C/out" 'xcode-select --install'
        fi
        expect_text "$C/out" 'Nothing was changed.'
        ls -A "$H" >"$C/home-after.txt"
        cmp -s "$C/home-before.txt" "$C/home-after.txt" || bad "a run with tools missing made something in the home folder"
    done
    # no CMake and no ninja is not an error on a Mac: the build fetches its own CMake (the real job runs without them)
    # the missing tool, then it is there: the next run installs
    new_case missing-then-there
    fake_init
    fake_release 0.0.1
    tool_farm "$C/farm" git
    PATH="$C/farm" run_line "$C/out1" --no-launch
    fail_run "$C/out1" '^RESULT fail Missing: git'
    run_line "$C/out2" --no-launch
    ok_run "$C/out2" 0.0.1
    # with an install in place, a missing tool leaves it working
    fake_release 0.0.2
    tool_farm "$C/farm2" python3
    PATH="$C/farm2" run_line "$C/out3" --no-launch
    fail_run "$C/out3" '^RESULT fail Missing: python3'
    check_build "$DIR" 0.0.1 stable
    check_new_shell "$DIR" 0.0.1
}

# The real build, from a local tag in the CI checkout: TPW_CI_SOURCE is the checkout (a git working tree of tpw-port). Takes
# a copy of it, says its launcher/VERSION.txt is a release (TPW_CI_VERSION, default 0.0.99) and tags that. The run is the
# one a player makes, on a Mac with only Apple's command line tools (no CMake, no ninja, no Homebrew: the build fetches
# its CMake), in a home folder with a space, an apostrophe and a non-ASCII letter.
case_real() {
    [ -n "${TPW_CI_SOURCE:-}" ] || bad "TPW_CI_SOURCE (the checkout to build) is not set"
    local version=${TPW_CI_VERSION:-0.0.99}
    QUIET=1
    new_case real "$WORK/real/Jo's Mac é/home"
    seed_data
    mkdir -p "$H/Documents"
    mkdir -p "$H/Documents/tpw-port/.git" "$H/Documents/tpw-port/launcher" "$H/Documents/tpw-port/build"
    printf '0.0.2\n' >"$H/Documents/tpw-port/launcher/VERSION.txt"
    printf 'an old launcher\n' >"$H/Documents/tpw-port/build/tpw_launcher"
    snap "$H/Documents/tpw-port" >"$C/clone-before.txt"
    snap "$DATA" >"$C/data-before.txt"
    git clone -q "$TPW_CI_SOURCE" "$C/release-repo" || bad "cannot copy the checkout"
    git -C "$C/release-repo" checkout -q -B main
    printf '%s\n' "$version" >"$C/release-repo/launcher/VERSION.txt"
    git -C "$C/release-repo" add launcher/VERSION.txt
    git -C "$C/release-repo" -c user.name=ci -c user.email=ci@example.invalid -c commit.gpgsign=false commit -q -m "Release $version"
    git -C "$C/release-repo" tag "v$version"
    # The submodules' addresses are relative (../tpw-decomp), so they resolve beside the release repository: bare copies
    # of the checkout's submodules there, each with a branch on the pinned commit (no download from GitHub).
    git -C "$TPW_CI_SOURCE" config -f .gitmodules --get-regexp '\.path$' | while read -r key path; do
        url=$(git -C "$TPW_CI_SOURCE" config -f .gitmodules --get "${key%.path}.url")
        case "$url" in
            ../*) ;;
            *) echo "::error::[real] the submodule $path has the address $url, not a relative one"; exit 1 ;;
        esac
        git clone -q --bare "$TPW_CI_SOURCE/$path" "$C/${url#../}" || exit 1
        git -C "$C/${url#../}" branch -f ci-pin "$(git -C "$TPW_CI_SOURCE/$path" rev-parse HEAD)" || exit 1
    done || bad "cannot copy the submodules"
    TPW_INSTALL_REPO="$C/release-repo"
    export TPW_INSTALL_REPO
    note "the player's PATH: Apple's tools only"
    export PATH=/usr/bin:/bin:/usr/sbin:/sbin
    if command -v cmake >/dev/null 2>&1 || command -v ninja >/dev/null 2>&1; then bad "cmake or ninja is on the PATH"; fi
    run_line "$C/o1" --no-launch
    ok_run "$C/o1" "$version"
    expect_text "$C/o1" 'fetching CMake'
    check_build "$DIR" "$version" stable
    check_app "$DIR" "$version" 1
    "$DIR/build/tpw_launcher" --version </dev/null | tee "$C/version.txt"
    expect_in "$C/version.txt" "STP/TPW Launcher v$version"
    grep -q '^build: ' "$C/version.txt" || bad "the launcher printed no build line"
    check_new_shell_real "$DIR"
    expect_in "$C/o1" "An older copy of the launcher is in $H/Documents/tpw-port."
    # again: nothing to do
    run_line "$C/o2" --no-launch
    ok_run "$C/o2" "$version"
    expect_text "$C/o2" 'Already up to date'
    snap "$H/Documents/tpw-port" >"$C/clone-after.txt"
    snap "$DATA" >"$C/data-after.txt"
    cmp -s "$C/clone-before.txt" "$C/clone-after.txt" || bad "the old clone was changed"
    cmp -s "$C/data-before.txt" "$C/data-after.txt" || bad "the data folder was changed"
    expect_eq ".zprofile lines" "$(marked_lines "$H/.zprofile")" 1
}

# The real launcher answers --version through themepark in a new window: themepark runs it in the foreground for that,
# so its version and build lines show. This checks the command, the exit code, those lines and that the launcher
# process ends by itself.
check_new_shell_real() { # DIR
    local out
    out=$(cd / && env -i HOME="$HOME" USER="${USER:-}" TERM=dumb PATH=/usr/bin:/bin:/usr/sbin:/sbin "${SHELL:-/bin/zsh}" -l -c \
        'command -v themepark; themepark --version; echo "exit=$?"' </dev/null 2>&1)
    echo "$out"
    case "$out" in "$1/bin/themepark"*) ;; *) bad "a new window finds themepark at \"${out%%$'\n'*}\"" ;; esac
    case "$out" in *"exit=0") ;; *) bad "themepark --version in a new window did not exit with 0" ;; esac
    case "$out" in *"STP/TPW Launcher v"*) ;; *) bad "themepark --version in a new window did not print the version line" ;; esac
    case "$out" in *$'\nbuild: '*) ;; *) bad "themepark --version in a new window did not print the build line" ;; esac
    for _ in $(seq 30); do
        ps -A -o command= | grep -F "$1/build/tpw_launcher" | grep -vq grep || return 0
        sleep 1
    done
    bad "the launcher started by themepark --version is still running after 30 seconds"
}

# The one-liner a player runs, against the newest real release (or main with PUBLIC_ARGS=--dev), in an empty home folder
# that holds an old clone and a data folder.
case_public() {
    new_case public
    QUIET=1
    unset TPW_INSTALL_REPO
    seed_data
    mkdir -p "$H/Downloads/tpw-port/.git" "$H/Downloads/tpw-port/launcher"
    printf '0.0.2\n' >"$H/Downloads/tpw-port/launcher/VERSION.txt"
    snap "$H/Downloads/tpw-port" >"$C/clone-before.txt"
    snap "$DATA" >"$C/data-before.txt"
    export PATH=/usr/bin:/bin:/usr/sbin:/sbin
    # shellcheck disable=SC2086  # PUBLIC_ARGS is a list of options
    curl -fsSL https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.sh | sh -s -- ${PUBLIC_ARGS:-} >"$C/o1" 2>&1
    RC=$?
    show_run "$C/o1"
    last=$(tail -n 1 "$C/o1")
    case "$last" in "RESULT ok "*) ;; *) bad "the one-liner did not end with RESULT ok: $last" ;; esac
    [ "$RC" = 0 ] || bad "the one-liner exited with $RC"
    version=${last#RESULT ok }
    check_build "$DIR" "$version" "$([ -n "${PUBLIC_ARGS:-}" ] && echo dev || echo stable)"
    expect_in "$C/o1" "An older copy of the launcher is in $H/Downloads/tpw-port."
    # the line started the real launcher; give it a moment, then end it
    sleep 5
    if ps -A -o command= | grep -F "$DIR/build/tpw_launcher" | grep -vq grep; then
        note "the launcher is running"
        pkill -f "$DIR/build/tpw_launcher"
    else
        echo "::warning::[public] the launcher the line started was not running after 5 seconds"
    fi
    check_new_shell_real "$DIR"
    # shellcheck disable=SC2086  # PUBLIC_ARGS is a list of options
    curl -fsSL https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.sh | sh -s -- --no-launch ${PUBLIC_ARGS:-} >"$C/o2" 2>&1
    RC=$?
    show_run "$C/o2"
    ok_run "$C/o2" "$version"
    expect_text "$C/o2" 'Already up to date'
    snap "$H/Downloads/tpw-port" >"$C/clone-after.txt"
    snap "$DATA" >"$C/data-after.txt"
    cmp -s "$C/clone-before.txt" "$C/clone-after.txt" || bad "the old clone was changed"
    cmp -s "$C/data-before.txt" "$C/data-after.txt" || bad "the data folder was changed"
    expect_eq ".zprofile lines" "$(marked_lines "$H/.zprofile")" 1
}

# ---- main

main() {
    if [ $# -eq 0 ]; then
        set -- fresh shells legacy rerun updater dev tags refusals paths missing buildfail interrupt interrupt_first
    fi
    run_cases "$@"
}

main "$@"
