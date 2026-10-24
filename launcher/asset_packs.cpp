// asset_packs.cpp: see asset_packs.h. The zips are read with the importer's reader (tpw-decomp/assets). Every file
// lands inside the pack's own folder: a name that would reach outside it is left out (PlaceInPack), and only a pack's
// own folder, directly inside ".unpacked", is ever emptied (RemovePackFolder).
#include "asset_packs.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "packs.h"
#include "platform.h"
#include "source.h"

namespace tpw::launcher {
namespace {

namespace fs = std::filesystem;

constexpr char kUnpackedFolder[] = ".unpacked";
// In each unpacked folder: the rules it was unpacked by, and the size and time of the zip it was unpacked from.
constexpr char kStampFile[] = ".pack";
// The rules, raised whenever they change (tpw::PackMayHold, LeftOutBecause), so a folder unpacked by older rules is
// unpacked again. The stamps of the first packs had none.
constexpr char kRules[] = "rules 2";
// The most a pack may unpack to (8 GB, over 25 times the game folder), so a zip that expands without end cannot fill
// the disk.
constexpr uint64_t kMaxPackBytes = uint64_t{8} << 30;

std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string StampOf(const fs::path& zip) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(zip, ec);
    const auto time = fs::last_write_time(zip, ec).time_since_epoch().count();
    return std::string(kRules) + " " + std::to_string(size) + " " + std::to_string(static_cast<long long>(time));
}

std::string ReadStamp(const fs::path& folder) {
    std::ifstream in(folder / kStampFile);
    std::string line;
    std::getline(in, line);
    return line;
}

// What a zip made on a desktop holds besides the files: a Mac's "__MACOSX" folder and "._" files, and ".DS_Store". They
// are left out without a log line.
bool IsDesktopClutter(const std::string& path) {
    size_t start = 0;
    while (start < path.size()) {
        const size_t slash = std::min(path.find('/', start), path.size());
        const std::string part = path.substr(start, slash - start);
        if (part == "__MACOSX" || part == ".DS_Store" || part.compare(0, 2, "._") == 0) return true;
        start = slash + 1;
    }
    return false;
}

// An entry's name without its "." folders, which some zip tools write ("./data/x.tga" is data/x.tga).
std::string WithoutDotFolders(const std::string& path) {
    std::string out;
    size_t start = 0;
    while (start < path.size()) {
        const size_t slash = std::min(path.find('/', start), path.size());
        const std::string part = path.substr(start, slash - start);
        if (!part.empty() && part != ".") out += (out.empty() ? "" : "/") + part;
        start = slash + 1;
    }
    return out;
}

// Whether a zip's name can name its unpacked folder: not empty and not starting with a dot. A stem of "." or ".."
// ("..zip", "...zip") would name ".unpacked" or the packs folder itself, and a dot name a hidden folder.
bool UsableStem(const std::string& stem) {
    return !stem.empty() && stem[0] != '.';
}

// Empties and removes `folder` only when it is a pack's own folder, directly inside `unpacked`.
void RemovePackFolder(const fs::path& unpacked, const fs::path& folder) {
    if (folder.parent_path() != unpacked || !UsableStem(folder.filename().string())) return;
    std::error_code ec;
    fs::remove_all(folder, ec);
}

// Where an entry goes inside `out`, or empty when its name would reach outside it: a drive or a root ("C:/x", "C:x",
// "/x"), a climb ("a/../../x") or a colon, which no game path can name, as it separates a game path's folders.
fs::path PlaceInPack(const fs::path& out, const std::string& path) {
    const fs::path name(path);
    if (path.empty() || path.find(':') != std::string::npos || name.has_root_name() || name.has_root_directory())
        return {};
    const fs::path target = (out / name).lexically_normal();
    const fs::path inside = target.lexically_relative(out.lexically_normal());
    if (inside.empty() || *inside.begin() == "..") return {};
    return target;
}

// Why a pack leaves out the file at `path` (from the game folder, '/' separated), or empty when it takes it: a folder in
// the path whose name holds a dot, or that is a file in the game folder, would hide that file from the game (the game
// would find "bfbin.wad" a folder), and the file must be one a pack may hold (tpw::PackMayHold).
std::string LeftOutBecause(const std::string& path) {
    size_t start = 0;
    for (size_t slash; (slash = path.find('/', start)) != std::string::npos; start = slash + 1) {
        if (path.find('.', start) < slash) return "a folder in its path has a dot in its name";
        if (tpw::IsGameFile(path.substr(0, slash))) return "a folder in its path is a file in the game folder";
    }
    if (!tpw::PackMayHold(path)) return "it is not a picture, sound, font or film the game keeps there";
    return {};
}

// An entry and its name without "." folders.
struct Item {
    const assets::Entry* entry;
    std::string path;
};

// A zip made of a folder holds everything under the folder's name: the length of one folder above "data" that every
// entry shares, with its slash, or 0.
size_t SharedFolderLength(const std::vector<Item>& items) {
    if (items.empty()) return 0;
    const std::string& first = items.front().path;
    const size_t slash = first.find('/');
    if (slash == std::string::npos || Lower(first.substr(0, slash)) == "data") return 0;
    const std::string folder = first.substr(0, slash + 1);
    for (const Item& item : items)
        if (item.path.compare(0, folder.size(), folder) != 0) return 0;
    return folder.size();
}

// Unpacks the zip into `out` (emptied first) and writes the stamp last, so a folder without one is never used.
bool Unpack(const fs::path& zip, const fs::path& unpacked, const fs::path& out, size_t& files, std::string& err) {
    std::shared_ptr<assets::Blob> blob = assets::OpenFileBlob(zip.string(), err);
    if (!blob) return false;
    std::unique_ptr<assets::Source> source = assets::OpenZip(blob, err);
    if (!source) return false;
    std::vector<Item> items;
    for (const assets::Entry& entry : source->Entries()) {
        std::string path = WithoutDotFolders(entry.path);
        if (!path.empty() && !IsDesktopClutter(path)) items.push_back({&entry, std::move(path)});
    }
    const size_t skip = SharedFolderLength(items);
    const std::string name = zip.filename().string();
    uint64_t total = 0;
    for (const Item& item : items) {
        if (item.entry->size > kMaxPackBytes - total) {
            err = "it unpacks to more than 8 GB";
            return false;
        }
        total += item.entry->size;
    }
    std::error_code ec;
    RemovePackFolder(unpacked, out);
    fs::create_directories(out, ec);
    std::vector<char> buffer(1 << 16);
    uint64_t written = 0;
    for (const Item& item : items) {
        const assets::Entry* entry = item.entry;
        // A resource fork is kept as the game folder keeps one, in an ".rsrc" side file.
        const std::string path = item.path.substr(skip) + (entry->kind == assets::EntryKind::Rsrc ? ".rsrc" : "");
        const fs::path target = PlaceInPack(out, path);
        if (target.empty()) {
            tpw::Log("asset pack: %s: \"%s\" left out, as its name reaches outside the pack", name.c_str(),
                     entry->path.c_str());
            continue;
        }
        const std::string why = LeftOutBecause(path);
        if (!why.empty()) {
            tpw::Log("asset pack: %s: \"%s\" left out, as %s", name.c_str(), entry->path.c_str(), why.c_str());
            continue;
        }
        fs::create_directories(target.parent_path(), ec);
        std::unique_ptr<assets::Stream> stream = source->Open(*entry, err);
        if (!stream) return false;
        std::FILE* file = std::fopen(target.string().c_str(), "wb");
        if (file == nullptr) {
            err = "cannot write " + target.string();
            return false;
        }
        int64_t read;
        while ((read = stream->Read(buffer.data(), buffer.size(), err)) > 0) {
            written += static_cast<uint64_t>(read);
            if (written > kMaxPackBytes) {   // (more than the zip said)
                read = -1;
                err = "it unpacks to more than 8 GB";
                break;
            }
            if (std::fwrite(buffer.data(), 1, static_cast<size_t>(read), file) != static_cast<size_t>(read)) {
                read = -1;
                err = "cannot write " + target.string();
                break;
            }
        }
        if (std::fclose(file) != 0 && read == 0) {
            read = -1;
            err = "cannot write " + target.string();
        }
        if (read < 0) return false;
        ++files;
    }
    std::ofstream(out / kStampFile) << StampOf(zip) << "\n";
    return true;
}

}  // namespace

