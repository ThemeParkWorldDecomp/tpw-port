#!/bin/bash
# shellcheck disable=SC2034  # SUBREPO, QUIET and PROFILE are read by common.sh's functions
# The broad installer cases for Linux (installer-broad.yml runs them; they also run on a Linux machine or in a container
# by hand:
#   bash .github/installer-ci/linux.sh CASE...      with no CASE, every case but packages_real, real and public).
#
# The cases of a Mac (mac.sh), against the same local fake release, with what is Linux's own in place of the Mac's: the
# default folder ~/.local/share/STP-TPW-Launcher, ~/.profile and ~/.bashrc in place of ~/.zprofile, the menu entry in
# place of the application, the build's packages in place of Apple's Command Line Tools, and the Flatpak install. Every
# case has its own empty home folder (HOME and TPW_INSTALL_HOME); XDG_DATA_HOME is unset, so the menu entry goes in that
# home too. The fake builds need no package, so the cases run with TPW_INSTALL_PACKAGES=skip, all but packages (the
# package step against stand-ins for the package tools and sudo) and packages_real and real (the system's own package
# manager: as root, or with a sudo that asks no password, as on a CI runner).
#
# Cases: fresh shells legacy rerun updater dev tags refusals paths packages buildfail interrupt interrupt_first flatpak
#   packages_real  the package step for real: the build's packages installed with the system's package manager
#   real     one real build, from a local tag of the CI checkout (needs TPW_CI_SOURCE: the checkout to take); with
#            TPW_CI_UPDATE=1, then the launcher's Update button's run against a newer local tag (a second build)
#   public   the one-liner a player runs, against the newest real release (manual workflow only)
# The checks, the fake release and the cases a Mac and Linux run alike are in common.sh, beside this file.

set -u

HERE=$(cd "$(dirname "$0")" && pwd)
INSTALL_SH=${INSTALL_SH:-$HERE/../../install.sh}
WORK=${TPW_CI_WORK:-$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/tpw-broad.XXXXXX")}
mkdir -p "$WORK"
CASE=""

if [ "$(uname -s)" != Linux ]; then echo "This is for Linux."; exit 2; fi

# a local file:// style clone and submodule (git 2.38.1 and newer refuse the file protocol in submodules otherwise)
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=protocol.file.allow GIT_CONFIG_VALUE_0=always
export GIT_TERMINAL_PROMPT=0
# a container's root has no USER
USER=${USER:-$(id -un 2>/dev/null || echo player)}
export USER

# shellcheck source=common.sh
. "$HERE/common.sh"

# ---- one case's world

# New case NAME: an empty home folder (HOME_DIR, default under the case's folder), the player's shell (bash, most
# distributions' own), the fake release.
new_case() { # NAME [HOME_DIR]
    CASE=$1
    C=$WORK/$1
    mkdir -p "$C"
    H=${2:-$C/home}
    mkdir -p "$H"
    H=$(cd "$H" && pwd -P)
    HOME=$H TPW_INSTALL_HOME=$H SHELL=/bin/bash
    export HOME TPW_INSTALL_HOME SHELL
    FAKE=$C/fake
    SUBREPO=$C/sub
    TPW_INSTALL_REPO=$FAKE
    export TPW_INSTALL_REPO
    TPW_INSTALL_PACKAGES=skip   # the fake builds need no package (the package cases unset it)
    export TPW_INSTALL_PACKAGES
    unset TPW_INSTALL_STUB TPW_INSTALL_STUB_VERSION TPW_FAKE_BUILD ZDOTDIR XDG_DATA_HOME TPW_INSTALL_APPS
    TPW_FAKE_SLEEP=604
    export TPW_FAKE_SLEEP
    DIR=$H/.local/share/STP-TPW-Launcher
    DATA="$H/.local/share/STP-TPW Launcher"
    APPS=$H/.local/share/applications
    PROFILE=.profile
    cd "$H" || exit 1
    echo "== case $CASE: home $H"
}

# The script as a file, run in a terminal window: script(1) (util-linux's) gives it a tty, so it shows the progress
# view (the frame, redrawn in place, with the escape codes kept in OUT). UTF-8.
run_tty() { # OUT SCRIPT ARGS...
    local out=$1 script=$2 cmd
    shift 2
    command -v script >/dev/null 2>&1 || bad "script(1) (util-linux) is needed for a terminal window"
    cmd=$(printf '%q ' sh "$script" "$@")
    TERM=xterm-256color LC_ALL=C.UTF-8 script -q -e -c "$cmd" "$out" >/dev/null 2>&1 </dev/null
    RC=$?
    show_run "$out"
}

# ---- looking at what is there

# Everything about a folder: each entry's name, kind and permissions, size, modification time and link target, and each file's SHA-256.
snap() { # FOLDER
    (cd "$1" && {
        find . -exec stat -c '%n %A %s %Y %N' {} + | sort
        find . -type f -exec sha256sum {} + | sort -k2
    })
}

seed_data() {
    mkdir -p "$DATA/games/us/Theme Park World" "$DATA/saves/T5L48" "$DATA/packs" "$DATA/logs"
    printf '[launcher]\nfullscreen=0\nvolume=7\nlanguage=en\n' >"$DATA/launcher.ini"
    head -c 300000 /dev/urandom >"$DATA/games/us/Theme Park World/Data.wad"
    head -c 4096 /dev/urandom >"$DATA/saves/T5L48/park1.sav"
    printf 'a log line\n' >"$DATA/logs/launcher.log"
}

# A new terminal window of the player's shell, with a bare environment, types themepark --version from another folder:
# the version line must show in the window (themepark runs the launcher in the foreground for it), and the stand-in
# launcher must have been started with --version, as version VERSION. Both kinds of window: an interactive shell that is
# not a login shell (what a terminal window starts on Linux: bash reads ~/.bashrc, zsh ~/.zshrc), and a login shell
# (a console or ssh: ~/.profile and the like; sh reads nothing else).
check_new_shell() { # DIR VERSION [SHELL]
    local dir=$1 v=$2 shell=${3:-${SHELL:-/bin/bash}} want seen out mode
    want="STP/TPW Launcher v$v args=--version"
    for mode in -l -i; do
        case "$mode $shell" in "-i "*/sh | "-i "*/dash) continue ;; esac
        seen=$(launch_count "$dir" "$want")
        # (an interactive shell with no terminal says so on stderr: only standard output is read)
        out=$(cd / && env -i HOME="$HOME" USER="${USER:-}" TERM=dumb PATH=/usr/bin:/bin:/usr/sbin:/sbin "$shell" "$mode" -c \
            'command -v themepark; themepark --version; echo "exit=$?"' </dev/null 2>/dev/null)
        echo "$out"
        case "$out" in "$dir/bin/themepark"*) ;; *) bad "a new $shell $mode window finds themepark at \"${out%%$'\n'*}\", not $dir/bin/themepark" ;; esac
        case "$out" in *"exit=0") ;; *) bad "themepark --version in a new $shell $mode window did not exit with 0" ;; esac
        case "$out" in *"STP/TPW Launcher v$v"*) ;; *) bad "themepark --version in a new $shell $mode window did not print the version line of $v" ;; esac
        wait_for_count "$dir" "$want" "$seen" || bad "themepark --version in a new $shell $mode window did not start version $v"
    done
}

