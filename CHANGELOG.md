# Changelog

## 0.5.1 (2026-11-12)

### Bug Fixes

- The TPI Worlds and Rides mod's rides, shops and scenery now have the names Theme Park Inc shows for them, such as Bellows Bouncer and InvenTiVe World, instead of working names like bellows and TV sim
- Fixed pieces of ground flashing on the islands of the TPI Worlds and Rides mod's three worlds on the island screen
- Fixed visitors in the TPI Worlds and Rides mod's worlds being drawn with a blue outline and no shadow on some ground, such as the road outside Land of Invention's gate
- Fixed a loud crackle that never stopped in Land of Invention with sound effects on: the gate's crackle now plays as quietly as Theme Park Inc's other background sounds
- The advisor no longer greets you with another world's welcome, such as "Welcome to Halloween World" in Polar Zone, when you open a park in one of the TPI Worlds and Rides mod's worlds: it gives the game's own welcome instead
- Fixed a crash in the TPI Worlds and Rides mod's Land of Invention with sound effects on: some of its rides' sounds were read from the wrong place in their sound file
- Fixed visitors in the TPI Worlds and Rides mod's worlds standing in front of the park gate instead of paying at the ticket booths, and visitors leaving never getting to the bus: they now wait on the pavement by the bus stops, where Theme Park Inc's entrance has them
- Fixed white squares and bare ground where Theme Park Inc's lake, bridges and tunnels stand in the TPI Worlds and Rides mod's worlds: the lake is drained and the bridges and tunnels are drawn as Theme Park Inc shows them once they are cleared

## 0.5.0 (2026-11-12)

### New Features

- A new mod, TPI Worlds and Rides: three worlds from Theme Park Inc (SimCoaster), the game's sequel: Arabian Nights, Polar Zone and Land of Invention, each with its own rides, coasters, shops, sideshows and scenery, played by Theme Park World's rules with Theme Park World's visitors and staff. Drop your own Theme Park Inc disc, disc image or installed game folder on the launcher to unlock it in the Mods box
- With the mod on, the island screen has a second ring of islands for the three new worlds: Tab (or a right click) flies between the two rings, left and right browse inside one. They cost golden keys like the original worlds, in the order Theme Park Inc opens them: Land of Invention 1, Polar Zone 3 and Arabian Nights 5
- The Game Data page lists Theme Park Inc like the PlayStation disc, with the same Convert again and Forget buttons, and the launcher's data dropper takes its disc, image, archive or install folder
- A park saved in one of the three worlds loads only with the mod on and its worlds converted, and cannot be published online, because the server has no such worlds

## 0.4.3 (2026-11-11)

### Bug Fixes

- A PlayStation ride picked from Buy Ride now starts facing the same way as every other ride, with its entrance at the bottom of the screen, instead of the opposite way. Rides already in a park are not changed
- Fixed the launcher failing to install or update on Windows when the user name has an accented letter, such as é (Thanks @Jerome67)

## 0.4.2 (2026-11-10)

### New Features

- With "W A S D move the camera" on, two of the keys held together now scroll the park diagonally (W with A, W with D, S with A and S with D), and in first person walk and turn at once, whether "Several keys at once" is on or off. Asked for by the maintainer

### Bug Fixes

- PlayStation rides converted by 0.4.0 are updated on their own: the launcher now converts them again at its start from the disc file it remembers, and when it has none (a disc dropped before 0.4.2) it asks for the disc again on its home screen instead of keeping the old rides (the mirrored Aztec Bounce, the doors on the wrong side) quietly

## 0.4.1 (2026-11-09)

### New Features

- The launcher now opens .7z and .rar archives, as it opens zips: drop a downloaded 7-Zip or RAR archive of your disc (a RAR in several parts too) and it is checked like the files in it. A password-protected or damaged archive gets a message saying so

### Bug Fixes

