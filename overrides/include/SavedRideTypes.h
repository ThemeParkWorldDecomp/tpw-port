// SavedRideTypes.h: the ride types a park's saved rides need (the port's fixes heldpark and rideloadskip). A park keeps
// each ride it has as a record in its rides module (LoadRides, src/game/rides/TPRide.cpp): the slot's marker, then the
// record's type number, place and flags (0x26 bytes), its node and embedded counts and words (0x10 bytes), its embedded
// and node words, and its animation time controls (0x2c bytes each). How many time controls a record has is not in the
// file: it is its type's. A record whose type the game has not got (a Theme Park Inc ride the imported worlds lack) has no
// instance to restore, and the rides after it were lost with it, so the track rides restoring their cars from those slots
// faulted. CSaveLoad::Load walks the records before any module is read (MissingSavedRideType) and refuses such a park as
// LoadHeader refuses one whose ride record needs a missing ride. Header only and pure, so the host tests see what the game
// does.
#pragma once

// TPW_PORT_FIX rideloadskip SkippedTimeControls | Mac: no reading ahead in the block | Port: the records of the slots after one that cannot be restored, measured from the file's words with each loaded type's number of time controls | Why: see LoadRides in src/game/rides/TPRide.cpp

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace savedrides {

// The module tags LoadModules reads around the rides module (the file's bytes read as a little endian word): the game
// system's before it, the ride system's after it.
inline constexpr uint32_t kRidesBeforeTag = 0x47535953;
inline constexpr uint32_t kRidesAfterTag = 0x52535953;
// A record's fixed part (type number to the embedded words), and one time control.
inline constexpr size_t kRecordFixed = 0x36;
inline constexpr size_t kTimeControl = 0x2c;

inline uint32_t Word(const uint8_t* p, bool swap) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return swap ? __builtin_bswap32(v) : v;
}

inline uint32_t Half(const uint8_t* p, bool swap) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return swap ? __builtin_bswap16(v) : v;
}

// A record's size up to its time controls; `cursor` is at its type number.
inline size_t RecordFixedSize(const uint8_t* cursor, bool swap) {
    return kRecordFixed + static_cast<size_t>(Half(cursor + 0x26 + 6, swap)) * 8 + static_cast<size_t>(Half(cursor + 0x26 + 4, swap)) * 4;
}

// Whether the records of slots `slot` to `slots` - 1 run from `cursor` to exactly `end`, every one of them a loaded type's
// (`controls(type)` gives its number of time controls, below 0 when no loaded type carries the number) or an empty slot.
// Strict on purpose: a wrong start reads garbage, which a looser walk could take for records.
template <class Controls>
bool RecordsFit(const uint8_t* cursor, const uint8_t* end, uint32_t slot, uint32_t slots, bool swap, Controls controls) {
    for (; slot < slots; ++slot) {
        if (cursor >= end) return false;
        const uint8_t marker = *cursor++;
        if (marker == 0) continue;
        if (marker != 1) return false;
        if (static_cast<size_t>(end - cursor) < kRecordFixed) return false;
        const uint32_t typeNumber = Word(cursor, swap);
        const size_t fixed = RecordFixedSize(cursor, swap);
        if (static_cast<size_t>(end - cursor) < fixed) return false;
        cursor += fixed;
        const int32_t count = controls(typeNumber);
        if (count < 0) return false;
        const size_t size = static_cast<size_t>(count) * kTimeControl;
        if (static_cast<size_t>(end - cursor) < size) return false;
        cursor += size;
    }
    return cursor == end;
}

// The number of time controls after `cursor` (the end of the node words of a record that could not be restored) that lets
// the records of the slots after it fill the block to `end`; -1 when no count does.
template <class Controls>
int32_t SkippedTimeControls(const uint8_t* cursor, const uint8_t* end, uint32_t slot, uint32_t slots, bool swap, Controls controls) {
    if (cursor > end) return -1;
    for (size_t skip = 0; skip <= static_cast<size_t>(end - cursor); skip += kTimeControl)
        if (RecordsFit(cursor + skip, end, slot, slots, swap, controls)) return static_cast<int32_t>(skip / kTimeControl);
    return -1;
}

}  // namespace savedrides

// TPW_PORT_FIX heldpark MissingSavedRideType | Mac: no such walk | Port: a park's ride records walked as LoadRides walks them, before any module is read, naming the first whose type the game has not got | Why: see gPortMissingRideType in SaveLoad.h

namespace savedrides {

// What a park's rides module needs that the game has not got.
struct Missing {
    uint32_t type = 0;   // the first record's type number no loaded type carries; 0 when there is none
    uint32_t slot = 0;   // its slot (the record's index + 1, the instance LoadRides restores it as)
};

// The rides module in a park's unpacked modules: the block LoadRides reads (after its size word), between the game
// system's tag and the ride system's. False when no such block is found.
inline bool FindRidesBlock(const uint8_t* modules, size_t size, bool swap, const uint8_t*& block, size_t& blockSize) {
    for (size_t at = 0; at + 12 <= size; ++at) {
        if (Word(modules + at, swap) != kRidesBeforeTag) continue;
        const size_t length = Word(modules + at + 4, swap);
        if (length > size - at - 12) continue;
        if (Word(modules + at + 8 + length, swap) != kRidesAfterTag) continue;
        block = modules + at + 8;
        blockSize = length;
        return true;
    }
    return false;
}

// Walks the records of a rides module (`block`, as LoadRides reads it) as LoadRides does and gives the first one whose
// type is not loaded. A record of type 0 (an old autosave's ride preview, which LoadRides skips) is measured as LoadRides
// measures it. When the walk cannot go on (a preview it cannot measure, a block cut short), nothing is reported: the
// game's own reading decides, as before.
template <class Controls>
Missing MissingSavedRideType(const uint8_t* block, size_t size, bool swap, Controls controls) {
    Missing missing;
    if (size < 12) return missing;
    const uint8_t* const end = block + size;
    const uint32_t slots = Word(block, swap) + Word(block + 4, swap);
    const uint8_t* cursor = block + 12;
    for (uint32_t index = 0; index < slots; ++index) {
        if (cursor >= end) return missing;
        if (*cursor++ != 1) continue;
        if (static_cast<size_t>(end - cursor) < kRecordFixed) return missing;
        const uint32_t typeNumber = Word(cursor, swap);
        const size_t fixed = RecordFixedSize(cursor, swap);
        if (static_cast<size_t>(end - cursor) < fixed) return missing;
        cursor += fixed;
        const int32_t count = controls(typeNumber);
        if (count >= 0) {
            if (static_cast<size_t>(end - cursor) < static_cast<size_t>(count) * kTimeControl) return missing;
            cursor += static_cast<size_t>(count) * kTimeControl;
            continue;
        }
        if (typeNumber != 0) {
            missing.type = typeNumber;
            missing.slot = index + 1;
            return missing;
        }
        const int32_t skipped = SkippedTimeControls(cursor, end, index + 1, slots, swap, controls);
        if (skipped < 0) return missing;
        cursor += static_cast<size_t>(skipped) * kTimeControl;
    }
    return missing;
}

}  // namespace savedrides