# themepark ARGS... typed in a new terminal window (a login shell with a bare environment, plus what the fake build
# needs), from another folder: the output in OUT, the exit code in RC. For themepark update, which runs the installer in
# source/ in the window.
run_themepark() { # OUT ARGS...
    local out=$1
    shift
    (cd / && env -i HOME="$HOME" USER="${USER:-}" TERM=dumb PATH=/usr/bin:/bin:/usr/sbin:/sbin \
        TPW_INSTALL_HOME="$TPW_INSTALL_HOME" TPW_INSTALL_REPO="$TPW_INSTALL_REPO" \
        TPW_INSTALL_PACKAGES="${TPW_INSTALL_PACKAGES:-}" TPW_FAKE_SLEEP="$TPW_FAKE_SLEEP" GIT_CONFIG_COUNT=1 \
        GIT_CONFIG_KEY_0=protocol.file.allow GIT_CONFIG_VALUE_0=always GIT_TERMINAL_PROMPT=0 \
        "${SHELL:-/bin/bash}" -l -c 'themepark "$@"' themepark "$@") >"$out" 2>&1 </dev/null
    RC=$?
    show_run "$out"
}

# The menu entry (in the case's ~/.local/share/applications, or TPW_INSTALL_APPS): the installer's, named STP/TPW
# Launcher, its Exec, TryExec and Path pointing at DIR, and valid for desktop-file-validate when that is installed; its
# Exec, run in its Path as a menu runs it, starts the launcher of DIR (the stand-in notes "args= cwd=DIR" once more).
# ICON=1: the icon is there too (a real build), alone, in the hicolor folder of its own size.
check_entry() { # DIR VERSION [ICON]
    local apps=${TPW_INSTALL_APPS:-$APPS} entry exe path before icons png size
    entry=$apps/stp-tpw-launcher.desktop
    [ -f "$entry" ] || bad "$entry is missing"
    expect_in "$entry" '# tpw-port installer: the STP/TPW Launcher menu entry'
    expect_in "$entry" 'Name=STP/TPW Launcher'
    expect_in "$entry" "TryExec=$1/build/tpw_launcher"
    expect_in "$entry" "Path=$1"
    exe=$(sed -n 's/^Exec="\(.*\)"$/\1/p' "$entry" | sed 's/%%/%/g')
    expect_eq "the entry's program" "$exe" "$1/build/tpw_launcher"
    path=$(sed -n 's/^Path=//p' "$entry")
    if command -v desktop-file-validate >/dev/null 2>&1; then
        desktop-file-validate "$entry" || bad "desktop-file-validate refuses $entry"
    fi
    if [ "${3:-0}" = 1 ]; then
        expect_in "$entry" 'Icon=stp-tpw-launcher'
        icons="$(dirname "$apps")/icons/hicolor"
        png=$(ls "$icons"/*/apps/stp-tpw-launcher.png 2>/dev/null)
        [ -n "$png" ] && [ "$(printf '%s\n' "$png" | wc -l)" = 1 ] || bad "not one icon in $icons: $png"
        size=$(od -An -tu1 -j16 -N4 "$png" | awk '{ print $1 * 16777216 + $2 * 65536 + $3 * 256 + $4 }')
        case "$png" in "$icons/${size}x${size}/apps/"*) ;; *) bad "the icon of $size pixels is in another size's folder: $png" ;; esac
        note "the icon: $png"
    else
        before=$(launch_count "$1" "STP/TPW Launcher v$2 args= cwd=$1")
        (cd "$path" && "$exe" </dev/null >/dev/null 2>&1) || bad "the entry's program did not run"
        wait_for_count "$1" "STP/TPW Launcher v$2 args= cwd=$1" "$before" || bad "the entry did not start the launcher of $1"
    fi
}

# A PATH that holds only links to the commands the installer and the fake build need, minus the ones named: a Linux
# without them. The package tools and sudo are not linked (fake_packages puts its stand-ins beside them).
tool_farm() { # FOLDER OMITTED...
    local farm=$1 t skip o p
    shift
    mkdir -p "$farm"
    for t in sh bash uname git make python3 df awk sed tr head tail sort cat ls dirname basename mkdir rm mv cp ps kill \
        sleep wc grep env tee date stat find printf cmp touch chmod ln pwd od mktemp nohup hostname setsid script true; do
        skip=0
        for o in "$@"; do [ "$o" = "$t" ] && skip=1; done
        [ "$skip" = 1 ] && continue
        # the program's own file, also for a builtin such as true or printf (a stand-in may exec it)
        p=$(type -P "$t") && ln -s "$p" "$farm/$t"
    done
}

# Stand-ins for apt's tools, sudo and id in the farm FOLDER: dpkg-query answers from the file $C/pkg-state (the
# installed packages, one a line), apt-get install adds to it, and every call goes to $C/pkg-log. FAKE_UID is what id -u
# says (1000 by default); FAKE_SUDO says what sudo does: nopasswd (runs anything), password (asks: -n fails, -v works
# only with a terminal to ask on, and then -n runs anything), or none (no sudo at all). FAKE_APT_FAIL=1 makes the install
# fail, FAKE_APT_DROP=NAME leaves that package out of it.
fake_packages() { # FOLDER
    local farm=$1
    : >>"$C/pkg-state"
    : >>"$C/pkg-log"
    cat >"$farm/dpkg-query" <<EOF
#!/bin/sh
echo "dpkg-query \$*" >>"$C/pkg-log"
rc=0
for a in "\$@"; do
    case "\$a" in -*) continue ;; esac
    if grep -qxF "\$a" "$C/pkg-state"; then echo "\$a install ok installed"; else echo "dpkg-query: no packages found matching \$a" >&2; rc=1; fi
done
exit \$rc
EOF
    cat >"$farm/apt-get" <<EOF
#!/bin/sh
echo "apt-get \$*" >>"$C/pkg-log"
[ "\$1" = update ] && exit 0
[ "\${FAKE_APT_FAIL:-0}" = 1 ] && { echo "E: Unable to locate package something"; exit 100; }
for a in "\$@"; do
    case "\$a" in -* | install) continue ;; esac
    [ "\$a" = "\${FAKE_APT_DROP:-}" ] || echo "\$a" >>"$C/pkg-state"
done
echo "Setting up the packages (a stand-in)"
EOF
    cat >"$farm/id" <<EOF
#!/bin/sh
case "\$*" in
    -u) echo "\${FAKE_UID:-1000}" ;;
    -un) echo player ;;
    *) exec "$(command -v id)" "\$@" ;;
esac
EOF
    cat >"$farm/sudo" <<EOF
#!/bin/sh
echo "sudo \$*" >>"$C/pkg-log"
mode=\${FAKE_SUDO:-nopasswd}
if [ "\$1" = -v ]; then
    [ "\$mode" = password ] && (: </dev/tty) 2>/dev/null && : >"$C/sudo-ok" && exit 0
    exit 1
fi
[ "\$1" = -n ] && shift
[ "\$mode" = nopasswd ] || [ -e "$C/sudo-ok" ] || { echo "sudo: a password is required" >&2; exit 1; }
exec "\$@"
EOF
    chmod 755 "$farm/dpkg-query" "$farm/apt-get" "$farm/id" "$farm/sudo"
    [ "${FAKE_SUDO:-nopasswd}" != none ] || rm -f "$farm/sudo"
}

# The packages install.sh asks apt for, from the script itself.
apt_packages() {
    sed -n '/^PKGS_APT="/,/"$/p' "$INSTALL_SH" | tr -d '"' | sed 's/^PKGS_APT=//' | tr '\n' ' ' | sed 's/  */ /g; s/ $//'
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
    expect_text "$C/out1" 'From now on, type themepark in any new terminal window to play.'
    expect_in "$C/out1" 'STEP 6/7 Adding STP/TPW Launcher to the applications menu'
    check_build "$DIR" 0.0.1 stable
    check_entry "$DIR" 0.0.1
    [ "$(git -C "$DIR/source" rev-parse HEAD)" = "$(git -C "$FAKE" rev-parse 'v0.0.1^{commit}')" ] || bad "the source is not at the tag's commit"
    expect_in "$DIR/build/install-info.txt" "commit=$(git -C "$FAKE" rev-parse 'v0.0.1^{commit}')"
    [ -f "$DIR/source/tpw-decomp/README" ] || bad "the submodule was not downloaded"
    for f in .tpw-installer last-build.log bin/themepark; do [ -e "$DIR/$f" ] || bad "$DIR/$f is missing"; done
    [ -x "$DIR/bin/themepark" ] || bad "bin/themepark is not executable"
    expect_eq "the PATH lines in ~/.profile" "$(marked_lines "$H/.profile")" 1
    expect_eq "the PATH lines in ~/.bashrc" "$(marked_lines "$H/.bashrc")" 1
    # the launcher the line started (from DIR, detached)
    wait_for_count "$DIR" "STP/TPW Launcher v0.0.1 args= cwd=$DIR" 0 || bad "the install did not start the launcher"
    # nothing outside the install but the PATH lines and the menu entry
    expect_eq "the home folder" "$(LC_ALL=C ls -A "$H" | tr '\n' ' ')" ".bashrc .local .profile "
    expect_eq "the share folder" "$(LC_ALL=C ls -A "$H/.local/share" | tr '\n' ' ')" "STP-TPW-Launcher applications "
    [ ! -e "$DATA" ] || bad "the install made the launcher's data folder"
    check_new_shell "$DIR" 0.0.1
}

# The player's shell and profile files, each in an empty home.
case_shells() {
    local variant path0=$PATH fix
    for variant in bash sh bash_profile bash_login zsh zdotdir symlink nonewline once foreign takeover flatpak_first; do
        PATH=$path0
        case "$variant" in
            zsh | zdotdir)
                if ! command -v zsh >/dev/null 2>&1; then
                    echo "::warning::[shells-$variant] zsh is not installed here; skipped"
                    continue
                fi ;;
        esac
        new_case "shells-$variant"
        fake_init
        fake_release 0.0.1
        case "$variant" in
            bash | once) ;;
            sh) SHELL=/bin/sh ;;
            bash_profile)
                # bash then reads ~/.bash_profile and not ~/.profile when it is a login shell
                printf '# my profile\n' >"$H/.bash_profile"
                ;;
            bash_login)
                printf '# login\n' >"$H/.bash_login"
                ;;
            zsh) SHELL=$(command -v zsh) ;;
            zdotdir)
                SHELL=$(command -v zsh)
                mkdir -p "$H/zdot"
                printf 'export ZDOTDIR="$HOME/zdot"\n' >"$H/.zshenv"
                ;;
            symlink)
                mkdir -p "$H/dotfiles"
                printf 'export EDITOR=vi\n' >"$H/dotfiles/profile"
                ln -s dotfiles/profile "$H/.profile"
                ;;
            nonewline)
                printf 'export EDITOR=vi' >"$H/.profile"
                ;;
            foreign)
                mkdir -p "$C/other"
                printf '#!/bin/sh\necho not ours\n' >"$C/other/themepark"
                chmod 755 "$C/other/themepark"
                PATH="$C/other:$PATH"
                ;;
            takeover) ;;
            flatpak_first)
                # the Flatpak install's command in ~/.local/bin, which its own line puts first on the PATH
                mkdir -p "$H/.local/bin"
                printf "#!/bin/sh\n# tpw-port installer: the Flatpak's themepark command\necho flatpak\n" >"$H/.local/bin/themepark"
                chmod 755 "$H/.local/bin/themepark"
                # shellcheck disable=SC2016  # for the profile to expand
                printf 'export PATH="$HOME/.local/bin:$PATH"  # tpw-port installer: the Flatpak'"'"'s themepark command\n' >"$H/.profile"
                cp "$H/.profile" "$H/.bashrc"
                ;;
        esac
        export SHELL
        run_line "$C/out" --no-launch
        ok_run "$C/out" 0.0.1
        check_build "$DIR" 0.0.1 stable
        case "$variant" in
            bash)
                expect_eq ".profile lines" "$(marked_lines "$H/.profile")" 1
                expect_eq ".bashrc lines" "$(marked_lines "$H/.bashrc")" 1
                [ ! -e "$H/.bash_profile" ] || bad "a .bash_profile was made"
                check_new_shell "$DIR" 0.0.1 /bin/bash
                ;;
            sh)
                expect_eq ".profile lines" "$(marked_lines "$H/.profile")" 1
                [ ! -e "$H/.bashrc" ] || bad "sh as the shell wrote a .bashrc"
                check_new_shell "$DIR" 0.0.1 /bin/sh
                ;;
            bash_profile)
                expect_eq ".bash_profile lines" "$(marked_lines "$H/.bash_profile")" 1
                expect_in "$H/.bash_profile" '# my profile'
                expect_eq ".profile lines" "$(marked_lines "$H/.profile")" 1
                expect_eq ".bashrc lines" "$(marked_lines "$H/.bashrc")" 1
                check_new_shell "$DIR" 0.0.1 /bin/bash
                ;;
            bash_login)
                expect_eq ".bash_login lines" "$(marked_lines "$H/.bash_login")" 1
                expect_eq ".bashrc lines" "$(marked_lines "$H/.bashrc")" 1
                check_new_shell "$DIR" 0.0.1 /bin/bash
                ;;
            zsh)
                for f in .profile .zprofile .zshrc; do expect_eq "$f lines" "$(marked_lines "$H/$f")" 1; done
                check_new_shell "$DIR" 0.0.1 "$SHELL"
                ;;
            zdotdir)
                # a new window reads ~/zdot's files, not ~/.zshrc: the check says so and gives the line that fixes it
                expect_text "$C/out" 'A new terminal window does not find themepark yet'
                fix=$(grep -A1 'run this line once' "$C/out" | tail -n 1 | sed 's/^  //')
                note "the fix line: $fix"
                env -i HOME="$H" ZDOTDIR="$H/zdot" TERM=dumb PATH=/usr/bin:/bin:/usr/sbin:/sbin "$SHELL" -c "$fix" || bad "the fix line failed"
                expect_text "$H/zdot/.zshrc" "$DIR/bin"
                out=$(cd / && env -i HOME="$H" TERM=dumb PATH=/usr/bin:/bin:/usr/sbin:/sbin "$SHELL" -i -c 'command -v themepark' 2>/dev/null </dev/null)
                expect_eq "a new zsh window after the fix" "$out" "$DIR/bin/themepark"
                ;;
            symlink)
                [ -L "$H/.profile" ] || bad ".profile stopped being a link"
                expect_eq "the dotfiles copy" "$(marked_lines "$H/dotfiles/profile")" 1
                expect_in "$H/dotfiles/profile" 'export EDITOR=vi'
                check_new_shell "$DIR" 0.0.1 /bin/bash
                ;;
            nonewline)
                expect_in "$H/.profile" 'export EDITOR=vi'
                expect_eq ".profile lines" "$(marked_lines "$H/.profile")" 1
                check_new_shell "$DIR" 0.0.1 /bin/bash
                ;;
            once)
                # a login shell that reads both files (Ubuntu's ~/.profile reads ~/.bashrc) has DIR/bin once on its PATH
                printf '. "$HOME/.bashrc"\n' >>"$H/.profile"
                out=$(cd / && env -i HOME="$H" TERM=dumb PATH=/usr/bin:/bin bash -l -c 'printf "%s\n" "$PATH"' 2>/dev/null </dev/null)
                note "a login shell's PATH: $out"
                expect_eq "DIR/bin on a login shell's PATH" "$(printf '%s\n' "$out" | tr ':' '\n' | grep -cxF "$DIR/bin")" 1
                ;;
            foreign)
                # a themepark this script did not make is not shadowed: no PATH line, and the player is told
                expect_text "$C/out" 'that this installer did not make, so it is left alone'
                [ ! -e "$H/.profile" ] || bad "a PATH line was written beside a foreign themepark"
                ;;
            takeover)
                # an install in another folder takes the command over: one marked line a file, now the new folder's
                run_line "$C/out2" --no-launch --dir "$C/second install"
                ok_run "$C/out2" 0.0.1
                for f in .profile .bashrc; do
                    expect_eq "$f lines" "$(marked_lines "$H/$f")" 1
                    expect_text "$H/$f" "$C/second install/bin"
                    expect_no_text "$H/$f" "$DIR/bin"
                done
                check_new_shell "$C/second install" 0.0.1 /bin/bash
                ;;
            flatpak_first)
                # the Flatpak's line is kept, this install's added, and the player is told which themepark comes first
                expect_text "$C/out" 'A new terminal window does not find themepark yet'
                expect_text "$C/out" 'It finds the themepark command of the Flatpak install first'
                expect_eq ".profile lines" "$(marked_lines "$H/.profile")" 1
                expect_text "$H/.profile" "the Flatpak's themepark command"
                ;;
        esac
        # run again: the line is never doubled
        run_line "$C/outb" --no-launch
        case "$variant" in
            foreign) ;;
            *) ok_run "$C/outb" 0.0.1 ;;
        esac
        case "$variant" in
            foreign | zdotdir | flatpak_first) ;;
            *) expect_eq ".profile lines after a second run" "$(marked_lines "$H/.profile")" 1 ;;
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
    cp "$H/.profile" "$C/profile-before"
    cp "$H/.bashrc" "$C/bashrc-before"
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
    # the swap waits for the launcher (a process of 4 seconds; on Linux the game plays in the launcher's own process)
    sleep 4 &
    waitpid=$!
    start=$(date +%s)
    run_file "$C/u4" "$DIR/source/install.sh" --swap-and-launch --wait-pid "$waitpid" --dir "$DIR"
    end=$(date +%s)
    ok_run "$C/u4" 0.0.3
    [ $((end - start)) -ge 3 ] || bad "the swap did not wait for the launcher to exit ($((end - start)) seconds)"
    expect_in "$C/u4" 'STEP 3/4 Adding STP/TPW Launcher to the applications menu'
    check_build "$DIR" 0.0.3 stable
    expect_in "$DIR/build-prev/install-info.txt" "version=0.0.1"
    [ ! -e "$DIR/build-next" ] || bad "the swap left a build-next"
    wait_for_count "$DIR" "STP/TPW Launcher v0.0.3 args= cwd=$DIR" 0 || bad "the swap did not start the new launcher"
    check_entry "$DIR" 0.0.3
    check_new_shell "$DIR" 0.0.3
    # nothing waiting: the swap only starts the launcher in use
    run_file "$C/u5" "$DIR/source/install.sh" --swap-and-launch --no-launch --dir "$DIR"
    ok_run "$C/u5" 0.0.3
    # an update never touches the PATH lines or the data
    cmp -s "$C/profile-before" "$H/.profile" || bad "the updater changed ~/.profile"
    cmp -s "$C/bashrc-before" "$H/.bashrc" || bad "the updater changed ~/.bashrc"
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
    # its options go to the installer: themepark update --dev moves the install to main
    run_themepark "$C/u10" update --dev --no-launch
    ok_run "$C/u10" 0.0.6-dev
    check_build "$DIR" 0.0.6-dev dev
}

