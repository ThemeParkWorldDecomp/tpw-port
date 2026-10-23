# tpw-port

An unofficial port of Sim Theme Park (AKA Theme Park World, 1999) for Apple Silicon Macs and 64-bit Windows. You need your
own copy of the game. Nothing from the game is in here.

Not affiliated with or endorsed by Electronic Arts. Sim Theme Park and Theme Park World are trademarks of their
respective owners.

## Quick start

### macOS

On a Mac with Apple Silicon, in Terminal:

    git clone --recurse-submodules https://github.com/ThemeParkWorldDecomp/tpw-port.git
    cd tpw-port && make
    make run

If macOS asks to install the command line developer tools, click Install, wait, then run the first line again. The first
build takes a few minutes. If your Mac has no CMake, `make` fetches it into the build
folder. Nothing else is installed.

### Windows

On 64-bit Windows 10 or 11, in PowerShell, the build tools:

    winget install Git.Git Kitware.CMake Ninja-build.Ninja Python.Python.3.14

and llvm-mingw: download llvm-mingw-20261006-ucrt-x86_64.zip from
https://github.com/mstorsjo/llvm-mingw/releases/tag/20261006 and extract it, for example to C:\llvm-mingw. Then, in a
new PowerShell window (so that winget's tools are on the PATH):

    git -c core.autocrlf=false clone --recurse-submodules https://github.com/ThemeParkWorldDecomp/tpw-port.git
    cd tpw-port
    $env:PATH = "C:\llvm-mingw\bin;$env:PATH"
    cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    cmake --build build --target tpw_launcher
    build\tpw_launcher.exe

The clone keeps the files' line endings as they are (core.autocrlf=false). The first build takes a few minutes. The
launcher is one .exe that needs nothing beside it.

### Playing

`make run` (on Windows, `build\tpw_launcher.exe`) opens the launcher. Drop your game disc on its window: the Mac CD, the
PC CD, or a zip or disc image of either. The launcher checks every file and ticks off the editions it finds. Press Play:
a progress bar shows while the game starts, and the launcher's window closes when the game's window appears. Once the
US, European and Gold discs are all checked, the window shows just two tiles, Theme Park World and Sim Theme Park, each
with its own Play button. At every start the launcher compares each file's size and date with what it recorded,
rehashes only the files that changed and removes any that no longer match. Check again in Settings rehashes every file.

## What you need

- A Mac with Apple Silicon (M1 or later), or a PC with 64-bit Windows 10 or 11, and about 2.5 GB of free space.
- Your own copy of the game:
  - The US Mac CD gives Sim Theme Park.
  - The Mac CD plus the European PC CD give Theme Park World too. Only the 2000 pressings of the PC CD work (Redump
    66484 and 9465), and the 1999 pressing (Redump 66100) is refused.
  - The PC Gold bonus CD unlocks the Gold bonus rides in the launcher. This build does not add them to the game yet.
- No patches. The port is built from the last Mac patch (2.04b3) and reads only the game's data files.

Your checked game files and your saves live in `~/Library/Application Support/STP-TPW Launcher` on a Mac and in
`%APPDATA%\STP-TPW Launcher` on Windows. `make clean` never touches that folder. On Windows the launcher has no
console: its log goes to launcher.log in that folder, unless its output is redirected
(`build\tpw_launcher.exe 2> log.txt`).

## Where it stands

Every function of the game is translated. Both editions play from the lobby into a park. 158 names are still generated
stand-ins. All of them belong to EA's online service, which is gone (the lobby's globe), or to the Mac Sound Manager's
channel layer. None of them is reached in play.

Known problem: clicking the globe in the lobby crashes the game.

## For developers

### The code

