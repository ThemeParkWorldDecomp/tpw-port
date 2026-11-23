// FlyZone.h: where a flying ride's car (the High Flyer, the blimp, the bird, the spaceship, Jurassic Tours) crosses into the
// park and out of it: a point just outside one edge of the fly zone, along that edge (EmployEric, where a car starts, and
// FLY_Tick's state 4 to 9, where a leaving car goes; src/game/rides/FlyingRide.cpp). The Mac takes labs of a random word,
// multiplies it by 100 in 32 bits and keeps the signed remainder by the edge's length, and the point is that far from the
// edge's low end. Any labs result of 21474837 or more makes the product wrap, about half of them to a negative word, and a
// negative word's signed remainder is negative: the point then lies up to a whole edge's length beyond the zone's corner,
// far past the map's edge. Header only and pure, so the host tests see what the game does.
#pragma once
// TPW_PORT_FIX flyzone FlyZoneOffset | Mac: the signed remainder of labs(rand) * 100 by the edge's length, negative when the product wraps | Port: the unsigned remainder, from 0 to the length less one, the Mac's own whenever the product does not wrap | Why: about half the flying rides' cars started, or left, up to a whole edge's length beyond the fly zone's corner, far past the map's edge
#include <cstdint>
namespace tpw {

// The distance from the edge's low end of the point picked by the product `m` (labs(rand) * 100, 32 bit wrap) on an edge
// `span` long: always on the edge for a positive span.
inline constexpr int32_t FlyZoneOffset(int32_t m, int32_t span) {
    return static_cast<int32_t>(static_cast<uint32_t>(m) % static_cast<uint32_t>(span));
}

}  // namespace tpw
