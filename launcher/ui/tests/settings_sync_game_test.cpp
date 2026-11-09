// settings_sync_game_test.cpp: the launcher's Settings box against the game's own code, on real files. CPlayerSettings::Init,
// LoadGlobal, SaveGlobal, SerialiseGlobal and SerialiseLocal are the real ones, cut from src/game/TPSettings.cpp, and the sizes
// are PortGameSizeRules on the real lists of include/UI_Base.h and numbers of include/TPSettings.h; the file calls and the
// sound table are stand-ins for the game's. The game must read what the launcher wrote, the launcher what the game wrote, and
// a first file of the launcher's must have the bytes the game's own would. Run by ctest ("settings_sync"); made only with the
// port's fixes, the original has none of this. Exits nonzero on a failure.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "Debug.h"
#include "LbFile.h"
#include "SaveLoad.h"
#include "Serialise.h"
#include "SoundBalance.h"
#include "TPSettings.h"
#include "TbDynamicStringTemplate.h"
#include "game_size_rules.h"
#include "ui/game_settings.h"

namespace fs = std::filesystem;
using namespace tpw::ui;

// ------------------------------------------------------------------ the rest of the game, as far as these functions reach
CSoundBalance gSoundBalance;
CSaveLoad gSaveLoad;
CPlayerSettings gPlayerSettings;

void CBalanceTable::Init() {}
CSoundBalance::~CSoundBalance() {}
const BalanceDescription* CSoundBalance::GetBalanceDescription(int32_t) { return nullptr; }
BalanceValue* CSoundBalance::GetBalanceValuesStart() { return mValues; }
void CSaveLoad::WriteSerialiseLog(int32_t, const char*, LogfileState) {}
void CPlayerSettings::UpdateVolume() {}
void CTRACE(uint32_t, const char*, ...) {}
void GAME_ASSERTMSG(int32_t, const char*, ...) {}

// The port's Gestalt (platform/sams_core.cpp): the top machine class.
int16_t Gestalt(uint32_t selector, int32_t* answer) {
    *answer = selector == 0x72616d20 ? 0x40000000 : 1000000000;
    return 0;
}

// The files' own instances (src/game/TPResource.cpp, src/game/advisor/Advisor.cpp): the shared bodies of Serialise.h, unchanged.
void SwapForFile(CPlayerSettings::Volume& volume) { volume.mVolume = serialise::SwapWord(volume.mVolume); }
void UnswapForFile(CPlayerSettings::Volume& volume) { volume.mVolume = serialise::UnswapWord(volume.mVolume); }
#define VAR_INSTANCES(Type)                                                                                             \
    template<> int32_t LOADVAR<Type>(void* file, Type* value, const char* name, int32_t noLog) {                        \
        return serialise::LoadVar(file, value, name, noLog);                                                            \
    }                                                                                                                   \
    template<> int32_t SAVEVAR<Type>(void* file, Type* value, const char* name, int32_t noLog) {                        \
        return serialise::SaveVar(file, value, name, noLog);                                                            \
    }
VAR_INSTANCES(int32_t)
VAR_INSTANCES(bool)
VAR_INSTANCES(CPlayerSettings::eRENDERER)
VAR_INSTANCES(CPlayerSettings::eRESOLUTION)
VAR_INSTANCES(CPlayerSettings::eDETAIL)
VAR_INSTANCES(CPlayerSettings::eVIDEOCARD)
template<> int32_t LOADVARSTRUCT<CPlayerSettings::Volume>(void* file, CPlayerSettings::Volume* value, const char* name, int32_t noLog) {
    return serialise::LoadStruct(file, value, name, noLog);
}
template<> int32_t SAVEVARSTRUCT<CPlayerSettings::Volume>(void* file, CPlayerSettings::Volume* value, const char* name, int32_t noLog) {
    return serialise::SaveStruct(file, value, name, noLog);
}

// The file calls on plain files (kLbFileRead must find the file, kLbFileCreate starts it empty).
TbError LbFile_Open(void*& file, const char* name, uint32_t mode) {
    file = std::fopen(name, mode == 0x80000001 ? "rb" : "wb");
    return TbError(file != nullptr ? 0 : -1);
}
TbError LbFile_Close(void* file) {
    std::fclose(static_cast<std::FILE*>(file));
    return TbError(0);
}
TbError LbFile_Read(void* file, void* buffer, uint32_t length, uint32_t* transferred) {
    *transferred = static_cast<uint32_t>(std::fread(buffer, 1, length, static_cast<std::FILE*>(file)));
    return TbError(*transferred == length ? 0 : -1);
}
TbError LbFile_Write(void* file, const void* buffer, uint32_t length, uint32_t* transferred) {
    *transferred = static_cast<uint32_t>(std::fwrite(buffer, 1, length, static_cast<std::FILE*>(file)));
    return TbError(*transferred == length ? 0 : -1);
}

