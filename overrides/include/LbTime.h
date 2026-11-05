// LbTime: PARTIAL bullfrog library clock (Draw.cpp range: GetTime 0x100468e4, GetDate
// 0x10046968, GetClock 0x100469ec). GetTime and GetDate fill the structure from
// NS_MacDoze::GetLocalTime (a Windows SYSTEMTIME); r3 still holds the argument on
// return, which no caller seen so far uses.
#pragma once
#include <cstdint>

#include "platform/samsutils.h"

struct TbTime {
    int32_t mHour;          // 0x00
    int32_t mMinute;        // 0x04
    int32_t mSecond;        // 0x08
    int32_t mMillisecond;   // 0x0c
};

struct TbDate {
    int32_t mDay;           // 0x00
    int32_t mMonth;         // 0x04
    int32_t mYear;          // 0x08
    int32_t mDayOfWeek;     // 0x0c
};

void LbTime_GetTime(TbTime* time);
void LbTime_GetDate(TbDate* date);
// The game clock in its own units (CVanillaTime saves and compares it).
uint32_t LbTime_GetClock();   // 0x100469ec

// TPW_PORT_FIX framerate kPortFrameStepTime/PortFrameSteps/PortFrameScale | Mac: (none: the game draws as fast as the machine lets it, and some of its animations, sounds and camera moves step once a frame drawn) | Port: the steps that count frames take their pace from the clock instead: one every kPortFrameStepTime (31 ms), the simulation's tick (PortFrameSteps, PortFrameScale) | Why: those steps ran faster the faster the machine (the water, the flags, the lobby's lightning and sounds, the camera's turn), most of all on a screen above 60 Hz; they keep one pace at any frame rate
// The time of a frame the frame-counted steps are paced for, in microseconds: 31 ms, the tick of the simulation
// (GAMESYSTEM_Tick) and the step of a screenshot movie's clock (UseFixedTime(0x20)), about 32 frames a second. The
// original names no frame time (nothing in it waits for one), so this is the nearest it comes to one; at 60 frames a
// second the water and the flags look too fast.
inline constexpr uint64_t kPortFrameStepTime = 31000;

// The clock of one frame-counted step (one each place that steps), for PortFrameSteps and PortFrameScale.
struct PortFramePace {
    uint64_t last = 0;   // UTimer::GetAbsolute (microseconds) of the last call; 0: none yet
    double due = 0;      // the part of a step owed since then
};

// The steps' times (kPortFrameStepTime) since the pace's last call, the call noted; -1 for the first call and after a gap of over a
// quarter of a second (a pause, another screen, a load), which start again from now.
inline double PortFrameElapsed(PortFramePace& pace) {
    const uint64_t now = SamsUtilities::UTimer::GetAbsolute();
    const uint64_t last = pace.last;
    pace.last = now;
    if (last == 0 || now < last || now - last > 250000) {
        pace.due = 0;
        return -1;
    }
    return static_cast<double>(now - last) / static_cast<double>(kPortFrameStepTime);
}

// How many times a step the original takes once a frame is taken now: once for each step's time since the last call
// (0 at a faster frame rate, more at a slower one), and once on the first call and after a gap.
inline int32_t PortFrameSteps(PortFramePace& pace) {
    const double elapsed = PortFrameElapsed(pace);
    if (elapsed < 0) return 1;
    pace.due += elapsed;
    const int32_t steps = static_cast<int32_t>(pace.due);
    pace.due -= steps;
    return steps;
}

// For a motion the original moves by a fixed amount once a frame: by how much to multiply that amount now, the steps'
// times since the last call (1 on the first call and after a gap).
inline float PortFrameScale(PortFramePace& pace) {
    const double elapsed = PortFrameElapsed(pace);
    return elapsed < 0 ? 1.0f : static_cast<float>(elapsed);
}
