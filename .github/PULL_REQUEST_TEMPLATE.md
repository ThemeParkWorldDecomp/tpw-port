## What and why

<!-- One concern per pull request, and one per commit. A change to how a park plays belongs in tpw-mods, not here: see CONTRIBUTING.md. -->

## Checklist

- [ ] A park plays the same with this change on and with it off (rules, money, visitors, staff, rides, the advisor, content, what a control does and the save format are unchanged).
- [ ] A new setting is off by default, off is exactly the original, and it changes only display, input devices, sound output, files or the launcher.
- [ ] The tpw-decomp and mods/ submodule pointers are unchanged.
- [ ] Changes to game source files are whole-file copies under overrides/ at the same path, each with a row in FIXES.md (what the Mac does, what the port does, why).
- [ ] Host code for another system is under platform/ and launcher/, behind that system's macro.
- [ ] No test aids (scratch programs, local scripts, logs).
- [ ] One concern per commit.

## Tested on

Edition (US, European, Gold) and system:
