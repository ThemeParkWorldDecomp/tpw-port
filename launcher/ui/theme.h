// theme.h: the launcher screen's name, wording and look. The look is a generic fairground scheme: orange and
// sunshine-yellow panels, lime "go" buttons, a sky-blue backdrop, thick dark outlines, rounded corners and soft shadows.
// Every colour below was picked by eye for this file. None is sampled from any game image, and the screen draws no
// logo, character, ride or wordmark of the game (launcher-design section 5 and section 8).
#pragma once
#include <cstdint>

namespace tpw::ui {

// The product's name as the player reads it (window title, home screen wordmark, About, --help), in one place so it can
// be renamed. The per-user data folder cannot hold a slash, so it has its own name just below.
#define TPW_UI_PRODUCT_NAME "STP/TPW Launcher"
inline constexpr const char* kProductName = TPW_UI_PRODUCT_NAME;
// The product name for the per-user data folder (SDL_GetPrefPath's application name; on macOS
// ~/Library/Application Support/STP-TPW Launcher). The same words without the slash.
inline constexpr const char* kDataFolderName = "STP-TPW Launcher";
// The launcher's version, as a player quotes it in a bug report. Its only source is launcher/VERSION.txt:
// CMakeLists.txt reads it into TPW_LAUNCHER_VERSION (and into the Windows version resource and the macOS Info.plist
// template), and every place that shows the version uses one of the three constants below. A release says X.Y.Z; a
// build of main between releases says the next version with -dev (0.0.4-dev), so it never passes for a release.
#ifdef TPW_LAUNCHER_VERSION
#define TPW_UI_VERSION TPW_LAUNCHER_VERSION
#else
#define TPW_UI_VERSION "unknown"
#endif
inline constexpr const char* kLauncherVersion = TPW_UI_VERSION;
// "v" and the version: the label beside the home screen's title and in the footer.
inline constexpr const char* kVersionLabel = "v" TPW_UI_VERSION;
// The product name, " v" and the version: the application's name as the system shows it (window title, the macOS
// application menu while the launcher window is up, the About title, the first line of --version and of the log).
inline constexpr const char* kProductNameWithVersion = TPW_UI_PRODUCT_NAME " v" TPW_UI_VERSION;
#undef TPW_UI_VERSION
#undef TPW_UI_PRODUCT_NAME
// How the games are mentioned: descriptively, never as a logo (launcher-design section 5, "Trademark naming").
inline constexpr const char* kTagline =
    "An unofficial port for playing your own copy of Sim Theme Park (Mac) and Theme Park World (PC).";
// Shown on the first screen and in the About box, word for word.
inline constexpr const char* kDisclaimer =
    "Not affiliated with or endorsed by Electronic Arts. Sim Theme Park and Theme Park World are trademarks of their "
    "respective owners. No game content is included; you need your own discs.";

struct Rgba {
    uint8_t r, g, b, a;
    constexpr Rgba WithAlpha(uint8_t alpha) const { return {r, g, b, alpha}; }
};

namespace colour {
inline constexpr Rgba kSkyTop{86, 178, 238, 255};
inline constexpr Rgba kSkyBottom{178, 226, 252, 255};
inline constexpr Rgba kCloud{255, 255, 255, 170};
inline constexpr Rgba kOrange{255, 148, 46, 255};
inline constexpr Rgba kOrangeDeep{226, 104, 24, 255};
inline constexpr Rgba kYellow{255, 211, 66, 255};
inline constexpr Rgba kYellowSoft{255, 236, 160, 255};
inline constexpr Rgba kLime{136, 206, 44, 255};
inline constexpr Rgba kLimeBright{162, 228, 70, 255};
inline constexpr Rgba kLimeDeep{88, 152, 22, 255};
inline constexpr Rgba kCream{255, 250, 238, 255};
inline constexpr Rgba kPaper{255, 255, 255, 255};
inline constexpr Rgba kInk{42, 30, 62, 255};        // outlines and text
inline constexpr Rgba kInkSoft{96, 82, 112, 255};   // secondary text
inline constexpr Rgba kRed{234, 80, 76, 255};
inline constexpr Rgba kSkyChip{120, 196, 242, 255};
inline constexpr Rgba kGrey{196, 194, 204, 255};
inline constexpr Rgba kShadow{28, 18, 48, 80};
inline constexpr Rgba kDim{28, 18, 48, 140};         // behind the About box
}  // namespace colour

namespace metric {
inline constexpr float kMargin = 16;     // the window's side margins, in points
inline constexpr float kGap = 12;        // between panels
inline constexpr float kRadius = 16;     // panel corners
inline constexpr float kOutline = 3;     // the dark outline of panels, tiles and buttons
inline constexpr float kShadowDrop = 5;  // how far a shadow sits below its panel
inline constexpr float kShadowBlur = 10;
// The smallest window the layout is made for: the title row holds the title, its version, Settings and About side by
// side (720 before the version joined it).
inline constexpr int kMinWidth = 760;
inline constexpr int kMinHeight = 520;
inline constexpr int kStartWidth = 800;
inline constexpr int kStartHeight = 560;
}  // namespace metric

}  // namespace tpw::ui
