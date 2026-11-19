// EntertainerSecondActs.h: the entertainer-second-acts mod's second acts (TPW_MOD_HOOK entertainer_second_acts).
// An entertainer's act is one of its sprite file's custom blocks (CEntertainer::SetState, src/game/people/Entertainer.cpp):
// the block names the act's action and the script that plays it. Every entertainer file the game shipped has one block,
// naming action 4, so each costume has one act. Eight costumes also have a finished second act in their sprite file that
// no block names and nothing plays: action 10 of the Halloween world's vampire, Frankenstein's monster and hunchback
// (spr_dr, spr_fk, spr_hu), of the Jungle world's dinosaur, explorer and tribal dancer (spr_di, spr_ex, spr_na) and of the
// Space world's orange alien (spr_sm), and action 11 of the Space world's purple monster (spr_mo). With the hook on, an
// act of one of them is the second act or the first, chosen at random each time an act starts, with the random number
// the original already draws there (no other draw: the park's random numbers stay the original's). The act's length,
// its sound when it ends and the crowd's reaction are the first act's. Only with the port's fixes: off (the default) or
// in a build without them, every act is the original's.
#pragma once


#include <cctype>
#include <cstdint>
#include <cstring>

#include "ESP_sprites.h"

// TPW_PORT_FIX mods gModEntertainerSecondActs | Mac: no such flag | Port: TPW_MOD_HOOK entertainer_second_acts, set by the launcher while a tpw-mods mod that switches it on is on (platform/mods.h); set, an act of the eight costumes with an unused second act plays that act at random instead of the first (CEntertainer::SetState) | Why: tpw-mods entertainer-second-acts
inline bool gModEntertainerSecondActs = false;

// The action of the second act of the costume whose sprite file is `name` (the .ESP file's name without its extension,
// ESP_CSprite::mName: its last part, after any folder, compared without case), or -1 for a costume without one.
inline int32_t EntertainerSecondActOfName(const char* name) {
    struct SecondAct {
        const char* file;
        int32_t action;
    };
    static constexpr SecondAct kSecondActs[] = {
        {"spr_dr", 10}, {"spr_fk", 10}, {"spr_hu", 10},   // Halloween: the vampire, Frankenstein's monster, the hunchback
        {"spr_di", 10}, {"spr_ex", 10}, {"spr_na", 10},   // Jungle: the dinosaur, the explorer, the tribal dancer
        {"spr_sm", 10}, {"spr_mo", 11},                    // Space: the orange alien, the purple monster
    };
    if (name == nullptr) return -1;
    const char* file = name;
    for (const char* p = name; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\' || *p == ':') file = p + 1;
    }
    for (const SecondAct& act : kSecondActs) {
        if (std::strlen(file) != std::strlen(act.file)) continue;
        bool same = true;
        for (size_t i = 0; act.file[i] != '\0'; ++i) {
            if (std::tolower(static_cast<unsigned char>(file[i])) != act.file[i]) same = false;
        }
        if (same) return act.action;
    }
    return -1;
}

// The second act of a loaded entertainer sprite: its action, or -1 when the costume has none or its file does not hold
// that action (no records in a view).
inline int32_t EntertainerSecondAct(const ESP_CSprite& sprite) {
    const int32_t action = EntertainerSecondActOfName(sprite.mName);
    if (action < 0 || sprite.mActions[action].mFrames == 0) return -1;
    return action;
}

// Whether an act that starts plays the second act, from the random number CEntertainer::SetState draws for it: one of
// two acts at random, as the original's pick among a file's custom blocks (r modulo their count) would have chosen with
// two. Bit 16, not bit 0: the low bit of the world's generator (CWorld::GetRand, a linear congruence with an odd
// multiplier and increment) only alternates from one draw to the next.
inline bool EntertainerPlaysSecondAct(uint32_t r) { return ((r >> 16) & 1u) != 0; }

