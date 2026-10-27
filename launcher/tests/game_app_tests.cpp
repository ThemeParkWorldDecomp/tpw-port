// game_app_tests.cpp: the game's own application on a Mac (launcher/game_app.h), the part that runs everywhere: the
// bundles' names, the Info.plist the launcher writes for them from its own, the arguments to the game's process and
// back, and the .icns the launcher writes from the icon (its elements, each a PNG of the right size).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game_app.h"
#include "icon.h"

using namespace tpw::launcher;
using tpw::assets::IconImage;

namespace {

int gFailures = 0;

void Check(bool ok, const std::string& what) {
    if (ok) return;
    ++gFailures;
    std::printf("FAILED: %s\n", what.c_str());
}

uint32_t Be32(const std::vector<uint8_t>& bytes, size_t at) {
    return (static_cast<uint32_t>(bytes[at]) << 24) | (static_cast<uint32_t>(bytes[at + 1]) << 16) |
           (static_cast<uint32_t>(bytes[at + 2]) << 8) | bytes[at + 3];
}

const char* const kLauncherPlist =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\">\n<dict>\n"
    "\t<key>CFBundleExecutable</key>\n\t<string>tpw_launcher</string>\n"
    "\t<key>CFBundleIdentifier</key>\n\t<string>org.tpw-port.launcher</string>\n"
    "\t<key>CFBundleName</key>\n\t<string>STP/TPW Launcher</string>\n"
    "\t<key>CFBundleShortVersionString</key>\n\t<string>0.1.0</string>\n"
    "\t<key>NSHighResolutionCapable</key>\n\t<true/>\n"
    "</dict>\n</plist>\n";

void Names() {
    Check(std::string(GameAppName(true)) == "Sim Theme Park", "the US application's name");
    Check(std::string(GameAppName(false)) == "Theme Park World", "the European application's name");
    Check(GameBundleName(true) == "Sim Theme Park.app", "the US bundle's name");
    Check(GameBundleName(false) == "Theme Park World.app", "the European bundle's name");
}

void Plist() {
    Check(PlistString(kLauncherPlist, "CFBundleIdentifier") == "org.tpw-port.launcher", "PlistString reads a string");
    Check(PlistString(kLauncherPlist, "CFBundleVersion").empty(), "PlistString: a missing key is empty");
    Check(PlistString(kLauncherPlist, "NSHighResolutionCapable").empty(), "PlistString: a key with no string is empty");
    for (const bool us : {true, false}) {
        const std::string plist = GamePlist(us, kLauncherPlist);
        const std::string name = GameAppName(us);
        Check(plist.rfind("<?xml", 0) == 0 && plist.find("<plist version=\"1.0\">") != std::string::npos &&
                  plist.find("</plist>") != std::string::npos,
              name + ": a plist");
        Check(PlistString(plist, "CFBundleName") == name, name + ": CFBundleName");
        Check(PlistString(plist, "CFBundleDisplayName") == name, name + ": CFBundleDisplayName");
        Check(PlistString(plist, "CFBundleIdentifier") == std::string("org.tpw-port.launcher.") + (us ? "stp" : "tpw"),
              name + ": its own identifier, the launcher's with a suffix");
        Check(PlistString(plist, "CFBundleExecutable") == name, name + ": the program inside the bundle carries the game's name");
        Check(plist.find("tpw_launcher") == std::string::npos, name + ": the launcher's program name is nowhere in it");
        Check(PlistString(plist, "CFBundleShortVersionString") == "0.1.0" && PlistString(plist, "CFBundleVersion") == "0.1.0",
              name + ": the launcher's version");
        Check(PlistString(plist, "CFBundleIconFile") == "icon", name + ": the icon file");
        Check(PlistString(plist, "CFBundlePackageType") == "APPL", name + ": an application");
        Check(plist.find("<key>NSHighResolutionCapable</key>\n\t<true/>") != std::string::npos, name + ": high resolution");
        Check(plist.find("<string>STP/TPW Launcher</string>") == std::string::npos, name + ": the launcher's name is no value in it");
    }
    // No plist to go by (a program without one embedded): the defaults, still a usable plist.
    const std::string bare = GamePlist(true, "");
    Check(PlistString(bare, "CFBundleExecutable") == "Sim Theme Park" && PlistString(bare, "CFBundleIdentifier") == "org.tpw-port.launcher.stp" &&
              PlistString(bare, "CFBundleShortVersionString") == "0",
          "without a launcher plist: the defaults");
}

void Arguments() {
    const char* argv[] = {"/x/build/STP:TPW Launcher.app/Contents/MacOS/tpw_launcher", "--user", "B/user-a", "--mute",
                          "--window-click", "205,373@60", "--display", "builtin"};
    const int argc = static_cast<int>(sizeof argv / sizeof argv[0]);
    char** args = const_cast<char**>(argv);
    const std::vector<std::string> game = GameArguments(argc, args, false, "/x");
    const std::vector<std::string> want = {"--user", "B/user-a", "--mute", "--display", "builtin",
                                           "--play", "eur", "--from-launcher", "/x"};
    Check(game == want, "the game's arguments: the launcher's without its scripted click, then --play eur --from-launcher DIR");
    Check(GameArguments(argc, args, true, "/y").back() == "/y" && GameArguments(argc, args, true, "/y")[6] == "us",
          "the game's arguments: --play us, the folder given");
    Check(FromLauncherFolder(argc, args).empty(), "the launcher's own run is not from the launcher");
    // the game's process sees them with its own program first; the launcher gets its own back
    std::vector<std::string> gameRun = {"/x/build/Sim Theme Park.app/Contents/MacOS/tpw_launcher"};
    gameRun.insert(gameRun.end(), game.begin(), game.end());
    std::vector<char*> gameArgv;
    for (std::string& arg : gameRun) gameArgv.push_back(arg.data());
    const int gameArgc = static_cast<int>(gameArgv.size());
    Check(FromLauncherFolder(gameArgc, gameArgv.data()) == "/x", "the game's process knows the launcher's folder");
    const std::vector<std::string> back = LauncherArguments(gameArgc, gameArgv.data());
    Check(back == std::vector<std::string>(want.begin(), want.begin() + 5), "the launcher's arguments again, without --play and --from-launcher");
    // a --play in the middle, and one at the end with no value, both taken out
    const char* odd[] = {"p", "--play", "us", "--mute", "--from-launcher"};
    Check(LauncherArguments(5, const_cast<char**>(odd)) == std::vector<std::string>{"--mute"}, "an unfinished marker is taken out too");
    Check(std::string(kFromLauncherOption) == "--from-launcher", "the marker's name");
}

IconImage Checker(int size) {
    IconImage icon;
    icon.width = icon.height = size;
    icon.bitDepth = 32;
    icon.rgba.assign(static_cast<size_t>(size) * size * 4, 0);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            uint8_t* p = &icon.rgba[(static_cast<size_t>(y) * size + x) * 4];
            const bool left = x < size / 2, top = y < size / 2;
            p[0] = left ? 255 : 0;
            p[1] = top ? 255 : 0;
            p[2] = 40;
            p[3] = left && top ? 0 : 255;   // the top left quarter is clear
        }
    return icon;
}

