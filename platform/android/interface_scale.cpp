#include "interface_scale.h"

#include <algorithm>

#include "Game.h"
#include "InterfaceWindow.h"
#include "TPAdvisor.h"
#include "TPMain.h"
#include "UI_Base.h"
#include "ui_DrawObject.h"
#include "ui_main.h"

namespace tpw::mobile {
namespace {
float gPreferredScale = 1.0f;

bool PanelIsVisible() {
    return gUIPanelWindow != nullptr && (gUIPanelWindow->GetAttr() & 1) != 0;
}

float PanelScale(int32_t spanWide, int32_t spanHigh, const InterfaceWindow* parent) {
    constexpr float margin = 32.0f;
    const float width = spanWide + margin;
    const float height = spanHigh + margin;
    const float baseWidth = gUIWideAspect > 0.0f ? UI_UnitWidthFor(gScreenWidth, gScreenHeight) : 0x800;
    float scale = std::min(baseWidth / width, 0x600 / height);
    if (parent == gGameWindow && spanWide >= 0x400 &&
        gMainPanel != nullptr && (gMainPanel->GetAttr() & 1) != 0) {
        const CoolRect& toolbar = gMainPanel->mRect;
        const float sideBySide = baseWidth / (width + toolbar.mRight);
        const float stacked = 0x600 / (height + toolbar.mBottom - toolbar.mTop);
        scale = std::min(scale, std::max(sideBySide, stacked));
    }
    return scale;
}

void ApplyScale(float scale) {
    if (gUIScale == scale || gUIFullWindows == nullptr || gUIRootWindow == nullptr || gGameWindow == nullptr || !gUIInterfaceSizeOn ||
        gUIRealScreenWidth <= 0 || gUIRealScreenHeight <= 0) return;
    const int16_t oldWide = gUIUnitWidth, oldHigh = gUIUnitHeight;
    const int32_t pointerPixelX = gUIPointerX * gUIRealScreenWidth / 0x800;
    const int32_t pointerPixelY = gUIPointerY * gUIRealScreenHeight / 0x600;
    gUIScale = scale;
    UI_SetRealScreenRez(static_cast<int16_t>(gScreenWidth), static_cast<int16_t>(gScreenHeight));
    UI_FollowUnits(oldWide, oldHigh);
    PortFollowUnits();
    PortAdvisorFollowUnits();
    InterfaceDrawObjectSprite::PortFollowScreen();
    UI_DevicePointerMove(static_cast<int16_t>(pointerPixelX * 0x800 / gUIRealScreenWidth),
                         static_cast<int16_t>(pointerPixelY * 0x600 / gUIRealScreenHeight));
}
}

void SetInterfaceScale(int percent) {
    switch (percent) {
        case 70:
        case 85:
        case 115:
        case 125: gPreferredScale = static_cast<float>(percent) / 100.0f; break;
        default: gPreferredScale = 1.0f; break;
    }
}

void FitInterfaceForMenu() {
    if (gPreferredScale != 1.0f) ApplyScale(1.0f);
}

void PrepareInterfaceForBuild(const InterfaceWindow* parent, const int16_t* script) {
    if (gPreferredScale == 1.0f || gGameWindow == nullptr) return;
    // Full-screen screens such as the map and Options cache geometry during construction.
    if (gGame.mF3C != 1) {
        ApplyScale(1.0f);
        return;
    }
    if (parent != gGameWindow || script[0] != 0) return;

    // The first create-window record has kind, attributes and ID before its rectangle.
    const int32_t width = script[8] - script[6];
    const int32_t height = script[9] - script[7];
    if (width < 0x400 || height <= 0) return;
    ApplyScale(std::clamp(PanelScale(width, height, parent), std::min(1.0f, gPreferredScale), gPreferredScale));
}

bool InterfaceNeedsControlSpace() {
    return gPreferredScale > 1.0f && (gGame.mF3C != 1 || PanelIsVisible());
}

void UpdateInterfaceScale() {
    if (gPreferredScale == 1.0f || gGameWindow == nullptr || !gUIInterfaceSizeOn) return;
    if (gGame.mF3C != 1) {
        ApplyScale(1.0f);
        return;
    }
    if (gUICaptureDevice != nullptr || UI_GetButtonFlags() != 0) return;

    float scale = gPreferredScale;
    if (PanelIsVisible()) {
        const CoolRect& rect = gUIPanelWindow->mRect;
        scale = std::min(scale, PanelScale(rect.mRight - rect.mLeft, rect.mBottom - rect.mTop, gUIPanelWindow->mParent));
    }
    ApplyScale(std::max(std::min(1.0f, gPreferredScale), scale));
}
}
