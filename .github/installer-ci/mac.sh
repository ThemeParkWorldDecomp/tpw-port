#!/bin/bash
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

# ---- checks

bad() {
    echo "::error::[$CASE] $*"
    exit 1
}

note() {
    echo "-- [$CASE] $*"
}

expect_eq() { # WHAT GOT WANT
    [ "$2" = "$3" ] || bad "$1: got \"$2\", wanted \"$3\""
}

expect_in() { # FILE TEXT (a fixed line, anywhere)
    grep -qxF -- "$2" "$1" 2>/dev/null || bad "$1 has no line \"$2\""
}

expect_text() { # FILE TEXT (a fixed string, anywhere in a line)
    grep -qF -- "$2" "$1" 2>/dev/null || bad "$1 has no \"$2\""
}

expect_no_text() { # FILE TEXT
    if grep -qF -- "$2" "$1" 2>/dev/null; then bad "$1 has \"$2\""; fi
}

# STEP n/N lines count up from 1, N stays the same, and the last one is N (the launcher's updater reads them).
check_steps() {
    awk '/^STEP [0-9]+\/[0-9]+ / { split($2, a, "/"); n++; if (a[1] != n) wrong = 1; if (total && a[2] != total) wrong = 1; total = a[2]; last = a[1] }
         END { exit (n == 0 || wrong || last != total) }' "$1" || bad "the STEP lines of $1 are not 1/N to N/N"
}

# After run_line: it must have exited 0 with exactly one RESULT line, the last, "RESULT ok V".
ok_run() { # OUT VERSION
    [ "$RC" = 0 ] || bad "the install exited with $RC, not 0"
    expect_eq "the last line" "$(tail -n 1 "$1")" "RESULT ok $2"
    expect_eq "the number of RESULT lines" "$(grep -c '^RESULT ' "$1")" 1
    check_steps "$1"
}

# After run_line: a failure, exit not 0, exactly one RESULT line, the last, "RESULT fail" and the text PATTERN (ERE).
fail_run() { # OUT PATTERN
    [ "$RC" != 0 ] || bad "the run exited with 0, but had to fail"
    last=$(tail -n 1 "$1")
    case "$last" in "RESULT fail "*) ;; *) bad "the last line is \"$last\", not a RESULT fail line" ;; esac
    printf '%s\n' "$last" | grep -Eq -- "$2" || bad "RESULT fail \"$last\" does not match $2"
    expect_eq "the number of RESULT lines" "$(grep -c '^RESULT ' "$1")" 1
}

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
    export TPW_INSTALL_REPO
    unset TPW_INSTALL_STUB TPW_INSTALL_STUB_VERSION TPW_FAKE_BUILD ZDOTDIR
    TPW_FAKE_SLEEP=604
    export TPW_FAKE_SLEEP
    DIR=$H/Applications/STP-TPW-Launcher
    DATA="$H/Library/Application Support/STP-TPW Launcher"
    cd "$H" || exit 1
    echo "== case $CASE: home $H"
}

# The line a player runs, from the current folder: the script on standard input, as `curl ... | sh` gives it.
run_line() { # OUT ARGS...
    local out=$1
    shift
    cat "$INSTALL_SH" | sh -s -- "$@" >"$out" 2>&1
    RC=$?
    show_run "$out"
}

# The output of a run, all of it, or (QUIET set: a real build prints tens of thousands of lines) its STEP and RESULT lines
# and the last ones, unless it failed.
show_run() { # OUT
    if [ -n "${QUIET:-}" ] && [ "$RC" = 0 ]; then
        grep -E '^(STEP|RESULT) ' "$1"
        echo "... the build's output is in $1; the last lines:"
        tail -n 25 "$1"
    elif [ -n "${QUIET:-}" ]; then
        tail -n 400 "$1"
    else
        cat "$1"
    fi
    echo "[exit $RC]"
}

# The script as a file, as the launcher's updater runs it: standard input closed.
run_file() { # OUT SCRIPT ARGS...
    local out=$1 script=$2
    shift 2
    sh "$script" "$@" >"$out" 2>&1 </dev/null
    RC=$?
    show_run "$out"
}

# ---- the fake release

