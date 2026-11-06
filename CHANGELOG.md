# Changelog

## 0.3.0 (not released yet)

### New Features

- New Settings option: Several keys at once (off by default). Hold two arrow keys to walk and turn at the same time in first person, as on the PC. (Thanks Addable13)
- A Mods window in the launcher, for optional changes to how the game plays (all off by default, and off is the original), and the first mod, Enable strikes: staff can go on strike in an open park, as Bullfrog's code was written to do, and the advisor gives the two strike messages the original never used (a last warning a week before a strike, and word when it is over). (Thanks BMCTV and slappyhead for the strike reports)
- A second mod, Patrolling staff can cross bridges: staff walking their rounds can cross a footbridge, like the bridge over the stream in the Lost Kingdom, not only when sent to a job on the other side. A bridge outside a worker's patrol area is still never taken.
- The game plays the films from a Windows disc (the .TGQ files) when your game data has no Mac films, and the launcher counts them in place of the Mac films.
- Play from a Windows disc alone: the US Windows CD (2.0) gives Sim Theme Park and the European one Theme Park World, with no Mac CD. They play with the Windows release's own text, settings files, sign fonts and films. With a Mac CD there too, the Mac edition is played as before. A US Windows CD 1.0 gets a message saying how to bring it to 2.0 with EA's official patch.
- The online screens can connect to a revived online server that speaks the original game's protocol, the same server original PC games can use: Logging in, the news, the cities, their parks and top 10, and publishing a park work. The launcher learns the server's address with its update check; Settings has an Online server field for another one. Sending mail and postcards doesn't work yet: the game shows its own error.
- Parks on the online server can be visited again: the players visiting a park see each other in its visitor list, chat, keep their buddy and ignore lists, and vote for the park, as in the original. Chat commands such as /afk come in a later version.
- When the online server can't be reached (no internet connection, or the server is down), the game says so plainly: "Can't reach the online server. Please check your internet connection and try again." The reason is written to launcher.log. A wrong password or a full server still gets the game's own message.
- Create an online account in the game: the online login panel has a Create account button. Choose a login name and a password (typed twice), give an e-mail address for password resets (kept private, never shown) and tick "I am 13 or older". OK puts the name and the password in the login boxes, so you only press Log in. Forgot password? e-mails you a 6-digit code to set a new password in the game.
- The water, flags, lightning, lobby sounds, camera and menus now move at one steady pace on any monitor, and the launcher's new Frame rate limit sets how many frames the game draws. (Thanks Decidetto)
- The launcher's Settings box shows every setting of the game's Options panel, and they stay in step: change the screen size, a volume or anything else in the game or in the launcher, and the other has it next time.

### Bug Fixes

#### Game

