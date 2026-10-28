// game_app.cpp: the game's own application on macOS and Windows (game_app.h), the part that runs everywhere: the
// bundle's names, its Info.plist, the arguments both ways and the .icns; the program's name, its version resource and its
// icon resources, and the command line. The macOS calls are in game_app_mac.cpp, the Windows ones in game_app_win.cpp.
#include "game_app.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

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

bool AppRunsProgram(const std::string& app, const std::string& program) {
    namespace fs = std::filesystem;
    const fs::path contents = fs::path(app) / "Contents";
    std::ifstream in(contents / "Info.plist", std::ios::binary);
    if (!in) return false;
    std::ostringstream plist;
    plist << in.rdbuf();
    const std::string named = PlistString(plist.str(), "CFBundleExecutable");
    if (named.empty() || named.find('/') != std::string::npos) return false;
    std::error_code ec;
    return fs::equivalent(contents / "MacOS" / named, program, ec) && !ec;   // links followed: a missing end is false
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
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], kGameAppTestOption) == 0) {
            args.push_back(argv[i]);
            args.push_back(argv[i + 1]);
            break;
        }
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
            std::strcmp(argv[i], "--window-click") == 0 || std::strcmp(argv[i], kGameAppTestOption) == 0) {
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

std::string GameAppId(bool us) { return std::string("org.tpw-port.launcher") + (us ? ".stp" : ".tpw"); }

std::string GameExeName(bool us) { return std::string(GameAppName(us)) + ".exe"; }

std::string WindowsCommandLine(const std::vector<std::string>& args) {
    std::string line;
    for (const std::string& arg : args) {
        if (!line.empty()) line += ' ';
        if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) {
            line += arg;
            continue;
        }
        line += '"';
        size_t slashes = 0;
        for (const char c : arg) {
            if (c == '\\') {
                ++slashes;
                continue;
            }
            // backslashes before a quote are doubled, and the quote escaped; elsewhere they are as they are
            line.append(c == '"' ? slashes * 2 + 1 : slashes, '\\');
            slashes = 0;
            line += c;
        }
        line.append(slashes * 2, '\\');   // before the closing quote
        line += '"';
    }
    return line;
}

namespace {

// UTF-8 to UTF-16 and back, for the version resource's text (a bad byte becomes U+FFFD).
std::u16string Utf16(const std::string& text) {
    std::u16string out;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t code = 0xFFFD;
        size_t length = 1;
        if (c < 0x80) code = c;
        else if ((c >> 5) == 6) code = c & 0x1F, length = 2;
        else if ((c >> 4) == 14) code = c & 0x0F, length = 3;
        else if ((c >> 3) == 30) code = c & 0x07, length = 4;
        if (length > 1) {
            if (i + length > text.size()) length = 1, code = 0xFFFD;
            for (size_t k = 1; k < length; ++k) {
                const unsigned char next = static_cast<unsigned char>(text[i + k]);
                if ((next >> 6) != 2) {
                    length = 1, code = 0xFFFD;
                    break;
                }
                code = (code << 6) | (next & 0x3F);
            }
        }
        i += length;
        if (code >= 0x10000) {
            code -= 0x10000;
            out += static_cast<char16_t>(0xD800 + (code >> 10));
            out += static_cast<char16_t>(0xDC00 + (code & 0x3FF));
        } else {
            out += static_cast<char16_t>(code);
        }
    }
    return out;
}

std::string Utf8(const std::u16string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t code = text[i];
        if (code >= 0xD800 && code < 0xDC00 && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000)
            code = 0x10000 + ((code - 0xD800) << 10) + (text[++i] - 0xDC00);
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }
    return out;
}

uint16_t Le16(const std::vector<uint8_t>& bytes, size_t at) { return static_cast<uint16_t>(bytes[at] | (bytes[at + 1] << 8)); }
uint32_t Le32(const std::vector<uint8_t>& bytes, size_t at) {
    return static_cast<uint32_t>(Le16(bytes, at)) | (static_cast<uint32_t>(Le16(bytes, at + 2)) << 16);
}
void PutLe16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}
void PutLe32(std::vector<uint8_t>& out, uint32_t v) {
    PutLe16(out, static_cast<uint16_t>(v));
    PutLe16(out, static_cast<uint16_t>(v >> 16));
}
void Align4(std::vector<uint8_t>& out) {
    while (out.size() % 4 != 0) out.push_back(0);
}