# Folder names with spaces, non-ASCII letters and shell characters, for the home folder, the install folder and the
# repository. On Linux the build cannot hold ' ( ) & : ; # * ? [ ] or a comma in its folder (CMake and SDL's Wayland
# step), so install.sh refuses those before anything is made; the repository's name may have them.
case_paths() {
    local odd="Jo Linux é ü 日本"
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
    expect_in "$C/o1" "They are in ~/.local/share/STP-TPW Launcher, and this install uses them as they are."
    check_build "$DIR" 0.0.1 stable
    check_entry "$DIR" 0.0.1
    expect_eq ".profile lines" "$(marked_lines "$H/.profile")" 1
    check_new_shell "$DIR" 0.0.1
    snap "$H/Downloads/tpw-port" >"$C/clone-after.txt"
    cmp -s "$C/clone-before.txt" "$C/clone-after.txt" || bad "the old clone was changed"
    # the install folders
    new_case paths_dir
    fake_init
    fake_release 0.0.1
    local n=0 d
    for d in "$C/tpw port é" "$C/日本語 ü" "$C/percent %HOME% x" "$C/  two  spaces" "$C/a+b=c@d~e!f{g}"; do
        n=$((n + 1))
        run_line "$C/o2-$n" --no-launch --dir "$d"
        ok_run "$C/o2-$n" 0.0.1
        check_build "$d" 0.0.1 stable
        check_entry "$d" 0.0.1
        check_new_shell "$d" 0.0.1
    done
    # a character the build cannot hold in a folder name: refused before anything is made, in words
    for d in "$C/dollar \$HOME" "$C/quote \"q\"" "$C/back\`tick" "$C/back\\slash" "$C/tpw port's é" "$C/a (b) & c" \
        "$C/semi;colon" "$C/hash#tag" "$C/colon:x" "$C/star*" "$C/what?" "$C/br[a]ckets" "$C/com,ma"; do
        n=$((n + 1))
        run_line "$C/o2-$n" --no-launch --dir "$d"
        fail_run "$C/o2-$n" 'breaks the build'
        [ ! -e "$d" ] || bad "$d was made though it was refused"
    done
    # Linux's system folders are refused too
    for d in /root /boot /proc /run /srv /mnt /media; do
        run_line "$C/o2-sys" --no-launch --dir "$d"
        fail_run "$C/o2-sys" 'Not using'
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
    # the default folder, with a ~/.local/share that is a link
    new_case paths_link
    fake_init
    fake_release 0.0.1
    mkdir -p "$C/real-share" "$H/.local"
    ln -s "$C/real-share" "$H/.local/share"
    run_line "$C/o5" --no-launch
    ok_run "$C/o5" 0.0.1
    [ -x "$C/real-share/STP-TPW-Launcher/build/tpw_launcher" ] || bad "a ~/.local/share link was not followed"
    check_entry "$DIR" 0.0.1
    # $XDG_DATA_HOME: the install folder and the menu entry go there, beside the launcher's data folder
    new_case paths_xdg
    fake_init
    fake_release 0.0.1
    XDG_DATA_HOME="$C/xdg data"
    export XDG_DATA_HOME
    run_line "$C/o6" --no-launch
    ok_run "$C/o6" 0.0.1
    check_build "$XDG_DATA_HOME/STP-TPW-Launcher" 0.0.1 stable
    APPS="$XDG_DATA_HOME/applications"
    check_entry "$XDG_DATA_HOME/STP-TPW-Launcher" 0.0.1
    [ ! -e "$H/.local" ] || bad "an install with XDG_DATA_HOME set wrote in ~/.local"
    unset XDG_DATA_HOME
}

