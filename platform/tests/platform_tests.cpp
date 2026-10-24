// platform_tests.cpp: checks of the platform layer that need no window: path mapping over
// a scratch game folder and user folder, directory searches, the CD check and debug
// options calls, key tables, string helpers. Run by ctest ("platform"); exits nonzero on
// the first failure.
#include <unistd.h>
#include <SDL3/SDL.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <functional>
#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <iterator>
#include <string>
#include <vector>

#include "Ptr32.h"
#include "dialogs.h"
#include "email.h"
#include "packs.h"
#include "TbAudioSystem.h"
#include "platform.h"
#include "platform/arena.h"
#include "platform/ltms.h"
#include "platform/macdoze.h"
#include "platform/msl.h"
#include "platform/samsutils.h"
#include "platform/upixels.h"
#include "ui_ime.h"
#include "TbIRLE4bitFont.h"
#include "UI_Base.h"
#include "fit.h"
#include "handoff.h"
#include "gdi_text.h"
#include "resource_fork.h"
#include "CMpegBase.h"
#include "FS_MasterSave.h"
#include "CSprocketsModule.h"
#include "TbSoundBank.h"
#include "TbSoundPipeChannel.h"
#include "sound_device.h"
#include "sound_module.h"

namespace fs = std::filesystem;

static int gChecks = 0;

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        ++gChecks;                                                                          \
        if (!(condition)) {                                                                 \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);     \
            std::exit(1);                                                                   \
        }                                                                                   \
    } while (0)

static void Touch(const fs::path& path, const char* text = "x") {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

// Two host paths that name the same entry. On a file system that ignores case a lookup
// may answer in the caller's spelling instead of the disk's, so paths are compared folded.
static bool Same(const fs::path& a, const fs::path& b) {
    auto fold = [](const fs::path& path) {
        std::string text = path.lexically_normal().string();
        for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    return !a.empty() && fold(a) == fold(b);
}

static std::string Join(const std::vector<std::string>& parts) {
    std::string out;
    for (const std::string& part : parts) out += "/" + part;
    return out;
}

// setenv and unsetenv, which Windows' C runtime has as _putenv_s (an empty value removes the variable there).
static void SetEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

static void UnsetEnv(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

static void TestPaths(const fs::path& scratch) {
    tpw::Config& config = tpw::config();
    config.gameDir = scratch / "game";
    config.userDir = scratch / "user";
    config.logToStderr = false;
    Touch(config.gameDir / "data" / "ui" / "Here2000.txt", "");
    Touch(config.gameDir / "data" / "Language" / "American" / "strings.txt");
    Touch(config.gameDir / "data" / "sound.sam");
    Touch(config.gameDir / "data" / "sound.sam.finfo");   // the disc dump's side files
    Touch(config.gameDir / "data" / "sound.sam.rsrc");
    Touch(config.gameDir / "data" / "notes.rsrc");        // a real file that happens to end in .rsrc
    fs::create_directories(config.userDir);

    // Mac, Windows and host separators, the project and CD prefixes, "." and doubled separators.
    CHECK(Join(tpw::SplitGamePath("data:ui:Here2000.txt")) == "/data/ui/Here2000.txt");
    CHECK(Join(tpw::SplitGamePath("data\\ui\\Here2000.txt")) == "/data/ui/Here2000.txt");
    CHECK(Join(tpw::SplitGamePath(":data::ui/Here2000.txt")) == "/data/ui/Here2000.txt");
    CHECK(Join(tpw::SplitGamePath("TPW:Theme Park Data:data:sound.sam")) == "/data/sound.sam");
    CHECK(Join(tpw::SplitGamePath("Theme Park:Theme Park Data:data:ui:Here2000.txt")) == "/data/ui/Here2000.txt");
    CHECK(Join(tpw::SplitGamePath(".:data:.:sound.sam")) == "/data/sound.sam");
    CHECK(tpw::SplitGamePath("").empty());
    CHECK(tpw::SplitGamePath(tpw::kProjectDirMac).empty());

    // Lookups ignore case and find folders as well as files.
    CHECK(Same(tpw::FindHostPath("DATA:UI:here2000.TXT"), config.gameDir / "data" / "ui" / "Here2000.txt"));
    CHECK(Same(tpw::FindHostPath("data:language"), config.gameDir / "data" / "Language"));
    CHECK(tpw::FindHostPath("data:missing.sam").empty());
    CHECK(Same(tpw::FindHostPath(""), config.userDir));

    // Writing goes to the user folder, which then wins the lookup; the game folder is untouched.
    const fs::path saved = tpw::HostPathForWriting("save:config.tcf");
    CHECK(Same(saved, config.userDir / "save" / "config.tcf"));
    CHECK(fs::is_directory(config.userDir / "save"));
    Touch(saved);
    CHECK(Same(tpw::FindHostPath("Save:Config.tcf"), saved));
    CHECK(Same(tpw::HostPathForWriting("SAVE:users:1Tess:gms.dat"), config.userDir / "save" / "users" / "1Tess" / "gms.dat"));
    CHECK(!fs::exists(config.userDir / "save" / "users" / "1Tess" / "gms.dat") && fs::is_directory(config.userDir / "save" / "users" / "1Tess"));
    Touch(tpw::HostPathForWriting("data:sound.sam"), "override");
    CHECK(Same(tpw::FindHostPath("data:sound.sam"), config.userDir / "data" / "sound.sam"));
    CHECK(!fs::exists(config.gameDir / "save"));

    // Attributes and folders, the Win32 way.
    CHECK(NS_MacDoze::GetFileAttributes("data:ui:Here2000.txt") == 0x80);
    CHECK(NS_MacDoze::GetFileAttributes("DATA") == 0x10);
    CHECK(NS_MacDoze::GetFileAttributes("data:nothing") == 0xffffffffu);
    CHECK(NS_MacDoze::CreateDirectory("save:users", nullptr) == 0);        // made above
    CHECK(NS_MacDoze::CreateDirectory("save:online", nullptr) == 1);
    CHECK(fs::is_directory(config.userDir / "save" / "online"));
    CHECK(NS_MacDoze::CreateDirectory("data", nullptr) == 0);              // exists in the game folder

    // Listings merge both folders, user entries first, without the dump's side files.
    const auto data = tpw::ListGameFolder("data:");
    std::string names;
    for (const tpw::DirEntry& entry : data) names += entry.name + (entry.isDirectory ? "/ " : " ");
    CHECK(names == "sound.sam Language/ notes.rsrc ui/ ");
    CHECK(tpw::ListGameFolder("data:nowhere").empty());
}

// Asset packs (packs.h) come after the user folder and before the game folder, the first pack before the next, and only
// with the files a pack may hold: never another kind of file, nor a folder named like a game file.
static void TestPacks(const fs::path& scratch) {
    CHECK(tpw::PackMayHold("data/init/640/Splash_American.tga"));
    CHECK(tpw::PackMayHold("DATA/INIT/640/SPLASH_AMERICAN.TGA"));
    CHECK(tpw::PackMayHold("data/fonts/x.ttf.rsrc"));   // a resource fork goes by its file
    CHECK(!tpw::PackMayHold("data/levels/fantasy/global.sam"));
    CHECK(!tpw::PackMayHold("data/extra.txt"));
    CHECK(!tpw::PackMayHold("data/init/README"));
    CHECK(!tpw::PackMayHold("data/init/.x.tga"));
    CHECK(!tpw::PackMayHold("data/levels/fantasy/features/bfbin.wad/x.tga"));   // a folder whose name holds a dot
    CHECK(!tpw::PackMayHold("data/levels/fantasy/global.sam/x.tga"));
    CHECK(!tpw::PackMayHold("data/.hidden/x.tga"));
    // A sound bank and its index alike, and only in the folders the game keeps the banks in.
    for (const char* folder : {"data/global/sound/", "data/global/Speech/", "data/levels/fantasy/Sound/",
                               "data/levels/jungle/Speech/", "data/levels/space/Music/"}) {
        CHECK(tpw::PackMayHold(std::string(folder) + "bank.sdt"));
        CHECK(tpw::PackMayHold(std::string(folder) + "bank.map"));
    }
    for (const char* folder : {"data/", "data/init/", "data/levels/fantasy/", "data/levels/fantasy/features/",
                               "data/levels/Sound/"}) {
        CHECK(!tpw::PackMayHold(std::string(folder) + "bank.sdt"));
        CHECK(!tpw::PackMayHold(std::string(folder) + "bank.map"));
    }

    tpw::Config& config = tpw::config();
    const fs::path gameDir = config.gameDir;   // TestPaths' folders, which later tests use
    const fs::path userDir = config.userDir;
    config.gameDir = scratch / "game";
    config.userDir = scratch / "user";
    const fs::path first = scratch / "pack-b";
    const fs::path second = scratch / "pack-a";
    const fs::path features = fs::path("data") / "levels" / "fantasy" / "features";
    Touch(config.gameDir / "data" / "init" / "640" / "Splash_American.tga");
    Touch(config.gameDir / "data" / "sound.sam");
    Touch(config.gameDir / features / "bfbin.wad");
    Touch(config.userDir / "data" / "sound.sam");
    Touch(first / "data" / "init" / "640" / "splash_american.tga");
    Touch(first / "data" / "logo.tga");
    Touch(first / "data" / "sound.sam");
    Touch(first / "data" / "extra.txt");
    Touch(first / features / "BFBIN.WAD" / "x.tga");   // put there by hand, or unpacked by older rules
    Touch(second / "DATA" / "init" / "640" / "Splash_American.tga");
    tpw::PackDirs() = {first, second};

    CHECK(tpw::IsGameFile("data/levels/fantasy/features/bfbin.wad"));
    CHECK(tpw::IsGameFile("DATA/Levels/fantasy/features/BFBIN.WAD"));
    CHECK(!tpw::IsGameFile("data/levels/fantasy/features"));   // a folder
    CHECK(!tpw::IsGameFile("data/nowhere.wad"));

    CHECK(Same(tpw::FindHostPath("data:init:640:Splash_American.tga"), first / "data" / "init" / "640" / "splash_american.tga"));
    CHECK(Same(tpw::FindHostPath("data:sound.sam"), config.userDir / "data" / "sound.sam"));   // the user folder still wins
    CHECK(tpw::FindHostPath("data:extra.txt").empty());   // in a pack, but not a file a pack may hold
    CHECK(Same(tpw::FindHostPath("data:levels:fantasy:features:bfbin.wad"), config.gameDir / features / "bfbin.wad"));
    const auto data = tpw::ListGameFolder("data:");
    std::string names;
    for (const tpw::DirEntry& entry : data) names += entry.name + (entry.isDirectory ? "/ " : " ");
    CHECK(names == "sound.sam logo.tga init/ levels/ ");
    const auto listed = tpw::ListGameFolder("data:levels:fantasy:features");
    CHECK(listed.size() == 1 && listed[0].name == "bfbin.wad" && !listed[0].isDirectory);

    tpw::PackDirs().clear();
    CHECK(Same(tpw::FindHostPath("data:init:640:Splash_American.tga"), config.gameDir / "data" / "init" / "640" / "Splash_American.tga"));
    config.gameDir = gameDir;
    config.userDir = userDir;
}

// The std::ifstream shims of SwearFilter::LoadWordList (platform/msl_ifstream.cpp): a word file as
// the disc ships it (UTF-16 little-endian, XORed, CR LF) comes back as big-endian halfwords, the
// stream ends with eofbit and failbit, and a missing file or a second open answers NULL.
// The edition by the game folder's Language (as the US and UK discs lay it out, in any case) and the window's title.
static void TestEdition(const fs::path& scratch) {
    Touch(scratch / "stp" / "data" / "Language" / "American" / "strings.str");
    Touch(scratch / "tpw" / "Data" / "language" / "English" / "strings.str");
    Touch(scratch / "both" / "data" / "Language" / "english" / "strings.str");
    Touch(scratch / "both" / "data" / "Language" / "AMERICAN" / "strings.str");
    fs::create_directories(scratch / "none" / "data");
    CHECK(tpw::EditionOf(scratch / "stp") == "stp");
    CHECK(tpw::EditionOf(scratch / "tpw") == "tpw");
    CHECK(tpw::EditionOf(scratch / "both") == "stp");
    CHECK(tpw::EditionOf(scratch / "none") == "tpw");
    CHECK(tpw::EditionOf(scratch / "missing") == "tpw");
    const unsigned char title[] = "\x10" "Theme Park World";
    CHECK(tpw::WindowTitle("stp", title) == "SimTheme Park");
    CHECK(tpw::WindowTitle("tpw", title) == "Theme Park World");
}

static void TestSwearStreams() {
    tpw::Config& config = tpw::config();
    const fs::path file = config.gameDir / "data" / "Language" / "American" / "swears.txt";
    fs::create_directories(file.parent_path());
    {
        // "ab" CR LF "c" CR LF, each character XORed with 0xffff, little-endian.
        const uint8_t bytes[] = {0x9e, 0xff, 0x9d, 0xff, 0x0d, 0x00, 0x0a, 0x00, 0x9c, 0xff, 0x0d, 0x00, 0x0a, 0x00};
        std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bytes), sizeof bytes);
    }
    SwearIfstream s;
    swear_callee::IfstreamCtor(&s, 1);
    CHECK(s.mIos == &s.mIosStorage);
    CHECK(s.mIos->mState == 0 && s.mIos->mMask == 0 && s.mIos->mRdbuf != 0);
    CHECK(s.mFilebuf.mFile == 0);
    CHECK(swear_callee::FilebufOpen(&s.mFilebuf, "data:language:american:SWEARS.TXT", 0xc) == &s.mFilebuf);
    CHECK(s.mFilebuf.mFile != 0);
    CHECK(swear_callee::FilebufOpen(&s.mFilebuf, "data:language:american:SWEARS.TXT", 0xc) == nullptr);
    uint8_t got[16] = {};
    for (int i = 0; i < 7; ++i) {
        swear_callee::IstreamRead(&s, reinterpret_cast<char*>(got + 2 * i), 2);
        CHECK(s.mGcount == 2);
        CHECK(s.mIos->mState == 0);
    }
    // Big-endian now: the loop's (p[0] << 8 | p[1]) ^ 0xffff gives 'a', 'b', CR, LF, 'c', CR, LF.
    const uint8_t want[] = {0xff, 0x9e, 0xff, 0x9d, 0x00, 0x0d, 0x00, 0x0a, 0xff, 0x9c, 0x00, 0x0d, 0x00, 0x0a};
    CHECK(std::memcmp(got, want, sizeof want) == 0);
    swear_callee::IstreamRead(&s, reinterpret_cast<char*>(got), 2);
    CHECK(s.mGcount == 0);
    CHECK(s.mIos->mState == (2 | 4));
    CHECK(swear_callee::FilebufClose(&s.mFilebuf) == &s.mFilebuf);
    CHECK(s.mFilebuf.mFile == 0);
    CHECK(swear_callee::FilebufClose(&s.mFilebuf) == nullptr);
    swear_callee::FilebufDtor(&s.mFilebuf, -1);
    swear_callee::IstreamDtor(&s, 0);
    swear_callee::IosDtor(&s.mIosStorage, 0);

    // A big-endian file is handed over unchanged, and a short last read ends the stream.
    {
        const uint8_t bytes[] = {0xff, 0x9e, 0x00, 0x0d, 0x00, 0x0a, 0xff};
        std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(bytes), sizeof bytes);
    }
    swear_callee::IfstreamCtor(&s, 1);
    CHECK(swear_callee::FilebufOpen(&s.mFilebuf, "data:Language:American:swears.txt", 0xc) == &s.mFilebuf);
    for (int i = 0; i < 3; ++i) swear_callee::IstreamRead(&s, reinterpret_cast<char*>(got + 2 * i), 2);
    CHECK(std::memcmp(got, "\xff\x9e\x00\x0d\x00\x0a", 6) == 0 && s.mIos->mState == 0);
    swear_callee::IstreamRead(&s, reinterpret_cast<char*>(got), 2);
    CHECK(s.mGcount == 1 && got[0] == 0xff && s.mIos->mState == (2 | 4));
    swear_callee::FilebufDtor(&s.mFilebuf, -1);
    CHECK(s.mFilebuf.mFile == 0);

    // No such file: open answers NULL and leaves the filebuf closed; a read then gives nothing and ends the stream.
    swear_callee::IfstreamCtor(&s, 1);
    CHECK(swear_callee::FilebufOpen(&s.mFilebuf, "data:Language:American:nosuch.txt", 0xc) == nullptr);
    CHECK(s.mFilebuf.mFile == 0);
    swear_callee::IstreamRead(&s, reinterpret_cast<char*>(got), 2);
    CHECK(s.mGcount == 0 && s.mIos->mState == (2 | 4));
    fs::remove(file);
}

// The Metrowerks runtime imports (platform/msl_runtime.cpp): a Mac-sized request gets zeroed
// memory large enough for the host's layout, the string and wide-memory primitives behave as
// their C library namesakes, and __throw logs and returns as the unit tests' fakes do.
// The Metrowerks double-to-unsigned glue at 0x10237cd8 (platform/cvt_glue.cpp) under its three game names.
int32_t FloatToCell(double value);
int32_t FloatToIntGlue(float value);
int32_t FLY_FloatToInt(float value);

static void TestCvtGlue() {
    CHECK(FloatToCell(0.0) == 0 && FloatToCell(-0.0) == 0 && FloatToCell(-1.5) == 0 && FloatToCell(-1e30) == 0);
    CHECK(FloatToCell(0.99) == 0 && FloatToCell(1.0) == 1 && FloatToCell(37.9) == 37 && FloatToCell(2147483647.0) == 0x7fffffff);
    CHECK(static_cast<uint32_t>(FloatToCell(2147483648.0)) == 0x80000000u);
    CHECK(static_cast<uint32_t>(FloatToCell(3000000000.5)) == 3000000000u);
    CHECK(static_cast<uint32_t>(FloatToCell(4294967295.0)) == 0xffffffffu);
    CHECK(static_cast<uint32_t>(FloatToCell(4294967296.0)) == 0xffffffffu && static_cast<uint32_t>(FloatToCell(1e30)) == 0xffffffffu);
    CHECK(static_cast<uint32_t>(FloatToCell(std::nan(""))) == 0xffffffffu);
    CHECK(FloatToIntGlue(12.75f) == 12 && FloatToIntGlue(-3.0f) == 0 && FLY_FloatToInt(255.5f) == 255 && FLY_FloatToInt(-0.25f) == 0);
}

static void TestMslRuntime() {
    // 0xe bytes is a tree node on the Mac; the host node holds pointers and is far larger.
    uint8_t* node = static_cast<uint8_t*>(IME::MslNew(0xe));
    CHECK(node != nullptr);
    bool zero = true;
    for (int i = 0; i < 64; ++i) zero = zero && node[i] == 0;
    CHECK(zero);
    node[60] = 0x5a;   // writable well beyond the Mac size
    CHECK(node[60] == 0x5a);
    IME::MslDelete(node);
    void* rep = IME::MslNewRep();
    CHECK(rep != nullptr);
    CHECK(static_cast<IME::MslWideStringRep*>(rep)->mData == nullptr);
    IME::MslDelete(rep);
    char* chars = static_cast<char*>(IME::MslNewArray(4));
    CHECK(chars != nullptr);
    CHECK(IME::MslStrcpy(chars, "abc") == chars && std::strcmp(chars, "abc") == 0);
    CHECK(IME::MslStrlen(chars) == 3 && IME::MslStrlen("") == 0 && IME::MslStrlen(nullptr) == 0);
    IME::MslDeleteArray(chars);
    IME::MslDelete(nullptr);
    IME::MslDeleteArray(nullptr);

    const char16_t a[] = {u'a', u'b', u'c', 0};
    const char16_t b[] = {u'a', u'b', u'd', 0};
    CHECK(IME::MslWmemcmp(a, b, 2) == 0 && IME::MslWmemcmp(a, b, 3) < 0 && IME::MslWmemcmp(b, a, 3) > 0);
    CHECK(IME::MslWmemcmp(a, b, 0) == 0);
    char16_t buffer[8] = {};
    CHECK(IME::MslWmemcpy(buffer, a, 4) == buffer && std::memcmp(buffer, a, 8) == 0);
    CHECK(IME::MslWmemset(buffer, u'x', 3) == buffer && buffer[0] == u'x' && buffer[2] == u'x' && buffer[3] == 0);
    // An overlapping move of four units up one slot: x x x NUL becomes x x x x NUL.
    CHECK(IME::MslWmemmove(buffer + 1, buffer, 4) == buffer + 1);
    CHECK(buffer[1] == u'x' && buffer[3] == u'x' && buffer[4] == 0);
    CHECK(IME::MslWmemcpy(buffer, a, 0) == buffer && IME::MslWmemmove(buffer, a, 0) == buffer);

    // __throw returns (logged); a NULL object or message is tolerated.
    IME::MslExceptionObject exception;
    exception.mVtbl = nullptr;
    exception.mRc.mPtr = const_cast<char*>("string compare: pos > len");
    exception.mRc.mCount = nullptr;
    IME::MslThrow("!std::exception!!std::logic_error!!std::out_of_range!!", &exception, nullptr);
    exception.mRc.mPtr = nullptr;
    IME::MslThrow("!std::exception!", &exception, nullptr);
    IME::MslThrow(nullptr, nullptr, nullptr);
    CHECK(true);
}

