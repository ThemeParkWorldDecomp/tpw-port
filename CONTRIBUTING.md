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

## How a change lands

Both tpw-decomp and tpw-port are exported from one private working tree. An accepted pull request is merged here and is
also made in that tree (a change to a game file as an `#ifdef TPW_PORT_FIX` block, which the export turns back into the
file under overrides/), so every later export keeps it. The contributor is credited in the commit message and in
FIXES.md. A pull request that belongs in another repository is closed with a pointer to it.

## Issues

Issues are welcome. A useful bug report says which edition you play (US, European or Gold), attaches the launcher's
newest log (`~/Library/Application Support/STP-TPW Launcher/logs` on a Mac, `%APPDATA%\STP-TPW Launcher\logs` on Windows)
and says what you clicked before it went wrong.
