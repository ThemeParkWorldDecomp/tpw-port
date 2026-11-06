// game_settings.cpp: game_settings.h. The offsets are the game's: CPlayerSettings::SerialiseGlobal and SerialiseLocal
// (src/game/TPSettings.cpp), CFSGlobalMasterSave::Serialise and CFSGlobalParkData::Serialise (src/game/save/FS_MasterSave.cpp),
// and the default values CPlayerSettings::Init's.
#include "game_settings.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace tpw::ui {

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kSettingsVersion = 1;       // CPlayerSettings' kSettingsVersion: the game refuses a lower one
constexpr uint32_t kMasterSaveVersion = 0xc;   // CFSGlobalMasterSave's: the game refuses a lower one
constexpr size_t kParkRecordSize = 6 + 4 * (1 + 4) + 0x21 * (2 + 2) + 2;   // CFSGlobalParkData::Serialise
constexpr size_t kThemeCountAt = 4 + 6 + 4 + 4 + 3;   // the version, the tickets, spent, extra keys, three flags
constexpr uint32_t kLongestThemeName = 4096;   // far above any theme's; a larger number is a damaged file
constexpr const char* kConfigName = "Config.tcf";   // gTPGlobals.mConfigFile
constexpr const char* kLockName = "game-running.lock";

int32_t ReadInt(const std::vector<uint8_t>& bytes, size_t at) {
    return static_cast<int32_t>(static_cast<uint32_t>(bytes[at]) | static_cast<uint32_t>(bytes[at + 1]) << 8 |
                                static_cast<uint32_t>(bytes[at + 2]) << 16 | static_cast<uint32_t>(bytes[at + 3]) << 24);
}

void WriteInt(std::vector<uint8_t>& bytes, size_t at, int32_t value) {
    const uint32_t bits = static_cast<uint32_t>(value);
    for (int i = 0; i < 4; ++i) bytes[at + static_cast<size_t>(i)] = static_cast<uint8_t>(bits >> (8 * i));
}

GameVolume ReadVolume(const std::vector<uint8_t>& bytes, size_t at) {
    return {bytes[at] != 0, ReadInt(bytes, at + 4)};
}

void WriteVolume(std::vector<uint8_t>& bytes, size_t at, const GameVolume& volume) {
    bytes[at] = volume.on ? 1 : 0;
    WriteInt(bytes, at + 4, volume.volume);
}

// An option's place in Config.tcf: a byte (an on and off) or a number. -1: it has none.
struct Field {
    long at;
    bool isByte;
};

Field ConfigField(GameOption option) {
    switch (option) {
        case GameOption::Rendering: return {4, false};
        case GameOption::Resolution: return {8, false};
        case GameOption::GraphicsQuality: return {12, false};
        case GameOption::Videocard: return {16, false};
        case GameOption::MovieOn: return {20, true};
        case GameOption::MovieVolume: return {24, false};
        case GameOption::AudioQuality: return {28, false};
        default: return {-1, false};
    }
}

Field PlayerField(GameOption option) {
    switch (option) {
        case GameOption::SfxOn: return {0, true};
        case GameOption::SfxVolume: return {4, false};
        case GameOption::MusicOn: return {8, true};
        case GameOption::MusicVolume: return {12, false};
        case GameOption::SpeechOn: return {16, true};
        case GameOption::SpeechVolume: return {20, false};
        case GameOption::MovieOn: return {24, true};
        case GameOption::MovieVolume: return {28, false};
        case GameOption::Advisor: return {32, true};
        case GameOption::Tutorial: return {33, true};
        case GameOption::PopupHelp: return {34, true};
        case GameOption::Confirmations: return {35, true};
        case GameOption::Scroll: return {36, true};
        case GameOption::RmbCancel: return {37, true};
        case GameOption::Rotation: return {38, true};
        default: return {-1, false};
    }
}

