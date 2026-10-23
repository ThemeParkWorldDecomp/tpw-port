// startup.cpp: the launcher's side of the hand-off to the game (startup.h).
#include "startup.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "handoff.h"
#include "platform.h"
#include "platform/game_tables.h"

namespace tpw::launcher {

bool gNoPreload = false;
std::vector<ui::WindowPlacement::Click> gWindowClicks;

namespace {

namespace fs = std::filesystem;

// launcher.ini's key for the bytes the last start of an edition read.
std::string StartupBytesKey(assets::Edition edition) {
    return std::string("startup_bytes_") + (edition == assets::kEditionUs ? "us" : "eur");
}

// Where the count goes once the game's window is shown (the backend is gone by then).
std::string gPrefPath;
std::string gKey;
assets::Edition gPreloadEdition = assets::kEditionEur;
bool gPreloadForEdition = false;

// The files the game opens before its window is shown, in the order it opens them (measured with both editions on
// 2026-10-22 with the open trace of platform/file_handles.cpp): read ahead into the system's file cache while the
// launcher's window is open. The game reads only their first parts before its window is shown and the rest while it
// loads the lobby. The two editions differ only in the language folder and the European font file.
std::vector<fs::path> FirstFiles(assets::Edition edition, const fs::path& gameDir) {
    static const char* const kBefore[] = {"data/sound.sam", "data/online.sam"};
    static const char* const kLanguage[] = {
        "ResIdx.dat",         "UniToMB.dat",      "MBToUni.dat",      "KEYBOARD.str",          "ERRORMSG.str",
        "INGREDIENT.str",     "TAG_SYSTEM.str",   "UITEXT.str",       "UIHELPTEXT.str",        "THOUGHTS.str",
        "KIDSTATES.str",      "STAFFSTATES.str",  "HANDYMAN_NAMES.str", "MECHANIC_NAMES.str",  "ENTERTAINER_NAMES.str",
        "GUARD_NAMES.str",    "RESEARCHER_NAMES.str", "FEMALE_NAMES.str", "STAFF_TYPES.str",   "LOANNAMES.str",
        "CHAT_COMMANDS.str",  "OBJECT_NAMES.str", "ITEMTYPES.str",    "THEMENAMES.str",        "MENUBIG.bf4",
        "CASHBIG.bf4",        "SESHBIG.bf4",      "DATEBIG.bf4",      "GAME12.bf4",            "TITLEBIG.bf4",
        "GAME9.bf4",          "GAME10.bf4",       "CONSOLE6.bf4",     "POSTCARD.bf4",
    };
    static const char* const kAfter[] = {"data/ui.wad",           "data/esprites.wad",
                                         "data/Lobby.wad",        "data/global/advisor.wad",
                                         "data/global/speech/lips.wad", "data/high.sam"};
    const bool us = edition == assets::kEditionUs;
    const fs::path language = fs::path("data/Language") / (us ? "American" : "English");
    std::vector<fs::path> files;
    for (const char* name : kBefore) files.push_back(gameDir / name);
    for (const char* name : kLanguage) files.push_back(gameDir / language / name);
    if (!us) files.push_back(gameDir / "data/fonts.wad");
    for (const char* name : kAfter) files.push_back(gameDir / name);
    return files;
}

void Pump(float fraction) {
    if (!ui::DrawStartingWindow(fraction)) {
        tpw::Log("launcher: the window was closed while the game was starting");
        tpw::LogFileF("exit: code 0 (the window was closed while the game was starting)");
        std::_Exit(0);
    }
}

void Shown(uint64_t gameBytes) {
    ui::CloseStartingWindow();
    tpw::StartupStamp("launcher window closed");
    if (gPrefPath.empty()) return;
    std::string err;
    if (ui::ImporterBackend::SaveIniExtra(gPrefPath, gKey, std::to_string(gameBytes), err))
        tpw::Log("startup: %s=%llu kept in launcher.ini for the next start", gKey.c_str(),
                 static_cast<unsigned long long>(gameBytes));
    else
        tpw::Log("startup: could not keep %s in launcher.ini: %s", gKey.c_str(), err.c_str());
}

}  // namespace

ui::WindowOutcome OpenLauncherWindow(ui::ImporterBackend& backend, ui::LaunchChoice& choice, std::string& err,
                                     const ui::WindowPlacement& placement) {
    if (gNoPreload) {
        tpw::Log("preload: off (--no-preload)");
    } else {
        tpw::PreloadPlan plan;
        if (backend.HasProgram()) {
            plan.program = backend.ProgramPath();
            plan.loadTables = &LoadGameTables;
        }
        const std::string last = backend.LastChoice();
        if (!last.empty() && backend.importer() != nullptr) {
            gPreloadEdition = last == "us" ? assets::kEditionUs : assets::kEditionEur;
            gPreloadForEdition = true;
            if (backend.Status(gPreloadEdition).unlocked)
                plan.files = FirstFiles(gPreloadEdition, backend.importer()->GameFolder(gPreloadEdition));
        }
        tpw::StartPreload(plan);
    }
    tpw::SetStartupPump(&Pump);
    const ui::WindowOutcome outcome = ui::RunLauncherWindow(backend, choice, err, placement);
    if (outcome == ui::WindowOutcome::Play) {
        BeginStartup(backend, choice.edition);
    } else {
        tpw::CancelPreload();
        tpw::WaitPreload();
    }
    return outcome;
}

void BeginStartup(ui::ImporterBackend& backend, assets::Edition edition) {
    gPrefPath = backend.PrefPath();
    gKey = StartupBytesKey(edition);
    const std::string kept = backend.IniExtra(gKey);
    const uint64_t expected = kept.empty() ? 0 : std::strtoull(kept.c_str(), nullptr, 10);
    // The files read ahead were the other edition's: no use waiting for the rest.
    if (gPreloadForEdition && gPreloadEdition != edition) tpw::CancelPreload();
    tpw::SetGameWindowShownHook(&Shown);
    tpw::StartupBegin(expected);
    tpw::StartupStamp(expected ? "Play" : "Play (no total from an earlier start: the bar has no number)");
    tpw::OnStartupProgress();
}

bool LoadTables(const fs::path& program, std::string* why) {
    if (!tpw::StartupBegun()) {
        tpw::StartupBegin(0);
        tpw::StartupStamp("start (counted from here, no total)");
    }
    bool ok = false;
    if (tpw::PreloadedTables(program, &ok, why)) {
        tpw::StartupStamp(ok ? "tables loaded (by the preload)" : "tables failed (in the preload)");
        return ok;
    }
    ok = LoadGameTables(program.string().c_str(), why);
    tpw::StartupStamp(ok ? "tables loaded" : "tables failed");
    return ok;
}

}  // namespace tpw::launcher
