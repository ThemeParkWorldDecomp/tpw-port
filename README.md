# tpw-port

An unofficial port of Sim Theme Park (AKA Theme Park World, 1999) for Macs (Apple Silicon and Intel) and 64-bit Windows.
You need your own copy of the game. Nothing from the game is in here.

Not affiliated with or endorsed by Electronic Arts. Sim Theme Park and Theme Park World are trademarks of their
respective owners.

**Reporting a problem? Always say which launcher version you run** (for example 0.0.3). The launcher's window title,
its About box and the first line of its log all show it. See [Reporting a problem](#reporting-a-problem).

**What's new:** [CHANGELOG.md](CHANGELOG.md) lists what each version adds and fixes.

## Quick start

The easiest way is one line. It downloads the source of the newest release and builds the launcher on your computer,
in a few minutes. A launcher your computer built is not blocked by macOS or Windows SmartScreen, so there are no
warnings to click through. Run the same line again to update.

- Mac (macOS 11 or newer, Apple Silicon or Intel), in Terminal:

      curl -fsSL https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.sh | sh

  It needs Apple's command line developer tools. If they are missing, it says so and how to get them, and changes
  nothing. The launcher goes in `~/Applications/STP-TPW-Launcher/build/tpw_launcher`.
- Windows 10 (version 1803 or newer) or 11, 64-bit, in PowerShell:

      irm https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.ps1 | iex

  It needs Git (`winget install Git.Git`, then a new PowerShell window). The launcher goes in
  `%LOCALAPPDATA%\STP-TPW-Launcher\build\tpw_launcher.exe`.

If you already play from a clone of tpw-port (the way below), run the same line too. It installs in its own folder and
never touches the clone; at the end it says the clone is no longer needed, so you can delete it. Your games, saves and
settings are not in the clone: they are in the launcher's data folder (`~/Library/Application Support/STP-TPW Launcher`
on a Mac, `%APPDATA%\STP-TPW Launcher` on Windows), and the new install uses them as they are.

The installer starts the launcher when it is done. Options go after `sh -s --` on a Mac, and after a scriptblock on
Windows. For example, `--dev` (`-Dev`) installs main, the next version while it is being made (it can have bugs):

    curl -fsSL https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.sh | sh -s -- --dev
    & ([scriptblock]::Create((irm https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest/download/install.ps1))) -Dev

To stay on main, add `--dev` (`-Dev`) every time you run the line: without it, the line switches the install back to the
newest release, and says so. So does the launcher's Update button, which always gets the newest release.

`--dir PATH` (`-Dir PATH`) installs somewhere else, and `--help` (`-Help`) lists the rest. The installer needs no
administrator rights. It writes only in its own folder, apart from one line that lets you type `themepark` (below).
Your game files and saves are not in its folder, and it never touches them.

### Play after install

Type this in a Terminal (Mac) or PowerShell (Windows) window, in any folder:

    themepark

It starts the launcher, where the Update button is.

- On a Mac, the command works in every new Terminal window you open after installing (not in the window you installed
  from). The first install opens the launcher by itself, so you can play straight away.
- On Windows, it works straight away: in the PowerShell window you installed from and in every new one.

At the end, the installer checks that a new window finds `themepark`. If one does not (an unusual shell setup, for
example), it shows the one line to run that fixes it.

To remove the installed launcher, delete its folder (`~/Applications/STP-TPW-Launcher` on a Mac,
`%LOCALAPPDATA%\STP-TPW-Launcher` on Windows). On a Mac, also delete the line that ends with
`# tpw-port installer: the themepark command` from `~/.zprofile` (and from `~/.bash_profile`, `~/.bash_login` or
`~/.profile` if it is there too). On Windows, also remove the folder's `bin` entry from your Path: search the Start menu
for "Edit environment variables for your account", then edit Path. Your game files, saves and settings are kept in the
launcher's data folder and stay.

### Ready-made downloads

