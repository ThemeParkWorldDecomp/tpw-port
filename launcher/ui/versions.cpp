// versions.cpp: the Licenses box's libraries (versions.h). The libraries' headers are included here only, so
// libjpeg's macros (GLOBAL, LOCAL and the like) never meet the rest of the launcher's code.
#include "versions.h"

#include <cstddef>
#include <cstdio>

#include <SDL3/SDL.h>
#include <archive.h>
#include <lzma.h>
#include <zlib.h>
extern "C" {
#include <jpeglib.h>
}
#if TPW_HAVE_FFMPEG
extern "C" {
#include <libavutil/version.h>
}
#endif

#include "text.h"

#ifndef TPW_STB_TRUETYPE_VERSION
#error "CMakeLists.txt passes the stb headers' versions (TPW_STB_TRUETYPE_VERSION, TPW_STB_RECT_PACK_VERSION)"
#endif

namespace tpw::ui {

namespace licensedata {   // CMakeLists.txt writes them (ui_licenses.cpp), from the license files in third_party/
extern const char kSdl[], kZlib[], kLibarchive[], kXz[], kStb[], kFredoka[], kNunito[], kLgpl[];
}  // namespace licensedata

namespace {
std::string Dotted(int major, int minor, int micro) {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(micro);
}

// libjpeg numbers its releases 6a, 6b, ... and its JPEG_LIB_VERSION as major * 10 + the letter (a = 1): 62 is 6b.
std::string JpegRelease(int v) {
    std::string out = std::to_string(v / 10);
    if (v % 10 > 0) out += static_cast<char>('a' + v % 10 - 1);
    return out;
}
}  // namespace

std::vector<MadeWith> MadeWithRows(const Text& text) {
    int sdl = SDL_GetVersion();
    using namespace licensedata;
    std::vector<MadeWith> rows = {
        {"SDL", Dotted(SDL_VERSIONNUM_MAJOR(sdl), SDL_VERSIONNUM_MINOR(sdl), SDL_VERSIONNUM_MICRO(sdl)), "zlib License",
         kSdl},
        {"zlib", zlibVersion(), "zlib License", kZlib},
        // The IJG license asks a program shipped without libjpeg's source to say these words in its documentation.
        {"libjpeg (the game's pictures)", JpegRelease(JPEG_LIB_VERSION), "IJG License",
         "This software is based in part on the work of the Independent JPEG Group.\n"
         "libjpeg is copyright (C) 1991-1998, Thomas G. Lane. It is provided \"as is\", with no warranty."},
        {"libarchive (7-Zip and RAR archives)", ARCHIVE_VERSION_ONLY_STRING, "BSD 2-Clause License", kLibarchive},
        {"liblzma (XZ Utils)", LZMA_VERSION_STRING, "Zero-Clause BSD", kXz},
        {"stb_truetype, stb_rect_pack", std::string(TPW_STB_TRUETYPE_VERSION) + ", " + TPW_STB_RECT_PACK_VERSION,
         "MIT License or public domain", kStb},
        {"Fredoka (headings)", text.FontVersion(Face::Heading), "SIL Open Font License 1.1", kFredoka},
        {"Nunito (text)", text.FontVersion(Face::Body), "SIL Open Font License 1.1", kNunito},
    };
#if TPW_HAVE_FFMPEG
    // FFmpeg is built into the program, so the LGPL asks for its text and for the way to build the program again with a
    // changed FFmpeg: the port's whole source, FFmpeg's included, is published where the program is.
    rows.push_back({"FFmpeg libavutil (the game's films)",
                    Dotted(LIBAVUTIL_VERSION_MAJOR, LIBAVUTIL_VERSION_MINOR, LIBAVUTIL_VERSION_MICRO),
                    "GNU LGPL 2.1 or later",
                    std::string("This program uses FFmpeg (ffmpeg.org), built into it from FFmpeg's own source, "
                                "unchanged. FFmpeg is licensed under the GNU Lesser General Public License, version 2.1 "
                                "or later, whose text follows. The program's complete source, with the FFmpeg source it "
                                "was built from (third_party/ffmpeg) and the build files to build it again with a changed "
                                "FFmpeg, is published with every release at github.com/ThemeParkWorldDecomp/tpw-port.\n\n") +
                        kLgpl});
#endif
    return rows;
}

}  // namespace tpw::ui
