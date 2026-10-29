#!/bin/bash
# What the broad installer cases of a Mac (mac.sh) and of Linux (linux.sh) share: the checks, the ways to run
# install.sh, the fake release, and the cases that run the same on both. Each driver sources this file after setting
# INSTALL_SH, WORK and CASE, and defines the parts of its own system: new_case (the home folder, the player's shell,
# DIR, DATA and PROFILE, the file whose PATH line the cases count: ~/.zprofile on a Mac, ~/.profile on Linux), run_tty,
# snap, seed_data, check_new_shell and run_themepark, then its own cases; a Mac's also defines app_step (below). Bash
# 3.2 (the Mac's own) is enough.

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

# ---- running install.sh

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
if [ "$1" = --write-icon ]; then exit 1; fi
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

# ---- the cases both systems run

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

# The rerun case's steps for the Mac's application in /Applications (mac.sh defines its own; Linux has none): "old" makes
# it the one 0.1.0 wrote, "updated VERSION" checks it after the update, "foreign" makes it another program's of that
# name and "foreign_kept OUT" checks that the update left that one alone and said so in OUT.
app_step() { :; }

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
    cp "$H/$PROFILE" "$C/profile-before"
    run_line "$C/out2" --no-launch
    ok_run "$C/out2" 0.0.1
    expect_text "$C/out2" 'Already up to date: version 0.0.1.'
    snap "$DIR/build" >"$C/build-after.txt"
    cmp -s "$C/build-before.txt" "$C/build-after.txt" || bad "a run with nothing new changed the build"
    cmp -s "$C/profile-before" "$H/$PROFILE" || bad "a run with nothing new changed ~/$PROFILE"
    [ ! -e "$DIR/build-prev" ] || bad "a run with nothing new made a build-prev"
    [ ! -e "$DIR/build-next" ] || bad "a run with nothing new left a build-next"
    check_new_shell "$DIR" 0.0.1

    app_step old
    # a newer release (annotated this time): it updates, keeps the one before
    fake_release 0.0.2
    run_line "$C/out3" --no-launch
    ok_run "$C/out3" 0.0.2
    expect_text "$C/out3" 'Switched to version 0.0.2. The one before is kept in'
    check_build "$DIR" 0.0.2 stable
    app_step updated 0.0.2
    expect_in "$DIR/build-prev/install-info.txt" "version=0.0.1"
    expect_eq "$PROFILE lines" "$(marked_lines "$H/$PROFILE")" 1
    check_new_shell "$DIR" 0.0.2
    # the launcher the update starts (no --no-launch): from DIR, as the new version
    app_step foreign
    fake_release 0.0.3
    run_line "$C/out4"
    ok_run "$C/out4" 0.0.3
    app_step foreign_kept "$C/out4"
    wait_for_count "$DIR" "STP/TPW Launcher v0.0.3 args= cwd=$DIR" 0 || bad "the update did not start the new launcher"
    snap "$DATA" >"$C/data-after.txt"
    cmp -s "$C/data-before.txt" "$C/data-after.txt" || bad "the data folder was changed"
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
    expect_eq "$PROFILE lines" "$(marked_lines "$H/$PROFILE")" 1
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
    # a dev install on a Mac with no release at all: the plain line and --update say that it stays on main and how to
    # update it, and never that it now follows a release
    new_case dev_only
    fake_init
    run_line "$C/o8" --dev --no-launch
    ok_run "$C/o8" 0.0.1-dev
    run_line "$C/o9" --no-launch
    fail_run "$C/o9" 'No release has been published yet\. This install follows main; run themepark update --dev to update it\.'
    expect_text "$C/o9" 'themepark update --dev'
    expect_no_text "$C/o9" 'it now follows the newest release'
    check_build "$DIR" 0.0.1-dev dev
    cd "$DIR" || exit 1
    run_file "$C/o10" "$DIR/source/install.sh" --update --dir "$DIR" --no-launch
    fail_run "$C/o10" 'No release has been published yet\. This install follows main; run themepark update --dev'
    expect_no_text "$C/o10" 'it now follows the newest release'
    check_build "$DIR" 0.0.1-dev dev
    # the same in a terminal window (themepark update): the log of the dev install's build is still there, and a run
    # that failed before it started a log of its own must not show that one and ask for a bug report about it
    [ -s "$DIR/last-build.log" ] || bad "the dev install left no last-build.log for this check"
    run_tty "$C/o11" "$DIR/source/install.sh" --update --dir "$DIR" --no-launch
    [ "$RC" != 0 ] || bad "--update with no release exited with 0 in a terminal"
    expect_text "$C/o11" 'No release has been published yet'
    expect_text "$C/o11" 'themepark update --dev'
    expect_no_text "$C/o11" 'The last lines of its output'
    expect_no_text "$C/o11" 'bug report'
    expect_no_text "$C/o11" '==> building version'
    expect_no_text "$C/o11" 'RESULT '
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
    [ ! -e "$H/$PROFILE" ] || bad "a failed first build wrote a PATH line"
    # a build that ends well but made nothing, or a launcher of another version: refused before anything is switched
    export TPW_FAKE_BUILD=empty
    run_line "$C/o1b" --no-launch
    fail_run "$C/o1b" 'made no tpw_launcher'
    export TPW_FAKE_BUILD=wrongversion
    run_line "$C/o1c" --no-launch
    fail_run "$C/o1c" 'did not answer with version 0.0.1'
    [ ! -e "$DIR/build" ] || bad "a wrong build was switched in"
    [ ! -e "$H/$PROFILE" ] || bad "a wrong build wrote a PATH line"
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
    expect_eq "$PROFILE lines" "$(marked_lines "$H/$PROFILE")" 1

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
    [ ! -e "$H/$PROFILE" ] || bad "a PATH line of a stopped first install"
    run_line "$C/o2" --no-launch
    ok_run "$C/o2" 0.0.1
    check_build "$DIR" 0.0.1 stable
    expect_eq "$PROFILE lines" "$(marked_lines "$H/$PROFILE")" 1
    check_new_shell "$DIR" 0.0.1
}

# ---- main

# Runs the cases named (case_NAME each), each in a subshell: a failed check exits the case only, and its environment is
# gone.
run_cases() { # CASE...
    local failed="" c t0
    for c in "$@"; do
        echo "::group::case $c"
        t0=$(date +%s)
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
    echo "all cases passed: $*"
}
