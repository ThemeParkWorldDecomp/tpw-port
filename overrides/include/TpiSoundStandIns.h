// TpiSoundStandIns.h: the TPI Worlds and Rides mod's broken music samples (TPW_MOD_HOOK tpi_worlds).
// Two of Theme Park Inc's retail music banks end in samples that are not there: Arabian Nights' Music/MusicHD.sdt has 143
// offsets but only the first 103 point at a sample's 0x28-byte header (the file stops at 0x26e6000, and from the 104th on
// the offsets land in MPEG data of another encoding, so the "headers" are noise: sizes of up to 4 GB, rates such as 27216,
// 87 bits, no MPEG flag), and Polar Zone's (water) has 116 of which the last 15 (from the 102nd, the file stops at exactly
// 30 MB and repeats its own earlier clusters) are the same. Every Theme Park Inc copy has these files byte for byte (a
// Theme Park Inc install and the SimCoaster USA disc). The park music's event (2, a branching sentence)
// lists them: Arabian Nights' sounds 6 (how busy 75 to 90) and 7 (91 to 100) are made of them only and its sound 5 (59 to
// 74) has 14 of them among its 31; Polar Zone's sound 5 (77 to 90) has 15 of 38. The game started such a sample as
// 8-bit PCM read from the MPEG data, a burst of noise that ended at once, and the sentence started it again: one sample
// over and over, 100 times a second ("fast repeats of the same sound", the maintainer, 2026-11-17).
// In the mod's worlds a streaming bank's sample whose header is not one is kept with no size (so it never starts, as the
// codec factory refuses a sample of no size), and each sample of a category's map that names such a sample stands in for
// another of the same event that plays: one of its own sound's, in their order, else one of the nearest sound before it
// that has any (the level of music below), else after it. Only the sample's number, length and bank change; its place,
// weight and flags stay, so the sound is chosen and its samples drawn as before. Nothing is saved.
// Header only and pure, so the host test sees what the game does.
#pragma once


// TPW_PORT_FIX mods TpiSoundStandIns | Mac: no such header; a bank's header is taken as it is read and the map's samples as they are written | Port: TpiSdtSampleIsSound (TbMMFileBank::RegisterBank) and TpiStandInSamples (TbMapStreamer::RegisterSFXData), used in the mod's worlds (TPW_MOD_HOOK tpi_worlds, tpw::TpiWorldInPlay) | Why: tpw-mods tpi-worlds; Theme Park Inc's Arabian Nights and Polar Zone music banks list samples that are not in the file, and the park music repeated a burst of noise 100 times a second

#include <cstdint>
#include <vector>

namespace tpw {

// The size of a sample's header in a .sdt bank, which is also the header's own first word.
inline constexpr uint32_t kSdtSampleHeaderSize = 0x28;

// Whether the header read at `offset` of a .sdt bank of `fileSize` bytes is a sample's: its first word is the header's
// size, it has data, and the header and the data end within the file.
inline bool TpiSdtSampleIsSound(uint32_t headerWord, uint32_t size, uint32_t offset, uint64_t fileSize) {
    if (headerWord != kSdtSampleHeaderSize || size == 0) return false;
    return static_cast<uint64_t>(offset) + kSdtSampleHeaderSize + size <= fileSize;
}

// Lets the samples of one event's sounds that do not play (`plays(sample)` false) stand in for ones that do, as the
// header says: `sounds[i]` holds `counts[i]` samples. `Sample` is the map's CSampleData (mF00 the number, mF08 the length,
// mBankIndex the bank). The n-th sample of a sound that does not play takes the (n mod k)-th of the k that play in the
// donor sound, which is the sound itself when any of its own play, else the nearest one before it, else after it. The
// donors are read before anything changes. An event none of whose samples play is left as it is. Answers how many
// samples changed.
template <class Sample, class Plays>
uint32_t TpiStandInSamples(Sample* const* sounds, const uint32_t* counts, uint32_t soundCount, Plays plays) {
    if (sounds == nullptr || counts == nullptr || soundCount == 0) return 0;
    struct Donor {
        uint32_t number;
        uint32_t length;
        uint16_t bank;
    };
    std::vector<std::vector<Donor>> donors(soundCount);
    std::vector<bool> broken(soundCount, false);
    bool any = false;
    for (uint32_t s = 0; s < soundCount; ++s) {
        if (counts[s] != 0 && sounds[s] == nullptr) return 0;
        for (uint32_t k = 0; k < counts[s]; ++k) {
            const Sample& sample = sounds[s][k];
            if (plays(sample)) {
                donors[s].push_back(Donor{sample.mF00, sample.mF08, sample.mBankIndex});
                any = true;
            } else {
                broken[s] = true;
            }
        }
    }
    if (!any) return 0;
    uint32_t changed = 0;
    for (uint32_t s = 0; s < soundCount; ++s) {
        if (!broken[s]) continue;
        uint32_t from = s;
        if (donors[s].empty()) {
            from = soundCount;
            for (uint32_t d = 1; d < soundCount && from == soundCount; ++d) {
                if (s >= d && !donors[s - d].empty()) from = s - d;
                else if (s + d < soundCount && !donors[s + d].empty()) from = s + d;
            }
            if (from == soundCount) continue;
        }
        uint32_t n = 0;
        for (uint32_t k = 0; k < counts[s]; ++k) {
            Sample& sample = sounds[s][k];
            if (plays(sample)) continue;
            const Donor& donor = donors[from][n++ % donors[from].size()];
            sample.mF00 = donor.number;
            sample.mF08 = donor.length;
            sample.mBankIndex = donor.bank;
            ++changed;
        }
    }
    return changed;
}

}  // namespace tpw

