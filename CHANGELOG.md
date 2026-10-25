# Changelog

What's new and what's fixed in each version of the STP/TPW Launcher, newest first. How to write it: CONTRIBUTING.md,
The changelog.

## 0.1.0 (not released yet)

The first release: play Sim Theme Park and Theme Park World from your own discs, on Windows and Mac.

### New

- A Windows version, alongside the Mac one.
- Setting: skip the intro films.
- Setting: go back to the launcher when you quit the game.
- Setting: Full screen can be switched off to play in a window; Alt+Enter switches it while playing on Windows (Cmd+Ctrl+F on a Mac).
- Park rules: choose how many visitors each bus brings and how many your park can hold.
- Interface size: smaller panels, so you see more of your park.
- The original game's graphics options, now in the launcher's Settings.
- Asset packs: drop in zips of replacement pictures, sounds and films.
- Varied sounds: rides and guests use all their recordings, like the PC version, not the same one every time.
- The mouse wheel zooms smoothly toward the pointer.
- The game runs at a steady 60 frames a second.
- The date shows on the control panel.
- The Load panel shows save times in your own time zone.
- Wide screens: the game fits your screen, and the advisor, menus and Options screen are no longer stretched.
- Export logs, in the About box, saves the launcher's newest log to your Downloads folder, ready to attach to a bug report.
- Logs no longer contain your username or computer name, so they're safe to share.
- Install and update the launcher by pasting one line into Terminal or PowerShell. It builds on your computer, so there are no security warnings to click through.
- Type themepark in any new Terminal or PowerShell window to start the launcher, from any folder.
- The installer's --dir takes a folder written as ./name or with a trailing slash, and refuses a folder name the build cannot hold before anything is downloaded.
- The launcher tells you when a new version is out and its Update button installs it, and it stays quiet when you're offline. (Thanks @samueleagle91)
- Sim Theme Park is listed first everywhere, Theme Park World second; the launcher shows the US tile on the left.

### Fixed

- Windows: Ctrl-click copies a placed object, as Command-click does on a Mac, so the Start menu no longer pops up. (Thanks Addable13, Fred and Piro on Discord)
- Shop panel: Apply to all now gives your other shops this shop's price, quality and ingredient, instead of emptying their prices. (Thanks Piro and Fladfisk on Discord)
- Intel Macs on macOS Monterey can now build the launcher. (Thanks slappyhead on Discord)
- Windows: Show logs now opens the logs folder, with the newest log selected. (Thanks Dan on Discord)
- Fixed a crash when renaming a ride, shop or sign. (Thanks slappyhead and Fladfisk on Discord)
- Fixed a crash when closing the Save or Load panel.
- Fixed a crash when sending a postcard.
- Fixed a crash when closing the park map.
- Fixed a crash when pressing Backspace while laying water track or a queue line.
- Fixed a crash when opening a staff room with someone resting in it.
- Fixed a crash the first time a bumper car ride runs.
- Fixed crashes when starting at a small resolution on a wide screen.
- Fixed a crash when loading a busy park. (Thanks @Decidetto)
- Coasters can now be built. Placing a station used to crash the game.
- Handymen no longer wander off the edge of the map.
- The advisor no longer bases its advice on the wrong numbers.
- Staff now show under the pointer when you place them, and in the hire and staff room panels.
- The hire panel's staff figure stays inside its picture box.
- Thought bubbles, balloons, litter, vomit, stink bombs, riders' heads and ride upgrade effects now show.
- Your park's name shows on the gate and on ride signs.
- Message boxes now show their background.
- The park no longer drifts into a corner when the game window isn't focused.
- The launcher's Settings box scrolls when the window is too short.
- Windows: building works when the folder's name has a space in it.
- Windows: the installer's build no longer fails on the rules that keep your name out of the logs.

Most of this release was made by @samueleagle91. Thank you!