The translation is the [tpw-decomp](https://github.com/ThemeParkWorldDecomp/tpw-decomp) submodule: the game's own functions, each behaving exactly as
on the Mac, bugs included. This repository adds what it takes to play:

- launcher/: the program (the launcher's window, the options, the game's start-up) and the generated stand-ins for the
  names nothing defines yet.
- platform/: the Mac Toolbox, Sound Sprockets, QuickTime and the game's Windows-style shims, done on SDL3, OpenGL and
  CoreText.
- overrides/: the port's fixes, 60 so far, each one listed in [FIXES.md](FIXES.md) with what the Mac does, what
  the port does and why. Each file there is built in place of the tpw-decomp file of the same path. Headers are read
  from a copy of tpw-decomp's headers in the build folder with the overridden ones laid over it.

### The Windows build

It was built and tested with llvm-mingw 20261006 (UCRT, x86-64, clang 23.1.3), CMake 4.4.4, Ninja 1.13.2, Python 3.14.7
and Git 2.55.0 on Windows 10 22H2. What it does differently is all in the platform layer and the build: the signs' text
is drawn with stb_truetype (platform/gdi_text_win.cpp) from the game's fonts and the Windows fonts folder; the POSIX
calls MinGW lacks are in platform/win and platform/posix_win.cpp; a division the x86 traps (by zero, INT_MIN / -1) gets
the answer the Mac's processor gives (platform/host_win.cpp); and the build makes its own copy of the one tpw-decomp file
the game library builds sliced (devtools/slice_win.py), so it needs no libclang. tpw_platform_tests is not built on
Windows: its sources are cut by tpw-decomp's slicer, which does need libclang. The overrides of unit win in
[FIXES.md](FIXES.md) are the Windows build's.

### Building by hand

`make` takes BUILD=, CMAKE= and JOBS= (the Makefile's header says how). `make clean` deletes build/, with the CMake it
fetched. To run the steps yourself you need CMake 3.20 or newer, a C++17 compiler and Python 3. SDL3, zlib, libjpeg and
a trimmed FFmpeg are built from source with the port (third_party/).

    git clone --recurse-submodules https://github.com/ThemeParkWorldDecomp/tpw-port.git
    cd tpw-port
    cmake -S . -B build
    cmake --build build -j8 --target tpw_launcher

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
behind the others), and waits for the display's refresh (vsync). It is widescreen by default: 16:9 at 3840 x 2160. The game renders offscreen at that size and the window shows it scaled to fit the
screen, so a small window still gets a sharp picture (3D edges and the TrueType text; the game's own textures are 256 x
256 at most and do not gain detail). The game's bitmap fonts have one size for every resolution from 1024 x 768 up, so
the panels' small print is smaller on the screen at the larger sizes (at 3840 x 2160 about 0.7 times its size at 2730 x
1536); --resolution 2048x1536 keeps the size the original gives at its largest preset. The park and the lobby are drawn
out to the new edges with the same vertical field of view, so the sides show more of the park and nothing is cut off;
the panels on the park view are anchored to the edges of the screen (the money and the control panel left, the advisor
right, dialogs in the middle); the 4:3 art (the logo, the loading screens, the films and the full-screen pictures) keeps
its shape in the middle of the screen with black bars at the sides. The window fits the screen at that aspect, so a
16:9 screen shows no bars round the game. A machine whose framebuffers or textures are smaller than the size asked for
gets the largest preset that fits, and the log says so once (TPW_LARGEST_GAME_SIZE=3000 in the environment tries that
on a machine that has room).

    tpw_launcher --resolution 2048x1536   a smaller size, named by its 4:3 size (the game's own presets from 512x384 to
                                          2048x1536, which keep their height when widened: 2730 x 1536 at 16:9; or
                                          the port's 2560x1920, 3200x2400 and 4096x3072, which keep the width of their
                                          16:9 size: 2560 x 1440, 3200 x 1800, 3840 x 2160)
    tpw_launcher --aspect 16:10           another shape (anything from 4:3 wider: 3840 x 2400 at the default size)
    tpw_launcher --aspect 4:3             the original layout, exactly as the Mac draws it (4096 x 3072 by default)

devtools/run_launcher.sh passes --aspect 4:3 and --resolution 2048x1536 unless its options name them, because the
scripted clicks of the test recipes are 4:3 game pixels at 2048 x 1536; a widescreen test run passes --aspect 16:9 and
moves its clicks on right-anchored and centred panels (the script's header has the widescreen recipes for the easy park).

## See also

- [tpw-decomp](https://github.com/ThemeParkWorldDecomp/tpw-decomp): the translation and its differential tests.
- [FIXES.md](FIXES.md): every place the port behaves differently from the Mac.

No license has been chosen yet for the port's own code. third_party/ffmpeg is FFmpeg 9.0.2 (trimmed), under the GNU LGPL
version 2.1 or later (third_party/ffmpeg/COPYING.LGPLv2.1); it is built from that source with the game, so you can change
it and rebuild. Details in third_party/ffmpeg/NOTE.md.

The port also builds SDL3 3.2.30 and zlib 1.3.2 (zlib License) and the IJG's libjpeg 6b (IJG License). The launcher's
text uses Fredoka and Nunito (SIL Open Font License 1.1, third_party/fonts) and is drawn with stb_truetype and
stb_rect_pack (MIT or public domain, third_party/stb). The launcher's About box lists the versions it was built with.