git_f() {
    git -C "$FAKE" -c user.name=ci -c user.email=ci@example.invalid -c commit.gpgsign=false -c tag.gpgsign=false "$@"
}

fake_set_version() { # VERSION MESSAGE
    printf '%s\n' "$1" >"$FAKE/launcher/VERSION.txt"
    git_f add launcher/VERSION.txt
    git_f commit -q -m "$2"
}

# A release: a commit that says VERSION and the tag vVERSION on it, then main goes on as the next -dev. Tags are
# annotated unless the second argument is lightweight.
fake_release() { # VERSION [lightweight]
    fake_set_version "$1" "Release $1"
    if [ "${2:-}" = lightweight ]; then git_f tag "v$1"; else git_f tag -a "v$1" -m "Release $1"; fi
    local next
    next=$(printf '%s' "$1" | awk -F. '{ printf "%d.%d.%d-dev", $1, $2, $3 + 1 }')
    fake_set_version "$next" "Start $next"
}

# A change on main that is not a release.
fake_dev_change() {
    date >>"$FAKE/launcher/dev-notes.txt"
    git_f add launcher/dev-notes.txt
    git_f commit -q -m "A change on main"
}

fake_init() {
    git init -q -b main "$SUBREPO"
    printf 'stand-in for the translation\n' >"$SUBREPO/README"
    git -C "$SUBREPO" add README
    git -C "$SUBREPO" -c user.name=ci -c user.email=ci@example.invalid -c commit.gpgsign=false commit -q -m "tpw-decomp"
    git init -q -b main "$FAKE"
    mkdir -p "$FAKE/launcher"
    printf '%s\n' \
        'BUILD := build' \
        '.PHONY: all find-cmake launcher' \
        'all: launcher' \
        'find-cmake:' \
        $'\t@mkdir -p "$(BUILD)"' \
        $'\t@echo "==> a stand-in for the CMake step"' \
        'launcher: find-cmake' \
        $'\t@sh launcher/fake_build.sh "$(BUILD)"' >"$FAKE/Makefile"
    cat >"$FAKE/launcher/fake_build.sh" <<'EOF'
#!/bin/sh
# The stand-in build: TPW_FAKE_BUILD=slow hangs after half a build, =fail fails like a compiler, else it makes the launcher.
build=$1
v=$(cat launcher/VERSION.txt)
echo "==> building version $v (${TPW_FAKE_BUILD:-ok})"
case "${TPW_FAKE_BUILD:-ok}" in
    slow)
        mkdir -p "$build/half"
        echo "half a build" >"$build/half/file"
        sleep "${TPW_FAKE_SLEEP:-600}"
        ;;
    fail)
        echo "fake compiler error: expected ';'"
        exit 2
        ;;
    empty)
        exit 0
        ;;
    wrongversion)
        v=9.9.9
        ;;
esac
sed "s/@VERSION@/$v/" launcher/fake_launcher.in >"$build/tpw_launcher" && chmod 755 "$build/tpw_launcher"
EOF
    cat >"$FAKE/launcher/fake_launcher.in" <<'EOF'
#!/bin/sh
# A stand-in launcher of version @VERSION@: answers --version, and when run from a folder named build notes it.
if [ "$1" = --version ]; then printf 'STP/TPW Launcher v@VERSION@\nbuild: fake\n'; fi
here=$(cd "$(dirname "$0")" && pwd)
if [ "$(basename "$here")" = build ]; then
    printf 'STP/TPW Launcher v@VERSION@ args=%s cwd=%s\n' "$*" "$PWD" >>"$here/../stub-launch.txt"
fi
EOF
    # the installer is in the repository, as in tpw-port: the launcher's updater runs source/install.sh
    cp "$INSTALL_SH" "$FAKE/install.sh"
    git_f add -A
    git_f submodule add -q "$SUBREPO" tpw-decomp
    fake_set_version 0.0.1-dev "The first commit"
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

launch_count() { # FOLDER TEXT
    local n
    n=$(grep -cF -- "$2" "$1/stub-launch.txt" 2>/dev/null || true)
    echo "${n:-0}"
}