// The values the Options panel can give the option (Screen resolution's numbers are the game's own: GameSizeRules).
bool ValueAllowed(GameOption option, int value) {
    switch (option) {
        case GameOption::Resolution: return value >= 0;
        case GameOption::GraphicsQuality: return value >= 0 && value <= 2;
        case GameOption::Rendering:
        case GameOption::Videocard: return value == 0 || value == 1;
        case GameOption::AudioQuality:
        case GameOption::SfxVolume:
        case GameOption::MusicVolume:
        case GameOption::SpeechVolume:
        case GameOption::MovieVolume: return value >= 0 && value <= 100;
        default: return true;
    }
}

bool InRange(const GameVolume& volume) { return volume.volume >= 0 && volume.volume <= 100; }

bool ReadBytes(const fs::path& file, std::vector<uint8_t>& bytes) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

// Replaces `file` in one step: the new bytes in a file beside it, then the rename. The old file's permissions stay.
bool ReplaceFile(const fs::path& file, const std::vector<uint8_t>& bytes, std::string& err) {
    std::error_code ec;
    fs::path part = file;
    part += ".part";
    {
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out) {
            err = "cannot write beside " + file.filename().u8string();
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) {
            err = "cannot write beside " + file.filename().u8string();
            fs::remove(part, ec);
            return false;
        }
    }
    const fs::perms perms = fs::status(file, ec).permissions();
    if (!ec && perms != fs::perms::unknown) fs::permissions(part, perms, ec);
    ec.clear();
    fs::rename(part, file, ec);
    if (ec) {
        err = "cannot replace " + file.filename().u8string() + ": " + ec.message();
        std::error_code ignored;
        fs::remove(part, ignored);
        return false;
    }
    return true;
}

// The file with this name in `folder`, however its letters are written (the game opens "config.tcf" and writes
// "Config.tcf", and its own lookup does not mind the case), or the game's name for it when there is none.
fs::path FindIgnoringCase(const fs::path& folder, std::string_view name) {
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string found = it->path().filename().u8string();
        if (found.size() == name.size() &&
            std::equal(found.begin(), found.end(), name.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            }))
            return it->path();
    }
    return folder / fs::u8path(std::string(name));
}

fs::path ConfigFile(const fs::path& userFolder) { return FindIgnoringCase(userFolder / "save", kConfigName); }

// The players of a folder as the game lists them (CFSUserInfo::GameLoginInit): save/users/<digit 1 to 4><name>/gms.dat.
std::vector<GamePlayer> ListPlayers(const fs::path& userFolder) {
    std::vector<GamePlayer> players;
    std::error_code ec;
    const fs::path users = userFolder / "save" / "users";
    for (fs::directory_iterator it(users, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string folder = it->path().filename().u8string();
        if (folder.size() < 2 || folder[0] < '1' || folder[0] > '4') continue;
        const fs::path file = FindIgnoringCase(it->path(), "gms.dat");
        std::error_code stat;
        const fs::file_time_type written = fs::last_write_time(file, stat);
        if (stat) continue;
        GamePlayer player;
        player.slot = folder[0] - '0';
        player.name = folder.substr(1);
        player.file = file;
        player.written = written;
        players.push_back(player);
    }
    std::sort(players.begin(), players.end(), [](const GamePlayer& a, const GamePlayer& b) {
        return a.slot != b.slot ? a.slot < b.slot : a.name < b.name;
    });
    return players;
}

int ChoosePlayer(const std::vector<GamePlayer>& players, const std::string& name) {
    int newest = -1;
    for (size_t i = 0; i < players.size(); ++i) {
        if (!name.empty() && players[i].name == name) return static_cast<int>(i);
        if (newest < 0 || players[i].written > players[static_cast<size_t>(newest)].written) newest = static_cast<int>(i);
    }
    return newest;
}

}  // namespace

bool OptionInConfig(GameOption option) { return ConfigField(option).at >= 0; }
bool OptionInPlayer(GameOption option) { return PlayerField(option).at >= 0; }

