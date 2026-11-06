// ui_gamesize.cpp: the port's live change of the game's size (no Mac code: the Mac changes size by restarting).

#include "InterfaceScrollBar.h"
#include "InterfaceWindow.h"
#include "MainWindowCallback.h"
#include "platform/samsutils.h"
#include "TPAdvisor.h"
#include "TPMain.h"
#include "TPSettings.h"
#include "Text.h"
#include "UI_Base.h"
#include "tb_ddraw.h"
#include "tb_matrix.h"
#include "tb_resource_display.h"
#include "ui_controls.h"
#include "ui_DrawObject.h"
#include "ui_main.h"
#include "ui_mainmenu.h"

extern InterfaceWindow* gGameWindow;           // src/game/ui/ui_main.cpp
extern float gGameWindowCentreX;
extern float gGameWindowCentreY;
extern float gGameWindowHalfWidth;
extern float gGameWindowHalfHeight;
extern uint8_t gOnlinePanelShown;              // src/game/ui/ui_onlinePanel.cpp
extern CMatrix gScreenMatrix;                  // src/engine/tb_matrix.cpp
extern CDirectDraw* gei_DDraw;
namespace options_callee {
void WindowSetValue(InterfaceWindow* window, int32_t value);   // src/game/ui/ui_parkStatus.cpp
}

// TPW_PORT_FIX resslider PortShowShape | Mac: (none) | Port: the size slider's stops become those of a shape, with the thumb on the stop closest to the size asked for, which the game then changes to, and that shape's button stands down | Why: the slider is sizes only, the buttons are the shape
void PortShowShape(int32_t shape) {
    gUISizeStopCount = UI_SizeStops(shape, gPortPanelWide, gPortPanelHigh, gPortScreenWide, gPortScreenHigh, gUISizeStops);
    const int32_t stop = UI_ClosestStop(gUISizeStops, gUISizeStopCount, gPortWantedWide, gPortWantedHigh, gPortLargestGameSize);
    auto* slider = static_cast<InterfaceScrollBar*>(gSetOnOffRoot->GetWindow(0x1d4d5));
    slider->SetMinMax(0, gUISizeStopCount - 1);
    options_callee::WindowSetValue(slider, stop);
    slider->ReCalcThumbPos(-2, 0);  // (the thumb goes to the value even if the new range leaves the value as it was)
    options_panel::DoScroll(0x1d4d5, stop);
    for (int32_t i = 0; i < kUISizeShapeCount; ++i)
        static_cast<InterfaceButton*>(gSetOnOffRoot->GetWindow(kPortShapeButton + i))->SetState(i == shape ? 1 : 0, 1);
}


// TPW_PORT_FIX resslider PortMakeSizeRow | Mac: no such windows | Port: the exact size, beside the slider, and a button for each shape, all in the row's free room, in the look of the game's column buttons | Why: the slider is sizes only, the buttons are the shape
void PortMakeSizeRow(InterfaceFont* font, int32_t fontIndex, int16_t z) {
    InterfaceWindow* readout = gSetOnOffRoot->GetWindow(0x1d4d5)->CreateObject(1, 1, kPortSizeReadout, 745, 375, 925, 420);
    readout->SetClientDrawObject(new InterfaceTextObjectSprite(nullptr));
    readout->SetFontColour(0, 0, 0, 0xff);
    readout->SetFont(font);
    readout->SetFontJust(2, 1);
    readout->SetClientDrawText(nullptr);
    static char16_t kShapeNames[kUISizeShapeCount][6] = {u"4:3", u"16:10", u"16:9"};
    for (int32_t i = 0; i < kUISizeShapeCount; ++i) {
        const int16_t left = static_cast<int16_t>(405 + i * 114);
        InterfaceWindow* button = gSetOnOffRoot->CreateObject(2, 1, kPortShapeButton + i, left, 363, static_cast<int16_t>(left + 108), 432);
        MAKECOLUMNBUTTON(button, kShapeNames[i], fontIndex, 0xb0, 0xc8, 0xd8, -1);
        static_cast<InterfaceButton*>(button)->SetTextDownColour({0xff, 0xf0, 0x40, 0xff});  // the shape on show stands down
        button->setZ(z);
    }
}