wait_for_count() { # FOLDER TEXT MORE_THAN
    for _ in $(seq 40); do
        [ "$(launch_count "$1" "$2")" -gt "$3" ] && return 0
        sleep 0.5
    done
    return 1
}

# A new Terminal window of the player's shell: a login shell with a bare environment. Types themepark --version from
# another folder, and the stand-in launcher must have been started with --version, as version VERSION.
check_new_shell() { # DIR VERSION [SHELL]
    local dir=$1 v=$2 shell=${3:-${SHELL:-/bin/zsh}} want seen out
    want="STP/TPW Launcher v$v args=--version"
    seen=$(launch_count "$dir" "$want")
    out=$(cd / && env -i HOME="$HOME" USER="${USER:-}" TERM=dumb PATH=/usr/bin:/bin:/usr/sbin:/sbin "$shell" -l -c \
        'command -v themepark; themepark --version; echo "exit=$?"' </dev/null 2>&1)
    echo "$out"
    case "$out" in "$dir/bin/themepark"*) ;; *) bad "a new $shell window finds themepark at \"${out%%$'\n'*}\", not $dir/bin/themepark" ;; esac
    case "$out" in *"exit=0") ;; *) bad "themepark --version in a new $shell window did not exit with 0" ;; esac
    wait_for_count "$dir" "$want" "$seen" || bad "themepark --version in a new $shell window did not start version $v"
}

# The folder DIR holds this version's launcher in build/, and says so.
check_build() { # DIR VERSION CHANNEL
    local first
    first=$("$1/build/tpw_launcher" --version </dev/null 2>&1 | sed -n 1p)
    expect_eq "the launcher in $1/build" "$first" "STP/TPW Launcher v$2"
    expect_in "$1/build/install-info.txt" "version=$2"
    expect_in "$1/build/install-info.txt" "channel=$3"
    expect_eq "channel.txt" "$(cat "$1/channel.txt")" "$3"
}

marked_lines() { # FILE
    local n
    n=$(grep -cF '# tpw-port installer: the themepark command' "$1" 2>/dev/null || true)
    echo "${n:-0}"
}