static void TestFindFirstFile() {
    NS_MacDoze::_WIN32_FIND_DATAA found;
    // The game's language detection: the first entry of data:language.
    NS_MacDoze::InternalHandle* handle = NS_MacDoze::FindFirstFile("TPW:Theme Park Data:data:language:*", &found);
    CHECK(handle != reinterpret_cast<NS_MacDoze::InternalHandle*>(static_cast<intptr_t>(-1)));
    CHECK(std::strcmp(found.cFileName, "American") == 0);
    CHECK(found.dwFileAttributes == 0x10);
    CHECK(NS_MacDoze::FindClose(handle) == 1);
    handle = NS_MacDoze::FindFirstFile("data:*.SAM", &found);
    CHECK(std::strcmp(found.cFileName, "sound.sam") == 0);
    CHECK(found.dwFileAttributes == 0x80 && found.nFileSizeLow == 8);   // the user folder's "override"
    NS_MacDoze::FindClose(handle);
    // A file's last write time comes out as a FILETIME (unit w108: the newest park file is picked by it).
    CHECK(found.ftLastWriteTime[1] != 0 || found.ftLastWriteTime[0] != 0);
    CHECK(found.ftLastWriteTime[0] == found.ftCreationTime[0] && found.ftLastWriteTime[1] == found.ftCreationTime[1]);
    const uint64_t written = (static_cast<uint64_t>(found.ftLastWriteTime[1]) << 32) | found.ftLastWriteTime[0];
    CHECK(written > 116444736000000000ull + 1500000000ull * 10000000ull);   // after 2017
    handle = NS_MacDoze::FindFirstFile("data:*.wad", &found);
    CHECK(handle == reinterpret_cast<NS_MacDoze::InternalHandle*>(static_cast<intptr_t>(-1)));
}

// NS_MacDoze::CompareFileTime: the pairs and answers are the original's (macdoze 0x10006310 run in the emulator by
// a one-off script which also found 0 differences over 400 random pairs). Times are compared to the second
// (the Mac epoch 1904 is 95616284229632000 ticks after 1601), so the pairs less than a second apart in the same second answer 0.
static void TestCompareFileTime() {
    struct Pair {
        uint64_t a, b;
        int32_t answer;
    };
    static const Pair pairs[] = {
        {0x1d882cb9b208000ull, 0x1d882cb9b208000ull, 0},
        {0x1d882cb9b208000ull, 0x1d882cb9b208001ull, 0},
        {0x1d882cb9b208001ull, 0x1d882cb9b208000ull, 0},
        {0x1d882cb9b208000ull, 0x1d882cb9bb9167full, -1},
        {0x1d882cb9b208000ull, 0x1d882cb9bb91680ull, -1},
        {0x1d882cb9bb91680ull, 0x1d882cb9b208000ull, 1},
        {0x1d882cb9e1b7080ull, 0x1d882cb9d82da03ull, 1},
        {0x0ull, 0x0ull, 0},
        {0x0ull, 0x1ull, 0},
        {0x1ull, 0x0ull, 0},
        {0x153b28100400000ull, 0x153b28100400000ull, 0},
        {0x153b28100d8967full, 0x153b28100400000ull, 0},
        {0x153b281003fffffull, 0x153b28100400000ull, 0},
        {0x153b280ffa76980ull, 0x153b28100400000ull, -1},
        {0x153b280ff0ed300ull, 0x153b280ffa76980ull, -1},
        {0x153b280ffa76981ull, 0x153b280ffa76980ull, 1},
        {0xffffffffffffffffull, 0x0ull, 0},
        {0x0ull, 0xffffffffffffffffull, 0},
        {0xffffffffffffffffull, 0xfffffffffffffffeull, 0},
        {0x8000000000000000ull, 0x7fffffffffffffffull, 0},
        {0xffffffffull, 0x100000000ull, 0},
        {0x100000000ull, 0xffffffffull, 0},
    };
    for (const Pair& pair : pairs) {
        const NS_MacDoze::tagFILETIME a = {static_cast<uint32_t>(pair.a), static_cast<uint32_t>(pair.a >> 32)};
        const NS_MacDoze::tagFILETIME b = {static_cast<uint32_t>(pair.b), static_cast<uint32_t>(pair.b >> 32)};
        CHECK(NS_MacDoze::CompareFileTime(&a, &b) == pair.answer);
    }
}

// The Win32 file calls, with the Mac shim's habits (short reads fail, no seeking past the end).
static void TestFileHandles() {
    using namespace NS_MacDoze;
    InternalHandle* const invalid = reinterpret_cast<InternalHandle*>(static_cast<intptr_t>(-1));
    tpw::Config& config = tpw::config();
    Touch(config.gameDir / "data" / "ten.bin", "0123456789");

    CHECK(CreateFile("data:missing.bin", 0x80000000u, 1, nullptr, 3, 0, nullptr) == invalid);
    CHECK(CreateFile("data", 0x80000000u, 1, nullptr, 3, 0, nullptr) == invalid);            // a folder
    CHECK(CreateFile("data:ten.bin", 0xc0000000u, 0, nullptr, 1, 0, nullptr) == invalid);    // CREATE_NEW over a file
    InternalHandle* file = CreateFile("DATA:Ten.bin", 0x80000000u, 1, nullptr, 3, 0x10000000, nullptr);
    CHECK(file != invalid);
    uint32_t done = 99;
    char buffer[16] = {};
    CHECK(GetFileSize(file, nullptr) == 10);
    CHECK(ReadFile(file, buffer, 4, &done, nullptr) == 1 && done == 4 && std::memcmp(buffer, "0123", 4) == 0);
    CHECK(SetFilePointer(file, 2, nullptr, 1) == 6);            // from the current position
    CHECK(SetFilePointer(file, -1, nullptr, 2) == 9);           // from the end
    CHECK(ReadFile(file, buffer, 4, &done, nullptr) == 0 && done == 1 && buffer[0] == '9');   // short: fails
    CHECK(SetFilePointer(file, 11, nullptr, 0) == 0xffffffffu); // past the end: refused
    CHECK(SetFilePointer(file, -1, nullptr, 0) == 0xffffffffu);
    CHECK(SetFilePointer(file, 10, nullptr, 0) == 10);
    CHECK(GetMacFileReference(file) != 0 && FlushFileBuffers(file) == 1);

    // ReadFileReference, the MPEG file player's PBRead: a read at an absolute offset by the Mac reference number, short at the end
    // of the file, -1 for a reference no open file has; the mark is left after the bytes read.
    const int32_t reference = GetMacFileReference(file);
    char bytes[16] = {};
    CHECK(tpw::ReadFileReference(reference, 3, bytes, 4) == 4 && std::memcmp(bytes, "3456", 4) == 0);
    CHECK(SetFilePointer(file, 0, nullptr, 1) == 7);
    CHECK(tpw::ReadFileReference(reference, 8, bytes, 6) == 2 && std::memcmp(bytes, "89", 2) == 0);
    CHECK(tpw::ReadFileReference(reference, 10, bytes, 6) == 0 && tpw::ReadFileReference(reference, 4000, bytes, 6) == 0);
    CHECK(tpw::ReadFileReference(reference, 0, bytes, 0) == 0);
    CHECK(tpw::ReadFileReference(reference + 1000, 0, bytes, 4) == -1 && tpw::ReadFileReference(0, 0, bytes, 4) == -1);
    CHECK(SetFilePointer(file, 10, nullptr, 0) == 10);

    // A read-only mapping of the whole file, and views into it.
    InternalHandle* mapping = CreateFileMapping(file, nullptr, 2, 0, 0, nullptr);
    CHECK(mapping != nullptr);
    auto* view = static_cast<const char*>(MapViewOfFileEx(mapping, 4, 0, 0, 0, nullptr));
    CHECK(view != nullptr && std::memcmp(view, "0123456789", 10) == 0);
    CHECK(static_cast<const char*>(MapViewOfFileEx(mapping, 4, 0, 3, 4, nullptr)) == view + 3);
    CHECK(MapViewOfFileEx(mapping, 4, 0, 8, 4, nullptr) == nullptr);
    CHECK(UnmapViewOfFile(const_cast<char*>(view)) == 1);
    CHECK(CloseHandle(mapping) == 1 && CloseHandle(mapping) == 0);
    CHECK(CloseHandle(file) == 1 && CloseHandle(file) == 0 && CloseHandle(nullptr) == 0 && CloseHandle(invalid) == 0);
    CHECK(ReadFile(file, buffer, 1, &done, nullptr) == 0 && GetFileSize(file, nullptr) == 0xffffffffu);
    CHECK(tpw::ReadFileReference(reference, 0, bytes, 4) == -1);   // a closed file's reference is no longer known

    // Creating and writing happen in the user folder; the game's file is copied before a write.
    InternalHandle* made = CreateFile("save:new.bin", 0xc0000000u, 0, nullptr, 2, 0, nullptr);
    CHECK(made != invalid && WriteFile(made, "abcdef", 6, &done, nullptr) == 1 && done == 6);
    CHECK(SetFilePointer(made, 0, nullptr, 0) == 0 && ReadFile(made, buffer, 6, &done, nullptr) == 1);
    CHECK(std::memcmp(buffer, "abcdef", 6) == 0 && CloseHandle(made) == 1);
    CHECK(fs::file_size(config.userDir / "save" / "new.bin") == 6);
    InternalHandle* edited = CreateFile("data:ten.bin", 0xc0000000u, 0, nullptr, 3, 0, nullptr);
    CHECK(edited != invalid && WriteFile(edited, "XY", 2, &done, nullptr) == 1 && CloseHandle(edited) == 1);
    CHECK(fs::file_size(config.gameDir / "data" / "ten.bin") == 10);
    std::ifstream original(config.gameDir / "data" / "ten.bin");
    std::string text;
    original >> text;
    CHECK(text == "0123456789");
    CHECK(fs::exists(config.userDir / "data" / "ten.bin"));

    // A ride's height map (unit v5L44): SaveRideHeightMap's fopen of the Mac path goes to tpw::GamePathForWriting, the user folder's
    // copy, and the next read of the path (CreateFile, as TPFile_Open's disc storage opens it) takes that copy before the game's.
    const char* const kHeightMap = "data:levels:jungle:Features:1x1east:1x1east.hmp";
    Touch(config.gameDir / "data" / "levels" / "jungle" / "Features" / "1x1east" / "1x1east.hmp", "disc");
    InternalHandle* disc = CreateFile(kHeightMap, 0x80000000u, 0, nullptr, 3, 0, nullptr);
    CHECK(disc != invalid && ReadFile(disc, buffer, 4, &done, nullptr) == 1 && done == 4 && std::memcmp(buffer, "disc", 4) == 0);
    CHECK(CloseHandle(disc) == 1);
    const std::string cached = tpw::GamePathForWriting(kHeightMap);
    CHECK(Same(cached, config.userDir / "data" / "levels" / "jungle" / "Features" / "1x1east" / "1x1east.hmp"));
    std::FILE* written = std::fopen(cached.c_str(), "wb");
    CHECK(written != nullptr && std::fwrite("made!", 5, 1, written) == 1 && std::fclose(written) == 0);
    InternalHandle* reread = CreateFile(kHeightMap, 0x80000000u, 0, nullptr, 3, 0, nullptr);
    CHECK(reread != invalid && ReadFile(reread, buffer, 5, &done, nullptr) == 1 && done == 5 && std::memcmp(buffer, "made!", 5) == 0);
    CHECK(CloseHandle(reread) == 1);
    CHECK(fs::file_size(config.gameDir / "data" / "levels" / "jungle" / "Features" / "1x1east" / "1x1east.hmp") == 4);
    // One the game folder has no copy of is made, folders and all, and then read back the same way.
    const std::string fresh = tpw::GamePathForWriting("data:levels:jungle:Features:_gidol_5:_gidol.hmp");
    CHECK(Same(fresh, config.userDir / "data" / "levels" / "jungle" / "Features" / "_gidol_5" / "_gidol.hmp"));
    CHECK(fs::is_directory(config.userDir / "data" / "levels" / "jungle" / "Features" / "_gidol_5"));
    CHECK(CreateFile("data:levels:jungle:Features:_gidol_5:_gidol.hmp", 0x80000000u, 0, nullptr, 3, 0, nullptr) == invalid);
    written = std::fopen(fresh.c_str(), "wb");
    CHECK(written != nullptr && std::fclose(written) == 0);
    InternalHandle* made2 = CreateFile("data:levels:jungle:Features:_gidol_5:_gidol.hmp", 0x80000000u, 0, nullptr, 3, 0, nullptr);
    CHECK(made2 != invalid && CloseHandle(made2) == 1);
    CHECK(!fs::exists(config.gameDir / "data" / "levels" / "jungle" / "Features" / "_gidol_5"));

    // The player's mail files (unit v5L48). tpw::GamePathForReading gives the user folder's copy of a game path, else the game
    // folder's, else the path as it is (so a missing file still fails to open).
    Touch(config.gameDir / "save" / "users" / "T5L48" / "outbox" / "00000001.jpg", "game");
    CHECK(Same(tpw::GamePathForReading("TPW:Theme Park Data:save:users:T5L48:outbox:00000001.jpg"),
               config.gameDir / "save" / "users" / "T5L48" / "outbox" / "00000001.jpg"));
    Touch(config.userDir / "save" / "users" / "T5L48" / "outbox" / "00000001.jpg", "user");
    CHECK(Same(tpw::GamePathForReading("TPW:Theme Park Data:save:users:T5L48:outbox:00000001.jpg"),
               config.userDir / "save" / "users" / "T5L48" / "outbox" / "00000001.jpg"));
    CHECK(tpw::GamePathForReading("TPW:Theme Park Data:save:users:T5L48:outbox:00000002.jpg") ==
          "TPW:Theme Park Data:save:users:T5L48:outbox:00000002.jpg");
    // AddressBook::Save, Outbox::Save and Outbox::WriteMailData write through the SaveStream glue: its open of the colon path makes
    // the file in the user folder, and the loaders' CreateFile (TextFile::Open) reads the same file back.
    {
        const char* const kBook = "TPW:Theme Park Data:save:users:T5L48:addrbook.dat";
        SaveStream out;
        SaveStreamCtor(&out);
        CHECK(SaveStreamOpen(&out, kBook) && out.filebufOpen == 1);
        SaveStreamWrite(&out, "a@b.c\n");
        CHECK(SaveStreamClose(&out));
        SaveStreamDestroy(&out);
        const fs::path bookFile = config.userDir / "save" / "users" / "T5L48" / "addrbook.dat";
        CHECK(fs::exists(bookFile) && fs::file_size(bookFile) == 6);
        CHECK(!fs::exists(kBook) && !fs::exists(config.gameDir / "save" / "users" / "T5L48" / "addrbook.dat"));
        InternalHandle* book = CreateFile(kBook, 0x80000000u, 0, nullptr, 3, 0, nullptr);
        CHECK(book != invalid && ReadFile(book, buffer, 6, &done, nullptr) == 1 && done == 6 && std::memcmp(buffer, "a@b.c\n", 6) == 0);
        CHECK(CloseHandle(book) == 1);
        // A mail written the same way into the outbox (the user folder's, as Outbox::Load's CreateDirectory makes it) is read back by
        // ReadMailData's stream.
        const char* const kMail = "TPW:Theme Park Data:save:users:T5L48:outbox:00000003.dat";
        SaveStreamCtor(&out);
        CHECK(SaveStreamOpen(&out, kMail));
        SaveStreamWrite(&out, "7\nsubject\n");
        CHECK(SaveStreamClose(&out));
        SaveStreamDestroy(&out);
        const fs::path mailFile = config.userDir / "save" / "users" / "T5L48" / "outbox" / "00000003.dat";
        CHECK(fs::exists(mailFile) && fs::file_size(mailFile) == 10);
        SaveStream in;
        outbox_callee::StreamCtor(&in);
        CHECK(outbox_callee::StreamOpen(&in, kMail) && in.filebufOpen == 1);
        char line[16] = {};
        outbox_callee::StreamGetline(&in, line, sizeof(line), outbox_callee::StreamWiden(&in));
        CHECK(std::strcmp(line, "7") == 0);
        outbox_callee::StreamGetline(&in, line, sizeof(line), '\n');
        CHECK(std::strcmp(line, "subject") == 0 && in.ios->mState == 0);
        outbox_callee::StreamDestroy(&in);
        // A mail neither folder has fails to open, and reading makes no folder for it.
        outbox_callee::StreamCtor(&in);
        CHECK(!outbox_callee::StreamOpen(&in, "TPW:Theme Park Data:save:users:U5L48:outbox:00000004.dat") && in.filebufOpen == 0);
        outbox_callee::StreamDestroy(&in);
        CHECK(!fs::exists(config.userDir / "save" / "users" / "U5L48"));
        CHECK(!fs::exists(kMail));
    }

    // Listing more than one entry, then files and folders coming and going.
    _WIN32_FIND_DATAA found;
    InternalHandle* search = FindFirstFile("save:*", &found);
    int entries = 1;
    while (FindNextFile(search, &found) != 0) ++entries;
    CHECK(entries >= 3 && FindNextFile(search, &found) == 0 && FindClose(search) == 1);
    CHECK(CopyFile("data:ten.bin", "save:copy.bin", 1) == 1 && CopyFile("data:ten.bin", "save:copy.bin", 1) == 0);
    CHECK(MoveFile("save:copy.bin", "save:moved.bin") == 1 && GetFileAttributes("save:copy.bin") == 0xffffffffu);
    CHECK(MoveFile("save:moved.bin", "save:new.bin") == 0);     // the target exists
    CHECK(SetFileAttributes("save:moved.bin", 0x80) == 1 && SetFileAttributes("save:copy.bin", 0x80) == 0);
    CHECK(DeleteFile("save:moved.bin") == 1 && DeleteFile("save:moved.bin") == 0);
    CHECK(DeleteFile("data:sound.sam.finfo") == 0);             // nothing in the game folder is deleted
    CHECK(CreateDirectory("save:empty", nullptr) == 1 && RemoveDirectory("save:empty") == 1);
    _SYSTEM_INFO info;
    GetSystemInfo(&info);
    CHECK(info.dwAllocationGranularity == 0x10000);
}

static void TestMacFileCalls() {
    // The CD check of TPMain.cpp: the marker file on a locked volume.
    unsigned char path[256];
    SamsUtilities::UString::c2pcpy(path, "Theme Park:Theme Park Data:data:ui:Here2000.txt");
    FSSpec spec;
    CHECK(FSMakeFSSpec(0, 0, path, &spec) == 0);
    CHECK(spec.name[0] == 12 && std::memcmp(spec.name + 1, "Here2000.txt", 12) == 0);
    // The host file behind the spec (what QuickTime is handed for a film).
    CHECK(tpw::HostPathOfSpec(&spec).filename() == "Here2000.txt" && std::filesystem::exists(tpw::HostPathOfSpec(&spec)));
    HVolumeParam volume = {};
    volume.ioVRefNum = spec.vRefNum;
    CHECK(PBHGetVInfoSync(&volume) == 0);
    CHECK((volume.ioVAtrb & 0x8080) != 0);
    SamsUtilities::UString::c2pcpy(path, "data:ui:NotHere.txt");
    CHECK(FSMakeFSSpec(0, 0, path, &spec) == -43);
    FSSpec unknown = {};
    SamsUtilities::UString::c2pcpy(unknown.name, "never made.mov");
    CHECK(tpw::HostPathOfSpec(&unknown).empty());

    // "debug options" exists only when the launcher set an override; its resource holds the bytes.
    SamsUtilities::UString::c2pcpy(path, "debug options");
    CHECK(FSMakeFSSpec(0, 0, path, &spec) == -43);
    tpw::config().debugOptions[1] = 0;   // windowed
    CHECK(FSMakeFSSpec(0, 0, path, &spec) == 0);
    const int16_t file = FSpOpenResFile(&spec, 1);
    CHECK(file != -1);
    UseResFile(file);
    Handle options = GetNamedResource(0x444f5054, path);
    CHECK(options != nullptr);
    CHECK(static_cast<uint8_t>((*options)[0]) == 0xff && (*options)[1] == 0 &&
          static_cast<uint8_t>((*options)[3]) == 0xff);
    CloseResFile(file);
    CHECK(GetNamedResource(0x444f5054, path) == nullptr);
    tpw::config().debugOptions[1] = 0xff;

    // The application folder the game reads its project directory from.
    SamsUtilities::UString projectDir;
    SamsUtilities::UFolder* folder = SamsUtilities::gThisProcess.GetFolder();
    folder->GetPath(projectDir);
    CHECK(std::strcmp(projectDir.mText, "TPW:Theme Park Data:") == 0);
    CHECK(folder->GetItem(SamsUtilities::UString("development data"), 0x666f6c64) == nullptr);
    SamsUtilities::UFileItem* dataFolder = folder->GetItem(SamsUtilities::UString("DATA"), 0x666f6c64);
    CHECK(dataFolder != nullptr);
    dataFolder->GetPath(projectDir);
    CHECK(std::strcmp(projectDir.mText, "TPW:Theme Park Data:DATA:") == 0);
    delete dataFolder;
}

static void TestDialogs() {
    // MessageBoxW (the game's exit error box): no box, a log line with the title and the text, and IDOK.
    const char16_t* title = u"Theme Park World";
    CHECK(NS_MacDoze::MessageBoxW(nullptr, u"out of memory (x)", title, 0x10010) == 1);
    CHECK(tpw::MessageBoxLine(u"out of memory (x)", title, 0x10010) ==
          "message box \"Theme Park World\": out of memory (x) (flags 0x10010)");
    CHECK(tpw::MessageBoxLine(u"", u"", 0) == "message box \"\":  (flags 0x0)");
    CHECK(tpw::MessageBoxLine(nullptr, nullptr, 0xffffffffu) == "message box \"(null)\": (null) (flags 0xffffffff)");
    // UTF-16 to UTF-8: ASCII, two-, three- and four-byte characters, a surrogate pair and lone surrogates.
    CHECK(tpw::Utf8(u"abc") == "abc");
    CHECK(tpw::Utf8(u"\u00e9") == "\xc3\xa9");
    CHECK(tpw::Utf8(u"\u20ac") == "\xe2\x82\xac");
    CHECK(tpw::Utf8(u"\U0001f600") == "\xf0\x9f\x98\x80");
    CHECK(tpw::Utf8(u"\xd800") == "\xef\xbf\xbd");
    CHECK(tpw::Utf8(u"\xdc00x") == "\xef\xbf\xbdx");
    CHECK(tpw::Utf8(u"\xd83dx") == "\xef\xbf\xbdx");
}

