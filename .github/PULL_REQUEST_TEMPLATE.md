## What and why

<!-- One concern per pull request, and one per commit. A change to how a park plays belongs in tpw-mods, not here, unless it is an option the original game had in its .sam files: see CONTRIBUTING.md. -->

## Changelog line

<!-- One plain sentence for players about what they will notice, such as "Fixed a crash when renaming a ride." No technical words. Thank whoever reported it (@name for GitHub users). Write "none" if players will not notice the change. See CONTRIBUTING.md, The changelog. -->

## Checklist

- [ ] A park plays the same with this change on and with it off (rules, money, visitors, staff, rides, the advisor, content, what a control does and the save format are unchanged), or it is an original .sam option (see CONTRIBUTING.md).
- [ ] An option from the original's .sam files offers only the values the original accepts, defaults to the original's, and is set in the launcher or a game menu.
- [ ] Any other new setting is off by default, off is exactly the original, and it changes only display, input devices, sound output, files or the launcher.
- [ ] The tpw-decomp and mods/ submodule pointers are unchanged.
- [ ] It targets main and leaves launcher/VERSION.txt unchanged (releases are cut by the maintainers, see CONTRIBUTING.md).
- [ ] Changes to game source files are whole-file copies under overrides/ at the same path, each with a row in FIXES.md (what the Mac does, what the port does, why).
- [ ] Host code for another system is under platform/ and launcher/, behind that system's macro.
- [ ] No test aids (scratch programs, local scripts, logs), and no link to the game's files or note of where they are.
- [ ] The verifier shows 100% for US, European and Gold and the playtest passed on all three, pasted below (or "docs or CI only", or "Linux, no playtest").
- [ ] One concern per commit.

## Tested on

Edition (US, European, Gold), system, and the launcher version and build (a run's first line in the log):

## Verifier and playtest

<!-- A pull request that changes the game or the launcher: paste the lines `sh devtools/playtest.sh` (macOS) or `devtools\playtest.ps1` (Windows) printed, on your own game files: the verifier line and one result line per edition. See CONTRIBUTING.md, "Before a pull request". Never write where your game files are. A pull request that only changes documents, CI or other files the game and launcher never run: write "docs or CI only" and nothing else. The launcher is not exempt. On Linux, where the port does not run yet: write "Linux, no playtest"; a maintainer runs the playtest on a Mac before merging. -->

```
playtest: verifier: US 770/770 EUR 862/862 Gold 36/36, 100% for US, European and Gold
playtest: US: PASS ...
playtest: EUR: PASS ...
playtest: Gold: PASS ...
```
