// game_size_rules.h: the game's Screen resolution as the launcher's Settings box uses it (tpw::ui::GameSizeRules), by the
// game's own size lists and saved numbers (include/UI_Base.h, include/TPSettings.h), so the two cannot differ.
// A build with the port's fixes only: launcher/main.cpp, and the test launcher/ui/tests/settings_sync_game_test.cpp.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "UI_Base.h"
#include "TPSettings.h"
#include "ui/game_settings.h"

// `defaultPreset`: the size of a saved number that names none (the game's first start). `screenShape`: the shape of a number
// that names no shape (one saved by an earlier version, 0x100 + preset, or the first start's), the screen's. `screenSize`:
// the screen's size in pixels, which the slider has as a stop as the panel's does (UI_SizeStops).
inline tpw::ui::GameSizeRules PortGameSizeRules(int32_t defaultPreset, std::function<int()> screenShape,
                                                std::function<tpw::ui::GameSize()> screenSize) {
    tpw::ui::GameSizeRules rules;
    for (int shape = 0; shape < kUISizeShapeCount; ++shape)
        rules.shapes.push_back(std::to_string(kUISizeShapes[shape].mWide) + ":" + std::to_string(kUISizeShapes[shape].mHigh));
    rules.chosen = [defaultPreset, screenShape](int setting, int& shape) {
        int32_t width = 0, height = 0;
        if (PortResolutionSize(setting, width, height)) {
            shape = UI_NearestShape(width, height);
        } else {
            shape = PortResolutionShape(setting);
            if (shape < 0) shape = screenShape();
            UI_PresetSize(PortResolutionPreset(setting, defaultPreset), UI_ShapeAspect(shape), width, height);
        }
        return tpw::ui::GameSize{width, height};
    };
    rules.stops = [screenSize](int shape, tpw::ui::GameSize opened) {
        const tpw::ui::GameSize screen = screenSize();
        UISizeStop stops[kUISizeStopLimit];
        const int32_t count = UI_SizeStops(shape, opened.width, opened.height, screen.width, screen.height, stops);
        std::vector<tpw::ui::GameSize> sizes;
        for (int32_t i = 0; i < count; ++i) sizes.push_back({stops[i].mWidth, stops[i].mHeight});
        return sizes;
    };
    rules.closest = [](const std::vector<tpw::ui::GameSize>& sizes, tpw::ui::GameSize size) {
        std::vector<UISizeStop> stops;
        for (const tpw::ui::GameSize& stop : sizes) stops.push_back({stop.width, stop.height});
        return static_cast<int>(UI_ClosestStop(stops.data(), static_cast<int32_t>(stops.size()), size.width, size.height, 0));
    };
    rules.setting = [](tpw::ui::GameSize size) { return static_cast<int>(PortResolutionExact(size.width, size.height)); };
    return rules;
}
