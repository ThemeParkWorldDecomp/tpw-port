#!/bin/sh
# playtest.sh [options]: the local playtest every pull request that changes the game or the launcher shows (CONTRIBUTING.md,
# "Before a pull request"). On your own copy of the retail discs' files (the US Mac CD, the European PC CD and the Gold
# bonus CD, in the launcher's data folder) it
#   1. runs the verifier: the launcher's own check of the data folder, which must show 100% for US, European and Gold
#      (every required file there); with less the script refuses to play,
#   2. plays each edition through the easy-park recipe (lobby, new player, the first park, the Park Information panel) and
#      prints one result line per edition; Gold is started from the launcher window's Play, as a player does.
# The windows are run_launcher.sh's: behind the other windows, muted, on the built-in screen (--display NAME or N changes
# that); a run takes about 70 to 110 s per edition.
#
# Where the data is: the launcher's data folder (the one with launcher.ini and assets/) named by the environment variable
# TPW_PLAYTEST_DATA, or else by the first line of the git-ignored file .playtest-data in this repository's root. The
# location is a local setting: it is never committed, never put in a pull request. With neither set the script says so and
# exits 0 having tested nothing; that is a skip, not a pass, and a pull request cannot cite it.
#
# Options:
#   --build DIR      the build folder holding tpw_launcher (default: build, where `make` puts it)
#   --import PATH... first import these discs, disc images, folders or archives into the data folder (the launcher's
#                    --import), then verify; the verifier line is then the import's
#   --editions LIST  the editions to play, separated by spaces: us eur gold (default: all three; the verifier still
#                    must show all three at 100%)
#   --frames N       passes of the game's main loop per run (default 3300; the recipe's last click is at 2850)
#   --display D      the screen (default: the built-in one)
#   --keep DIR       keep each run's frames (every 150th, as .bmp) and the launcher's output in DIR (default: deleted)
# Exit code: 0 all passed (or skipped), 1 the verifier is below 100% or an edition failed, 2 bad options or no launcher.
# Windows: devtools/playtest.ps1 does the same.
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
build=build; editions="us eur gold"; frames=3300; display=""; keep=""; imports=""

while [ $# -gt 0 ]; do
  case "$1" in
    --build) build=$2; shift 2 ;;
    --editions) editions=$2; shift 2 ;;
    --frames) frames=$2; shift 2 ;;
    --display) display=$2; shift 2 ;;
    --keep) keep=$2; shift 2 ;;
    --import) shift; while [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; do imports="$imports
$1"; shift; done ;;
    -h|--help) sed -n '2,/^# Windows/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "playtest: unknown option $1 (--help lists them)" >&2; exit 2 ;;
  esac
done

data=${TPW_PLAYTEST_DATA:-}
if [ -z "$data" ] && [ -f "$root/.playtest-data" ]; then
  data=$(sed -n '/^[[:space:]]*#/d; /^[[:space:]]*$/d; p; q' "$root/.playtest-data")
fi
if [ -z "$data" ]; then
  echo "playtest: SKIPPED, nothing was tested. Set TPW_PLAYTEST_DATA (or put the path on the first line of the"
  echo "playtest: git-ignored file .playtest-data in the repository's root) to the launcher's data folder holding your"
  echo "playtest: own US, European and Gold game files. See CONTRIBUTING.md, \"Before a pull request\"."
  exit 0
fi
[ -d "$data" ] || { echo "playtest: the data folder does not exist: $data" >&2; exit 2; }
case "$frames" in ''|*[!0-9]*) echo "playtest: --frames wants a whole number: $frames" >&2; exit 2 ;; esac
[ "$frames" -ge 2900 ] || { echo "playtest: --frames $frames ends the run before the Park Information panel (2900 or more)" >&2; exit 2; }
for e in $editions; do
  case "$e" in us|eur|gold) ;; *) echo "playtest: editions are us, eur and gold: $e" >&2; exit 2 ;; esac
done
launcher="$build/tpw_launcher"
[ -x "$launcher" ] || { echo "playtest: no launcher at $launcher; build it first (make), or name the build folder with --build" >&2; exit 2; }