- Fixed a crash when a park is rebuilt from its building record and the record has a ride this game does not have, for example a park built in a full game loaded into an easy park
- Dragging files over the launcher no longer makes a game's tile grow as if the files went into that game. The whole window now shows one drop area that says what it takes, since every file is checked by its content and added to the game it belongs to
- The PlayStation rides now stand on their own ground from the disc, such as Diplo-Dip's pool and its sandy shore, instead of plain grass and path. The disc's plain ground (the Lost Kingdom's grass, Halloween World's flagstones and lawn, Space Zone's purple ground) lets the park's own ground show through, its grass, dirt patches and wear, around the ride's own floor, such as Diplo-Dip's water, shore and rocks, the Clown's ring and the Skulls' stone circles (with Ground blending on, as the PC rides' own bases are), so the ride's square no longer stands out. They are no longer the mirror image of the PlayStation's (Aztec Bounce and Diplo-Dip faced the wrong way), and every ride's entrance and exit are where the PlayStation has them (they were the other way round, and Diplo-Dip's exit was on the far side). Drop your PlayStation disc on the launcher again to update rides you have already imported
- Parks saved with 0.4.0 that have PlayStation rides now load with each of those rides' queue and exit path joined again to its entrance and exit, which 0.4.1 moves to where the PlayStation has them. Drop your PlayStation disc on the launcher again first, so the rides are converted again
- The riders on the PlayStation rides now sit in their seats as heads, as on the PC's car rides, instead of running in place on top of the ride. In the Crazy Clown's bells and the Jumping Skulls' skulls only the top of each head shows, and Romper Stomper's riders ride out of sight inside its capsule. The PlayStation rides' cameras now look ahead and stay upright, and Eye Slide and Romper Stomper get a ride camera too, placed so that no rider's head fills the view
- With "W A S D move the camera" on, the camera no longer keeps scrolling left after you let go of A when you pressed another key (W, S, D, Escape or any other) while holding it. The camera also stops when the game window loses focus and when you leave a park for the lobby
- With Zoom out further on, the park is now drawn all around when zoomed all the way out, also in a wide window. Before, the ground, rides and scenery stopped at a square around the camera and the sides of the picture were left empty

## 0.4.0 (2026-11-09)

### New Features

- A new mod, PlayStation rides: the seven rides only the PlayStation edition had, and its Tower as Halloween scenery. Drop your own USA, European or Japanese PlayStation disc on the launcher to unlock it in the Mods box
- A new setting, W A S D move the camera (off by default): W, A, S and D scroll the park and walk in first person (Z, Q, S and D on an AZERTY keyboard). With it on, A and S no longer open the All items and All staff screens
- A new setting, Zoom out further (off by default): the mouse wheel and the zoom keys can take the camera further out, to see more of the park at once
- Buy Ride now marks the Gold Edition's extra rides with a small GOLD tag and the PlayStation rides with a PS1 tag. Settings has Show Ride Badges to turn them off
- The launcher's Game data button now shows what your discs have unlocked: Sim Theme Park, Theme Park World, the Gold Edition, the PlayStation rides mod and the extra languages, each locked or unlocked, with how to unlock it or what is there. In a wide window it sits in the header, where the header's Play button was (the game tiles keep their own Play buttons)

### Bug Fixes

- Fixed a crash when visiting an online park that has a ride the server does not list
- A saved park that needs rides you have switched off (the Gold rides, or a mod's) is no longer replaced by a new park: the game says which rides it needs without opening it, and keeps it for when they are back
- The camera no longer keeps scrolling by itself after the pointer leaves the game's window, in windowed mode or onto a second screen (Thanks slappyhead and @samueleagle91)
- Fixed letters such as ä, ö, ü, ß, é and ñ not being typed in names (Thanks @johhnyjefferson34133 and @samueleagle91)
- Roller coasters in online parks that were started before 0.3.3 no longer lose their loops and track, or show their supports at the wrong height, when the park is visited (Thanks Addable13)
- Walking in first person in an online park no longer disconnects you from chat
- On a widescreen, a park visited online scrolls right again when the pointer is at the screen's right edge while the contacts and chat are open

### Changes

- The launcher's home screen no longer has the panel along its bottom with the port's description and the disclaimer (they stay in the About box), so the tiles have more room. The version is still shown beside the title

## 0.3.3 (2026-11-08)

### Bug Fixes

- Fixed another crash while visiting some online parks that have a roller coaster (Thanks slappyhead and Addable13)
- The launcher now remembers which mods are switched on or off after a game is played
- Fixed a crash when publishing a park to an online server that offers no upload server (Thanks @samueleagle91)
- The launcher's Settings and Mods scroll bars can now be dragged, so their lower rows can be reached without a mouse wheel (Thanks slappyhead and @samueleagle91)
- The Gold Edition now unlocks from downloads that have its 35 rides but not the installer's ride list (Thanks OncleSam and a pretty cool guy)
- Ride signs now draw accented letters such as ä, ö, ü, ß and é correctly in the German, French and other PC languages (Thanks @johhnyjefferson34133 and @samueleagle91)
- Fixed a crash when a member of staff walked along one edge of the map, for example after being placed there with the cheat keys (Thanks @samueleagle91)
- Fixed a crash when opening a city on the online globe whose park list is empty or holds a park of a theme the server does not list (Thanks @samueleagle91)

## 0.3.2 (2026-11-07)

### Bug Fixes

- Fixed a crash while watching some online parks that have a roller coaster
- Fixed a crash when publishing a park that has certain rides (Thanks slappyhead and Addable13)
- Fixed the camera not scrolling left or right at the screen's edges while visiting a park online on a widescreen (Thanks Addable13)
- On a widescreen, the online globe now turns left at the screen's left edge, and the city under the pointer is the one that lights up (Thanks Addable13)
- While visiting a park online on a widescreen, the contacts and chat now sit against the screen's left edge instead of in a 4:3 box in the middle (Thanks slappyhead)

## 0.3.1 (2026-11-06)

### Bug Fixes

- `--version` and the logs now show the right build number. On 0.3.0 they said `v0.2.0-19-gd0b2f9f`
- The launcher's home now has a Play button on each playable edition, even without every disc
- Pressing Play with only a Windows disc imported no longer closes the launcher without starting the game
- Windows: a user name with accents or non-Latin letters no longer breaks the launcher
- Fixed a crash when starting the game with no working sound device; it now plays silently
- Sound starts by itself when a sound device becomes available, and moves when the device changes, with no restart

## 0.3.0 (2026-11-06)

### New Features

- Online is back: connect to a revived online server, the same one original PC games can use. Log in, read the news, browse cities, parks and top 10s, publish a park, visit parks with other players, chat, keep buddy and ignore lists, and vote. Mail and postcards don't work yet.
- Create an online account in the game, with a Forgot password button that e-mails you a code.
- Play from a Windows disc alone: the US CD (2.0) gives Sim Theme Park and the European CD gives Theme Park World, with no Mac CD. Films from a Windows disc play too.
- A Mods window in the launcher (all off by default), with two mods: Enable strikes and Patrolling staff can cross bridges. (Thanks BMCTV and slappyhead)
- New Settings option: Several keys at once (off by default). Hold two arrow keys to walk and turn at the same time in first person. (Thanks Addable13)
- The water, flags, lightning, lobby sounds, camera and menus move at one steady pace on any monitor, and a new Frame rate limit setting sets how many frames the game draws. (Thanks Decidetto)
- The launcher's Settings show every setting of the game's Options panel, and the two stay in sync.
- Edit the game's .sam settings files freely: your edits are kept, and the launcher shows a Customized settings badge with a Restore original button. (Thanks BMCTV, slappyhead and @samueleagle91)

### Bug Fixes

#### Game

- Fixed the Screen resolution slider not moving: it now changes the game's size live, in about ten sizes including 1920 x 1080, with buttons for 4:3, 16:10 and 16:9.
- Fixed visitors' happiness changing by the wrong amounts: a small disappointment dropped it to 0, which often sent them to the exit, and rides matching their taste gave no bonus.
- Fixed the advisor's warnings about staff and ride breakdowns having the wrong importance, so most were never spoken.
- Fixed tired staff standing still for good when no path leads to a staff room. (Thanks BMCTV and @samueleagle91)
- Fixed queues stopping for a few moments when a ride let on someone who wasn't at the front. (Thanks @samueleagle91)
- Each notification shows its own icon again, not always the yellow "i". (Thanks a pretty cool guy, Piro and @samueleagle91)
- Fixed the visitor, staff room and hire lists keeping people who had left, and lists opened from a visitor, staff member, ride or item not selecting it. (Thanks @samueleagle91)
- Fixed time in park and visitor numbers missing from visitor panels and the all-kids list.
- Fixed missing or wrongly coloured lines on the Park Information and Financial Information charts. (Thanks a pretty cool guy and @samueleagle91)
- Fixed the park map not showing paths, queues, rides, shops or the park's land, showing the wrong colours, and drawing coaster track black.
- Fixed the park map not highlighting a staff member's patrol area, and scrolling by itself after being reopened. (Thanks @samueleagle91)
- Fixed coaster track pieces not showing the see-through blue preview while being placed. (Thanks Addable13 and Piro)
- Fixed being able to lay track across a ride after moving a coaster's support, which deleted the ride.
- Fixed coaster track through a loop support stretching too far after moving its slider.
- Fixed the camera not scrolling sideways on a widescreen with the interface hidden (F3) or after the Bankrupt screen, and right clicks at the screen's sides not closing the Bankrupt and Congratulations screens. (Thanks Addable13 and @samueleagle91)
- Fixed the Send Postcard preview looking dark and dotted at large game sizes.
- European edition: fixed a crash placing the Shocker coaster in Space Zone, or loading a park that has one. (Thanks pulpmetal and @samueleagle91)
- Fixed crashes loading a damaged save, or a park whose coaster has a pylon without its ride part. (Thanks @samueleagle91)
- Fixed crashes in staff and visitor lists: pressing Down or Home in All Staff with none of that kind of staff, and opening a list after using the cheat keys. (Thanks BMCTV and @samueleagle91)
- Fixed two rare sound crashes in busy parks and long play.
- Fixed a possible crash after loading a park saved by the original Mac game. A park saved by this version can crash older versions of the port, so update before opening it.
- Linux: fixed the game closing when you save a park from the in-park menu. (Thanks xoob and @adds39939)
- Windows: fixed saved parks being lost or cut off when the file name was long, and a crash listing players with names your language setting can't show. (Thanks @samueleagle91)

#### Launcher

- Every copy of the launcher on a computer shows the game's own icon once the game is imported, even with a different game data folder.
- Fixed the bottom of the Settings box being cut off. (Thanks @samueleagle91)
- The launcher checks for a new version each time it starts, not once a day.
- The launcher no longer needs the Mac program from a disc.

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
