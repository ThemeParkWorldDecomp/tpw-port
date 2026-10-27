// game_app_tests.cpp: the game's own application on a Mac and on Windows (launcher/game_app.h), the part that runs
// everywhere: the bundles' names, the Info.plist the launcher writes for them from its own, the arguments to the game's
// process and back, and the .icns the launcher writes from the icon (its elements, each a PNG of the right size); the
// Windows program's name, its command line, its version resource (read as the resource compiler writes one, and the
// game's made from the launcher's) and its icon resources.
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
    // the hand-over's test: its game gets the option, and the launcher opened again after it does not
    const char* test[] = {"tpw_launcher.exe", "--data", "D", "--game-app-test", "us", "--frames", "90"};
    const std::vector<std::string> testGame = GameArguments(7, const_cast<char**>(test), true, "C:\\w");
    Check(testGame == std::vector<std::string>{"--data", "D", "--frames", "90", "--game-app-test", "us", "--play", "us",
                                               "--from-launcher", "C:\\w"},
          "the test's game: --game-app-test kept, before --play");
    std::vector<std::string> testRun = {"Sim Theme Park.exe"};
    testRun.insert(testRun.end(), testGame.begin(), testGame.end());
    std::vector<char*> testArgv;
    for (std::string& arg : testRun) testArgv.push_back(arg.data());
    Check(LauncherArguments(static_cast<int>(testArgv.size()), testArgv.data()) ==
              std::vector<std::string>{"--data", "D", "--frames", "90"},
          "the launcher again after the test's game: no --game-app-test");
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

void WindowsNames() {
    Check(GameExeName(true) == "Sim Theme Park.exe" && GameExeName(false) == "Theme Park World.exe", "the programs' names");
    Check(GameAppId(true) == "org.tpw-port.launcher.stp" && GameAppId(false) == "org.tpw-port.launcher.tpw",
          "the identifiers, the Mac bundles' own");
    Check(GameAppId(true).find(' ') == std::string::npos && GameAppId(true).size() <= 128, "an AppUserModelID has no space and 128 characters at most");
}

void CommandLine() {
    Check(WindowsCommandLine({"C:\\x\\Sim Theme Park.exe", "--play", "us"}) == "\"C:\\x\\Sim Theme Park.exe\" --play us",
          "a path with spaces is quoted, a plain word is not");
    Check(WindowsCommandLine({"a", ""}) == "a \"\"", "an empty argument is a pair of quotes");
    Check(WindowsCommandLine({"C:\\a b\\"}) == "\"C:\\a b\\\\\"", "a backslash before the closing quote is doubled");
    Check(WindowsCommandLine({"say \"hi\""}) == "\"say \\\"hi\\\"\"", "a quote inside is escaped");
    Check(WindowsCommandLine({"a\\\\\"b c"}) == "\"a\\\\\\\\\\\"b c\"", "backslashes before a quote are doubled, then the quote escaped");
    Check(WindowsCommandLine({"C:\\no\\spaces\\"}) == "C:\\no\\spaces\\", "backslashes elsewhere are as they are");
}

