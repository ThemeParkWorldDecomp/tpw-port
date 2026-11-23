#pragma once

#include <cstdint>

#ifdef __ANDROID__
#include "android/interface_scale.h"
#include "android/mobile_controls.h"
#endif

class InterfaceWindow;

namespace tpw::mobile {
#ifdef __ANDROID__
inline constexpr bool kEnlargedInterface = true;
#else
inline constexpr bool kEnlargedInterface = false;
inline void FitInterfaceForMenu() {}
inline void PrepareInterfaceForBuild(const InterfaceWindow*, const int16_t*) {}
inline void UpdateInterfaceScale() {}
inline void CameraAxes(float& x, float& y) { x = y = 0.0f; }
#endif
}
