// startup.h: the launcher's side of the hand-off to the game (platform/handoff.h). The launcher's window opens with the
// preload running behind it; after Play it stays open on the Starting screen, a progress bar the platform layer keeps
// drawn while the game starts, and closes when the game's window is shown. The bytes that start read are kept in
// launcher.ini (startup_bytes_us, startup_bytes_eur), the bar's total for the next start of that edition.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "ui/backend.h"
#include "ui/window.h"

namespace tpw::launcher {

// --no-preload: the launcher's window opens without reading anything ahead.
extern bool gNoPreload;
// --window-click: scripted clicks in the launcher's window (a test run).
extern std::vector<ui::WindowPlacement::Click> gWindowClicks;

// RunLauncherWindow (launcher/ui/window.h) with the preload started first. Play: the window stays open on the Starting
// screen and the start is being counted (BeginStartup). Otherwise the preload is stopped and joined.
ui::WindowOutcome OpenLauncherWindow(ui::ImporterBackend& backend, ui::LaunchChoice& choice, std::string& err,
                                     const ui::WindowPlacement& placement);

// The start of `edition` from the data folder (Play, or --play): the clock and the byte count start, with the total
// launcher.ini kept from the last start; when the game's window is shown the new total is written there.
void BeginStartup(ui::ImporterBackend& backend, assets::Edition edition);

// The game starts: the preload is waited for (FinishPreload, behind the bar). A start without the data folder (--game) is
// counted from here, with no total.
void StartGame();

}  // namespace tpw::launcher