// The launcher's version resource as llvm-mingw's resource compiler writes it from launcher/win/launcher.rc.in (0.1.1).
const uint8_t kLauncherGolden[] = {
    0x5c, 0x02, 0x34, 0x00, 0x00, 0x00, 0x56, 0x00, 0x53, 0x00, 0x5f, 0x00, 0x56, 0x00, 0x45, 0x00, 0x52, 0x00, 0x53, 0x00,
    0x49, 0x00, 0x4f, 0x00, 0x4e, 0x00, 0x5f, 0x00, 0x49, 0x00, 0x4e, 0x00, 0x46, 0x00, 0x4f, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xbd, 0x04, 0xef, 0xfe, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xbc, 0x01, 0x00, 0x00, 0x01, 0x00, 0x53, 0x00,
    0x74, 0x00, 0x72, 0x00, 0x69, 0x00, 0x6e, 0x00, 0x67, 0x00, 0x46, 0x00, 0x69, 0x00, 0x6c, 0x00, 0x65, 0x00, 0x49, 0x00,
    0x6e, 0x00, 0x66, 0x00, 0x6f, 0x00, 0x00, 0x00, 0x98, 0x01, 0x00, 0x00, 0x01, 0x00, 0x30, 0x00, 0x34, 0x00, 0x30, 0x00,
    0x39, 0x00, 0x30, 0x00, 0x34, 0x00, 0x42, 0x00, 0x30, 0x00, 0x00, 0x00, 0x58, 0x00, 0x18, 0x00, 0x01, 0x00, 0x46, 0x00,
    0x69, 0x00, 0x6c, 0x00, 0x65, 0x00, 0x44, 0x00, 0x65, 0x00, 0x73, 0x00, 0x63, 0x00, 0x72, 0x00, 0x69, 0x00, 0x70, 0x00,
    0x74, 0x00, 0x69, 0x00, 0x6f, 0x00, 0x6e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x00, 0x54, 0x00, 0x50, 0x00, 0x2f, 0x00,
    0x54, 0x00, 0x50, 0x00, 0x57, 0x00, 0x20, 0x00, 0x4c, 0x00, 0x61, 0x00, 0x75, 0x00, 0x6e, 0x00, 0x63, 0x00, 0x68, 0x00,
    0x65, 0x00, 0x72, 0x00, 0x20, 0x00, 0x76, 0x00, 0x30, 0x00, 0x2e, 0x00, 0x31, 0x00, 0x2e, 0x00, 0x31, 0x00, 0x00, 0x00,
    0x2c, 0x00, 0x06, 0x00, 0x01, 0x00, 0x46, 0x00, 0x69, 0x00, 0x6c, 0x00, 0x65, 0x00, 0x56, 0x00, 0x65, 0x00, 0x72, 0x00,
    0x73, 0x00, 0x69, 0x00, 0x6f, 0x00, 0x6e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x2e, 0x00, 0x31, 0x00, 0x2e, 0x00,
    0x31, 0x00, 0x00, 0x00, 0x3a, 0x00, 0x0d, 0x00, 0x01, 0x00, 0x49, 0x00, 0x6e, 0x00, 0x74, 0x00, 0x65, 0x00, 0x72, 0x00,
    0x6e, 0x00, 0x61, 0x00, 0x6c, 0x00, 0x4e, 0x00, 0x61, 0x00, 0x6d, 0x00, 0x65, 0x00, 0x00, 0x00, 0x74, 0x00, 0x70, 0x00,
    0x77, 0x00, 0x5f, 0x00, 0x6c, 0x00, 0x61, 0x00, 0x75, 0x00, 0x6e, 0x00, 0x63, 0x00, 0x68, 0x00, 0x65, 0x00, 0x72, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x4a, 0x00, 0x11, 0x00, 0x01, 0x00, 0x4f, 0x00, 0x72, 0x00, 0x69, 0x00, 0x67, 0x00, 0x69, 0x00,
    0x6e, 0x00, 0x61, 0x00, 0x6c, 0x00, 0x46, 0x00, 0x69, 0x00, 0x6c, 0x00, 0x65, 0x00, 0x6e, 0x00, 0x61, 0x00, 0x6d, 0x00,
    0x65, 0x00, 0x00, 0x00, 0x74, 0x00, 0x70, 0x00, 0x77, 0x00, 0x5f, 0x00, 0x6c, 0x00, 0x61, 0x00, 0x75, 0x00, 0x6e, 0x00,
    0x63, 0x00, 0x68, 0x00, 0x65, 0x00, 0x72, 0x00, 0x2e, 0x00, 0x65, 0x00, 0x78, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x42, 0x00, 0x11, 0x00, 0x01, 0x00, 0x50, 0x00, 0x72, 0x00, 0x6f, 0x00, 0x64, 0x00, 0x75, 0x00, 0x63, 0x00, 0x74, 0x00,
    0x4e, 0x00, 0x61, 0x00, 0x6d, 0x00, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x00, 0x54, 0x00, 0x50, 0x00, 0x2f, 0x00,
    0x54, 0x00, 0x50, 0x00, 0x57, 0x00, 0x20, 0x00, 0x4c, 0x00, 0x61, 0x00, 0x75, 0x00, 0x6e, 0x00, 0x63, 0x00, 0x68, 0x00,
    0x65, 0x00, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x06, 0x00, 0x01, 0x00, 0x50, 0x00, 0x72, 0x00, 0x6f, 0x00,
    0x64, 0x00, 0x75, 0x00, 0x63, 0x00, 0x74, 0x00, 0x56, 0x00, 0x65, 0x00, 0x72, 0x00, 0x73, 0x00, 0x69, 0x00, 0x6f, 0x00,
    0x6e, 0x00, 0x00, 0x00, 0x30, 0x00, 0x2e, 0x00, 0x31, 0x00, 0x2e, 0x00, 0x31, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x56, 0x00, 0x61, 0x00, 0x72, 0x00, 0x46, 0x00, 0x69, 0x00, 0x6c, 0x00, 0x65, 0x00, 0x49, 0x00, 0x6e, 0x00,
    0x66, 0x00, 0x6f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x00, 0x04, 0x00, 0x00, 0x00, 0x54, 0x00, 0x72, 0x00, 0x61, 0x00,
    0x6e, 0x00, 0x73, 0x00, 0x6c, 0x00, 0x61, 0x00, 0x74, 0x00, 0x69, 0x00, 0x6f, 0x00, 0x6e, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x09, 0x04, 0xb0, 0x04,
};