bool ParseGameConfig(const std::vector<uint8_t>& bytes, GameConfig& out) {
    if (bytes.size() < kGameConfigSize) return false;
    if (static_cast<uint32_t>(ReadInt(bytes, 0)) < kSettingsVersion) return false;
    out.renderer = ReadInt(bytes, 4);
    out.resolution = ReadInt(bytes, 8);
    out.detail = ReadInt(bytes, 12);
    out.videoCard = ReadInt(bytes, 16);
    out.movie = ReadVolume(bytes, 20);
    out.soundQuality = ReadInt(bytes, 28);
    return true;
}

std::vector<uint8_t> EncodeGameConfig(const GameConfig& config) {
    std::vector<uint8_t> bytes(kGameConfigSize, 0);
    WriteInt(bytes, 0, static_cast<int32_t>(kSettingsVersion));
    WriteInt(bytes, 4, config.renderer);
    WriteInt(bytes, 8, config.resolution);
    WriteInt(bytes, 12, config.detail);
    WriteInt(bytes, 16, config.videoCard);
    WriteVolume(bytes, 20, config.movie);
    WriteInt(bytes, 28, config.soundQuality);
    return bytes;
}

bool SetGameConfigOption(std::vector<uint8_t>& bytes, GameOption option, int value) {
    GameConfig current;
    if (!ParseGameConfig(bytes, current)) return false;
    const Field field = ConfigField(option);
    if (field.at < 0 || !ValueAllowed(option, value)) return false;
    if (field.isByte) bytes[static_cast<size_t>(field.at)] = value != 0 ? 1 : 0;
    else WriteInt(bytes, static_cast<size_t>(field.at), value);
    // The panel's button: the detail read back through a slider that is halved in Software rendering (case 17, DoScroll).
    if (option == GameOption::Rendering && value == 0 && current.renderer != 0)
        WriteInt(bytes, 12, current.detail * 100 / 2 / 100);
    return true;
}

long PlayerOptionsOffset(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < kThemeCountAt + 4) return -1;
    if (static_cast<uint32_t>(ReadInt(bytes, 0)) < kMasterSaveVersion) return -1;
    const int32_t themes = ReadInt(bytes, kThemeCountAt);
    if (themes < 0) return -1;
    size_t at = kThemeCountAt + 4;
    for (int32_t i = 0; i < themes; ++i) {
        if (at + 4 > bytes.size()) return -1;
        const uint32_t length = static_cast<uint32_t>(ReadInt(bytes, at));
        if (length > kLongestThemeName) return -1;
        at += 4 + length + kParkRecordSize;
        if (at > bytes.size()) return -1;
    }
    if (at + kPlayerOptionsSize + 4 > bytes.size()) return -1;
    const int32_t bought = ReadInt(bytes, at + kPlayerOptionsSize);
    if (bought < 0 || static_cast<uint64_t>(bought) * 2 > bytes.size() - at - kPlayerOptionsSize - 4) return -1;
    if (!InRange(ReadVolume(bytes, at)) || !InRange(ReadVolume(bytes, at + 8)) || !InRange(ReadVolume(bytes, at + 16)) ||
        !InRange(ReadVolume(bytes, at + 24)))
        return -1;
    return static_cast<long>(at);
}

bool ParsePlayerOptions(const std::vector<uint8_t>& bytes, PlayerOptions& out) {
    const long found = PlayerOptionsOffset(bytes);
    if (found < 0) return false;
    const size_t at = static_cast<size_t>(found);
    out.sfx = ReadVolume(bytes, at);
    out.music = ReadVolume(bytes, at + 8);
    out.speech = ReadVolume(bytes, at + 16);
    out.movie = ReadVolume(bytes, at + 24);
    out.advisor = bytes[at + 32] != 0;
    out.tutorial = bytes[at + 33] != 0;
    out.tooltips = bytes[at + 34] != 0;
    out.confirmDelete = bytes[at + 35] != 0;
    out.rmbScroll = bytes[at + 36] != 0;
    out.rmbCancel = bytes[at + 37] != 0;
    out.isometric = bytes[at + 38] != 0;
    return true;
}