template<> TbDynamicStringTemplate<char>::TbDynamicStringTemplate() : mGrowBy(0) {
    mCapacity = 0;
    mData = nullptr;
    mLength = 0;
}
template<> TbDynamicStringTemplate<char>::TbDynamicStringTemplate(const char* text) : mGrowBy(0) {
    mCapacity = 0;
    mData = const_cast<char*>(text);
    mLength = static_cast<int32_t>(std::strlen(text));
}
template<> TbDynamicStringTemplate<char>::~TbDynamicStringTemplate() {}
// The class's table of virtual functions names these; nothing here calls them.
template<> void TbDynamicStringTemplate<char>::AllocBuffer(uint32_t) {}
template<> void TbDynamicStringTemplate<char>::Format(const char*, ...) {}
template<> void TbDynamicStringTemplate<char>::ResizeTo(uint32_t) {}
template<> TbDynamicStringTemplate<char>& TbDynamicStringTemplate<char>::operator=(const char*) { return *this; }
template<> TbDynamicStringTemplate<char>& TbDynamicStringTemplate<char>::operator=(const TbStringBase<char>&) { return *this; }
template<> TbDynamicStringTemplate<char>& TbDynamicStringTemplate<char>::operator=(const TbDynamicStringTemplate<char>&) { return *this; }
template<> TbDynamicStringTemplate<char>& TbDynamicStringTemplate<char>::operator=(const char&) { return *this; }
template<> TbDynamicStringTemplate<char>& TbDynamicStringTemplate<char>::operator+=(const char*) { return *this; }
template<> TbDynamicStringTemplate<char>& TbDynamicStringTemplate<char>::operator+=(const TbStringBase<char>&) { return *this; }
template<> TbDynamicStringTemplate<char>& TbDynamicStringTemplate<char>::operator+=(const char&) { return *this; }

namespace {

int gFailures = 0;
int gChecks = 0;

void Check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        std::printf("FAILED: %s\n", what.c_str());
        ++gFailures;
    }
}

void CheckEq(long got, long want, const std::string& what) {
    if (got != want) std::printf("  got %ld, wanted %ld\n", got, want);
    Check(got == want, what);
}