- Windows: fixed saved parks being lost, or saved under a cut-off name, when the file name was long. (Thanks @samueleagle91)
- Windows: fixed a crash listing the players when a player's name has letters the computer's language setting cannot show, such as Japanese on an English Windows. (Thanks @samueleagle91)
- European edition: fixed a crash placing the Shocker coaster in Space Zone, or loading a park that has one. (Thanks pulpmetal and @samueleagle91)
- Each in-game notification shows its own icon again (the red "!", the speech bubble, the golden ticket, a challenge's icon, the postcard's envelope), not always the yellow "i". (Thanks a pretty cool guy, Piro and @samueleagle91)
- Fixed tired staff standing still for good when no path leads to a staff room: like in the original, they now keep working. (Thanks BMCTV and @samueleagle91)
- Fixed the visitor list, the staff room panel and the hire panel keeping people who had already left, and the lists opened from a visitor, a member of staff, a ride or an item not selecting it. (Thanks @samueleagle91)
- Fixed a crash when pressing Down or Home in the All Staff list after switching to a kind of staff you have none of. (Thanks @samueleagle91)
- Fixed the Send Postcard preview looking dark and dotted at game sizes larger than 1024x768.
- Coaster track pieces being placed show the see-through blue preview again. (Thanks Addable13, Piro)
- Fixed the camera not scrolling left or right on a widescreen with the game's interface hidden (F3), or after closing the Bankrupt (game over) screen. (Thanks Addable13 and @samueleagle91)
- Fixed a right click at the left or right of a widescreen not closing the Bankrupt (game over) screen.
- Fixed a right click at the left or right of a widescreen not closing the Congratulations screen when you win a park.
- Fixed a crash loading a park whose saved roller coaster has a pylon without its ride part. (Thanks @samueleagle91)
- Fixed a crash loading a park whose save file was cut short or damaged, for example when the game was closed while it was saving: such a save is now left unloaded, like any save the game cannot read.
- Fixed a crash opening a staff or visitor list after adding people or staff with the cheat keys. (Thanks BMCTV)
- Linux: fixed the game closing when you save a park from the in-park menu. (Thanks xoob and @adds39939)
- Fixed lines missing, or drawn in the wrong colour, on the Park Information and Financial Information charts. (Thanks a pretty cool guy and @samueleagle91)
- Fixed the park map not showing paths, queues, rides, shops or the park's land and its edge, and showing the map's other colours wrong.
- Fixed the park map not highlighting the patrol area of the member of staff under the pointer. (Thanks @samueleagle91)
- Fixed the park map scrolling by itself after it was closed during a right-button drag with the Ctrl-click scroll setting and then reopened. (Thanks @samueleagle91)
- Fixed roller coaster track always being black on the park map with coasters shown: it has the original's blues and greens again.
- Fixed being able to lay track across a ride after moving a coaster's support, which deleted the ride: the game refuses it, as in the original.
- Fixed a rare crash in a busy park when a sound started just as another one ended.
- Fixed a rare crash after long play when a set of sounds was freed while its sounds were still playing.
- Fixed the advisor's warnings about unhappy or striking staff and about ride breakdowns having the wrong importance: most of them were never spoken, and a few always pushed ahead of everything else.
- Fixed visitors' happiness changes using the wrong settings: a small disappointment took a visitor's happiness to 0 whatever it was, which often started them towards the exit, a medium one or a cheap entry fee changed nothing, a big one took too much, and rides that matched a visitor's taste perfectly or only loosely gave no happiness bonus.
- Fixed coaster track through a loop support being too stretched after moving its slider: the curve always used the longest tangent instead of the one that depends on the support's angle and slider.
- Fixed queues stopping for a few moments when a ride let on someone who wasn't at the front yet. (Thanks @samueleagle91)
- Fixed the Screen resolution slider in the Options panel not moving: it now changes the game's size while it runs, with no restart, in about ten sizes people know (1920 x 1080 among them), shown beside it, with buttons next to it for 4:3, 16:10 and 16:9.
- Fixed visitor status panels and the all-kids list not showing time in park, and the all-kids list not showing visitor numbers.
- Fixed a possible crash, a little after loading, in a park saved by the original Mac game. Parks saved by earlier versions of the port still load. A park saved by this version can crash an earlier version of the port in the same way, so update before opening it.

#### Launcher

- Every copy of the launcher on a computer shows the game's own icon once the game has been imported on it, including a copy that uses a different game data folder. Before, such a copy showed the stand-in icon.
- Fixed the bottom of the launcher's Settings box being cut off. (Thanks @samueleagle91)
- The launcher now checks for a new version each time it starts, not once a day, so the Update banner shows as soon as a release is out.
- Edit the game's .sam settings files freely, as in the original: an edited file is now kept and used by the game, instead of being removed at the next start. The edition's tile shows a Customized settings badge; it lists the edited files, warns of a line the game can't read, and has a Restore original button for each. (Thanks BMCTV, slappyhead and @samueleagle91)
- The game's data tables are built in: the launcher no longer needs the Mac program from a disc.

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