The other way to install is a ready-made launcher from the [Releases page](https://github.com/ThemeParkWorldDecomp/tpw-port/releases/latest). It was
not built on your computer, so macOS and Windows warn about it at first. It does not update itself: its Update button
opens the Releases page.

- Windows 10 or 11 (64-bit): the `-windows-x64.zip`. Unzip it and run tpw_launcher.exe. If Windows SmartScreen stops it,
  click More info, then Run anyway.
- Mac with Apple Silicon: the `-macos-arm64.tar.gz`. Mac with an Intel processor: the `-macos-x64.tar.gz`. Double-click it
  to unpack it. The launcher is not signed yet, so macOS blocks it at first: in Terminal, run
  `xattr -d com.apple.quarantine ` followed by the unpacked tpw_launcher (drag it onto the Terminal window), then
  double-click tpw_launcher.

A release is fixed: 0.0.3 is the same code on every Mac and PC and never changes afterwards, and fixes come in the next
version. The Mac and Windows downloads of a release and the installers' builds of it are all built from the same commit,
its tag (v0.0.3).

Then see [Playing](#playing).

### Building from source by hand

Follow the steps below. Their clone uses `--branch stable`, the newest release (the stable branch follows the
releases). `--branch v0.0.3` gives one release in particular. A clone without `--branch` is main: the next version
while it is being made (its launcher says so, 0.0.4-dev for example), for contributors and testers.

### Building on macOS

On a Mac with Apple Silicon or an Intel processor, in Terminal:

    git clone --branch stable --recurse-submodules https://github.com/ThemeParkWorldDecomp/tpw-port.git
    cd tpw-port && make
    make run

If macOS asks to install the command line developer tools, click Install, wait, then run the first line again. The first
build takes a few minutes. If your Mac has no CMake, `make` fetches it into the build
folder. Nothing else is installed.

### Building on Windows

On a PC with 64-bit Windows 10 (version 1803 or newer) or Windows 11, Intel, AMD or Arm (an Arm PC needs Windows 11),
install Git once if you do not have it:

    winget install Git.Git

After installing Git, open a new PowerShell or Command Prompt window. Paste these lines:

    git -c core.autocrlf=false clone --branch stable --recurse-submodules https://github.com/ThemeParkWorldDecomp/tpw-port.git
    cd tpw-port
    .\build.cmd
    .\run.cmd

The first build downloads its tools and compiles the launcher. No Visual Studio, CMake, Python or SDK installation is
needed. Next time, open the tpw-port folder and double-click run.cmd, or run `.\run.cmd` there. You can also start
build\tpw_launcher.exe directly. To update an existing clone, run `git pull`, `git submodule update --init --recursive`
and `.\build.cmd`.

Git for Windows is the only thing installed. build.cmd fetches the rest into build\tools\, in the versions the Windows
build was tested with, each checked against its publisher's SHA-256: llvm-mingw (the compiler), CMake, Ninja and Python
(about 250 MB to download, once). `.\build.cmd clean` deletes build\ with those tools. The clone keeps the files' line
endings as they are (core.autocrlf=false). The first build takes a few minutes. The launcher is one .exe that needs
nothing beside it.

### Playing

`make run` (on Windows, `.\run.cmd` or `build\tpw_launcher.exe`) opens the launcher. Drop your game disc on its window: the Mac CD, the
PC CD, or a zip or disc image of either. The launcher checks every file and ticks off the editions it finds. Press Play:
a progress bar shows while the game starts, and the launcher's window closes when the game's window appears. Once the
US, European and Gold discs are all checked, the window shows just two tiles, Theme Park World and Sim Theme Park, each
with its own Play button. At every start the launcher compares each file's size and date with what it recorded,
rehashes only the files that changed and removes any that no longer match.

The Settings box has these rows:

- Skip the intro films: off by default.
- Back to the launcher: off by default.
- Smooth wheel zoom: off by default, which keeps the original's step zoom. On, the wheel eases the zoom toward the pointer.
- Varied sounds: on by default, as the PC release plays them: a sound changes between its recordings, volumes and
  pitches. Off is the Mac's way, where each sound plays the same recording all through a launch.
- Interface size: 100% by default, the original. 85% or 70% shrink the panels so the park shows more, with the panels
  still at the edges of the screen.
- Graphics: seven options of the original's options files (low.sam, med.sam, high.sam), each Default by default, which
  keeps the value of the game's own graphics quality: view distance (Short, Medium, Long, Max), sky layers (1, 2, 4),
  cloud shadows, ground blending (Off, 16, 32, 64), texture quality (Low, High), lighting and visitor shadows (Off, On).
- Check for updates: on by default (the one launcher setting that is; see Updates below). Off, the launcher never asks.
- Check again: rehashes every file.

### Updates

Once a day, when it starts, the launcher asks GitHub whether a newer version is out. It asks in the background and the
window never waits for it. It sends nothing about you or your game: the question is the same for everyone, and GitHub
sees only that someone asked. The first start says so in a line at the bottom of the window.

- It looks only for releases. A build of main (a version ending in -dev, from the install script's dev option) hears of
  a release newer than itself, and of the release of its own number once it is out, but never of newer commits of main.
- When there is one, a banner under the title says "Version X.Y.Z is out", with What's new (the release's page) and
  Update. Installed with the install script, Update builds the new release beside the one you run and says each step
  (a dev install too: Update always goes to the newest release); when it is ready, Restart to update closes the
  launcher and starts the new one. If anything goes wrong, nothing is changed and the banner says why, with Show logs.
  Downloaded or built by hand, Update opens the release's page.
- With no internet, or when GitHub does not answer, the launcher says nothing and asks again at the next start. Update
  is greyed out with "No internet connection" until a later start reaches GitHub.
- To turn it off: Settings, Check for updates. `tpw_launcher --check-updates` asks once and prints the answer.

The game itself never needs the internet: everything plays offline.

## What you need

- A Mac (Apple Silicon or Intel) with macOS 11 or newer, or a PC with 64-bit Windows 10 or 11 (Intel, AMD or Arm; an
  Arm PC needs Windows 11), and about 2.5 GB of free space.
- Your own copy of the game:
  - The US Mac CD gives Sim Theme Park.
  - The Mac CD plus the European PC CD give Theme Park World too. Only the 2000 pressings of the PC CD work (Redump
    66484 and 9465), and the 1999 pressing (Redump 66100) is refused.
  - The PC Gold bonus CD unlocks the Gold bonus rides in the launcher. This build does not add them to the game yet.
- No patches. The port is built from the last Mac patch (2.04b3) and reads only the game's data files.

Your checked game files and your saves live in `~/Library/Application Support/STP-TPW Launcher` on a Mac and in
`%APPDATA%\STP-TPW Launcher` on Windows. `make clean` and `build.cmd clean` never touch that folder. Each run's log goes to its logs folder
(launcher-YYYYMMDD-HHMMSS.log, the 10 most recent runs kept; the About box's Show logs button opens it with the newest log selected, and its Export logs button saves the newest to your Downloads folder), and
`tpw_launcher --version` prints the launcher's version. On Windows the launcher has no console: what it prints goes to
launcher.log in that folder too, unless its output is redirected (`build\tpw_launcher.exe 2> log.txt`).

## Asset packs

An asset pack swaps some of the game's files for your own, such as a sharper Bullfrog logo, without changing the
checked game files. A pack is a zip of files, each at its place in the game folder: `data/init/640/Splash_American.tga`
is the Bullfrog logo shown before the films. A zip made of a folder works too, as one folder around `data` is ignored.

Put packs in the `packs` folder beside `saves` and `assets`: `~/Library/Application Support/STP-TPW Launcher/packs`
on a Mac and `%APPDATA%\STP-TPW Launcher\packs` on Windows (the launcher makes it). When you press Play, the launcher
unpacks a new or changed zip into `packs/.unpacked`, and the game looks in a pack before its own files. Your saves
still come first. To stop using a pack, remove its zip. Packs are used in name order: when two hold the same file, the
later name's copy is used. `--packs DIR` names another folder, and it also works with `--game`.

A pack changes only how the game looks or sounds. It takes pictures (`.tga`, the `.wct` textures, the `.ani` and `.cur`
cursors), sounds (`.sf2`, and the sound banks: an `.sdt` and its `.map` index, in the folders the game keeps them in,
`data/global/sound` and `Speech` and each level's `Sound`, `Speech` and `Music`), fonts (`.ttf`, the `.bf4` bitmap
fonts) and films (`.mov`). Any other file, a `.sam` settings file above all, is left out: changing how a park plays is
not what packs are for. So is a file under a folder whose name holds a dot or is a game file's name
(`data/levels/fantasy/features/bfbin.wad/x.tga`), as that folder would hide the game's file. The game takes nothing else
from a pack's folder either, so a file put in `.unpacked` by hand is not used.

The launcher also leaves out a zip whose name starts with a dot, a file in a zip whose name holds a drive or a colon
(`C:/x`), and a pack that unpacks to more than 8 GB. Its log names each file it leaves out and why, apart from a Mac's
`__MACOSX`, `._` and `.DS_Store` files. The zip reader itself never lists folder entries or a name that climbs out of
the zip (`..` or a leading `/`), so those have no log line. A newer launcher that checks packs by other rules unpacks
them again.

A broken file in a pack can crash the game, as a broken game file would: a cursor `.tga` shorter than 32 rows, for
example, or a `.map` that isn't its sound bank's index.

A replacement must be in the format the game reads: the logo and the loading screens are uncompressed TGA. The game
picks the logo by the screen's width, from the `640`, `800` or `1024` folder (`640` above 1024 pixels), so a pack
replaces all three. A logo of another shape than 4:3 keeps its shape: a 16:9 logo fills a 16:9 screen.

A loading screen is `welcome.tga` with `legal.tga` or `welcome_<language>.tga` copied onto its left edge, so keep a
replacement `welcome.tga` at least as wide as those, or their right side is cut off. On a loading picture wider than
4:3 they stay at its left edge, at the rows they have on a 4:3 one, and the progress bar stays in the screen's 4:3
middle.

## Where it stands

Every function of the game is translated. Both editions play from the lobby into a park. 158 names are still generated
stand-ins. All of them belong to EA's online service, which is gone (the lobby's globe), or to the Mac Sound Manager's
channel layer. None of them is reached in play.

The lobby's online world is unavailable: the original service is gone.

## For developers

### The code

The translation is the [tpw-decomp](https://github.com/ThemeParkWorldDecomp/tpw-decomp) submodule: the game's own functions, each behaving exactly as
on the Mac, bugs included. This repository adds what it takes to play:

- launcher/: the program (the launcher's window, the options, the game's start-up) and the generated stand-ins for the
  names nothing defines yet. launcher/net/ is the only code that uses the internet (the update check; CONTRIBUTING.md,
  "Playable offline").
- platform/: the Mac Toolbox, Sound Sprockets, QuickTime and the game's Windows-style shims, done on SDL3, OpenGL and
  CoreText (on Windows, stb_truetype in place of CoreText).
- overrides/: the port's fixes, 197 so far, each one listed in [FIXES.md](FIXES.md) with what the Mac does, what
  the port does and why. Each file there is built in place of the tpw-decomp file of the same path. Headers are read
  from a copy of tpw-decomp's headers in the build folder with the overridden ones laid over it.
- mods/: the [tpw-mods](https://github.com/ThemeParkWorldDecomp/tpw-mods) submodule, for optional changes to how the game plays. It holds no mods yet
  and the launcher has no Mods screen yet; the port builds and plays the same without it.

### The Windows build

It was built and tested with llvm-mingw 20261006 (UCRT, x86-64, clang 23.1.3), CMake 4.4.4, Ninja 1.13.2, Python 3.14.7
and Git 2.55.0 on Windows 10 22H2. build.cmd hands to devtools/win/build.ps1 (Windows PowerShell 5.1, part of Windows,
run with -ExecutionPolicy Bypass), which fetches those versions of the first four (Python as the embeddable package) and
pins each with its SHA-256. It takes a tool from the PATH instead when the version there is usable: a MinGW clang for
x86-64 not older than 23.1.3, CMake 3.20 or newer, Ninja 1.10 or newer, Python 3.10 or newer. It prints which it chose.
`.\build.cmd TOOLS=fetch` fetches all four whatever the PATH has, `.\build.cmd test` builds everything and runs the
tests, `BUILD=<folder>` builds in another folder, and each step's output is kept in build\logs\. Every build checks the
launcher's DLL imports, and test builds check the test programs too: only Windows system libraries are allowed. The
record is build\logs\imports.log. CI also copies the launcher away from its build tools and starts it with only
Windows system folders on PATH. What it does differently is all in the platform layer and the build: the signs' text
is drawn with stb_truetype (platform/gdi_text_win.cpp) from the game's fonts and the Windows fonts folder; the POSIX
calls MinGW lacks are in platform/win and platform/posix_win.cpp; a division the x86 traps (by zero, INT_MIN / -1) gets
the answer the Mac's processor gives (platform/host_win.cpp); and the build makes its own copy of the one tpw-decomp file
the game library builds sliced (devtools/slice_win.py), so it needs no libclang. tpw_platform_tests does: its sources
are cut by tpw-decomp's slicer, which runs on libclang, and llvm-mingw has none. Give the build one with
-DTPW_LIBCLANG=<path>, such as bin\libclang.dll of an LLVM release (23.1.3 was used), and the slicer runs through
devtools/slicer_win.py; without it the platform tests are left out. The overrides of units win and winnewline in
[FIXES.md](FIXES.md) are the Windows build's.

### Building by hand

`make` takes BUILD=, CMAKE= and JOBS= (the Makefile's header says how). `make clean` deletes build/, with the CMake it
fetched. To run the steps yourself you need CMake 3.20 or newer, a C++17 compiler and Python 3. SDL3, zlib, libjpeg and
a trimmed FFmpeg are built from source with the port (third_party/).

    git clone --recurse-submodules https://github.com/ThemeParkWorldDecomp/tpw-port.git
    cd tpw-port
    cmake -S . -B build
    cmake --build build -j8 --target tpw_launcher

On Windows, with your own toolchain (llvm-mingw extracted to C:\llvm-mingw, CMake, Ninja and Python 3 on the PATH), the
same in PowerShell:

    git -c core.autocrlf=false clone --recurse-submodules https://github.com/ThemeParkWorldDecomp/tpw-port.git
    cd tpw-port
    $env:PATH = "C:\llvm-mingw\bin;$env:PATH"
    cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    cmake --build build --target tpw_launcher
    build\tpw_launcher.exe

-DTPW_MOVIES=OFF builds without the films. The launcher reads the game's data tables at run time from the game folder's
"SimTheme Park" program (or the one --program names), so the build holds none of them. -DTPW_TABLES_AT_CONFIGURE=ON
compiles them in at configure time instead, from -DTPW_GAME_PROGRAM=<path> (default: the CD's program in the
extracted/ folder beside tpw-port, below).

The submodule's URL is relative (../tpw-decomp), so it resolves to tpw-decomp beside tpw-port, on the same server or in
the same folder. A clone of a local folder needs `git -c protocol.file.allow=always submodule update --init`.

### Game data from the command line

The launcher's window is the easy way. devtools/import_originals.py does the same from the command line. It finds the
originals by their MD5 (any file names), unpacks them and checks every file it writes against the SHA-256 list it
carries:

| Original | MD5 |
| --- | --- |
| From the US Mac CD (SimThemePark Mac US CD v2.0.1): the zip holding the CD image "Theme Park.toast"; all the US edition needs | f03abce3f375bc72d863cc527749e645 |
| From the demo and bonus item (ThemeParkWorld Mac Demo and Bonus Stuff): the Bonus Stuff zip; optional, its fonts serve only the platform tests | 1adeba0b0cf5edcc623d44181a321f28 |
| From the UK PC CD item (ThemeParkWorld PC UK-European CD Bullfrog EA): Redump 66484, ThemeParkWorld.img; optional, for the European (Theme Park World) edition, with the Mac CD zip (devtools/import_eur.py writes extracted/tpw-eur) | cd1cc4c33c1bd10673b855edd4b9e0f2 |
| From the same item: Redump 9465, TPWORLD.bin; optional, the other European image (either one will do; the two older ISO images, df7d864394ad46860984be60f2a3e448 and 50ffee97f9abc37e751f36d7239caea4, are still accepted) | 3c6590e438b3134e9208b407cc970d33 |

It needs Python 3.8 or later. Put the originals in one folder, then from WORK, the folder holding tpw-port:

    python3 tpw-port/devtools/import_originals.py --from ORIGINALS --to extracted

It takes about half a minute and 2 GB of memory, writes nothing outside extracted/ and ends with "CHECK OK" (or the list
of the files that differ). `--check extracted` checks the folders again later. `--whole-disc` also writes the rest of
the CD (manuals, installers, extras), which nothing reads. A patch file given to it is ignored. The result:

    WORK/
      tpw-port/                        this repository (tpw-port/tpw-decomp is the submodule)
      extracted/
        disc-hfs/Theme Park Data/      each file's data fork, with NAME.rsrc (its resource fork) and NAME.finfo
        tpw-eur/Theme Park Data/       the European edition's game folder, when the PC image is there
        Theme_Park_World_Bonus_Stuff/  the bonus fonts, when that zip is there

From WORK, `tpw-port/build/tpw_launcher --game "extracted/disc-hfs/Theme Park Data"` plays from there.
`tpw_launcher --play us` (or eur) starts an edition from the launcher's data folder without its window.
`tpw_launcher --help` lists every option.

After Play the launcher's window stays open with a progress bar until the game's window appears, then it closes. While
the window is open a thread reads ahead the game's tables and the first files of the edition last played, so that Play
finds them in the system's file cache; `--no-preload` turns that off. The bar counts bytes: what the game read before
its window appeared the last time is kept in the data folder's launcher.ini (startup_bytes_us, startup_bytes_eur), and
the first start of an edition shows a moving bar without a number.

### Running from the command line

    tpw-port/devtools/run_launcher.sh tpw-port/build NAME SECONDS [options]

run from WORK, starts tpw-port/build/tpw_launcher with its own user folder (tpw-port/build/user-NAME) and log
(tpw-port/build/launch-NAME.log) and quits it after SECONDS. Options: --mute, --no-movies, --windowed, --display builtin,
--background, --launcher-window, --play us|eur and --data DIR (start from the launcher's data folder instead of
extracted/), and the scripted input used to test it (--click X,Y@PASS, --type TEXT@PASS, --drag X0,Y0-X1,Y1@PASS,
--frames N, --window-click X,Y@FRAME for the launcher's own window (in its points), and --fullscreen: the game's real
full-screen window on the --display screen, without --windowed or --background; that screen is taken over until the
run ends. tpw_platform_tests (cmake --build build --target tpw_platform_tests) checks the platform layer without a
display.

### Widescreen and resolution

The port runs as a real full-screen window, a Space of its own with the menu bar and the dock hidden (Cmd+Ctrl+F
leaves and re-enters it; --windowed opens a window with a title bar; --background keeps a window fitted to the screen
behind the others). The game is held to 60 frames a second: the swap waits for as many of the display's refreshes (vsync)
as come nearest a 60th of a second, so a 120 Hz or 144 Hz screen waits for 2 and the animations run at the speed they
were made for. The window takes the screen's own shape, 4:3, 16:10 or 16:9, so no bars show on any of them (16:10 at
3840 x 2400 on a MacBook, 16:9 at 3840 x 2160); --aspect still forces one. The game renders offscreen at that size and
the window shows it scaled to fit the screen, so a small window still gets a sharp picture (3D edges and the TrueType text; the game's own textures are 256 x
256 at most and do not gain detail). The game's bitmap fonts have one size for every resolution from 1024 x 768 up, so
the panels' small print is smaller on the screen at the larger sizes (at 3840 x 2160 about 0.7 times its size at 2730 x
1536); --resolution 2048x1536 keeps the size the original gives at its largest preset. The park and the lobby are drawn
out to the new edges with the same vertical field of view, so the sides show more of the park and nothing is cut off;
the panels on the park view are anchored to the edges of the screen (the money and the control panel left, the advisor
right, dialogs in the middle); the 4:3 art (the logo, the loading screens, the films and the full-screen pictures) keeps
its shape in the middle of the screen with black bars at the sides. The window fits the screen at that aspect, so any
4:3, 16:10 or 16:9 screen shows no bars round the game. A machine whose framebuffers or textures are smaller than the size asked for
gets the largest preset that fits, and the log says so once (TPW_LARGEST_GAME_SIZE=3000 in the environment tries that
on a machine that has room).

    tpw_launcher --resolution 2048x1536   a smaller size, named by its 4:3 size (the game's own presets from 512x384 to
                                          2048x1536, which keep their height when widened: 2730 x 1536 at 16:9; or
                                          the port's 2560x1920, 3200x2400 and 4096x3072, which keep the width of their
                                          16:9 size: 2560 x 1440, 3200 x 1800, 3840 x 2160)
    tpw_launcher --aspect 16:10           another shape (anything from 4:3 wider: 3840 x 2400 at the default size);
                                          without it the screen's own shape is used
    tpw_launcher --aspect 4:3             the original layout, exactly as the Mac draws it (4096 x 3072 by default)

devtools/run_launcher.sh passes --aspect 4:3 and --resolution 2048x1536 unless its options name them, because the
scripted clicks of the test recipes are 4:3 game pixels at 2048 x 1536; a widescreen test run passes --aspect 16:9 and
moves its clicks on right-anchored and centred panels (the script's header has the widescreen recipes for the easy park).
The recipes' pass numbers are timed without the 60 frames a second pacing, so the script sets TPW_VSYNC_EVERY_REFRESH=1
(a test-only variable that swaps at every refresh as before the pacing); --paced leaves it unset to test the pacing itself.

## Reporting a problem

Open an issue (the form asks for each of these) or post on Discord, and say:

1. **The launcher version**, for example 0.0.3. The window title, the About box and the log's first line show it, and
   `tpw_launcher --version` prints it. A report without it cannot be checked, since each version is different code. A
   version ending in -dev (0.0.4-dev) is a build of main, not a release: give the build too, which the log's first line
   names (`build: ...`).
2. Your system (macOS or Windows and its version, Apple Silicon, Intel or AMD).
3. Your edition: US, European or Gold.
4. The launcher's newest log, attached. Open the launcher's About box, click **Export logs**, then attach the file it
   saves in your Downloads folder ("STP-TPW Launcher log" and the date). The log's own folder is
   `~/Library/Application Support/STP-TPW Launcher/logs` on a Mac and `%APPDATA%\STP-TPW Launcher\logs` on Windows
   (the About box's Show logs button opens it).
5. What you clicked before it went wrong, and whether it happens again when you do the same.

Before reporting, check that you run the newest release: a fix comes in the next version, never in a release already
out.

## Contributing

The project is three repositories, and every change belongs in exactly one of them:

| Repository | What it holds | Pull requests |
| --- | --- | --- |
| [tpw-decomp](https://github.com/ThemeParkWorldDecomp/tpw-decomp) | The translation: the Mac program's own functions in C++, each behaving exactly as the Mac build does, bugs included, with the differential tests that check it. | None. A function that behaves differently from the Mac is reported as an issue on tpw-port. |
| [tpw-port](https://github.com/ThemeParkWorldDecomp/tpw-port) | The original game on today's Macs and Windows PCs: the launcher, the platform layer, the builds, and the fixes that make it run as it did on the Mac. | Welcome, by its CONTRIBUTING.md. |
| [tpw-mods](https://github.com/ThemeParkWorldDecomp/tpw-mods) | Optional changes to how the game plays, each one off until the player turns it on. tpw-port carries it as a submodule at mods/. | Welcome, by its CONTRIBUTING.md. |

The test for where a change goes: does a park play the same with it on and with it off? If it does, the change belongs in
tpw-port. If it changes the rules, the money, the visitors, the staff, the rides, the advisor, the game's content, what a
control does or the save files, it belongs in tpw-mods, even as a setting that is off by default.

One exception: an option the original game already had in its .sam data files belongs in tpw-port, even when it
changes how a park plays, as long as it offers only the values the original accepts and its default is the original's.
Players set it in the launcher or a game menu, never by editing a .sam file.

Pull requests are welcome here for anything that passes that test: fixes, other systems (Linux, for example), build
changes, tools. [CONTRIBUTING.md](CONTRIBUTING.md) has the rules, including the three a setting must meet. An accepted
pull request is merged here and also made in the private working tree both tpw-decomp and tpw-port are exported from,
so every later export keeps it.

Contributors: samueleagle91 (spamaerie on Discord) made the Windows build (pull request #1). slappyhead (on Discord)
was the first to get the port running on an Intel Mac, which showed what the Intel build was missing.

Every push and pull request is built and tested by GitHub Actions (.github/workflows/build.yml): on Windows Server 2022
and 2025 (the nearest GitHub has to the Windows 10 and 11 kernels; 2025 is the build of Windows 11 24H2), on Windows 11
on Arm (under its x64 emulation) and on macOS (Apple Silicon and Intel). Each Windows build also starts the launcher
once (--version), and each run keeps the launchers it built as artifacts.

Pull requests go to main, which is always the next version (launcher/VERSION.txt says it with -dev, 0.0.4-dev for
example). A release is cut from main: its version without -dev, then a tag (v0.0.4) on that commit, from which
.github/workflows/release.yml builds, tests and publishes the Windows and Mac downloads together. A release is never
changed or replaced afterwards, so a fix accepted after it ships in the next version.

## See also

- [tpw-decomp](https://github.com/ThemeParkWorldDecomp/tpw-decomp): the translation and its differential tests.
- [tpw-mods](https://github.com/ThemeParkWorldDecomp/tpw-mods): optional changes to how the game plays.
- [CHANGELOG.md](CHANGELOG.md): what each version adds and fixes, in plain words.
- [FIXES.md](FIXES.md): every place the port behaves differently from the Mac.

No license has been chosen yet for the port's own code. third_party/ffmpeg is FFmpeg 9.0.2 (trimmed), under the GNU LGPL
version 2.1 or later (third_party/ffmpeg/COPYING.LGPLv2.1); it is built from that source with the game, so you can change
it and rebuild. Details in third_party/ffmpeg/NOTE.md.

The port also builds SDL3 3.2.30 and zlib 1.3.2 (zlib License) and the IJG's libjpeg 6b (IJG License). The launcher's
text uses Fredoka and Nunito (SIL Open Font License 1.1, third_party/fonts) and is drawn with stb_truetype and
stb_rect_pack (MIT or public domain, third_party/stb). The launcher's About box lists the versions it was built with.