// One block of a version resource: wLength, wValueLength, wType (1: the value is text), the key, padding to four bytes,
// the value, padding, the children. Offsets count from the resource's start, which is four-byte aligned.
struct VersionNode {
    std::u16string key;
    bool text = false;
    std::vector<uint8_t> value;   // as stored (text: UTF-16LE with its terminating zero)
    std::vector<VersionNode> children;
};

bool ParseNode(const std::vector<uint8_t>& blob, size_t at, size_t end, VersionNode& node, size_t& next) {
    if (at + 6 > end) return false;
    const size_t length = Le16(blob, at), valueLength = Le16(blob, at + 2), type = Le16(blob, at + 4);
    if (length < 6 || at + length > end || type > 1) return false;
    const size_t stop = at + length;
    size_t p = at + 6;
    node.key.clear();
    for (;; p += 2) {
        if (p + 2 > stop) return false;
        const char16_t c = static_cast<char16_t>(Le16(blob, p));
        if (c == 0) break;
        node.key += c;
    }
    p = (p + 2 + 3) & ~size_t(3);
    node.text = type == 1;
    // Text values count characters; some tools count bytes, so a text value is read up to its zero instead.
    size_t valueBytes = node.text ? valueLength * 2 : valueLength;
    if (node.text && valueLength > 0) {
        size_t q = p;
        while (q + 2 <= stop && Le16(blob, q) != 0) q += 2;
        valueBytes = std::min(stop, q + 2) - p;
    }
    if (p + valueBytes > stop) valueBytes = stop > p ? stop - p : 0;
    node.value.assign(blob.begin() + static_cast<long>(p), blob.begin() + static_cast<long>(p + valueBytes));
    p = (p + valueBytes + 3) & ~size_t(3);
    node.children.clear();
    while (p + 6 <= stop) {
        VersionNode child;
        size_t after = 0;
        if (!ParseNode(blob, p, stop, child, after)) return false;
        node.children.push_back(std::move(child));
        p = (after + 3) & ~size_t(3);
    }
    next = stop;
    return true;
}

void EncodeNode(const VersionNode& node, std::vector<uint8_t>& out) {
    const size_t at = out.size();
    PutLe16(out, 0);
    PutLe16(out, static_cast<uint16_t>(node.text ? node.value.size() / 2 : node.value.size()));
    PutLe16(out, node.text ? 1 : 0);
    for (const char16_t c : node.key) PutLe16(out, static_cast<uint16_t>(c));
    PutLe16(out, 0);
    Align4(out);
    out.insert(out.end(), node.value.begin(), node.value.end());
    for (const VersionNode& child : node.children) {
        Align4(out);
        EncodeNode(child, out);
    }
    const size_t length = out.size() - at;
    out[at] = static_cast<uint8_t>(length);
    out[at + 1] = static_cast<uint8_t>(length >> 8);
}

std::vector<uint8_t> TextValue(const std::string& text) {
    std::vector<uint8_t> value;
    for (const char16_t c : Utf16(text)) PutLe16(value, static_cast<uint16_t>(c));
    PutLe16(value, 0);
    return value;
}

std::string TextOf(const std::vector<uint8_t>& value) {
    std::u16string text;
    for (size_t i = 0; i + 1 < value.size(); i += 2) {
        const char16_t c = static_cast<char16_t>(Le16(value, i));
        if (c == 0) break;
        text += c;
    }
    return Utf8(text);
}

}  // namespace

bool ParseVersionResource(const std::vector<uint8_t>& blob, VersionResource& out) {
    VersionNode root;
    size_t next = 0;
    if (!ParseNode(blob, 0, blob.size(), root, next) || root.key != u"VS_VERSION_INFO") return false;
    out = VersionResource();
    if (root.value.size() == 52 && Le32(root.value, 0) == 0xFEEF04BD) out.fixed = root.value;
    for (const VersionNode& info : root.children) {
        if (info.key == u"StringFileInfo" && !info.children.empty()) {
            const VersionNode& table = info.children.front();
            out.table = Utf8(table.key);
            for (const VersionNode& entry : table.children) out.strings.emplace_back(Utf8(entry.key), TextOf(entry.value));
        } else if (info.key == u"VarFileInfo") {
            for (const VersionNode& var : info.children)
                if (var.key == u"Translation" && var.value.size() >= 4) out.translation = Le32(var.value, 0);
        }
    }
    return true;
}

