// game_app.cpp: the game's own application on macOS (game_app.h), the part that runs everywhere: the bundle's names,
// its Info.plist, the arguments both ways and the .icns. The macOS calls are in game_app_mac.cpp.
#include "game_app.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tpw::launcher {

using tpw::assets::IconImage;

const char* GameAppName(bool us) { return us ? "Sim Theme Park" : "Theme Park World"; }

std::string GameBundleName(bool us) { return std::string(GameAppName(us)) + ".app"; }

std::string PlistString(const std::string& plist, const char* key) {
    const std::string tag = std::string("<key>") + key + "</key>";
    const size_t at = plist.find(tag);
    if (at == std::string::npos) return "";
    const size_t open = plist.find("<string>", at + tag.size());
    const size_t nextKey = plist.find("<key>", at + tag.size());
    if (open == std::string::npos || (nextKey != std::string::npos && nextKey < open)) return "";
    const size_t close = plist.find("</string>", open);
    if (close == std::string::npos) return "";
    return plist.substr(open + 8, close - open - 8);
}

std::string GamePlist(bool us, const std::string& launcherPlist) {
    std::string identifier = PlistString(launcherPlist, "CFBundleIdentifier");
    std::string version = PlistString(launcherPlist, "CFBundleShortVersionString");
    if (identifier.empty()) identifier = "org.tpw-port.launcher";
    if (version.empty()) version = "0";
    identifier += us ? ".stp" : ".tpw";
    const char* name = GameAppName(us);
    // The program inside the bundle carries the game's name too (the launcher's program hard-linked under it, KeepBundle):
    // the Dock, Cmd-Tab and Force Quit go by CFBundleName, but ps, Activity Monitor and System Events name a process
    // after its program, and would say tpw_launcher otherwise.
    const std::string executable = name;
    std::string text;
    text += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";   // no DOCTYPE line: macOS needs none, and its address is a URL
    text += "<!-- The game's application, written by the launcher beside its own (launcher/game_app.h): its program is the\n";
    text += "     launcher's, hard-linked under the game's name, and the Dock, Cmd-Tab, Force Quit and Activity Monitor name\n";
    text += "     the game being played after this file. -->\n";
    text += "<plist version=\"1.0\">\n<dict>\n";
    auto pair = [&text](const char* key, const std::string& value) {
        text += "\t<key>";
        text += key;
        text += "</key>\n\t<string>";
        text += value;
        text += "</string>\n";
    };
    pair("CFBundleDevelopmentRegion", "en");
    pair("CFBundleExecutable", executable);
    pair("CFBundleIconFile", "icon");
    pair("CFBundleIdentifier", identifier);
    pair("CFBundleInfoDictionaryVersion", "6.0");
    pair("CFBundleName", name);
    pair("CFBundleDisplayName", name);
    pair("CFBundlePackageType", "APPL");
    pair("CFBundleShortVersionString", version);
    pair("CFBundleVersion", version);
    text += "\t<key>NSHighResolutionCapable</key>\n\t<true/>\n";
    text += "\t<key>NSSupportsAutomaticGraphicsSwitching</key>\n\t<true/>\n";
    text += "</dict>\n</plist>\n";
    return text;
}

std::vector<std::string> GameArguments(int argc, char** argv, bool us, const std::string& workingFolder) {
    std::vector<std::string> args = LauncherArguments(argc, argv);
    args.push_back("--play");
    args.push_back(us ? "us" : "eur");
    args.push_back(kFromLauncherOption);
    args.push_back(workingFolder);
    return args;
}

std::vector<std::string> LauncherArguments(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--play") == 0 || std::strcmp(argv[i], kFromLauncherOption) == 0 ||
            std::strcmp(argv[i], "--window-click") == 0) {
            if (i + 1 < argc) ++i;   // its value
            continue;
        }
        args.push_back(argv[i]);
    }
    return args;
}

std::string FromLauncherFolder(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], kFromLauncherOption) == 0) return argv[i + 1];
    return "";
}

IconImage ResampleIcon(const IconImage& icon, int size) {
    IconImage out;
    out.width = out.height = size;
    out.bitDepth = icon.bitDepth;
    if (size <= 0 || icon.width <= 0 || icon.height <= 0 || icon.rgba.size() < static_cast<size_t>(icon.width) * icon.height * 4) {
        out.width = out.height = 0;
        return out;
    }
    out.rgba.assign(static_cast<size_t>(size) * size * 4, 0);
    const double sx = static_cast<double>(icon.width) / size, sy = static_cast<double>(icon.height) / size;
    for (int y = 0; y < size; ++y) {
        const double y0 = y * sy, y1 = (y + 1) * sy;
        for (int x = 0; x < size; ++x) {
            const double x0 = x * sx, x1 = (x + 1) * sx;
            double r = 0, g = 0, b = 0, a = 0, area = 0;
            for (int iy = static_cast<int>(std::floor(y0)); iy < icon.height && iy < y1; ++iy) {
                const double wy = std::min(y1, iy + 1.0) - std::max(y0, static_cast<double>(iy));
                if (wy <= 0) continue;
                for (int ix = static_cast<int>(std::floor(x0)); ix < icon.width && ix < x1; ++ix) {
                    const double wx = std::min(x1, ix + 1.0) - std::max(x0, static_cast<double>(ix));
                    if (wx <= 0) continue;
                    const double w = wx * wy;
                    const uint8_t* p = &icon.rgba[(static_cast<size_t>(iy) * icon.width + ix) * 4];
                    const double alpha = p[3] / 255.0;
                    r += p[0] * alpha * w;
                    g += p[1] * alpha * w;
                    b += p[2] * alpha * w;
                    a += alpha * w;
                    area += w;
                }
            }
            uint8_t* q = &out.rgba[(static_cast<size_t>(y) * size + x) * 4];
            if (a <= 0 || area <= 0) continue;   // fully clear
            auto byte = [](double v) { return static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L)); };
            q[0] = byte(r / a);
            q[1] = byte(g / a);
            q[2] = byte(b / a);
            q[3] = byte(a / area * 255.0);
        }
    }
    return out;
}

std::vector<uint8_t> EncodeIcns(const IconImage& icon) {
    struct Element { const char* type; int size; };
    static const Element kElements[] = {{"icp4", 16},  {"ic11", 32},  {"icp5", 32},  {"ic12", 64},  {"ic07", 128},
                                        {"ic13", 256}, {"ic08", 256}, {"ic14", 512}, {"ic09", 512}, {"ic10", 1024}};
    std::vector<uint8_t> file(8, 0);
    std::memcpy(file.data(), "icns", 4);
    auto be32 = [](std::vector<uint8_t>& out, size_t at, uint32_t v) {
        out[at] = static_cast<uint8_t>(v >> 24);
        out[at + 1] = static_cast<uint8_t>(v >> 16);
        out[at + 2] = static_cast<uint8_t>(v >> 8);
        out[at + 3] = static_cast<uint8_t>(v);
    };
    for (const Element& element : kElements) {
        const IconImage scaled = ResampleIcon(icon, element.size);
        if (scaled.width == 0) return {};
        const std::vector<uint8_t> png = tpw::assets::EncodeIconPng(scaled, "");
        if (png.empty()) return {};
        const size_t at = file.size();
        file.resize(at + 8);
        std::memcpy(&file[at], element.type, 4);
        be32(file, at + 4, static_cast<uint32_t>(png.size() + 8));
        file.insert(file.end(), png.begin(), png.end());
    }
    be32(file, 4, static_cast<uint32_t>(file.size()));
    return file;
}

}  // namespace tpw::launcher
