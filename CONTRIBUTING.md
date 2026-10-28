# Contributing

The project is three repositories, and every change belongs in exactly one of them:

| Repository | What it holds | Pull requests |
| --- | --- | --- |
| [tpw-decomp](https://github.com/ThemeParkWorldDecomp/tpw-decomp) | The translation: the Mac program's own functions in C++, each behaving exactly as the Mac build does, bugs included, with the differential tests that check it. | None. A function that behaves differently from the Mac is reported as an issue on tpw-port. |
| [tpw-port](https://github.com/ThemeParkWorldDecomp/tpw-port) | The original game on today's Macs, Windows PCs and Linux machines: the launcher, the platform layer, the builds, and the fixes that make it run as it did on the Mac. | Welcome, by its CONTRIBUTING.md. |
| [tpw-mods](https://github.com/ThemeParkWorldDecomp/tpw-mods) | Optional changes to how the game plays, each one off until the player turns it on. tpw-port carries it as a submodule at mods/. | Welcome, by its CONTRIBUTING.md. |

The test for where a change goes: does a park play the same with it on and with it off? If it does, the change belongs in
tpw-port. If it changes the rules, the money, the visitors, the staff, the rides, the advisor, the game's content, what a
control does or the save files, it belongs in tpw-mods, even as a setting that is off by default.

One exception: an option the original game already had in its .sam data files belongs in tpw-port, even when it
changes how a park plays, as long as it offers only the values the original accepts and its default is the original's.
Players set it in the launcher or a game menu, never by editing a .sam file.

## What belongs here

- Running the game: other systems (the BSDs, for example), builds, the launcher, tools.
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
- On the Macs, the tests run a second time with the network blocked (sandbox-exec), and on Linux too (unshare -n), and
  `tpw_launcher --check-updates` must come back at once with no answer and exit 0. Windows has no simple way to block
  one program's network, so the Windows jobs do not repeat the tests offline.

## Versions and releases

- Pull requests target main. Main is always the next version: launcher/VERSION.txt says it with -dev (0.0.4-dev).
- A release is a version without -dev, its tag (v0.0.4) on one commit of main, and the GitHub Release that
  .github/workflows/release.yml builds from that tag: Windows, both Macs and Linux (the Flatpak bundle) from the same
  code.
- A release never changes. Its tag is never moved or deleted, its downloads are never replaced, and a version number is
  never used twice. A fix found after a release, even one for a problem in it, lands on main and ships in the next
  version.
- Only the maintainers change launcher/VERSION.txt and cut releases. A pull request leaves it as it is.
- Players get stable. The stable branch is the newest release: release.yml moves it forward to each new tag, and nothing
  else changes it. The one-line installers (install.sh and install.ps1, attached to every release) build the newest
  release too. Pull requests target main, never stable.
- To work on main or test it, install it with the installer's `--dev` (`-Dev` on Windows), which builds main and updates
  to the newest main, or clone main by hand: `git clone --recurse-submodules https://github.com/ThemeParkWorldDecomp/tpw-port.git`, then the README's
  building steps. A change to an installer is tested on every system it runs on (install.sh on Macs and on Linux,
  install.ps1 on Windows; .github/workflows/installer.yml and installer-broad.yml run them on each), and keeps its
  contract (the comment at the top of each script): the launcher's updater reads its output.

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
the logs folder itself is `~/Library/Application Support/STP-TPW Launcher/logs` on a Mac,
`%APPDATA%\STP-TPW Launcher\logs` on Windows and `~/.local/share/STP-TPW Launcher/logs` on Linux) and says what you
clicked before it went wrong.
