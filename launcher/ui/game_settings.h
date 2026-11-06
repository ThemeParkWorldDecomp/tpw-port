// game_settings.h: the game's own settings files as the launcher's Settings box reads and writes them. The game keeps the
// settings of its Options panel in two files of the edition's save folder (<user folder>/save): Config.tcf, one for all
// its players, and each player's gms.dat (users/<slot><name>/gms.dat), and these stay the one place they are kept: the
// launcher holds no copy. A write changes only the bytes of the one setting, on the bytes the file holds at that moment,
// and replaces the file in one step, so what the launcher does not edit stays as it was. A file it cannot read is left
// alone. No SDL and no game header here: launcher/game_size_rules.h gives it the game's sizes, launcher/main.cpp the
// folders, and the tests the rest.
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace tpw::ui {

// A volume as the game keeps it (CPlayerSettings::Volume): 0 to 100, with its own on and off.
struct GameVolume {
    bool on = false;
    int volume = 0;
};

// Config.tcf: 32 bytes, little-endian (CPlayerSettings::SerialiseGlobal after the version). Renderer 0 is Software
// rendering, 1 3D card rendering; detail 0 Low, 1 Medium, 2 High; videoCard 0 Primary, 1 Secondary.
struct GameConfig {
    int renderer = 1;
    int resolution = 2;
    int detail = 2;
    int videoCard = 0;
    GameVolume movie;
    int soundQuality = 0;
};
inline constexpr size_t kGameConfigSize = 32;

// What a player's gms.dat keeps of the Options panel (CPlayerSettings::SerialiseLocal), 39 bytes in the middle of it.
struct PlayerOptions {
    GameVolume sfx, music, speech, movie;
    bool advisor = true, tutorial = true, tooltips = true, confirmDelete = true;
    bool rmbScroll = false, rmbCancel = true, isometric = true;
};
inline constexpr size_t kPlayerOptionsSize = 39;

// A file's state: not there, read, or there and not readable (left alone: the game replaces it when it next saves).
enum class GameFileState { Missing, Ok, Damaged };

// Everything of the Options panel that is kept, by the name the game shows. A value is the number the file holds (0 or 1
// for an on and off), Rendering 0 Software rendering, Rotation 1 "90 degs" and 0 "Smooth", Scroll 1 "Right button" and 0
// "Pushscroll".
enum class GameOption {
    Resolution, GraphicsQuality, Rendering, Videocard, AudioQuality,
    SfxOn, SfxVolume, MusicOn, MusicVolume, SpeechOn, SpeechVolume, MovieOn, MovieVolume,
    Advisor, Tutorial, PopupHelp, Confirmations, RmbCancel, Rotation, Scroll,
    Count
};
// Whether Config.tcf keeps the option, and whether a player's gms.dat does. The movie volume is in both: the game's last
// saved value in Config.tcf until a player logs in, that player's from then on.
bool OptionInConfig(GameOption option);
bool OptionInPlayer(GameOption option);

// The bytes of the two files. The parsers look at the bytes only; `Set` changes only the option's own bytes (false when
// the file does not read, or does not keep the option). Rendering to Software also lowers Graphics quality as the Options
// panel's button does (options_panel::ButtonPressed, case 17).
bool ParseGameConfig(const std::vector<uint8_t>& bytes, GameConfig& out);
std::vector<uint8_t> EncodeGameConfig(const GameConfig& config);
bool SetGameConfigOption(std::vector<uint8_t>& bytes, GameOption option, int value);
// Where the 39 bytes start, by reading gms.dat's fields before and after them (CFSGlobalMasterSave::Serialise); -1 when
// the file is not one the game would read whole.
long PlayerOptionsOffset(const std::vector<uint8_t>& bytes);
bool ParsePlayerOptions(const std::vector<uint8_t>& bytes, PlayerOptions& out);
bool SetPlayerOption(std::vector<uint8_t>& bytes, GameOption option, int value);

// What the game starts with when Config.tcf is missing, from the edition's sound.sam (CPlayerSettings::Init). False when
// the file cannot be read (the edition's data is not in the data folder).
bool ReadGameDefaults(const std::filesystem::path& soundSam, GameConfig& config, PlayerOptions& options);

// The size of a game picture.
struct GameSize {
    int width = 0;
    int height = 0;
    bool operator==(const GameSize& other) const { return width == other.width && height == other.height; }
};
// The game's Screen resolution, by the game's own code (launcher/game_size_rules.h): the shape buttons, the size and shape a
// Config.tcf number starts the game at, the slider's stops at a shape, smallest first, with `opened` among them at its own
// shape as the panel's slider has the size it opened at, the stop nearest a size, and the number a size is saved as.
struct GameSizeRules {
    std::vector<std::string> shapes;
    std::function<GameSize(int setting, int& shape)> chosen;
    std::function<std::vector<GameSize>(int shape, GameSize opened)> stops;
    std::function<int(const std::vector<GameSize>& stops, GameSize size)> closest;
    std::function<int(GameSize size)> setting;
};

// Where an edition's files are, from launcher/main.cpp. The edition is 0 US, 1 European (and Gold).
struct GameSettingsSource {
    GameSizeRules sizes;
    std::function<std::filesystem::path(int edition)> userFolder;
    std::function<std::filesystem::path(int edition)> soundSam;
};

// A player of the edition: its slot (1 to 4), its name, its gms.dat and when that was last written.
struct GamePlayer {
    int slot = 0;
    std::string name;
    std::filesystem::path file;
    std::filesystem::file_time_type written;
};

// Everything the Settings box shows of an edition, read from the files as they are now.
struct GameSettingsView {
    std::filesystem::path userFolder;
    GameFileState configState = GameFileState::Missing;
    bool defaultsKnown = false;      // no Config.tcf, and sound.sam was read: a first write can make the file
    GameConfig config;               // the file's values, or the game's defaults
    std::vector<GamePlayer> players; // by slot
    int player = -1;                 // the index of the player shown; -1 none
    GameFileState playerState = GameFileState::Missing;
    PlayerOptions options;           // that player's values, or the game's defaults
    bool running = false;            // the game has this folder (GameIsRunning)
};

// The files of `userFolder` now. `playerName` empty, or no longer a player: the player whose gms.dat was written last,
// the last one played.
GameSettingsView ReadGameSettings(const std::filesystem::path& userFolder, const std::filesystem::path& soundSam,
                                  const std::string& playerName);
// Changes one option in the files, reading them again first. Under GameFileLock: false and `err` when the game is running.
// An option of Config.tcf with no Config.tcf yet makes the file from the game's defaults. The movie volume changes both
// files, and neither when either cannot be written. A file that does not read is left alone: false and `err`.
bool WriteGameOption(const std::filesystem::path& userFolder, const std::filesystem::path& soundSam,
                     const std::string& playerName, GameOption option, int value, std::string& err);

// Whether the game is running on a user folder: it holds this lock for as long as it plays, and the launcher takes it for
// each write, so neither writes a settings file while the other can. The system frees it when the holder ends, however it
// ends; the file stays. Where the system has no such locks it can always be taken (and the game plays anyway).
class GameFileLock {
public:
    GameFileLock() = default;
    ~GameFileLock() { Release(); }
    GameFileLock(const GameFileLock&) = delete;
    GameFileLock& operator=(const GameFileLock&) = delete;
    // Tries for `waitMs` milliseconds (a launcher that has just started the game may be finishing a write).
    bool Take(const std::filesystem::path& userFolder, int waitMs = 0);
    void Release();
    bool Held() const { return held_; }

private:
    bool held_ = false;
#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};
bool GameIsRunning(const std::filesystem::path& userFolder);

}  // namespace tpw::ui
