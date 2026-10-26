// app_icon_standin.h: the launcher's stand-in icon, drawn in code (app_icon.h): the icon until a game is imported. It is
// in a file of its own, needing only assets/icon.h and ui/theme.h, so that the Windows build can draw it without SDL into
// the launcher's icon resource (launcher/win/make_ico.cpp).
#pragma once
#include "icon.h"

namespace tpw::launcher {

// The stand-in, 256 x 256, 8-bit RGBA with straight alpha: a rounded tile with the launcher's sky, a lime lawn, a sun
// and a cloud, outlined in ink, as the launcher window's panels are.
tpw::assets::IconImage StandInAppIcon();

}  // namespace tpw::launcher
