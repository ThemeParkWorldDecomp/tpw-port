// asset_packs.cpp: see asset_packs.h. The zips are read with the importer's reader (tpw-decomp/assets), which leaves out
// names that climb out of the archive, so every file lands inside the pack's own folder.
#include "asset_packs.h"

#include <algorithm>
#include <cctype>
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
// In each unpacked folder: the size and time of the zip it was unpacked from.
constexpr char kStampFile[] = ".pack";

std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string StampOf(const fs::path& zip) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(zip, ec);
    const auto time = fs::last_write_time(zip, ec).time_since_epoch().count();
    return std::to_string(size) + " " + std::to_string(static_cast<long long>(time));
}

std::string ReadStamp(const fs::path& folder) {
    std::ifstream in(folder / kStampFile);
    std::string line;
    std::getline(in, line);
    return line;
}

// What a zip made on a desktop holds besides the files: a Mac's "__MACOSX" folder and "._" files, and ".DS_Store".
bool IsDesktopClutter(const std::string& path) {
    size_t start = 0;
    while (start < path.size()) {
        const size_t slash = std::min(path.find('/', start), path.size());
        if (path.compare(start, slash - start, "__MACOSX") == 0 || path[start] == '.') return true;
        start = slash + 1;
    }
    return false;
}

// A zip made of a folder holds everything under the folder's name: the length of one folder above "data" that every
// entry shares, with its slash, or 0.
size_t SharedFolderLength(const std::vector<const assets::Entry*>& entries) {
    if (entries.empty()) return 0;
    const std::string& first = entries.front()->path;
    const size_t slash = first.find('/');
    if (slash == std::string::npos || Lower(first.substr(0, slash)) == "data") return 0;
    const std::string folder = first.substr(0, slash + 1);
    for (const assets::Entry* entry : entries)
        if (entry->path.compare(0, folder.size(), folder) != 0) return 0;
    return folder.size();
}

// Unpacks the zip into `out` (emptied first) and writes the stamp last, so a folder without one is never used.
bool Unpack(const fs::path& zip, const fs::path& out, size_t& files, std::string& err) {
    std::shared_ptr<assets::Blob> blob = assets::OpenFileBlob(zip.string(), err);
    if (!blob) return false;
    std::unique_ptr<assets::Source> source = assets::OpenZip(blob, err);
    if (!source) return false;
    std::vector<const assets::Entry*> entries;
    for (const assets::Entry& entry : source->Entries())
        if (!IsDesktopClutter(entry.path)) entries.push_back(&entry);
    const size_t skip = SharedFolderLength(entries);
    std::error_code ec;
    fs::remove_all(out, ec);
    std::vector<char> buffer(1 << 16);
    for (const assets::Entry* entry : entries) {
        // A resource fork is kept as the game folder keeps one, in an ".rsrc" side file.
        const std::string path = entry->path.substr(skip) + (entry->kind == assets::EntryKind::Rsrc ? ".rsrc" : "");
        const fs::path target = out / path;
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
        if (it->is_regular_file(kind) && Lower(it->path().extension().string()) == ".zip") zips.push_back(it->path());
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
    for (const fs::path& path : stale) fs::remove_all(path, ec);

    std::vector<fs::path>& dirs = tpw::PackDirs();
    dirs.clear();
    for (const fs::path& zip : zips) {
        const fs::path out = unpacked / zip.stem();
        const std::string name = zip.filename().string();
        if (ReadStamp(out) != StampOf(zip)) {
            size_t files = 0;
            std::string err;
            if (!Unpack(zip, out, files, err)) {
                fs::remove_all(out, ec);
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
