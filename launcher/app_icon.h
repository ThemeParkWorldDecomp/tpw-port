// app_icon.h: the launcher's icon in the Dock (macOS) and the taskbar (Windows): the game's own, <data folder>/icon.png,
// which the importer writes from the player's files (assets/icon.h), or, until a game is imported, a stand-in drawn here
// in the launcher's own colours (launcher/ui/theme.h). No picture file is part of the tree: the game's icon is Bullfrog's
// artwork and never leaves the player's machine.
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

}  // namespace tpw::launcher
