// msl_ifstream.cpp: the std::ifstream of SwearFilter::LoadWordList (src/game/online/IPBase.cpp,
// unit v5094) on the host's file I/O. The translation keeps the Mac's stream object (SwearIfstream
// in include/FS_MasterSave.h) and reaches MSL's basic_ifstream through the swear_callee shims
// declared there. Until 2026-10-20 the launcher's generated stand-ins answered them with 0, so the
// stream was used unconstructed and the park entry crashed in LoadWordList (CFSGlobalMasterSave::Load
// reads the word lists when the master save's swear filter flag is on). These are the shims.
//
// The word files (data:Language:<language>:swears.txt and alloweds.txt) are UTF-16 little-endian
// text, XORed with 0xffff, with CR LF line ends: PC files shipped unchanged on the Mac disc. The
// original loads each halfword big-endian (lhz) and compares it with 0xa, so on the Mac it never
// saw a line end in these files and read on past its 0xac-byte buffer (tests/test_SwearFilter_Filter.py
// notes the overrun). Here open() reads the whole file and, when its line ends are little-endian,
// swaps every pair, so read() hands the translated loop the big-endian halfwords it was written for
// and the words come out right. A big-endian file is handed over as it is.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

#include "FS_MasterSave.h"
#include "platform.h"
#include "path_text.h"

namespace {

struct OpenFile {
    std::vector<uint8_t> bytes;
    size_t at = 0;
};

// The open files by the filebuf that holds them: SwearFilebuf keeps one 32-bit word (the Mac's
// FILE*), which only tells the loop whether the file is open.
std::unordered_map<const SwearFilebuf*, OpenFile>& Files() {
    static auto* files = new std::unordered_map<const SwearFilebuf*, OpenFile>;
    return *files;
}

// True when the first line end is a little-endian LF (0a 00 at an even offset); a big-endian file
// has 00 0a there. Without a line end the order does not matter to the loop.
bool LittleEndian(const std::vector<uint8_t>& bytes) {
    for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
        if (bytes[i] == 0x0a && bytes[i + 1] == 0x00) return true;
        if (bytes[i] == 0x00 && bytes[i + 1] == 0x0a) return false;
    }
    return false;
}

}  // namespace

namespace swear_callee {

// basic_ifstream(): a stream with its filebuf (FailureCheck adds badbit to a stream without one),
// no state bits and no exception mask. The vtable words are the translation's business (its
// Destroy writes them before the destructors run); nothing reads them here.
void IfstreamCtor(SwearIfstream* s, int32_t) {
    std::memset(s, 0, sizeof *s);
    s->mIos = &s->mIosStorage;
    s->mIosStorage.mRdbuf = 1;
    s->mIosStorage.mOffset = reinterpret_cast<char*>(&s->mIosStorage) - reinterpret_cast<char*>(s);
    Files().erase(&s->mFilebuf);
}

// basic_filebuf::open(path, mode): the game path is mapped like every other file the game opens
// (the user folder's copy first, then the game folder's, names matched ignoring case). NULL when
// the filebuf is already open or the file cannot be read; the mode is always in | binary here.
SwearFilebuf* FilebufOpen(SwearFilebuf* buf, const char* path, int32_t) {
    if (buf->mFile != 0) return nullptr;
    const std::filesystem::path host = tpw::FindHostPath(path);
    if (host.empty()) return nullptr;
    std::FILE* file = std::fopen(tpw::PathText(host).c_str(), "rb");
    if (file == nullptr) return nullptr;
    OpenFile open;
    uint8_t chunk[4096];
    for (size_t n; (n = std::fread(chunk, 1, sizeof chunk, file)) > 0;) open.bytes.insert(open.bytes.end(), chunk, chunk + n);
    std::fclose(file);
    if (LittleEndian(open.bytes)) {
        for (size_t i = 0; i + 1 < open.bytes.size(); i += 2) std::swap(open.bytes[i], open.bytes[i + 1]);
    }
    Files()[buf] = std::move(open);
    buf->mFile = 1;
    return buf;
}

// basic_istream::read(buffer, count): the bytes asked for, or what is left with eofbit and failbit
// set (nothing at all, with the same bits, from a stream that is not open). gcount is what came.
void IstreamRead(SwearIfstream* s, char* buffer, int32_t count) {
    size_t got = 0;
    auto it = Files().find(&s->mFilebuf);
    if (it != Files().end() && count > 0) {
        OpenFile& open = it->second;
        got = std::min(static_cast<size_t>(count), open.bytes.size() - open.at);
        std::memcpy(buffer, open.bytes.data() + open.at, got);
        open.at += got;
    }
    s->mGcount = static_cast<int32_t>(got);
    if (count > 0 && got < static_cast<size_t>(count)) s->mIos->mState |= 2 | 4;
}

// basic_filebuf::close(): NULL when nothing was open.
SwearFilebuf* FilebufClose(SwearFilebuf* buf) {
    if (Files().erase(buf) == 0) return nullptr;
    buf->mFile = 0;
    return buf;
}

void FilebufDtor(SwearFilebuf* buf, int16_t) {
    Files().erase(buf);
    buf->mFile = 0;
}

void IstreamDtor(SwearIfstream*, int16_t) {}

void IosDtor(SwearIos*, int16_t) {}

}  // namespace swear_callee
