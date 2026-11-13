// TpiRideCamera.h: the TPI Worlds and Rides mod's ride camera node (TPW_MOD_HOOK tpi_worlds, SetupRideRideMatrixAndSuch).
// The first person view of a ride sits at an embedded node of the ride's mesh with flag 0x1000, the camera node; with none
// the view does not open. Theme Park Inc's Polar zone ice skates (Bumper Ride, ride 6030) have a camera mesh of their own
// whose two embedded entries are a rider's head (flag 0x80) and an accessory (0x400), and no camera node, so the ride's
// "ride it" button did nothing. In the mod's worlds a mesh with no camera node puts the view at its first rider's head:
// the head with id 1, else the first head. Header only and pure, so the host test sees what the game does.
#pragma once


// TPW_PORT_FIX mods TpiRideCameraFallback | Mac: no such function; a mesh with no camera node (flag 0x1000) has no first person view | Port: in the mod's worlds (TPW_MOD_HOOK tpi_worlds, SetupRideRideMatrixAndSuch) such a mesh puts the view at its first rider's head | Why: tpw-mods tpi-worlds; Theme Park Inc's Polar zone ice skates have no camera node, so their "ride it" button did nothing

#include <cstdint>

namespace tpw {

inline constexpr uint32_t kRideNodeNone = 0xffffffffu;
inline constexpr uint32_t kRideNodeCamera = 0x1000;   // the camera node's flag
inline constexpr uint32_t kRideNodeHead = 0x80;       // a rider's head's flag

// The embedded entry the view uses when the mesh has no camera node (kRideNodeNone: none will do). `count` entries;
// entry(i, &id, &flags) gives entry i's id and flags.
template <typename Entry>
uint32_t TpiRideCameraFallback(uint32_t count, Entry entry) {
    uint32_t first = kRideNodeNone;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t id = 0, flags = 0;
        entry(i, &id, &flags);
        if ((flags & kRideNodeHead) == 0) continue;
        if (id == 1) return i;
        if (first == kRideNodeNone) first = i;
    }
    return first;
}

}  // namespace tpw

