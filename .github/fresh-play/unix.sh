#!/bin/bash
# The steps of fresh-play.yml on Linux and macOS: a fresh install of this commit with the players' install line, the
# game bundle imported by the installed launcher and the easy-park plays. TEMPORARY, on the ci-020-fresh branch only.
#
#   bash .github/fresh-play/unix.sh STEP [ARGS]
#
# STEP is one of: secrets, release, install, version, desktop, fetch, import, play NAME, frames, clean. Every step
# writes what it saw into $RUNNER_TEMP/play-out (the run's artifact: outputs, logs and the frames as png). The game's
# files exist only on the runner: the bundle ($RUNNER_TEMP/tpw-bundle.zip), the data folder and the bmp frames are
# never kept, and `clean` deletes them.
set -o pipefail

STEP=${1:-}
[ $# -gt 0 ] && shift
OUT=$RUNNER_TEMP/play-out
FRAMES=$RUNNER_TEMP/tpw-frames
ZIP=$RUNNER_TEMP/tpw-bundle.zip
MIRROR=$RUNNER_TEMP/release
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../.." && pwd)
mkdir -p "$OUT"

case "$(uname -s)" in
    Darwin)
        OS=mac
        DIR=$HOME/Applications/STP-TPW-Launcher
        DATA="$HOME/Library/Application Support/STP-TPW Launcher"
        ;;
    Linux)
        OS=linux
        DIR=${XDG_DATA_HOME:-$HOME/.local/share}/STP-TPW-Launcher
        DATA="${XDG_DATA_HOME:-$HOME/.local/share}/STP-TPW Launcher"
        ;;
    *) echo "::error::this script is for Linux and macOS"; exit 2 ;;
esac
LAUNCHER=$DIR/build/tpw_launcher
VERSION=$(sed -n '1p' "$SRC/launcher/VERSION.txt" | tr -d '\r')

fail() {
    echo "::error::$*"
    exit 1
}

# The sizes of the data folder's log files now, one "SIZE<tab>PATH" line each, into $1.
snap_logs() {
    : >"$1"
    [ -d "$DATA" ] || return 0
    find "$DATA" -maxdepth 2 -type f -name '*.log' | while IFS= read -r f; do
        printf '%s\t%s\n' "$(wc -c <"$f" | tr -d ' ')" "$f"
    done >"$1"
}

# What the log files gained since the snapshot $1 (a new file whole).
new_log_text() {
    [ -d "$DATA" ] || return 0
    find "$DATA" -maxdepth 2 -type f -name '*.log' | sort | while IFS= read -r f; do
        old=$(awk -F '\t' -v f="$f" '$2 == f { print $1 }' "$1")
        echo "==== $f (from byte ${old:-0})"
        tail -c +$((${old:-0} + 1)) "$f"
    done
}

# Processes running a program of the install folder's build (the launcher, its bundle, the game's bundle on a Mac).
build_pids() {
    pgrep -f "^$DIR/build/" 2>/dev/null
}

