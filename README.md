# tpw-port

An unofficial port of Sim Theme Park (AKA Theme Park World, 1999) for Macs (Apple Silicon and Intel) and 64-bit Windows.
You need your own copy of the game. Nothing from the game is in here.

Not affiliated with or endorsed by Electronic Arts. Sim Theme Park and Theme Park World are trademarks of their
respective owners.

## Quick start

### macOS

On a Mac with Apple Silicon or an Intel processor, in Terminal:

    git clone --recurse-submodules https://github.com/ThemeParkWorldDecomp/tpw-port.git
    cd tpw-port && make
    make run

If macOS asks to install the command line developer tools, click Install, wait, then run the first line again. The first
build takes a few minutes. If your Mac has no CMake, `make` fetches it into the build
folder. Nothing else is installed.

### Windows

On a PC with 64-bit Windows 10 (version 1803 or newer) or Windows 11, Intel, AMD or Arm (an Arm PC needs Windows 11),
install Git once if you do not have it:

    winget install Git.Git

After installing Git, open a new PowerShell or Command Prompt window. Paste these lines:

    git -c core.autocrlf=false clone --recurse-submodules https://github.com/ThemeParkWorldDecomp/tpw-port.git
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
- Check again: rehashes every file.

## What you need

- A Mac (Apple Silicon or Intel), or a PC with 64-bit Windows 10 or 11 (Intel, AMD or Arm; an Arm PC needs Windows 11),
  and about 2.5 GB of free space.
- Your own copy of the game:
  - The US Mac CD gives Sim Theme Park.
  - The Mac CD plus the European PC CD give Theme Park World too. Only the 2000 pressings of the PC CD work (Redump
    66484 and 9465), and the 1999 pressing (Redump 66100) is refused.
  - The PC Gold bonus CD unlocks the Gold bonus rides in the launcher. This build does not add them to the game yet.
- No patches. The port is built from the last Mac patch (2.04b3) and reads only the game's data files.

Your checked game files and your saves live in `~/Library/Application Support/STP-TPW Launcher` on a Mac and in
`%APPDATA%\STP-TPW Launcher` on Windows. `make clean` and `build.cmd clean` never touch that folder. Each run's log goes to its logs folder
(launcher-YYYYMMDD-HHMMSS.log, the 10 most recent runs kept; the About box's Show logs button opens it), and
`tpw_launcher --version` prints the launcher's version. On Windows the launcher has no console: what it prints goes to
launcher.log in that folder too, unless its output is redirected (`build\tpw_launcher.exe 2> log.txt`).

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
  names nothing defines yet.
- platform/: the Mac Toolbox, Sound Sprockets, QuickTime and the game's Windows-style shims, done on SDL3, OpenGL and
  CoreText (on Windows, stb_truetype in place of CoreText).
- overrides/: the port's fixes, 125 so far, each one listed in [FIXES.md](FIXES.md) with what the Mac does, what
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

Issues are welcome too. A useful bug report says which edition you play (US, European or Gold), attaches the launcher's newest
log (`~/Library/Application Support/STP-TPW Launcher/logs` on a Mac, `%APPDATA%\STP-TPW Launcher\logs` on Windows; its
first lines name the launcher's version, the system's version and the machine) and says what you clicked before it went
wrong.

## See also

- [tpw-decomp](https://github.com/ThemeParkWorldDecomp/tpw-decomp): the translation and its differential tests.
- [tpw-mods](https://github.com/ThemeParkWorldDecomp/tpw-mods): optional changes to how the game plays.
- [FIXES.md](FIXES.md): every place the port behaves differently from the Mac.

No license has been chosen yet for the port's own code. third_party/ffmpeg is FFmpeg 9.0.2 (trimmed), under the GNU LGPL
version 2.1 or later (third_party/ffmpeg/COPYING.LGPLv2.1); it is built from that source with the game, so you can change
it and rebuild. Details in third_party/ffmpeg/NOTE.md.

The port also builds SDL3 3.2.30 and zlib 1.3.2 (zlib License) and the IJG's libjpeg 6b (IJG License). The launcher's
text uses Fredoka and Nunito (SIL Open Font License 1.1, third_party/fonts) and is drawn with stb_truetype and
stb_rect_pack (MIT or public domain, third_party/stb). The launcher's About box lists the versions it was built with.
