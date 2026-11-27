// PortLightningFlash.h: the launcher's "Hide lightning flash" (launcher/ui/launcher_ui.h kHideLightningFlashKey). A strike's
// flash is a sprite over the whole screen (CRain::__LightningFlash, src/engine/Rain.cpp) whose blending turns the screen to
// its colour negative for a moment; the island screen asks for one with each bolt of the island in view while it has
// lightning (CLBScreenComponentIslands::TickIslands, src/game/online/lobby_island.cpp: Halloween World's LIGHTNING(63)). With
// the setting on, CRain::__RenderLightning drops the flash instead of drawing it; the bolts, the thunder, the random numbers
// and the timers are the original's. Only with the port's fixes: off (the default) or in a build without them, the flash is
// the original's.
#pragma once


// TPW_PORT_FIX lightflash gPortNoLightningFlash | Mac: no such switch | Port: true when the launcher's "Hide lightning flash" is on: a lightning strike's flash over the whole screen is not drawn (CRain::__RenderLightning); false, the original's flash, by default | Why: tpw-mods #9 (johhnyjefferson34133): the flash turns the whole screen negative with each bolt over Halloween World; a display setting, as an opt-in that keeps the original's flash by default
// False unless the launcher sets it, so a program that does not (the differential tests) keeps the original's flash.
inline bool gPortNoLightningFlash = false;