# The build's packages (apt's tools, sudo and id stood in for: fake_packages): installed with one sudo, or as root
# without; when nothing can ask for the password, the exact command and nothing made; tools missing; no package manager.
case_packages() {
    local all farm missing2
    all=$(apt_packages)
    missing2="libdecor-0-dev libvulkan1"

    new_case packages-none
    note "everything installed: nothing asked"
    unset TPW_INSTALL_PACKAGES
    fake_init
    fake_release 0.0.1
    tool_farm "$C/farm"
    fake_packages "$C/farm"
    printf '%s\n' $all >"$C/pkg-state"
    PATH="$C/farm" run_line "$C/o1" --no-launch
    ok_run "$C/o1" 0.0.1
    expect_text "$C/o1" "The build's packages are installed (apt-get)."
    expect_no_text "$C/pkg-log" 'apt-get install'
    expect_no_text "$C/pkg-log" 'sudo'

    new_case packages-root
    note "as root: apt-get, with no sudo"
    unset TPW_INSTALL_PACKAGES
    fake_init
    fake_release 0.0.1
    tool_farm "$C/farm"
    FAKE_SUDO=none fake_packages "$C/farm"
    FAKE_UID=0 PATH="$C/farm" run_line "$C/o1" --no-launch
    ok_run "$C/o1" 0.0.1
    expect_in "$C/pkg-log" "apt-get install -y --no-install-recommends $all"
    expect_eq "apt-get installs" "$(grep -c '^apt-get install' "$C/pkg-log")" 1
    expect_text "$C/o1" "Installed: $all"

    new_case packages-nopasswd
    note "a sudo that asks no password: one install, of the missing ones only"
    unset TPW_INSTALL_PACKAGES
    fake_init
    fake_release 0.0.1
    tool_farm "$C/farm"
    fake_packages "$C/farm"
    printf '%s\n' $all | grep -vxF -e libdecor-0-dev -e libvulkan1 >"$C/pkg-state"
    FAKE_SUDO=nopasswd PATH="$C/farm" run_line "$C/o1" --no-launch
    ok_run "$C/o1" 0.0.1
    expect_in "$C/pkg-log" "sudo -n env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends $missing2"
    expect_eq "apt-get installs" "$(grep -c '^apt-get install' "$C/pkg-log")" 1
    check_build "$DIR" 0.0.1 stable

    new_case packages-password
    note "a sudo that asks a password, and no terminal to ask on: the command, and nothing made"
    unset TPW_INSTALL_PACKAGES
    fake_init
    fake_release 0.0.1
    tool_farm "$C/farm"
    fake_packages "$C/farm"
    printf '%s\n' $all | grep -vxF -e libdecor-0-dev -e libvulkan1 >"$C/pkg-state"
    ls -A "$H" >"$C/home-before.txt"
    FAKE_SUDO=password PATH="$C/farm" run_line "$C/o1" --no-launch
    fail_run "$C/o1" "Missing packages: $missing2\\. Install them \\(sudo apt-get update && sudo apt-get install --no-install-recommends $missing2\\)"
    expect_in "$C/o1" "  sudo apt-get update && sudo apt-get install --no-install-recommends $missing2"
    expect_text "$C/o1" 'Nothing was changed.'
    expect_no_text "$C/pkg-log" 'apt-get install'
    ls -A "$H" >"$C/home-after.txt"
    cmp -s "$C/home-before.txt" "$C/home-after.txt" || bad "a run that could not install the packages made something"
    [ ! -e "$DIR" ] || bad "a run that could not install the packages made $DIR"

    note "the same with a terminal: sudo asks once, then installs"
    if command -v script >/dev/null 2>&1; then
        FAKE_SUDO=password PATH="$C/farm" run_tty "$C/o2" "$INSTALL_SH" --no-launch
        [ "$RC" = 0 ] || bad "the install in a terminal with sudo's password exited with $RC"
        expect_eq "sudo -v (the one question)" "$(grep -c '^sudo -v' "$C/pkg-log")" 1
        expect_in "$C/pkg-log" "sudo -n env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends $missing2"
        expect_text "$C/o2" 'sudo asks for your password'
        check_build "$DIR" 0.0.1 stable
    else
        echo "::warning::[$CASE] script(1) is not installed here; the terminal's sudo question was not tried"
    fi

    note "the launcher's Update button with a package missing: the command, and the launcher in use as it was"
    if [ -x "$DIR/build/tpw_launcher" ]; then
        rm -f "$C/sudo-ok"   # the password given in the terminal above is not there for the Update button
        fake_release 0.0.2
        grep -vxF libvulkan1 "$C/pkg-state" >"$C/pkg-state.new" && mv "$C/pkg-state.new" "$C/pkg-state"
        FAKE_SUDO=password PATH="$C/farm" run_file "$C/o3" "$DIR/source/install.sh" --update --stage --dir "$DIR"
        fail_run "$C/o3" 'Missing packages: libvulkan1'
        check_build "$DIR" 0.0.1 stable
        [ ! -e "$DIR/build-next" ] || bad "a stage that could not install the packages left a build-next"
    fi

    new_case packages-nosudo
    note "no sudo, not root: the command to run as root"
    unset TPW_INSTALL_PACKAGES
    fake_init
    fake_release 0.0.1
    tool_farm "$C/farm"
    FAKE_SUDO=none fake_packages "$C/farm"
    printf '%s\n' $all | grep -vxF libvulkan1 >"$C/pkg-state"
    PATH="$C/farm" run_line "$C/o1" --no-launch
    fail_run "$C/o1" 'Missing packages: libvulkan1'
    expect_text "$C/o1" 'There is no sudo here to install them with. As root, run:'
    expect_in "$C/o1" "  apt-get update && apt-get install --no-install-recommends libvulkan1"

    new_case packages-aptfail
    note "apt-get fails, or leaves one out"
    unset TPW_INSTALL_PACKAGES
    fake_init
    fake_release 0.0.1
    tool_farm "$C/farm"
    fake_packages "$C/farm"
    printf '%s\n' $all | grep -vxF libvulkan1 >"$C/pkg-state"
    FAKE_APT_FAIL=1 PATH="$C/farm" run_line "$C/o1" --no-launch
    fail_run "$C/o1" "Installing the build's packages with apt-get failed"
    [ ! -e "$DIR" ] || bad "a failed package install made $DIR"
    FAKE_APT_DROP=libvulkan1 PATH="$C/farm" run_line "$C/o2" --no-launch
    fail_run "$C/o2" 'The packages libvulkan1 are still missing after apt-get ran'
    PATH="$C/farm" run_line "$C/o3" --no-launch
    ok_run "$C/o3" 0.0.1

    new_case packages-unknown
    note "no apt-get, dnf or pacman: the list, then TPW_INSTALL_PACKAGES=skip goes on"
    unset TPW_INSTALL_PACKAGES
    fake_init
    fake_release 0.0.1
    tool_farm "$C/farm"
    PATH="$C/farm" run_line "$C/o1" --no-launch
    fail_run "$C/o1" 'This Linux has no apt-get, dnf or pacman'
    expect_text "$C/o1" 'libdecor-0-dev'
    expect_text "$C/o1" 'TPW_INSTALL_PACKAGES=skip'
    TPW_INSTALL_PACKAGES=skip PATH="$C/farm" run_line "$C/o2" --no-launch
    ok_run "$C/o2" 0.0.1

    note "tools the script itself runs, missing (the package step left out)"
    for what in git "make python3"; do
        new_case "packages-tools-${what// /-}"
        fake_init
        fake_release 0.0.1
        # shellcheck disable=SC2086  # one name per word
        tool_farm "$C/farm" $what
        ls -A "$H" >"$C/home-before.txt"
        PATH="$C/farm" run_line "$C/out" --no-launch
        fail_run "$C/out" '^RESULT fail Missing: '
        for t in $what; do expect_text "$C/out" "$t"; done
        expect_text "$C/out" 'Nothing was changed.'
        ls -A "$H" >"$C/home-after.txt"
        cmp -s "$C/home-before.txt" "$C/home-after.txt" || bad "a run with tools missing made something in the home folder"
    done
}

