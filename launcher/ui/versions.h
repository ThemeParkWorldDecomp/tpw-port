// versions.h: the libraries the launcher and the game are built with, as the Licenses box lists them (the About / Game
// data box's Licenses button). Every version in it is read from the library or font itself (the headers of the copy the
// build compiles, the library at run time, a font's own name table), never written here; every notice is the library's
// own license file, compiled in by CMakeLists.txt (ui_licenses.cpp).
#pragma once

#include <string>
#include <vector>

namespace tpw::ui {

class Text;

struct MadeWith {
    std::string name, version, license;
    std::string notice;   // what the license asks to go with the program: its text, and any words it asks for
};

// The rows in the order the Licenses box shows them. FFmpeg is listed only in a build that plays the films (TPW_MOVIES).
std::vector<MadeWith> MadeWithRows(const Text& text);

}  // namespace tpw::ui