// TPW_PORT_FIX resslider PortChangeGameSize | Mac: (none): the size is read once at the start, by InitialiseGameStage3 and what it starts | Port: what was read at the start follows here: the game's size and centres, the UI's pixel box and font scale, the font set, gei_Display, the framebuffers and surfaces, the screen matrix's aspect, the game window's pixels and every interface surface | Why: the options panel's slider changes the size live
void PortChangeGameSize(int32_t width, int32_t height) {
    if (width == gScreenWidth && height == gScreenHeight) return;
    gScreenWidth = width;
    gScreenHeight = height;
    // (as InitialiseGameStage3 does it)
    gViewCentreX = static_cast<float>(static_cast<uint32_t>(gScreenWidth)) * 0.5f;
    gViewCentreY = static_cast<float>(static_cast<uint32_t>(gScreenHeight)) * 0.5f;
    gScreenCentreX = gViewCentreX;
    gScreenCentreY = gViewCentreY;
    // The shape comes with the size, and the unit space with the shape; the windows stood in the old one.
    const int32_t oldWide = gUIUnitWidth;
    const int32_t oldHigh = gUIUnitHeight;
    gUIWideAspect = UI_SizeAspect(width, height);
    UI_SetRealScreenRez(static_cast<int16_t>(width), static_cast<int16_t>(height));
    gText.SetRez(gScreenWidth);
    gei_DDraw->PortResize(width, height);
    gScreenMatrix.m[0][0] = 2.0f * gei_Display.mF04;
    UI_FollowUnits(oldWide, oldHigh);
    PortFollowUnits();
    PortAdvisorFollowUnits();
    if (gOnlinePanelShown != 0) {
        // (as the online panel does it when it is shown: src/game/ui/ui_onlinePanel.cpp)
        gGameWindowCentreX = gScreenCentreX;
        gGameWindowCentreY = gScreenCentreY;
        gGameWindowHalfWidth = gViewCentreX;
        gGameWindowHalfHeight = gViewCentreY;
    } else if (gGameWindow != nullptr) {
        // (as InitGameUI does it)
        CoolRect rect;
        gGameWindow->GetPosition(&rect, 1);
        const int32_t halfWidth = (static_cast<int32_t>(rect.mRight) - rect.mLeft) / 2;
        const int32_t halfHeight = (static_cast<int32_t>(rect.mBottom) - rect.mTop) / 2;
        gGameWindowCentreX = static_cast<float>((gUIRealScreenWidth * (rect.mLeft + halfWidth)) / 2048);
        gGameWindowCentreY = static_cast<float>((gUIRealScreenHeight * (rect.mTop + halfHeight)) / 1536);
        gGameWindowHalfWidth = static_cast<float>((gUIRealScreenWidth * halfWidth) / 2048);
        gGameWindowHalfHeight = static_cast<float>((gUIRealScreenHeight * halfHeight) / 1536);
    }
    InterfaceDrawObjectSprite::PortFollowScreen();
}

void PortApplyGameSize() {
    static UISizeWait wait;
    static bool buttonWasHeld = false;
    if (gPortWantedWide <= 0 || gPortWantedHigh <= 0) return;
    if (gPortWantedWide == gScreenWidth && gPortWantedHigh == gScreenHeight) {
        wait = UISizeWait{};
        return;
    }
    const bool held = UI_GetButtonFlags() != 0;
    const bool justUp = buttonWasHeld && !held;
    buttonWasHeld = held;
    if (!UI_SizeHasStopped(wait, gPortWantedWide, gPortWantedHigh, static_cast<uint32_t>(SamsUtilities::UTimer::GetAbsolute() / 1000))) return;
    // A new shape moves the window and the panel from under a mouse still holding the thumb, which would then ask for another
    // size, so it waits for the button to come up, and for one tick more: the panel only hears of the release after this tick.
    // The same shape stays where it is and changes under the drag.
    if ((held || justUp) && !UI_SameShape(gScreenWidth, gScreenHeight, gPortWantedWide, gPortWantedHigh)) return;
    PortChangeGameSize(gPortWantedWide, gPortWantedHigh);
}