# The package step for real: the system's package manager installs what is missing (this must run as root, or with a
# sudo that asks no password), then the same line finds everything installed.
case_packages_real() {
    new_case packages_real
    unset TPW_INSTALL_PACKAGES
    fake_init
    fake_release 0.0.1
    run_line "$C/o1" --no-launch
    ok_run "$C/o1" 0.0.1
    if grep -q '^The build needs these packages, which are not installed' "$C/o1"; then
        expect_text "$C/o1" 'Installed: '
    fi
    run_line "$C/o2" --no-launch
    ok_run "$C/o2" 0.0.1
    expect_text "$C/o2" "The build's packages are installed"
    check_build "$DIR" 0.0.1 stable
}

# The Flatpak install (--flatpak), with a stand-in for flatpak (TPW_INSTALL_FLATPAK) and a bundle file of nothing
# (TPW_INSTALL_BUNDLE): its own themepark command in ~/.local/bin and its own PATH line, the source install's options
# refused, an update that writes no PATH line, and an install from source beside it.
case_flatpak() {
    new_case flatpak
    cat >"$C/flatpak" <<EOF
#!/bin/sh
echo "flatpak \$*" >>"$C/flatpak-log"
case "\$1" in
    --default-arch) echo x86_64 ;;
    remote-add) ;;
    info) [ -e "$C/flatpak-installed" ] ;;
    install) echo 0.0.7 >"$C/flatpak-installed"; echo "Installing (a stand-in)" ;;
    run) shift 2; [ "\${1:-}" = --version ] && printf 'STP/TPW Launcher v%s\nbuild: flatpak\n' "\$(cat "$C/flatpak-installed")" ;;