// NS_MacDoze::SetCursor (platform/cursor.cpp): a cursor becomes the one drawn over the frame; NULL keeps it.
static int gCursorDraws = 0;
static void TestMacDozeCursor() {
    SamsUtilities::UCursor cursor;
    cursor.mDraw = [](SamsUtilities::UCursor*) { ++gCursorDraws; };
    NS_MacDoze::SetCursor(nullptr);
    tpw::DrawCursorOverlay();
    CHECK(gCursorDraws == 0);
    NS_MacDoze::SetCursor(&cursor);
    tpw::DrawCursorOverlay();
    CHECK(gCursorDraws == 1);
    NS_MacDoze::SetCursor(nullptr);
    tpw::DrawCursorOverlay();
    CHECK(gCursorDraws == 2);
    cursor.Reset();
    tpw::DrawCursorOverlay();
    CHECK(gCursorDraws == 2);
}

static void TestKeys() {
    // Values of the original's table (macdoze InitialiseTables): return, escape, the
    // keypad's enter and '/' as extended keys, F1, a letter (sent as its character).
    uint16_t flags = 0xffff;
    CHECK(NS_MacDoze::MacToWindowsVirtualKey(0x24, flags) == 0x0d && flags == 0);
    CHECK(NS_MacDoze::MacToWindowsVirtualKey(0x35, flags) == 0x1b && flags == 0);
    CHECK(NS_MacDoze::MacToWindowsVirtualKey(0x4c, flags) == 0x0d && flags == 0x100);
    CHECK(NS_MacDoze::MacToWindowsVirtualKey(0x4b, flags) == 0x6f && flags == 0x100);
    CHECK(NS_MacDoze::MacToWindowsVirtualKey(0x7a, flags) == 0x70 && flags == 0);
    CHECK(NS_MacDoze::MacToWindowsVirtualKey(0x00, flags) == 0 && flags == 0);
    CHECK(tpw::MacKeyCodeFromScancode(SDL_SCANCODE_RETURN) == 0x24);
    CHECK(tpw::MacKeyCodeFromScancode(SDL_SCANCODE_A) == 0x00);
    CHECK(tpw::MacKeyCodeFromScancode(SDL_SCANCODE_MUTE) == 0xff);
    // GetAsyncKeyState's keys: escape and space skip the movies, letters are themselves.
    CHECK(tpw::ScancodeFromWindowsKey(0x1b) == SDL_SCANCODE_ESCAPE);
    CHECK(tpw::ScancodeFromWindowsKey(0x20) == SDL_SCANCODE_SPACE);
    CHECK(tpw::ScancodeFromWindowsKey('Q') == SDL_SCANCODE_Q);
    CHECK(tpw::ScancodeFromWindowsKey('0') == SDL_SCANCODE_0);
    CHECK(tpw::ScancodeFromWindowsKey(0x25) == SDL_SCANCODE_LEFT);
    CHECK(tpw::ScancodeFromWindowsKey(0x07) == 0);
    CHECK(tpw::MacModifiersFromSDL(SDL_KMOD_LSHIFT | SDL_KMOD_RCTRL) == 0x1200);
    CHECK(tpw::MacModifiersFromSDL(SDL_KMOD_LGUI | SDL_KMOD_LALT) == 0x900);
}

static void TestKeyboard() {
    // SamsUtilities::UKeyboard (platform/keyboard.cpp): the object SOUND_Init makes and asks through its vtable whether
    // Option and Command are held. SDL's modifier state is set directly; no window is needed.
    SDL_SetModState(SDL_KMOD_NONE);
    SamsUtilities::UKeyboard* keyboard = new SamsUtilities::UKeyboard;
    CHECK(keyboard->IsOptionDown() == 0 && keyboard->IsCommandDown() == 0);
    SDL_SetModState(SDL_KMOD_LALT);
    CHECK(keyboard->IsOptionDown() == 1 && keyboard->IsCommandDown() == 0);
    SDL_SetModState(SDL_KMOD_RGUI);
    CHECK(keyboard->IsOptionDown() == 0 && keyboard->IsCommandDown() == 1);
    SDL_SetModState(static_cast<SDL_Keymod>(SDL_KMOD_LGUI | SDL_KMOD_RALT));
    CHECK(keyboard->IsOptionDown() == 1 && keyboard->IsCommandDown() == 1);
    SDL_SetModState(static_cast<SDL_Keymod>(SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_CAPS));
    CHECK(keyboard->IsOptionDown() == 0 && keyboard->IsCommandDown() == 0);
    // GetModifiers answers the Mac's word from GetKeys: control 0x1000, command 0x100, shift 0x200, option 0x800, no Caps Lock.
    CHECK(keyboard->GetModifiers() == 0x1200);
    SDL_SetModState(SDL_KMOD_NONE);
    CHECK(keyboard->GetModifiers() == 0);
    SDL_SetModState(SDL_KMOD_LGUI);
    CHECK(keyboard->GetModifiers() == 0x100);
    SDL_SetModState(static_cast<SDL_Keymod>(SDL_KMOD_RGUI | SDL_KMOD_LALT | SDL_KMOD_RSHIFT | SDL_KMOD_LCTRL));
    CHECK(keyboard->GetModifiers() == 0x1b00);
    SDL_SetModState(SDL_KMOD_NONE);
    delete keyboard;
}

namespace {
// A UGamework that records what the run loop sends it (TestScriptedClick).
struct ClickRecorder : SamsUtilities::UGamework {
    std::vector<std::string> log;
    int passes = 0;
    ClickRecorder() : UGamework(0) {}
    static std::string At(const SamsUtilities::UEvent& event) {
        const SamsUtilities::UPoint where = event.GetPosition();
        return std::to_string(where.x) + "," + std::to_string(where.y);
    }
    bool Process() override {
        ++passes;
        float x = -1, y = -1;
        bool down = false;
        const bool scripted = tpw::ScriptedPointer(x, y, down);
        log.push_back("process " + std::to_string(passes) + " scripted " + (scripted ? "1" : "0") + " at " +
                      std::to_string(static_cast<int>(x)) + "," + std::to_string(static_cast<int>(y)) + " held " +
                      (down ? "1" : "0") + " async " + (NS_MacDoze::GetAsyncKeyState(1) != 0 ? "1" : "0"));
        return true;
    }
    void HandleMouseMoved(const SamsUtilities::UEvent& event) override { log.push_back("moved " + At(event)); }
    void HandleMouseDown(const SamsUtilities::UEvent& event, SamsUtilities::MouseButton button) override {
        log.push_back("down " + std::to_string(button) + " " + At(event));
    }
    void HandleMouseUp(const SamsUtilities::UEvent& event, SamsUtilities::MouseButton button) override {
        log.push_back("up " + std::to_string(button) + " " + At(event));
    }
    void HandleTyping(char character, char keyCode, uint32_t modifiers, bool down) override {
        char text[64];
        std::snprintf(text, sizeof text, "typing %d key %02x mods %x %s", character, static_cast<uint8_t>(keyCode), modifiers,
                      down ? "down" : "up");
        log.push_back(text);
    }
};
}  // namespace

static void TestScriptedClick() {
    // --click X,Y@N (platform/gamework.cpp): on pass N the loop sends a move and a left button down at the game pixel, on
    // pass N + 1 the button up; between the two GetAsyncKeyState(1) reports the button held and the cursor sits on the
    // pixel; after the release the pointer stays scripted (and still) until a real mouse motion. No window: the origin is
    // 0,0 and the game pixel is the event's position. Two clicks, the second on the last pass (released never).
    tpw::Config& config = tpw::config();
    const std::vector<tpw::Config::ScriptedClick> savedClicks = config.clicks;
    const int32_t savedFrames = config.maxFrames;
    config.clicks = {{2, 1326, 121}, {4, 10, 20}};
    config.typing = {{1, "aB\r"}};   // 'a' down on pass 1 and up on 2, 'B' (shifted) on 3 and 4, return on 5 (never reached)
    config.maxFrames = 4;
    ClickRecorder recorder;
    float x = 0, y = 0;
    bool down = false;
    CHECK(!tpw::ScriptedPointer(x, y, down));
    const bool events = SDL_InitSubSystem(SDL_INIT_EVENTS);   // the queue, for the synthetic motion below; no window
    recorder.Run();
    config.clicks = savedClicks;
    config.typing.clear();
    config.maxFrames = savedFrames;
    const std::vector<std::string> expected = {
        "typing 97 key 00 mods 0 down",
        "process 1 scripted 0 at -1,-1 held 0 async 0",
        "moved 1326,121",
        "down 0 1326,121",
        "typing 97 key 00 mods 0 up",
        "process 2 scripted 1 at 1326,121 held 1 async 1",
        "up 0 1326,121",
        "typing 66 key 0b mods 200 down",
        "process 3 scripted 1 at 1326,121 held 0 async 0",
        "moved 10,20",
        "down 0 10,20",
        "typing 66 key 0b mods 200 up",
        "process 4 scripted 1 at 10,20 held 1 async 1",
    };
    CHECK(recorder.passes == 4);
    CHECK(recorder.log.size() == expected.size());
    for (size_t i = 0; i < expected.size() && i < recorder.log.size(); ++i) {
        if (recorder.log[i] != expected[i]) std::fprintf(stderr, "scripted click line %zu: %s\n", i, recorder.log[i].c_str());
        CHECK(recorder.log[i] == expected[i]);
    }
    // A real mouse motion ends the scripted pointer. SDL's queue takes a synthetic motion event without a window.
    CHECK(tpw::ScriptedPointer(x, y, down) && x == 10 && y == 20 && down);
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 5;
    motion.motion.y = 6;
    CHECK(events && SDL_PushEvent(&motion));
    config.maxFrames = 1;
    ClickRecorder after;
    after.Run();
    config.maxFrames = savedFrames;
    CHECK(after.log.size() == 2 && after.log[0] == "moved 5,6");
    CHECK(!tpw::ScriptedPointer(x, y, down));
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
}

static void TestStrings() {
    CHECK(NS_MacDoze::_stricmp("Theme", "tHEME") == 0);
    CHECK(NS_MacDoze::_stricmp("abc", "abd") < 0 && NS_MacDoze::_stricmp("b", "A") > 0);
    CHECK(NS_MacDoze::_wcsicmp(u"American", u"AMERICAN") == 0 && NS_MacDoze::_wcsicmp(u"a", u"ab") < 0);
    char16_t wide[40];
    CHECK(std::u16string(NS_MacDoze::_itow(-1234, wide, 10)) == u"-1234");
    CHECK(std::u16string(NS_MacDoze::_itow(0, wide, 10)) == u"0");
    CHECK(std::u16string(NS_MacDoze::_itow(-1, wide, 16)) == u"ffffffff");
    char narrow[16];
    CHECK(NS_MacDoze::WideCharToMultiByte(0, 0, u"Tess", -1, narrow, sizeof narrow, nullptr, nullptr) == 5);
    CHECK(std::strcmp(narrow, "Tess") == 0);
    CHECK(NS_MacDoze::WideCharToMultiByte(0, 0, u"Tess", -1, nullptr, 0, nullptr, nullptr) == 5);
    CHECK(NS_MacDoze::WideCharToMultiByte(0, 0, u"Tess", -1, narrow, 3, nullptr, nullptr) == 0);
    CHECK(NS_MacDoze::MultiByteToWideChar(0, 0, "park", -1, wide, 40) == 5 && std::u16string(wide) == u"park");

    CHECK(wcslen(u"park") == 4 && wcslen(u"") == 0);
    char16_t buffer[16];
    CHECK(std::u16string(wcscpy(buffer, u"theme")) == u"theme");
    CHECK(std::u16string(wcscat(buffer, u" park")) == u"theme park");
    CHECK(std::u16string(wcsncat(buffer, u"world", 2)) == u"theme parkwo");
    char16_t padded[6] = {9, 9, 9, 9, 9, 9};
    wcsncpy(padded, u"ab", 5);
    CHECK(padded[0] == u'a' && padded[1] == u'b' && padded[2] == 0 && padded[4] == 0 && padded[5] == 9);
    CHECK(wcsncmp(u"abc", u"abd", 2) == 0 && wcsncmp(u"abc", u"abd", 3) < 0 && wcsncmp(u"b", u"a", 1) > 0);
    CHECK(wcsstr(buffer, u"park") == buffer + 6 && wcsstr(buffer, u"zoo") == nullptr && wcsstr(buffer, u"") == buffer);
    // swprintf: 16-bit characters, the string and number conversions, truncation and the zero size
    char16_t text16[16];
    CHECK(swprintf(text16, 16, u"%ls %ls", u"Ab", u"cd") == 5 && std::u16string(text16) == u"Ab cd");
    CHECK(swprintf(text16, 16, u"%d x", 12) == 4 && std::u16string(text16) == u"12 x");
    char16_t text32[32];
    CHECK(swprintf(text32, 32, u"%5d|%-5d|%05d|%x|%X", 42, 42, 42, 255, 255) == 23 &&
          std::u16string(text32) == u"   42|42   |00042|ff|FF");
    CHECK(swprintf(text16, 16, u"%.2ls|%4ls|%-4ls|", u"abcd", u"ab", u"ab") == 13 && std::u16string(text16) == u"ab|  ab|ab  |");
    CHECK(swprintf(text16, 4, u"%ls", u"abcdef") == -1 && std::u16string(text16) == u"abc");
    // a plain %s takes a narrow string (MSL's wide formatter converts it), %ls a wide one
    CHECK(swprintf(text16, 16, u"v %s", "2.04b3") == 8 && std::u16string(text16) == u"v 2.04b3");
    CHECK(swprintf(text16, 16, u"%.3s|%5s|%-4s|", "abcdef", "ab", "ab") == 15 && std::u16string(text16) == u"abc|   ab|ab  |");
    CHECK(swprintf(text16, 16, u"%s|%ls", static_cast<const char*>(nullptr), u"x") == 8 && std::u16string(text16) == u"(null)|x");
    text16[0] = u'z';
    CHECK(swprintf(text16, 0, u"") == -1 && text16[0] == u'z');
    CHECK(swprintf(text16, 16, u"") == 0 && text16[0] == 0);
    CHECK(swprintf(text16, 16, u"100%%%c", u'!') == 5 && std::u16string(text16) == u"100%!");
    // the floating conversions take a double: the number text's "%.2f %%" (flag 8), TbDoubleVTag's "%f"
    CHECK(swprintf(text16, 16, u"%.2f %%", static_cast<double>(100.0f * (512.0f * 0.0009765625f))) == 7 &&
          std::u16string(text16) == u"50.00 %");
    CHECK(swprintf(text16, 16, u"%f", 1.5) == 8 && std::u16string(text16) == u"1.500000");
    CHECK(swprintf(text32, 32, u"%8.3f|%-6.1f|%+.0f|%d", -2.25, 0.25, 2.5, 7) == 20 &&
          std::u16string(text32) == u"  -2.250|0.2   |+2|7");
    CHECK(swprintf(text32, 32, u"%e|%G", 1250.0, 0.5) == 16 && std::u16string(text32) == u"1.250000e+03|0.5");
    CHECK(msl_lower_map['A'] == 'a' && msl_lower_map['Z'] == 'z' && msl_lower_map['['] == '[' && msl_lower_map['a'] == 'a');
    CHECK(NS_MacDoze::IsDBCSLeadByte(0x81) == 0);

    char text[32] = "cd missing";
    const unsigned char* pascal = c2pstr(text);
    CHECK(pascal[0] == 10 && std::memcmp(pascal + 1, "cd missing", 10) == 0);
    unsigned char a[16], b[16];
    SamsUtilities::UString::c2pcpy(a, "American");
    SamsUtilities::UString::c2pcpy(b, "AMERICAN");
    CHECK(EqualString(a, b, false, false) && !EqualString(a, b, true, false));

    SamsUtilities::UString empty;
    SamsUtilities::UString cut("Quit now", 4);
    CHECK(empty.mLength == 0 && empty.mText[0] == '\0');
    CHECK(cut.mLength == 4 && std::strcmp(cut.mText, "Quit") == 0);
    SamsUtilities::UResourceId id(cut, 7);
    CHECK(id.mId == 7 && std::strcmp(id.mType.mText, "Quit") == 0 && id.mType.mText != cut.mText);
    // the version object (platform/samsutils_version.cpp): the default 'vers' resource answers "2.04b3"; a string assignment copies
    SamsUtilities::UVersion version(2, 0, 0, 0);
    CHECK(version.mMajor == 2 && version.mStage == 0x80 && version.mText.mLength == 0);
    version.LoadFromResource(SamsUtilities::UResourceId(SamsUtilities::UString("none"), 9), 0);
    CHECK(version.mText.mLength == 0);
    version.LoadFromResource(SamsUtilities::UVersion::kDefaultVersionResource, 0);
    CHECK(version.mText.mLength == 6 && std::strcmp(version.mText.mText, "2.04b3") == 0 && version.mComment.mLength == 0);
    SamsUtilities::UString copy("old text");
    copy = version.mText;
    CHECK(copy.mLength == 6 && std::strcmp(copy.mText, "2.04b3") == 0 && copy.mText != version.mText.mText);
    SamsUtilities::UString& same = copy;
    copy = same;
    CHECK(copy.mLength == 6 && std::strcmp(copy.mText, "2.04b3") == 0);
    copy = SamsUtilities::UString();
    CHECK(copy.mLength == 0 && copy.mText[0] == '\0');

    int32_t count = 1;
    CHECK(NS_MacDoze::InterlockedIncrement(&count) == 2 && count == 2);
    CHECK(NS_MacDoze::InterlockedDecrement(&count) == 1 && NS_MacDoze::InterlockedDecrement(&count) == 0);

    uint32_t word = 0x11223344;
    SamsUtilities::_USwapBlock32(&word, 1);
    const uint16_t probe = 1;
    CHECK(word == (*reinterpret_cast<const uint8_t*>(&probe) == 1 ? 0x11223344u : 0x44332211u));
    {
        const uint32_t two[2] = {0x11223344, 0xAABBCCDD};
        uint32_t* copy = SamsUtilities::_USwapCopy32(two, 2);
        uint32_t expect[2] = {0x11223344, 0xAABBCCDD};
        SamsUtilities::_USwapBlock32(expect, 2);
        CHECK(copy != two && copy[0] == expect[0] && copy[1] == expect[1] && two[0] == 0x11223344 && two[1] == 0xAABBCCDD);
        SamsUtilities::_USwapDelete(copy);
    }
    {
        // _USwapAllocate: a writable block of the size asked for (also 0 and a size that is not a multiple of 4), freed by _USwapDelete.
        for (uint32_t bytes : {0u, 1u, 7u, 32u, 4096u}) {
            uint8_t* block = static_cast<uint8_t*>(SamsUtilities::_USwapAllocate(bytes));
            CHECK(block != nullptr);
            for (uint32_t i = 0; i < bytes; ++i) block[i] = static_cast<uint8_t>(i * 7);
            CHECK(bytes == 0 || block[bytes - 1] == static_cast<uint8_t>((bytes - 1) * 7));
            SamsUtilities::_USwapDelete(block);
        }
    }
    CHECK(msl_upper_map['a'] == 'A' && msl_upper_map['z'] == 'Z' && msl_upper_map['{'] == '{' &&
          msl_upper_map[0xe9] == 0xe9 && msl_upper_map['5'] == '5');
    const uint64_t before = SamsUtilities::UTimer::GetAbsolute();
    CHECK(SamsUtilities::UTimer::GetAbsolute() >= before);

    // MSL's generator: the first values from its starting state, and after a seed.
    CHECK(msl_rand() == 16838 && msl_rand() == 5758 && msl_rand() == 10113);
    msl_srand(1);
    CHECK(msl_rand() == 16838);
    msl_srand(0);
    CHECK(msl_rand() == 0);

    // The engine's texture upload buffer: 0x10000 elements of 4 bytes, 16-byte aligned,
    // every byte of it usable and zero.
    {
        SamsUtilities::UMemoryAlignedBase block(0x10000, 0x10, 4, nullptr);
        auto* bytes = static_cast<unsigned char*>(block.mData);
        CHECK(bytes != nullptr && (reinterpret_cast<uintptr_t>(bytes) & 0xf) == 0);
        bool zero = true;
        for (size_t i = 0; i < 0x40000; ++i) zero = zero && bytes[i] == 0;
        CHECK(zero);
        bytes[0x3ffff] = 0x5a;
        CHECK(bytes[0x3ffff] == 0x5a);
    }
}

