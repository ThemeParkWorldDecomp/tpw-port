# SDL3

- What: Simple DirectMedia Layer 3 (window, OpenGL context, input, audio), built static with the launcher.
- Version: 3.4.20 (3 November 2026), the newest stable release of the 3.4 line (tag release-3.4.20, commit
  f00dc919ca35b9756316e86c4777392de442f9b2). Taken for its trackpad pinch events (SDL_EVENT_PINCH_BEGIN, _UPDATE and
  _END, sent on macOS, Wayland and X11 with XInput 2.4); 3.2.30 had none.
- Origin: https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz
- Archive SHA-256: f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f (the digest GitHub publishes for the release asset; the SDL project publishes no checksum file for it).
- Signature: SDL3-3.4.20.tar.gz.sig from the same release holds two signatures, both good. One is made with the SDL
  project's key that verified 3.2.30 (fingerprint 1528 635D 8053 A57F 77D1 E086 30A5 9377 A776 3BE6, from
  https://www.libsdl.org/release/pubring.gpg). The other is made with Sam Lantinga's newer RSA key (fingerprint 0900 1043
  63B4 C9D4 223D E149 D913 FE7D 4B61 D39B, created 2026-01-15, from keyserver.ubuntu.com; libsdl.org's pubring.gpg does
  not hold it yet).
- Taken: 2026-11-14. Replaces 3.2.30 (taken 2026-10-21, archive SHA-256 4c3b09330d866dc52eb65b66259a6684ad387252ca8c57901b3a2b534eb42e3d).
- License: zlib license (LICENSE.txt, as published); src/hidapi/ has its own licenses (LICENSE*.txt there).
- How it was taken: the 3.2.30 import was first checked against its archive (every kept file identical, so no local change
  to carry). Then every path 3.2.30 had is kept or dropped as it was in that import, and a file new in 3.4 is kept when it
  sits in a kept folder and is not one of the dropped kinds below.
- Kept: CMakeLists.txt, cmake/ (the modules SDL's CMakeLists.txt includes, now also sdlcommands.cmake and
  PreseedNokiaNGageCache.cmake, which it includes on every system), include/, src/ and wayland-protocols/ for macOS,
  Linux and Windows (the Windows source folders as they were added for 3.2.30 on 2026-10-22, with what 3.4 adds to them),
  LICENSE.txt, README.md, CREDITS.md, REVISION.txt. Seven shader sources of src/render/gpu/shaders (*.hlsl) are in the
  archive but under SDL's own .gitignore there; they are added anyway so the folder is the archive's.
- Dropped: test/, examples/, docs/, Xcode/, VisualC*/, android-project/, build-scripts/, SDL_test (src/test/), the source folders of Android, iOS, tvOS, BSD, the worker model, Emscripten, PlayStation, Vita, 3DS, N-Gage, GDK, QNX, RISC OS, hidapi's libusb copy, tests, docs and build files (src/hidapi/windows keeps only its sources: no CMakeLists.txt, Makefile*, hidapi.rc, *.sln, *.vcproj or *.vcxproj), the archive's AGENTS.md and RULES.md, and the generator scripts (*.py, *.sh, *.pl, and the shader scripts compile_shaders*.bat). The Xbox parts that came with the Windows folders stay: src/video/directx/SDL_d3d12_xbox_cmacros.h is included by SDL_d3d12.h (for the Xbox targets only), and render/direct3d12's *_xbox* sources compile to nothing elsewhere.
- Local change: none to any file kept. src/misc/macos/SDL_sysurl.m holds CRLF line ends upstream and git stores it with LF.
- Built by the CMakeLists.txt of the launcher with SDL_STATIC ON, SDL_SHARED OFF, SDL_TEST_LIBRARY OFF, SDL_TESTS OFF, SDL_EXAMPLES OFF, SDL_INSTALL OFF, SDL_HIDAPI_LIBUSB OFF (no libusb of the host is looked for); target SDL3-static (alias SDL3::SDL3).
- Export: tpw-port only (the launcher is the port's).