esac
exit 0
EOF
    chmod 755 "$C/flatpak"
    : >"$C/bundle.flatpak"
    export TPW_INSTALL_FLATPAK="$C/flatpak" TPW_INSTALL_BUNDLE="$C/bundle.flatpak"
    run_line "$C/o1" --flatpak --no-launch
    ok_run "$C/o1" 0.0.7
    expect_in "$C/flatpak-log" "flatpak install --user -y --noninteractive --reinstall --bundle $C/bundle.flatpak"
    expect_in "$H/.local/bin/themepark" "# tpw-port installer: the Flatpak's themepark command"
    expect_text "$H/.local/bin/themepark" 'sh -s -- --flatpak --update'
    for f in .profile .bashrc; do
        expect_eq "the Flatpak's lines in $f" "$(grep -cF "the Flatpak's themepark command" "$H/$f")" 1
    done
    # themepark in a new window: the Flatpak's command, which runs the launcher through flatpak (the stand-in, on the PATH)
    out=$(cd / && env -i HOME="$H" TERM=dumb PATH="$C:/usr/bin:/bin" bash -l -c 'command -v themepark; themepark --version' 2>/dev/null </dev/null)
    echo "$out"
    expect_eq "a new login shell's themepark" "${out%%$'\n'*}" "$H/.local/bin/themepark"
    case "$out" in *"STP/TPW Launcher v0.0.7"*) ;; *) bad "themepark --version did not run the Flatpak's launcher" ;; esac
    # the source install's options are refused, --dev too
    run_line "$C/o2" --flatpak --no-launch --dev
    fail_run "$C/o2" 'The Flatpak follows the releases only'
    run_line "$C/o2" --flatpak --no-launch --dir "$C/x"
    fail_run "$C/o2" '--dir is an option of the install built from source'
    for o in --stage --swap-and-launch --wait-pid; do
        run_line "$C/o2" --flatpak --no-launch "$o" 1
        fail_run "$C/o2" "$o is an option of the install built from source"
    done
    # the update (themepark update runs it so) writes no PATH line
    cp "$H/.profile" "$C/profile-kept"
    printf '# mine\n' >"$H/.profile"
    run_line "$C/o3" --flatpak --update --no-launch
    ok_run "$C/o3" 0.0.7
    expect_eq "the profile after an update" "$(cat "$H/.profile")" "# mine"
    cp "$C/profile-kept" "$H/.profile"
    # an install from source beside it keeps the Flatpak's lines and adds its own
    unset TPW_INSTALL_FLATPAK TPW_INSTALL_BUNDLE
    fake_init
    fake_release 0.0.1
    run_line "$C/o4" --no-launch
    ok_run "$C/o4" 0.0.1
    expect_eq "the source install's lines in ~/.bashrc" "$(marked_lines "$H/.bashrc")" 1
    expect_eq "the Flatpak's lines in ~/.bashrc" "$(grep -cF "the Flatpak's themepark command" "$H/.bashrc")" 1
    expect_text "$C/o4" 'It finds the themepark command of the Flatpak install first'
    # --help names it, and the source install's folder
    run_line "$C/o5" --help
    [ "$RC" = 0 ] || bad "--help exited with $RC"
    expect_text "$C/o5" '--flatpak'
    # shellcheck disable=SC2088  # the ~ is the text
    expect_text "$C/o5" '~/.local/share/STP-TPW-Launcher'
}