descendants() { # PID: the pids of everything it started that still runs
    ps -A -o pid= -o ppid= | awk -v top="$1" '
        { parent[$1] = $2 }
        END {
            do {
                n = 0
                for (p in parent) if (!(p in mine) && (parent[p] == top || parent[p] in mine)) { mine[p] = 1; n++ }
            } while (n)
            for (p in mine) print p
        }'
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

# An old README-style clone (tpw-port main, recursed submodules, built) with a data folder: the install leaves both
# alone and names the clone.
case_legacy() {
    local where
    for where in home Downloads Desktop Documents cwd; do
        new_case "legacy-$where"
        fake_init
        fake_release 0.0.1
        fake_release 0.0.2
        case "$where" in
            home) clone=$H/tpw-port ;;
            cwd) clone=$H/projects/tpw-port ;;
            *) clone=$H/$where/tpw-port ;;
        esac
        mkdir -p "$(dirname "$clone")"
        git clone -q --recurse-submodules "$FAKE" "$clone"
        make -s -C "$clone" >/dev/null || bad "the old clone did not build"
        [ -x "$clone/build/tpw_launcher" ] || bad "the old clone has no launcher"
        seed_data
        clone=$(cd "$clone" && pwd -P)
        snap "$clone" >"$C/clone-before.txt"
        snap "$DATA" >"$C/data-before.txt"
        if [ "$where" = cwd ]; then cd "$clone" || exit 1; fi
        run_line "$C/out" --no-launch
        ok_run "$C/out" 0.0.2
        expect_in "$C/out" "An older copy of the launcher is in $clone."
        expect_text "$C/out" 'Your games, saves and settings are not in it.'
        snap "$clone" >"$C/clone-after.txt"
        snap "$DATA" >"$C/data-after.txt"
        cmp -s "$C/clone-before.txt" "$C/clone-after.txt" || { diff "$C/clone-before.txt" "$C/clone-after.txt" | head -20; bad "the old clone was changed"; }
        cmp -s "$C/data-before.txt" "$C/data-after.txt" || { diff "$C/data-before.txt" "$C/data-after.txt" | head -20; bad "the data folder was changed"; }
        [ ! -e "$clone/stub-launch.txt" ] || bad "the old clone's launcher was started"
        check_build "$DIR" 0.0.2 stable
        check_new_shell "$DIR" 0.0.2
        # the same line again, and a newer release: the clone and the data stay as they are
        fake_release 0.0.3
        run_line "$C/out2" --no-launch
        ok_run "$C/out2" 0.0.3
        expect_in "$C/out2" "An older copy of the launcher is in $clone."
        snap "$clone" >"$C/clone-after.txt"
        snap "$DATA" >"$C/data-after.txt"
        cmp -s "$C/clone-before.txt" "$C/clone-after.txt" || bad "the old clone was changed by the update"
        cmp -s "$C/data-before.txt" "$C/data-after.txt" || bad "the data folder was changed by the update"
        if [ "$where" = home ]; then
            # the install never goes into the old clone, though it is the folder the README used
            run_line "$C/out3" --no-launch --dir "$clone"
            fail_run "$C/out3" 'is not empty and not an install'
            snap "$clone" >"$C/clone-after.txt"
            cmp -s "$C/clone-before.txt" "$C/clone-after.txt" || bad "the old clone was changed by a refused install"
        fi
        cd "$H" || exit 1
    done
    # several old copies at once, each named once
    new_case legacy-all
    fake_init
    fake_release 0.0.1
    for where in tpw-port Downloads/tpw-port Desktop/tpw-port Documents/tpw-port; do
        mkdir -p "$H/$(dirname "$where")"
        git clone -q --recurse-submodules "$FAKE" "$H/$where"
    done
    seed_data
    snap "$H" >"$C/home-before.txt"
    run_line "$C/out" --no-launch
    ok_run "$C/out" 0.0.1
    for where in tpw-port Downloads/tpw-port Desktop/tpw-port Documents/tpw-port; do
        expect_in "$C/out" "An older copy of the launcher is in $H/$where."
    done
    expect_text "$C/out" 'they are no longer needed'
    for where in tpw-port Downloads/tpw-port Desktop/tpw-port Documents/tpw-port; do
        snap "$H/$where" >"$C/c-after.txt"
        (cd "$H/$where" && git status --porcelain | grep -c . | grep -qx 0) || bad "$where has changes"
    done
    snap "$DATA" >"$C/data-after.txt"
    (cd "$DATA" && snap . >"$C/data-b.txt") && true
}

# Run again with nothing new (nothing changes), with a newer release (updates), tags that are not releases.
case_rerun() {
    new_case rerun
    fake_init
    fake_release 0.0.1 lightweight
    seed_data
    snap "$DATA" >"$C/data-before.txt"
    run_line "$C/out1" --no-launch
    ok_run "$C/out1" 0.0.1
    snap "$DIR/build" >"$C/build-before.txt"
    cp "$H/.zprofile" "$C/zprofile-before"
    run_line "$C/out2" --no-launch
    ok_run "$C/out2" 0.0.1
    expect_text "$C/out2" 'Already up to date: version 0.0.1.'
    snap "$DIR/build" >"$C/build-after.txt"
    cmp -s "$C/build-before.txt" "$C/build-after.txt" || bad "a run with nothing new changed the build"
    cmp -s "$C/zprofile-before" "$H/.zprofile" || bad "a run with nothing new changed ~/.zprofile"
    [ ! -e "$DIR/build-prev" ] || bad "a run with nothing new made a build-prev"
    [ ! -e "$DIR/build-next" ] || bad "a run with nothing new left a build-next"
    check_new_shell "$DIR" 0.0.1

    # a newer release (annotated this time): it updates, keeps the one before
    fake_release 0.0.2
    run_line "$C/out3" --no-launch
    ok_run "$C/out3" 0.0.2
    expect_text "$C/out3" 'Switched to version 0.0.2. The one before is kept in'
    check_build "$DIR" 0.0.2 stable
    expect_in "$DIR/build-prev/install-info.txt" "version=0.0.1"
    expect_eq ".zprofile lines" "$(marked_lines "$H/.zprofile")" 1
    check_new_shell "$DIR" 0.0.2
    # the launcher the update starts (no --no-launch): from DIR, as the new version
    fake_release 0.0.3
    run_line "$C/out4"
    ok_run "$C/out4" 0.0.3
    wait_for_count "$DIR" "STP/TPW Launcher v0.0.3 args= cwd=$DIR" 0 || bad "the update did not start the new launcher"
    snap "$DATA" >"$C/data-after.txt"
    cmp -s "$C/data-before.txt" "$C/data-after.txt" || bad "the data folder was changed"
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
    # the swap waits for the launcher (a process of 3 seconds) to exit
    sleep 3 &
    waitpid=$!
    start=$(date +%s)
    run_file "$C/u4" "$DIR/source/install.sh" --swap-and-launch --wait-pid "$waitpid" --dir "$DIR"
    end=$(date +%s)
    ok_run "$C/u4" 0.0.3
    [ $((end - start)) -ge 2 ] || bad "the swap did not wait for the launcher to exit ($((end - start)) seconds)"
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
}