void UsePacks(const fs::path& folder) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    std::vector<fs::path> zips;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code kind;
        if (!it->is_regular_file(kind) || Lower(it->path().extension().string()) != ".zip") continue;
        if (UsableStem(it->path().stem().string())) zips.push_back(it->path());
        else tpw::Log("asset pack: %s left out, as its name starts with a dot", it->path().filename().string().c_str());
    }
    std::sort(zips.begin(), zips.end());

    // The unpacked folders of zips that are gone go too.
    const fs::path unpacked = folder / kUnpackedFolder;
    std::vector<fs::path> stale;
    for (fs::directory_iterator it(unpacked, ec), end; !ec && it != end; it.increment(ec)) {
        const bool used = std::any_of(zips.begin(), zips.end(),
                                      [&](const fs::path& zip) { return zip.stem() == it->path().filename(); });
        if (!used) stale.push_back(it->path());
    }
    for (const fs::path& path : stale) RemovePackFolder(unpacked, path);

    std::vector<fs::path>& dirs = tpw::PackDirs();
    dirs.clear();
    for (const fs::path& zip : zips) {
        const fs::path out = unpacked / zip.stem();
        const std::string name = zip.filename().string();
        if (ReadStamp(out) != StampOf(zip)) {
            size_t files = 0;
            std::string err;
            if (!Unpack(zip, unpacked, out, files, err)) {
                RemovePackFolder(unpacked, out);
                tpw::Log("asset pack: %s left out: %s", name.c_str(), err.c_str());
                continue;
            }
            tpw::Log("asset pack: %s unpacked (%zu file%s)", name.c_str(), files, files == 1 ? "" : "s");
        }
        tpw::Log("asset pack: %s", name.c_str());
        dirs.insert(dirs.begin(), out);   // a later name is looked in first
    }
}

}  // namespace tpw::launcher