// The arena and the 32-bit pointer fields that live in it.
static void TestArena() {
    // Blocks of every kind: aligned, inside the arena, apart from each other, usable to
    // their last byte; a freed block of a size comes back for the next request of it.
    const size_t sizes[] = {0, 1, 15, 16, 17, 500, 512, 513, 1024, 1025, 70000, 1 << 20, (1 << 20) + 1, 5 << 20};
    void* blocks[sizeof sizes / sizeof sizes[0]];
    int index = 0;
    for (size_t size : sizes) {
        auto* block = static_cast<uint8_t*>(tpw::ArenaAlloc(size));
        CHECK(block != nullptr && tpw::InArena(block) && (reinterpret_cast<uintptr_t>(block) & 15) == 0);
        CHECK(tpw::ArenaSize(block) == size);
        std::memset(block, 0x11 * (index + 1), size);
        blocks[index++] = block;
    }
    for (int i = 0; i < index; ++i) {
        const auto* block = static_cast<const uint8_t*>(blocks[i]);
        bool whole = true;
        for (size_t k = 0; k < sizes[i]; ++k) whole = whole && block[k] == static_cast<uint8_t>(0x11 * (i + 1));
        CHECK(whole);
    }
    void* small = blocks[3];
    void* large = blocks[13];
    tpw::ArenaFree(small);
    tpw::ArenaFree(large);
    CHECK(tpw::ArenaAlloc(16) == small);
    CHECK(tpw::ArenaAlloc((5 << 20) - 100) == large);
    tpw::ArenaFree(nullptr);
    auto* zeroed = static_cast<uint8_t*>(tpw::ArenaAllocZeroed(16));   // a block that held 0x44 bytes before
    CHECK(zeroed != nullptr && zeroed[0] == 0 && zeroed[15] == 0);
    int outside = 0;
    CHECK(!tpw::InArena(&outside) && !tpw::InArena(nullptr));
    CHECK(tpw::gArenaBase == tpw::ArenaBase() && tpw::ArenaOffset(small) == static_cast<uint8_t*>(small) - tpw::gArenaBase);

    char moved[8] = "abcdef";
    NS_MacDoze::CopyMemory(moved + 2, moved, 4);
    CHECK(std::memcmp(moved, "ababcd", 6) == 0);

    // The game's allocator for the engine is the arena.
    void* global = NS_MacDoze::GlobalAlloc(0x40, 100);
    CHECK(tpw::InArena(global) && static_cast<uint8_t*>(global)[99] == 0);
    CHECK(NS_MacDoze::GlobalFree(static_cast<NS_MacDoze::InternalHandle*>(global)) == nullptr);

    // A record with pointer fields, as a file has it: offsets become pointers.
    struct Record {
        uint32_t mValue;
        Ptr32<Record> mNext;
        Ptr32<uint16_t> mWords;
    };
    static_assert(sizeof(Record) == 12, "a record keeps the file's size");
    auto* file = static_cast<uint8_t*>(tpw::ArenaAllocZeroed(64));
    auto* first = reinterpret_cast<Record*>(file);
    auto* second = reinterpret_cast<Record*>(file + 16);
    first->mValue = 7;
    first->mNext.mOffset = 16;    // offsets from the start of the file
    first->mWords.mOffset = 40;
    second->mValue = 9;
    second->mNext.mOffset = 0;
    file[40] = 0x34;
    file[41] = 0x12;
    first->mNext.Relocate(file);
    first->mWords.Relocate(file);
    second->mNext.Relocate(file);
    CHECK(first->mNext == second && first->mNext->mValue == 9 && (*first->mNext).mValue == 9);
    CHECK(second->mNext == nullptr && !second->mNext && static_cast<bool>(first->mNext));
    CHECK(first->mWords[0] == 0x1234 && first->mWords.get() == reinterpret_cast<uint16_t*>(file + 40));
    second->mNext = first;
    CHECK(second->mNext.get() == first && second->mNext->mNext->mNext == first);
    Record copy = *first;         // records can be copied as bytes
    CHECK(copy.mNext == second);
    tpw::ArenaFree(file);

    // The live bytes follow the blocks, and a block made again after a free reuses the range (the high-water mark stays).
    const size_t live = tpw::ArenaLiveBytes();
    void* probe = tpw::ArenaAlloc(100);
    const size_t top = tpw::ArenaHighWater();
    CHECK(tpw::ArenaLiveBytes() == live + 100 && top > tpw::ArenaOffset(probe));
    tpw::ArenaFree(probe);
    CHECK(tpw::ArenaLiveBytes() == live);
    CHECK(tpw::ArenaAlloc(100) == probe && tpw::ArenaHighWater() == top && tpw::ArenaLiveBytes() == live + 100);
    tpw::ArenaFree(probe);
}

// ---- GDI text (platform/gdi.cpp) ---------------------------------------------------------

// A 8-bit BMP with the grey ramp: 14-byte file header, 40-byte info header, 256 colours, rows
// padded to 4 bytes and stored bottom-up. The pixels come top-down, width bytes per row.
static void WriteGreyBmp(const fs::path& path, const uint8_t* pixels, int width, int height) {
    const int stride = (width + 3) & ~3;
    const uint32_t offset = 14 + 40 + 256 * 4;
    const uint32_t size = offset + static_cast<uint32_t>(stride * height);
    std::vector<uint8_t> file(size, 0);
    auto put32 = [&](size_t at, uint32_t value) {
        for (int i = 0; i < 4; ++i) file[at + i] = static_cast<uint8_t>(value >> (8 * i));
    };
    file[0] = 'B';
    file[1] = 'M';
    put32(2, size);
    put32(10, offset);
    put32(14, 40);
    put32(18, static_cast<uint32_t>(width));
    put32(22, static_cast<uint32_t>(height));
    file[26] = 1;
    file[28] = 8;
    put32(34, static_cast<uint32_t>(stride * height));
    put32(46, 256);
    for (int i = 0; i < 256; ++i) {
        for (int k = 0; k < 3; ++k) file[54 + i * 4 + k] = static_cast<uint8_t>(i);
    }
    for (int row = 0; row < height; ++row) {
        std::memcpy(&file[offset + static_cast<size_t>(stride) * (height - 1 - row)], pixels + static_cast<size_t>(row) * width, width);
    }
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
}

// The BITMAPINFO of TrueTypeRender::render_font_unicode: a top-down 8-bit DIB with a grey ramp.
struct GreyInfo {
    NS_MacDoze::tagBITMAPINFOHEADER header;
    NS_MacDoze::tagRGBQUAD colors[256];
};

static GreyInfo MakeGreyInfo(int width, int height) {
    GreyInfo info;
    std::memset(&info, 0, sizeof info);
    info.header.biSize = sizeof info.header;
    info.header.biWidth = width;
    info.header.biHeight = -height;
    info.header.biPlanes = 1;
    info.header.biBitCount = 8;
    info.header.biClrUsed = 0x100;
    for (int i = 0; i < 256; ++i) {
        info.colors[i].rgbBlue = info.colors[i].rgbGreen = info.colors[i].rgbRed = static_cast<uint8_t>(i);
    }
    return info;
}

// What the sign renderer does for one line of text, into a 2 * 256 by 2 * 64 DIB and the 256 by 64
// mask the 2x2 box filter makes of it; both are written as BMP files when a folder is given.
static void RenderSignPicture(const char* face, int height, int width, int weight, const char* text,
                              const fs::path& folder, const char* name, const char* dibFile, const char* maskFile) {
    using namespace NS_MacDoze;
    InternalHandle* window = GetDC(nullptr);
    GreyInfo info = MakeGreyInfo(512, 128);
    void* bits = nullptr;
    InternalHandle* dib = CreateDIBSection(window, reinterpret_cast<tagBITMAPINFO*>(&info), 0, &bits, nullptr, 0);
    InternalHandle* dc = CreateCompatibleDC(window);
    InternalHandle* oldBitmap = SelectObject(dc, dib);
    SetMapMode(dc, 1);
    tagLOGFONTA logfont;
    std::memset(&logfont, 0, sizeof logfont);
    logfont.lfHeight = -height;
    logfont.lfWidth = width;
    logfont.lfWeight = weight;
    std::strncpy(logfont.lfFaceName, face, sizeof logfont.lfFaceName - 1);
    InternalHandle* font = CreateFontIndirect(&logfont);
    InternalHandle* oldFont = SelectObject(dc, font);
    tagSIZE size = {0, 0};
    GetTextExtentPoint(dc, text, static_cast<int32_t>(std::strlen(text)), &size);
    SetBkColor(dc, 0);
    SetTextColor(dc, 0xffffff);
    TextOut(dc, 0x100 - size.cx / 2, 8, text, static_cast<int32_t>(std::strlen(text)));
    SelectObject(dc, oldFont);
    DeleteObject(font);
    std::vector<uint8_t> mask(256 * 64);
    const auto* source = static_cast<const uint8_t*>(bits);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 256; ++x) {
            const int sum = source[(2 * y) * 512 + 2 * x] + source[(2 * y) * 512 + 2 * x + 1] +
                            source[(2 * y + 1) * 512 + 2 * x] + source[(2 * y + 1) * 512 + 2 * x + 1];
            mask[y * 256 + x] = static_cast<uint8_t>(sum / 4);
        }
    }
    if (!folder.empty()) {
        WriteGreyBmp(folder / dibFile, source, 512, 128);
        WriteGreyBmp(folder / maskFile, mask.data(), 256, 64);
    }
    std::printf("gdi picture %s: \"%s\" in \"%s\" %d: extent %d x %d\n", name, text, face, height, static_cast<int>(size.cx),
                static_cast<int>(size.cy));
    SelectObject(dc, oldBitmap);
    DeleteObject(dib);
    DeleteDC(dc);
    ReleaseDC(nullptr, window);
}

static void TestGdi() {
    using namespace NS_MacDoze;
    const char* const arial = "/System/Library/Fonts/Supplemental/Arial.ttf";
    const bool haveArial = fs::exists(arial);
    if (!haveArial) std::printf("gdi: %s is missing, the font checks are skipped\n", arial);

    // Critical sections: entered twice by one thread and left twice.
    void* section = nullptr;
    EnterCriticalSection(&section);
    EnterCriticalSection(&section);
    LeaveCriticalSection(&section);
    LeaveCriticalSection(&section);
    CHECK(section != nullptr);

    CHECK(AddFontResource("/nonexistent/folder/missing.ttf") == 0);
    CHECK(AddFontResource("data:fonts:missing.ttf") == 0);
    if (haveArial) {
        CHECK(AddFontResource(arial) >= 1);
        CHECK(AddFontResource(arial) >= 1);   // idempotent
    }

    // A handle that is not ours is refused.
    InternalHandle* const bogus = reinterpret_cast<InternalHandle*>(static_cast<intptr_t>(0x10));
    CHECK(ReleaseDC(nullptr, bogus) == 0);
    CHECK(DeleteObject(bogus) == 0);
    CHECK(SelectObject(bogus, bogus) == nullptr);
    CHECK(SelectObject(nullptr, nullptr) == nullptr);

    InternalHandle* window = GetDC(nullptr);
    CHECK(window != nullptr);
    CHECK(SetMapMode(window, 1) == 1);
    CHECK(SetTextColor(window, 0x123456) == 0xffffff);
    CHECK(SetTextColor(window, 0xffffff) == 0x123456);
    CHECK(SetBkColor(window, 0) == 0xffffff);
    CHECK(SetBkColor(window, 0) == 0);

    // A DC begins with its default font and bitmap selected: they are what the first selection answers.
    tagLOGFONTA logfont;
    std::memset(&logfont, 0, sizeof logfont);
    logfont.lfHeight = -24;
    logfont.lfWeight = 400;
    std::strcpy(logfont.lfFaceName, "Arial");
    InternalHandle* font = CreateFontIndirect(&logfont);
    CHECK(font != nullptr);
    InternalHandle* defaultFont = SelectObject(window, font);
    CHECK(defaultFont != nullptr && defaultFont != font);
    CHECK(DeleteObject(font) == 0);          // selected: still in use

    // The extents: the empty string is 0 wide and the height is lfHeight as stored.
    tagSIZE size = {99, 99};
    CHECK(GetTextExtentPoint(window, "", 0, &size) == 1);
    CHECK(size.cx == 0 && size.cy == -24);
    tagSIZE hello = {0, 0};
    tagSIZE twice = {0, 0};
    CHECK(GetTextExtentPoint(window, "Hello", 5, &hello) == 1);
    CHECK(hello.cx > 0 && hello.cx < 200 && hello.cy == -24);
    CHECK(GetTextExtentPoint(window, "Hello Hello", 11, &twice) == 1);
    CHECK(twice.cx > hello.cx);
    CHECK(GetTextExtentPoint(window, "Hello", 3, &size) == 1);
    CHECK(size.cx > 0 && size.cx < hello.cx);   // the length counts, not the NUL

    // lfWidth scales the advance by lfWidth / (average width of 92 characters).
    tagSIZE reference = {0, 0};
    GetTextExtentPoint(window, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@\xa3$%^&*()_+-=[]{}()|;':?/<>,.", 92, &reference);
    const int average = reference.cx / 92;
    CHECK(average > 4 && average < 24);
    SelectObject(window, defaultFont);
    DeleteObject(font);
    logfont.lfWidth = average / 2;
    InternalHandle* narrow = CreateFontIndirect(&logfont);
    SelectObject(window, narrow);
    tagSIZE half = {0, 0};
    GetTextExtentPoint(window, "Hello", 5, &half);
    std::printf("gdi: Arial -24: Hello is %d wide, the average width is %d, at lfWidth %d it is %d\n", static_cast<int>(hello.cx),
                average, static_cast<int>(logfont.lfWidth), static_cast<int>(half.cx));
    CHECK(std::abs(half.cx - hello.cx * logfont.lfWidth / average) <= 2);
    CHECK(std::abs(half.cx * 2 - hello.cx) <= hello.cx / 4 + 2);
    SelectObject(window, defaultFont);
    CHECK(DeleteObject(narrow) == 1);

    // A DIB and one letter. The "memory DC" is the window DC again.
    GreyInfo info = MakeGreyInfo(128, 32);
    void* bits = nullptr;
    InternalHandle* dib = CreateDIBSection(window, reinterpret_cast<tagBITMAPINFO*>(&info), 0, &bits, nullptr, 0);
    CHECK(dib != nullptr && bits != nullptr);
    auto* pixels = static_cast<uint8_t*>(bits);
    bool zero = true;
    for (int i = 0; i < 128 * 32; ++i) zero = zero && pixels[i] == 0;
    CHECK(zero);
    CHECK(CreateDIBSection(window, reinterpret_cast<tagBITMAPINFO*>(&info), 1, &bits, nullptr, 0) == nullptr);   // the window's palette: refused
    InternalHandle* memDC = CreateCompatibleDC(window);
    CHECK(memDC == window);
    InternalHandle* defaultBitmap = SelectObject(memDC, dib);
    CHECK(defaultBitmap != nullptr && defaultBitmap != dib);
    logfont.lfWidth = 0;
    font = CreateFontIndirect(&logfont);
    CHECK(SelectObject(memDC, font) == defaultFont);
    CHECK(SetBkColor(memDC, 0) == 0);
    CHECK(SetTextColor(memDC, 0xffffff) == 0xffffff);
    CHECK(TextOut(memDC, 10, 0, "I", 1) == 1);
    bool binary = true;
    int inked = 0;
    int inkInFirstColumn = 0;
    int inkInLastColumn = 0;
    int inkInLastRow = 0;
    int firstRow = 99;
    int lastRow = -1;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 128; ++x) {
            const uint8_t value = pixels[y * 128 + x];
            binary = binary && (value == 0 || value == 0xff);
            if (value == 0) continue;
            ++inked;
            inkInFirstColumn += x == 0;
            inkInLastColumn += x == 127;
            inkInLastRow += y == 31;
            firstRow = std::min(firstRow, y);
            lastRow = std::max(lastRow, y);
        }
    }
    std::printf("gdi: an Arial -24 \"I\": %d inked pixels, rows %d to %d\n", inked, firstRow, lastRow);
    CHECK(binary && inked > 0);
    CHECK(inkInFirstColumn == 0 && inkInLastColumn == 0 && inkInLastRow == 0);
    CHECK(lastRow - firstRow + 1 < 24);
    // Drawn text is only added: a second letter leaves the first where it was.
    const std::vector<uint8_t> before(pixels, pixels + 128 * 32);
    CHECK(TextOut(memDC, 40, 0, "I", 1) == 1);
    bool kept = true;
    for (int i = 0; i < 128 * 32; ++i) kept = kept && (before[i] == 0 || pixels[i] == before[i]);
    CHECK(kept && std::memcmp(before.data(), pixels, before.size()) != 0);

    CHECK(SelectObject(memDC, defaultBitmap) == dib);
    CHECK(SelectObject(memDC, defaultFont) == font);
    CHECK(DeleteObject(font) == 1);
    CHECK(DeleteObject(dib) == 1);
    CHECK(DeleteDC(memDC) == 1);
    CHECK(GetTextExtentPoint(window, "Hello", 5, &size) == 1);   // the window DC lives on
    CHECK(ReleaseDC(nullptr, window) == 1);
    CHECK(tpw::GdiLiveHandles() == 0);
    CHECK(GetTextExtentPoint(window, "Hello", 5, &size) == 0);   // and now it is gone

    // Pictures of the sign path, when a folder is given (TPW_GDI_PICTURES): a system font, then a game
    // font file (TPW_GDI_GAME_FONT, its family in TPW_GDI_GAME_FACE). Not checks.
    const char* folderName = std::getenv("TPW_GDI_PICTURES");
    const fs::path folder = folderName != nullptr ? fs::path(folderName) : fs::path();
    const fs::path blackFont = "/System/Library/Fonts/Supplemental/Arial Black.ttf";
    if (fs::exists(blackFont)) {
        CHECK(AddFontResource(blackFont.string().c_str()) >= 1);
        RenderSignPicture("Arial Black", 40, 24, 700, "Create New Player", folder, "sign", "sign-dib.bmp", "sign-mask.bmp");
        RenderSignPicture("Arial Black", 40, 24, 700, "Quit Game", folder, "sign-quit", "sign-quit-dib.bmp", "sign-quit-mask.bmp");
    }
    const char* gameFont = std::getenv("TPW_GDI_GAME_FONT");
    const char* gameFace = std::getenv("TPW_GDI_GAME_FACE");
    if (gameFont != nullptr) {
        const int faces = AddFontResource(gameFont);
        std::printf("gdi: game font %s: %d faces\n", gameFont, faces);
        if (gameFace != nullptr) RenderSignPicture(gameFace, 40, 24, 400, "Gargoyle Park", folder, "sign-garg", "sign-garg.bmp", "sign-garg-mask.bmp");
    }
    CHECK(tpw::GdiLiveHandles() == 0);
}

// ---- Resource forks and the fonts of the data disc ---------------------------------------------

// One resource to put in a synthetic fork.
struct TestResource {
    const char* type;
    int id;
    const char* name;    // nullptr: no name
    std::string payload;
};

static void Put16(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    bytes[at] = static_cast<uint8_t>(value >> 8);
    bytes[at + 1] = static_cast<uint8_t>(value);
}

static void Put32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    Put16(bytes, at, value >> 16);
    Put16(bytes, at + 2, value & 0xffff);
}

// A resource fork in the file's layout: header and padding up to 0x100, the data section, then the
// map (type list, reference lists, name list). The resources are grouped by type in the given order.
static std::vector<uint8_t> MakeFork(const std::vector<TestResource>& resources) {
    std::vector<std::string> types;
    for (const TestResource& r : resources) {
        if (std::find(types.begin(), types.end(), r.type) == types.end()) types.push_back(r.type);
    }
    std::vector<uint8_t> fork(0x100, 0);
    std::vector<uint32_t> dataOffsets;
    for (const TestResource& r : resources) {
        dataOffsets.push_back(static_cast<uint32_t>(fork.size() - 0x100));
        fork.resize(fork.size() + 4);
        Put32(fork, fork.size() - 4, static_cast<uint32_t>(r.payload.size()));
        fork.insert(fork.end(), r.payload.begin(), r.payload.end());
    }
    const size_t mapOffset = fork.size();
    const size_t typeListAt = 28;
    size_t map = typeListAt + 2 + 8 * types.size() + 12 * resources.size();
    const size_t nameListAt = map;
    std::vector<uint8_t> names;
    std::vector<uint8_t> mapBytes(map, 0);
    Put16(mapBytes, 24, static_cast<uint32_t>(typeListAt));
    Put16(mapBytes, 26, static_cast<uint32_t>(nameListAt));
    Put16(mapBytes, typeListAt, static_cast<uint32_t>(types.size() - 1));
    size_t entry = typeListAt + 2;
    size_t reference = typeListAt + 2 + 8 * types.size();
    for (const std::string& type : types) {
        uint32_t count = 0;
        for (size_t i = 0; i < resources.size(); ++i) {
            if (type != resources[i].type) continue;
            ++count;
            Put16(mapBytes, reference, static_cast<uint32_t>(resources[i].id) & 0xffff);
            if (resources[i].name != nullptr) {
                Put16(mapBytes, reference + 2, static_cast<uint32_t>(names.size()));
                names.push_back(static_cast<uint8_t>(std::strlen(resources[i].name)));
                names.insert(names.end(), resources[i].name, resources[i].name + std::strlen(resources[i].name));
            } else {
                Put16(mapBytes, reference + 2, 0xffff);
            }
            Put32(mapBytes, reference + 4, dataOffsets[i]);   // attributes byte 0
            reference += 12;
        }
        std::memcpy(&mapBytes[entry], type.data(), 4);
        Put16(mapBytes, entry + 4, count - 1);
        Put16(mapBytes, entry + 6, static_cast<uint32_t>(reference - 12 * count - typeListAt));
        entry += 8;
    }
    mapBytes.insert(mapBytes.end(), names.begin(), names.end());
    fork.insert(fork.end(), mapBytes.begin(), mapBytes.end());
    Put32(fork, 0, 0x100);
    Put32(fork, 4, static_cast<uint32_t>(mapOffset));
    Put32(fork, 8, static_cast<uint32_t>(mapOffset - 0x100));
    Put32(fork, 12, static_cast<uint32_t>(mapBytes.size()));
    return fork;
}

