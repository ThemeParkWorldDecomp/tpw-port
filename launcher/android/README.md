# Sim Theme Park and Theme Park World on Android

The APK requires Android 13 or newer and Vulkan. The ARM64 build is for phones and handhelds. The x86-64 build is for compatible Android emulators.

## First launch

Open **STP/TPW Launcher** and select **Import files** for disc images or archives of your own copy of the game, or **Import folder** for unpacked files. You can select several related files together and add language packs or [your own translations](../../README.md#your-own-translation) later. The launcher verifies the imported data before enabling **Play**. Imports need temporary space for a copy of the selected files.

The launcher lists Sim Theme Park before Theme Park World. Import the Gold bonus CD alongside either base game to unlock **Gold Edition rides**. The switch is on by default once the bonus data is verified and is saved separately for each game. The PlayStation and SimCoaster content mods require their own game data. Import a supported PlayStation 2 disc to convert its 3D guest, staff and entertainer models. Enable **PS2 Guest Models (3D)** in **Mods** after the import and start the game.

The launcher and touch controls follow the system or app language in English and Polish. Game language is selected separately for each game from the installed language packs.

The launcher and game appear as separate cards in Android Recents. The game card uses the running edition's name and the imported game icon when the device displays it. The ticket is its fallback icon. **Play** returns to an already-running game. Quit that game before applying a new edition, language, Gold selection or interface size.

## Controls and settings

| Controller | Default action |
| --- | --- |
| Left stick | Move the cursor. A small tilt gives finer control. |
| Right stick | Pan the camera. |
| A / B | Left / right mouse button. Hold to drag. |
| D-pad | Arrow keys. In the park, left/right rotates and up/down zooms. |
| L1 / R1 | Zoom out / in. |
| L2 / R2 | Rotate the object being placed left / right. |
| Left / right stick click | Hold Ctrl / Shift. |
| X / Y | Backspace / Enter. |
| Start / Select | Game menu / show or hide the camera overlay. |

L2 and R2 can be analog axes or buttons. If a controller reports both for the same trigger, the game uses the axis to produce a single action.

Open the on-screen gear in the top-left corner of the player lobby or the top-right corner of the park. Choose **Mouse mode** to select **Direct touch** or **Touchpad**. A swipe moves the cursor and a separate tap clicks. In Direct touch, drag a slider by its thumb. In Touchpad, position the cursor over the thumb or track, lift your finger, then swipe again to change the value. A two-finger tap right-clicks. The shortcuts beneath zoom are right click (which cancels the current building tool) and Backspace. The build shortcuts include rotation, Buy Ride, the map, Ctrl and Shift. Ctrl keeps a placement blueprint active. Ctrl-click picks up the blueprint of an existing object.

**Move controls** lets you drag on-screen controls and save their positions with **Done**. The controller settings save stick sensitivity, button mappings and mouse mode. You can also bind an action to open the keyboard. If Android put the park on pause while the app was away, **Resume game** appears on return.

### Free camera

In a park, hold **Select** or tap **↗** in the camera panel to enter or leave free camera. A short Select press toggles the overlay. Bind **Free camera** or **Reset camera view** to another button under **Button mapping**.

| Action | Controller | Touch |
| --- | --- | --- |
| Fly around the park | Left stick | Left **Move** stick |
| Turn and tilt | Right stick | Drag in the right **Look** panel |
| Rise or descend | R2 or L2 | Hold **↑** or **↓** |
| Go back to the starting view | **Reset camera view** binding | **Reset** |

You can switch between touch and controller while flying. Both **Direct touch** and **Touchpad** let you use the cursor outside the camera controls. Exiting flight restores the pre-flight view. Held controller inputs resume their park actions after you release them.

### Display and graphics

The launcher's **Game interface size** setting offers 100%, 115% and 125%. Larger sizes increase the panels, text and hit areas in a park. The map, pause menu and other full-screen panels fit at 100%. The camera overlay and shortcuts tuck away when a large park interface would overlap them.

Android starts at 1920 × 1080 by default. Change the rendering resolution in the game's **Options** menu. The game restores the saved resolution on the next launch.

Under **Settings → Graphics**, choose **Auto**, **30 FPS** or **60 FPS**. Auto follows the display refresh rate without an explicit frame rate cap. **Draw distance** controls how far the game renders while riding an attraction or walking with the camcorder. The menu also exposes the original game's graphics options, including texture quality, lighting and shadows. **Game default** follows the graphics detail level selected in the game. Changes apply the next time the game starts.

The Android launcher exposes smooth wheel zoom, extended zoom, varied sounds, stereo panning and ride badges. The game starts in landscape with Vulkan. Builds with movie support play intro films from the imported game files. Use **Skip the intro films** in **Settings** to go straight to the player lobby. Desktop windowing and renderer choices, the Mac camera-notch option, and the desktop return-to-launcher switch have no Android control. The launcher also leaves out the desktop keyboard options. Android has its own controller and touch controls, interface-size choices and return to the launcher through Recents or the game's Quit action.

## Manage files and diagnostics

- **Installed editions** shows how many required files each edition has. **Check again** rehashes the imported files. **Remove game data** deletes the chosen edition's imported files while keeping its saves.
- **Asset packs** imports and removes [replacement asset ZIPs](../../README.md#asset-packs). Add the ZIP with **Import pack ZIP**. Its replacement files must be at their game paths under `data/`. A pack with the same filename replaces the installed copy. The game applies packs in filename order at the next start.
- **Export saves** stores the chosen game's profiles and parks in a ZIP with `save/` at its root. On desktop, ZIP the `save` folder from `saves/us` for Sim Theme Park or `saves/eur` for Theme Park World. **Import saves** restores that ZIP for the game you select, replacing its existing profiles and parks. Close the game before importing or exporting saves. A park that uses Gold or a content mod needs the same content enabled on the destination.
- **Diagnostics** shows the APK version, a preview of recent logs and third-party licenses. Export the scrubbed logs to a location you choose. The releases button opens GitHub in your browser.

## Online play

Open **Settings → Online server** to choose a host name or IP address. Leave it blank to use the default server. The new address applies the next time the game starts. Select the globe in the player lobby to connect. Starting the launcher, single-player play and saves work offline.

The original online protocol sends passwords without encryption. Use a password unique to this game.

## Build

Install CMake, Ninja, Python 3, a JDK (17 or newer), Android NDK r28c (`28.2.13676358`), SDK Platform 36 and Build Tools 36.0.0. Initialize the repository's submodules. CMake fetches SDL 3.4.20 and checks the archive's SHA-256 on the first configure. No game data is needed to build or package.

On macOS or Linux, from the repository root with `ANDROID_HOME` set to the SDK:

```sh
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_HOME/ndk/28.2.13676358/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-33 -DANDROID_STL=c++_static
cmake --build build-android --target tpw_launcher
python3 launcher/android/package.py --build-dir build-android \
  --android-jar "$ANDROID_HOME/platforms/android-36/android.jar" \
  --build-tools-dir "$ANDROID_HOME/build-tools/36.0.0" \
  --output-apk build-android/stp-tpw-arm64-debug.apk
```

On Windows, from PowerShell with `ANDROID_HOME` set:

```powershell
$sdk = $env:ANDROID_HOME
cmake -S . -B build-android -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$sdk/ndk/28.2.13676358/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-33 -DANDROID_STL=c++_static
cmake --build build-android --target tpw_launcher
python launcher/android/package.py --build-dir build-android `
  --android-jar "$sdk/platforms/android-36/android.jar" --build-tools-dir "$sdk/build-tools/36.0.0" `
  --output-apk build-android/stp-tpw-arm64-debug.apk
```

The packager reads the ABI, SDL sources and strip tool from the selected CMake build. The default `TPW_MOVIES=ON` builds the bundled [FFmpeg sources](../../third_party/ffmpeg/NOTE.md) as a separate `libtpw_ffmpeg.so`. Rebuild `tpw_ffmpeg` and repackage the APK with your own signing key to use a modified decoder. Pass `-DTPW_MOVIES=OFF` to build without films.

## Install and test

For an ARM64 device, enable USB debugging and run `adb install -r build-android/stp-tpw-arm64-debug.apk`. Open **STP/TPW Launcher** and import your own game data with the file picker. On a debug build, read the game log with `adb shell run-as org.tpwport.android cat files/logs/launcher.log`.

### Android Emulator

Create an x86-64 Android Virtual Device with Android 13 or newer and Vulkan support. In the build commands above, set `-DANDROID_ABI=x86_64`, use `build-android-x86_64` as the build directory, and package to `build-android-x86_64/stp-tpw-x86_64-debug.apk`. Start the emulator, run `adb install -r build-android-x86_64/stp-tpw-x86_64-debug.apk`, then use the launcher to import your own files.

The emulator playtest used a Pixel 7 profile with the Android 15 (API 35) x86-64 system image and SwiftShader software Vulkan on Ubuntu 24.04 under WSL. The [Android Emulator graphics documentation](https://developer.android.com/studio/run/emulator-acceleration#accel-graphics) describes how to select a graphics backend.

The test imported the Gold bonus through Android's folder picker and confirmed that each game remembered its own Gold switch. Theme Park World loaded Lost Kingdom with 35 bonus rides enabled. The park map opened, and Dizzy Dinos could be bought and placed. Sim Theme Park launched with the bonus rides disabled when its switch was off.

## Packaging for distribution

Set `TPW_SIGNING_PASSWORD` and pass `--application-id`, `--version-code`, `--version-name`, `--release-keystore` and `--release-key-alias` to `package.py`. Updates use the same application ID and signing key with a higher version code. Debug packages use a local test key. The APK contains the port and its libraries, with third-party notices in `assets/licenses/`. Players import their own game files.

For direct distribution, provide the signed APK, its SHA-256 checksum and the device requirements. Test a new installation through the file picker and an update over the previous signed build.
