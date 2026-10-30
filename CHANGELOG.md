# Changelog

## 0.2.1 (not released yet)

### Bug Fixes

#### Game

- Windows: fixed saved parks being lost, or saved under a cut-off name, when the file name was long. (Thanks @samueleagle91)
- Windows: fixed a crash listing the players when a player's name has letters the computer's language setting cannot show, such as Japanese on an English Windows. (Thanks @samueleagle91)

#### Launcher

- The launcher now checks for a new version each time it starts, not once a day, so the Update banner shows as soon as a release is out.

## 0.2.0 (2026-10-29)

### New Features

- Linux: the launcher and the game now run on 64-bit Linux, installed with the same one line as on a Mac (or as a Flatpak). OpenGL by default, Vulkan in Settings. (Thanks @adds39939)
- Play in other languages: a new Language option per game uses any language on your discs, including Japanese, Korean and Chinese. The Hebrew release can be added too. (Thanks Itay)
- New Settings options from the original game: Visitor looks (2, 4, 6 or 8 kinds of visitor) and Stereo panning (off by default). (Thanks @samueleagle91)
- One log file, launcher.log, instead of one per run. Export logs now includes the game's log, so crash logs are no longer left out.
- The lobby's globe button works again (logging in shows the game's "cannot connect" message, since the online service closed long ago).
- Windows: the taskbar and Alt+Tab show the game's name and icon while you play.
- A new --verify option rechecks your game data.

### Bug Fixes

#### Game

- Every cheat key now works, such as Option-1 for $10,000. (Thanks slappyhead)
- Fixed most particle effects not showing: win stars, building sparkles (shown as a black hole), smoke and splashes. (Thanks a pretty cool guy, Pixus and pulpmetal)
- Fixed films stuttering on slower computers and at large game sizes. (Thanks Addable13, Fladfisk and @samueleagle91)
- Fixed the park map at large screen sizes: drawn too small, sliding out of view, and clicking an empty spot now closes it and moves there. (Thanks Addable13, Pixus and @samueleagle91)
- Fixed coaster crashes: Backspace while building, and track stacked on a pylon. (Thanks BMCTV, pulpmetal and @samueleagle91)
- Fixed go-karts falling from the sky or sinking, and a crash on Wonder Land's bee jump. (Thanks pulpmetal and @samueleagle91)
- Fixed staff freezing at work, visitors stuttering and lost balloons not popping. (Thanks @samueleagle91)
- Fixed research getting stuck after a project finished, including in saved parks. (Thanks @samueleagle91)
- Fixed first-person walking stopping when Shift, Ctrl, Alt or Caps Lock is pressed. (Thanks Addable13 and @samueleagle91)
- Fixed a park sometimes starting with no sound. (Thanks @samueleagle91)
- Fixed F8 screenshots coming out black. (Thanks Piro)
- Fixed Instant Action and Full Simulation showing the wrong controls after switching player. (Thanks @samueleagle91)
- Fixed Hebrew, Japanese, Korean and Chinese player names turning into question marks.
- Fixed a dark line along ground tiles with Ground blending off, and the Send Postcard preview's colours. (Thanks @samueleagle91)
- Windows: fixed parks with ? or * in their name failing to save. (Thanks @samueleagle91)

#### Launcher

- Fixed the app icon staying as the placeholder, or blank in the Dock, after adding your game. (Thanks slappyhead, Fladfisk and @samueleagle91)
- Mac: fixed a second bouncing Dock icon, and "Keep in Dock" keeping the wrong one.
- Fixed some .mdf disc images being read as empty.
- Windows: fixed an error when updating the launcher in the same folder. (Thanks @samueleagle91)

## 0.1.0 (2026-10-27)

### Bug Fixes

#### Game

