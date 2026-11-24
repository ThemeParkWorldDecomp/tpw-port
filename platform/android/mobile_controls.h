#pragma once

struct SDL_KeyboardEvent;
union SDL_Event;

namespace tpw::mobile {
void UpdateControls();
void CameraAxes(float& x, float& y);
bool IgnorePrintableTextKey(const SDL_KeyboardEvent& key);
void CommitText(const char* text);
void KeyboardHidden();
void InstallControlEvents();
bool HandleSliderDrag(const SDL_Event& event);
bool ShareSliderButton(bool down);
void ParkFocusWillBeLost();
}