void Version() {
    VersionResource launcher;
    for (uint32_t word : {0xFEEF04BDu, 0x00010000u, 0x00000001u, 0x00010000u, 0x00000001u, 0x00010000u, 0x3Fu, 0u, 0x40004u,
                          1u, 0u, 0u, 0u})
        for (int b = 0; b < 4; ++b) launcher.fixed.push_back(static_cast<uint8_t>(word >> (8 * b)));
    launcher.strings = {{"FileDescription", "STP/TPW Launcher v0.1.1"}, {"FileVersion", "0.1.1"},
                        {"InternalName", "tpw_launcher"},                {"OriginalFilename", "tpw_launcher.exe"},
                        {"ProductName", "STP/TPW Launcher"},             {"ProductVersion", "0.1.1"}};
    const std::vector<uint8_t> blob = EncodeVersionResource(launcher);
    Check(blob.size() >= 6 && static_cast<size_t>(blob[0] | (blob[1] << 8)) == blob.size(), "the resource's length is its size");
    Check(blob.size() > 40 && blob[2] == 52 && blob[3] == 0, "the fixed part's 52 bytes");
    Check(blob.size() == sizeof kLauncherGolden && std::memcmp(blob.data(), kLauncherGolden, blob.size()) == 0,
          "written byte for byte as the resource compiler writes it");
    VersionResource read;
    Check(ParseVersionResource(std::vector<uint8_t>(kLauncherGolden, kLauncherGolden + sizeof kLauncherGolden), read),
          "the resource compiler's resource reads");
    Check(read.fixed == launcher.fixed && read.strings == launcher.strings && read.table == "040904B0" &&
              read.translation == 0x04B00409,
          "every part of it read back");
    Check(VersionString(read, "OriginalFilename") == "tpw_launcher.exe" && VersionString(read, "Comments").empty(),
          "VersionString finds a value, and an absent one is empty");
    for (const bool us : {true, false}) {
        const std::string name = GameAppName(us);
        const VersionResource game = GameVersionResource(us, read, "12345 bytes");
        VersionResource back;
        Check(ParseVersionResource(EncodeVersionResource(game), back) && back.strings == game.strings && back.fixed == read.fixed,
              name + ": the game's resource reads back");
        Check(VersionString(back, "FileDescription") == name, name + ": the Task Manager's name for it");
        Check(VersionString(back, "ProductName") == name && VersionString(back, "InternalName") == name, name + ": its product name");
        Check(VersionString(back, "OriginalFilename") == GameExeName(us), name + ": its file's name");
        Check(VersionString(back, "FileVersion") == "0.1.1" && VersionString(back, "ProductVersion") == "0.1.1",
              name + ": the launcher's version");
        Check(VersionString(back, kGameExeStampKey) == "12345 bytes", name + ": the stamp it was made from");
        Check(!VersionString(back, "Comments").empty(), name + ": a line saying what the file is");
        bool launcherNamed = false;
        for (const auto& entry : back.strings) launcherNamed = launcherNamed || entry.second.find("STP/TPW Launcher") != std::string::npos;
        Check(!launcherNamed, name + ": the launcher's name is no value in it");
    }
    VersionResource none;
    const VersionResource bare = GameVersionResource(true, none, "s");
    Check(bare.fixed.size() == 52 && VersionString(bare, "FileVersion") == "0", "without the launcher's resource: version 0");
    VersionResource accented;
    accented.strings = {{"Comments", "Caf\xc3\xa9 \xf0\x9f\x8e\xa2"}};
    VersionResource accentedBack;
    Check(ParseVersionResource(EncodeVersionResource(accented), accentedBack) && accentedBack.strings == accented.strings,
          "text beyond ASCII, a surrogate pair too, reads back");
    Check(!ParseVersionResource({1, 2, 3}, accentedBack) && !ParseVersionResource({}, accentedBack), "a few bytes are no resource");
}