# The real build, from a local tag in the CI checkout: TPW_CI_SOURCE is the checkout (a git working tree of tpw-port).
# Takes a copy of it, says its launcher/VERSION.txt is a release (TPW_CI_VERSION, default 0.0.99) and tags that. The run
# is the one a player makes, with the package step for real (this runs as root, or with a sudo that asks no password),
# in a home folder with a space and a non-ASCII letter (not an apostrophe, which a Linux account's name cannot have and
# the Linux build cannot hold). With TPW_CI_UPDATE=1, then a newer release (the
# version's last number plus one) and the launcher's Update button's run: --update --stage, then --swap-and-launch.
case_real() {
    [ -n "${TPW_CI_SOURCE:-}" ] || bad "TPW_CI_SOURCE (the checkout to build) is not set"
    local version=${TPW_CI_VERSION:-0.0.99} newer
    QUIET=1
    new_case real "$WORK/real/Jo Linux é/home"
    unset TPW_INSTALL_PACKAGES
    # the checkout may belong to another user than this one (a container's root on a CI runner's checkout): git takes
    # safe.directory from a global configuration only (Ubuntu's 2.43 not from the environment's), one of the case's own
    printf '[safe]\n\tdirectory = *\n' >"$C/gitconfig"
    export GIT_CONFIG_GLOBAL="$C/gitconfig"
    seed_data
    mkdir -p "$H/Documents/tpw-port/.git" "$H/Documents/tpw-port/launcher" "$H/Documents/tpw-port/build"
    printf '0.0.2\n' >"$H/Documents/tpw-port/launcher/VERSION.txt"
    printf 'an old launcher\n' >"$H/Documents/tpw-port/build/tpw_launcher"
    snap "$H/Documents/tpw-port" >"$C/clone-before.txt"
    snap "$DATA" >"$C/data-before.txt"
    git clone -q "$TPW_CI_SOURCE" "$C/release-repo" || bad "cannot copy the checkout"
    git -C "$C/release-repo" checkout -q -B main
    # a checkout with the project's tags (a clone of one, not CI's) would offer its newest release instead of this one
    git -C "$C/release-repo" tag -l | while read -r t; do git -C "$C/release-repo" tag -d "$t" >/dev/null; done
    real_release "$version"
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
    run_line "$C/o1" --no-launch
    ok_run "$C/o1" "$version"
    grep -E "^(Linux: |The build's packages|The build needs|Installed: )" "$C/o1"
    check_build "$DIR" "$version" stable
    check_entry "$DIR" "$version" 1
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
    expect_eq ".profile lines" "$(marked_lines "$H/.profile")" 1
    [ "${TPW_CI_UPDATE:-0}" = 1 ] || return 0

    newer=$(printf '%s' "$version" | awk -F. '{ printf "%d.%d.%d", $1, $2, $3 + 1 }')
    note "the launcher's Update button: $version to $newer"
    real_release "$newer"
    cd "$DIR" || exit 1
    run_file "$C/u1" "$DIR/source/install.sh" --update --stage --dir "$DIR"
    ok_run "$C/u1" "$newer"
    check_build "$DIR" "$version" stable
    expect_in "$DIR/build-next/install-info.txt" "version=$newer"
    sleep 3 &
    run_file "$C/u2" "$DIR/source/install.sh" --swap-and-launch --wait-pid "$!" --dir "$DIR" --no-launch
    ok_run "$C/u2" "$newer"
    check_build "$DIR" "$newer" stable
    expect_in "$DIR/build-prev/install-info.txt" "version=$version"
    check_entry "$DIR" "$newer" 1
    check_new_shell_real "$DIR"
    snap "$DATA" >"$C/data-after.txt"
    cmp -s "$C/data-before.txt" "$C/data-after.txt" || bad "the data folder was changed by the update"
}

