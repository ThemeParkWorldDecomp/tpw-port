# Contributing

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

## What belongs here

- Running the game: other systems (Linux, for example), builds, the launcher, tools.
- Fixes that make the game run as it did on the Mac: a crash, something the Mac draws that the port did not, a file the
  port could not read.
- A setting, only when all three hold:
  1. It is off by default, and off is exactly the original.
  2. It changes only how the game is shown (display, resolution, interface size), the input devices, the sound output,
     files or the launcher. Never the rules, the money, the visitors, the staff, the rides, the advisor, the game's
     content, what a control does or the save format.
  3. The launcher's Settings box sets it, and the game's code reads it from a flag the launcher sets: with the flag off,
     the game runs the original code.
- An option the original game already had in its .sam data files, even one that changes how a park plays, when all
  three hold:
  1. It offers only the values the original accepts for it (NUMKIDS: 0 to 3), and its default is the original's.
  2. Players set it in the launcher's Settings box or in a game menu drawn like the game's own, never by editing a
     .sam file.
  3. Its FIXES.md row names the .sam file and key it comes from.

  A value fixed in the game's code is not an option: changing the 1,500 visitor limit, for example, is a mod.

## How to make a change

- The tpw-decomp and mods/ submodules are never touched. Their pointers stay where the maintainers set them.
- A changed game source file is a whole-file copy under overrides/ at the same path, with a row in FIXES.md: what the Mac
  does, what the port does and why.
- Host code for another system goes under platform/ and launcher/, behind that system's macro.
- Test aids stay out of a pull request.
- One concern per pull request, and one per commit.
- Say in the pull request what you tested and on what: the edition and the system.
- Naming order: Sim Theme Park (the US edition) is always named before Theme Park World (the European one) wherever
  the two appear together: screens, options, texts, logs, docs. The launcher's tiles follow it, US on the left.

## Before a pull request

The game's files are the retail discs' (the US Mac CD, the European PC CD and the Gold bonus CD), and you need your own
copy of them in the launcher's data folder to test a change. This repository never holds them, a link to them, or the
place a copy came from, and a pull request never shows where yours are.

CI also refuses a pull request that adds a link to the game's files: a download or item link of an archive site, a web
address ending in a disc image, a magnet or torrent link, a file host's share link, or a file named like a disc image.
It refuses what names a particular download too: an MD5, a long hash beside the word disc, image, zip, download or item, a
Redump id, the name of a game-download site, an item's title, or the address of the archive site itself
(`devtools/check_no_game_data.py` lists the patterns). Say "the discs" and "the game's files", and name no source.

## Playable offline

The game is played offline, and the port keeps it that way:

- Nothing needs the internet to start, play, save or load. The launcher, the game and their tests run the same with no
  connection.
- What does use the internet is optional and fails silently. Today that is the launcher's update check (README,
  "Updates"): with no answer it says nothing and asks again at the next start. The Update button's script downloads
  only when the player presses it.
- All network code is in one place, launcher/net/. It asks with the system's own curl, a short time limit and a cap on
  what it reads, and anything but an answer in the exact shape expected is no answer.

CI checks this on every push and pull request (.github/workflows/build.yml):

- `python3 -I devtools/check_offline.py` reads every C, C++ and Objective-C file and fails on a socket or HTTP header, a
  network call, a web address in a string, or curl or wget started as a program, anywhere outside launcher/net/. Run it
  before a pull request; it takes a few seconds. The original game's own online layer (the TPW Online world, its e-mail
  and FTP client, and platform/winsock.cpp under them) is listed there by file and kept as the original had it: the port
  never offers the online world, and its servers are gone. A vendored library's file is listed only with the reason it
  never talks to the network (FFmpeg is built without its network code).
- On the Macs, the tests run a second time with the network blocked (sandbox-exec), and `tpw_launcher --check-updates`
  must come back at once with no answer and exit 0. There is no Linux build here, and Windows has no simple way to block
  one program's network, so the Windows jobs do not repeat the tests offline.

## The game's own process