std::vector<uint8_t> ReadAll(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteAll(const fs::path& file, const std::vector<uint8_t>& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// What sound.sam gives gSoundBalance, as the same text the launcher reads.
void SoundTable(const fs::path& soundSam) {
    gSoundBalance.mValues[9].mInteger = 50;     // DEFAULTQUALITY
    gSoundBalance.mValues[11].mInteger = 1;     // SOUNDON
    gSoundBalance.mValues[12].mInteger = 75;    // SFX
    gSoundBalance.mValues[13].mInteger = 60;    // MUSIC
    gSoundBalance.mValues[14].mInteger = 75;    // SPEECH
    gSoundBalance.mValues[15].mInteger = 100;   // MOVIE
    std::ofstream out(soundSam);
    out << "SoundInfo.DEFAULTQUALITY\t50\nSoundInfo.SOUNDON\t\t1\nDefaultVolume.SFX 75\nDefaultVolume.MUSIC 60\n"
           "DefaultVolume.SPEECH 75\nDefaultVolume.MOVIE 100\n";
}

bool GameLoad(CPlayerSettings& game, const fs::path& file) {
    const std::string name = file.u8string();
    TbDynamicStringTemplate<char> path(name.c_str());
    return game.LoadGlobal(path) != 0;
}

bool GameSave(CPlayerSettings& game, const fs::path& file) {
    const std::string name = file.u8string();
    TbDynamicStringTemplate<char> path(name.c_str());
    return game.SaveGlobal(path) != 0;
}

// The 39 bytes as the game's SerialiseLocal writes them, in a new player's gms.dat around them.
std::vector<uint8_t> GamePlayerFile(CPlayerSettings& game, const fs::path& file) {
    fs::create_directories(file.parent_path());
    void* out = nullptr;
    LbFile_Open(out, file.u8string().c_str(), 0xc0000010);
    const uint8_t head[25] = {0x0c, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0};
    uint32_t moved = 0;
    LbFile_Write(out, head, sizeof head, &moved);
    game.SerialiseLocal(out, 1, 0xc);
    const uint8_t tail[4] = {0, 0, 0, 0};
    LbFile_Write(out, tail, sizeof tail, &moved);
    LbFile_Close(out);
    return ReadAll(file);
}

void GameReadsPlayer(CPlayerSettings& game, const fs::path& file) {
    void* in = nullptr;
    LbFile_Open(in, file.u8string().c_str(), 0x80000001);
    uint8_t head[25];
    uint32_t moved = 0;
    LbFile_Read(in, head, sizeof head, &moved);
    game.SerialiseLocal(in, 0, 0xc);
    LbFile_Close(in);
}

// Bytes 21 to 23 of Config.tcf are padding: the three after mMovie.mOn in a CPlayerSettings::Volume. What the game writes there is
// not fixed. serialise::SaveStruct saves a temporary copy of the struct, a copy need not carry padding, and so they can be whatever
// was on the stack: the same settings saved twice can differ there, on any compiler that scalarises the copy. Nothing reads them
// (CheckPaddingHarmless), so files are the same when every other byte is.
constexpr size_t kConfigSize = 32;
constexpr size_t kConfigMovieOn = 20;
constexpr size_t kConfigMovieVolume = 24;
static_assert(sizeof(CPlayerSettings::Volume) == 8 && offsetof(CPlayerSettings::Volume, mVolume) == kConfigMovieVolume - kConfigMovieOn,
              "the padding is the bytes between a Volume's two fields");

bool IsConfigPadding(size_t at) { return at > kConfigMovieOn && at < kConfigMovieVolume; }

bool SameConfig(const std::vector<uint8_t>& got, const std::vector<uint8_t>& want) {
    if (got.size() != want.size()) return false;
    for (size_t at = 0; at < got.size(); ++at)
        if (got[at] != want[at] && !IsConfigPadding(at)) return false;
    return true;
}

void CheckSameConfig(const std::vector<uint8_t>& got, const std::vector<uint8_t>& want, const std::string& what) {
    if (got.size() != want.size()) std::printf("  %zu bytes, wanted %zu\n", got.size(), want.size());
    for (size_t at = 0; at < got.size() && at < want.size(); ++at)
        if (got[at] != want[at] && !IsConfigPadding(at))
            std::printf("  byte %zu is 0x%02x, wanted 0x%02x\n", at, got[at], want[at]);
    Check(SameConfig(got, want), what);
}

// The compare forgives the padding and nothing else.
void CheckConfigCompare() {
    std::vector<uint8_t> file(kConfigSize);
    for (size_t at = 0; at < kConfigSize; ++at) file[at] = static_cast<uint8_t>(at * 3 + 1);
    Check(SameConfig(file, file), "compare: a file is itself");
    for (size_t at = 0; at < kConfigSize; ++at) {
        std::vector<uint8_t> changed = file;
        changed[at] ^= 0x80;
        const bool padding = at == 21 || at == 22 || at == 23;
        Check(IsConfigPadding(at) == padding, "compare: only bytes 21 to 23 are padding");
        Check(SameConfig(changed, file) == padding, "compare: a changed byte counts unless it is padding");
    }
    std::vector<uint8_t> shorter = file;
    shorter.pop_back();
    Check(!SameConfig(shorter, file), "compare: a shorter file is another file");
    std::vector<uint8_t> longer = file;
    longer.push_back(0);
    Check(!SameConfig(longer, file), "compare: a longer file is another file");
    Check(!SameConfig({}, file), "compare: an empty file is another file");
}

// The padding is why the compare can forgive it: with anything in those bytes the game and the launcher read the same settings,
// the launcher changes only what it is told to, and opening the box changes nothing.
void CheckPaddingHarmless(const fs::path& root) {
    const fs::path sam = root / "sound.sam";
    const fs::path user = root / "padding";
    const fs::path config = user / "save" / "Config.tcf";
    SoundTable(sam);
    fs::create_directories(config.parent_path());
    CPlayerSettings source;
    source.mCurrent.mResolution = CPlayerSettings::eRESOLUTION(PortResolutionExact(1024, 768));
    source.mCurrent.mDetail = CPlayerSettings::eDETAIL(1);
    source.mCurrent.mSoundQuality = 12;
    source.mCurrent.mMovie = {false, 9};
    Check(GameSave(source, config), "padding: the game's save");
    const std::vector<uint8_t> saved = ReadAll(config);
    CheckEq(static_cast<long>(saved.size()), static_cast<long>(kConfigSize), "padding: 32 bytes");

    for (const bool movieOn : {false, true}) {
        std::vector<uint8_t> marked = saved;
        marked[kConfigMovieOn] = movieOn ? 1 : 0;
        marked[21] = 0xff;
        marked[22] = 0x5a;
        marked[23] = 0xa5;
        WriteAll(config, marked);

        CPlayerSettings game;
        Check(GameLoad(game, config), "padding: the game's load takes the file");
        CheckEq(game.mCurrent.mResolution, source.mCurrent.mResolution, "padding: the game's size");
        CheckEq(game.mCurrent.mDetail, 1, "padding: the game's graphics quality");
        CheckEq(game.mCurrent.mSoundQuality, 12, "padding: the game's audio quality");
        CheckEq(game.mCurrent.mMovie.mVolume, 9, "padding: the game's movie volume");
        Check(game.mCurrent.mMovie.mOn == movieOn, "padding: the game's movie switch is byte 20 alone");

        const GameSettingsView view = ReadGameSettings(user, sam, "");
        Check(view.configState == GameFileState::Ok, "padding: the launcher reads the file");
        CheckEq(view.config.resolution, source.mCurrent.mResolution, "padding: the launcher's size");
        CheckEq(view.config.detail, 1, "padding: the launcher's graphics quality");
        CheckEq(view.config.soundQuality, 12, "padding: the launcher's audio quality");
        CheckEq(view.config.movie.volume, 9, "padding: the launcher's movie volume");
        Check(view.config.movie.on == movieOn, "padding: the launcher's movie switch is byte 20 alone");
        Check(ReadAll(config) == marked, "padding: opening the box changes nothing");

        std::string err;
        Check(WriteGameOption(user, sam, "", GameOption::AudioQuality, 40, err), "padding: the launcher's write");
        std::vector<uint8_t> expected = marked;
        expected[28] = 40;
        Check(ReadAll(config) == expected, "padding: the launcher's write changes the setting's bytes alone");
    }

    // A player's file has four Volumes, so the same three bytes after each one's switch.
    const fs::path playerFile = user / "save" / "users" / "1Ann" / "gms.dat";
    CPlayerSettings player;
    player.mCurrent.mSfx = {true, 21};
    player.mCurrent.mMusic = {false, 22};
    player.mCurrent.mSpeech = {true, 23};
    player.mCurrent.mMovie = {false, 24};
    std::vector<uint8_t> marked = GamePlayerFile(player, playerFile);
    const size_t block = 25;
    for (size_t volume = 0; volume < 4; ++volume)
        for (size_t pad = 1; pad < 4; ++pad) marked[block + volume * 8 + pad] = static_cast<uint8_t>(0xc0 + volume * 4 + pad);
    WriteAll(playerFile, marked);
    CPlayerSettings login;
    GameReadsPlayer(login, playerFile);
    Check(login.mCurrent.mSfx.mOn && login.mCurrent.mSfx.mVolume == 21, "padding: the game's login has sound effects");
    Check(!login.mCurrent.mMusic.mOn && login.mCurrent.mMusic.mVolume == 22, "padding: the game's login has music");
    Check(login.mCurrent.mSpeech.mOn && login.mCurrent.mSpeech.mVolume == 23, "padding: the game's login has speech");
    Check(!login.mCurrent.mMovie.mOn && login.mCurrent.mMovie.mVolume == 24, "padding: the game's login has the movie");
    const GameSettingsView view = ReadGameSettings(user, sam, "");
    Check(view.playerState == GameFileState::Ok, "padding: the launcher reads the player");
    Check(view.options.sfx.on && view.options.sfx.volume == 21, "padding: the launcher's sound effects");
    Check(!view.options.music.on && view.options.music.volume == 22, "padding: the launcher's music");
    Check(view.options.speech.on && view.options.speech.volume == 23, "padding: the launcher's speech");
    Check(!view.options.movie.on && view.options.movie.volume == 24, "padding: the launcher's movie");
    Check(ReadAll(playerFile) == marked, "padding: opening the box leaves the player's file");
}

void CheckFirstFile(const fs::path& root) {
    const fs::path sam = root / "sound.sam";
    SoundTable(sam);
    CPlayerSettings game;
    const fs::path own = root / "own" / "save" / "Config.tcf";
    fs::create_directories(own.parent_path());
    Check(!GameLoad(game, own), "first start: the game finds no file");
    Check(GameSave(game, own), "first start: the game writes one");
    CheckEq(static_cast<long>(ReadAll(own).size()), 32, "first start: 32 bytes");

    const fs::path user = root / "first";
    const GameSettingsView view = ReadGameSettings(user, sam, "");
    CheckEq(view.config.renderer, game.mCurrent.mRenderer, "defaults: the game's renderer");
    CheckEq(view.config.resolution, game.mCurrent.mResolution, "defaults: the game's resolution");
    CheckEq(view.config.detail, game.mCurrent.mDetail, "defaults: the game's graphics quality");
    CheckEq(view.config.videoCard, game.mCurrent.mVideoCard, "defaults: the game's video card");
    CheckEq(view.config.soundQuality, game.mCurrent.mSoundQuality, "defaults: the game's audio quality");
    CheckEq(view.config.movie.volume, game.mCurrent.mMovie.mVolume, "defaults: the game's movie volume");
    Check(view.config.movie.on == game.mCurrent.mMovie.mOn, "defaults: the game's movie switch");
    CheckEq(view.options.sfx.volume, game.mCurrent.mSfx.mVolume, "defaults: the game's sound effects");
    CheckEq(view.options.music.volume, game.mCurrent.mMusic.mVolume, "defaults: the game's music");
    CheckEq(view.options.speech.volume, game.mCurrent.mSpeech.mVolume, "defaults: the game's speech");
    Check(view.options.advisor == game.mCurrent.mAdvisorOn, "defaults: the game's advisor");
    Check(view.options.tutorial == game.mCurrent.mTutorialOn, "defaults: the game's tutorial");
    Check(view.options.tooltips == game.mCurrent.mTooltipsOn, "defaults: the game's popup help");
    Check(view.options.confirmDelete == game.mCurrent.mConfirmDeleteOn, "defaults: the game's confirmations");
    Check(view.options.rmbScroll == game.mCurrent.mRMBScrollOn, "defaults: the game's scroll");
    Check(view.options.rmbCancel == game.mCurrent.mRMBCancelOn, "defaults: the game's RMB cancel");
    Check(view.options.isometric == game.mCurrent.mIsometricOn, "defaults: the game's rotation");

    // The launcher's first change: the game's own file, but for that one setting.
    std::string err;
    Check(WriteGameOption(user, sam, "", GameOption::AudioQuality, game.mCurrent.mSoundQuality, err), "first write: made");
    CheckSameConfig(ReadAll(user / "save" / "Config.tcf"), ReadAll(own), "first write: the very bytes of the game's own first file, but for the padding");
}

void CheckBothWays(const fs::path& root) {
    const fs::path sam = root / "sound.sam";
    const fs::path user = root / "both";
    const fs::path config = user / "save" / "Config.tcf";
    SoundTable(sam);
    CPlayerSettings game;
    std::string err;
    int screenShape = 1;
    const GameSizeRules rules = PortGameSizeRules(10, [&] { return screenShape; }, [] { return GameSize{2880, 1800}; });

    // The launcher writes every setting of Config.tcf, and the game's load has it.
    const int size = rules.setting({1920, 1080});
    Check(WriteGameOption(user, sam, "", GameOption::Resolution, size, err), "launcher to game: a size");
    Check(WriteGameOption(user, sam, "", GameOption::GraphicsQuality, 1, err), "launcher to game: graphics quality");
    Check(WriteGameOption(user, sam, "", GameOption::AudioQuality, 33, err), "launcher to game: audio quality");
    Check(WriteGameOption(user, sam, "", GameOption::MovieVolume, 44, err), "launcher to game: movie volume");
    Check(WriteGameOption(user, sam, "", GameOption::MovieOn, 0, err), "launcher to game: movie off");
    Check(GameLoad(game, config), "launcher to game: the game's load takes the file");
    CheckEq(game.mCurrent.mResolution, size, "launcher to game: the game has the size");
    int32_t gameWide = 0, gameHigh = 0;
    Check(PortResolutionSize(game.mCurrent.mResolution, gameWide, gameHigh), "launcher to game: the game takes it as a size");
    CheckEq(gameWide, 1920, "launcher to game: the game's width");
    CheckEq(gameHigh, 1080, "launcher to game: the game's height");
    CheckEq(game.mCurrent.mDetail, 1, "launcher to game: graphics quality");
    CheckEq(game.mCurrent.mSoundQuality, 33, "launcher to game: audio quality");
    CheckEq(game.mCurrent.mMovie.mVolume, 44, "launcher to game: movie volume");
    Check(!game.mCurrent.mMovie.mOn, "launcher to game: movie off");
    // Written back by the game unchanged, the file is the same.
    const std::vector<uint8_t> written = ReadAll(config);
    Check(GameSave(game, config), "launcher to game: the game saves");
    CheckSameConfig(ReadAll(config), written, "launcher to game: the game writes the launcher's bytes back, but for the padding");

    // Software rendering: the launcher ends where the panel's button does. The game's load then needs no other rule.
    Check(WriteGameOption(user, sam, "", GameOption::GraphicsQuality, 2, err), "rendering: High first");
    Check(WriteGameOption(user, sam, "", GameOption::Rendering, 0, err), "rendering: Software");
    Check(GameLoad(game, config), "rendering: loads");
    CheckEq(game.mCurrent.mRenderer, 0, "rendering: the game has Software");
    CheckEq(game.mCurrent.mDetail, 1, "rendering: the game has Medium, the top of Software's slider");
    Check(WriteGameOption(user, sam, "", GameOption::Rendering, 1, err), "rendering: 3D card");

    // The game changes every one, through its own save, and the launcher reads them.
    Check(GameLoad(game, config), "game to launcher: loads");
    game.SetResolution(CPlayerSettings::eRESOLUTION(PortResolutionExact(1024, 768)));
    game.mCurrent.mDetail = CPlayerSettings::eDETAIL(0);
    game.mCurrent.mSoundQuality = 12;
    game.mCurrent.mMovie.mVolume = 9;
    game.mCurrent.mMovie.mOn = true;
    Check(GameSave(game, config), "game to launcher: saves");
    GameSettingsView view = ReadGameSettings(user, sam, "");
    Check(view.configState == GameFileState::Ok, "game to launcher: reads");
    CheckEq(view.config.detail, 0, "game to launcher: graphics quality");
    CheckEq(view.config.soundQuality, 12, "game to launcher: audio quality");
    CheckEq(view.config.movie.volume, 9, "game to launcher: movie volume");
    Check(view.config.movie.on, "game to launcher: movie on");
    int shape = -1;
    const GameSize chosen = rules.chosen(view.config.resolution, shape);
    CheckEq(shape, 0, "game to launcher: the slider's shape");
    CheckEq(chosen.width, 1024, "game to launcher: the size shown");
    CheckEq(chosen.height, 768, "game to launcher: the size shown");
    const std::vector<GameSize> stops = rules.stops(shape, chosen);
    UISizeStop gameStops[kUISizeStopLimit];
    const int32_t gameCount = UI_SizeStops(shape, chosen.width, chosen.height, 2880, 1800, gameStops);
    CheckEq(static_cast<long>(stops.size()), gameCount, "game to launcher: the stops are the panel's");
    for (int32_t i = 0; i < gameCount && i < static_cast<int32_t>(stops.size()); ++i) {
        CheckEq(stops[static_cast<size_t>(i)].width, gameStops[i].mWidth, "game to launcher: the panel's stop");
        CheckEq(stops[static_cast<size_t>(i)].height, gameStops[i].mHeight, "game to launcher: the panel's stop");
    }
    const GameSize atStop = stops[static_cast<size_t>(rules.closest(stops, chosen))];
    Check(atStop == chosen, "game to launcher: the slider is on it");
    CheckEq(rules.setting(atStop), view.config.resolution, "game to launcher: the same number when it is written back");

    // Every stop of every shape: the number saved for it is that stop at that shape when it is read, on both sides.
    for (int s = 0; s < kUISizeShapeCount; ++s) {
        for (const GameSize& size : rules.stops(s, GameSize{})) {
            int back = -1;
            int32_t wide = 0, high = 0;
            const int number = rules.setting(size);
            Check(rules.chosen(number, back) == size, "stops: the launcher reads its size back");
            CheckEq(back, s, "stops: at its shape");
            Check(PortResolutionSize(number, wide, high) && wide == size.width && high == size.height, "stops: the game reads the size");
        }
    }
    // A size off the lists (a screen's, or an older choice) is a stop at its own shape while it is the one opened, and
    // only there; a shape button keeps the height, so its way back returns to the size.
    const GameSize offList{2730, 1536};
    std::vector<GameSize> around = rules.stops(2, offList);
    Check(around[static_cast<size_t>(rules.closest(around, offList))] == offList, "off the lists: a stop at its shape");
    const std::vector<GameSize> other = rules.stops(0, offList);
    Check(std::find(other.begin(), other.end(), offList) == other.end(), "off the lists: not at another");
    const GameSize wider{1920, 1080};
    const std::vector<GameSize> squarer = rules.stops(1, wider);
    const GameSize there = squarer[static_cast<size_t>(rules.closest(squarer, wider))];
    around = rules.stops(2, wider);
    Check(around[static_cast<size_t>(rules.closest(around, there))] == wider, "a shape and back: the same size");

    // A player's options, through the game's SerialiseLocal in both directions.
    const fs::path playerFile = user / "save" / "users" / "1Ann" / "gms.dat";
    game.mCurrent.mSfx = {true, 21};
    game.mCurrent.mMusic = {false, 22};
    game.mCurrent.mSpeech = {true, 23};
    game.mCurrent.mMovie = {false, 24};
    game.mCurrent.mAdvisorOn = false;
    game.mCurrent.mTutorialOn = true;
    game.mCurrent.mTooltipsOn = false;
    game.mCurrent.mConfirmDeleteOn = true;
    game.mCurrent.mRMBScrollOn = true;
    game.mCurrent.mRMBCancelOn = false;
    game.mCurrent.mIsometricOn = false;
    const std::vector<uint8_t> player = GamePlayerFile(game, playerFile);
    CheckEq(static_cast<long>(player.size()), 68, "player: the game's block is 39 bytes");
    view = ReadGameSettings(user, sam, "");
    Check(view.playerState == GameFileState::Ok && view.player == 0, "game to launcher: the player reads");
    CheckEq(view.options.sfx.volume, 21, "game to launcher: sound effects");
    Check(!view.options.music.on, "game to launcher: music off");
    CheckEq(view.options.music.volume, 22, "game to launcher: music");
    CheckEq(view.options.speech.volume, 23, "game to launcher: speech");
    Check(!view.options.movie.on, "game to launcher: movie off");
    CheckEq(view.options.movie.volume, 24, "game to launcher: movie");
    Check(!view.options.advisor && view.options.tutorial && !view.options.tooltips, "game to launcher: Advisor, Tutorial, Popup help");
    Check(view.options.confirmDelete && view.options.rmbScroll && !view.options.rmbCancel && !view.options.isometric,
          "game to launcher: Confirmations, Scroll, RMB cancel, Rotation");

    Check(WriteGameOption(user, sam, "Ann", GameOption::SfxVolume, 31, err), "launcher to game: sound effects");
    Check(WriteGameOption(user, sam, "Ann", GameOption::MusicOn, 1, err), "launcher to game: music on");
    Check(WriteGameOption(user, sam, "Ann", GameOption::Advisor, 1, err), "launcher to game: Advisor");
    Check(WriteGameOption(user, sam, "Ann", GameOption::Scroll, 0, err), "launcher to game: Scroll");
    Check(WriteGameOption(user, sam, "Ann", GameOption::Rotation, 1, err), "launcher to game: Rotation");
    Check(WriteGameOption(user, sam, "Ann", GameOption::MovieVolume, 34, err), "launcher to game: movie volume");
    CPlayerSettings again;
    Check(GameLoad(again, config), "launcher to game: Config.tcf");
    CheckEq(again.mCurrent.mMovie.mVolume, 34, "launcher to game: Config.tcf's movie volume");
    GameReadsPlayer(again, playerFile);
    CheckEq(again.mCurrent.mSfx.mVolume, 31, "launcher to game: the game's login has sound effects");
    Check(again.mCurrent.mMusic.mOn, "launcher to game: the game's login has music on");
    Check(again.mCurrent.mAdvisorOn, "launcher to game: the game's login has Advisor");
    Check(!again.mCurrent.mRMBScrollOn, "launcher to game: the game's login has Pushscroll");
    Check(again.mCurrent.mIsometricOn, "launcher to game: the game's login has 90 degs");
    CheckEq(again.mCurrent.mMovie.mVolume, 34, "launcher to game: the game's login has the movie volume");
    CheckEq(again.mCurrent.mSpeech.mVolume, 23, "launcher to game: what was not changed stays");
    Check(!again.mCurrent.mRMBCancelOn, "launcher to game: what was not changed stays");
}

void CheckOldValues(const fs::path& root) {
    const fs::path sam = root / "sound.sam";
    const fs::path user = root / "old";
    const fs::path config = user / "save" / "Config.tcf";
    SoundTable(sam);
    int screenShape = 1;
    const GameSizeRules rules = PortGameSizeRules(10, [&] { return screenShape; }, [] { return GameSize{2880, 1800}; });
    int shape = -1;
    int32_t wide = 0, high = 0;

    // A file of an earlier version: 0x100 + preset has no shape, so the screen's; both sides agree on the size.
    for (int preset = 1; preset <= kUISizePresetCount; ++preset) {
        const int old = kPortResolutionChosen + preset;
        const GameSize size = rules.chosen(old, shape);
        CheckEq(shape, screenShape, "old value: the screen's shape");
        CheckEq(PortResolutionPreset(old, 0), preset, "old value: the game's size is the same");
        UI_PresetSize(preset, UI_ShapeAspect(screenShape), wide, high);
        Check(size == GameSize{wide, high}, "old value: its size");
    }
    // The form between it and the exact size names its shape as well.
    for (int s = 0; s < kUISizeShapeCount; ++s) {
        const GameSize size = rules.chosen(PortResolutionSetting(4, s), shape);
        CheckEq(shape, s, "shape and preset: the shape it names");
        UI_PresetSize(4, UI_ShapeAspect(s), wide, high);
        Check(size == GameSize{wide, high}, "shape and preset: its size");
    }
    // The game's own first number: no size named, so the launcher's default, the game's largest at the screen's shape.
    {
        const GameSize size = rules.chosen(2, shape);
        CheckEq(shape, screenShape, "no size saved: the screen's shape");
        CheckEq(PortResolutionPreset(2, 10), 10, "no size saved: the game's default is the same");
        UI_PresetSize(10, UI_ShapeAspect(screenShape), wide, high);
        Check(size == GameSize{wide, high}, "no size saved: the default size");
    }
    // A number of nothing: what the game would take too, and no crash.
    for (const int odd : {0, -1, 0x7fffffff, 0x0500, 0x050a, 0x010b, 0x0100, kPortResolutionExact, PortResolutionExact(100, 50)}) {
        const GameSize size = rules.chosen(odd, shape);
        Check(shape >= 0 && shape < kUISizeShapeCount, "strange value: a shape");
        Check(!PortResolutionSize(odd, wide, high), "strange value: no exact size for the game");
        UI_PresetSize(PortResolutionPreset(odd, 10), UI_ShapeAspect(shape), wide, high);
        Check(size == GameSize{wide, high}, "strange value: the size the game takes");
    }
    // A file with an old value, the rest changed by the launcher: the old value stays as it was.
    CPlayerSettings game;
    fs::create_directories(config.parent_path());
    Check(!GameLoad(game, config), "old file: none yet");
    game.mCurrent.mResolution = CPlayerSettings::eRESOLUTION(0x103);
    Check(GameSave(game, config), "old file: the game's save");
    std::string err;
    Check(WriteGameOption(user, sam, "", GameOption::AudioQuality, 20, err), "old file: another setting changed");
    CPlayerSettings after;
    Check(GameLoad(after, config), "old file: loads");
    CheckEq(after.mCurrent.mResolution, 0x103, "old file: its old size number is kept");
    CheckEq(after.mCurrent.mSoundQuality, 20, "old file: the change");

    // Short and damaged: the game keeps defaults, the launcher will not touch it.
    const std::vector<uint8_t> whole = ReadAll(config);
    std::vector<uint8_t> shorter(whole.begin(), whole.begin() + 18);
    WriteAll(config, shorter);
    CPlayerSettings damaged;
    Check(!GameLoad(damaged, config), "short file: the game's load says it failed");
    CheckEq(damaged.mCurrent.mDetail, 2, "short file: the game keeps what it had");
    const GameSettingsView view = ReadGameSettings(user, sam, "");
    Check(view.configState == GameFileState::Damaged, "short file: the launcher says it does not read");
    Check(!WriteGameOption(user, sam, "", GameOption::AudioQuality, 20, err), "short file: the launcher refuses");
    Check(ReadAll(config) == shorter, "short file: left as it was");
}

}  // namespace

int main() {
    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec) / ("tpw-settings-sync-" + std::to_string(reinterpret_cast<uintptr_t>(&ec)));
    fs::remove_all(root, ec);
    fs::create_directories(root);
    CheckConfigCompare();
    CheckPaddingHarmless(root);
    CheckFirstFile(root);
    CheckBothWays(root);
    CheckOldValues(root);
    fs::remove_all(root, ec);
    std::printf("settings sync: %d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
