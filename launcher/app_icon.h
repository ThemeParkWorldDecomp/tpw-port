// app_icon.h: the launcher's icon in the Dock (macOS) and the taskbar (Windows): the game's own, <data folder>/icon.png,
// which the importer writes from the player's files (assets/icon.h), or, until a game is imported, a stand-in drawn in
// code in the launcher's own colours (app_icon_standin.h, launcher/ui/theme.h). No picture file is part of the tree: the
// game's icon is Bullfrog's artwork and never leaves the player's machine. On Windows the stand-in is also the icon of
// tpw_launcher.exe itself (Explorer's and the Start menu's), made at build time (launcher/win/make_ico.cpp).
#pragma once
#include <SDL3/SDL.h>

#include <string>

namespace tpw::launcher {

// Sets the icon for `dataFolder`'s icon.png, or the stand-in when there is none. On macOS the application's icon
// (NSApp setApplicationIconImage:, through the Objective-C runtime as launcher/main.cpp names the menu), so the Dock,
// Cmd-Tab and Force Quit show it; elsewhere the window's (SDL_SetWindowIcon, the taskbar's and the title bar's on
// Windows). Logs what it did. Called when the launcher window is made and again when an import finishes (window.h
// WindowPlacement::windowIcon).
void ApplyAppIcon(const std::string& dataFolder, SDL_Window* window);

// Writes the icon ApplyAppIcon would show (`dataFolder`'s icon.png, else the stand-in) as the file `path`: a multi-size
// .ico when the name ends in .ico, a PNG otherwise. The launcher's --write-icon: install.sh makes the icon of the
// application it puts in Applications from the PNG (THE APPLICATION); install.ps1 gives the .ico to the Start menu
// shortcut (THE START MENU SHORTCUT). `what` says which icon it was; false with `err` when the file could not be written.
bool WriteAppIcon(const std::string& dataFolder, const std::string& path, std::string& what, std::string& err);

}  // namespace tpw::launcher