The launcher and the game are two processes, and everywhere the system shows running programs (the dock or taskbar,
the app switcher, the task manager) the one running is named and drawn as itself: the launcher as "STP/TPW Launcher"
with the launcher's icon, the game the launcher started as "Sim Theme Park" or "Theme Park World" with the game's icon
(the icon.png of the player's imported disc, or the stand-in until then). Code for another system does the same:

- The launcher window's Play starts the game as a process of its own, from a program named after the game beside the
  launcher's, with the launcher's arguments and `--play us|eur --from-launcher DIR` (launcher/game_app.h). The launcher
  keeps its Starting screen up until the game's window is shown, then exits. Only when the game's program cannot be
  made or started does the game play in the launcher's process, and the log says why.
- The game's window has the game's name as its title and the game's icon.
- When the game quits with "Back to the launcher after playing" on, the launcher starts again as a process of its own.
- The game's program started by itself (pinned, double-clicked) starts the launcher instead.
- The log says what was done: the launcher's `play: ... handed to ...` and `game app: ...` lines, and the game's own
  lines on how it named itself.
- `tpw_launcher --game-app-test us|eur` runs this hand-over with a bare window in place of the game, for CI, which has no
  discs.

How each system does it:

- macOS: "Sim Theme Park.app" or "Theme Park World.app" beside "STP:TPW Launcher.app" (which the Finder shows as
  STP/TPW Launcher), made by the launcher with the game's name, an identifier of its own, the game's icon and the
  launcher's program hard-linked inside, and opened through NSWorkspace. The Dock, Cmd-Tab, Force Quit and Activity
  Monitor name it.
- Windows: "Sim Theme Park.exe" or "Theme Park World.exe" beside tpw_launcher.exe, a copy of tpw_launcher.exe the
  launcher writes when Play is pressed, whose version resource (FileDescription, ProductName, OriginalFilename) names the
  game and whose icon is the game's. It is not a hard link: the Task Manager names a process by its program's version
  resource, which a hard link shares with tpw_launcher.exe, and the build cannot hold the game's icon, which comes from
  the player's disc. It is written again only when tpw_launcher.exe or the icon changes. The game's process sets an
  AppUserModelID of its own (org.tpw-port.launcher.stp or .tpw) and puts it on its window, so the taskbar gives the game
  a button of its own with its name and icon; pinned, that button starts the game's program, which starts the launcher.
  The launcher keeps the ID Windows gives it from tpw_launcher.exe's path, the same as the Start menu shortcut
  install.ps1 makes. The Task Manager's Processes tab shows "Sim Theme Park" and its Details tab "Sim Theme Park.exe".
  Started from Explorer, the game's process logs to game.log beside launcher.log.
- Linux (no build yet): the process named after the game (a program file named after it, as on Windows, so ps and the
  system monitor show it); the window's WM_CLASS, and on Wayland its app_id, the game's own (SDL_HINT_APP_ID, set before
  SDL's video starts), matching the StartupWMClass of a .desktop file the install script writes for the game (on Wayland
  the .desktop file's name is the app_id), with the game's name and icon, so the dock and the app switcher keep it apart
  from the launcher; _NET_WM_NAME the game's name (SDL's window title) and _NET_WM_ICON the game's icon
  (SDL_SetWindowIcon).

## Versions and releases

- Pull requests target main. Main is always the next version: launcher/VERSION.txt says it with -dev (0.0.4-dev).
- A release is a version without -dev, its tag (v0.0.4) on one commit of main, and the GitHub Release that
  .github/workflows/release.yml builds from that tag: Windows and both Macs from the same code.
- A release never changes. Its tag is never moved or deleted, its downloads are never replaced, and a version number is
  never used twice. A fix found after a release, even one for a problem in it, lands on main and ships in the next
  version.
- Only the maintainers change launcher/VERSION.txt and cut releases. A pull request leaves it as it is.
- Players get stable. The stable branch is the newest release: release.yml moves it forward to each new tag, and nothing
  else changes it. The one-line installers (install.sh and install.ps1, attached to every release) build the newest
  release too. Pull requests target main, never stable.
- To work on main or test it, install it with the installer's `--dev` (`-Dev` on Windows), which builds main and updates
  to the newest main, or clone main by hand: `git clone --recurse-submodules https://github.com/ThemeParkWorldDecomp/tpw-port.git`, then the README's
  building steps. A change to an installer is tested on both systems, and keeps its contract (the comment at the top of
  each script): the launcher's updater reads its output.

## The changelog

CHANGELOG.md tells players what changed in each version, and each release's page shows its section. Every change a
player can notice gets a line there, written when the change lands, not at release time. A pull request gives its line
under "Changelog line" in the template, and the maintainers add it.

- Write for players, not programmers. Say what they will notice: "Fixed a crash when renaming a ride." Not why it
  happened or how it was fixed: that goes in the commit message and FIXES.md.
- One short, plain sentence. No file, function or code names and no technical words (memory, thread, pointer, heap,
  lock, offset).
- It goes under the top heading, the next version ("not released yet"): in Bug Fixes or New Features, under Game or
  Launcher, or in Changed.
- Thank the people we know reported or made it, in brackets at the end: GitHub users as @name, anyone else by name and
  where, such as (Thanks Fladfisk on Discord). Never put @ before a name that is not a GitHub account: GitHub would tag a
  stranger.
- A change players cannot notice (tests, build scripts, CI, tidying code) gets no line.
- When a version is released, "not released yet" becomes the release date, and the next landing adds the next
  version's heading. The export refuses a top heading that does not match launcher/VERSION.txt, and a release refuses
  to publish without its dated section.

## How a change lands

Both tpw-decomp and tpw-port are exported from one private working tree. An accepted pull request is merged here and is
also made in that tree (a change to a game file as an `#ifdef TPW_PORT_FIX` block, which the export turns back into the
file under overrides/), so every later export keeps it. The contributor is credited in the commit message and in
FIXES.md. A pull request that belongs in another repository is closed with a pointer to it.

## Issues

Issues are welcome. Every bug report names the launcher version it happened on (0.1.0, for example: the window title,
the About box and the log's first line show it), since each version is different code; a report on a -dev build of main
also gives the build named in the log's first line. It says which edition you play (US, European or Gold), attaches the launcher's
newest log (open the launcher's About box, click Export logs, then attach the file it saves in your Downloads folder;
the logs folder itself is `~/Library/Application Support/STP-TPW Launcher/logs` on a Mac and
`%APPDATA%\STP-TPW Launcher\logs` on Windows) and says what you clicked before it went wrong.
