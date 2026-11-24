#include <SDL3/SDL.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "android/interface_scale.h"

int TpwLauncherMain(int argc, char** argv);

extern "C" SDL_DECLSPEC int SDLCALL SDL_main(int argc, char** argv) {
    // SDL places the library name before the Activity arguments.
    const char* edition = argc > 1 ? argv[1] : "eur-pc";
    const char* language = argc > 2 ? argv[2] : "";
    const char* storageError = argc > 3 ? argv[3] : "App storage is unavailable.";
    const char* startupError = argc > 4 ? argv[4] : "Could not start the game. Check or reimport your game data.";
    const char* storage = SDL_GetAndroidInternalStoragePath();
    if (storage == nullptr) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Theme Park World", storageError, nullptr);
        return 1;
    }

    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");   // the activity routes direct and cursor-mode touches
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    if (argc > 5) tpw::mobile::SetInterfaceScale(std::atoi(argv[5]));
    std::vector<std::string> args = {"Theme Park World", "--data", storage, "--play", edition, "--no-preload"};
    if (*language != '\0') {
        args.emplace_back("--language");
        args.emplace_back(language);
    }

    std::vector<char*> launchArgs;
    for (auto& arg : args) launchArgs.push_back(arg.data());
    launchArgs.push_back(nullptr);
    const int code = TpwLauncherMain(static_cast<int>(args.size()), launchArgs.data());
    if (code != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Theme Park World", startupError, nullptr);
    }
    return code;
}