void IconGroup() {
    const std::vector<uint8_t> ico = tpw::assets::EncodeIco(ResampleIcon(Checker(32), 256));
    IconResources res;
    Check(IconResourcesFromIco(ico, 1, res), "an .ico becomes resources");
    const size_t count = ico.size() >= 6 ? static_cast<size_t>(ico[4] | (ico[5] << 8)) : 0;
    Check(count == 7 && res.images.size() == count, "a picture each for 16, 24, 32, 48, 64, 128 and 256 pixels");
    Check(res.group.size() == 6 + 14 * count, "the group: a header and fourteen bytes a picture");
    bool same = res.group.size() == 6 + 14 * count;
    for (size_t i = 0; same && i < count; ++i) {
        const size_t g = 6 + 14 * i, e = 6 + 16 * i;
        const uint32_t size = ico[e + 8] | (ico[e + 9] << 8) | (ico[e + 10] << 16) | (static_cast<uint32_t>(ico[e + 11]) << 24);
        const uint32_t offset = ico[e + 12] | (ico[e + 13] << 8) | (ico[e + 14] << 16) | (static_cast<uint32_t>(ico[e + 15]) << 24);
        same = std::memcmp(&res.group[g], &ico[e], 12) == 0 && static_cast<size_t>(res.group[g + 12] | (res.group[g + 13] << 8)) == 1 + i &&
               res.images[i].size() == size && std::memcmp(res.images[i].data(), &ico[offset], size) == 0;
    }
    Check(same, "each entry as the file's, with its resource ID, and each picture its bytes");
    Check(!IconResourcesFromIco({0, 0, 1, 0, 1, 0}, 1, res) && res.images.empty(), "a cut-off .ico is refused");
}

}  // namespace

int main() {
    Names();
    Plist();
    Arguments();
    Resample();
    Icns();
    WindowsNames();
    CommandLine();
    Version();
    IconGroup();
    if (gFailures == 0) std::printf("game_app tests: all passed\n");
    else std::printf("game_app tests: %d FAILED\n", gFailures);
    return gFailures == 0 ? 0 : 1;
}
