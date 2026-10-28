// app_icon.h: the launcher's icon in the Dock (macOS) and the taskbar (Windows): the game's own, <data folder>/icon.png,
// which the importer writes from the player's files (assets/icon.h), or, until a game is imported, a stand-in drawn in
// code in the launcher's own colours (app_icon_standin.h, launcher/ui/theme.h). No picture file is part of the tree: the
// game's icon is Bullfrog's artwork and never leaves the player's machine. On Windows the stand-in is also the icon of
// tpw_launcher.exe itself (Explorer's and the Start menu's), made at build time (launcher/win/make_ico.cpp).
#pragma once
#include <SDL3/SDL.h>

#include <string>

#include "icon.h"

namespace tpw::launcher {

// Sets the icon for `dataFolder`'s icon.png, or the stand-in when there is none. On macOS the application's icon
// (NSApp setApplicationIconImage:, through the Objective-C runtime as launcher/main.cpp names the menu), so the Dock,
// Cmd-Tab and Force Quit show it; elsewhere the window's (SDL_SetWindowIcon, the taskbar's and the title bar's on
// Windows). Logs what it did. Called when the launcher window is made and again when an import finishes or the data
// folder changes (window.h WindowPlacement::windowIcon).
void ApplyAppIcon(const std::string& dataFolder, SDL_Window* window);

// Writes the icon ApplyAppIcon would show into the installer's files for the install the launcher runs from
// (launcher/installed_icon.h): on macOS the icon.icns of the application install.sh put in Applications (TPW_INSTALL_APPS
// in a test), on Windows DIR\icon.ico, which install.ps1's Start menu shortcut shows. Nothing for a launcher that was not
// installed, or a file that already holds that icon. Logs what it wrote. Called when an import ends or the data folder
// changes (window.h WindowPlacement::windowIcon) and after an --import, so that the Finder, Launchpad and the Start menu
// show the game's icon as soon as there is one.
void RefreshInstalledIcon(const std::string& dataFolder);

// Writes the icon ApplyAppIcon would show (`dataFolder`'s icon.png, else the stand-in) as the file `path`: a multi-size
// .ico when the name ends in .ico, a PNG otherwise. The launcher's --write-icon: install.sh makes the icon of the
// application it puts in Applications from the PNG (THE APPLICATION); install.ps1 gives the .ico to the Start menu
// shortcut (THE START MENU SHORTCUT); RefreshInstalledIcon keeps both up to date between installs. `what` says which icon it was; false with `err` when the file could not be written.
bool WriteAppIcon(const std::string& dataFolder, const std::string& path, std::string& what, std::string& err);

// The icon ApplyAppIcon shows, as pixels: `dataFolder`'s icon.png (`own` true, `source` its Source text) or the
// stand-in. The game's own application bundle on macOS gets it as an .icns (launcher/game_app.h).
tpw::assets::IconImage LoadAppIcon(const std::string& dataFolder, bool& own, std::string& source);

}  // namespace tpw::launcher
