// mobile_hooks.h: the game's calls to the port's phone and tablet support (platform/android/ in tpw-port). On Android they
// are platform/android/interface_scale.h and mobile_controls.h; elsewhere they do nothing, so desktop's game is unchanged.
#pragma once
// TPW_PORT_FIX androiduiscale tpw::mobile::kEnlargedInterface/FitInterfaceForMenu/PrepareInterfaceForBuild/UpdateInterfaceScale | Mac: no such header | Port: on Android the park's interface can be enlarged (platform/android/interface_scale.cpp); elsewhere kEnlargedInterface is false and the calls do nothing | Why: on a phone the park's buttons and text must be large enough to touch and read; contributed by mario910 (tpw-port PR #135)
#include <cstdint>
#ifdef __ANDROID__
#include "android/interface_scale.h"
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
#endif
}  // namespace tpw::mobile
// TPW_PORT_FIX androidcamera tpw::mobile::CameraAxes | Mac: no such header | Port: on Android the right stick or the touch screen's camera control (platform/android/mobile_controls.cpp); elsewhere it reads 0 | Why: a phone or tablet has no camera keys; contributed by mario910 (tpw-port PR #135)
#ifdef __ANDROID__
#include "android/mobile_controls.h"
#else
namespace tpw::mobile {
inline void CameraAxes(float& x, float& y) { x = y = 0.0f; }
}  // namespace tpw::mobile
#endif
