# SDL3

- What: Simple DirectMedia Layer 3 (window, OpenGL context, input, audio), built static with the launcher.
- Version: 3.2.30 (1 January 2026), the newest stable release of the 3.2 line.
- Origin: https://github.com/libsdl-org/SDL/releases/download/release-3.2.30/SDL3-3.2.30.tar.gz
- Archive SHA-256: 4c3b09330d866dc52eb65b66259a6684ad387252ca8c57901b3a2b534eb42e3d (the digest GitHub publishes for the release asset; the SDL project publishes no checksum file for it).
- Signature: SDL3-3.2.30.tar.gz.sig from the same release verified with the SDL project's key (fingerprint 1528 635D 8053 A57F 77D1 E086 30A5 9377 A776 3BE6, from https://www.libsdl.org/release/pubring.gpg): good signature.
- Taken: 2026-10-21.
- License: zlib license (LICENSE.txt, as published); src/hidapi/ has its own licenses (LICENSE*.txt there).
- Kept: CMakeLists.txt, cmake/ (the modules SDL's CMakeLists.txt includes), include/, src/ and wayland-protocols/ for macOS and Linux, LICENSE.txt, README.md, CREDITS.md, REVISION.txt.
- Added for the Windows build (2026-10-22, from the same archive, its SHA-256 checked against the one above): the Windows source folders of src/ (core, video, audio's directsound and wasapi, render's direct3d, direct3d11 and direct3d12, joystick, haptic, hidapi, thread, timer, time, loadso, filesystem, io, dialog, locale, main, misc, power, process, sensor, tray) and the cmake/ modules SDL's CMakeLists.txt includes for Windows, without hidapi's tests. No kept file changed.
- Dropped: test/, examples/, docs/, Xcode/, VisualC*/, android-project/, build-scripts/, SDL_test (src/test/), the source folders of Android, iOS, tvOS, BSD, the worker model, Emscripten, PlayStation, Vita, 3DS, GDK, QNX, RISC OS, hidapi's libusb copy, tests, docs and build files, and the generator scripts (*.py, *.sh, *.pl).
- Local change: none to any file kept. Two files held CRLF line ends upstream (cmake/FindLibUSB.cmake, src/misc/macos/SDL_sysurl.m) and git stores them with LF.
- Built by the CMakeLists.txt of the launcher with SDL_STATIC ON, SDL_SHARED OFF, SDL_TEST_LIBRARY OFF, SDL_TESTS OFF, SDL_EXAMPLES OFF, SDL_INSTALL OFF, SDL_HIDAPI_LIBUSB OFF (no libusb of the host is looked for); target SDL3-static (alias SDL3::SDL3).
- Export: tpw-port only (the launcher is the port's).