bool SetPlayerOption(std::vector<uint8_t>& bytes, GameOption option, int value) {
    const long found = PlayerOptionsOffset(bytes);
    const Field field = PlayerField(option);
    if (found < 0 || field.at < 0 || !ValueAllowed(option, value)) return false;
    const size_t at = static_cast<size_t>(found + field.at);
    if (field.isByte) bytes[at] = value != 0 ? 1 : 0;
    else WriteInt(bytes, at, value);
    return true;
}

bool ReadGameDefaults(const fs::path& soundSam, GameConfig& config, PlayerOptions& options) {
    std::ifstream in(soundSam, std::ios::binary);
    if (!in) return false;
    int quality = 0, sfx = 0, music = 0, speech = 0, movie = 0, soundOn = 0;
    const struct {
        const char* key;
        int* value;
    } keys[] = {{"soundinfo.defaultquality", &quality}, {"soundinfo.soundon", &soundOn}, {"defaultvolume.sfx", &sfx},
                {"defaultvolume.music", &music},        {"defaultvolume.speech", &speech}, {"defaultvolume.movie", &movie}};
    std::string line;
    while (std::getline(in, line)) {
        size_t start = 0;
        while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) ++start;
        size_t end = start;
        while (end < line.size() && !std::isspace(static_cast<unsigned char>(line[end]))) ++end;
        std::string key = line.substr(start, end - start);
        if (key.empty() || key[0] == '#') continue;
        for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (const auto& known : keys)
            if (key == known.key) *known.value = std::atoi(line.c_str() + end);
    }
    // CPlayerSettings::Init on a machine of the top tier, which the port's Gestalt always answers (platform/sams_core.cpp).
    config = GameConfig{};
    config.soundQuality = (quality + 100) / 2;
    config.movie = {soundOn != 0, movie};
    options = PlayerOptions{};
    options.sfx = {soundOn != 0, sfx};
    options.music = {soundOn != 0, music};
    options.speech = {soundOn != 0, speech};
    options.movie = {soundOn != 0, movie};
    return true;
}

GameSettingsView ReadGameSettings(const fs::path& userFolder, const fs::path& soundSam, const std::string& playerName) {
    GameSettingsView view;
    view.userFolder = userFolder;
    view.running = GameIsRunning(userFolder);
    view.defaultsKnown = ReadGameDefaults(soundSam, view.config, view.options);
    std::vector<uint8_t> bytes;
    std::error_code ec;
    const fs::path config = ConfigFile(userFolder);
    if (fs::exists(config, ec)) {
        GameConfig read = view.config;
        if (ReadBytes(config, bytes) && ParseGameConfig(bytes, read)) {
            view.config = read;
            view.configState = GameFileState::Ok;
        } else {
            view.configState = GameFileState::Damaged;
        }
    }
    view.players = ListPlayers(userFolder);
    view.player = ChoosePlayer(view.players, playerName);
    if (view.player >= 0) {
        PlayerOptions read = view.options;
        if (ReadBytes(view.players[static_cast<size_t>(view.player)].file, bytes) && ParsePlayerOptions(bytes, read)) {
            view.options = read;
            view.playerState = GameFileState::Ok;
        } else {
            view.playerState = GameFileState::Damaged;
        }
    }
    return view;
}

