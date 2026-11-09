// game_settings_tests.cpp: the launcher's reads and writes of the game's settings files (launcher/ui/game_settings.h), on real
// files in a folder of its own: only the setting's bytes change, a missing file gives the game's defaults, an old, short or
// damaged file does no harm and is left as it is, and nothing is written while the game holds its lock. No SDL and no game
// code; the same files against the game's own code are in settings_sync_game_test.cpp. Run by ctest ("game_settings");
// exits nonzero on a failure.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "game_settings.h"

namespace fs = std::filesystem;
using namespace tpw::ui;

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

void Put(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

// Config.tcf as a real run of the game writes it, with marks in its padding that no write may touch.
std::vector<uint8_t> RealConfig() {
    return {1, 0, 0, 0,  1, 0, 0, 0,  2, 0, 0, 0,  2, 0, 0, 0,  0, 0, 0, 0,  1, 0xa1, 0xa2, 0xa3,  100, 0, 0, 0,  75, 0, 0, 0};
}

// A gms.dat: the game's layout around the 39 bytes (CFSGlobalMasterSave::Serialise), with `themes` parks, `bought` things after
// it, and every byte that is not a setting different from its neighbours. Its local block is the first start's values.
constexpr long kRecord = 160;
std::vector<uint8_t> RealPlayer(const std::vector<std::string>& themes, uint32_t bought, long* blockAt = nullptr) {
    std::vector<uint8_t> out;
    Put(out, 0xc);
    for (uint8_t i = 0; i < 6; ++i) out.push_back(static_cast<uint8_t>(0x10 + i));
    Put(out, 12345);
    Put(out, 2);
    out.push_back(1); out.push_back(1); out.push_back(0);
    Put(out, static_cast<uint32_t>(themes.size()));
    for (const std::string& theme : themes) {
        Put(out, static_cast<uint32_t>(theme.size()));
        out.insert(out.end(), theme.begin(), theme.end());
        for (long i = 0; i < kRecord; ++i) out.push_back(static_cast<uint8_t>(i * 7));
    }
    if (blockAt != nullptr) *blockAt = static_cast<long>(out.size());
    for (const uint32_t volume : {75u, 60u, 75u, 100u}) {
        out.push_back(1); out.push_back(0xb1); out.push_back(0xb2); out.push_back(0xb3);
        Put(out, volume);
    }
    for (const uint8_t flag : {1, 1, 1, 1, 0, 1, 1}) out.push_back(flag);
    Put(out, bought);
    for (uint32_t i = 0; i < bought; ++i) { out.push_back(static_cast<uint8_t>(i)); out.push_back(0x77); }
    return out;
}

// Only the bytes at [at, at + count) differ.
bool DiffersOnlyIn(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, long at, long count) {
    if (a.size() != b.size()) return false;
    for (long i = 0; i < static_cast<long>(a.size()); ++i)
        if (a[static_cast<size_t>(i)] != b[static_cast<size_t>(i)] && (i < at || i >= at + count)) return false;
    return true;
}

void CheckConfigBytes() {
    const std::vector<uint8_t> real = RealConfig();
    GameConfig config;
    Check(ParseGameConfig(real, config), "Config.tcf: a real file reads");
    CheckEq(config.renderer, 1, "Config.tcf: renderer");
    CheckEq(config.resolution, 2, "Config.tcf: resolution");
    CheckEq(config.detail, 2, "Config.tcf: detail");
    CheckEq(config.videoCard, 0, "Config.tcf: video card");
    Check(config.movie.on, "Config.tcf: movie on");
    CheckEq(config.movie.volume, 100, "Config.tcf: movie volume");
    CheckEq(config.soundQuality, 75, "Config.tcf: audio quality");

    struct Case { GameOption option; int value; long at; long count; };
    const Case cases[] = {{GameOption::Resolution, 0x0307, 8, 4},    {GameOption::GraphicsQuality, 1, 12, 4},
                          {GameOption::Videocard, 1, 16, 4},         {GameOption::AudioQuality, 40, 28, 4},
                          {GameOption::MovieVolume, 30, 24, 4},      {GameOption::MovieOn, 0, 20, 1},
                          {GameOption::Rendering, 1, 4, 4}};
    for (const Case& c : cases) {
        std::vector<uint8_t> bytes = real;
        Check(SetGameConfigOption(bytes, c.option, c.value), "Config.tcf: a change is made");
        Check(DiffersOnlyIn(real, bytes, c.at, c.count), "Config.tcf: only the setting's own bytes change");
        Check(bytes[21] == 0xa1 && bytes[22] == 0xa2 && bytes[23] == 0xa3, "Config.tcf: the padding stays");
    }
    std::vector<uint8_t> bytes = real;
    Check(SetGameConfigOption(bytes, GameOption::Resolution, 0x0307), "Config.tcf: a size");
    Check(ParseGameConfig(bytes, config) && config.resolution == 0x0307, "Config.tcf: reads back as written");
    // The Options panel's Rendering button: Software has no High.
    bytes = real;
    Check(SetGameConfigOption(bytes, GameOption::Rendering, 0), "Config.tcf: Software rendering");
    Check(ParseGameConfig(bytes, config) && config.renderer == 0 && config.detail == 1,
          "Config.tcf: Software rendering lowers High to Medium, as the panel's button does");
    Check(DiffersOnlyIn(real, bytes, 4, 12), "Config.tcf: Software rendering changes only those");
    bytes = real;
    Check(!SetGameConfigOption(bytes, GameOption::Advisor, 0), "Config.tcf: it keeps no Advisor");
    Check(bytes == real, "Config.tcf: a refused change changes nothing");
    // A value out of the field's range is not written.
    Check(!SetGameConfigOption(bytes, GameOption::AudioQuality, 101), "Config.tcf: audio quality has no 101");
    Check(!SetGameConfigOption(bytes, GameOption::GraphicsQuality, 3), "Config.tcf: graphics quality has no 3");
    Check(!SetGameConfigOption(bytes, GameOption::MovieVolume, -1), "Config.tcf: movie volume has no -1");
    Check(bytes == real, "Config.tcf: out of range changes nothing");

    // Old, short and strange ones.
    std::vector<uint8_t> shortFile(real.begin(), real.begin() + 20);
    Check(!ParseGameConfig(shortFile, config), "Config.tcf: a short file does not read");
    Check(!SetGameConfigOption(shortFile, GameOption::Rendering, 1), "Config.tcf: a short file cannot be changed");
    Check(!ParseGameConfig({}, config), "Config.tcf: an empty file does not read");
    std::vector<uint8_t> version0 = real;
    version0[0] = 0;
    Check(!ParseGameConfig(version0, config), "Config.tcf: version 0 does not read, as in the game");
    std::vector<uint8_t> longer = real;
    longer.push_back(0x55);
    Check(ParseGameConfig(longer, config), "Config.tcf: a longer file reads, as in the game, which stops at its last field");
    Check(SetGameConfigOption(longer, GameOption::AudioQuality, 50) && longer.size() == 33 && longer[32] == 0x55,
          "Config.tcf: what follows the fields stays");
    // The encoder is the game's first file.
    GameConfig first;
    first.movie = {true, 100};
    first.soundQuality = 75;
    std::vector<uint8_t> expected = real;
    expected[21] = expected[22] = expected[23] = 0;
    Check(EncodeGameConfig(first) == expected, "Config.tcf: a new file has the bytes a real first run wrote");
}

void CheckPlayerBytes() {
    // The sizes of a real file: a new player is 68 bytes, with its block at 25.
    const std::vector<uint8_t> none = {
        0x0c, 0, 0, 0,  0, 0, 0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  1, 1, 1,  0, 0, 0, 0,
        1, 0, 0, 0, 75, 0, 0, 0,  1, 0, 0, 0, 60, 0, 0, 0,  1, 0, 0, 0, 75, 0, 0, 0,  1, 0, 0, 0, 100, 0, 0, 0,
        1, 1, 1, 1, 0, 1, 1,  0, 0, 0, 0};
    CheckEq(static_cast<long>(none.size()), 68, "gms.dat: a new player's file as a real run wrote it");
    CheckEq(PlayerOptionsOffset(none), 25, "gms.dat: where a new player's block is");
    PlayerOptions options;
    Check(ParsePlayerOptions(none, options), "gms.dat: a new player's reads");
    CheckEq(options.sfx.volume, 75, "gms.dat: sound effects");
    CheckEq(options.music.volume, 60, "gms.dat: music");
    CheckEq(options.speech.volume, 75, "gms.dat: speech");
    CheckEq(options.movie.volume, 100, "gms.dat: movie");
    Check(options.sfx.on && options.music.on && options.speech.on && options.movie.on, "gms.dat: on");
    Check(options.advisor && options.tutorial && options.tooltips && options.confirmDelete, "gms.dat: flags on");
    Check(!options.rmbScroll && options.rmbCancel && options.isometric, "gms.dat: the rest");

    long blockAt = 0;
    const std::vector<uint8_t> real = RealPlayer({"fantasy", "hallow", "jungle", "space"}, 3, &blockAt);
    CheckEq(PlayerOptionsOffset(real), blockAt, "gms.dat: the block is found after four parks' records");
    struct Case { GameOption option; int value; long at; long count; };
    const Case cases[] = {{GameOption::SfxOn, 0, 0, 1},       {GameOption::SfxVolume, 10, 4, 4},   {GameOption::MusicOn, 0, 8, 1},
                          {GameOption::MusicVolume, 11, 12, 4}, {GameOption::SpeechOn, 0, 16, 1},  {GameOption::SpeechVolume, 12, 20, 4},
                          {GameOption::MovieOn, 0, 24, 1},    {GameOption::MovieVolume, 13, 28, 4}, {GameOption::Advisor, 0, 32, 1},
                          {GameOption::Tutorial, 0, 33, 1},   {GameOption::PopupHelp, 0, 34, 1},   {GameOption::Confirmations, 0, 35, 1},
                          {GameOption::Scroll, 1, 36, 1},     {GameOption::RmbCancel, 0, 37, 1},   {GameOption::Rotation, 0, 38, 1}};
    for (const Case& c : cases) {
        std::vector<uint8_t> bytes = real;
        Check(SetPlayerOption(bytes, c.option, c.value), "gms.dat: a change is made");
        Check(DiffersOnlyIn(real, bytes, blockAt + c.at, c.count), "gms.dat: only the setting's own bytes change");
        Check(bytes != real, "gms.dat: the change is in the file");
    }
    std::vector<uint8_t> bytes = real;
    Check(SetPlayerOption(bytes, GameOption::Scroll, 1) && SetPlayerOption(bytes, GameOption::SfxVolume, 33),
          "gms.dat: two changes");
    Check(ParsePlayerOptions(bytes, options) && options.rmbScroll && options.sfx.volume == 33, "gms.dat: reads back");
    bytes = real;
    Check(!SetPlayerOption(bytes, GameOption::GraphicsQuality, 0), "gms.dat: it keeps no graphics quality");
    Check(!SetPlayerOption(bytes, GameOption::SfxVolume, 101), "gms.dat: a volume has no 101");
    Check(bytes == real, "gms.dat: a refused change changes nothing");

    // Damaged in every way a cut can: no prefix of the file is taken for the whole.
    for (size_t cut = 0; cut < real.size(); cut += 37) {
        std::vector<uint8_t> shorter(real.begin(), real.begin() + static_cast<long>(cut));
        CheckEq(PlayerOptionsOffset(shorter), -1, "gms.dat: a cut file is not read");
    }
    std::vector<uint8_t> shorter(real.begin(), real.end() - 1);
    CheckEq(PlayerOptionsOffset(shorter), -1, "gms.dat: a byte short is not read");
    std::vector<uint8_t> older = real;
    older[0] = 0xb;
    CheckEq(PlayerOptionsOffset(older), -1, "gms.dat: an older version is not read, as in the game");
    std::vector<uint8_t> hugeCount = real;
    hugeCount[24] = 0x7f;
    CheckEq(PlayerOptionsOffset(hugeCount), -1, "gms.dat: an impossible count is not read");
    std::vector<uint8_t> hugeName = real;
    hugeName[28] = 0xff;
    CheckEq(PlayerOptionsOffset(hugeName), -1, "gms.dat: an impossible name is not read");
    std::vector<uint8_t> trailing = real;
    trailing.push_back(9);
    CheckEq(PlayerOptionsOffset(trailing), blockAt, "gms.dat: what follows the fields does not matter to the game");
}

constexpr const char* kSoundSam =
    "; sound\nSoundInfo.DEFAULTQUALITY\t50\nSoundInfo.SOUNDON\t\t1\nDefaultVolume.SFX 75\nDefaultVolume.MUSIC 60\n"
    "DefaultVolume.SPEECH 75\nDefaultVolume.MOVIE 100\n";

void CheckFolders(const fs::path& root) {
    const fs::path user = root / "user";
    const fs::path sam = root / "sound.sam";
    {
        std::ofstream out(sam);
        out << kSoundSam;
    }
    // Nothing there: the game's defaults, nothing made.
    GameSettingsView view = ReadGameSettings(user, sam, "");
    Check(view.configState == GameFileState::Missing, "no folder: no Config.tcf");
    Check(view.defaultsKnown, "no folder: the defaults come from sound.sam");
    CheckEq(view.config.resolution, 2, "no folder: the game's resolution");
    CheckEq(view.config.detail, 2, "no folder: the game's graphics quality");
    CheckEq(view.config.soundQuality, 75, "no folder: the game's audio quality ((50 + 100) / 2)");
    CheckEq(view.config.movie.volume, 100, "no folder: the game's movie volume");
    CheckEq(view.player, -1, "no folder: no player");
    Check(!view.running, "no folder: no game");
    Check(!fs::exists(user), "no folder: reading makes nothing");

    GameSettingsView noSam = ReadGameSettings(user, root / "none.sam", "");
    Check(!noSam.defaultsKnown, "no sound.sam: the defaults are not known");
    std::string err;
    Check(!WriteGameOption(user, root / "none.sam", "", GameOption::AudioQuality, 50, err), "no sound.sam: no write");
    Check(!fs::exists(user / "save" / "Config.tcf"), "no sound.sam: no file");

    // The first write makes the whole file with the game's defaults and the one change.
    Check(WriteGameOption(user, sam, "", GameOption::GraphicsQuality, 1, err), "first write: made");
    GameConfig expected;
    expected.movie = {true, 100};
    expected.soundQuality = 75;
    expected.detail = 1;
    Check(ReadAll(user / "save" / "Config.tcf") == EncodeGameConfig(expected), "first write: the defaults and the one change");
    view = ReadGameSettings(user, sam, "");
    Check(view.configState == GameFileState::Ok, "first write: reads");
    CheckEq(view.config.detail, 1, "first write: the change");
    Check(!fs::exists(user / "save" / "Config.tcf.part"), "first write: nothing left beside it");
    // A player's option with no player.
    Check(!WriteGameOption(user, sam, "", GameOption::Advisor, 0, err), "no player: no write");

    // Written under a name of another case: the same file.
    fs::rename(user / "save" / "Config.tcf", user / "save" / "config.tcf");
    Check(WriteGameOption(user, sam, "", GameOption::AudioQuality, 40, err), "lower case name: written");
    CheckEq(ReadGameSettings(user, sam, "").config.soundQuality, 40, "lower case name: read");
    fs::remove(user / "save" / "config.tcf");
    fs::remove(user / "save" / "Config.tcf");

    // Damaged: shown as such, refused, left exactly as it was.
    const std::vector<uint8_t> damaged = {1, 0, 0, 0, 9, 9};
    WriteAll(user / "save" / "Config.tcf", damaged);
    view = ReadGameSettings(user, sam, "");
    Check(view.configState == GameFileState::Damaged, "damaged Config.tcf: said so");
    CheckEq(view.config.detail, 2, "damaged Config.tcf: shows the game's defaults");
    Check(!WriteGameOption(user, sam, "", GameOption::AudioQuality, 50, err), "damaged Config.tcf: refused");
    Check(!err.empty(), "damaged Config.tcf: says why");
    Check(ReadAll(user / "save" / "Config.tcf") == damaged, "damaged Config.tcf: left alone");
    WriteAll(user / "save" / "Config.tcf", RealConfig());

    // Players: the last written, unless named; a change keeps the file's time, so it stays "the last one played".
    const fs::path first = user / "save" / "users" / "1Ann" / "gms.dat";
    const fs::path second = user / "save" / "users" / "3Bob" / "gms.dat";
    long blockAt = 0;
    const std::vector<uint8_t> ann = RealPlayer({"jungle"}, 0, &blockAt);
    const std::vector<uint8_t> bob = RealPlayer({}, 2);
    WriteAll(first, ann);
    WriteAll(second, bob);
    fs::create_directories(user / "save" / "users" / "not a player");
    fs::create_directories(user / "save" / "users" / "2Empty");
    const auto now = fs::last_write_time(first);
    fs::last_write_time(first, now - std::chrono::hours(2));
    fs::last_write_time(second, now - std::chrono::hours(1));
    view = ReadGameSettings(user, sam, "");
    CheckEq(static_cast<long>(view.players.size()), 2, "players: those with a gms.dat, each in a slot");
    CheckEq(view.players[0].slot, 1, "players: by slot");
    Check(view.players[0].name == "Ann" && view.players[1].name == "Bob", "players: their names");
    CheckEq(view.player, 1, "players: the last written is shown");
    view = ReadGameSettings(user, sam, "Ann");
    CheckEq(view.player, 0, "players: one named is shown");
    view = ReadGameSettings(user, sam, "Gone");
    CheckEq(view.player, 1, "players: one no longer there falls back to the last written");
    const auto bobTime = fs::last_write_time(second);
    Check(WriteGameOption(user, sam, "", GameOption::Advisor, 0, err), "players: written to the last one");
    Check(fs::last_write_time(second) == bobTime, "players: a change keeps the file's time");
    CheckEq(ReadGameSettings(user, sam, "").player, 1, "players: still the last one played");
    Check(!ReadGameSettings(user, sam, "Bob").options.advisor, "players: the change is the shown player's");
    Check(ReadGameSettings(user, sam, "Ann").options.advisor, "players: not the other's");
    Check(ReadAll(first) == ann, "players: the other's file is untouched");
    Check(WriteGameOption(user, sam, "Ann", GameOption::Rotation, 0, err), "players: written to one named");
    Check(!ReadGameSettings(user, sam, "Ann").options.isometric, "players: Rotation to Smooth");
    Check(DiffersOnlyIn(ann, ReadAll(first), blockAt + 38, 1), "players: only that byte");

    // The movie volume is in both files, and both change.
    const std::vector<uint8_t> configBefore = ReadAll(user / "save" / "Config.tcf");
    Check(WriteGameOption(user, sam, "Ann", GameOption::MovieVolume, 55, err), "movie: written");
    view = ReadGameSettings(user, sam, "Ann");
    CheckEq(view.config.movie.volume, 55, "movie: in Config.tcf");
    CheckEq(view.options.movie.volume, 55, "movie: in gms.dat");
    Check(DiffersOnlyIn(configBefore, ReadAll(user / "save" / "Config.tcf"), 24, 4), "movie: only its bytes in Config.tcf");

    // A damaged player's file: said so, left, and the movie volume then changes neither file.
    const std::vector<uint8_t> badPlayer = {0xc, 0, 0, 0, 1, 2, 3};
    const std::vector<uint8_t> configKept = ReadAll(user / "save" / "Config.tcf");
    WriteAll(first, badPlayer);
    view = ReadGameSettings(user, sam, "Ann");
    Check(view.playerState == GameFileState::Damaged, "damaged gms.dat: said so");
    Check(!WriteGameOption(user, sam, "Ann", GameOption::Advisor, 0, err), "damaged gms.dat: refused");
    Check(ReadAll(first) == badPlayer, "damaged gms.dat: left alone");
    Check(!WriteGameOption(user, sam, "Ann", GameOption::MovieVolume, 20, err), "damaged gms.dat: the movie volume refused");
    Check(ReadAll(first) == badPlayer && ReadAll(user / "save" / "Config.tcf") == configKept, "damaged gms.dat: neither file changed");
    WriteAll(first, ann);

    // Videocard: this system has one device, and the game would put a saved Secondary back (TPEngine).
    Check(!WriteGameOption(user, sam, "", GameOption::Videocard, 1, err), "Videocard: not written");

    // Unwritable folder: said so, the file as it was. Not on Windows, where the read-only mark on a folder does not stop files
    // being made in it, so this would make no folder unwritable.
#ifndef _WIN32
    const std::vector<uint8_t> beforeLocked = ReadAll(user / "save" / "Config.tcf");
    fs::permissions(user / "save", fs::perms::owner_read | fs::perms::owner_exec);
    const bool wrote = WriteGameOption(user, sam, "", GameOption::AudioQuality, 11, err);
    fs::permissions(user / "save", fs::perms::owner_all);
    Check(!wrote && !err.empty(), "read-only folder: refused, and says why");
    Check(ReadAll(user / "save" / "Config.tcf") == beforeLocked, "read-only folder: the file as it was");
#endif
}

void CheckLock(const fs::path& root) {
    const fs::path user = root / "locked";
    const fs::path sam = root / "sound.sam";
    fs::create_directories(user / "save");
    WriteAll(user / "save" / "Config.tcf", RealConfig());
    Check(!GameIsRunning(user), "lock: no game");
    {
        GameFileLock game;
        Check(game.Take(user), "lock: the game takes it");
        Check(game.Held(), "lock: held");
        Check(GameIsRunning(user), "lock: the launcher sees the game");
        GameSettingsView view = ReadGameSettings(user, sam, "");
        Check(view.running, "lock: the read says so");
        CheckEq(view.config.soundQuality, 75, "lock: the file still reads");
        std::string err;
        Check(!WriteGameOption(user, sam, "", GameOption::AudioQuality, 33, err), "lock: no write while the game runs");
        Check(!err.empty(), "lock: says why");
        Check(ReadAll(user / "save" / "Config.tcf") == RealConfig(), "lock: the file is untouched");
        GameFileLock other;
        Check(!other.Take(user), "lock: not twice");
        Check(!other.Held(), "lock: not held by the second");
    }
    Check(!GameIsRunning(user), "lock: the game ended");
    std::string err;
    Check(WriteGameOption(user, sam, "", GameOption::AudioQuality, 33, err), "lock: written once the game ended");
    Check(!GameIsRunning(user), "lock: the launcher's write lets go of it");
    {   // A launcher's write in progress does not stop the game: it waits and gets it.
        GameFileLock game;
        Check(game.Take(user, 1000), "lock: taken with a wait");
        game.Release();
        Check(!game.Held(), "lock: released");
    }
}

}  // namespace

int main() {
    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec) / ("tpw-game-settings-" + std::to_string(reinterpret_cast<uintptr_t>(&ec)));
    fs::remove_all(root, ec);
    fs::create_directories(root);
    CheckConfigBytes();
    CheckPlayerBytes();
    CheckFolders(root);
    CheckLock(root);
    fs::remove_all(root, ec);
    std::printf("game settings: %d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