# --dev follows main; the plain line and --update (which never passes --dev) move the install to the newest release.
case_dev() {
    new_case dev
    fake_init
    fake_release 0.0.1
    run_line "$C/o1" --dev --no-launch
    ok_run "$C/o1" 0.0.2-dev
    check_build "$DIR" 0.0.2-dev dev
    expect_in "$DIR/build/install-info.txt" "commit=$(git -C "$FAKE" rev-parse main)"
    expect_no_text "$C/o1" 'This install followed main'
    check_new_shell "$DIR" 0.0.2-dev
    # the plain line: the newest release, saying so
    run_line "$C/o2" --no-launch
    ok_run "$C/o2" 0.0.1
    expect_in "$C/o2" 'This install followed main (--dev); it now follows the newest release. Add --dev to stay on main.'
    check_build "$DIR" 0.0.1 stable
    expect_eq ".zprofile lines" "$(marked_lines "$H/.zprofile")" 1
    check_new_shell "$DIR" 0.0.1
    # --dev again: back on main, and no such message
    run_line "$C/o3" --dev --no-launch
    ok_run "$C/o3" 0.0.2-dev
    expect_no_text "$C/o3" 'This install followed main'
    check_build "$DIR" 0.0.2-dev dev
    # --dev with nothing new on main, then with a change on main
    run_line "$C/o4" --dev --no-launch
    ok_run "$C/o4" 0.0.2-dev
    expect_text "$C/o4" 'Already up to date: version 0.0.2-dev.'
    fake_dev_change
    run_line "$C/o5" --dev --no-launch
    ok_run "$C/o5" 0.0.2-dev
    expect_in "$DIR/build/install-info.txt" "commit=$(git -C "$FAKE" rev-parse main)"
    expect_no_text "$C/o5" 'Already up to date'
    # --update, as the Update button runs it, from a dev install
    cd "$DIR" || exit 1
    run_file "$C/o6" "$DIR/source/install.sh" --update --dir "$DIR" --no-launch
    ok_run "$C/o6" 0.0.1
    expect_in "$C/o6" 'This install followed main (--dev); it now follows the newest release. Add --dev to stay on main.'
    check_build "$DIR" 0.0.1 stable
    # --stable says the same as nothing
    run_line "$C/o7" --stable --no-launch
    ok_run "$C/o7" 0.0.1
    expect_text "$C/o7" 'Already up to date'
    # a dev install on a Mac with no release at all
    new_case dev_only
    fake_init
    run_line "$C/o8" --dev --no-launch
    ok_run "$C/o8" 0.0.1-dev
    run_line "$C/o9" --no-launch
    fail_run "$C/o9" 'No release has been published yet'
    check_build "$DIR" 0.0.1-dev dev
}

