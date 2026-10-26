// make_ico.cpp: tpw_make_ico FILE writes the launcher's stand-in icon (launcher/app_icon_standin.cpp) as the multi-size
// .ico FILE. CMakeLists.txt builds and runs it on Windows, at build time, to make the icon of tpw_launcher.exe's icon
// resource (launcher/win/launcher.rc.in), so that no picture file is in the tree and the stand-in is drawn in one place.
#include <cstdio>
#include <string>

#include "../app_icon_standin.h"
#include "icon.h"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: tpw_make_ico FILE.ico\n");
        return 2;
    }
    std::string err;
    if (!tpw::assets::WriteIconIco(argv[1], tpw::launcher::StandInAppIcon(), err)) {
        std::fprintf(stderr, "tpw_make_ico: %s\n", err.c_str());
        return 1;
    }
    return 0;
}
