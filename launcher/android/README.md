# Sim Theme Park and Theme Park World on Android

The APK requires Android 13 or newer and Vulkan. The ARM64 build is for phones and handhelds. The x86-64 build is for compatible Android emulators.

## First launch

Open **STP/TPW Launcher** and select **Import files** for disc images or archives of your own copy of the game, or **Import folder** for unpacked files. You can select several related files together and add language packs later. The launcher verifies the imported data before enabling **Play**. Imports need temporary space for a copy of the selected files.

The launcher lists Sim Theme Park before Theme Park World. Import the Gold bonus CD alongside either base game to unlock **Gold Edition rides**. The switch is on by default once the bonus data is verified and is saved separately for each game. The PlayStation and SimCoaster content mods require their own game data.

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

The launcher's **Game interface size** setting offers 100%, 115% and 125%. Larger sizes increase the panels, text and hit areas in a park. The map, pause menu and other full-screen panels fit at 100%. The camera overlay and shortcuts tuck away when a large park interface would overlap them.

Android starts at 1920 × 1080 by default. Change the rendering resolution in the game's **Options** menu. The game restores the saved resolution on the next launch.

Under **Settings → Graphics**, choose **Auto**, **30 FPS** or **60 FPS**. Auto follows the display refresh rate without an explicit frame rate cap. **Draw distance** controls how far the game renders while riding an attraction or walking with the camcorder. The menu also exposes the original game's graphics options, including texture quality, lighting and shadows. **Game default** follows the graphics detail level selected in the game. Changes apply the next time the game starts.

The Android launcher exposes smooth wheel zoom, extended zoom, varied sounds, stereo panning and ride badges. The preview starts in landscape with Vulkan, and intro films are disabled. Desktop windowing and renderer choices, the Mac camera-notch option, and the desktop return-to-launcher switch have no Android control. The launcher also leaves out the desktop free-camera and keyboard options. Android has its own controller and touch controls, interface-size choices and return to the launcher through Recents or the game's Quit action.

## Build

Install CMake, Ninja, Python 3, a JDK (17 or newer), Android NDK r28c (`28.2.13676358`), SDK Platform 36 and Build Tools 36.0.0. Initialize the repository's submodules. CMake fetches SDL 3.4.20 and checks the archive's SHA-256 on the first configure. No game data is needed to build or package.

On macOS or Linux, from the repository root with `ANDROID_HOME` set to the SDK:

```sh
cmake -S . -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_HOME/ndk/28.2.13676358/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-33 -DANDROID_STL=c++_static -DTPW_MOVIES=OFF
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
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-33 -DANDROID_STL=c++_static -DTPW_MOVIES=OFF
cmake --build build-android --target tpw_launcher
python launcher/android/package.py --build-dir build-android `
  --android-jar "$sdk/platforms/android-36/android.jar" --build-tools-dir "$sdk/build-tools/36.0.0" `
  --output-apk build-android/stp-tpw-arm64-debug.apk
```

The packager reads the ABI, SDL sources and strip tool from the selected CMake build.

## Install and test

For an ARM64 device, enable USB debugging and run `adb install -r build-android/stp-tpw-arm64-debug.apk`. Open **STP/TPW Launcher** and import your own game data with the file picker. On a debug build, read the game log with `adb shell run-as org.tpwport.android cat files/logs/launcher.log`.

### Android Emulator

Create an x86-64 Android Virtual Device with Android 13 or newer and Vulkan support. In the build commands above, set `-DANDROID_ABI=x86_64`, use `build-android-x86_64` as the build directory, and package to `build-android-x86_64/stp-tpw-x86_64-debug.apk`. Start the emulator, run `adb install -r build-android-x86_64/stp-tpw-x86_64-debug.apk`, then use the launcher to import your own files.

The emulator playtest used a Pixel 7 profile with the Android 15 (API 35) x86-64 system image and SwiftShader software Vulkan on Ubuntu 24.04 under WSL. The [Android Emulator graphics documentation](https://developer.android.com/studio/run/emulator-acceleration#accel-graphics) describes how to select a graphics backend.

The test imported the Gold bonus through Android's folder picker and confirmed that each game remembered its own Gold switch. Theme Park World loaded Lost Kingdom with 35 bonus rides enabled. The park map opened, and Dizzy Dinos could be bought and placed. Sim Theme Park launched with the bonus rides disabled when its switch was off.

## Packaging for distribution

Set `TPW_SIGNING_PASSWORD` and pass `--application-id`, `--version-code`, `--version-name`, `--release-keystore` and `--release-key-alias` to `package.py`. Updates use the same application ID and signing key with a higher version code. Debug packages use a local test key. The APK contains the port and its libraries, with third-party notices in `assets/licenses/`. Players import their own game files.

For direct distribution, provide the signed APK, its SHA-256 checksum and the device requirements. Test a new installation through the file picker and an update over the previous signed build.
