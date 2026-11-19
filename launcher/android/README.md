# Android prototype

The APK targets Android 13 or newer, ARM64 and Vulkan. The Android launcher checks imported editions and languages before starting the game. Intro films are disabled.

W launcherze wybierz **Ustawienia → Rozmiar interfejsu gry** i ustaw **100%**, **115%** lub **125%**. Wybór jest zapamiętywany i działa od kolejnego uruchomienia gry. Większy rozmiar powiększa panele, tekst i obszary dotyku w parku. Duże panele dopasowują się do miejsca, a ekrany pełnoekranowe, takie jak mapa i opcje gry, korzystają z rozmiaru 100%. Przy 115% i 125% panel kamery i skróty dotykowe chowają się na czas otwarcia menu lub panelu gry.

## Sterowanie

| AYN Thor | Działanie |
| --- | --- |
| Lewy analog | Przesuwa kursor. Lekkie wychylenie ułatwia precyzyjne celowanie. |
| Prawy analog | Przesuwa kamerę w parku. |
| A / B | Lewy / prawy przycisk myszy. Przytrzymanie pozwala przeciągać. |
| Krzyżak | Strzałki gry. W parku lewo/prawo obraca widok, góra/dół zmienia zoom. |
| L1 / R1 | Oddala / przybliża widok. |
| L2 / R2 | Obraca budowany obiekt w lewo / prawo. |
| Wciśnięcie lewego / prawego analogu | Przytrzymuje Ctrl / Shift. |
| X / Y | Backspace / Enter. |
| Start / Select | Menu gry / pokaż lub ukryj panel kamery. |

L2 i R2 korzystają z tego samego mapowania, gdy kontroler zgłasza je jako osie analogowe albo przyciski. Jeśli Android udostępnia oś dla danego spustu, gra używa jej także wtedy, gdy kontroler zgłasza dodatkowe naciśnięcie przycisku na końcu ruchu.

**⚙ → Tryb myszy** przełącza między bezpośrednim celowaniem (**Dotyk**) a przesunięciem kursora względem jego aktualnej pozycji (**Touchpad**). Przesunięcie palca celuje, a osobne stuknięcie klika. W trybie **Dotyk** możesz od razu przeciągnąć suwak za uchwyt. W trybie **Touchpad** ustaw kursor na uchwycie lub torze suwaka, unieś palec i przesuń go ponownie, aby zmienić wartość. Stuknięcie dwoma palcami działa jak PPM. Pod przyciskami zoomu są **PPM**, który anuluje aktywne narzędzie budowy, oraz **⌫**, który wysyła Backspace. Akcję otwierania klawiatury można przypisać w mapowaniu pada.

W **⚙ → Przesuń kontrolki** przeciągnij wybrane przyciski lub drążek i naciśnij **Gotowe**. Położenia, czułości obu analogów, tryb myszy i mapowanie gry są zapisywane. W **⚙** można też otworzyć skróty budowy obejmujące obrót projektu, zakup atrakcji, mapę oraz Ctrl i Shift. Ctrl przy stawianiu zostawia projekt do ponownego użycia, a Ctrl i LPM pobierają projekt istniejącego obiektu.

Jeśli przejście w tło zatrzymało park, po powrocie pojawi się przycisk **Wznów grę**.

## Pierwsze uruchomienie

Uruchom **STP/TPW Launcher** z listy aplikacji. Ikona przedstawia kolejkę górską. W granatowo-zielonym launcherze żółty przycisk **Graj** znajduje się pod wyborem wydania i języka. Na szerokim ekranie wybór gry i import danych są obok siebie. Na wąskim ekranie wybór gry jest nad importem.

W widoku **Ostatnie aplikacje** launcher i gra mają osobne karty. Karta gry ma nazwę uruchomionego wydania. Aplikacja przekazuje systemowi ikonę z zaimportowanych plików. Ikona biletu służy jako zapasowa, również na urządzeniach, które wyświetlają tylko ikonę z pakietu. **Graj** wraca do otwartej gry. Aby rozpocząć z innym wydaniem lub zmienionymi ustawieniami startowymi, najpierw zakończ bieżącą grę z jej menu.