# Which tags are releases: vX.Y.Z by number (v0.0.10 after v0.0.9), nothing else, and a tag must name its version.
case_tags() {
    new_case tags
    fake_init
    fake_release 0.0.9
    git_f tag v1.0.0-rc1
    git_f tag release-9.9.9
    git_f tag v2
    git_f tag 9.9.9
    fake_release 0.0.10
    run_line "$C/o1" --no-launch
    ok_run "$C/o1" 0.0.10
    expect_text "$C/o1" 'Newest release: 0.0.10'
    # a tag whose version file says something else is not installed (and the older release is not either)
    git_f tag v0.0.11
    run_line "$C/o2" --no-launch
    fail_run "$C/o2" 'The release v0.0.11 says it is version 0.0.11-dev'
    check_build "$DIR" 0.0.10 stable
    # a tag on a tree whose version file is not a version
    new_case tags_bad
    fake_init
    fake_set_version "not a version" "Broken"
    git_f tag v0.0.1
    run_line "$C/o3" --no-launch
    fail_run "$C/o3" 'does not name its version'
    [ ! -e "$DIR/build" ] || bad "a build was made from a source with no version"
}

# Refusals: each says why in words, ends with one RESULT fail line, exits not 0 and leaves what is there alone.
case_refusals() {
    new_case refusals
    fake_init
    fake_release 0.0.1
    ls -A "$H" >"$C/home-before.txt"
    # GitHub (here the repository) cannot be reached
    TPW_INSTALL_REPO="$C/does-not-exist" run_line "$C/o1" --no-launch
    fail_run "$C/o1" "Couldn't reach GitHub"
    ls -A "$H" >"$C/home-after.txt"
    cmp -s "$C/home-before.txt" "$C/home-after.txt" || bad "a run that could not reach the repository made something"
    # a folder that is somebody else's
    mkdir -p "$C/foreign"
    printf 'mine\n' >"$C/foreign/file.txt"
    snap "$C/foreign" >"$C/foreign-before.txt"
    run_line "$C/o2" --no-launch --dir "$C/foreign"
    fail_run "$C/o2" 'is not empty and not an install'
    snap "$C/foreign" >"$C/foreign-after.txt"
    cmp -s "$C/foreign-before.txt" "$C/foreign-after.txt" || bad "a foreign folder was changed"
    # folders that must never be the install folder
    local d
    for d in "$H" / .. "$C/a/../b" /usr /tmp "$H/."; do
        run_line "$C/o3" --no-launch --dir "$d"
        fail_run "$C/o3" 'Not using'
    done
    run_line "$C/o3" --no-launch --dir "$(printf 'two\nlines')"
    fail_run "$C/o3" 'Not using'
    run_line "$C/o4" --no-launch --nonsense
    fail_run "$C/o4" 'Unknown option --nonsense'
    run_line "$C/o5" --update --no-launch
    fail_run "$C/o5" 'There is no install in'
    run_line "$C/o6" --stage
    fail_run "$C/o6" '--stage goes with --update'
    run_line "$C/o7" --dir
    fail_run "$C/o7" '--dir needs a folder'
    run_line "$C/o8" --swap-and-launch --no-launch
    fail_run "$C/o8" 'There is no install in'
    run_line "$C/o9" --wait-pid x --swap-and-launch --no-launch
    fail_run "$C/o9" 'needs a process number'
    ls -A "$H" >"$C/home-after.txt"
    cmp -s "$C/home-before.txt" "$C/home-after.txt" || bad "a refusal made something in the home folder"
    # an install that is a link is not deleted or switched
    run_line "$C/o10" --no-launch
    ok_run "$C/o10" 0.0.1
    fake_release 0.0.2
    mkdir -p "$C/elsewhere"
    ln -s "$C/elsewhere" "$DIR/build-next"
    run_line "$C/o11" --no-launch
    fail_run "$C/o11" 'is a link'
    check_build "$DIR" 0.0.1 stable
    rm -f "$DIR/build-next"
    run_line "$C/o12" --no-launch
    ok_run "$C/o12" 0.0.2
    # --help
    run_line "$C/o13" --help
    [ "$RC" = 0 ] || bad "--help exited with $RC"
    expect_text "$C/o13" '--dir PATH'
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

# A build that fails: no launcher, no command, no PATH line on a first install; the working one stays on an update.
case_buildfail() {
    new_case buildfail
    fake_init
    fake_release 0.0.1
    export TPW_FAKE_BUILD=fail
    run_line "$C/o1" --no-launch
    fail_run "$C/o1" 'The build failed'
    expect_text "$C/o1" 'fake compiler error'
    [ ! -e "$DIR/build" ] || bad "a failed first build left a build folder"
    [ ! -e "$DIR/bin" ] || bad "a failed first build left the themepark command"
    [ ! -e "$DIR/channel.txt" ] || bad "a failed first build left a channel.txt"
    [ ! -e "$H/.zprofile" ] || bad "a failed first build wrote a PATH line"
    # a build that ends well but made nothing, or a launcher of another version: refused before anything is switched
    export TPW_FAKE_BUILD=empty
    run_line "$C/o1b" --no-launch
    fail_run "$C/o1b" 'made no tpw_launcher'
    export TPW_FAKE_BUILD=wrongversion
    run_line "$C/o1c" --no-launch
    fail_run "$C/o1c" 'did not answer with version 0.0.1'
    [ ! -e "$DIR/build" ] || bad "a wrong build was switched in"
    [ ! -e "$H/.zprofile" ] || bad "a wrong build wrote a PATH line"
    unset TPW_FAKE_BUILD
    run_line "$C/o2" --no-launch
    ok_run "$C/o2" 0.0.1
    check_new_shell "$DIR" 0.0.1
    fake_release 0.0.2
    export TPW_FAKE_BUILD=fail
    run_line "$C/o3" --no-launch
    fail_run "$C/o3" 'The build failed'
    expect_text "$C/o3" 'The launcher in use'
    check_build "$DIR" 0.0.1 stable
    check_new_shell "$DIR" 0.0.1
    # the updater's stage fails the same way, with the launcher in use as it was
    cd "$DIR" || exit 1
    run_file "$C/o4" "$DIR/source/install.sh" --update --stage --dir "$DIR"
    fail_run "$C/o4" 'The build failed'
    check_build "$DIR" 0.0.1 stable
    unset TPW_FAKE_BUILD
    run_line "$C/o5" --no-launch
    ok_run "$C/o5" 0.0.2
    check_build "$DIR" 0.0.2 stable
}

# Stops in the middle of the build. TERM (the updater stopping the script) ends it with a RESULT line and no process left;
# KILL (a power cut) leaves a half build-next. Either way the launcher in use works, and the next run recovers.
start_slow_install() { # OUT ARGS...: install.sh in the background, its build hangs; sets PID once the build is under way
    local out=$1
    shift
    TPW_FAKE_BUILD=slow sh "$INSTALL_SH" "$@" >"$out" 2>&1 </dev/null &
    PID=$!
    for _ in $(seq 120); do
        [ -f "$DIR/build-next/half/file" ] && return 0
        sleep 0.5
    done
    cat "$out"
    kill -9 "$PID" 2>/dev/null
    bad "the build did not start"
}

no_slow_left() {
    for _ in $(seq 20); do
        ps -A -o command= | grep -q '^sleep 604$' || return 0
        sleep 0.5
    done
    ps -A -o pid=,command= | grep ' sleep 604$'
    bad "a process of the stopped build is still running"
}

case_interrupt() {
    new_case interrupt
    fake_init
    fake_release 0.0.1
    run_line "$C/o1" --no-launch
    ok_run "$C/o1" 0.0.1
    seed_data
    snap "$DATA" >"$C/data-before.txt"

    note "TERM during the build of 0.0.2"
    fake_release 0.0.2
    start_slow_install "$C/o2" --no-launch
    kill -TERM "$PID"
    wait "$PID"
    RC=$?
    cat "$C/o2"
    fail_run "$C/o2" 'Stopped before it finished'
    no_slow_left
    check_build "$DIR" 0.0.1 stable
    check_new_shell "$DIR" 0.0.1
    run_line "$C/o3" --no-launch
    ok_run "$C/o3" 0.0.2
    check_build "$DIR" 0.0.2 stable
    expect_eq ".zprofile lines" "$(marked_lines "$H/.zprofile")" 1

    note "KILL during the build of 0.0.3"
    fake_release 0.0.3
    start_slow_install "$C/o4" --no-launch
    kids=$(descendants "$PID")
    # shellcheck disable=SC2086  # one number per word
    kill -9 "$PID" $kids 2>/dev/null
    wait "$PID" 2>/dev/null
    no_slow_left
    [ -d "$DIR/build-next" ] || bad "no half build was left to recover from"
    check_build "$DIR" 0.0.2 stable
    check_new_shell "$DIR" 0.0.2
    run_line "$C/o5" --no-launch
    ok_run "$C/o5" 0.0.3
    check_build "$DIR" 0.0.3 stable
    [ ! -e "$DIR/build-next/half" ] || bad "the half build is still in build-next"
    check_new_shell "$DIR" 0.0.3

    note "TERM during the updater's stage"
    fake_release 0.0.4
    cd "$DIR" || exit 1
    TPW_FAKE_BUILD=slow sh "$DIR/source/install.sh" --update --stage --dir "$DIR" >"$C/o6" 2>&1 </dev/null &
    PID=$!
    for _ in $(seq 120); do
        [ -f "$DIR/build-next/half/file" ] && break
        sleep 0.5
    done
    kill -TERM "$PID"
    wait "$PID"
    RC=$?
    cat "$C/o6"
    fail_run "$C/o6" 'Stopped before it finished'
    no_slow_left
    check_build "$DIR" 0.0.3 stable
    # a swap with a half-made build-next must refuse it
    run_file "$C/o7" "$DIR/source/install.sh" --swap-and-launch --no-launch --dir "$DIR"
    check_build "$DIR" 0.0.3 stable
    snap "$DATA" >"$C/data-after.txt"
    cmp -s "$C/data-before.txt" "$C/data-after.txt" || bad "the data folder was changed"
}

# A first install stopped in the build: nothing in place, and the next run installs.
case_interrupt_first() {
    new_case interrupt_first
    fake_init
    fake_release 0.0.1
    start_slow_install "$C/o1" --no-launch
    kids=$(descendants "$PID")
    # shellcheck disable=SC2086  # one number per word
    kill -9 "$PID" $kids 2>/dev/null
    wait "$PID" 2>/dev/null
    no_slow_left
    [ ! -e "$DIR/build" ] || bad "a build folder of a stopped first install"
    [ ! -e "$DIR/bin" ] || bad "a themepark command of a stopped first install"
    [ ! -e "$DIR/channel.txt" ] || bad "a channel.txt of a stopped first install"
    [ ! -e "$H/.zprofile" ] || bad "a PATH line of a stopped first install"
    run_line "$C/o2" --no-launch
    ok_run "$C/o2" 0.0.1
    check_build "$DIR" 0.0.1 stable
    expect_eq ".zprofile lines" "$(marked_lines "$H/.zprofile")" 1
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

# The real launcher answers --version through themepark in a new window. themepark detaches it and hides its output, so
# this checks the command, the exit code and that the launcher process ends by itself.
check_new_shell_real() { # DIR
    local out
    out=$(cd / && env -i HOME="$HOME" USER="${USER:-}" TERM=dumb PATH=/usr/bin:/bin:/usr/sbin:/sbin "${SHELL:-/bin/zsh}" -l -c \
        'command -v themepark; themepark --version; echo "exit=$?"' </dev/null 2>&1)
    echo "$out"
    case "$out" in "$1/bin/themepark"*) ;; *) bad "a new window finds themepark at \"${out%%$'\n'*}\"" ;; esac
    case "$out" in *"exit=0") ;; *) bad "themepark --version in a new window did not exit with 0" ;; esac
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
    local cases failed="" c t0
    cases=("$@")
    if [ ${#cases[@]} -eq 0 ]; then
        cases=(fresh shells legacy rerun updater dev tags refusals paths missing buildfail interrupt interrupt_first)
    fi
    for c in "${cases[@]}"; do
        echo "::group::case $c"
        t0=$(date +%s)
        # a subshell: a failed check exits the case only, and its environment is gone
        if ! ( "case_$c" ); then
            failed="$failed $c"
            echo "::endgroup::"
            echo "::error::case $c failed ($(($(date +%s) - t0)) seconds)"
        else
            echo "::endgroup::"
            echo "case $c passed ($(($(date +%s) - t0)) seconds)"
        fi
    done
    if [ -n "$failed" ]; then
        echo "::error::failed cases:$failed"
        exit 1
    fi
    echo "all cases passed: ${cases[*]}"
}

main "$@"