bool WriteGameOption(const fs::path& userFolder, const fs::path& soundSam, const std::string& playerName, GameOption option,
                     int value, std::string& err) {
    if (option == GameOption::Videocard) {   // the port has one display device: the game turns any other choice back
        err = "the game has one videocard here";
        return false;
    }
    GameFileLock lock;
    if (!lock.Take(userFolder)) {
        err = "the game is running";
        return false;
    }
    const bool inConfig = OptionInConfig(option), inPlayer = OptionInPlayer(option);
    const std::vector<GamePlayer> players = ListPlayers(userFolder);
    const int player = ChoosePlayer(players, playerName);
    std::vector<uint8_t> configBytes, playerBytes;
    fs::path configFile, playerFile;
    if (inPlayer && player >= 0) {
        playerFile = players[static_cast<size_t>(player)].file;
        if (!ReadBytes(playerFile, playerBytes) || !SetPlayerOption(playerBytes, option, value)) {
            err = playerFile.filename().u8string() + " of " + players[static_cast<size_t>(player)].name + " cannot be read";
            return false;
        }
    } else if (!inConfig) {
        err = "no player yet";
        return false;
    }
    if (inConfig) {
        std::error_code ec;
        configFile = ConfigFile(userFolder);
        if (fs::exists(configFile, ec)) {
            if (!ReadBytes(configFile, configBytes)) {
                err = configFile.filename().u8string() + " cannot be read";
                return false;
            }
        } else {
            GameConfig defaults;
            PlayerOptions unused;
            if (!ReadGameDefaults(soundSam, defaults, unused)) {
                err = "this edition's game data is not in the data folder";
                return false;
            }
            configBytes = EncodeGameConfig(defaults);
            fs::create_directories(configFile.parent_path(), ec);
        }
        if (!SetGameConfigOption(configBytes, option, value)) {
            err = configFile.filename().u8string() + " cannot be read";
            return false;
        }
    }
    if (!playerFile.empty()) {
        // The newest gms.dat is the last player the game had: a change here does not make another player the last one.
        std::error_code ec;
        const fs::file_time_type written = fs::last_write_time(playerFile, ec);
        if (!ReplaceFile(playerFile, playerBytes, err)) return false;
        if (!ec) fs::last_write_time(playerFile, written, ec);
    }
    if (!configFile.empty() && !ReplaceFile(configFile, configBytes, err)) return false;
    return true;
}

#ifdef _WIN32

bool GameFileLock::Take(const fs::path& userFolder, int waitMs) {
    Release();
    std::error_code ec;
    fs::create_directories(userFolder, ec);
    const fs::path file = userFolder / kLockName;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs);
    for (;;) {
        // No sharing: a second opener is refused until the first handle closes, which the system does when a process ends.
        HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            handle_ = handle;
            held_ = true;
            return true;
        }
        if (GetLastError() != ERROR_SHARING_VIOLATION) return true;   // no lock can be had here: nothing to wait for
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void GameFileLock::Release() {
    if (handle_ != nullptr) CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
    held_ = false;
}

bool GameIsRunning(const fs::path& userFolder) {
    const fs::path file = userFolder / kLockName;
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        return false;
    }
    return GetLastError() == ERROR_SHARING_VIOLATION;
}

#else

bool GameFileLock::Take(const fs::path& userFolder, int waitMs) {
    Release();
    std::error_code ec;
    fs::create_directories(userFolder, ec);
    const fs::path file = userFolder / kLockName;
    const int fd = open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) return true;   // no lock can be had here: nothing to wait for
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs);
    for (;;) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
            fd_ = fd;
            held_ = true;
            return true;
        }
        if (errno != EWOULDBLOCK) {   // this file system has no locks
            close(fd);
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            close(fd);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void GameFileLock::Release() {
    if (fd_ >= 0) close(fd_);   // closing the one descriptor frees the lock
    fd_ = -1;
    held_ = false;
}

bool GameIsRunning(const fs::path& userFolder) {
    const fs::path file = userFolder / kLockName;
    const int fd = open(file.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) return false;
    const bool busy = flock(fd, LOCK_EX | LOCK_NB) != 0 && errno == EWOULDBLOCK;
    close(fd);   // a lock it took goes with it
    return busy;
}

#endif

}  // namespace tpw::ui