case "$STEP" in
    # Fails: one of the three secrets is not set (they are the repository's Actions secrets, set by the maintainer).
    secrets)
        missing=""
        [ -n "${BUNDLE_URL:-}" ] || missing="$missing TPW_FRESH_BUNDLE_URL"
        [ -n "${BUNDLE_SIZE:-}" ] || missing="$missing TPW_FRESH_BUNDLE_SIZE"
        [ -n "${BUNDLE_MD5:-}" ] || missing="$missing TPW_FRESH_BUNDLE_MD5"
        [ -z "$missing" ] || fail "the bundle's secrets are not set:$missing (Settings, Secrets and variables, Actions)"
        echo "the bundle's three secrets are set"
        ;;

    # A local release of this checkout: a copy of it whose only release tag is v$VERSION on the checked-out commit, and
    # bare copies of its submodules beside it (their addresses are relative, ../tpw-decomp), so that the stable channel,
    # the players' path, finds this commit as the newest release. Nothing is pushed: the tag exists only on the runner.
    release)
        case "$VERSION" in [0-9]*.[0-9]*.[0-9]) ;; *) fail "launcher/VERSION.txt says $VERSION, not a release" ;; esac
        mkdir -p "$MIRROR"
        git clone -q "$SRC" "$MIRROR/tpw-port" || fail "cannot copy the checkout"
        git -C "$MIRROR/tpw-port" tag -l | while read -r t; do git -C "$MIRROR/tpw-port" tag -d "$t" >/dev/null; done
        git -C "$MIRROR/tpw-port" tag "v$VERSION" "$(git -C "$SRC" rev-parse HEAD)" || fail "cannot tag v$VERSION"
        git -C "$SRC" config -f .gitmodules --get-regexp '\.path$' | while read -r key path; do
            url=$(git -C "$SRC" config -f .gitmodules --get "${key%.path}.url")
            case "$url" in ../*) ;; *) echo "::error::the submodule $path has the address $url"; exit 1 ;; esac
            git clone -q --bare "$SRC/$path" "$MIRROR/${url#../}" || exit 1
            git -C "$MIRROR/${url#../}" branch -f ci-pin "$(git -C "$SRC/$path" rev-parse HEAD)" || exit 1
            echo "submodule $path: $(git -C "$SRC/$path" rev-parse HEAD)"
        done || fail "cannot copy the submodules"
        echo "the local release's tags:"
        git ls-remote --tags "$MIRROR/tpw-port" | tee "$OUT/release-tags.txt"
        [ "$(wc -l <"$OUT/release-tags.txt" | tr -d ' ')" = 1 ] || fail "the local release has other tags"
        echo "v$VERSION is $(git -C "$MIRROR/tpw-port" rev-parse "v$VERSION^{commit}")"
        ;;

    # The players' line (README, Quick start) with the local release in place of GitHub's: curl fetches install.sh and
    # sh runs it from the pipe, on its stable channel. --no-launch: the launcher is started by the steps below instead of
    # at the end of the install. On Linux it installs the build's packages itself with apt-get (sudo needs no password on
    # the runner). Fails: the install folder or the data folder is already there (not a fresh runner), the script fails,
    # or its last line is not "RESULT ok VERSION".
    install)
        [ ! -e "$DIR" ] || fail "an install folder is already there: $DIR"
        [ ! -e "$DATA" ] || fail "a data folder is already there: $DATA"
        export TPW_INSTALL_REPO=$MIRROR/tpw-port
        # git 2.38.1 and newer refuse a local submodule's file protocol unless allowed (installer-ci/mac.sh does the same)
        export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=protocol.file.allow GIT_CONFIG_VALUE_0=always GIT_TERMINAL_PROMPT=0
        echo "curl -fsSL file://$TPW_INSTALL_REPO/install.sh | sh -s -- --no-launch"
        start=$(date +%s)
        curl -fsSL "file://$TPW_INSTALL_REPO/install.sh" | sh -s -- --no-launch 2>&1 | tee "$OUT/install.txt"
        rc=$?
        echo "the install took $(($(date +%s) - start)) s, exit $rc"
        [ -f "$DIR/last-build.log" ] && cp "$DIR/last-build.log" "$OUT/last-build.log"
        last=$(tail -n 1 "$OUT/install.txt")
        [ $rc -eq 0 ] || fail "the install exited with $rc: $last"
        [ "$last" = "RESULT ok $VERSION" ] || fail "the install's last line is \"$last\", not \"RESULT ok $VERSION\""
        [ "$(cat "$DIR/channel.txt" 2>/dev/null)" = stable ] || fail "channel.txt says $(cat "$DIR/channel.txt" 2>/dev/null)"
        [ -x "$LAUNCHER" ] || fail "no launcher at $LAUNCHER"
        echo "installed: $LAUNCHER, channel stable, source $(git -C "$DIR/source" rev-parse HEAD)"
        ;;

    # The installed launcher's --version, then themepark --version in a new login shell of the player's shell (the
    # README's way to start it). Fails: the first line is not "STP/TPW Launcher vVERSION", or the new shell does not
    # find themepark or gets another answer.
    version)
        "$LAUNCHER" --version </dev/null | tee "$OUT/version.txt" || fail "--version exited with an error"
        first=$(sed -n '1p' "$OUT/version.txt")
        [ "$first" = "STP/TPW Launcher v$VERSION" ] || fail "--version says \"$first\""
        shell=${SHELL:-/bin/bash}
        out=$(cd / && env -i HOME="$HOME" USER="${USER:-}" LOGNAME="${LOGNAME:-}" TERM=dumb \
            PATH=/usr/bin:/bin:/usr/sbin:/sbin "$shell" -l -c 'command -v themepark; themepark --version; echo "exit=$?"' \
            </dev/null 2>&1)
        printf '%s\n' "$out" | tee "$OUT/themepark-version.txt"
        case "$out" in "$DIR/bin/themepark"*) ;; *) fail "a new $shell login shell finds themepark at \"${out%%$'\n'*}\"" ;; esac
        case "$out" in *"STP/TPW Launcher v$VERSION"*"exit=0") ;; *) fail "themepark --version in a new shell did not answer v$VERSION with exit 0" ;; esac
        echo "the installed launcher says v$VERSION, and themepark --version in a new $shell login shell too"
        ;;

    # Linux: the test desktop, which a player's computer has and the runner does not: a virtual screen (xvfb) and
    # Mesa's software drivers, llvmpipe for OpenGL and lavapipe for Vulkan. Installed after the install, so that the
    # install ran on the runner as it is.
    desktop)
        [ "$OS" = linux ] || exit 0
        sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
            xvfb xauth libgl1-mesa-dri libglx-mesa0 libegl-mesa0 mesa-vulkan-drivers libvulkan1 || fail "apt-get failed"
        ;;

    # The bundle, fetched fresh by this run. Fails: curl gives up after three tries or the server answers an error, the
    # file is not the bundle's size, or its MD5 is not the bundle's checksum.
    fetch)
        curl --fail --location --retry 3 --retry-delay 10 --silent --show-error --output "$ZIP" \
            --write-out 'fetched %{size_download} bytes in %{time_total} s (%{speed_download} bytes/s)\n' \
            "$BUNDLE_URL" || fail "the bundle could not be fetched (curl exit $?)"
        size=$(wc -c <"$ZIP" | tr -d ' ')
        want=$(printf '%s' "$BUNDLE_SIZE" | tr -d ' \r\n')
        [ "$size" = "$want" ] || fail "the bundle is $size bytes, not $want"
        if [ "$OS" = mac ]; then sum=$(md5 -q "$ZIP"); else sum=$(md5sum "$ZIP" | cut -d ' ' -f 1); fi
        want=$(printf '%s' "$BUNDLE_MD5" | tr -d ' \r\n' | tr 'A-F' 'a-f')
        [ "$sum" = "$want" ] || fail "the bundle's checksum does not match (a damaged fetch or another file)"
        echo "the bundle: $size bytes, its checksum matches"
        ;;

    # The bundle imported by the installed launcher: --import with no window, which is the drop's import (a drop on the
    # window and --import both hand the paths to ImporterBackend::Start: launcher/ui/launcher_ui.cpp OfferPaths and
    # StartImport, launcher/main.cpp ImportFromOptions), into the launcher's own data folder, fresh on this runner.
    # Fails: the launcher exits other than 0 (an edition not ready: the "import:" lines say what is missing).
    import)
        start=$(date +%s)
        "$LAUNCHER" --import "$ZIP" </dev/null >"$OUT/import.txt" 2>&1
        rc=$?
        cat "$OUT/import.txt"
        echo "the import took $(($(date +%s) - start)) s, exit $rc"
        [ $rc -eq 0 ] || fail "tpw_launcher --import exited with $rc"
        grep -F "import: into the data folder" "$OUT/import.txt" || fail "the import did not name its data folder"
        [ -d "$DATA" ] || fail "the data folder $DATA is not there after the import"
        ;;

    # One play through the easy-park recipe (devtools/playtest.sh's clicks at 2048 x 1536, halved for 1024 x 768): the
    # lobby's New Player, the name, the first park's gate, the park's buttons, Park Information. NAME: us, eur (--play),
    # gold (the launcher window's Play on the US tile, window point 205,373 of 800 x 560, which switches the Gold rides
    # on when all three editions are ready) or us-vulkan (--play us with --renderer vulkan). Fails: the launcher exits
    # other than 0 or runs past 15 minutes, the log has a crash, an exit other than code 0 or no exit line, the
    # "0 of N stand-ins were reached" line is missing, no frame was dumped, or (gold) the game was not the US edition
    # with the Gold rides on.
    play)
        name=$1
        fdir=$FRAMES/$name
        mkdir -p "$fdir"
        snap_logs "$OUT/logs-before-$name.txt"
        case "$name" in
            us) start_args="--play us" ;;
            eur) start_args="--play eur" ;;
            gold) start_args="--window-click 205,373@30" ;;
            us-vulkan) start_args="--play us --renderer vulkan" ;;
            *) fail "play wants us, eur, gold or us-vulkan: $name" ;;
        esac
        # Linux's test desktop: SDL's software renderer for the launcher's own 2D window (no GPU on the runner), and no
        # sound device. Neither is in the install.
        [ "$OS" = linux ] && export SDL_RENDER_DRIVER=software SDL_AUDIO_DRIVER=dummy
        recipe="--click 663,60@200 --type Tess@230 --click 709,283@300 --click 71,588@800 --click 130,707@2300"
        recipe="$recipe --click 173,596@2500 --click 252,176@2700 --click 598,100@2850"
        common="--mute --no-movies --windowed --display 0 --aspect 4:3 --resolution 1024x768 $recipe"
        common="$common --frames 3300 --dump-frames $fdir --dump-every 150"
        # shellcheck disable=SC2086  # the options are words
        if [ "$OS" = linux ]; then
            echo "xvfb-run -a -s \"-screen 0 1280x1024x24\" $LAUNCHER $start_args $common"
            xvfb-run -a -s "-screen 0 1280x1024x24" "$LAUNCHER" $start_args $common </dev/null >"$OUT/play-$name.txt" 2>&1 &
        else
            echo "$LAUNCHER $start_args $common"
            "$LAUNCHER" $start_args $common </dev/null >"$OUT/play-$name.txt" 2>&1 &
        fi
        pid=$!
        t0=$(date +%s)
        late=""
        while kill -0 "$pid" 2>/dev/null; do
            sleep 10
            tail -n 1 "$OUT/play-$name.txt"
            if [ $(($(date +%s) - t0)) -ge 900 ]; then late=yes; kill -9 "$pid"; break; fi
        done
        wait "$pid"
        rc=$?
        # The launcher window's Play on a Mac starts the game as an application of its own (launcher/game_app.h) and
        # exits: the run goes on while a program of the build runs, within the same 15 minutes.
        while [ -z "$late" ] && [ -n "$(build_pids)" ]; do
            sleep 10
            if [ $(($(date +%s) - t0)) -ge 900 ]; then late=yes; fi
        done
        left=$(build_pids | tr '\n' ' ')
        [ -z "$left" ] || { echo "still running, killed: $left"; build_pids | xargs kill -9 2>/dev/null; }
        secs=$(($(date +%s) - t0))
        { cat "$OUT/play-$name.txt"; new_log_text "$OUT/logs-before-$name.txt"; } >"$OUT/text-$name.txt"
        tail -n 25 "$OUT/play-$name.txt"
        nframes=$(find "$fdir" -name 'frame-*.bmp' | wc -l | tr -d ' ')
        lastframe=$(find "$fdir" -name 'frame-*.bmp' | sort | tail -n 1)
        why=""
        [ -z "$late" ] || why="$why; still running after 15 minutes (killed)"
        [ "$rc" = 0 ] || why="$why; the launcher exited with $rc"
        if grep -E 'launcher: crashed|exit: crashed|exit: abort' "$OUT/text-$name.txt"; then why="$why; a crash"; fi
        if grep -E 'exit: code [1-9]' "$OUT/text-$name.txt"; then why="$why; an exit code other than 0 in the log"; fi
        grep -qE 'exit: code 0' "$OUT/text-$name.txt" || why="$why; no \"exit: code 0\" line in the log"
        stand=$(grep -oE 'launcher: [0-9]+ of [0-9]+ stand-ins were reached' "$OUT/text-$name.txt" | tail -n 1)
        case "$stand" in
            "launcher: 0 of "*) ;;
            "") why="$why; no \"0 of N stand-ins were reached\" line" ;;
            *) why="$why; $stand" ;;
        esac
        [ "$nframes" -gt 0 ] || why="$why; no frames"
        if [ "$name" = gold ]; then
            grep -q 'play: Sim Theme Park, the US edition' "$OUT/text-$name.txt" ||
                why="$why; the game that started was not the US edition (the click missed the US tile's Play?)"
            grep -qE 'Gold rides: on, [0-9]+ rides overlaid from assets/gold$' "$OUT/text-$name.txt" ||
                why="$why; no whole \"Gold rides: on\" line"
        fi
        grep -E '^.{0,40}(play: |Gold rides: |renderer|OpenGL|Vulkan|vulkan:|opengl:)' "$OUT/text-$name.txt" | sort -u | head -n 20
        summary="$name: exit $rc, ${stand#launcher: }, $nframes frames (last $(basename "${lastframe:-none}")), $secs s"
        echo "$summary" >"$OUT/result-$name.txt"
        if [ -n "$why" ]; then
            echo "FAIL $summary" >>"$OUT/result-$name.txt"
            fail "--play $name: ${why#; } ($summary)"
        fi
        echo "PASS $summary" | tee -a "$OUT/result-$name.txt"
        ;;

    # The frames as png (python's standard library only), into play-out/frames/NAME, and the data folder's logs (the
    # launcher's and the game's run logs, no game file) into play-out/data-logs. Fails: never.
    frames)
        if [ -d "$DATA" ]; then
            mkdir -p "$OUT/data-logs"
            find "$DATA" -maxdepth 2 -type f -name '*.log' -exec cp {} "$OUT/data-logs/" \;
            ls -l "$OUT/data-logs"
        fi
        [ -d "$FRAMES" ] || exit 0
        python3 -I "$HERE/bmp2png.py" "$FRAMES" "$OUT/frames" || echo "some frames were not converted"
        exit 0
        ;;

    # The bundle, the data folder (the imported game files) and the bmp frames, deleted whatever happened. The install
    # folder holds no game file. Fails: never.
    clean)
        rm -rf "$ZIP" "$DATA" "$FRAMES" "$MIRROR"
        ls -a "$RUNNER_TEMP"
        [ ! -e "$ZIP" ] && [ ! -e "$DATA" ] && echo "the bundle and the data folder are deleted"
        ;;

    *) echo "::error::unknown step: $STEP"; exit 2 ;;
esac
