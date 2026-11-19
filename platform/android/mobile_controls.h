#pragma once

struct SDL_KeyboardEvent;

namespace tpw::mobile {
void UpdateControls();
void CameraAxes(float& x, float& y);
bool GeneratedTextKey(const SDL_KeyboardEvent& key);
void CommitText(const char* text);
void KeyboardHidden();
void InstallControlFocusFilter();
void ParkFocusWillBeLost();
}
