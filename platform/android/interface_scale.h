#pragma once

#include <cstdint>

class InterfaceWindow;

namespace tpw::mobile {
void SetInterfaceScale(int percent);
void FitInterfaceForMenu();
void PrepareInterfaceForBuild(const InterfaceWindow* parent, const int16_t* script);
void UpdateInterfaceScale();
bool InterfaceNeedsControlSpace();
}
