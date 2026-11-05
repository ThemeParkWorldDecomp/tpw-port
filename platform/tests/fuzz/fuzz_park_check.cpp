// fuzz_park_check.cpp: libFuzzer target for the park checks a downloaded park passes before the game may load it
// (tpw::online::boss::ftp::CheckPark, platform/online_ftp.cpp). The first byte picks how the input is used:
// - 0: the whole park file, as the server sent it;
// - 1: a park with a valid fixed part (version, language, legal text, ride record, key, flag) and the input as its BILZ block;
// - 2: the same, with the input deflated into a valid BILZ block (its unpacked size and packed size right), so the checks run
//      past the inflater; the next byte sets the header flag, the language and a few corrupted fields.
// Every answer is either nullptr or a reason; AddressSanitizer stops on any read outside the input.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <zlib.h>

#include "platform/online_ftp.h"

namespace ftp = tpw::online::boss::ftp;
using Bytes = std::vector<uint8_t>;

namespace {

const std::u16string kLegal = u"Theme Park World (c) fuzz legal text";

void Le32(Bytes& b, uint32_t v) {
    for (int shift = 0; shift < 32; shift += 8) b.push_back(static_cast<uint8_t>(v >> shift));
}

const ftp::LegalTexts& Legal() {
    static const ftp::LegalTexts legal = [] {
        ftp::LegalTexts l;
        for (std::u16string& text : l.text) text = kLegal;
        return l;
    }();
    return legal;
}

Bytes FixedPart(uint8_t language, bool header) {
    Bytes b;
    Le32(b, 500);
    b.push_back(language);
    for (size_t i = 0; i < 0x280; ++i) {
        const char16_t unit = i < kLegal.size() ? kLegal[i] : 0;
        b.push_back(static_cast<uint8_t>(unit));
        b.push_back(static_cast<uint8_t>(unit >> 8));
    }
    Bytes rides(0x100, 0);
    rides[0] = 1;
    b.insert(b.end(), rides.begin(), rides.end());
    for (uint8_t k : {0x01, 0x22, 0x19, 0x85}) b.push_back(k);
    Le32(b, header ? 1 : 0);
    if (header) b.insert(b.end(), 0x23a, 0x41);
    return b;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;
    const uint8_t mode = data[0] % 3;
    ++data;
    --size;
    Bytes park;
    if (mode == 0) {
        park.assign(data, data + size);
    } else if (mode == 1) {
        park = FixedPart(0, false);
        park.insert(park.end(), data, data + size);
    } else {
        if (size == 0) return 0;
        const uint8_t knobs = data[0];
        ++data;
        --size;
        park = FixedPart(static_cast<uint8_t>(knobs & 7), (knobs & 8) != 0);
        uLongf packed = compressBound(static_cast<uLong>(size));
        Bytes stream(packed);
        compress2(stream.data(), &packed, data, static_cast<uLong>(size), (knobs >> 4) % 10);
        stream.resize(packed);
        Le32(park, 0x5a4c4942);
        Le32(park, static_cast<uint32_t>(size) + ((knobs & 0x80) != 0 ? 1u : 0u));
        Le32(park, static_cast<uint32_t>(0x1c + stream.size()));
        Le32(park, 15);
        Le32(park, 9);
        Le32(park, 0);
        Le32(park, 0);
        park.insert(park.end(), stream.begin(), stream.end());
    }
    // An exact-size copy, so that a read one past the park's end is outside the block.
    auto* exact = new uint8_t[park.size() + (park.empty() ? 1 : 0)];
    if (!park.empty()) std::memcpy(exact, park.data(), park.size());
    const char* why = ftp::CheckPark(exact, park.size(), Legal());
    if (why != nullptr && why[0] == 0) __builtin_trap();   // a reason is never empty
    delete[] exact;
    return 0;
}