void Resample() {
    const IconImage icon = Checker(4);
    const IconImage big = ResampleIcon(icon, 8);
    Check(big.width == 8 && big.height == 8 && big.rgba.size() == 8 * 8 * 4, "enlarged to 8 x 8");
    // each source pixel becomes a 2 x 2 block, unchanged
    bool blocks = true;
    for (int y = 0; y < 8 && blocks; ++y)
        for (int x = 0; x < 8; ++x) {
            const uint8_t* want = &icon.rgba[(static_cast<size_t>(y / 2) * 4 + x / 2) * 4];
            const uint8_t* got = &big.rgba[(static_cast<size_t>(y) * 8 + x) * 4];
            // a clear pixel keeps only its clearness (its colour is nothing)
            if (want[3] == 0 ? got[3] != 0 : std::memcmp(want, got, 4) != 0) { blocks = false; break; }
        }
    Check(blocks, "an enlargement by a whole number is exact (pixel blocks)");
    const IconImage small = ResampleIcon(icon, 2);
    Check(small.width == 2 && small.rgba.size() == 2 * 2 * 4, "reduced to 2 x 2");
    const uint8_t* tl = &small.rgba[0];
    const uint8_t* br = &small.rgba[(1 * 2 + 1) * 4];
    Check(tl[3] == 0, "the clear quarter stays clear");
    Check(br[0] == 0 && br[1] == 0 && br[2] == 40 && br[3] == 255, "a solid quarter keeps its colour");
    // half clear, half red: the colour is the red's (alpha-weighted), the alpha the average
    IconImage half;
    half.width = 2;
    half.height = 1;
    half.bitDepth = 32;
    half.rgba = {200, 10, 10, 255, 0, 0, 0, 0};
    const IconImage one = ResampleIcon(half, 1);
    Check(one.width == 1 && one.rgba.size() == 4 && one.rgba[0] == 200 && one.rgba[1] == 10 && one.rgba[3] == 128,
          "a clear pixel does not darken its neighbour's colour; the alpha is averaged");
    Check(ResampleIcon(IconImage{}, 16).width == 0, "an empty icon resamples to nothing");
}

void Icns() {
    const std::vector<uint8_t> file = EncodeIcns(Checker(32));
    Check(file.size() > 8 && std::memcmp(file.data(), "icns", 4) == 0, "an icns header");
    Check(Be32(file, 4) == file.size(), "the header's length is the file's");
    static const char* const kTypes[] = {"icp4", "ic11", "icp5", "ic12", "ic07", "ic13", "ic08", "ic14", "ic09", "ic10"};
    static const int kSizes[] = {16, 32, 32, 64, 128, 256, 256, 512, 512, 1024};
    size_t at = 8, n = 0;
    while (at + 8 <= file.size() && n < 10) {
        const uint32_t length = Be32(file, at + 4);
        Check(std::memcmp(&file[at], kTypes[n], 4) == 0, std::string("element ") + std::to_string(n) + " is " + kTypes[n]);
        Check(length > 8 && at + length <= file.size(), std::string("element ") + kTypes[n] + " fits");
        if (!(length > 8 && at + length <= file.size())) break;
        IconImage decoded;
        const bool png = tpw::assets::DecodeIconPng(&file[at + 8], length - 8, decoded, nullptr);
        Check(png && decoded.width == kSizes[n] && decoded.height == kSizes[n],
              std::string("element ") + kTypes[n] + " is a PNG of " + std::to_string(kSizes[n]) + " pixels");
        at += length;
        ++n;
    }
    Check(n == 10 && at == file.size(), "ten elements and nothing after them");
    Check(EncodeIcns(IconImage{}).empty(), "no icns without an icon");
}

}  // namespace

int main() {
    Names();
    Plist();
    Arguments();
    Resample();
    Icns();
    if (gFailures == 0) std::printf("game_app tests: all passed\n");
    else std::printf("game_app tests: %d FAILED\n", gFailures);
    return gFailures == 0 ? 0 : 1;
}