static void TestResourceFork() {
    const std::vector<uint8_t> fork = MakeFork({{"sfnt", 1846, "Alpha", "first font"},
                                                {"FOND", -5, nullptr, "family record"},
                                                {"sfnt", 1847, nullptr, std::string("\0second\xff", 8)}});
    const std::vector<tpw::ForkResource> all = tpw::ListResources(fork.data(), fork.size());
    CHECK(all.size() == 3);
    // Grouped by type in the map: both sfnt first, then the FOND.
    CHECK(all[0].type == tpw::ForkType("sfnt") && all[0].id == 1846 && all[0].name == "Alpha");
    CHECK(all[0].size == 10 && std::memcmp(all[0].data, "first font", 10) == 0);
    CHECK(all[1].type == tpw::ForkType("sfnt") && all[1].id == 1847 && all[1].name.empty());
    CHECK(all[1].size == 8 && std::memcmp(all[1].data, "\0second\xff", 8) == 0);
    CHECK(all[2].type == tpw::ForkType("FOND") && all[2].id == -5 && all[2].name.empty());
    CHECK(all[2].size == 13 && std::memcmp(all[2].data, "family record", 13) == 0);
    CHECK(all[0].data >= fork.data() && all[2].data + all[2].size <= fork.data() + fork.size());
    CHECK(tpw::FindResources(fork.data(), fork.size(), tpw::ForkType("sfnt")).size() == 2);
    CHECK(tpw::FindResources(fork.data(), fork.size(), tpw::ForkType("FOND")).size() == 1);
    CHECK(tpw::FindResources(fork.data(), fork.size(), tpw::ForkType("NFNT")).empty());

    // A fork with no resource at all: a type count of 0xffff (count - 1).
    const std::vector<uint8_t> none = MakeFork({{"sfnt", 1, nullptr, "x"}});
    std::vector<uint8_t> empty = none;
    const size_t mapAt = empty[7] | empty[6] << 8 | empty[5] << 16 | empty[4] << 24;
    Put16(empty, mapAt + 28, 0xffff);
    CHECK(tpw::ListResources(empty.data(), empty.size()).empty());

    // Refusals: every one answers "no resources".
    CHECK(tpw::ListResources(nullptr, 0).empty());
    CHECK(tpw::ListResources(fork.data(), 0).empty());
    CHECK(tpw::ListResources(fork.data(), 15).empty());
    const size_t map = fork[7] | fork[6] << 8 | fork[5] << 16 | fork[4] << 24;
    const size_t reference0 = map + 28 + 2 + 8 * 2;   // the first reference of the sfnt list
    auto broken = [&](size_t at, uint32_t value, bool wide) {
        std::vector<uint8_t> copy = fork;
        if (wide) Put32(copy, at, value); else Put16(copy, at, value);
        return tpw::ListResources(copy.data(), copy.size()).empty();
    };
    CHECK(broken(4, static_cast<uint32_t>(fork.size()) + 1, true));     // map offset past the end
    CHECK(broken(4, 0xffffffffu, true));                                  // map offset wrapping
    CHECK(broken(12, 0xffffffffu, true));                                 // map length wrapping
    CHECK(broken(12, 20, true));                                          // map shorter than its fixed part
    CHECK(broken(8, 0xffffffffu, true));                                  // data length past the end
    CHECK(broken(0, 0xffffff00u, true));                                  // data offset past the end
    CHECK(broken(map + 24, 0x7fff, false));                               // type list outside the map
    CHECK(broken(map + 28, 0x0100, false));                               // more types than the map holds
    CHECK(broken(map + 28 + 2 + 6, 0x7fff, false));                       // reference list outside the map
    CHECK(broken(map + 28 + 2 + 4, 0x00ff, false));                       // too many references for the map
    CHECK(broken(reference0 + 2, 0x7000, false));                         // name offset outside the map
    CHECK(broken(reference0 + 4, 0x00ffffff, true));                      // data offset (3 bytes) past the data section
    CHECK(broken(reference0 + 4, 0x0000ffff, true));                      // data offset past the data section
    {
        // A data length that runs past the data section.
        std::vector<uint8_t> copy = fork;
        Put32(copy, 0x100, 0x00ffffffu);
        CHECK(tpw::ListResources(copy.data(), copy.size()).empty());
        // A name whose length byte runs past the map.
        copy = fork;
        const size_t nameAt = map + 28 + 2 + 8 * 2 + 12 * 3;
        copy[nameAt] = 0xff;
        CHECK(tpw::ListResources(copy.data(), copy.size()).empty());
    }
    // Truncation anywhere answers nothing, and corruption of any single byte never reads out of range.
    bool truncated = true;
    for (size_t n = 0; n < fork.size(); ++n) truncated = truncated && tpw::ListResources(fork.data(), n).empty();
    CHECK(truncated);
    bool inside = true;
    for (size_t at = 0; at < fork.size(); ++at) {
        for (uint8_t value : {uint8_t(0), uint8_t(0x7f), uint8_t(0xff)}) {
            std::vector<uint8_t> copy = fork;
            copy[at] = value;
            for (const tpw::ForkResource& r : tpw::ListResources(copy.data(), copy.size())) {
                inside = inside && r.data >= copy.data() && r.data + r.size <= copy.data() + copy.size();
            }
        }
    }
    CHECK(inside);
}

// The table tags of an sfnt, with each table's bytes; empty when the header does not fit.
struct SfntTables {
    std::map<std::string, std::vector<uint8_t>> tables;
    std::vector<uint8_t> raw;
};

static bool ReadSfnt(const uint8_t* data, size_t size, SfntTables& out) {
    if (size < 12 || data[0] != 0 || data[1] != 1 || data[2] != 0 || data[3] != 0) return false;
    const size_t count = data[4] << 8 | data[5];
    if (size < 12 + count * 16) return false;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* entry = data + 12 + i * 16;
        const size_t offset = entry[8] << 24 | entry[9] << 16 | entry[10] << 8 | entry[11];
        const size_t length = entry[12] << 24 | entry[13] << 16 | entry[14] << 8 | entry[15];
        if (offset > size || length > size - offset) return false;
        out.tables[std::string(reinterpret_cast<const char*>(entry), 4)].assign(data + offset, data + offset + length);
    }
    out.raw.assign(data, data + size);
    return true;
}