W programie startowym wybierz **Importuj pliki** i wskaż obrazy płyt lub archiwa własnej kopii gry. Do danych z rozpakowanego folderu służy **Importuj folder**. Ekran startowy udostępnia wybór wydania, język, ustawienia i mody. Dane dodatkowego języka można zaimportować później. Mody korzystające z płyt PlayStation albo SimCoaster odblokowują się po rozpoznaniu odpowiednich danych.

Launcher i ustawienia sterowania mają polski i angielski interfejs. Android używa języka systemu lub języka ustawionego dla aplikacji. Lista języków samej gry jest niezależna i korzysta z tej samej weryfikacji importowanych pakietów co launcher PC. Gra zapamiętuje wybór języka osobno dla wydania amerykańskiego i europejskiego.

## Build on Windows

Use CMake, Ninja, Python 3, a JDK, Android NDK r28c, SDK Platform 36, Build Tools 36.0.0 and the full SDL 3.4.20 source tree. Set `$ndk`, `$sdl`, `$androidJar` and `$buildTools` to their local paths. From the repository root, with the submodules initialized, run the following commands.

```powershell
cmake -S . -B build-android -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-33 -DANDROID_STL=c++_static `
  "-DTPW_ANDROID_SDL_DIR=$sdl" -DTPW_MOVIES=OFF
cmake --build build-android --target tpw_launcher
.\launcher\android\package.ps1 -BuildDir build-android -SdlDir $sdl -AndroidJar $androidJar `
  -BuildToolsDir $buildTools -OutputApk .\build-android\Theme-Park-World-android-arm64-debug.apk
```

## Install for testing over ADB

Import your game data in the desktop launcher first. Set `$data` to that launcher's data folder and `$apk` to the APK path. Connect one Android device with USB debugging enabled. The commands below install the APK and copy the European PC edition and its Polish language files to the app's private storage.

```powershell
adb install -r $apk
adb shell am force-stop org.tpwport.android
adb shell run-as org.tpwport.android mkdir -p files
$archive = Join-Path $env:TEMP ('tpw-data-' + [guid]::NewGuid().ToString('N') + '.tar')
$remote = '/data/local/tmp/' + (Split-Path -Leaf $archive)
tar -cf $archive -C $data assets/eur-pc assets/lang/Polish
adb push $archive $remote
adb shell "set -o pipefail && cat $remote | run-as org.tpwport.android tar -xmo -f - -C files"
adb shell rm $remote
Remove-Item -LiteralPath $archive
adb shell am start -n org.tpwport.android/.SetupActivity
```

To test in English without a Polish import, omit `assets/lang/Polish` from the `tar` command. To read the game log, run `adb shell run-as org.tpwport.android cat files/logs/launcher.log`.

## Packaging for distribution

Confirm the redistribution terms for the port, decompilation and bundled mods before publishing. The APK contains the port and its libraries. Third-party notices are under `assets/licenses/` in the APK.

Use a dedicated application ID and a release keystore. Pass `-ApplicationId`, `-VersionCode`, `-VersionName`, `-ReleaseKeyStore` and `-ReleaseKeyAlias` to `package.ps1`. Set `TPW_SIGNING_PASSWORD` for the signing process. Each update uses the same application ID and signing key, with a higher version code. The debug build uses a local test key.

For direct APK distribution, publish the signed APK with its SHA-256 checksum and the device requirements (Android 13 or newer, ARM64, Vulkan). Players install the APK, open it, and choose **Import files** for their own disc images or archives or **Import folder** for an unpacked game folder. They can select multiple related files together, and import additional language files later. Android copies the selection to temporary storage before verifying and importing it, so some extra free space is needed during import. The Play button becomes available when a base edition is complete.

Check first run with an empty installation and an update over the previous signed build. Use normal file picking for the first run to cover how players add their data on the device.