std::vector<uint8_t> EncodeVersionResource(const VersionResource& resource) {
    VersionNode root;
    root.key = u"VS_VERSION_INFO";
    root.value = resource.fixed;
    VersionNode info;
    info.key = u"StringFileInfo";
    info.text = true;
    VersionNode table;
    table.key = Utf16(resource.table);
    table.text = true;
    for (const auto& [key, value] : resource.strings) {
        VersionNode entry;
        entry.key = Utf16(key);
        entry.text = true;
        entry.value = TextValue(value);
        table.children.push_back(std::move(entry));
    }
    info.children.push_back(std::move(table));
    root.children.push_back(std::move(info));
    VersionNode vars;
    vars.key = u"VarFileInfo";
    vars.text = true;
    VersionNode translation;
    translation.key = u"Translation";
    PutLe32(translation.value, resource.translation);
    vars.children.push_back(std::move(translation));
    root.children.push_back(std::move(vars));
    std::vector<uint8_t> out;
    EncodeNode(root, out);
    return out;
}

std::string VersionString(const VersionResource& resource, const char* key) {
    for (const auto& [name, value] : resource.strings)
        if (name == key) return value;
    return "";
}

void SetVersionString(VersionResource& resource, const char* key, const std::string& value) {
    for (auto& [name, current] : resource.strings)
        if (name == key) {
            current = value;
            return;
        }
    resource.strings.emplace_back(key, value);
}

VersionResource GameVersionResource(bool us, const VersionResource& launcher, const std::string& stamp) {
    VersionResource game = launcher;
    const std::string name = GameAppName(us);
    if (game.fixed.size() != 52) {   // a launcher built without its resources: version 0, an application
        game.fixed.clear();
        for (uint32_t word : {0xFEEF04BDu, 0x00010000u, 0u, 0u, 0u, 0u, 0x3Fu, 0u, 0x40004u, 1u, 0u, 0u, 0u}) PutLe32(game.fixed, word);
    }
    SetVersionString(game, "FileDescription", name);   // what the Task Manager's Processes tab names the process
    SetVersionString(game, "InternalName", name);
    SetVersionString(game, "OriginalFilename", GameExeName(us));
    SetVersionString(game, "ProductName", name);
    if (VersionString(game, "FileVersion").empty()) SetVersionString(game, "FileVersion", "0");
    if (VersionString(game, "ProductVersion").empty()) SetVersionString(game, "ProductVersion", "0");
    SetVersionString(game, "Comments",
                     "The game's program, written by the launcher beside tpw_launcher.exe when its window's Play starts the "
                     "game: the launcher's program under the game's name, with the game's icon");
    SetVersionString(game, kGameExeStampKey, stamp);
    return game;
}

bool IconResourcesFromIco(const std::vector<uint8_t>& ico, uint16_t firstId, IconResources& out) {
    out = IconResources();
    if (ico.size() < 6 || Le16(ico, 0) != 0 || Le16(ico, 2) != 1) return false;
    const size_t count = Le16(ico, 4);
    if (count == 0 || ico.size() < 6 + 16 * count) return false;
    PutLe16(out.group, 0);
    PutLe16(out.group, 1);
    PutLe16(out.group, static_cast<uint16_t>(count));
    for (size_t i = 0; i < count; ++i) {
        const size_t entry = 6 + 16 * i;
        const uint32_t size = Le32(ico, entry + 8), offset = Le32(ico, entry + 12);
        if (size == 0 || offset > ico.size() || size > ico.size() - offset) {
            out = IconResources();
            return false;
        }
        // GRPICONDIRENTRY: the ICONDIRENTRY's first twelve bytes, then the picture's resource ID for its offset
        out.group.insert(out.group.end(), ico.begin() + static_cast<long>(entry), ico.begin() + static_cast<long>(entry + 12));
        PutLe16(out.group, static_cast<uint16_t>(firstId + i));
        out.images.emplace_back(ico.begin() + static_cast<long>(offset), ico.begin() + static_cast<long>(offset + size));
    }
    return true;
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