static std::vector<uint8_t> ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The folder holding extracted/ : TPW_GDI_ROOT, else the directory above the worktree of this source file.
static fs::path ProjectRoot() {
    if (const char* root = std::getenv("TPW_GDI_ROOT")) return root;
    return fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

static void TestGdiForkFonts() {
    using namespace NS_MacDoze;
    const fs::path root = ProjectRoot();
    const fs::path dataFonts = root / "extracted/disc-hfs/Theme Park Data/Data/Fonts";
    const fs::path bonusFonts = root / "extracted/Theme_Park_World_Bonus_Stuff/Theme Park World Bonus Stuff/tpwfnt";
    std::error_code ec;
    if (!fs::is_directory(dataFonts, ec) || !fs::is_directory(bonusFonts, ec)) {
        std::printf("gdi: %s or %s is missing, the data disc font checks are skipped\n", dataFonts.string().c_str(), bonusFonts.string().c_str());
        return;
    }
    const char* const names[] = {"BIGLA___.TTF", "BIGLOA__.TTF", "CLUNA___.TTF", "CLUNAB__.TTF", "EGGITAOE.TTF", "GARGA___.TTF",
                                 "GARGSA__.TTF", "GARGSAI_.TTF", "HAUNTAOE.TTF", "INTRA___.TTF", "INTRAB__.TTF", "KRELA___.TTF",
                                 "KRELAB__.TTF", "LINUPA__.TTF", "TANNA___.TTF", "TANNAO__.TTF", "YOUNIA__.TTF"};

    // (b) The sfnt in each resource fork against the Bonus disc's data-fork TTF of the same name.
    int forks = 0;
    for (const char* name : names) {
        const fs::path file = dataFonts / name;
        const fs::path fork = dataFonts / (std::string(name) + ".rsrc");
        const fs::path twin = bonusFonts / name;
        if (!fs::exists(fork, ec) || !fs::exists(twin, ec)) continue;
        ++forks;
        CHECK(fs::file_size(file, ec) == 0);   // the data fork is empty
        const std::vector<uint8_t> bytes = ReadAll(fork);
        const auto sfnts = tpw::FindResources(bytes.data(), bytes.size(), tpw::ForkType("sfnt"));
        CHECK(sfnts.size() == 1);
        CHECK(tpw::FindResources(bytes.data(), bytes.size(), tpw::ForkType("FOND")).size() == 1);
        SfntTables mac;
        CHECK(ReadSfnt(sfnts[0].data, sfnts[0].size, mac));
        const std::vector<uint8_t> twinBytes = ReadAll(twin);
        SfntTables windows;
        CHECK(ReadSfnt(twinBytes.data(), twinBytes.size(), windows));
        const bool identical = mac.raw == windows.raw;
        std::string extra;
        std::string differing;
        for (const auto& table : windows.tables) {
            if (mac.tables.count(table.first) == 0) extra += " " + table.first;
            else if (mac.tables[table.first] != table.second) differing += " " + table.first;
        }
        std::string missing;
        for (const auto& table : mac.tables) {
            if (windows.tables.count(table.first) == 0) missing += " " + table.first;
        }
        std::printf("gdi: %s: resource fork sfnt %zu bytes, tpwfnt %zu bytes, %s; only in tpwfnt:%s; only in the fork:%s; tables that differ:%s\n",
                    name, sfnts[0].size, twinBytes.size(), identical ? "identical" : "NOT identical",
                    extra.empty() ? " none" : extra.c_str(), missing.empty() ? " none" : missing.c_str(),
                    differing.empty() ? " none" : differing.c_str());
        // The same font in two builds: same glyph count and design grid; the Windows build also has a 'kern' table.
        CHECK(mac.tables["maxp"].size() >= 6 && windows.tables["maxp"].size() >= 6);
        CHECK(std::memcmp(&mac.tables["maxp"][4], &windows.tables["maxp"][4], 2) == 0);    // numGlyphs
        CHECK(std::memcmp(&mac.tables["head"][18], &windows.tables["head"][18], 2) == 0);  // unitsPerEm
        CHECK(missing.empty());
        CHECK(extra == " kern" || identical);
    }
    if (forks == 0) {
        std::printf("gdi: no .rsrc files in %s, the data disc font checks are skipped\n", dataFonts.string().c_str());
        return;
    }

    // (c) AddFontResource on the data disc path registers the sfnt from the fork.
    for (const char* name : names) {
        if (!fs::exists(dataFonts / (std::string(name) + ".rsrc"), ec)) continue;
        const std::string path = (dataFonts / name).string();
        CHECK(AddFontResource(path.c_str()) == 1);
        CHECK(AddFontResource(path.c_str()) == 1);   // idempotent
    }
    tpw::gdi_text::Style style;
    style.height = -40;
    style.weight = 400;
    style.face = "Big Limbo AOE";
    std::unique_ptr<tpw::gdi_text::Font> limbo = tpw::gdi_text::Font::Create(style);
    CHECK(limbo != nullptr);
    CHECK(limbo->FamilyName() == "Big Limbo AOE");   // the registered face, not the Helvetica fallback
    CHECK(limbo->Ascent() > 0 && limbo->AverageWidth() > 0);
    style.face = "no such font anywhere";
    std::unique_ptr<tpw::gdi_text::Font> fallback = tpw::gdi_text::Font::Create(style);
    CHECK(fallback != nullptr && fallback->FamilyName() == "Helvetica");
    const char sign[] = "Create New Player";
    const double limboWidth = limbo->Advance(sign, 17);
    const double helveticaWidth = fallback->Advance(sign, 17);
    std::printf("gdi: \"%s\" at 40 pixels: %.1f wide in Big Limbo AOE (data disc fork), %.1f in Helvetica\n", sign, limboWidth, helveticaWidth);
    CHECK(limboWidth > 100 && std::fabs(limboWidth - helveticaWidth) > 1);
    // The lookup also finds the face through a CreateFontIndirect font, which draws ink in it.
    InternalHandle* window = GetDC(nullptr);
    tagLOGFONTA logfont;
    std::memset(&logfont, 0, sizeof logfont);
    logfont.lfHeight = -40;
    logfont.lfWidth = 24;
    logfont.lfWeight = 400;
    std::strcpy(logfont.lfFaceName, "Krelesanta AOE");
    InternalHandle* krele = CreateFontIndirect(&logfont);
    InternalHandle* defaultFont = SelectObject(window, krele);
    tagSIZE extent = {0, 0};
    CHECK(GetTextExtentPoint(window, "Haunted", 7, &extent) == 1);
    CHECK(extent.cx > 20 && extent.cy == -40);
    SelectObject(window, defaultFont);
    CHECK(DeleteObject(krele) == 1);
    CHECK(ReleaseDC(nullptr, window) == 1);

    // (d) The Bonus disc's TTF (a data fork) still goes through the URL path. Its BIGLOA face is named
    // "Big Limbo'd Out AOE" (the fork's is "Big Limbod Out AOE"), so the two builds are told apart by name.
    const std::string oracle = (bonusFonts / "BIGLOA__.TTF").string();
    CHECK(AddFontResource(oracle.c_str()) == 1);
    style.face = "Big Limbo'd Out AOE";
    std::unique_ptr<tpw::gdi_text::Font> windows = tpw::gdi_text::Font::Create(style);
    CHECK(windows != nullptr && windows->FamilyName() == "Big Limbo'd Out AOE");
    style.face = "Big Limbod Out AOE";
    std::unique_ptr<tpw::gdi_text::Font> mac = tpw::gdi_text::Font::Create(style);
    CHECK(mac != nullptr && mac->FamilyName() == "Big Limbod Out AOE");
    std::printf("gdi: %d data disc fonts checked against the Bonus disc\n", forks);
    CHECK(tpw::GdiLiveHandles() == 0);
}

// ---- The window fitted to the screen (platform/fit.h) ------------------------------------------------------------
// The arithmetic only: the window and the framebuffer need a display, so the launcher's "fit:" log line and a frame dump
// are the proof of those. Every case is a screen the maintainer has, with the usable bounds SDL gives (the menu bar and the dock
// already taken off) and 28 points of title bar.
static void CheckFit(double usableWide, double usableHigh, double density, int gameWide, int gameHigh, int expectWide,
                     int expectHigh, double expectScale) {
    const tpw::fit::WindowFit fit =
        tpw::fit::ComputeWindowFit(usableWide, usableHigh, 0.0, 28.0, density, gameWide, gameHigh);
    CHECK(fit.pointsWide == expectWide);
    CHECK(fit.pointsHigh == expectHigh);
    CHECK(std::fabs(fit.scale - expectScale) < 1e-3);
}

static void TestFitArithmetic() {
    // The window fills the usable height (or width) of the screen with the game's shape, whatever the game's resolution
    // (the maintainer, 2026-10-13); scale is window pixels per game pixel, above 1 when the picture is enlarged.
    // MacBook 1512x982 at 2x, the dock hidden (usable height 945) or showing (a smaller one).
    CheckFit(1512, 945, 2.0, 2048, 1536, 1222, 917, 1222.0 * 2.0 / 2048.0);
    CheckFit(1512, 860, 2.0, 2048, 1536, 1109, 832, 1109.0 * 2.0 / 2048.0);
    // The screens SDL listed on the maintainer's desk on 2026-10-13: the built-in one set to
    // 2056x1329 points (usable 2056x1290) and the LG UltraFine at 3008x1692 (usable 3008x1662), both 2x: the height
    // less the title bar is the window's height, the width follows (space left and right).
    CheckFit(2056, 1290, 2.0, 2048, 1536, 1682, 1262, 1682.0 * 2.0 / 2048.0);
    CheckFit(3008, 1662, 2.0, 2048, 1536, 2178, 1634, 2178.0 * 2.0 / 2048.0);
    // The same screen with a tall dock: 700 less 28 leaves 672 points.
    CheckFit(1512, 700, 2.0, 2048, 1536, 896, 672, 896.0 * 2.0 / 2048.0);
    // The LG UltraFine 5K at its default scaling: 2560x1440 points at 2x.
    CheckFit(2560, 1400, 2.0, 2048, 1536, 1829, 1372, 1829.0 * 2.0 / 2048.0);
    // A 1440x900 screen at 1x: the usable height 875 less the title bar is 847, so the game is scaled down to 0.55.
    CheckFit(1440, 875, 1.0, 2048, 1536, 1129, 847, 1129.0 * 1.0 / 2048.0);
    // A usable height smaller than the game on a 1x screen (1080p); the width follows the height.
    CheckFit(1920, 1055, 1.0, 2048, 1536, 1369, 1027, 1369.0 * 1.0 / 2048.0);
    // A big 1x screen enlarges the game to its height.
    CheckFit(3840, 2100, 1.0, 2048, 1536, 2762, 2072, 2762.0 * 1.0 / 2048.0);
    // Other resolutions: a small game gets the same window (and a larger scale).
    CheckFit(1512, 945, 2.0, 640, 480, 1222, 917, 1222.0 * 2.0 / 640.0);
    CheckFit(1512, 945, 2.0, 1024, 768, 1222, 917, 1222.0 * 2.0 / 1024.0);
    // A 1.5x screen.
    CheckFit(1512, 945, 1.5, 2048, 1536, 1222, 917, 1222.0 * 1.5 / 2048.0);
    CheckFit(1512, 1100, 1.5, 2048, 1536, 1429, 1072, 1429.0 * 1.5 / 2048.0);
    // A usable area with no room at all still answers a window of a point (never zero), and bad input answers nothing.
    CheckFit(20, 20, 2.0, 2048, 1536, 1, 1, 1.0 * 2.0 / 2048.0);
    const tpw::fit::WindowFit none = tpw::fit::ComputeWindowFit(1000, 1000, 0, 0, 2.0, 0, 1536);
    CHECK(none.pointsWide == 0 && none.pointsHigh == 0);
    // A density that is not positive counts as 1.
    const tpw::fit::WindowFit odd = tpw::fit::ComputeWindowFit(5000, 5000, 0, 0, 0.0, 800, 600);
    CHECK(odd.pointsWide == 5000 && odd.pointsHigh == 3750);
    // The frame also takes width (a window with side borders).
    const tpw::fit::WindowFit framed = tpw::fit::ComputeWindowFit(1000, 1000, 40, 28, 1.0, 2048, 1536);
    CHECK(framed.pointsWide <= 960 && framed.pointsHigh <= 972);
    CHECK(framed.pointsWide == 960 && framed.pointsHigh == 720);

    // Whatever the screen: the window fits what is left, keeps the game's shape to within a point, and touches the
    // usable height or width (to within a point of rounding).
    for (int wide = 200; wide <= 3000; wide += 97) {
        for (int high = 150; high <= 2000; high += 89) {
            for (double density : {1.0, 2.0}) {
                const tpw::fit::WindowFit fit = tpw::fit::ComputeWindowFit(wide, high, 0.0, 28.0, density, 2048, 1536);
                CHECK(fit.pointsWide >= 1 && fit.pointsHigh >= 1);
                CHECK(fit.pointsWide <= std::max(wide, 1));
                CHECK(fit.pointsHigh <= std::max(high - 28, 1));
                CHECK(fit.pointsWide >= wide - 2 || fit.pointsHigh >= high - 28 - 2);
                if (fit.pointsWide > 4 && fit.pointsHigh > 4) {
                    CHECK(std::fabs(fit.pointsWide * 1536.0 - fit.pointsHigh * 2048.0) <= 2048.0);
                }
            }
        }
    }

    // The window centred in the usable bounds, the title bar above the content.
    int left = 0, top = 0;
    tpw::fit::CentreInUsable(0, 37, 1512, 945, 1024, 768, 0, 28, left, top);
    CHECK(left == 244);
    CHECK(top == 37 + (945 - 768 - 28) / 2 + 28 + 1);   // 139.5 rounds up
    tpw::fit::CentreInUsable(1512, 0, 2560, 1400, 1024, 768, 0, 28, left, top);
    CHECK(left == 1512 + (2560 - 1024) / 2);
    CHECK(top == (1400 - 768 - 28) / 2 + 28);
}

#ifdef TPW_PORT_FIX
// ---- Format 7, 24-bit RGB (overrides/platform/upixels.cpp, unit postcard24) -------------------------------------------
// Postcard::save copies the card (format 9, bytes alpha, red, green, blue) into a format 7 buffer of its own and hands its
// rows to the JPEG writer: the copy has a buffer, and its bytes are red, green, blue. Read back, the alpha is the default.
static void TestUPixels24RGB() {
    using namespace SamsUtilities;
    uint8_t card[] = {0xff, 0xfb, 0xf8, 0xc6, 0x80, 0x10, 0x20, 0x30};   // the card's back, then a half-transparent pixel
    UPixels source(kPixels32ARGB, card, 2, 1, -1, nullptr);
    UPixels rgb(kPixels24RGB, 2, 1, nullptr);
    const auto* bytes = static_cast<const uint8_t*>(rgb.GetData());
    CHECK(bytes != nullptr);
    if (bytes == nullptr) return;
    rgb.Convert(source, false);
    const uint8_t expected[] = {0xfb, 0xf8, 0xc6, 0x10, 0x20, 0x30};
    CHECK(std::memcmp(bytes, expected, sizeof expected) == 0);
    UPixels back(kPixels32ARGB, 2, 1, nullptr);
    back.Convert(rgb, false);
    const uint8_t opaque[] = {0xff, 0xfb, 0xf8, 0xc6, 0xff, 0x10, 0x20, 0x30};
    CHECK(std::memcmp(back.GetData(), opaque, sizeof opaque) == 0);
}

// ---- The widescreen screen (include/UI_Base.h) -----------------------------------------------------------------------
// The arithmetic the game runs: the preset's height at the launcher's aspect, the 4:3 box the 2D art keeps, the unit
// space's width and the anchoring of the 2048-unit layout in it. The frames of a 16:9 run are the proof of the rest.
static void TestWideScreen() {
    // The preset's height is kept and the width rounded to an even number of pixels.
    CHECK(UI_WideScreenWidth(1536, 16.0f / 9.0f) == 2730);   // 2730.67: the nearest even width
    CHECK(UI_WideScreenWidth(1536, 16.0f / 10.0f) == 2458);   // 2457.6
    CHECK(UI_WideScreenWidth(1536, 4.0f / 3.0f) == 2048);
    CHECK(UI_WideScreenWidth(768, 16.0f / 9.0f) == 1366);
    CHECK(UI_WideScreenWidth(480, 16.0f / 9.0f) == 854);
    CHECK(UI_WideScreenWidth(1536, 21.0f / 9.0f) == 3584);
    for (int high = 240; high <= 2160; high += 2) {
        for (float aspect : {16.0f / 9.0f, 16.0f / 10.0f, 21.0f / 9.0f}) {
            const int wide = UI_WideScreenWidth(high, aspect);
            CHECK(wide % 2 == 0);
            CHECK(std::fabs(wide - high * aspect) <= 1.0f + 1e-3f);
        }
    }
    // The larger presets keep the width of their 16:9 size and take the height (src/game/TPConfig.cpp).
    CHECK(UI_WideScreenHeight(2560, 16.0f / 9.0f) == 1440);
    CHECK(UI_WideScreenHeight(3200, 16.0f / 9.0f) == 1800);
    CHECK(UI_WideScreenHeight(3840, 16.0f / 9.0f) == 2160);
    CHECK(UI_WideScreenHeight(2560, 16.0f / 10.0f) == 1600);
    CHECK(UI_WideScreenHeight(3200, 16.0f / 10.0f) == 2000);
    CHECK(UI_WideScreenHeight(3840, 16.0f / 10.0f) == 2400);
    for (int wide = 320; wide <= 4096; wide += 2) {
        for (float aspect : {16.0f / 9.0f, 16.0f / 10.0f, 21.0f / 9.0f}) {
            const int high = UI_WideScreenHeight(wide, aspect);
            CHECK(high % 2 == 0);
            CHECK(std::fabs(high - wide / aspect) <= 1.0f + 1e-3f);
        }
    }
    // Every preset's size, as the game sets it and the launcher's fallback measures it.
    {
        const int32_t sizes[3][10][2] = {
            {{512, 384}, {640, 480}, {800, 600}, {1024, 768}, {1280, 1024}, {1600, 1200}, {2048, 1536}, {2560, 1920},
             {3200, 2400}, {4096, 3072}},
            {{682, 384}, {854, 480}, {1066, 600}, {1366, 768}, {1820, 1024}, {2134, 1200}, {2730, 1536}, {2560, 1440},
             {3200, 1800}, {3840, 2160}},
            {{614, 384}, {768, 480}, {960, 600}, {1228, 768}, {1638, 1024}, {1920, 1200}, {2458, 1536}, {2560, 1600},
             {3200, 2000}, {3840, 2400}}};
        const float aspects[3] = {0.0f, 16.0f / 9.0f, 16.0f / 10.0f};
        for (int a = 0; a < 3; ++a) {
            for (int preset = 1; preset <= 10; ++preset) {
                int32_t wide = 0;
                int32_t high = 0;
                UI_PresetSize(preset, aspects[a], wide, high);
                CHECK(wide == sizes[a][preset - 1][0]);
                CHECK(high == sizes[a][preset - 1][1]);
            }
        }
    }
    // The largest one at 16:9: the unit space is 2730 units across, as at 2730 x 1536, and the 4:3 box is 2880 pixels.
    CHECK(UI_UnitWidthFor(3840, 2160) == 2730);
    CHECK(UI_PillarboxLeft(3840, 2160) == 480);
    CHECK(UI_UnitWidthFor(4096, 3072) == 0x800);

    // The 4:3 box in the middle of the screen: the pillarbox of the splash, the loading screens and the films.
    CHECK(UI_PillarboxWidth(1536) == 2048);
    CHECK(UI_PillarboxLeft(2730, 1536) == 341);
    CHECK(UI_PillarboxLeft(2458, 1536) == 205);
    CHECK(UI_PillarboxLeft(1366, 768) == 171);
    CHECK(UI_PillarboxLeft(2048, 1536) == 0);   // 4:3: no bars
    CHECK(UI_PillarboxLeft(1280, 1024) == 0);   // 5:4 (narrower): no bars either
    // The box and its two bars cover the screen, the bars to within a pixel of each other.
    for (int high = 240; high <= 2160; high += 6) {
        const int wide = UI_WideScreenWidth(high, 16.0f / 9.0f);
        const int left = UI_PillarboxLeft(wide, high);
        const int right = wide - left - UI_PillarboxWidth(high);
        CHECK(left >= 0 && right >= left && right - left <= 1);
    }

    // The unit space: 2048 units span the 4:3 box, so a unit is as wide as it is high (1536 units span the height).
    CHECK(UI_UnitWidthFor(2048, 1536) == 0x800);
    CHECK(UI_UnitWidthFor(1280, 1024) == 0x800);
    CHECK(UI_UnitWidthFor(2730, 1536) == 2730);
    CHECK(UI_UnitWidthFor(2458, 1536) == 2458);
    CHECK(UI_UnitWidthFor(1366, 768) == 2732);
    CHECK(UI_UnitWidthFor(0, 0) == 0x800);
    // The mouse: the game maps a pixel to units by (px << 11) / the 4:3 box's width, so the first pixel is unit 0 and
    // the last one lands inside the unit space, never past it; the middle pixel is the middle unit.
    for (int high : {480, 600, 768, 1024, 1200, 1536, 1440, 1800, 2160, 2400}) {
        for (float aspect : {16.0f / 9.0f, 16.0f / 10.0f}) {
            const int wide = UI_WideScreenWidth(high, aspect);
            const int box = UI_PillarboxWidth(high);
            const int units = UI_UnitWidthFor(wide, high);
            CHECK(((wide - 1) << 11) / box < units);
            CHECK(((wide - 1) << 11) / box >= units - (0x800 / box) - 2);
            CHECK(std::abs(((wide / 2) << 11) / box - units / 2) <= 2);
        }
    }

    // The anchoring: nothing moves on a 4:3 screen.
    CHECK(UI_WideAnchor(0, 0x7ff, 0x800) == 0);
    CHECK(UI_WideAnchor(1700, 0x7ff, 0x800) == 0);
    // At 16:9 (2730 units, 682 more): a full-width window goes to the middle (341 each side), the money and the
    // control panel (left third) stay left, the advisor and the lobby's bars (right third) go to the right edge, a
    // dialog in the middle goes to the middle.
    CHECK(UI_WideAnchor(0, 0x7ff, 2730) == 341);
    CHECK(UI_WideAnchor(-10, 0x800, 2730) == 341);
    CHECK(UI_WideAnchor(0, 300, 2730) == 0);
    CHECK(UI_WideAnchor(20, 650, 2730) == 0);
    CHECK(UI_WideAnchor(1700, 0x7ff, 2730) == 682);
    CHECK(UI_WideAnchor(1050, 2040, 2730) == 682);
    CHECK(UI_WideAnchor(600, 1400, 2730) == 341);
    // The thirds' edges: a centre at exactly 682 or 1365 is in the middle third.
    CHECK(UI_WideAnchor(682, 682, 2730) == 341);
    CHECK(UI_WideAnchor(681, 681, 2730) == 0);
    CHECK(UI_WideAnchor(1365, 1365, 2730) == 341);
    CHECK(UI_WideAnchor(1366, 1366, 2730) == 682);
    // A right-anchored window keeps its distance from the right edge; a left one from the left edge.
    CHECK(0x7ff - 1700 == (2730 - 1) - (1700 + UI_WideAnchor(1700, 0x7ff, 2730)));
    // 16:10 (2458 units, 410 more).
    CHECK(UI_WideAnchor(0, 0x7ff, 2458) == 205);
    CHECK(UI_WideAnchor(1700, 0x7ff, 2458) == 410);

    // The window of a 16:9 game on a 16:9 screen has no bars: the box fills it to the pixel.
    for (int wide : {1920, 2560, 3840}) {
        const int high = wide * 9 / 16;
        const tpw::fit::PixelBox pixels = tpw::fit::RoundBox(tpw::fit::Letterbox(wide, high, 2730, 1536));
        CHECK(pixels.left == 0 && pixels.top == 0 && pixels.right == wide && pixels.bottom == high);
    }
    // The MacBook (2056 x 1290 usable points at 2x): the 16:9 game fills its width, as the launcher logged on 2026-10-20
    // ("fit: game 2730x1536 in a 2056x1156-point window").
    CheckFit(2056, 1290, 2.0, 2730, 1536, 2056, 1156, 2056.0 * 2.0 / 2730.0);
    CheckFit(2056, 1290, 2.0, 2458, 1536, 2019, 1262, 2019.0 * 2.0 / 2458.0);   // 16:10 fills the height
}

// ---- The widescreen picks (include/UI_Base.h, unit v5L51) ------------------------------------------------------------
// The 3D view (CMatrix::Projection and Matrix_Init's x scale of height over width) puts a point x / z across at pixel
// (1 + 2 (H / W) x / (z tan(fov / 2))) W / 2: it keeps the height's angle and widens. The pointer's ray
// (CalculatePointerWorldPosn) leaves the camera at x / z = 0.8 tan(0.885 fov / 2) (2 px / W - 1), its corners' 4:3
// reach, times UI_PickAcrossScale on a wider screen. Here the two are compared across the screen: the ray must point at
// the pixel under the pointer. The frames of 16:9 and 4:3 runs are the proof of the rest.
static double PickAcrossPixel(int32_t wide, int32_t high, double px, bool scaled) {
    const double fov = 1.5707963705062866;   // gCameraFov: InitCamera's 90 degrees
    const float centreX = static_cast<float>(static_cast<uint32_t>(wide)) * 0.5f;   // gViewCentreX (src/game/TPConfig.cpp)
    const float centreY = static_cast<float>(static_cast<uint32_t>(high)) * 0.5f;
    const double k = scaled ? UI_PickAcrossScale(centreX, centreY) : 1.0;
    const double sx = px / (2.0 * centreX);
    const double across = 0.8 * std::tan(fov * 0.5 * 0.885) * k * (2.0 * sx - 1.0);   // x / z of the ray
    return (1.0 + 2.0 * (static_cast<double>(high) / wide) * across / std::tan(fov * 0.5)) * wide * 0.5;
}

static void TestWidePick() {
    // 1 on a 4:3 view, so the original's ray is kept there; 4/3 at 16:9, 6/5 at 16:10.
    CHECK(UI_PickAcrossScale(2048.0f, 1536.0f) == 1.0f);
    CHECK(UI_PickAcrossScale(320.0f, 240.0f) == 1.0f);
    CHECK(std::fabs(UI_PickAcrossScale(1920.0f, 1080.0f) - 4.0f / 3.0f) < 1e-6f);
    CHECK(std::fabs(UI_PickAcrossScale(1280.0f, 800.0f) - 1.2f) < 1e-6f);
    const int32_t sizes[][2] = {{4096, 3072}, {1024, 768}, {3840, 2160}, {2730, 1536}, {1366, 768}, {2560, 1600}, {3840, 2400}};
    for (const auto& size : sizes) {
        const int32_t wide = size[0];
        const int32_t high = size[1];
        for (int32_t step = 0; step <= 64; ++step) {
            const double px = (wide - 1) * step / 64.0;
            // The ray hits the pixel under the pointer to within a thousandth of the screen: the Mac's own reach on its
            // 4:3 screen, where 0.8 tan(0.885 x 45 degrees) is 0.6671 for the 2/3 the view spans (1.4 pixels at 4096).
            CHECK(std::fabs(PickAcrossPixel(wide, high, px, true) - px) < wide * 0.001);
        }
        if (wide * 3 > high * 4) {
            // Without the scale the ray at the screen's edge points an eighth of the screen in from it at 16:9 (the bug).
            CHECK(std::fabs(PickAcrossPixel(wide, high, 0.0, false)) > wide * 0.08);
        }
    }

    // The hot sprite test's pointer x: the Mac's x / 1024 - 1, to the bit, on a 4:3 screen.
    for (int32_t x = -32768; x <= 32767; ++x) {
        const float mac = static_cast<float>(static_cast<int16_t>(x)) * 0.0009765625f - 1.0f;
        const float port = UI_UnitToViewX(static_cast<int16_t>(x), 0x800);
        CHECK(std::memcmp(&mac, &port, 4) == 0);
    }
    // On a wider screen -1 at the left edge, 0 in the middle and 1 at the right, as the projected view has it: the
    // pointer's units (Will_HandleCameraMessages: units x 4:3 box width / 2048 pixels) at the view's -1..1 to within 0.001.
    for (const auto& size : sizes) {
        const int32_t wide = size[0];
        const int32_t high = size[1];
        const int16_t units = UI_UnitWidthFor(wide, high);
        const int32_t box = wide * 3 > high * 4 ? UI_PillarboxWidth(high) : wide;
        for (int32_t x = 0; x < units; x += 7) {
            const double pixel = static_cast<double>(x * box / 2048);
            CHECK(std::fabs(UI_UnitToViewX(static_cast<int16_t>(x), units) - (2.0 * pixel / wide - 1.0)) < 0.002);
        }
    }
}
#endif

#ifdef TPW_PORT_FIX
// ---- The fonts' scale (include/TbIRLE4bitFont.h) ---------------------------------------------------------------------
// The line heights and advances scale with the UI (a byte still holds the line height) and DrawChar's glyphs are resampled
// to match. The frames of a 3840 x 2160 run are the proof of the rest.
static uint8_t NybbleAt(const uint8_t* pixels, int32_t width, int32_t x, int32_t y) {
    const uint32_t i = static_cast<uint32_t>(y * width + x);
    return (i & 1) != 0 ? pixels[i >> 1] & 0xf : pixels[i >> 1] >> 4;
}

static void TestFontScale() {
    std::vector<uint8_t> out;
    // Scale 1 (a UI no larger than 1024 x 768's): every font as it is.
    gTbFontScale = 1.0f;
    CHECK(TbFontScaleFor(13) == 1.0f && TbFontScaled(13, TbFontScaleFor(13)) == 13);
    {
        const uint8_t glyph[2] = {0xff, 0xff};
        const uint8_t* pixels = glyph;
        uint16_t width = 2, height = 2;
        int32_t x = 1, y = -3;
        TbFontScaleGlyph(13, pixels, width, height, x, y, out);
        CHECK(pixels == glyph && width == 2 && height == 2 && x == 1 && y == -3);
    }
    // 3840 x 2160 at 16:9: the 2880 x 2160 box, 2.8125 times 1024 x 768.
    gTbFontScale = 2.8125f;
    CHECK(TbFontScaleFor(13) == 2.8125f);
    CHECK(TbFontScaled(13, TbFontScaleFor(13)) == 37);   // GAME9's line, 36.6
    CHECK(TbFontScaled(51, TbFontScaleFor(13)) == 143);  // CASHBIG's widest advance
    CHECK(TbFontScaleFor(0) == 1.0f);
    // The scaled line height always fits its byte: CASHBIG (88) stops at 255 at 4096 x 3072's scale of 4.
    for (float scale : {1.25f, 2.0f, 2.8125f, 4.0f, 8.0f}) {
        gTbFontScale = scale;
        for (uint32_t line = 1; line <= 255; ++line) CHECK(TbFontScaled(line, TbFontScaleFor(line)) <= 255);
    }
    gTbFontScale = 4.0f;
    CHECK(TbFontScaled(88, TbFontScaleFor(88)) == 255);
    CHECK(TbFontScaled(55, TbFontScaleFor(55)) == 220);   // MENUBIG keeps the whole scale
    // A solid 4 x 4 one-colour glyph at scale 2: 8 x 8, solid inside, the edges a step lighter, the offsets doubled.
    gTbFontScale = 2.0f;
    {
        const uint8_t glyph[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        const uint8_t* pixels = glyph;
        uint16_t width = 4, height = 4;
        int32_t x = 1, y = -3;
        TbFontScaleGlyph(13, pixels, width, height, x, y, out);
        CHECK(pixels == out.data() && width == 8 && height == 8 && x == 2 && y == -6);
        for (int32_t row = 1; row < 7; ++row)
            for (int32_t column = 1; column < 7; ++column) CHECK(NybbleAt(pixels, 8, column, row) == 0xf);
        CHECK(NybbleAt(pixels, 8, 0, 3) > 8 && NybbleAt(pixels, 8, 0, 3) < 0xf);
        CHECK(NybbleAt(pixels, 8, 0, 0) > 0 && NybbleAt(pixels, 8, 0, 0) < NybbleAt(pixels, 8, 0, 3));
    }
    // An empty glyph stays empty; a lone antialiased grey pixel stays a soft dot no darker than it was.
    {
        const uint8_t glyph[2] = {0x00, 0x00};
        const uint8_t* pixels = glyph;
        uint16_t width = 2, height = 2;
        int32_t x = 0, y = 0;
        TbFontScaleGlyph(13, pixels, width, height, x, y, out);
        CHECK(width == 4 && height == 4);
        for (int32_t i = 0; i < 16; ++i) CHECK(NybbleAt(pixels, 4, i % 4, i / 4) == 0);
        const uint8_t grey[2] = {0x80, 0x00};
        pixels = grey;
        width = 2, height = 2;
        TbFontScaleGlyph(13, pixels, width, height, x, y, out);
        uint8_t darkest = 0;
        for (int32_t i = 0; i < 16; ++i) darkest = std::max(darkest, NybbleAt(pixels, 4, i % 4, i / 4));
        CHECK(darkest > 0 && darkest <= 8);
    }
    // The writers take the glyph's width as a byte: a wide glyph is scaled no wider than 255.
    gTbFontScale = 8.0f;
    {
        std::vector<uint8_t> wide(50 * 2 / 2, 0xff);
        const uint8_t* pixels = wide.data();
        uint16_t width = 50, height = 2;
        int32_t x = 0, y = 0;
        TbFontScaleGlyph(13, pixels, width, height, x, y, out);
        CHECK(width <= 255 && width >= 254 && out.size() == static_cast<size_t>((width * height + 1) / 2));
    }
    gTbFontScale = 1.0f;
}
#endif

static void TestFitLetterbox() {
    // A window of the game's own shape: the box is the whole window.
    tpw::fit::Rect box = tpw::fit::Letterbox(1024, 768, 2048, 1536);
    CHECK(box.left == 0.0 && box.top == 0.0 && box.width == 1024.0 && box.height == 768.0);
    // Wider than the game: bars left and right (the height decides). 1600 - 1024 = 576, 288 each.
    box = tpw::fit::Letterbox(1600, 768, 2048, 1536);
    CHECK(box.left == 288.0 && box.top == 0.0 && box.width == 1024.0 && box.height == 768.0);
    // Taller than the game: bars above and below.
    box = tpw::fit::Letterbox(1024, 1000, 2048, 1536);
    CHECK(box.left == 0.0 && box.top == 116.0 && box.width == 1024.0 && box.height == 768.0);
    // Smaller than the game: scaled down, centred.
    box = tpw::fit::Letterbox(896, 800, 2048, 1536);
    CHECK(box.width == 896.0 && box.height == 672.0 && box.left == 0.0 && box.top == 64.0);
    // Nothing to show in: an empty box.
    box = tpw::fit::Letterbox(0, 100, 2048, 1536);
    CHECK(box.width == 0.0 && box.height == 0.0);
    box = tpw::fit::Letterbox(100, 100, 0, 0);
    CHECK(box.width == 0.0 && box.height == 0.0);
    // The pixels of a box that is not a whole number: edges rounded, so neighbours neither overlap nor leave a gap.
    const tpw::fit::PixelBox pixels = tpw::fit::RoundBox({10.4, 20.6, 100.5, 50.2});
    CHECK(pixels.left == 10 && pixels.top == 21 && pixels.right == 111 && pixels.bottom == 71);
}

// The game's full-screen box is a real full-screen window only through the fit and never with --background; the
// window is then the whole screen and the game's picture is letterboxed into it.
static void TestFitFullScreen() {
    CHECK(tpw::fit::RealFullScreen(true, true, false));
    CHECK(!tpw::fit::RealFullScreen(false, true, false));   // --windowed: the document window
    CHECK(!tpw::fit::RealFullScreen(true, true, true));     // --background: the fitted window behind the others
    CHECK(!tpw::fit::RealFullScreen(true, false, false));   // --no-fit: the window is the game's size
    // A 16:10 screen (1440 x 900 points) holding a 16:9 game: bars above and below (45 each), none at the sides.
    tpw::fit::Rect box = tpw::fit::Letterbox(1440, 900, 3840, 2160);
    CHECK(box.left == 0.0 && box.width == 1440.0 && box.height == 810.0 && box.top == 45.0);
    // The same screen at 4:3: bars at the sides only (120 each).
    box = tpw::fit::Letterbox(1440, 900, 2048, 1536);
    CHECK(box.top == 0.0 && box.height == 900.0 && box.width == 1200.0 && box.left == 120.0);
    // A 16:9 screen holding a 16:9 game: no bars at all.
    box = tpw::fit::Letterbox(1920, 1080, 3840, 2160);
    CHECK(box.left == 0.0 && box.top == 0.0 && box.width == 1920.0 && box.height == 1080.0);
}

static void TestFitMouse() {
    const int gameWide = 2048, gameHigh = 1536;
    double gx = 0, gy = 0;
    // Window and game of one shape and a scale of 1/2: x2.
    tpw::fit::Rect box = tpw::fit::Letterbox(1024, 768, gameWide, gameHigh);
    tpw::fit::WindowToGame(box, gameWide, gameHigh, 512.0, 384.0, gx, gy);
    CHECK(gx == 1024.0 && gy == 768.0);
    tpw::fit::WindowToGame(box, gameWide, gameHigh, 0.0, 0.0, gx, gy);
    CHECK(gx == 0.0 && gy == 0.0);
    // Bars left and right: a click in the left bar is left of the game, in the right bar right of it; both clamp.
    box = tpw::fit::Letterbox(1600, 768, gameWide, gameHigh);
    tpw::fit::WindowToGame(box, gameWide, gameHigh, 100.0, 384.0, gx, gy);
    CHECK(gx < 0.0 && gy == 768.0);
    CHECK(tpw::fit::ClampToGame(gx, gameWide) == 0.0);
    tpw::fit::WindowToGame(box, gameWide, gameHigh, 1500.0, 384.0, gx, gy);
    CHECK(gx > gameWide);
    CHECK(tpw::fit::ClampToGame(gx, gameWide) == gameWide - 1);
    // The box's own edges: its left edge is game pixel 0, its right edge is the width (clamped to the last pixel).
    tpw::fit::WindowToGame(box, gameWide, gameHigh, 288.0, 0.0, gx, gy);
    CHECK(gx == 0.0 && gy == 0.0);
    tpw::fit::WindowToGame(box, gameWide, gameHigh, 288.0 + 1024.0, 768.0, gx, gy);
    CHECK(gx == gameWide && gy == gameHigh);
    CHECK(static_cast<int>(tpw::fit::ClampToGame(gx, gameWide)) == gameWide - 1);
    CHECK(static_cast<int>(tpw::fit::ClampToGame(gy, gameHigh)) == gameHigh - 1);
    // Bars above and below.
    box = tpw::fit::Letterbox(1024, 1000, gameWide, gameHigh);
    tpw::fit::WindowToGame(box, gameWide, gameHigh, 512.0, 10.0, gx, gy);
    CHECK(gy < 0.0);
    CHECK(tpw::fit::ClampToGame(gy, gameHigh) == 0.0);
    tpw::fit::WindowToGame(box, gameWide, gameHigh, 512.0, 990.0, gx, gy);
    CHECK(gy > gameHigh);
    // The clamp: negative, NaN and infinite values, and a degenerate size.
    CHECK(tpw::fit::ClampToGame(-5.0, 100) == 0.0);
    CHECK(tpw::fit::ClampToGame(std::nan(""), 100) == 0.0);
    CHECK(tpw::fit::ClampToGame(1e30, 100) == 99.0);
    CHECK(tpw::fit::ClampToGame(-1e30, 100) == 0.0);
    CHECK(tpw::fit::ClampToGame(50.5, 100) == 50.5);
    CHECK(tpw::fit::ClampToGame(5.0, 0) == 0.0);
    // An empty box maps to the origin rather than dividing by zero.
    tpw::fit::WindowToGame(tpw::fit::Rect(), gameWide, gameHigh, 10.0, 10.0, gx, gy);
    CHECK(gx == 0.0 && gy == 0.0);
    tpw::fit::GameToWindow(tpw::fit::Rect(), 0, 0, 10.0, 10.0, gx, gy);
    CHECK(gx == 0.0 && gy == 0.0);

    // Both ways agree: a game pixel to the window and back, for boxes of several shapes and scales.
    const tpw::fit::Rect boxes[] = {tpw::fit::Letterbox(1024, 768, gameWide, gameHigh), tpw::fit::Letterbox(1600, 768, gameWide, gameHigh),
                                    tpw::fit::Letterbox(1024, 1000, gameWide, gameHigh), tpw::fit::Letterbox(1129, 900, gameWide, gameHigh),
                                    tpw::fit::Letterbox(2048, 1536, gameWide, gameHigh)};
    for (const tpw::fit::Rect& each : boxes) {
        for (int gameX = 0; gameX < gameWide; gameX += 211) {
            for (int gameY = 0; gameY < gameHigh; gameY += 173) {
                double wx = 0, wy = 0, backX = 0, backY = 0;
                tpw::fit::GameToWindow(each, gameWide, gameHigh, gameX, gameY, wx, wy);
                CHECK(wx >= each.left - 1e-9 && wx <= each.left + each.width + 1e-9);
                CHECK(wy >= each.top - 1e-9 && wy <= each.top + each.height + 1e-9);
                tpw::fit::WindowToGame(each, gameWide, gameHigh, wx, wy, backX, backY);
                CHECK(std::fabs(backX - gameX) < 1e-6 && std::fabs(backY - gameY) < 1e-6);
            }
        }
    }

    // Without a window nothing is fitted and the mouse is not touched.
    CHECK(tpw::config().fit);
    CHECK(!tpw::FitActive());
    double mx = 0, my = 0;
    tpw::MouseToGame(123.5, -7.25, mx, my);
    CHECK(mx == 123.5 && my == -7.25);
    int wide = -1, high = -1;
    tpw::GameSize(wide, high);
    CHECK(wide == 0 && high == 0);
    tpw::SetGameSize(640, 480);   // (no window: ignored)
    tpw::CursorPlace place;
    CHECK(!tpw::CursorPlacement(place));
}

// ---- The host sound module (platform/sound_module.cpp over platform/sound_device.cpp, SDL3 audio) ------------------------
// The module is the real CSprocketsModule over the real TbSoundSystemModule base (compiled into this program by CMake's
// target_sources). What it pulls from, the game's TbSoundPipeChannel, is not linked: the four members the module calls are
// defined below over a FakePipe, and the SDL device is the "dummy" driver, so no sound card is needed.
namespace {

// A pipe that delivers a known pattern. Call n of the callback fills the chunk with samples (n + 1) * 256 + (k & 0xff) as
// 16-bit values (bytes (n + 1) * 16 + (k & 15) for 8-bit samples) and answers true, except the call numbered overAt, which
// leaves the buffer silent and answers false (the pipe is over).
struct FakePipe {
    uint32_t index = 0;
    uint32_t sampleFlags = 0;
    TbSampleHeader header = {};
    std::atomic<uint32_t> calls{0};
    uint32_t overAt = 0xffffffffu;
    std::atomic<uint32_t> lastSize{0};
    uint32_t hookAt = 0xffffffffu;      // the call that runs hook first (a game callback commanding the module from inside the pull)
    std::function<void()> hook;
};

FakePipe* Fake(const void* pipe) {
    return static_cast<FakePipe*>(const_cast<void*>(pipe));
}

TbSoundPipeChannel* AsPipe(FakePipe* fake) {
    return reinterpret_cast<TbSoundPipeChannel*>(fake);
}

// A record of the game's that has no constructor in this program: all zero, as the game's constructors leave the words the
// test does not set.
template <class T>
struct Zeroed {
    alignas(T) unsigned char bytes[sizeof(T)];
    Zeroed() { std::memset(bytes, 0, sizeof bytes); }
    T& operator*() { return *reinterpret_cast<T*>(bytes); }
    T* operator->() { return reinterpret_cast<T*>(bytes); }
};

// The pattern sample k of call n.
int16_t PatternSample(uint32_t call, uint32_t k) {
    return static_cast<int16_t>(((call + 1) << 8) | (k & 0xff));
}

bool WaitFor(const std::function<bool()>& done, int milliseconds) {
    for (int waited = 0; waited < milliseconds; waited += 10) {
        if (done()) return true;
        SDL_Delay(10);
    }
    return done();
}

void SetUpPipe(FakePipe& pipe, uint32_t index, uint32_t rate, uint32_t bits, bool stereo, uint32_t sampleFlags = 0) {
    pipe.index = index;
    pipe.sampleFlags = sampleFlags;
    pipe.header.mSize = 100000;
    pipe.header.mRate = static_cast<uint16_t>(rate);
    pipe.header.mBits = static_cast<uint8_t>(bits);
    pipe.header.mFlags = stereo ? 1 : 0;
    pipe.calls = 0;
    pipe.overAt = 0xffffffffu;
    pipe.hookAt = 0xffffffffu;
    pipe.hook = nullptr;
}

// The info a new sample is started with: flag 0x2000 in the sample flags word (+0x0c) and nothing else.
TbError StartSample(CSprocketsModule& module, uint32_t channel, FakePipe& pipe) {
    Zeroed<TbSoundSampleInfo> info;
    info->mField0C = 0x2000;
    return module.SetChannelInfo(channel, AsPipe(&pipe), *info);
}

}  // namespace

uint32_t TbSoundPipeChannel::GetIndex() {
    return Fake(this)->index;
}

uint32_t TbSoundPipeChannel::GetSampleFlags() {
    return Fake(this)->sampleFlags;
}

TbSampleHeader* TbSoundPipeChannel::GetSampleHeader() {
    return &Fake(this)->header;
}

// The MPEG file player pool (src/sound/LbMpeg.cpp, src/sound/MpegDecode.cpp, src/sound/CSprocketsModule.cpp, src/sound/StreamableMpeg2.cpp, not linked here): the two members the module calls are counted.
static int gKeepFullCalls = 0;
static uint8_t gKeepFullArgument = 0xff;
static int gMaintainCalls = 0;

template <>
void SamsUtilities::MpegFilePlayerPool::SetPoolKeepFull(uint8_t flag) {
    ++gKeepFullCalls;
    gKeepFullArgument = flag;
}

template <>
void SamsUtilities::MpegFilePlayerPool::MaintainPoolLevel() {
    ++gMaintainCalls;
}

bool TbSoundPipeChannel::StreamerCallback(void* buffer, uint32_t size, void* pipe) {
    FakePipe* fake = Fake(pipe);
    const uint32_t call = fake->calls++;
    fake->lastSize = size;
    if (call == fake->hookAt && fake->hook) fake->hook();
    if (call >= fake->overAt) {
        std::memset(buffer, 0, size);
        return false;
    }
    if (fake->header.mBits == 8) {
        uint8_t* bytes = static_cast<uint8_t*>(buffer);
        for (uint32_t k = 0; k < size; ++k) bytes[k] = static_cast<uint8_t>(((call + 1) << 4) | (k & 15));
    } else {
        for (uint32_t k = 0; k < size / 2; ++k) {
            const int16_t sample = PatternSample(call, k);
            std::memcpy(static_cast<uint8_t*>(buffer) + 2 * k, &sample, 2);
        }
    }
    return true;
}

static void TestSound(const fs::path& scratch) {
    // No sound card is needed or wanted: SDL's dummy driver takes the samples and drops them.
    SetEnv("SDL_AUDIO_DRIVER", "dummy");
    UnsetEnv("TPW_SOUND_CAPTURE");
    const int baselineStreams = tpw::SoundDevice::LiveStreams();
    CHECK(baselineStreams == 0);

    CHECK(tpw::SoundBytesPerSample(8) == 1);
    CHECK(tpw::SoundBytesPerSample(16) == 2);
    CHECK(tpw::SoundBytesPerSample(32) == 4);
    CHECK(tpw::SoundBytesPerSample(12) == 0);
    CHECK(tpw::SoundBytesPerSample(0) == 0);

    // Safe mode is only kept.
    CHECK(!tpw::SoundSafeMode());
    SamsUtilities::USoundChannel::SetSafeMode(1);
    CHECK(tpw::SoundSafeMode());
    SamsUtilities::USoundChannel::SetSafeMode(0);
    CHECK(!tpw::SoundSafeMode());

    // ---- Before Init: no channels, every command that needs one answers -1 (a NULL pipe is the one exception: 0).
    TbSpeaker::AutoDelete::_mpSpeaker = nullptr;
    CSprocketsModule* module = new CSprocketsModule();
    CHECK(TbSpeaker::AutoDelete::_mpSpeaker != nullptr);   // the CSprocketsSpeaker block the base frees
    CHECK(module->Channels() == nullptr);
    CHECK(module->ChannelAt(0) == nullptr);
    CHECK(static_cast<int32_t>(module->StopChannel(0)) == -1);
    CHECK(static_cast<int32_t>(module->PauseChannel(0)) == -1);
    CHECK(static_cast<int32_t>(module->RestartChannel(0)) == -1);
    CHECK(static_cast<int32_t>(module->SetListenerPosition(TbSoundPosition(1.0f, 2.0f, 3.0f))) == -1);
    CHECK(static_cast<int32_t>(module->StreamData(nullptr)) == 0);
    CHECK(static_cast<int32_t>(module->StopAll()) == 0);   // over zero channels
    CHECK(static_cast<int32_t>(module->SetOptions(5)) == 0);

    // ---- Init with 4 buffers makes 4 channels and answers 0.
    Zeroed<TbSoundSystemInfo> systemInfo;
    systemInfo->mBuffersToUse = 4;
    CHECK(static_cast<int32_t>(module->Init(*systemInfo)) == 0);
    CHECK(module->mBuffers == 4);
    CHECK(module->mRate == 22050);
    // Init sets the MPEG file player pool to be kept full and maintains it (the Tick that ends Init); each Tick maintains it.
    CHECK(gKeepFullCalls == 1 && gKeepFullArgument == 1);
    CHECK(gMaintainCalls == 1);
    module->Tick();
    CHECK(gMaintainCalls == 2);
    CHECK(module->Channels() != nullptr);
    for (uint32_t i = 0; i < 4; ++i) {
        CHECK(module->ChannelAt(i) != nullptr);
        CHECK(module->ChannelAt(i)->Index() == i);
        CHECK(module->ChannelAt(i)->Chunk() == 0);
        CHECK(!module->ChannelAt(i)->GetDescription());
    }
    CHECK(module->ChannelAt(4) == nullptr);
    CHECK(module->ChannelAt(0xffffffffu) == nullptr);
    CHECK(tpw::SoundDevice::LiveStreams() == baselineStreams + 4);
    CHECK(module->Device()->IsOpen());
    CHECK(module->Device()->StreamCount() == 4);
    CHECK(module->Listener().scale[0] == 0.01f && module->Listener().scale[1] == 0.0f);
    CHECK(module->Listener().soundDistance == 400.0);
    tpw::SoundDevice& device = *module->Device();
    // The device is frozen while a test looks at what has been pulled, so that SDL's own pulling cannot interfere.
    device.SetDevicePaused(true);

    CHECK(static_cast<int32_t>(module->StopChannel(4)) == -1);   // outside the array
    CHECK(static_cast<int32_t>(module->PauseChannel(4)) == -1);
    CHECK(static_cast<int32_t>(module->RestartChannel(4)) == -1);
    CHECK(static_cast<int32_t>(module->StopAll()) == 0);
    CHECK(static_cast<int32_t>(module->PauseAll()) == 0);
    CHECK(static_cast<int32_t>(module->RestartAll()) == 0);

    // ---- SetChannelInfo with a new sample: the pipe, the description, the 3D flag, the preroll (nothing is pulled yet).
    FakePipe mono;
    FakePipe stereo;
    FakePipe narrow;
    FakePipe odd;
    SetUpPipe(mono, 0, 22050, 16, false);
    SetUpPipe(stereo, 1, 22050, 16, true, 1);
    SetUpPipe(narrow, 2, 11025, 8, false);
    SetUpPipe(odd, 3, 22050, 12, false);
    CHECK(static_cast<int32_t>(StartSample(*module, 0, mono)) == 0);
    CHECK(module->ChannelAt(0)->Chunk() == 0x1200);
    CHECK(module->ChannelAt(0)->Is3D() == 1);   // sample flags bit 0 clear: a 3D sound
    CHECK(module->ChannelAt(0)->GetDescription());
    CHECK(module->ChannelAt(0)->Pipe() == AsPipe(&mono));
    CHECK(device.Chunk(0) == 0x1200);
    CHECK(mono.calls == 0);   // Preroll pulls nothing
    CHECK(device.Pulled(0) == 0);
    CHECK(!device.IsBound(0));
    CHECK(static_cast<int32_t>(StartSample(*module, 1, stereo)) == 0);
    CHECK(module->ChannelAt(1)->Chunk() == 0x2400);
    CHECK(module->ChannelAt(1)->Is3D() == 0);   // sample flags bit 0: a 2D sound
    CHECK(static_cast<int32_t>(StartSample(*module, 2, narrow)) == 0);
    CHECK(module->ChannelAt(2)->Chunk() == 0x1200);
    CHECK(module->ChannelAt(2)->GetDescription());
    // A width SDL cannot play: no description, the command fails, and StreamData refuses the channel.
    CHECK(static_cast<int32_t>(StartSample(*module, 3, odd)) == -1);
    CHECK(!module->ChannelAt(3)->GetDescription());
    CHECK(module->ChannelAt(3)->Chunk() == 0);
    CHECK(static_cast<int32_t>(module->StreamData(AsPipe(&odd))) == -1);
    CHECK(odd.calls == 0);
    // A channel that does not exist: -1 (the sample store handing over a NULL pipe fails the command as well).
    FakePipe stray;
    SetUpPipe(stray, 9, 22050, 16, false);
    CHECK(static_cast<int32_t>(StartSample(*module, 9, stray)) == -1);
    CHECK(static_cast<int32_t>(module->StreamData(AsPipe(&stray))) == -1);
    {
        Zeroed<TbSoundSampleInfo> info;
        info->mField0C = 0x2000;
        CHECK(static_cast<int32_t>(module->SetChannelInfo(0, nullptr, *info)) == -1);
        CHECK(module->ChannelAt(0)->Pipe() == AsPipe(&mono));   // kept
    }

    // ---- The volume: 0..127 becomes the stream's gain 0.0..1.0; SetChannelInfo flag 2 and SetChannelVolume.
    {
        Zeroed<TbSoundSampleInfo> info;
        const uint32_t volumes[] = {0, 64, 127};
        const float gains[] = {0.0f, 0.504f, 1.0f};
        CHECK(std::fabs(device.Gain(0) - 1.0f) < 1e-6);   // a new stream is at full volume
        for (int i = 0; i < 3; ++i) {
            info->mFlags = 2;
            info->mVolume = volumes[i];
            CHECK(static_cast<int32_t>(module->SetChannelInfo(0, AsPipe(&mono), *info)) == 0);
            CHECK(info->mFlags == 0);   // consumed
            CHECK(std::fabs(device.Gain(0) - gains[i]) < 0.001f);
            info->mFlags = 0x10;   // SetChannelVolume touches no flags
            info->mVolume = volumes[2 - i];
            CHECK(static_cast<int32_t>(module->SetChannelVolume(1, *info)) == 0);
            CHECK(info->mFlags == 0x10);
            CHECK(std::fabs(device.Gain(1) - gains[2 - i]) < 0.001f);
        }
        info->mVolume = 64;
        CHECK(std::fabs(device.Gain(1) - 0.0f) < 1e-6);
        CHECK(static_cast<int32_t>(module->SetChannelVolume(9, *info)) == -1);
        CHECK(static_cast<int32_t>(module->SetChannelVolume(4, *info)) == -1);
        CHECK(std::fabs(tpw::SoundDevice::LiveStreams() - (baselineStreams + 4)) == 0);
    }

    // ---- The flags: 0x400000 (frequency), 0x200 (distance) and 0x20 (pitch) are consumed and nothing is done; any other
    // flag stays set and the command answers -1.
    {
        Zeroed<TbSoundSampleInfo> info;
        const uint32_t consumed[] = {0x400000, 0x200, 0x20, 0x400000 | 0x200 | 0x20, 0x200 | 0x2};
        for (uint32_t flags : consumed) {
            info->mFlags = flags;
            info->mVolume = 127;
            CHECK(static_cast<int32_t>(module->SetChannelInfo(0, AsPipe(&mono), *info)) == 0);
            CHECK(info->mFlags == 0);
        }
        const uint32_t unknown[] = {4, 8, 0x10, 0x40, 0x80, 0x100, 0x400, 0x800, 0x1000, 0x8000, 0x10000000};
        for (uint32_t flag : unknown) {
            info->mFlags = flag | 0x200;
            CHECK(static_cast<int32_t>(module->SetChannelInfo(0, AsPipe(&mono), *info)) == -1);
            CHECK(info->mFlags == flag);   // the known part is consumed, the unknown stays
        }
        info->mFlags = 0;   // nothing to do: ok
        CHECK(static_cast<int32_t>(module->SetChannelInfo(0, AsPipe(&mono), *info)) == 0);
    }

    // ---- Position and velocity (flags 1 and 0x800000): one command, read only for a 3D channel whose sample flags have bit
    // 0x20. The Mac clears the velocity flag before it tests it for the location, so a location always zeroes the velocity.
    {
        const TbSoundPosition position(1.0f, 2.0f, 3.0f);
        const TbSoundPosition velocity(7.0f, 8.0f, 9.0f);
        Zeroed<TbSoundSampleInfo> info;
        info->mPosition = &position;
        info->mVelocity = &velocity;
        info->mField0C = 0x20;
        info->mFlags = 1;
        CHECK(static_cast<int32_t>(module->SetChannelInfo(0, AsPipe(&mono), *info)) == 0);
        CHECK(info->mFlags == 0);
        CHECK(module->ChannelAt(0)->Location()[0] == 1.0f && module->ChannelAt(0)->Location()[1] == 2.0f &&
              module->ChannelAt(0)->Location()[2] == 3.0f);
        CHECK(module->ChannelAt(0)->Velocity()[0] == 0.0f && module->ChannelAt(0)->Velocity()[2] == 0.0f);
        info->mFlags = 0x800000;
        CHECK(static_cast<int32_t>(module->SetChannelInfo(0, AsPipe(&mono), *info)) == 0);
        CHECK(info->mFlags == 0);
        CHECK(module->ChannelAt(0)->Velocity()[0] == 7.0f && module->ChannelAt(0)->Velocity()[1] == 8.0f &&
              module->ChannelAt(0)->Velocity()[2] == 9.0f);
        CHECK(module->ChannelAt(0)->Location()[0] == 0.0f);   // a fresh command: the location is not carried over
        info->mFlags = 0x800000 | 1;
        CHECK(static_cast<int32_t>(module->SetChannelInfo(0, AsPipe(&mono), *info)) == 0);
        CHECK(info->mFlags == 0);
        CHECK(module->ChannelAt(0)->Location()[2] == 3.0f);
        CHECK(module->ChannelAt(0)->Velocity()[0] == 0.0f);   // the quirk
        info->mField0C = 0;   // sample flags without bit 0x20: both flags are consumed, nothing is read
        info->mFlags = 0x800000 | 1;
        CHECK(static_cast<int32_t>(module->SetChannelInfo(0, AsPipe(&mono), *info)) == 0);
        CHECK(info->mFlags == 0);
        CHECK(module->ChannelAt(0)->Location()[0] == 0.0f && module->ChannelAt(0)->Velocity()[0] == 0.0f);
        info->mField0C = 0x20;   // a 2D channel (channel 1): consumed, not read
        info->mFlags = 0x800000 | 1;
        CHECK(static_cast<int32_t>(module->SetChannelInfo(1, AsPipe(&stereo), *info)) == 0);
        CHECK(info->mFlags == 0);
        CHECK(module->ChannelAt(1)->Location()[0] == 0.0f && module->ChannelAt(1)->Velocity()[0] == 0.0f);
        info->mFlags = 0x800000 | 1 | 2 | 0x200 | 0x20 | 0x400000;   // everything the Mac handles at once
        info->mVolume = 127;
        CHECK(static_cast<int32_t>(module->SetChannelInfo(0, AsPipe(&mono), *info)) == 0);
        CHECK(info->mFlags == 0);
        CHECK(static_cast<int32_t>(module->SetChannelInfo(9, AsPipe(&mono), *info)) == -1);
    }

    // ---- StreamData pulls exactly one chunk at once and queues it; the stream is bound.
    CHECK(static_cast<int32_t>(StartSample(*module, 0, mono)) == 0);
    CHECK(static_cast<int32_t>(module->StreamData(AsPipe(&mono))) == 0);
    CHECK(mono.calls == 1);
    CHECK(mono.lastSize == 0x1200);
    CHECK(device.Pulled(0) == 1);
    CHECK(device.Queued(0) == 0x1200);
    CHECK(device.IsBound(0));
    CHECK(!device.IsEnded(0));
    SDL_Delay(120);   // the device is paused: SDL asks for nothing
    CHECK(mono.calls == 1);
    CHECK(device.Queued(0) == 0x1200);
    // Further chunks come only when SDL asks (its get callback, called by hand here): none for zero bytes, one chunk per
    // chunk's worth, always whole chunks.
    CHECK(device.Feed(0, 0) == 0);
    CHECK(mono.calls == 1);
    CHECK(device.Feed(0, 1) == 1);
    CHECK(mono.calls == 2);
    CHECK(device.Feed(0, 0x1200) == 1);
    CHECK(device.Feed(0, 0x1201) == 2);
    CHECK(mono.calls == 5);
    CHECK(device.Pulled(0) == 5);
    CHECK(device.Queued(0) == 5 * 0x1200);
    // The stereo chunk is 0x2400 bytes.
    CHECK(static_cast<int32_t>(StartSample(*module, 1, stereo)) == 0);
    CHECK(static_cast<int32_t>(module->StreamData(AsPipe(&stereo))) == 0);
    CHECK(stereo.lastSize == 0x2400);
    CHECK(device.Queued(1) == 0x2400);
    CHECK(device.Feed(1, 0x2400) == 1);
    CHECK(stereo.calls == 2);
    // A channel without a sample, or beyond the array, is refused.
    FakePipe idle;
    SetUpPipe(idle, 3, 22050, 16, false);
    CHECK(static_cast<int32_t>(module->StreamData(AsPipe(&idle))) == -1);   // channel 3 has no description
    CHECK(idle.calls == 0);

    // ---- When SDL asks (the device runs again), it is the stream that pulls, on SDL's audio thread, until the data the
    // stream queued has been played.
    {
        const uint32_t before = device.Pulled(0);
        device.SetDevicePaused(false);
        CHECK(WaitFor([&] { return device.Pulled(0) > before + 3; }, 5000));
        CHECK(static_cast<int32_t>(module->StopChannel(0)) == 0);   // unbinds: waits for a callback in progress
        const uint32_t afterStop = mono.calls;
        SDL_Delay(60);
        CHECK(mono.calls == afterStop);   // nothing is pulled after a stop
        device.SetDevicePaused(true);
    }
    CHECK(!device.IsBound(0));
    CHECK(device.Queued(0) == 0);
    CHECK(module->ChannelAt(0)->Pipe() == nullptr);   // forgotten
    CHECK(module->ChannelAt(0)->GetDescription());   // the description stays
    CHECK(static_cast<int32_t>(module->StopChannel(0)) == 0);   // stopping twice is fine
    // A stop before the stream was ever played, and Play with no pipe set (StreamData after a stop and no new sample).
    CHECK(static_cast<int32_t>(module->StreamData(AsPipe(&mono))) == 0);
    CHECK(!device.IsBound(0));
    CHECK(device.Queued(0) == 0);

    // ---- A pipe that answers false ends the stream: its chunk is not played, nothing more is pulled, what was queued plays.
    SetUpPipe(stereo, 1, 22050, 16, true, 1);
    stereo.overAt = 2;
    CHECK(static_cast<int32_t>(StartSample(*module, 1, stereo)) == 0);
    CHECK(static_cast<int32_t>(module->StreamData(AsPipe(&stereo))) == 0);
    CHECK(device.Pulled(1) == 1);
    CHECK(device.Feed(1, 1) == 1);   // call 1 delivers
    CHECK(device.Feed(1, 1) == 0);   // call 2 answers false
    CHECK(device.IsEnded(1));
    CHECK(stereo.calls == 3);
    CHECK(device.Pulled(1) == 2);
    CHECK(device.Queued(1) == 2 * 0x2400);   // the silent chunk was not queued
    CHECK(device.Feed(1, 0x10000) == 0);
    CHECK(stereo.calls == 3);   // an ended stream is not pulled again
    // A sample that is over at the first pull: nothing plays and the stream is not bound.
    SetUpPipe(stereo, 1, 22050, 16, true, 1);
    stereo.overAt = 0;
    CHECK(static_cast<int32_t>(StartSample(*module, 1, stereo)) == 0);
    CHECK(static_cast<int32_t>(module->StreamData(AsPipe(&stereo))) == 0);
    CHECK(stereo.calls == 1);
    CHECK(device.IsEnded(1));
    CHECK(!device.IsBound(1));
    CHECK(device.Queued(1) == 0);
    // A new sample on a channel whose last one ended plays again.
    SetUpPipe(stereo, 1, 22050, 16, true, 1);
    CHECK(static_cast<int32_t>(StartSample(*module, 1, stereo)) == 0);
    CHECK(!device.IsEnded(1));
    CHECK(static_cast<int32_t>(module->StreamData(AsPipe(&stereo))) == 0);
    CHECK(device.IsBound(1));

    // ---- Pause and restart: unbind and bind again, the queued data is kept.
    CHECK(static_cast<int32_t>(StartSample(*module, 2, narrow)) == 0);
    CHECK(static_cast<int32_t>(module->StreamData(AsPipe(&narrow))) == 0);
    CHECK(narrow.lastSize == 0x1200);
    CHECK(device.IsBound(2));
    CHECK(device.Queued(2) == 0x1200);
    CHECK(static_cast<int32_t>(module->PauseChannel(2)) == 0);
    CHECK(!device.IsBound(2));
    CHECK(device.IsPaused(2));
    CHECK(device.Queued(2) == 0x1200);
    CHECK(static_cast<int32_t>(module->RestartChannel(2)) == 0);
    CHECK(device.IsBound(2));
    CHECK(!device.IsPaused(2));
    CHECK(static_cast<int32_t>(module->PauseChannel(9)) == -1);
    CHECK(static_cast<int32_t>(module->RestartChannel(9)) == -1);
    // A channel paused when its sample starts is bound by the restart (the Mac queued the commands behind the pause).
    CHECK(static_cast<int32_t>(module->PauseChannel(0)) == 0);
    mono.calls = 0;
    CHECK(static_cast<int32_t>(StartSample(*module, 0, mono)) == 0);
    CHECK(static_cast<int32_t>(module->StreamData(AsPipe(&mono))) == 0);
    CHECK(device.Pulled(0) == 1);
    CHECK(!device.IsBound(0));
    CHECK(static_cast<int32_t>(module->RestartChannel(0)) == 0);
    CHECK(device.IsBound(0));
    // A restart of a channel that never played does not bind it.
    CHECK(!device.IsBound(3));
    CHECK(static_cast<int32_t>(module->RestartChannel(3)) == 0);
    CHECK(!device.IsBound(3));

    // ---- PauseAll, RestartAll and StopAll over mixed NULL and live channels.
    delete module->Channels()[3];
    module->Channels()[3] = nullptr;
    CHECK(device.IsBound(0) && device.IsBound(1) && device.IsBound(2));
    CHECK(static_cast<int32_t>(module->StreamData(nullptr)) == 0);
    CHECK(static_cast<int32_t>(module->StreamData(AsPipe(&odd))) == -1);   // channel 3 is NULL now
    CHECK(static_cast<int32_t>(module->StopChannel(3)) == -1);
    CHECK(static_cast<int32_t>(module->PauseChannel(3)) == -1);
    CHECK(static_cast<int32_t>(module->PauseAll()) == 0);
    CHECK(!device.IsBound(0) && !device.IsBound(1) && !device.IsBound(2));
    CHECK(device.IsPaused(0) && device.IsPaused(1) && device.IsPaused(2));
    CHECK(device.Queued(0) > 0 && device.Queued(2) > 0);
    CHECK(static_cast<int32_t>(module->RestartAll()) == 0);
    CHECK(device.IsBound(0) && device.IsBound(1) && device.IsBound(2));
    CHECK(!device.IsPaused(0) && !device.IsPaused(1) && !device.IsPaused(2));
    CHECK(static_cast<int32_t>(module->StopAll()) == 0);
    CHECK(!device.IsBound(0) && !device.IsBound(1) && !device.IsBound(2));
    CHECK(device.Queued(0) == 0 && device.Queued(1) == 0 && device.Queued(2) == 0);
    CHECK(module->ChannelAt(1)->Pipe() == nullptr);

    // ---- GetPosition: -1 for a NULL channel, else 0 and the position is not written. The listener setters need channel 0.
    {
        TbSoundPosition position(5.0f, 6.0f, 7.0f);
        CHECK(static_cast<int32_t>(module->GetPosition(1, position)) == 0);
        CHECK(position.mX == 5.0f && position.mY == 6.0f && position.mZ == 7.0f);
        CHECK(static_cast<int32_t>(module->GetPosition(3, position)) == -1);
        CHECK(static_cast<int32_t>(module->GetPosition(77, position)) == -1);
        CHECK(static_cast<int32_t>(module->SetSpeedOfSound(343.0f)) == 0);
        CHECK(static_cast<int32_t>(module->Set3DDSPFunctionality(1, 0xffff)) == 0);
        Zeroed<int> speakers;
        const TbSpeaker::Config& config = *reinterpret_cast<const TbSpeaker::Config*>(speakers.bytes);
        CHECK(static_cast<int32_t>(module->SetSpeakerConfig(config)) == 0);
        const uint32_t calls = module->Listener().calls;
        CHECK(static_cast<int32_t>(module->SetListenerPosition(TbSoundPosition(1.0f, 2.0f, 3.0f))) == 0);
        CHECK(static_cast<int32_t>(module->SetListenerDirection(TbSoundPosition(0.0f, 0.0f, 1.0f), TbSoundPosition(0.0f, 1.0f, 0.0f))) == 0);
        CHECK(static_cast<int32_t>(module->SetListenerVelocity(TbSoundPosition(4.0f, 5.0f, 6.0f))) == 0);
        CHECK(module->Listener().calls == calls + 3);
        CHECK(module->Listener().position[0] == 1.0f && module->Listener().position[2] == 3.0f);
        CHECK(module->Listener().front[2] == 1.0f && module->Listener().top[1] == 1.0f);
        CHECK(module->Listener().velocity[1] == 5.0f);
        // Channel 0 NULL: the three listener commands and the speaker config answer -1 and keep the listener.
        CSprocketsChannel* zero = module->Channels()[0];
        module->Channels()[0] = nullptr;
        CHECK(static_cast<int32_t>(module->SetListenerPosition(TbSoundPosition(9.0f, 9.0f, 9.0f))) == -1);
        CHECK(static_cast<int32_t>(module->SetListenerDirection(TbSoundPosition(9.0f, 9.0f, 9.0f), TbSoundPosition(9.0f, 9.0f, 9.0f))) == -1);
        CHECK(static_cast<int32_t>(module->SetListenerVelocity(TbSoundPosition(9.0f, 9.0f, 9.0f))) == -1);
        CHECK(static_cast<int32_t>(module->SetSpeakerConfig(config)) == -1);
        CHECK(module->Listener().position[0] == 1.0f && module->Listener().calls == calls + 3);
        module->Channels()[0] = zero;
    }

    // ---- Stop and start, over and over, with the device running: after a stop returns nothing is pulled from the pipe
    // any more, and nothing deadlocks or crashes with SDL's audio thread pulling in between.
    device.SetDevicePaused(false);
    for (int round = 0; round < 40; ++round) {
        SetUpPipe(mono, 0, 22050, 16, false);
        CHECK(static_cast<int32_t>(StartSample(*module, 0, mono)) == 0);
        CHECK(static_cast<int32_t>(module->StreamData(AsPipe(&mono))) == 0);
        if (round % 4 == 0) SDL_Delay(15);
        CHECK(static_cast<int32_t>(module->StopChannel(0)) == 0);
        const uint32_t calls = mono.calls;
        CHECK(calls >= 1);
        if (round % 8 == 0) {
            SDL_Delay(25);
            CHECK(mono.calls == calls);
        }
    }

    // ---- A game callback that commands the module from inside the pull (a completion handler that stops its own channel on
    // SDL's audio thread): the stream is not unbound from inside its own callback, nothing deadlocks, nothing is pulled after
    // the stop, and the channel plays again afterwards.
    {
        std::atomic<bool> hookRan{false};
        SetUpPipe(mono, 0, 22050, 16, false);
        mono.hookAt = 3;
        mono.hook = [&] {
            module->StopChannel(0);
            hookRan = true;
        };
        CHECK(static_cast<int32_t>(StartSample(*module, 0, mono)) == 0);
        CHECK(static_cast<int32_t>(module->StreamData(AsPipe(&mono))) == 0);
        CHECK(WaitFor([&] { return hookRan.load(); }, 5000));
        const uint32_t calls = mono.calls;
        SDL_Delay(60);
        CHECK(mono.calls == calls);
        CHECK(module->ChannelAt(0)->Pipe() == nullptr);
        CHECK(!device.IsBound(0));
        CHECK(static_cast<int32_t>(module->StopChannel(0)) == 0);   // settles the unbind the callback could not do
        SetUpPipe(mono, 0, 22050, 16, false);
        CHECK(static_cast<int32_t>(StartSample(*module, 0, mono)) == 0);
        CHECK(static_cast<int32_t>(module->StreamData(AsPipe(&mono))) == 0);
        CHECK(device.IsBound(0));
        CHECK(WaitFor([&] { return device.Pulled(0) > 2; }, 5000));   // SDL pulls again
        CHECK(static_cast<int32_t>(module->StopChannel(0)) == 0);
    }

    // ---- Close destroys everything: the streams, the device, the channels; later calls find no channel.
    module->Close();
    CHECK(module->Channels() == nullptr);
    CHECK(module->ChannelAt(0) == nullptr);
    CHECK(!module->Device()->IsOpen());
    CHECK(module->Device()->StreamCount() == 0);
    CHECK(tpw::SoundDevice::LiveStreams() == baselineStreams);
    CHECK(static_cast<int32_t>(module->StopChannel(0)) == -1);
    CHECK(static_cast<int32_t>(module->StopAll()) == 0);
    module->Close();   // a second Close is harmless
    CHECK(tpw::SoundDevice::LiveStreams() == baselineStreams);

    // ---- Init again after Close; this time with the capture on: every chunk pulled is appended to the file, raw, and
    // described in "<file>.log".
    // TPW_SOUND_PICTURES names a folder to keep the capture in (sound-capture.raw and its .log).
    const char* pictures = std::getenv("TPW_SOUND_PICTURES");
    const fs::path capture = pictures != nullptr && pictures[0] != '\0' ? fs::path(pictures) / "sound-capture.raw" : scratch / "capture.raw";
    std::error_code gone;
    fs::remove(capture, gone);
    fs::remove(capture.string() + ".log", gone);
    SetEnv("TPW_SOUND_CAPTURE", capture.string().c_str());
    systemInfo->mBuffersToUse = 2;
    CHECK(static_cast<int32_t>(module->Init(*systemInfo)) == 0);
    CHECK(module->mBuffers == 2);
    CHECK(module->ChannelAt(1) != nullptr && module->ChannelAt(2) == nullptr);
    CHECK(tpw::SoundDevice::LiveStreams() == baselineStreams + 2);
    CHECK(tpw::SoundDevice::CaptureActive());
    module->Device()->SetDevicePaused(true);
    {
        SetUpPipe(mono, 0, 22050, 16, false);
        SetUpPipe(narrow, 1, 11025, 8, false);
        CHECK(static_cast<int32_t>(StartSample(*module, 0, mono)) == 0);
        CHECK(static_cast<int32_t>(StartSample(*module, 1, narrow)) == 0);
        CHECK(static_cast<int32_t>(module->StreamData(AsPipe(&mono))) == 0);
        CHECK(static_cast<int32_t>(module->StreamData(AsPipe(&narrow))) == 0);
        CHECK(module->Device()->Feed(0, 0x1200 + 1) == 2);
        CHECK(module->StopAll().mValue == 0);
    }
    module->Close();
    CHECK(!tpw::SoundDevice::CaptureActive());
    UnsetEnv("TPW_SOUND_CAPTURE");
    {
        std::ifstream file(capture, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        // Channel 0: three 16-bit chunks (StreamData and a two-chunk Feed); channel 1: one 8-bit chunk, in the order pulled.
        CHECK(bytes.size() == 3 * 0x1200 + 0x1200);
        int16_t sample;
        std::memcpy(&sample, bytes.data(), 2);
        CHECK(sample == PatternSample(0, 0));   // chunk 0, sample 0: 0x0100
        std::memcpy(&sample, bytes.data() + 2 * 5, 2);
        CHECK(sample == PatternSample(0, 5));
        // The 8-bit chunk came second (StreamData of channel 1 ran before the Feed).
        CHECK(static_cast<uint8_t>(bytes[0x1200]) == 0x10);   // call 0, byte 0: (0 + 1) << 4
        CHECK(static_cast<uint8_t>(bytes[0x1200 + 7]) == 0x17);
        std::memcpy(&sample, bytes.data() + 0x1200 + 0x1200, 2);
        CHECK(sample == PatternSample(1, 0));   // the second chunk of channel 0
        std::memcpy(&sample, bytes.data() + 0x1200 + 0x1200 + 2 * 2303, 2);
        CHECK(sample == PatternSample(1, 2303));
        std::memcpy(&sample, bytes.data() + 0x1200 + 2 * 0x1200, 2);
        CHECK(sample == PatternSample(2, 0));
        std::ifstream log(capture.string() + ".log");
        std::string line;
        std::vector<std::string> lines;
        while (std::getline(log, line)) lines.push_back(line);
        CHECK(lines.size() == 4);
        CHECK(lines[0] == "stream 0 rate 22050 bits 16 channels 1 bytes 4608");
        CHECK(lines[1] == "stream 1 rate 11025 bits 8 channels 1 bytes 4608");
        CHECK(lines[3] == lines[0]);
    }

    // ---- Deleting the module: the base frees the speaker block, an open module is closed first.
    SetUpPipe(mono, 0, 22050, 16, false);
    systemInfo->mBuffersToUse = 3;
    CHECK(static_cast<int32_t>(module->Init(*systemInfo)) == 0);
    CHECK(tpw::SoundDevice::LiveStreams() == baselineStreams + 3);
    delete module;   // never closed
    CHECK(tpw::SoundDevice::LiveStreams() == baselineStreams);
    CHECK(TbSpeaker::AutoDelete::_mpSpeaker == nullptr);

    // ---- And a module created the way CModuleFactory::GenerateModule does it: new, Init, Close, delete.
    {
        TbSoundSystemModule* made = new CSprocketsModule();
        CHECK(static_cast<int32_t>(made->Init(*systemInfo)) == 0);
        CHECK(tpw::SoundDevice::LiveStreams() == baselineStreams + 3);
        made->Close();
        delete made;
        CHECK(tpw::SoundDevice::LiveStreams() == baselineStreams);
    }
}

// The launcher's bar after Play (handoff.h): bytes read against the total, no number without a total, at most 99%
// until the game's window is shown, full once it is.
static void TestStartupFraction() {
    CHECK(tpw::StartupFraction(0, 0, false) == -1.0f);
    CHECK(tpw::StartupFraction(500, 0, false) == -1.0f);
    CHECK(tpw::StartupFraction(0, 100, false) == 0.0f);
    CHECK(std::fabs(tpw::StartupFraction(42, 100, false) - 0.42f) < 1e-6f);
    CHECK(std::fabs(tpw::StartupFraction(100, 100, false) - 0.99f) < 1e-6f);
    CHECK(std::fabs(tpw::StartupFraction(150, 100, false) - 0.99f) < 1e-6f);
    CHECK(tpw::StartupFraction(42, 100, true) == 1.0f);
    CHECK(tpw::StartupFraction(0, 0, true) == 1.0f);
    // Totals of the real size (a preload of about 17 MB and a start of about 0.5 MB).
    CHECK(std::fabs(tpw::StartupFraction(8924140, 17848280, false) - 0.5f) < 1e-6f);
}

int main() {
    // One folder per process, emptied first: a run that failed leaves its folder behind.
    const fs::path scratch = fs::temp_directory_path() / ("tpw_platform_tests_" + std::to_string(getpid()));
    std::error_code stale;
    fs::remove_all(scratch, stale);
    fs::create_directories(scratch);
    TestPaths(scratch);
    TestPacks(scratch / "packs");
    TestEdition(scratch / "edition");
    TestSwearStreams();
    TestMslRuntime();
    TestCvtGlue();
    TestFindFirstFile();
    TestCompareFileTime();
    TestFileHandles();
    TestMacFileCalls();
    TestDialogs();
    TestMacDozeCursor();
    TestKeys();
    TestKeyboard();
    TestScriptedClick();
    TestStrings();
    TestArena();
    TestGdi();
    TestResourceFork();
    TestGdiForkFonts();
    TestFitArithmetic();
    TestFitLetterbox();
    TestFitMouse();
    TestFitFullScreen();
#ifdef TPW_PORT_FIX
    TestUPixels24RGB();
    TestWideScreen();
    TestWidePick();
    TestFontScale();
#endif
    TestSound(scratch);
    TestStartupFraction();
    std::error_code ec;
    fs::remove_all(scratch, ec);
    std::printf("platform tests: %d checks passed\n", gChecks);
    return 0;
}
