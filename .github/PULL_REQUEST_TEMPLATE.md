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
- [ ] One concern per commit.

## Tested on

Edition (US, European, Gold), system, and the launcher version and build (the log's first line):
