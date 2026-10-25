# tpw-port: the playable port. The translation is the tpw-decomp submodule, built as it is except for the files under
# overrides/, which take the place of the submodule's file of the same path (the port's fixes, listed in FIXES.md).
cmake_minimum_required(VERSION 3.20)
# The oldest macOS the launcher runs on: 11, the first with Apple Silicon. Without it a build runs only on the macOS it
# was built on and newer, so a release built on GitHub's macOS 15 would refuse every older Mac. Set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "The oldest macOS the launcher runs on")
project(tpw_port LANGUAGES C CXX)   # C for the vendored libraries (third_party/)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter -ffp-contract=off -fwrapv)
add_compile_definitions(TPW_PORT_FIX)
# The WIN32 parts of this file: Windows port contributed by samueleagle91 (tpw-port PR #1).
if(WIN32)
  # The C++ runtime (libc++, libunwind) and winpthreads are linked into each program: a player's machine has none of the
  # toolchain's DLLs. Windows' own C runtime (the UCRT) is part of the system.
  add_link_options(-static)
endif()

set(TPW_DECOMP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tpw-decomp)
if(NOT EXISTS ${TPW_DECOMP_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "tpw-decomp is missing: git submodule update --init")
endif()
# The game's data tables come from your own copy of the game (see tpw-decomp's README). The launcher reads them at run
# time from the game folder's "SimTheme Park" application (or the one its --program names), so the build holds none of
# them. Only a build made with -DTPW_TABLES_AT_CONFIGURE=ON compiles them in at configure time, from TPW_GAME_PROGRAM;
# its default is the CD's own SimTheme Park application, as the import writes it next to this repository. Any full Mac
# build will do (the CD's 2.0.1, the 2.04b3 patch's, the 2.1 releases).
if(NOT TPW_GAME_PROGRAM AND NOT DEFINED ENV{TPW_GAME_PROGRAM})
  set(TPW_GAME_PROGRAM "${CMAKE_CURRENT_SOURCE_DIR}/../extracted/disc-hfs/Theme Park Data/SimTheme Park"
      CACHE FILEPATH "A full Mac SimTheme Park application (its data fork) the data tables are read from")
endif()
set(TPW_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# The headers: the submodule's include/ and platform/ headers, with overrides/include and overrides/platform laid over them,
# copied into the build folder at configure time (TPW_OVERLAY_DIR). Every target reads its headers from the copy and never
# from the submodule (the tail of this file rewrites the submodule's include folders in every target), so an overridden
# header is the override however it is included: a quoted include looks beside the including header first, and #pragma
# once is per file, so the submodule's own copy of a header must never be reachable.
set(TPW_OVERLAY_DIR ${CMAKE_CURRENT_BINARY_DIR}/tpw-overlay)
set(TPW_INCLUDE_DIR ${TPW_OVERLAY_DIR}/include)
file(GLOB_RECURSE TPW_DECOMP_HEADERS CONFIGURE_DEPENDS RELATIVE ${TPW_DECOMP_DIR}
     ${TPW_DECOMP_DIR}/include/* ${TPW_DECOMP_DIR}/platform/*.h)
file(GLOB_RECURSE TPW_OVERRIDE_HEADERS CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/overrides
     ${CMAKE_CURRENT_SOURCE_DIR}/overrides/include/* ${CMAKE_CURRENT_SOURCE_DIR}/overrides/platform/*.h)
foreach(header ${TPW_OVERRIDE_HEADERS})
  if(NOT header IN_LIST TPW_DECOMP_HEADERS)
    message(FATAL_ERROR "overrides/${header} has no file in tpw-decomp to replace")
  endif()
endforeach()
foreach(header ${TPW_DECOMP_HEADERS})
  if(header IN_LIST TPW_OVERRIDE_HEADERS)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/overrides/${header} ${TPW_OVERLAY_DIR}/${header} COPYONLY)
  else()
    configure_file(${TPW_DECOMP_DIR}/${header} ${TPW_OVERLAY_DIR}/${header} COPYONLY)
  endif()
endforeach()
file(GLOB_RECURSE TPW_OVERLAY_HEADERS RELATIVE ${TPW_OVERLAY_DIR} ${TPW_OVERLAY_DIR}/*)
foreach(header ${TPW_OVERLAY_HEADERS})
  if(NOT header IN_LIST TPW_DECOMP_HEADERS)
    file(REMOVE ${TPW_OVERLAY_DIR}/${header})   # gone from the submodule since the last configure
  endif()
endforeach()

add_subdirectory(${TPW_DECOMP_DIR} tpw-decomp)

if(WIN32)
  # The Windows build of the game library: tpw-decomp as it is, with what Windows lacks provided here. The POSIX headers
  # MinGW has no copy of (dlfcn.h, sys/statvfs.h, pthread/qos.h) are in platform/win, over platform/posix_win.cpp.
  target_include_directories(tpw PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/platform/win)
  # tpw-decomp builds src/sound/CSprocketsModule.cpp without the methods platform/sound_module.cpp defines, with its
  # slicer (tpw_slice in its CMakeLists.txt), which runs on libclang; devtools/slice_win.py makes the copy from the same
  # list, without libclang. The destructor goes too: it is the class's key function, so the file emits the vtable, which
  # its Mach-O directive makes weak and COFF cannot; the vtable is then platform/sound_module.cpp's.
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  get_property(TPW_WIN_SLICE_DROP GLOBAL PROPERTY TPW_SLICE_tpw_LIST)
  list(APPEND TPW_WIN_SLICE_DROP __ZN16CSprocketsModuleD1Ev)   # CSprocketsModule::~CSprocketsModule()
  get_target_property(TPW_WIN_SOURCES tpw SOURCES)
  set(TPW_WIN_NEW_SOURCES)
  set(TPW_WIN_COPIES)
  foreach(source ${TPW_WIN_SOURCES})
    if(source MATCHES "/slice/tpw/src/(.+)$")
      set(rel ${CMAKE_MATCH_1})
      set(copy ${CMAKE_CURRENT_BINARY_DIR}/slice-win/src/${rel})
      add_custom_command(OUTPUT ${copy}
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/devtools/slice_win.py
                --src ${TPW_DECOMP_DIR}/src/${rel} --out ${copy} --drop ${TPW_WIN_SLICE_DROP}
        DEPENDS ${TPW_DECOMP_DIR}/src/${rel} ${CMAKE_CURRENT_SOURCE_DIR}/devtools/slice_win.py
        COMMENT "Slicing src/${rel} for tpw (Windows)"
        VERBATIM)
      # Compiled as tpw-decomp's copy would be (its quoted includes are found beside the original).
      get_source_file_property(options ${source} TARGET_DIRECTORY tpw COMPILE_OPTIONS)
      set_source_files_properties(${copy} TARGET_DIRECTORY tpw PROPERTIES COMPILE_OPTIONS "${options}" GENERATED ON)
      list(APPEND TPW_WIN_COPIES ${copy})
      set(source ${copy})
    endif()
    list(APPEND TPW_WIN_NEW_SOURCES ${source})
  endforeach()
  set_property(TARGET tpw PROPERTY SOURCES ${TPW_WIN_NEW_SOURCES})
  # The commands run in this folder, and tpw is tpw-decomp's: it waits for them through a target of this folder.
  add_custom_target(tpw_slice_win DEPENDS ${TPW_WIN_COPIES})
  add_dependencies(tpw tpw_slice_win)
endif()

# The native launcher (Milestone 1 unit 9): the translated game code, the platform layer
# (platform/: the Elegant Chaos shims and Mac Toolbox calls on SDL3 and OpenGL) and
# generated stand-ins for everything that is not translated or implemented yet
# (launcher/gen_stubs.py, rerun by every build, so a stand-in disappears when its unit
# lands). Skipped when OpenGL or Python 3 is missing: the library and the tests do not need it.
option(TPW_BUILD_LAUNCHER "Build the native launcher (needs OpenGL and Python 3)" ON)
# The translation's tree: this one here; tpw-port sets it to its tpw-decomp submodule.
if(NOT TPW_DECOMP_DIR)
  set(TPW_DECOMP_DIR ${CMAKE_CURRENT_SOURCE_DIR})
endif()
# Its headers: include/ of that tree; tpw-port sets it to a copy of the submodule's include/ with its overrides laid over it.
if(NOT TPW_INCLUDE_DIR)
  set(TPW_INCLUDE_DIR ${TPW_DECOMP_DIR}/include)
endif()
if(TPW_BUILD_LAUNCHER)
  find_package(OpenGL QUIET)
  find_package(Python3 COMPONENTS Interpreter QUIET)
endif()
if(TPW_BUILD_LAUNCHER AND OpenGL_FOUND AND Python3_FOUND)
  # SDL3 is built from source here (third_party/SDL3, static), never taken from the host: only the system's own frameworks
  # and libraries are linked, and no copy of SDL, libusb or the like that the host has is looked for.
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_UNINSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_HIDAPI_LIBUSB OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/SDL3 SDL3 EXCLUDE_FROM_ALL)
  target_compile_options(SDL3-static PRIVATE -w)   # SDL's C is not ours to warn about
  file(GLOB TPW_PLATFORM_SOURCES CONFIGURE_DEPENDS platform/*.cpp ${TPW_DECOMP_DIR}/platform/*.cpp)
  list(REMOVE_DUPLICATES TPW_PLATFORM_SOURCES)
  add_library(tpw_platform STATIC ${TPW_PLATFORM_SOURCES})
  target_include_directories(tpw_platform PUBLIC ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
  target_link_libraries(tpw_platform PUBLIC SDL3::SDL3 OpenGL::GL)
  target_compile_definitions(tpw_platform PRIVATE GL_SILENCE_DEPRECATION)
  # The IJG's libjpeg 6b (third_party/libjpeg-6b, see its NOTE.md): the game's JPEG calls (platform/jpeg_bridge.cpp) run on it. Its
  # headers are only the platform layer's own; the unit test libraries never compile it (they fake the calls).
  enable_language(C)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/libjpeg-6b ${CMAKE_CURRENT_BINARY_DIR}/third_party/libjpeg-6b)
  target_link_libraries(tpw_platform PRIVATE tpw_libjpeg)
  if(TPW_TABLES_AT_CONFIGURE)
    target_compile_definitions(tpw_platform PRIVATE TPW_TABLES_AT_CONFIGURE=1)   # LoadGameTables reads nothing
  endif()
  if(APPLE)
    # platform/gdi.cpp draws the signs' text with CoreText into a CoreGraphics bitmap.
    target_link_libraries(tpw_platform PUBLIC "-framework CoreText" "-framework CoreGraphics" "-framework CoreFoundation" objc)
  elseif(WIN32)
    # The WIN32 parts of this file: Windows port contributed by samueleagle91 (tpw-port PR #1).
    # The signs' text is drawn with stb_truetype (third_party/stb, platform/gdi_text_win.cpp) in place of CoreText
    # (platform/gdi_text.cpp). The POSIX calls MinGW lacks come from platform/win: its headers, and posix_win.h ahead of
    # every file (quoted inside SHELL:, which splits at spaces, so a clone in a folder with a space builds). The sockets
    # are Windows' (platform/winsock.cpp), and gluBuild2DMipmaps, in the Mac's OpenGL framework, is glu32's.
    get_target_property(platform_sources tpw_platform SOURCES)
    list(REMOVE_ITEM platform_sources ${CMAKE_CURRENT_SOURCE_DIR}/platform/gdi_text.cpp)
    set_property(TARGET tpw_platform PROPERTY SOURCES ${platform_sources})
    target_include_directories(tpw_platform PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
    target_include_directories(tpw_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/platform/win)
    target_compile_options(tpw_platform PRIVATE "SHELL:-include \"${CMAKE_CURRENT_SOURCE_DIR}/platform/win/posix_win.h\"")
    target_link_libraries(tpw_platform PUBLIC ws2_32 glu32)
  endif()

  # The game's films (QuickTime, Sorenson video) play through FFmpeg (platform/quicktime.cpp), built from source here
  # (third_party/ffmpeg: a trimmed FFmpeg, LGPL 2.1 or later, static); no copy on the host is ever looked for. With
  # TPW_MOVIES off the game skips its films.
  option(TPW_MOVIES "Play the game's films (FFmpeg, built from third_party/ffmpeg)" ON)
  set(TPW_MOVIE_SYSLIBS)
  if(TPW_MOVIES)
    enable_language(C)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg ffmpeg)
    target_compile_definitions(tpw_platform PRIVATE TPW_HAVE_FFMPEG=1)
    target_link_libraries(tpw_platform PUBLIC tpw_ffmpeg)
    # The generator must not stand in for FFmpeg's functions: the static library defines them.
    set(TPW_MOVIE_SYSLIBS --provides $<TARGET_FILE:tpw_ffmpeg>)
    message(STATUS "tpw: films play (FFmpeg 9.0.2, third_party/ffmpeg)")
  endif()

  add_library(tpw_launcher_objects OBJECT launcher/main.cpp launcher/asset_packs.cpp launcher/pending_manual.cpp
              launcher/static_init.cpp launcher/startup.cpp)
  target_include_directories(tpw_launcher_objects PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform launcher)
  # main.cpp opens the launcher's own window (tpw_launcher_ui, below) when no --game is given.
  target_link_libraries(tpw_launcher_objects PRIVATE SDL3::SDL3 tpw_launcher_ui)
  if(WIN32)
    target_include_directories(tpw_launcher_objects PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/platform/win)   # dlfcn.h
  endif()
  # The build the log file's first line and --version name: `git describe` of this tree when configured, "unknown" when
  # there is no git or no repository (a source archive). Only ever a hint for a bug report; never fails the build.
  set(TPW_LAUNCHER_BUILD unknown)
  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --dirty WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE tpw_describe OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE tpw_describe_result ERROR_QUIET)
    if(tpw_describe_result EQUAL 0 AND tpw_describe MATCHES "^[A-Za-z0-9._+-]+$")
      set(TPW_LAUNCHER_BUILD ${tpw_describe})
    endif()
  endif()
  target_compile_definitions(tpw_launcher_objects PRIVATE TPW_LAUNCHER_BUILD="${TPW_LAUNCHER_BUILD}")

  # SDL3 and zlib's inflate (src/bullfrog/LbZLib.cpp) come from the static libraries built here (--provides), so the
  # generator must not stand in for them; nor for the launcher window's library and the importer under it.
  if(APPLE)
    set(TPW_STUB_SYSLIBS --syslib /System/Library/Frameworks/OpenGL.framework/OpenGL)
  elseif(WIN32)
    # MinGW's C runtime is partly static (libmingw32, libmingwex) and its DLLs are linked through import libraries, so
    # the generator reads those as it reads the other static libraries. The toolchain's demangler is llvm-cxxfilt.
    get_filename_component(nm_dir ${CMAKE_NM} DIRECTORY)
    find_program(TPW_CXXFILT NAMES c++filt llvm-cxxfilt HINTS ${nm_dir} REQUIRED)
    set(TPW_STUB_SYSLIBS --cxxfilt ${TPW_CXXFILT})
    foreach(lib mingw32 mingwex ucrt kernel32 user32 gdi32 opengl32 glu32 advapi32 shell32 ole32 oleaut32 imm32 winmm
                version setupapi ws2_32 c++ unwind)
      find_library(TPW_WIN_LIB_${lib} NAMES ${lib} HINTS ${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES})
      if(TPW_WIN_LIB_${lib})
        list(APPEND TPW_STUB_SYSLIBS --provides ${TPW_WIN_LIB_${lib}})
      endif()
    endforeach()
  else()
    set(TPW_STUB_SYSLIBS --syslib libGL.so.1)
  endif()
  set(TPW_STUBS ${CMAKE_CURRENT_BINARY_DIR}/stubs_generated.cpp)
  add_custom_command(
    OUTPUT ${TPW_STUBS}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/launcher/gen_stubs.py
            --out ${TPW_STUBS} --include-dir ${TPW_INCLUDE_DIR}
            --nm ${CMAKE_NM} --provides $<TARGET_FILE:SDL3-static> --provides $<TARGET_FILE:tpw_zlib> --provides $<TARGET_FILE:tpw_libjpeg>
            --provides $<TARGET_FILE:tpw_launcher_ui> --provides $<TARGET_FILE:tpw_assets> ${TPW_STUB_SYSLIBS} ${TPW_MOVIE_SYSLIBS}
            $<TARGET_FILE:tpw> $<TARGET_FILE:tpw_platform> $<TARGET_OBJECTS:tpw_launcher_objects>
    DEPENDS tpw tpw_platform SDL3-static tpw_zlib tpw_libjpeg tpw_launcher_ui tpw_assets $<TARGET_OBJECTS:tpw_launcher_objects>
            launcher/gen_stubs.py
    COMMAND_EXPAND_LISTS VERBATIM
    COMMENT "Generating stand-ins for untranslated and unimplemented functions")
  add_executable(tpw_launcher $<TARGET_OBJECTS:tpw_launcher_objects> ${TPW_STUBS})
  target_include_directories(tpw_launcher PRIVATE ${TPW_INCLUDE_DIR} launcher)
  target_link_libraries(tpw_launcher PRIVATE tpw tpw_platform tpw_launcher_ui)
  if(WIN32)
    # A Windows app, not a console one: started from Explorer it opens no console window, and its log goes to
    # launcher.log beside launcher.ini (launcher/main.cpp). Its symbols are exported, so that the game's
    # dlsym(RTLD_DEFAULT, ...) of its own functions finds them (platform/win/dlfcn.h).
    set_target_properties(tpw_launcher PROPERTIES WIN32_EXECUTABLE ON)
    target_link_options(tpw_launcher PRIVATE -Wl,--export-all-symbols)
  endif()

  # What can be checked of the platform layer without a display (path mapping, key
  # tables, string helpers): ctest runs it.
  enable_testing()
  add_executable(tpw_platform_tests platform/tests/platform_tests.cpp)
  # TestSound drives the real CSprocketsModule (platform/sound_module.cpp) over the real TbSoundSystemModule base; the pipe
  # channel it pulls from is the test program's own fake, so the game library is not linked.
  # The player's mail files (unit v5L48): the real SaveStream and outbox stream glue, on the user folder's paths. The
  # tests build only those functions of their src files (platform/tests/platform_tests_keep.cmake).
  include(platform/tests/platform_tests_keep.cmake)
  list(TRANSFORM TPW_PLATFORM_TESTS_SOURCES PREPEND ${TPW_DECOMP_DIR}/src/)
  target_sources(tpw_platform_tests PRIVATE ${TPW_PLATFORM_TESTS_SOURCES})
  target_include_directories(tpw_platform_tests PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
  tpw_slice(TARGET tpw_platform_tests NAME platform_tests KEEP ${TPW_PLATFORM_TESTS_KEEP})
  target_link_libraries(tpw_platform_tests PRIVATE tpw_platform)
  add_test(NAME platform COMMAND tpw_platform_tests)
  if(WIN32)
    # Its src files are cut by tpw-decomp's slicer, which runs on libclang, and llvm-mingw ships none: on Windows the
    # tests are made when TPW_LIBCLANG names one (an LLVM release's bin/libclang.dll), and the slicer runs through
    # devtools/slicer_win.py, by way of a script written here that gives it that libclang. The script carries the
    # wrapper's hash, so a change to the wrapper cuts the copies again.
    set(TPW_LIBCLANG "$ENV{TPW_LIBCLANG}" CACHE FILEPATH "libclang.dll, for the platform tests' slicer on Windows")
    if(EXISTS "${TPW_LIBCLANG}")
      set(wrapper ${CMAKE_CURRENT_SOURCE_DIR}/devtools/slicer_win.py)
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${wrapper})
      file(SHA1 ${wrapper} wrapper_sha1)
      set(runner ${CMAKE_CURRENT_BINARY_DIR}/slicer_win_run.py)
      file(WRITE ${runner}.new "# Written by CMakeLists.txt: devtools/slicer_win.py (${wrapper_sha1}) with this build's libclang.\n"
                               "import os, runpy\n"
                               "os.environ[\"TPW_LIBCLANG\"] = r\"${TPW_LIBCLANG}\"\n"
                               "runpy.run_path(r\"${wrapper}\", run_name=\"__main__\")\n")
      configure_file(${runner}.new ${runner} COPYONLY)
      set_property(GLOBAL PROPERTY TPW_SLICER ${runner})
    else()
      set_target_properties(tpw_platform_tests PROPERTIES EXCLUDE_FROM_ALL ON)
      set_tests_properties(platform PROPERTIES DISABLED ON)
      message(STATUS "tpw: platform tests not made (TPW_LIBCLANG names no libclang.dll)")
    endif()
  endif()
  # The launcher's log file (platform/log_file.cpp): its header, its name and the ten-file rotation, in a temporary folder.
  add_executable(tpw_log_tests platform/tests/log_tests.cpp)
  target_include_directories(tpw_log_tests PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
  target_link_libraries(tpw_log_tests PRIVATE tpw_platform)
  add_test(NAME log_file COMMAND tpw_log_tests)

  # Where the intro films are drawn (include/platform/filmbox.h): pure arithmetic, no game or window.
  add_executable(tpw_host_filmbox ${TPW_DECOMP_DIR}/tests/host_filmbox.cpp)
  target_include_directories(tpw_host_filmbox PRIVATE ${TPW_INCLUDE_DIR})
  add_test(NAME filmbox COMMAND tpw_host_filmbox)

  # The launcher's own window (launcher/ui/): widgets drawn with SDL_Renderer, text with stb_truetype
  # (third_party/stb/), in the fonts of third_party/fonts/, which are compiled in as byte arrays written here at
  # configure time, so the launcher needs no files beside it to draw its window.
  set(TPW_UI_FONTS
      kFredokaBold third_party/fonts/fredoka/Fredoka-Bold.ttf
      kFredokaSemiBold third_party/fonts/fredoka/Fredoka-SemiBold.ttf
      kNunitoRegular third_party/fonts/nunito/Nunito-Regular.ttf
      kNunitoBold third_party/fonts/nunito/Nunito-Bold.ttf)
  set(TPW_UI_FONTS_CPP ${CMAKE_CURRENT_BINARY_DIR}/ui_fonts.cpp)
  set(fonts_text "// Written by CMakeLists.txt from third_party/fonts/ (SIL Open Font License 1.1, see the OFL.txt files).\n")
  string(APPEND fonts_text "#include <cstddef>\nnamespace tpw::ui::fontdata {\n")
  list(LENGTH TPW_UI_FONTS fonts_length)
  math(EXPR fonts_last "${fonts_length} - 1")
  foreach(i RANGE 0 ${fonts_last} 2)
    math(EXPR j "${i} + 1")
    list(GET TPW_UI_FONTS ${i} name)
    list(GET TPW_UI_FONTS ${j} font)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${font})
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/${font} hex HEX)
    string(LENGTH "${hex}" hex_length)
    math(EXPR size "${hex_length} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex "${hex}")
    string(APPEND fonts_text "extern const unsigned char ${name}[];\nconst unsigned char ${name}[] = {\n${hex}\n};\n")
    string(APPEND fonts_text "extern const size_t ${name}Size;\nconst size_t ${name}Size = ${size};\n")
  endforeach()
  string(APPEND fonts_text "}  // namespace tpw::ui::fontdata\n")
  file(WRITE ${TPW_UI_FONTS_CPP}.new "${fonts_text}")
  configure_file(${TPW_UI_FONTS_CPP}.new ${TPW_UI_FONTS_CPP} COPYONLY)

  file(GLOB TPW_LAUNCHER_UI_SOURCES CONFIGURE_DEPENDS launcher/ui/*.cpp)
  add_library(tpw_launcher_ui STATIC ${TPW_LAUNCHER_UI_SOURCES} ${TPW_UI_FONTS_CPP})
  target_include_directories(tpw_launcher_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/launcher/ui)
  target_include_directories(tpw_launcher_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
  target_link_libraries(tpw_launcher_ui PUBLIC SDL3::SDL3 tpw_assets ZLIB::ZLIB)
  # The launcher's version: launcher/VERSION.txt is the one place it is written (not launcher/VERSION: launcher/ is an
  # include folder, and on a case-blind disk that name would stand in for the C++ header <version>). The window's corner,
  # the About box, --version and the log file's first line all take it from here. A release is X.Y.Z, and only its tag
  # (vX.Y.Z) says so; between releases main says the next one with -dev (0.0.4-dev), so a build of main never passes for a
  # release. The Windows version resource and the macOS Info.plist take the numbers only (TPW_LAUNCHER_VERSION_NUMBER).
  file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/launcher/VERSION.txt TPW_LAUNCHER_VERSION LIMIT_COUNT 1)
  if(NOT TPW_LAUNCHER_VERSION MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)(-dev)?$")
    message(FATAL_ERROR "launcher/VERSION.txt: \"${TPW_LAUNCHER_VERSION}\" is not a version like 0.0.3 or 0.0.4-dev")
  endif()
  set(TPW_LAUNCHER_VERSION_NUMBER "${CMAKE_MATCH_1}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/launcher/VERSION.txt)
  target_compile_definitions(tpw_launcher_ui PUBLIC TPW_LAUNCHER_VERSION="${TPW_LAUNCHER_VERSION}")
  # The product's name as the system shows it, read from launcher/ui/theme.h (TPW_UI_PRODUCT_NAME), its one place, for the
  # Windows version resource and the macOS Info.plist below.
  file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/launcher/ui/theme.h tpw_name_line REGEX "^#define TPW_UI_PRODUCT_NAME \"" LIMIT_COUNT 1)
  if(NOT tpw_name_line MATCHES "^#define TPW_UI_PRODUCT_NAME \"([^\"]+)\"")
    message(FATAL_ERROR "launcher/ui/theme.h: no #define TPW_UI_PRODUCT_NAME \"...\" line")
  endif()
  set(TPW_BUNDLE_NAME "${CMAKE_MATCH_1}")
  set(TPW_BUNDLE_EXECUTABLE tpw_launcher)
  string(REPLACE "." "," TPW_LAUNCHER_VERSION_COMMAS "${TPW_LAUNCHER_VERSION_NUMBER}")
  if(WIN32)
    # The executable's version resource (launcher/win/launcher.rc.in): FileVersion, ProductVersion, ProductName and a
    # FileDescription of the name with its version, which Explorer's Details and the Task Manager show. Compiled by the
    # toolchain's resource compiler (llvm-mingw's windres); without one the build goes on without the resource.
    include(CheckLanguage)
    check_language(RC)
    if(CMAKE_RC_COMPILER)
      enable_language(RC)
      configure_file(launcher/win/launcher.rc.in ${CMAKE_CURRENT_BINARY_DIR}/launcher.rc @ONLY)
      target_sources(tpw_launcher PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/launcher.rc)
    else()
      message(STATUS "tpw: no resource compiler, tpw_launcher.exe has no version resource")
    endif()
  endif()
  if(APPLE)
    # A macOS .app bundle (tpw_launcher.app) with launcher/macos/Info.plist.in, so the Dock, Cmd-Tab and the application
    # menu read the launcher's name. Off by default: the scripts, the README and CI run the bare tpw_launcher.
    option(TPW_BUNDLE "Build the macOS launcher as an .app bundle (launcher/macos/Info.plist.in)" OFF)
    set(TPW_BUNDLE_IDENTIFIER "org.tpw-port.launcher" CACHE STRING "The .app bundle's CFBundleIdentifier (TPW_BUNDLE)")
    if(TPW_BUNDLE)
      configure_file(launcher/macos/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)
      set_target_properties(tpw_launcher PROPERTIES MACOSX_BUNDLE ON
                            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist)
    endif()
  endif()
  # The About box's versions (launcher/ui/versions.cpp) come from the libraries' own headers: libjpeg's and FFmpeg's
  # (headers only; the game's platform layer links them), and the stb headers' first lines, which carry their versions.
  target_include_directories(tpw_launcher_ui PRIVATE $<TARGET_PROPERTY:tpw_libjpeg,INTERFACE_INCLUDE_DIRECTORIES>)
  if(TPW_MOVIES)
    target_include_directories(tpw_launcher_ui SYSTEM PRIVATE $<TARGET_PROPERTY:tpw_ffmpeg,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(tpw_launcher_ui PRIVATE TPW_HAVE_FFMPEG=1)
  endif()
  foreach(stb_header truetype rect_pack)
    file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb/stb_${stb_header}.h stb_first LIMIT_COUNT 1)
    if(NOT stb_first MATCHES " - v([0-9.]+) - ")
      message(FATAL_ERROR "third_party/stb/stb_${stb_header}.h: no version on its first line")
    endif()
    string(TOUPPER ${stb_header} stb_upper)
    target_compile_definitions(tpw_launcher_ui PRIVATE TPW_STB_${stb_upper}_VERSION="${CMAKE_MATCH_1}")
  endforeach()

  # Every screen of the launcher's window drawn offscreen (SDL's software renderer, no window) from a scripted import,
  # written as PNG files for a look by eye: to TPW_UI_SCREENS_DIR, or to ui-screens/ in the build folder.
  add_executable(tpw_launcher_ui_tests launcher/ui/tests/host_ui_screens.cpp)
  target_link_libraries(tpw_launcher_ui_tests PRIVATE tpw_launcher_ui)
  if(TPW_MOVIES)
    target_compile_definitions(tpw_launcher_ui_tests PRIVATE TPW_TEST_FFMPEG=1)
  endif()
  target_compile_definitions(tpw_launcher_ui_tests PRIVATE TPW_UI_SCREENS_DEFAULT="${CMAKE_CURRENT_BINARY_DIR}/ui-screens")
  add_test(NAME launcher_ui COMMAND tpw_launcher_ui_tests)
  # The run-time table loader (platform/game_tables.cpp) against the configure-time tables, where the build has them (the
  # differential tests' or TPW_TABLES_AT_CONFIGURE's): every table it reads from the CD's 2.0.1 program and from the 2.04b3
  # patch's, by the program's SHA-1 and by the content search, must equal the generated arrays byte for byte. The scorers
  # are the test's own (one body per name), so the game library is not linked. A program that is absent is skipped.
  if(TPW_GENERATED_DIR AND EXISTS ${TPW_GENERATED_DIR}/advisor_lines.cpp)
    add_executable(tpw_game_tables_tests platform/tests/game_tables_tests.cpp platform/game_tables.cpp ${TPW_GENERATED_SOURCES})
    target_include_directories(tpw_game_tables_tests PRIVATE ${TPW_INCLUDE_DIR} ${TPW_GENERATED_DIR})
    target_compile_definitions(tpw_game_tables_tests PRIVATE TPW_TABLES_AT_CONFIGURE=1)
    add_test(NAME game_tables COMMAND tpw_game_tables_tests
             "${CMAKE_CURRENT_SOURCE_DIR}/../extracted/disc-hfs/Theme Park Data/SimTheme Park"
             "${CMAKE_CURRENT_SOURCE_DIR}/../extracted/SimTPPatch_204b3/Sim Theme Park Patch/patch 2.04b3/SimTheme Park")
    set_tests_properties(game_tables PROPERTIES SKIP_RETURN_CODE 77)
  endif()
else()
  message(STATUS "tpw: launcher not built (needs OpenGL and Python 3)")
endif()

if(NOT TARGET tpw_launcher)
  message(WARNING "tpw-port: the launcher needs SDL3, OpenGL and Python 3")
endif()

# The fixes, in every target of the port and of the submodule: a source under overrides/ is built in place of the
# submodule's file of the same path, and the submodule's include/ and platform/ folders are replaced by their copies with
# the overrides laid over them (TPW_OVERLAY_DIR, above).
file(GLOB_RECURSE TPW_OVERRIDE_SOURCES CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/overrides
     ${CMAKE_CURRENT_SOURCE_DIR}/overrides/*.cpp)
set(TPW_OVERRIDES_USED)
string(LENGTH "${TPW_DECOMP_DIR}/" prefix_length)
get_property(TPW_DECOMP_TARGETS DIRECTORY ${TPW_DECOMP_DIR} PROPERTY BUILDSYSTEM_TARGETS)
get_property(TPW_PORT_TARGETS DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY BUILDSYSTEM_TARGETS)
foreach(target ${TPW_DECOMP_TARGETS} ${TPW_PORT_TARGETS})
  get_target_property(type ${target} TYPE)
  if(type STREQUAL "UTILITY")
    continue()
  endif()
  get_target_property(sources ${target} SOURCES)
  if(sources)
    set(new_sources)
    foreach(source ${sources})
      if(NOT IS_ABSOLUTE "${source}" AND target IN_LIST TPW_DECOMP_TARGETS AND NOT source MATCHES "^\\$<")
        set(source ${TPW_DECOMP_DIR}/${source})
      endif()
      string(SUBSTRING "${source}" 0 ${prefix_length} prefix)
      if(prefix STREQUAL "${TPW_DECOMP_DIR}/")
        string(SUBSTRING "${source}" ${prefix_length} -1 rel)
        if(rel IN_LIST TPW_OVERRIDE_SOURCES)
          set(source ${CMAKE_CURRENT_SOURCE_DIR}/overrides/${rel})
          list(APPEND TPW_OVERRIDES_USED ${rel})
        endif()
      endif()
      list(APPEND new_sources ${source})
    endforeach()
    set_property(TARGET ${target} PROPERTY SOURCES ${new_sources})
  endif()
  set(props INTERFACE_INCLUDE_DIRECTORIES)
  if(NOT type STREQUAL "INTERFACE_LIBRARY")
    list(APPEND props INCLUDE_DIRECTORIES)
  endif()
  foreach(prop ${props})
    get_target_property(dirs ${target} ${prop})
    if(NOT dirs)
      continue()
    endif()
    set(new_dirs)
    foreach(dir ${dirs})
      if(dir STREQUAL "${TPW_DECOMP_DIR}/include")
        set(dir ${TPW_OVERLAY_DIR}/include)
      elseif(dir STREQUAL "${TPW_DECOMP_DIR}/platform")
        set(dir ${TPW_OVERLAY_DIR}/platform)
      endif()
      string(SUBSTRING "${dir}" 0 ${prefix_length} prefix)
      if(dir STREQUAL "${TPW_DECOMP_DIR}/assets")
        # the launcher's asset library: no TPW_PORT_FIX, so nothing in it has an override
        list(APPEND new_dirs ${dir})
        continue()
      endif()
      if(dir STREQUAL "${TPW_DECOMP_DIR}" OR prefix STREQUAL "${TPW_DECOMP_DIR}/")
        message(FATAL_ERROR "tpw-port: ${target} reads headers from ${dir}, past the overrides")
      endif()
      list(APPEND new_dirs ${dir})
    endforeach()
    set_property(TARGET ${target} PROPERTY ${prop} ${new_dirs})
  endforeach()
endforeach()
foreach(rel ${TPW_OVERRIDE_SOURCES})
  if(NOT rel IN_LIST TPW_OVERRIDES_USED)
    message(FATAL_ERROR "overrides/${rel} has no file in tpw-decomp to replace")
  endif()
endforeach()