work=$(mktemp -d "${TMPDIR:-/tmp}/tpw-playtest.XXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT
[ -z "$keep" ] || mkdir -p "$keep"

echo "playtest: $("$launcher" --version 2>/dev/null | tr '\n' ' ')"

# 1. The verifier: --import with the given paths, or with an empty folder (nothing to import), prints the status of each
# edition in the data folder ("import: us: unlocked, 770 of 770 required files") and exits 0 only when all three are ready.
mkdir "$work/nothing"
if [ -n "$imports" ]; then
  oldifs=$IFS; IFS='
'
  # shellcheck disable=SC2086
  set -- $imports
  IFS=$oldifs
else
  set -- "$work/nothing"
fi
"$launcher" --data "$data" --import "$@" > "$work/verify.txt" 2>&1
[ -z "$keep" ] || cp "$work/verify.txt" "$keep/verify.txt"
line=""; short=""
for e in us eur gold; do
  # "import: us: unlocked, 770 of 770 required files" -> "us 770 770"
  nums=$(sed -n "s/.*import: $e: [^,]*, \([0-9][0-9]*\) of \([0-9][0-9]*\) required files.*/\1 \2/p" "$work/verify.txt" | tail -n 1)
  have=${nums% *}; need=${nums#* }
  label=$(echo "$e" | sed 's/us/US/; s/eur/EUR/; s/gold/Gold/')
  if [ -z "$nums" ] || [ "$have" != "$need" ] || [ "$need" -eq 0 ]; then
    short="$short $label"
    line="$line $label ${have:-?}/${need:-?}"
  else
    line="$line $label $have/$need"
  fi
done
if [ -n "$short" ]; then
  echo "playtest: verifier:$line, NOT 100% (not complete:$short). Refusing to play."
  grep 'import: \(error\|us:\|eur:\|gold:\)' "$work/verify.txt" | sed 's/^/playtest:   /'
  exit 1
fi
echo "playtest: verifier:$line, 100% for US, European and Gold"

# 2. The plays. The easy-park recipe at run_launcher.sh's 2048 x 1536 (4:3): the lobby's New Player, the name, the first
# park's gate, the park's buttons, Park Information (clicks are at passes up to 2850).
recipe="--click 1326,121@200 --type Tess@230 --click 1418,566@300 --click 143,1177@800 --click 261,1414@2300
--click 346,1192@2500 --click 504,353@2700 --click 1196,200@2850"
fails=0
for e in $editions; do
  case "$e" in us) label=US ;; eur) label=EUR ;; gold) label=Gold ;; esac
  name="playtest-$e"
  fdir="$work/frames-$e"; mkdir "$fdir"
  : > "$work/stamp-$e"
  common="--data $data --mute --no-movies --frames $frames --dump-frames $fdir --dump-every 150"
  [ -z "$display" ] || common="$common --display $display"
  start=$(date +%s)
  if [ "$e" = gold ]; then
    # The launcher's window (no --play) and a click on the US tile's Play (window point 205,373 of its 800 x 560; the European tile's is 595,373), which
    # starts the game with the Gold rides switched on when all three editions are ready.
    # shellcheck disable=SC2086
    sh "$here/run_launcher.sh" "$build" "$name" 900 $common --window-click 205,373@30 $recipe
  else
    # shellcheck disable=SC2086
    sh "$here/run_launcher.sh" "$build" "$name" 900 $common --play "$e" $recipe
  fi
  secs=$(( $(date +%s) - start ))
  runlog="$build/launch-$name.log"
  # The launcher's output, and the data folder's run logs written since (the game's own log: on a Mac the Gold run's
  # game is an application of its own beside the launcher).
  cat "$runlog" > "$work/out-$e.txt" 2>/dev/null
  find "$data/logs" -name '*.log' -newer "$work/stamp-$e" -exec cat {} + >> "$work/out-$e.txt" 2>/dev/null
  nframes=$(find "$fdir" -name '*.bmp' | wc -l | tr -d ' ')
  why=""
  rc=$(sed -n 's/^exit=//p' "$runlog" | tail -n 1)
  [ "$rc" = 0 ] || why="$why; exit ${rc:-unknown}"
  grep -q 'force-quit' "$runlog" && why="$why; force-quit at the time limit"
  stand=$(sed -n 's/.*launcher: \(0 of [0-9][0-9]*\) stand-ins were reached.*/\1/p' "$work/out-$e.txt" | tail -n 1)
  [ -n "$stand" ] || why="$why; no \"0 of N stand-ins were reached\" line"
  [ "$nframes" -gt 0 ] || why="$why; no frames"
  if [ "$e" = gold ]; then
    grep -q 'play: Sim Theme Park, the US edition' "$work/out-$e.txt" || why="$why; the game that started was not the US edition (the click missed the US tile's Play?)"
    grep -q 'Gold rides: on, [0-9][0-9]* rides overlaid from assets/gold$' "$work/out-$e.txt" || why="$why; no whole \"Gold rides: on\" line"
  fi
  if [ -z "$why" ]; then
    echo "playtest: $label: PASS (exit 0, $stand stand-ins, $nframes frames, $secs s)"
  else
    echo "playtest: $label: FAIL (${why#; })"
    tail -n 8 "$work/out-$e.txt" | sed 's/^/playtest:   /'
    fails=$((fails + 1))
  fi
  if [ -n "$keep" ]; then
    mkdir -p "$keep/$e"; cp "$fdir"/*.bmp "$keep/$e/" 2>/dev/null; cp "$work/out-$e.txt" "$keep/$e/output.txt"
  fi
done
if [ $fails -eq 0 ]; then echo "playtest: all passed"; exit 0; fi
echo "playtest: $fails edition(s) failed"; exit 1
