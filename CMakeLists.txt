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
set(TPW_PORT_FIX ON)   # the fixes are always on here: the ctest checks of the fixes (if(TPW_PORT_FIX) below) and the mods
# The mods the launcher is built with (platform/mods.cmake): the tpw-mods submodule. Without it the build has no mods.
set(TPW_MODS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mods CACHE PATH "The tpw-mods folder whose mods the launcher is built with")
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
set(TPW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# The Linux build (the TPW_LINUX parts of this file): the platform layer's host code is in platform/host_linux.cpp and
# platform/linux, and the game is drawn by the system's OpenGL or, when the player chooses it, by the port's own Vulkan
# renderer (platform/vulkan): every OpenGL call the game makes is the platform layer's own (platform/gl_entry.cpp), and
# SDL opens the OpenGL driver and the Vulkan loader at run time. Nothing of Vulkan or OpenGL is linked, so the build needs neither
# development package.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(TPW_LINUX ON)
  # GCC on Ubuntu (CI's Linux runner) defines _FORTIFY_SOURCE=2 by itself when optimising; GCC elsewhere (Arch,
  # MinGW) does not. It turns memset, memcpy, strcpy and the rest into checks against the destination's declared size,
  # which abort the program when exceeded. The translation reaches the Mac's memory through declarations narrower than
  # the storage behind them, so a check can fail where the Mac's code is right. The first one found aborted the game
  # while a theme loaded (BumperRide.cpp clears the 0x3c-byte bump world over gColList, whose tabCOLLIST was declared
  # 0x20 bytes; BMP_InsideArea.h now has the Mac's size), but the launcher has 585 checked calls, and until a park has
  # been played on Linux with the checks on, they stay off.
  # -U_FORTIFY_SOURCE is what Ubuntu's own compiler spec looks for to leave the definition out.
  add_compile_options(-U_FORTIFY_SOURCE)
endif()

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

if(NOT APPLE)
  # The Windows and Linux builds of the game library: tpw-decomp as it is, with what those systems lack provided here.
  if(WIN32)
    # The POSIX headers MinGW has no copy of (dlfcn.h, sys/statvfs.h, pthread/qos.h) are in platform/win, over
    # platform/posix_win.cpp.
    target_include_directories(tpw PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/platform/win)
  endif()
  # tpw-decomp builds src/sound/CSprocketsModule.cpp without the methods platform/sound_module.cpp defines, with its
  # slicer (tpw_slice in its CMakeLists.txt), which runs on libclang; devtools/slice_win.py makes the copy from the same
  # list, without libclang (on Linux too: a player's machine has no libclang, and GCC has none beside it). The destructor
  # goes too: it is the class's key function, so the file emits the vtable, which its Mach-O directive makes weak and
  # neither COFF nor ELF can (the script also drops that directive); the vtable is then platform/sound_module.cpp's.
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
        COMMENT "Slicing src/${rel} for tpw (without libclang)"
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
# lands). Skipped when OpenGL (on a Mac or Windows; Linux needs none to build) or Python 3 is missing: the library and
# the tests do not need it.
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
  if(NOT TPW_LINUX)   # Linux: the OpenGL calls are the platform layer's own (platform/gl_entry.cpp), nothing is linked
    find_package(OpenGL QUIET)
  endif()
  find_package(Python3 COMPONENTS Interpreter QUIET)
endif()
if(TPW_BUILD_LAUNCHER AND (OpenGL_FOUND OR TPW_LINUX) AND Python3_FOUND)
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
  target_include_directories(tpw_platform PRIVATE ${TPW_DECOMP_DIR}/assets)   # gold_rides.cpp's gold_ride_table.h
  target_link_libraries(tpw_platform PUBLIC SDL3::SDL3)
  target_link_libraries(tpw_platform PRIVATE ZLIB::ZLIB)   # platform/online_ftp.cpp's park checks
  if(NOT TPW_LINUX)
    target_link_libraries(tpw_platform PUBLIC OpenGL::GL)
  endif()
  target_compile_definitions(tpw_platform PRIVATE GL_SILENCE_DEPRECATION)
  # The IJG's libjpeg 6b (third_party/libjpeg-6b, see its NOTE.md): the game's JPEG calls (platform/jpeg_bridge.cpp) run on it. Its
  # headers are only the platform layer's own; the unit test libraries never compile it (they fake the calls).
  enable_language(C)
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/libjpeg-6b ${CMAKE_CURRENT_BINARY_DIR}/third_party/libjpeg-6b)
  target_link_libraries(tpw_platform PRIVATE tpw_libjpeg)
  if(APPLE)
    # platform/gdi.cpp draws the signs' text with CoreText into a CoreGraphics bitmap.
    target_link_libraries(tpw_platform PUBLIC "-framework CoreText" "-framework CoreGraphics" "-framework CoreFoundation" objc)
  elseif(WIN32)
    # The WIN32 parts of this file: Windows port contributed by samueleagle91 (tpw-port PR #1).
    # The signs' text is drawn with stb_truetype (third_party/stb, platform/gdi_text_stb.cpp, the Windows and Linux
    # builds' file) in place of CoreText (platform/gdi_text.cpp). The POSIX calls MinGW lacks come from platform/win: its
    # headers, and posix_win.h ahead of every file (quoted inside SHELL:, which splits at spaces, so a clone in a folder
    # with a space builds). The sockets are Windows' (platform/winsock.cpp), and gluBuild2DMipmaps, in the Mac's OpenGL
    # framework, is glu32's.
    get_target_property(platform_sources tpw_platform SOURCES)
    list(REMOVE_ITEM platform_sources ${CMAKE_CURRENT_SOURCE_DIR}/platform/gdi_text.cpp)
    set_property(TARGET tpw_platform PROPERTY SOURCES ${platform_sources})
    target_include_directories(tpw_platform PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
    target_include_directories(tpw_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/platform/win)
    target_compile_options(tpw_platform PRIVATE "SHELL:-include \"${CMAKE_CURRENT_SOURCE_DIR}/platform/win/posix_win.h\"")
    target_link_libraries(tpw_platform PUBLIC ws2_32 glu32)
    # platform/log_export.cpp: the Downloads folder is a Known Folder (SHGetKnownFolderPath, shell32; CoTaskMemFree, ole32).
    target_link_libraries(tpw_platform PUBLIC shell32 ole32)
  elseif(TPW_LINUX)
    # The signs' text is drawn with stb_truetype (platform/gdi_text_stb.cpp, as on Windows) in place of CoreText. The Mac's
    # thread quality-of-service header is platform/linux/pthread/qos.h, over platform/host_linux.cpp. Every OpenGL call
    # the game makes is the platform layer's own (platform/gl_entry.cpp): it goes to the system's OpenGL, which SDL opens
    # at run time, or, when the player chooses it and it can draw, to the port's Vulkan renderer (platform/vulkan/, with
    # the vendored Vulkan headers of third_party/vulkan; the loader is opened at run time through SDL);
    # gluBuild2DMipmaps is built there too. So no Vulkan, OpenGL or GLU
    # library is linked, and the only libraries of the system in the launcher are the C and C++ runtimes and SDL's (dl,
    # pthread).
    get_target_property(platform_sources tpw_platform SOURCES)
    list(REMOVE_ITEM platform_sources ${CMAKE_CURRENT_SOURCE_DIR}/platform/gdi_text.cpp)
    set_property(TARGET tpw_platform PROPERTY SOURCES ${platform_sources})
    file(GLOB TPW_VULKAN_SOURCES CONFIGURE_DEPENDS platform/vulkan/*.cpp)
    target_sources(tpw_platform PRIVATE ${TPW_VULKAN_SOURCES})
    target_compile_definitions(tpw_platform PUBLIC TPW_HAVE_VULKAN=1)
    target_include_directories(tpw_platform PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
                               ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan/include)
    target_include_directories(tpw_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/platform/linux)
    find_package(Threads REQUIRED)
    target_link_libraries(tpw_platform PUBLIC ${CMAKE_DL_LIBS} Threads::Threads)
  endif()

  # The game's films (the Mac CD's QuickTime ones, Sorenson video, or the PC CD's EA .TGQ ones, EA TQI video) play
  # through FFmpeg (platform/quicktime.cpp), built from source here (third_party/ffmpeg: a trimmed FFmpeg, LGPL 2.1 or
  # later, static); no copy on the host is ever looked for. With TPW_MOVIES off the game skips its films.
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

  add_library(tpw_launcher_objects OBJECT launcher/main.cpp launcher/app_bundle.cpp launcher/app_icon.cpp launcher/app_icon_standin.cpp launcher/asset_packs.cpp
              launcher/game_app.cpp launcher/game_app_mac.cpp launcher/game_app_win.cpp launcher/installed_icon.cpp launcher/pending_manual.cpp
              launcher/static_init.cpp launcher/startup.cpp)
  target_include_directories(tpw_launcher_objects PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform launcher)
  if(APPLE)
    # The game's own application (launcher/game_app.h) is opened through NSWorkspace, whose completion handler is a block.
    set_source_files_properties(launcher/game_app_mac.cpp PROPERTIES COMPILE_OPTIONS -fblocks)
  endif()
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
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
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
  elseif(TPW_LINUX)
    # The OpenGL calls are the platform layer's own (platform/gl_entry.cpp), and the C and C++ runtimes are dlsym'ed by the
    # generator; the part of glibc that is linked statically into every program (libc_nonshared.a: atexit, among others)
    # is read as a static library, so that its functions get no stand-in.
    set(TPW_STUB_SYSLIBS)
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libc_nonshared.a OUTPUT_VARIABLE tpw_libc_nonshared
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(EXISTS "${tpw_libc_nonshared}")
      set(TPW_STUB_SYSLIBS --provides ${tpw_libc_nonshared})
    endif()
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
  if(TPW_LINUX)
    # GNU ld reads each static library once, in order: the game functions only the platform layer calls (the JPEG loader's
    # constructor body, a mail attachment's) are asked for after libtpw.a has gone by, so it is read once more at the end.
    target_link_libraries(tpw_launcher PRIVATE $<TARGET_FILE:tpw>)
    # The C++ runtime goes into the launcher, so that it needs only the C library of the machine it runs on: the file
    # built on one distribution runs on another with a glibc as new. Off, the launcher uses the machine's libstdc++ and
    # libgcc_s, as a distribution's package would.
    option(TPW_STATIC_RUNTIME "Link libstdc++ and libgcc into the launcher, so it needs only glibc" ON)
    if(TPW_STATIC_RUNTIME)
      target_link_options(tpw_launcher PRIVATE -static-libstdc++ -static-libgcc)
    endif()
  endif()
  if(WIN32)
    # A Windows app, not a console one: started from Explorer it opens no console window, and what it prints goes into
    # the log file, logs/launcher.log (launcher/main.cpp, platform/host_win.h). Its symbols are exported, so that the game's
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
  elseif(TPW_LINUX)
    # The same on Linux, where a GCC build has no libclang beside the compiler: the tests are made when a libclang is
    # found, the distribution's (libclang-NN-dev or libclang1-NN on Debian and Ubuntu, /usr/lib/llvm-NN/lib) or the one
    # TPW_LIBCLANG names, and the slicer runs through devtools/slicer_linux.py, which gives it that libclang and clang's
    # resource folder beside it.
    set(TPW_LIBCLANG "$ENV{TPW_LIBCLANG}" CACHE FILEPATH "libclang (a .so), for the platform tests' slicer on Linux")
    set(tpw_libclang "${TPW_LIBCLANG}")
    if(NOT EXISTS "${tpw_libclang}")
      file(GLOB tpw_libclang_candidates /usr/lib/llvm-*/lib/libclang-*.so.* /usr/lib/llvm-*/lib/libclang.so.* /usr/lib/*/libclang-*.so.*
           /usr/lib64/libclang.so.* /usr/lib/libclang.so.* /usr/local/lib/libclang.so.*)
      list(FILTER tpw_libclang_candidates EXCLUDE REGEX "libclang-cpp")
      if(tpw_libclang_candidates)
        list(SORT tpw_libclang_candidates COMPARE NATURAL ORDER DESCENDING)
        list(GET tpw_libclang_candidates 0 tpw_libclang)
      endif()
    endif()
    if(EXISTS "${tpw_libclang}")
      set(wrapper ${CMAKE_CURRENT_SOURCE_DIR}/devtools/slicer_linux.py)
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${wrapper})
      file(SHA1 ${wrapper} wrapper_sha1)
      set(runner ${CMAKE_CURRENT_BINARY_DIR}/slicer_linux_run.py)
      file(WRITE ${runner}.new "# Written by CMakeLists.txt: devtools/slicer_linux.py (${wrapper_sha1}) with this build's libclang.\n"
                               "import os, runpy\n"
                               "os.environ[\"TPW_LIBCLANG\"] = r\"${tpw_libclang}\"\n"
                               "runpy.run_path(r\"${wrapper}\", run_name=\"__main__\")\n")
      configure_file(${runner}.new ${runner} COPYONLY)
      set_property(GLOBAL PROPERTY TPW_SLICER ${runner})
      message(STATUS "tpw: platform tests sliced with ${tpw_libclang}")
    else()
      set_target_properties(tpw_platform_tests PROPERTIES EXCLUDE_FROM_ALL ON)
      set_tests_properties(platform PROPERTIES DISABLED ON)
      message(STATUS "tpw: platform tests not made (no libclang found; TPW_LIBCLANG names one)")
    endif()
  endif()
  # The tpw-mods mods (platform/mods.h, platform/mods.cmake: TPW_MODS_DIR, their catalog and tests), with the port's
  # fixes only: a build without them has no mods and no Mods box.
  if(TPW_PORT_FIX)
    include(platform/mods.cmake)
  else()
    get_target_property(platform_sources tpw_platform SOURCES)
    list(REMOVE_ITEM platform_sources ${CMAKE_CURRENT_SOURCE_DIR}/platform/mods.cpp)
    set_property(TARGET tpw_platform PROPERTY SOURCES ${platform_sources})
  endif()
  # The sound library's pools taken from on one thread and given back on another (the game and SDL's audio thread): no
  # object handed out twice. The program builds only the pools' functions of their src files (tests/host_sound_pools_keep.cmake),
  # cut by the same slicer as the platform tests, so it is made where they are. The original's pools are not locked (their
  # give-back ran at interrupt time), so ctest runs it only with the port's fixes; a build without them makes it, and it fails.
  find_package(Threads REQUIRED)
  add_executable(tpw_host_sound_pools ${TPW_DECOMP_DIR}/tests/host_sound_pools.cpp)
  include(${TPW_DECOMP_DIR}/tests/host_sound_pools_keep.cmake)
  list(TRANSFORM TPW_SOUND_POOLS_SOURCES PREPEND ${TPW_DECOMP_DIR}/src/)
  target_sources(tpw_host_sound_pools PRIVATE ${TPW_SOUND_POOLS_SOURCES})
  target_include_directories(tpw_host_sound_pools PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
  target_link_libraries(tpw_host_sound_pools PRIVATE Threads::Threads)
  tpw_slice(TARGET tpw_host_sound_pools NAME sound_pools KEEP ${TPW_SOUND_POOLS_KEEP})
  get_target_property(tpw_platform_tests_skipped tpw_platform_tests EXCLUDE_FROM_ALL)
  if(tpw_platform_tests_skipped)
    set_target_properties(tpw_host_sound_pools PROPERTIES EXCLUDE_FROM_ALL ON)
  elseif(TPW_PORT_FIX)
    add_test(NAME sound_pools COMMAND tpw_host_sound_pools)
  endif()
  # Quitting with the park map open: the map's destroy message cancels its advisor message after the world is deleted. The
  # program builds only UIMain_CancelAdvisorMessage of src/game/ui/ui_main.cpp (the world and the advisor are the test's
  # stand-ins). The original asks the null world, so ctest runs it only with the port's fixes; a build without them fails.
  add_executable(tpw_host_map_quit ${TPW_DECOMP_DIR}/tests/host_map_quit.cpp ${TPW_DECOMP_DIR}/src/game/ui/ui_main.cpp)
  target_include_directories(tpw_host_map_quit PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
  if(WIN32)
    # ui_main.cpp includes dlfcn.h, which MinGW has no copy of: platform/win's, as the game library gets it (the slicer
    # indexes the file with this target's own include folders).
    target_include_directories(tpw_host_map_quit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/platform/win)
  endif()
  if(TPW_PORT_FIX)
    target_compile_definitions(tpw_host_map_quit PRIVATE TPW_HOST_MAP_QUIT_GUARD)   # the test sets the fix's flag
  endif()
  tpw_slice(TARGET tpw_host_map_quit NAME map_quit KEEP __Z27UIMain_CancelAdvisorMessage14AdvisorMessage)
  if(tpw_platform_tests_skipped)
    set_target_properties(tpw_host_map_quit PROPERTIES EXCLUDE_FROM_ALL ON)
  elseif(TPW_PORT_FIX)
    add_test(NAME map_quit COMMAND tpw_host_map_quit)
  endif()
  # A sound bank's reference count raised by the game and lowered by SDL's audio thread at once: no change lost. The program
  # builds src/bullfrog/TbRefRelease.cpp whole (it holds only the count). The original counts with a plain load and store, so
  # ctest runs it only with the port's fixes; a build without them makes it, and it fails.
  add_executable(tpw_host_ref_count ${TPW_DECOMP_DIR}/tests/host_ref_count.cpp ${TPW_DECOMP_DIR}/src/bullfrog/TbRefRelease.cpp)
  target_include_directories(tpw_host_ref_count PRIVATE ${TPW_INCLUDE_DIR})
  target_link_libraries(tpw_host_ref_count PRIVATE Threads::Threads)
  if(TPW_PORT_FIX)
    add_test(NAME ref_count COMMAND tpw_host_ref_count)
  endif()
  # The launcher's "W A S D move the camera" (the port's fix wasd-camera, include/port_wasd.h): which camera key each of the four
  # presses, over the park and in first person, on another layout and with a modifier. The original has no such function, so
  # the program is made only with the port's fixes.
  if(TPW_PORT_FIX)
    add_executable(tpw_host_wasd_camera ${TPW_DECOMP_DIR}/tests/host_wasd_camera.cpp)
    target_include_directories(tpw_host_wasd_camera PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
    add_test(NAME wasd_camera COMMAND tpw_host_wasd_camera)
  endif()
  # A park file's packed block cut short or damaged, unpacked with the block's length (the port's fixes parkload and
  # truncsave): no read past the block, and only a whole block unpacks. The program builds the packing and that unpacking of
  # src/bullfrog/LbZLib.cpp, with the host's zlib; the original has no such call, so it is made only with the port's fixes.
  if(TPW_PORT_FIX)
    add_executable(tpw_host_truncated_save ${TPW_DECOMP_DIR}/tests/host_truncated_save.cpp ${TPW_DECOMP_DIR}/src/bullfrog/LbZLib.cpp)
    target_include_directories(tpw_host_truncated_save PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
    target_link_libraries(tpw_host_truncated_save PRIVATE ZLIB::ZLIB)
    tpw_slice(TARGET tpw_host_truncated_save NAME truncated_save KEEP
      __Z31LbZLib_PortMemDecompressCheckedPvjS_j   # LbZLib_PortMemDecompressChecked(void*, unsigned int, void*, unsigned int)
      __Z18LbZLib_MemCompressPvS_jS_   # LbZLib_MemCompress(void*, void*, unsigned int, void*)
      __ZNK3bfl8compress5CZLib11MemCompressEPhPKhjS2_   # bfl::compress::CZLib::MemCompress(unsigned char*, unsigned char const*, unsigned int, unsigned char*) const
      __ZNK3bfl8compress5CZLib21CompressWorkspaceSizeEv   # bfl::compress::CZLib::CompressWorkspaceSize() const
      __Z10SwapHeaderP14ZLibCompHeader)   # SwapHeader(ZLibCompHeader*)
    if(tpw_platform_tests_skipped)
      set_target_properties(tpw_host_truncated_save PROPERTIES EXCLUDE_FROM_ALL ON)
    else()
      add_test(NAME truncated_save COMMAND tpw_host_truncated_save)
    endif()
  endif()
  # Old parks' pylon codes (the port's fix loopcode): recordings of coaster building as the game writes them, with the 0s the
  # Mac's recorder wrote for the pylon types and undos, put back by the real PortRepairOldPylonCodes of src/game/ActionRecorder.cpp.
  # The original has no such repair, so it is made only with the port's fixes.
  if(TPW_PORT_FIX)
    add_executable(tpw_host_pylon_codes ${TPW_DECOMP_DIR}/tests/host_pylon_codes.cpp ${TPW_DECOMP_DIR}/src/game/ActionRecorder.cpp)
    target_include_directories(tpw_host_pylon_codes PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
    tpw_slice(TARGET tpw_host_pylon_codes NAME pylon_codes KEEP
      __Z23PortRepairOldPylonCodesPhjPKj)   # PortRepairOldPylonCodes(unsigned char*, unsigned int, unsigned int const*)
    if(tpw_platform_tests_skipped)
      set_target_properties(tpw_host_pylon_codes PROPERTIES EXCLUDE_FROM_ALL ON)
    else()
      add_test(NAME pylon_codes COMMAND tpw_host_pylon_codes)
    endif()
  endif()
  # The options panel's Screen resolution slider in the launcher (the port's fix resslider): the real DoScroll,
  # CPlayerSettings::SetResolution and PortApplyGameSize, the windows and the clock being the test's stand-ins. Made only with
  # the port's fixes: the original has no such choice of sizes.
  if(TPW_PORT_FIX)
    add_executable(tpw_host_resolution_slider ${TPW_DECOMP_DIR}/tests/host_resolution_slider.cpp ${TPW_DECOMP_DIR}/src/game/ui/ui_parkStatus.cpp
                   ${TPW_DECOMP_DIR}/src/game/TPSettings.cpp ${TPW_DECOMP_DIR}/src/game/ui/ui_gamesize.cpp ${TPW_DECOMP_DIR}/platform/msl.cpp)
    target_include_directories(tpw_host_resolution_slider PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
    tpw_slice(TARGET tpw_host_resolution_slider NAME resolution_slider KEEP
      __ZN13options_panel8DoScrollEii   # options_panel::DoScroll(int, int)
      __ZN15CPlayerSettings13SetResolutionENS_11eRESOLUTIONE   # CPlayerSettings::SetResolution(CPlayerSettings::eRESOLUTION)
      __Z17PortApplyGameSizev   # PortApplyGameSize()
      __Z13PortShowShapei)   # PortShowShape(int)
    if(tpw_platform_tests_skipped)
      set_target_properties(tpw_host_resolution_slider PROPERTIES EXCLUDE_FROM_ALL ON)
    else()
      add_test(NAME resolution_slider COMMAND tpw_host_resolution_slider)
    endif()
    # Walking in first person in a park visited online (the port's fix movethrottle): the real UpdateAvatarPos,
    # ChatGoFirstPerson and ChatBackToThirdPerson, the control, the windows and the clock being the test's stand-ins. Made only
    # with the port's fixes: the original sends a Move every frame.
    add_executable(tpw_host_move_throttle ${TPW_DECOMP_DIR}/tests/host_move_throttle.cpp ${TPW_DECOMP_DIR}/src/game/ui/ui_onlinePanel.cpp)
    target_include_directories(tpw_host_move_throttle PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
    tpw_slice(TARGET tpw_host_move_throttle NAME move_throttle KEEP
      __Z15UpdateAvatarPosv   # UpdateAvatarPos()
      __Z17ChatGoFirstPersonv   # ChatGoFirstPerson()
      __Z21ChatBackToThirdPersonv   # ChatBackToThirdPerson()
      _gOnlineFirstPersonCountdown _gOnlineMapX _gOnlineMapZ _gOnlineSavedFlags)
    if(tpw_platform_tests_skipped)
      set_target_properties(tpw_host_move_throttle PROPERTIES EXCLUDE_FROM_ALL ON)
    else()
      add_test(NAME move_throttle COMMAND tpw_host_move_throttle)
    endif()
    # A live change of shape reaches the mesh and gradient draw objects (the port's fix resslider): the real objects with the real
    # Change and GetUIMatrix; the engine's meshes and textures and the base class are the test's stand-ins.
    add_executable(tpw_host_ui_follow ${TPW_DECOMP_DIR}/tests/host_ui_follow.cpp ${TPW_DECOMP_DIR}/src/game/ui/InterfaceDrawEngine.cpp
                   ${TPW_DECOMP_DIR}/src/game/TPMesh.cpp ${TPW_DECOMP_DIR}/src/game/LMatrix.cpp ${TPW_DECOMP_DIR}/src/game/ui/CoolRect.cpp
                   ${TPW_DECOMP_DIR}/src/game/ui/CoolRegion.cpp ${TPW_DECOMP_DIR}/platform/arena.cpp)
    target_include_directories(tpw_host_ui_follow PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
    tpw_slice(TARGET tpw_host_ui_follow NAME ui_follow KEEP
      __Z15PortFollowUnitsv   # PortFollowUnits()
      __Z11GetUIMatrixP7sMatrixssssffffsss   # GetUIMatrix(...)
      __Z21UpdateMeshLocalMatrixP9tag_sMeshP7sMatrix   # UpdateMeshLocalMatrix(tag_sMesh*, sMatrix*)
      __Z25CombineMatricesNoWDest1stP7sMatrixS0_   # CombineMatricesNoWDest1st(sMatrix*, sMatrix*)
      __Z17SetMatrixIdentityP7sMatrix   # SetMatrixIdentity(sMatrix*)
      __Z23VectorThroughLMatrixNoWP7sMatrixP8s3DPoint   # VectorThroughLMatrixNoW(sMatrix*, s3DPoint*)
      __ZN23InterfaceDrawEngineMeshC1EPv __ZN23InterfaceDrawEngineMeshD1Ev __ZN23InterfaceDrawEngineMeshD0Ev
      __ZN23InterfaceDrawEngineMesh4ShowEs __ZN23InterfaceDrawEngineMesh4DrawEsP8CoolRects __ZN23InterfaceDrawEngineMesh4DrawEsP8CoolRectsPDs
      __ZN23InterfaceDrawEngineMesh6ChangeEjP8DOChange __ZN23InterfaceDrawEngineMesh14CalculateFrameEjP8DOChange __ZN23InterfaceDrawEngineMesh10InstanceMeEv
      __ZN33InterfaceDrawEngineResizableFrameC1EPv __ZN33InterfaceDrawEngineResizableFrameD1Ev __ZN33InterfaceDrawEngineResizableFrameD0Ev
      __ZN33InterfaceDrawEngineResizableFrame6ChangeEjP8DOChange __ZN33InterfaceDrawEngineResizableFrame10InstanceMeEv
      __ZN34InterfaceDrawEngineResizableSliderC1EPv __ZN34InterfaceDrawEngineResizableSliderD1Ev __ZN34InterfaceDrawEngineResizableSliderD0Ev
      __ZN34InterfaceDrawEngineResizableSlider6ChangeEjP8DOChange __ZN34InterfaceDrawEngineResizableSlider10InstanceMeEv
      __ZN28InterfaceDrawEngineVGradientC1EPv __ZN28InterfaceDrawEngineVGradientD1Ev __ZN28InterfaceDrawEngineVGradientD0Ev
      __ZN28InterfaceDrawEngineVGradient4ShowEs __ZN28InterfaceDrawEngineVGradient6ChangeEjP8DOChange
      __ZN28InterfaceDrawEngineVGradient9CalcVertsEv __ZN28InterfaceDrawEngineVGradient7CalcUVsEv
      __ZN28InterfaceDrawEngineHGradientC1EPv __ZN28InterfaceDrawEngineHGradientD1Ev __ZN28InterfaceDrawEngineHGradientD0Ev
      __ZN28InterfaceDrawEngineHGradient4ShowEs __ZN28InterfaceDrawEngineHGradient6ChangeEjP8DOChange
      __ZN28InterfaceDrawEngineHGradient9CalcVertsEv __ZN28InterfaceDrawEngineHGradient7CalcUVsEv
      __ZN10CoolRegion5ShiftEss __ZN10CoolRegion6AdjustEssss __ZN10CoolRegion6MiddleEPsS0_ __ZN10CoolRegion7PointInEss
      __ZN10CoolRegionD1Ev __ZN10CoolRegioneqERS_ __ZN8CoolRect14RectIntersectsEPS_ __ZN8CoolRect3GetEPsS0_S0_S0_
      __ZN8CoolRect3SetEssss __ZN8CoolRect5ShiftEss __ZN8CoolRect6AdjustEssss __ZN8CoolRect6MiddleEPsS0_
      __ZN8CoolRect7GetDimsEPsS0_ __ZN8CoolRect7PointInEss __ZN8CoolRectC1Essss __ZN8CoolRectC1Ev __ZN8CoolRectD1Ev
      __ZN8CoolRectC1Ev __ZN8CoolRectD1Ev)
    if(tpw_platform_tests_skipped)
      set_target_properties(tpw_host_ui_follow PROPERTIES EXCLUDE_FROM_ALL ON)
    else()
      add_test(NAME ui_follow COMMAND tpw_host_ui_follow)
    endif()
  endif()
  # The launcher's log file (platform/log_file.cpp): one file that each run adds to, its run headers, the cut to its newest
  # 5 MB, the old files folded in, and Export logs (platform/log_export.cpp), in a temporary folder.
  add_executable(tpw_log_tests platform/tests/log_tests.cpp)
  target_include_directories(tpw_log_tests PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
  target_link_libraries(tpw_log_tests PRIVATE tpw_platform)
  add_test(NAME log_file COMMAND tpw_log_tests)
  # What the log files and Export logs scrub (platform/log_scrub.h): the rules for the Mac's and Windows' names and paths.
  add_executable(tpw_log_scrub_tests platform/tests/log_scrub_tests.cpp)
  target_include_directories(tpw_log_scrub_tests PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
  target_link_libraries(tpw_log_scrub_tests PRIVATE tpw_platform)
  add_test(NAME log_scrub COMMAND tpw_log_scrub_tests)
  # The ride badges (include/RideBadges.h, platform/ride_badges.cpp): which rides get the GOLD and PS1 tags, none with
  # "Show Ride Badges" off, and the tag's place in the Buy Ride picture. Only with the port's fixes.
  if(TPW_PORT_FIX)
    add_executable(tpw_ride_badges_tests platform/tests/ride_badges_tests.cpp)
    target_include_directories(tpw_ride_badges_tests PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
    target_link_libraries(tpw_ride_badges_tests PRIVATE tpw_platform)
    add_test(NAME ride_badges COMMAND tpw_ride_badges_tests)
  endif()
  # The online server client (platform/online_boss.cpp) against a fake connection: the framing, every request and every reply
  # parser, byte for byte, with malformed and oversized replies.
  add_executable(tpw_online_boss_tests platform/tests/online_boss_tests.cpp)
  target_include_directories(tpw_online_boss_tests PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
  target_link_libraries(tpw_online_boss_tests PRIVATE tpw_platform)
  add_test(NAME online_boss COMMAND tpw_online_boss_tests)
  # The online sign-up (platform/online_signup.cpp) against a fake connection: the request, every reply, the name rules, the
  # job and the texts. Its popup (platform/online_signup_ui.cpp, the fix `signup`) with the game's window calls as fakes.
  add_executable(tpw_online_signup_tests platform/tests/online_signup_tests.cpp)
  target_include_directories(tpw_online_signup_tests PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
  target_link_libraries(tpw_online_signup_tests PRIVATE tpw_platform)
  add_test(NAME online_signup COMMAND tpw_online_signup_tests)
  add_executable(tpw_online_signup_ui_tests platform/tests/online_signup_ui_tests.cpp)
  target_include_directories(tpw_online_signup_ui_tests PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
  target_link_libraries(tpw_online_signup_ui_tests PRIVATE tpw_platform)
  add_test(NAME online_signup_ui COMMAND tpw_online_signup_ui_tests)
  # The online server's park downloads (platform/online_ftp.cpp) against fake connections: each FTP step, its errors and bounds,
  # and the park checks with malformed parks built in the test.
  add_executable(tpw_online_ftp_tests platform/tests/online_ftp_tests.cpp)
  target_include_directories(tpw_online_ftp_tests PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
  target_link_libraries(tpw_online_ftp_tests PRIVATE tpw_platform ZLIB::ZLIB)
  add_test(NAME online_ftp COMMAND tpw_online_ftp_tests)
  # The same client against a running online server (decomp-onlinesrv's tpw-online) on this computer: skips (77) unless
  # TPW_ONLINE_SERVER names one.
  # The numbers on the online globe's chat satellites through the real code (tests/host_chat_satellites.cpp), with only the
  # connection a fake. It is linked as tpw_launcher is, with its own stand-ins, so a body that is empty or missing answers 0 here as
  # it does in the game instead of being a link error or a test's fake.
  if(TARGET tpw_launcher)
    add_library(tpw_host_chat_satellites_objects OBJECT ${TPW_DECOMP_DIR}/tests/host_chat_satellites.cpp launcher/pending_manual.cpp)
    target_include_directories(tpw_host_chat_satellites_objects PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform launcher)
    set(TPW_CHAT_SATELLITES_STUBS ${CMAKE_CURRENT_BINARY_DIR}/stubs_chat_satellites.cpp)
    add_custom_command(
      OUTPUT ${TPW_CHAT_SATELLITES_STUBS}
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/launcher/gen_stubs.py
              --out ${TPW_CHAT_SATELLITES_STUBS} --include-dir ${TPW_INCLUDE_DIR}
              --nm ${CMAKE_NM} --provides $<TARGET_FILE:SDL3-static> --provides $<TARGET_FILE:tpw_zlib> --provides $<TARGET_FILE:tpw_libjpeg>
              --provides $<TARGET_FILE:tpw_launcher_ui> --provides $<TARGET_FILE:tpw_assets> ${TPW_STUB_SYSLIBS} ${TPW_MOVIE_SYSLIBS}
              $<TARGET_FILE:tpw> $<TARGET_FILE:tpw_platform> $<TARGET_OBJECTS:tpw_host_chat_satellites_objects>
      DEPENDS tpw tpw_platform SDL3-static tpw_zlib tpw_libjpeg tpw_launcher_ui tpw_assets $<TARGET_OBJECTS:tpw_host_chat_satellites_objects>
              launcher/gen_stubs.py
      COMMAND_EXPAND_LISTS VERBATIM
      COMMENT "Generating stand-ins for the chat satellites test")
    add_executable(tpw_host_chat_satellites $<TARGET_OBJECTS:tpw_host_chat_satellites_objects> ${TPW_CHAT_SATELLITES_STUBS})
    target_include_directories(tpw_host_chat_satellites PRIVATE ${TPW_INCLUDE_DIR} launcher)
    target_link_libraries(tpw_host_chat_satellites PRIVATE tpw tpw_platform tpw_launcher_ui)
    if(TPW_LINUX)
      target_link_libraries(tpw_host_chat_satellites PRIVATE $<TARGET_FILE:tpw>)
    endif()
    if(WIN32)
      target_link_options(tpw_host_chat_satellites PRIVATE -Wl,--export-all-symbols)
    endif()
    add_test(NAME chat_satellites COMMAND tpw_host_chat_satellites)
  endif()
  # Starting the game when no sound device opens (tests/host_no_sound_device.cpp): the real SOUND_Init down to SoundDevice, linked
  # as tpw_launcher is, with its own stand-ins. It needs no slicer, so Windows CI runs it. The original crashes there, so ctest
  # runs it only with the port's fixes; a build without them makes it, and it fails.
  if(TARGET tpw_launcher)
    add_library(tpw_host_no_sound_device_objects OBJECT ${TPW_DECOMP_DIR}/tests/host_no_sound_device.cpp launcher/pending_manual.cpp)
    target_include_directories(tpw_host_no_sound_device_objects PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform launcher)
    target_link_libraries(tpw_host_no_sound_device_objects PRIVATE SDL3::SDL3)
    set(TPW_NO_SOUND_DEVICE_STUBS ${CMAKE_CURRENT_BINARY_DIR}/stubs_no_sound_device.cpp)
    add_custom_command(
      OUTPUT ${TPW_NO_SOUND_DEVICE_STUBS}
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/launcher/gen_stubs.py
              --out ${TPW_NO_SOUND_DEVICE_STUBS} --include-dir ${TPW_INCLUDE_DIR}
              --nm ${CMAKE_NM} --provides $<TARGET_FILE:SDL3-static> --provides $<TARGET_FILE:tpw_zlib> --provides $<TARGET_FILE:tpw_libjpeg>
              --provides $<TARGET_FILE:tpw_launcher_ui> --provides $<TARGET_FILE:tpw_assets> ${TPW_STUB_SYSLIBS} ${TPW_MOVIE_SYSLIBS}
              $<TARGET_FILE:tpw> $<TARGET_FILE:tpw_platform> $<TARGET_OBJECTS:tpw_host_no_sound_device_objects>
      DEPENDS tpw tpw_platform SDL3-static tpw_zlib tpw_libjpeg tpw_launcher_ui tpw_assets $<TARGET_OBJECTS:tpw_host_no_sound_device_objects>
              launcher/gen_stubs.py
      COMMAND_EXPAND_LISTS VERBATIM
      COMMENT "Generating stand-ins for the no sound device test")
    add_executable(tpw_host_no_sound_device $<TARGET_OBJECTS:tpw_host_no_sound_device_objects> ${TPW_NO_SOUND_DEVICE_STUBS})
    target_include_directories(tpw_host_no_sound_device PRIVATE ${TPW_INCLUDE_DIR} launcher)
    target_link_libraries(tpw_host_no_sound_device PRIVATE tpw tpw_platform tpw_launcher_ui)
    if(TPW_LINUX)
      target_link_libraries(tpw_host_no_sound_device PRIVATE $<TARGET_FILE:tpw>)
    endif()
    if(WIN32)
      target_link_options(tpw_host_no_sound_device PRIVATE -Wl,--export-all-symbols)
    endif()
    if(TPW_PORT_FIX)
      add_test(NAME no_sound_driver COMMAND tpw_host_no_sound_device driver)
      add_test(NAME no_sound_device COMMAND tpw_host_no_sound_device open)
    endif()
    foreach(mode silent arrive late lost stale mute stress)
      add_test(NAME no_sound_${mode} COMMAND tpw_host_no_sound_device ${mode})
      set_tests_properties(no_sound_${mode} PROPERTIES TIMEOUT 120)
    endforeach()
  endif()
  add_executable(tpw_online_boss_e2e platform/tests/online_boss_e2e.cpp)
  target_include_directories(tpw_online_boss_e2e PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
  target_link_libraries(tpw_online_boss_e2e PRIVATE tpw_platform)
  add_test(NAME online_boss_e2e COMMAND tpw_online_boss_e2e)
  set_tests_properties(online_boss_e2e PROPERTIES SKIP_RETURN_CODE 77)

  if(TPW_LINUX)
    # The Vulkan renderer against the system's OpenGL, through the game's OpenGL calls (platform/tests/renderer_tests.cpp):
    # needs a display and a Vulkan device (CI runs it under Xvfb with Mesa's software ones), skips (77) without them.
    add_executable(tpw_renderer_tests platform/tests/renderer_tests.cpp)
    target_include_directories(tpw_renderer_tests PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
    target_link_libraries(tpw_renderer_tests PRIVATE tpw_platform)
    add_test(NAME renderer COMMAND tpw_renderer_tests)
    set_tests_properties(renderer PROPERTIES SKIP_RETURN_CODE 77)
  endif()

  # Where the intro films are drawn (include/platform/filmbox.h): pure arithmetic, no game or window.
  add_executable(tpw_host_filmbox ${TPW_DECOMP_DIR}/tests/host_filmbox.cpp)
  target_include_directories(tpw_host_filmbox PRIVATE ${TPW_INCLUDE_DIR})
  add_test(NAME filmbox COMMAND tpw_host_filmbox)
  if(TPW_MOVIES)
    # A PC CD's film (an EA .TGQ file) made in the test, opened and decoded the way platform/quicktime.cpp plays one:
    # the ea demuxer, EA TQI pictures, EA ADPCM sound. No game file, no window.
    add_executable(tpw_tgq_tests platform/tests/tgq_tests.cpp)
    target_link_libraries(tpw_tgq_tests PRIVATE tpw_ffmpeg)
    add_test(NAME tgq COMMAND tpw_tgq_tests)
  endif()
  # An older build's language files read by the later program (include/platform/old_text_tables.h): its UITEXT and
  # CHAT_COMMANDS made the later build's, its fonts found by file name. No game, no files.
  add_executable(tpw_host_old_text_tables ${TPW_DECOMP_DIR}/tests/host_old_text_tables.cpp)
  target_include_directories(tpw_host_old_text_tables PRIVATE ${TPW_INCLUDE_DIR})
  add_test(NAME old_text_tables COMMAND tpw_host_old_text_tables)
  # A sprite of the particle sprite file read as the texture a particle is drawn with (include/tb_particle.h, include/tb_starfield.h):
  # the record's words line up on the host, so a textured particle has its width. No game, no files.
  add_executable(tpw_host_particle_texture ${TPW_DECOMP_DIR}/tests/host_particle_texture.cpp)
  target_include_directories(tpw_host_particle_texture PRIVATE ${TPW_INCLUDE_DIR} ${TPW_DECOMP_DIR}/platform)
  add_test(NAME particle_texture COMMAND tpw_host_particle_texture)

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
  # launcher/net/: the only network code of the port (the update check's questions, over the system's curl). Nothing else
  # may talk to the internet: devtools/check_offline.py holds the tree to that (port_CONTRIBUTING.md, "Playable offline").
  add_library(tpw_launcher_ui STATIC ${TPW_LAUNCHER_UI_SOURCES} launcher/net/net.cpp ${TPW_UI_FONTS_CPP})
  target_include_directories(tpw_launcher_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/launcher/ui ${CMAKE_CURRENT_SOURCE_DIR}/launcher/net)
  target_include_directories(tpw_launcher_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb ${TPW_DECOMP_DIR}/platform)   # path_text.h
  target_link_libraries(tpw_launcher_ui PUBLIC SDL3::SDL3 tpw_assets ZLIB::ZLIB)
  # The launcher's version: launcher/VERSION.txt is the one place it is written (not launcher/VERSION: launcher/ is an
  # include folder, and on a case-blind disk that name would stand in for the C++ header <version>). The window's corner,
  # the About box, --version and the log file's first line all take it from here. A release is X.Y.Z, and only its tag
  # (vX.Y.Z) says so; between releases main says the next one with -dev (0.0.4-dev), so a build of main never passes for a
  # release. The Windows version resource and the macOS Info.plist take the numbers only (TPW_LAUNCHER_VERSION_NUMBER).
  file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/launcher/VERSION.txt TPW_LAUNCHER_VERSION LIMIT_COUNT 1)
  if(NOT TPW_LAUNCHER_VERSION MATCHES "^([0-9]+\\.[0-9]+\\.[0-9]+)(-dev)?$")
    message(FATAL_ERROR "launcher/VERSION.txt: \"${TPW_LAUNCHER_VERSION}\" is not a version like 0.1.0 or 0.2.0-dev")
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
    # The executable's resources (launcher/win/launcher.rc.in): the version resource (FileVersion, ProductVersion,
    # ProductName and a FileDescription of the name with its version, which Explorer's Details and the Task Manager show)
    # and the icon Explorer, the taskbar and the Start menu shortcut show for tpw_launcher.exe. The icon is the launcher's
    # stand-in (launcher/app_icon_standin.cpp, the same code the window shows before a game is imported), written as a
    # multi-size .ico into the build folder by tpw_make_ico (launcher/win/make_ico.cpp) at build time: no picture file is
    # in the tree. The manifest (launcher/win/tpw_launcher.manifest) makes UTF-8 the process's code page, for the paths of a
    # player's name outside plain ASCII (platform/path_text.h). Compiled by the toolchain's resource compiler (llvm-mingw's
    # windres, or MSVC's rc); without one the build goes on without the resources, as before, and build.yml's check fails.
    include(CheckLanguage)
    check_language(RC)
    if(CMAKE_RC_COMPILER)
      enable_language(RC)
      add_executable(tpw_make_ico launcher/win/make_ico.cpp launcher/app_icon_standin.cpp)
      target_include_directories(tpw_make_ico PRIVATE launcher)
      target_link_libraries(tpw_make_ico PRIVATE tpw_assets)
      set(TPW_LAUNCHER_ICO ${CMAKE_CURRENT_BINARY_DIR}/tpw_launcher.ico)
      add_custom_command(
        OUTPUT ${TPW_LAUNCHER_ICO}
        COMMAND tpw_make_ico ${TPW_LAUNCHER_ICO}
        DEPENDS tpw_make_ico
        VERBATIM
        COMMENT "Drawing the launcher's icon")
      set(TPW_LAUNCHER_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/launcher/win/tpw_launcher.manifest)
      configure_file(launcher/win/launcher.rc.in ${CMAKE_CURRENT_BINARY_DIR}/launcher.rc @ONLY)
      # the .ico is listed as a source so that its command belongs to the target; the .rc is compiled again when it or the
      # manifest changes
      set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/launcher.rc PROPERTIES OBJECT_DEPENDS "${TPW_LAUNCHER_ICO};${TPW_LAUNCHER_MANIFEST}")
      target_sources(tpw_launcher PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/launcher.rc ${TPW_LAUNCHER_ICO})
    else()
      message(STATUS "tpw: no resource compiler, tpw_launcher.exe has no version resource and no icon")
    endif()
  endif()
  if(APPLE)
    # The launcher's Info.plist (launcher/macos/Info.plist.in). The bare tpw_launcher, the one file the scripts, the README,
    # the installer and CI all run (build/tpw_launcher), carries it embedded in its __TEXT,__info_plist section: that names
    # the program in Cmd-Tab and Force Quit (CFBundleName, "STP/TPW Launcher"; the maintainer, 2026-10-25), gives it its identifier,
    # and is the plist of the application bundle the launcher makes beside itself at run time and runs from, which names it
    # in the Dock (launcher/app_bundle.cpp). Its icon is set at run time too (launcher/app_icon.cpp). TPW_BUNDLE (off by
    # default) builds a .app bundle around it instead, with the same plist.
    option(TPW_BUNDLE "Build the macOS launcher as an .app bundle (launcher/macos/Info.plist.in)" OFF)
    set(TPW_BUNDLE_IDENTIFIER "org.tpw-port.launcher" CACHE STRING "The launcher's CFBundleIdentifier")
    configure_file(launcher/macos/Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)
    if(TPW_BUNDLE)
      set_target_properties(tpw_launcher PROPERTIES MACOSX_BUNDLE ON
                            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist)
    else()
      target_link_options(tpw_launcher PRIVATE "LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
      set_property(TARGET tpw_launcher APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/Info.plist)
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
  # The launcher's reads and writes of the game's settings files (launcher/ui/game_settings.h).
  add_executable(tpw_game_settings_tests launcher/ui/tests/game_settings_tests.cpp)
  target_link_libraries(tpw_game_settings_tests PRIVATE tpw_launcher_ui)
  add_test(NAME game_settings COMMAND tpw_game_settings_tests)
  # The same files against the game's own code: the real CPlayerSettings load and save, the file calls and the sound table being
  # the test's stand-ins, and the real size lists (launcher/game_size_rules.h). Made only with the port's fixes, as the
  # Settings box's Game options are.
  if(TPW_PORT_FIX)
    add_executable(tpw_settings_sync_test launcher/ui/tests/settings_sync_game_test.cpp ${TPW_DECOMP_DIR}/src/game/TPSettings.cpp)
    target_include_directories(tpw_settings_sync_test PRIVATE ${TPW_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/launcher
                               ${CMAKE_CURRENT_SOURCE_DIR}/platform ${TPW_DECOMP_DIR}/platform)
    target_link_libraries(tpw_settings_sync_test PRIVATE tpw_launcher_ui)
    tpw_slice(TARGET tpw_settings_sync_test NAME settings_sync KEEP
      __ZN15CPlayerSettings4InitEv   # CPlayerSettings::Init()
      __ZN15CPlayerSettingsD1Ev   # CPlayerSettings::~CPlayerSettings()
      __ZN15CPlayerSettingsD2Ev
      __ZN15CPlayerSettings10LoadGlobalER23TbDynamicStringTemplateIcE   # CPlayerSettings::LoadGlobal
      __ZN15CPlayerSettings10SaveGlobalER23TbDynamicStringTemplateIcE   # CPlayerSettings::SaveGlobal
      __ZN15CPlayerSettings15SerialiseGlobalEPvij   # CPlayerSettings::SerialiseGlobal
      __ZN15CPlayerSettings14SerialiseLocalEPvij   # CPlayerSettings::SerialiseLocal
      __ZN15CPlayerSettings13SetResolutionENS_11eRESOLUTIONE)   # CPlayerSettings::SetResolution
    if(tpw_platform_tests_skipped)
      set_target_properties(tpw_settings_sync_test PROPERTIES EXCLUDE_FROM_ALL ON)
    else()
      add_test(NAME settings_sync COMMAND tpw_settings_sync_test)
    endif()
  endif()
  # The network module (launcher/net/) and the update check (launcher/ui/updater.h): no answer, fast, when nothing
  # answers; junk refused; the Update button's runs of the script. Its curl is the test program itself (TPW_NET_CURL), so
  # it needs no internet and runs where networking is blocked (the CI's offline runs).
  add_executable(tpw_update_tests launcher/net/tests/update_tests.cpp)
  target_link_libraries(tpw_update_tests PRIVATE tpw_launcher_ui)
  # The online.txt to publish in tpw-port (at its root there, devtools/split here) parses to the built-in address.
  foreach(online_txt online.txt devtools/split/online.txt)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${online_txt})
      target_compile_definitions(tpw_update_tests PRIVATE TPW_ONLINE_TXT="${CMAKE_CURRENT_SOURCE_DIR}/${online_txt}")
      break()
    endif()
  endforeach()
  add_test(NAME update COMMAND tpw_update_tests)
  # The game's own application on a Mac (launcher/game_app.h), the part that runs everywhere: the bundles' names, the
  # Info.plist the launcher writes for them, the arguments both ways and the .icns it writes from the icon.
  add_executable(tpw_game_app_tests launcher/tests/game_app_tests.cpp launcher/game_app.cpp)
  target_include_directories(tpw_game_app_tests PRIVATE launcher)
  target_link_libraries(tpw_game_app_tests PRIVATE tpw_assets)
  add_test(NAME game_app COMMAND tpw_game_app_tests)
  # The launcher's icon where the installer put it (launcher/installed_icon.h): which application in Applications is the
  # installer's for this launcher, the Start menu shortcut's icon file, and the icon written again only when it changed.
  add_executable(tpw_installed_icon_tests launcher/tests/installed_icon_tests.cpp launcher/installed_icon.cpp launcher/game_app.cpp)
  target_include_directories(tpw_installed_icon_tests PRIVATE launcher)
  target_link_libraries(tpw_installed_icon_tests PRIVATE tpw_assets)
  if(WIN32)
    target_link_libraries(tpw_installed_icon_tests PRIVATE shell32 ole32)   # TellExplorer
  endif()
  add_test(NAME installed_icon COMMAND tpw_installed_icon_tests)
  # Which icon.png the launcher shows: the run's data folder's, the per-user folder's, or the stand-in (launcher/app_icon.h).
  add_test(NAME launcher_icon COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/launcher/tests/icon_choice_test.py $<TARGET_FILE:tpw_launcher>)
  # No function of the game or the launcher is looked up by its symbol at run time (a Linux launcher exports none of its
  # own, so the lookup finds nothing there and Save crashed; Discord, xoob, 2026-11-01): devtools/check_own_dlsym.py.
  add_test(NAME own_dlsym COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/devtools/check_own_dlsym.py)
  # A path's string() may throw on Windows under the UTF-8 process code page: tpw::PathText in its place (platform/path_text.h).
  add_test(NAME path_text COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/devtools/check_path_text.py)
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