- Windows: Ctrl-click copies a placed object, as Command-click does on a Mac, so the Start menu no longer pops up. (Thanks Addable13, Fred and Piro on Discord)
- Shop panel: Apply to all now gives your other shops this shop's price, quality and ingredient, instead of emptying their prices. (Thanks Piro and Fladfisk on Discord)
- Fixed a crash when renaming a ride, shop or sign. (Thanks slappyhead and Fladfisk on Discord)
- Fixed a crash when closing the Save or Load panel.
- Fixed a crash when sending a postcard.
- Fixed a crash when closing the park map.
- Fixed a crash when pressing Backspace while laying water track or a queue line.
- Fixed a crash when opening a staff room with someone resting in it.
- Fixed a crash the first time a bumper car ride runs.
- Fixed crashes when starting at a small resolution on a wide screen.
- Fixed a crash when loading a busy park. (Thanks @Decidetto)
- Fixed a crash when loading a park with a flying ride. (Thanks @skelt)
- The date now shows on the control panel.
- Coasters can now be built. Placing a station used to crash the game.
- Handymen no longer wander off the edge of the map.
- The advisor no longer bases its advice on the wrong numbers.
- Staff now show under the pointer when you place them, and in the hire and staff room panels.
- The hire panel's staff figure stays inside its picture box.
- Thought bubbles, balloons, litter, vomit, stink bombs, riders' heads and ride upgrade effects now show.
- The close button of the slide-out panels on the control panel (staff locator with Ctrl+S, finances, and the others) answers clicks again.
- Your park's name shows on the gate and on ride signs.
- Message boxes now show their background.
- The park no longer drifts into a corner when the game window isn't focused.
- Leaving or restarting a park with a ride's or shop's panel open no longer corrupts the autosave, and such an autosave now loads: the panel's ride preview was saved as a ride no type could make again. (Thanks Piro and Addable13 on Discord)
- Leaving full screen (the Esc menu's Exit Full Screen, Alt+Enter on Windows, the green button or Cmd+Ctrl+F on a Mac) now gives a normal window: a title bar with the game's name, the close, minimise and zoom buttons, and edges you can drag to resize it, with the game kept to its shape inside.

#### Launcher

- Intel Macs on macOS Monterey can now build the launcher. (Thanks slappyhead on Discord)
- Windows: Show logs now opens the logs folder, with the newest log selected. (Thanks Dan on Discord)
- The launcher's Settings box scrolls when the window is too short.
- Windows: building works when the folder's name has a space in it.
- Windows: the installer's build no longer fails on the rules that keep your name out of the logs.
- The launcher comes to the front when started from the `themepark` command or the installer.
- On a development install, before any release is out, `themepark update` now says that there is no release yet and how to stay on main, instead of showing the last build's output and asking for a bug report.

### New Features

#### Game

- The game's Esc menu has an Enter Full Screen button (Exit Full Screen when you are full screen), above Quit Game.
- Varied sounds: rides and guests use all their recordings, like the PC version, not the same one every time.
- The mouse wheel zooms smoothly toward the pointer.
- The game runs at a steady 60 frames a second.
- The Load panel shows save times in your own time zone.
- Wide screens: the game fits your screen, and the advisor, menus and Options screen are no longer stretched.
- The Gold bonus CD's rides are in the game: the 35 rides of its RideConfig.txt are laid over the game's files when you play, as the Gold installer copied them (18 new rides, 17 newer copies), without touching your imported files.

#### Launcher

- Install and update the launcher by pasting one line into Terminal or PowerShell. It builds on your computer, so there are no security warnings to click through.
- Type `themepark` in any new Terminal or PowerShell window to start the launcher, from any folder.
- Type `themepark update` to update the launcher, and `themepark --version` to see which version you have.
- The launcher tells you when a new version is out and its Update button installs it, and it stays quiet when you're offline. (Thanks @samueleagle91)
- Setting: skip the intro films.
- Setting: go back to the launcher when you quit the game.
- Setting: Full screen can be switched off to play in a window; Alt+Enter switches it while playing on Windows (Cmd+Ctrl+F on a Mac).
- Windows: the installer fetches a portable Git when the PC has none, so there is nothing to install first.
- On a Mac, the installer puts STP/TPW Launcher in your Applications folder, with the game's icon, so you can open it from Launchpad, Spotlight, the Finder or the Dock as well as by typing `themepark`.
- On Windows, the installer puts STP/TPW Launcher in the Start menu, with the game's icon, so you can open it from there or find it with Search as well as by typing `themepark`.
- Mac: the Dock, Cmd-Tab and Force Quit call the launcher STP/TPW Launcher, and show the game's own icon once your discs are imported (until then, a picture in the launcher's colours with THEME PARK on it).
- Mac: while you play, the Dock, Cmd-Tab, Force Quit and Activity Monitor say Sim Theme Park or Theme Park World, with the game's icon, instead of the launcher's name.
- The installer shows a clean progress bar instead of compiler output; run it with `--plain` (`-Plain` in PowerShell) for the full text.
- The installer's progress view sits in a frame with the version in its title, and says what the build is doing (fetching tools, configuring, starting the compiler) until the compiler reports a percentage.
- The installer's `--dir` takes a folder written as `./name` or with a trailing slash, and refuses a folder name the build cannot hold before anything is downloaded.
- The original game's graphics options, now in the launcher's Settings.
- Gold Edition switch on each play tile: play the plain edition even when the Gold data is installed; the launcher remembers it. A park saved with a Gold ride built is refused with the switch off (the game's own missing-rides answer, with a log line) instead of crashing.
- Asset packs: drop in zips of replacement pictures, sounds and films.
- Export logs, in the About box, saves the launcher's newest log to your Downloads folder, ready to attach to a bug report.
- The log names your graphics driver (maker, card and OpenGL version), and says if the driver lacks a call the game's films need, so a "sound but no picture" report can be traced.

Most of this release was made by @samueleagle91. Thank you!