# A release of the copy of the checkout: launcher/VERSION.txt says VERSION, and the tag vVERSION is on that commit.
real_release() { # VERSION
    printf '%s\n' "$1" >"$C/release-repo/launcher/VERSION.txt"
    git -C "$C/release-repo" add launcher/VERSION.txt
    git -C "$C/release-repo" -c user.name=ci -c user.email=ci@example.invalid -c commit.gpgsign=false commit -q -m "Release $1"
    git -C "$C/release-repo" tag "v$1"
}

# The real launcher answers --version through themepark in a new login shell: themepark runs it in the foreground for
# that, so its version and build lines show. This checks the command, the exit code, those lines and that the launcher
# process ends by itself.
check_new_shell_real() { # DIR
    local out
    out=$(cd / && env -i HOME="$HOME" USER="${USER:-}" TERM=dumb PATH=/usr/bin:/bin:/usr/sbin:/sbin "${SHELL:-/bin/bash}" -l -c \
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
# that holds an old clone and a data folder. The newest release's install.sh must be one that builds on Linux (0.1.1 or
# later): an older one says it is for macOS only.
case_public() {
    new_case public
    QUIET=1
    unset TPW_INSTALL_REPO TPW_INSTALL_PACKAGES
    seed_data
    mkdir -p "$H/Downloads/tpw-port/.git" "$H/Downloads/tpw-port/launcher"
    printf '0.0.2\n' >"$H/Downloads/tpw-port/launcher/VERSION.txt"
    snap "$H/Downloads/tpw-port" >"$C/clone-before.txt"
    snap "$DATA" >"$C/data-before.txt"
    # shellcheck disable=SC2086  # PUBLIC_ARGS is a list of options
    curl -fsSL https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.sh | sh -s -- --no-launch ${PUBLIC_ARGS:-} >"$C/o1" 2>&1
    RC=$?
    show_run "$C/o1"
    last=$(tail -n 1 "$C/o1")
    case "$last" in "RESULT ok "*) ;; *) bad "the one-liner did not end with RESULT ok: $last" ;; esac
    [ "$RC" = 0 ] || bad "the one-liner exited with $RC"
    version=${last#RESULT ok }
    check_build "$DIR" "$version" "$([ -n "${PUBLIC_ARGS:-}" ] && echo dev || echo stable)"
    check_entry "$DIR" "$version" 1
    expect_in "$C/o1" "An older copy of the launcher is in $H/Downloads/tpw-port."
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
    expect_eq ".profile lines" "$(marked_lines "$H/.profile")" 1
}

# ---- main

main() {
    if [ $# -eq 0 ]; then
        set -- fresh shells legacy rerun updater dev tags refusals paths packages buildfail interrupt interrupt_first flatpak
    fi
    run_cases "$@"
}

main "$@"
