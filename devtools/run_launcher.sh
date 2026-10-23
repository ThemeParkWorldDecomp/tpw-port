#!/bin/sh
# run_launcher.sh BUILD_DIR NAME SECONDS [launcher options]: runs BUILD_DIR/tpw_launcher from
# the project root with its own user folder (BUILD_DIR/user-NAME) and log
# (BUILD_DIR/launch-NAME.log), and force-quits it after SECONDS. (The launcher turns a plain
# kill into a quit event, which a hung loop never sees, so the kill is -9.) Unless the options
# name a --display, the window opens on the MacBook's built-in screen: test and development
# windows must never land on the maintainer's external monitor (2026-10-13).
build=$1; name=$2; limit=$3; shift 3
root=$(cd "$(dirname "$0")/../.." && pwd)
display="--display builtin"
for arg in "$@"; do [ "$arg" = "--display" ] && display=""; done
# By default the launcher takes the screen's own shape (the nearest of 4:3, 16:10 and 16:9; tpw-port PR #9) at its
# largest preset, but a test run is 4:3 at
# 2048 x 1536 unless the options name an --aspect or a --resolution: the click recipes in HANDOFF.md
# and the process logs are 4:3 game pixels (2048 x 1536), and their frames stay comparable with the
# older ones. A widescreen run passes --aspect 16:9 and
# moves the clicks on right- and centre-anchored panels; the easy-park recipe at 16:9 (2730 x 1536) is
#   --aspect 16:9 --click 2008,121@200 --type Tess@230 --click 1759,566@300 --click 484,1177@800
#   --click 261,1414@2300
# (the lobby's "Create New Player" bar is right-anchored, 682 pixels further right; the name
# dialog is centred, 341 further right; the island panel's gate button, bottom left, is in the centred 4:3 box since
# tpw-port PR #12, 341 further right; the park buttons bottom left do not move). At 16:10 (2458 x 1536)
# the three moved clicks are 1736,121, 1623,566 and 348,1177. The launcher's own default on the MacBook (16:10 at
# 3840 x 2400, the 2458-pixel recipe times 1.5625; --launcher-defaults, below, runs it with neither option) is
#   --launcher-defaults --click 2712,189@200 --type Tess@230 --click 2536,884@300
#   --click 544,1839@800 --click 408,2209@2300
# (all checked by frames on 2026-10-23; on a 16:9 screen the default is 16:9 at 3840 x 2160, the 2730-pixel recipe
# times 1.40625: 2824,170, 2474,796, 681,1655 and 367,1988, not checked, as no 16:9 screen is used for test runs).
aspect="--aspect 4:3"
for arg in "$@"; do [ "$arg" = "--aspect" ] && aspect=""; done
resolution="--resolution 2048x1536"
for arg in "$@"; do [ "$arg" = "--resolution" ] && resolution=""; done
# --launcher-defaults (the script's own option) passes neither, for a run exactly as a player starts the game.
for arg in "$@"; do [ "$arg" = "--launcher-defaults" ] && aspect="" && resolution=""; done
# A test run swaps at every refresh of the screen (TPW_VSYNC_EVERY_REFRESH=1, platform/window.cpp), as before the 60-frame
# pacing (tpw-port PR #8): the recipes' clicks are at pass numbers, and with the pacing on a 120 Hz screen 5000 passes take
# 105 s instead of 66 to 69 s. The script's own --paced leaves it unset, for the pacing a player gets ("vsync: every 2
# refreshes of 120.00 Hz" in the log on the MacBook; a run without it logs "vsync: every refresh ... a test run").
paced=""
for arg in "$@"; do [ "$arg" = "--paced" ] && paced=yes; done
if [ -z "$paced" ]; then TPW_VSYNC_EVERY_REFRESH=1; export TPW_VSYNC_EVERY_REFRESH; fi
# A test run also opens behind the other windows and never takes the focus (--background), so the
# person working on that screen is not interrupted; pass --foreground to have it in front.
background="--background"
# A test run is in the game's window with a title bar (--windowed). The script's own --fullscreen passes neither
# --windowed nor --background: the game's real full-screen window, a Space of its own on the --display screen (the
# built-in one unless the options name another), as a player gets it; that screen is taken over until the run ends.
windowed="--windowed"
# The game comes from the extracted US CD (--game, --program below) unless the options start from the launcher's data
# folder instead: --play us|eur (that edition, no launcher window), --data DIR (another data folder than the per-user
# one), or the script's own --launcher-window (the launcher's window opens, on the same screen and behind the others like
# the game's; drop the discs on it and press Play). The run keeps its own user folder either way.
from_data=""
for arg in "$@"; do
  case "$arg" in --play|--data|--launcher-window) from_data=yes ;; esac
done
rest=""
for arg in "$@"; do
  if [ "$arg" = "--foreground" ]; then background=""
  elif [ "$arg" = "--fullscreen" ]; then background=""; windowed=""
  elif [ "$arg" != "--launcher-defaults" ] && [ "$arg" != "--launcher-window" ] && [ "$arg" != "--paced" ]; then
    rest="$rest $arg"
  fi
done
mkdir -p "$build/user-$name"
# Sign the build with a stable identity (TPW_SIGN_IDENTITY, by default the keychain's "Apple Development"
# certificate). The linker's ad-hoc signature changes with every build, so macOS took each one for a new
# app and asked again for the builds' removable volume; a signed build keeps the grant. No identity: the
# ad-hoc signature stays.
codesign -f -s "${TPW_SIGN_IDENTITY:-Apple Development}" "$build/tpw_launcher" >/dev/null 2>&1 || true
cd "$root" || exit 1
# The game's data tables come from the US CD's program (--program), whichever game folder the options name: the European
# folder (devtools/import_eur.py) has no Mac program of its own. A --program in the options takes its place.
if [ -n "$from_data" ]; then
  "$build/tpw_launcher" --user "$build/user-$name" $windowed $display $aspect $resolution $background $rest > "$build/launch-$name.log" 2>&1 &
else
  "$build/tpw_launcher" --game "extracted/disc-hfs/Theme Park Data" --program "extracted/disc-hfs/Theme Park Data/SimTheme Park" --user "$build/user-$name" $windowed $display $aspect $resolution $background $rest > "$build/launch-$name.log" 2>&1 &
fi
pid=$!
n=0
while kill -0 $pid 2>/dev/null; do
  sleep 1; n=$((n + 1))
  if [ $n -ge $limit ]; then kill -9 $pid; echo "force-quit after ${limit}s" >> "$build/launch-$name.log"; break; fi
done
wait $pid 2>/dev/null
echo "exit=$?" >> "$build/launch-$name.log"
