// online_slots.cpp: the strings the online client keeps as 32-bit slots (the model is in include/platform/online_slots.h), and the
// string callees of the client that build, copy and drop them. Before this the launcher had stand-ins that did nothing, so a slot
// stayed whatever its word held and COnlineControl::InitClient read the chat server's characters through a NULL representation.
//
// These are the Metrowerks std::basic_string members at the Mac addresses in the comments, on the one layout above. A copy makes a
// representation of its own (the Mac shares one and counts its users; nothing the client does tells the two apart). Only the arena is
// used, so the host tests link this file without the game.
#include "platform/online_slots.h"

#include <cstring>
#include <string>
#include <unordered_set>

#include "online.h"
#include "platform/arena.h"

namespace tpw::slots {

namespace {

struct Rep {
    uint32_t mLength;     // +0x00
    uint32_t mCapacity;   // +0x04
    uint32_t mRefs;       // +0x08
    uint32_t mWord;       // +0x0c  the characters' arena offset (the Mac's word)
    void* mData;          // +0x10  the same characters, native (IME::MslWideStringRep::mData)
};
static_assert(sizeof(Rep) == 24 && offsetof(Rep, mWord) == 0x0c && offsetof(Rep, mData) == 0x10, "the representation's layout");

std::unordered_set<uint32_t>& Live() {
    static std::unordered_set<uint32_t> live;
    return live;
}

Rep* At(uint32_t word) { return word != 0 ? reinterpret_cast<Rep*>(tpw::gArenaBase + word) : nullptr; }

uint32_t Make(const void* text, size_t length, size_t unit) {
    Rep* rep = static_cast<Rep*>(tpw::ArenaAllocZeroed(sizeof(Rep)));
    uint8_t* data = static_cast<uint8_t*>(tpw::ArenaAllocZeroed((length + 1) * unit));
    if (rep == nullptr || data == nullptr) {
        tpw::ArenaFree(rep);
        tpw::ArenaFree(data);
        return 0;
    }
    if (length != 0) std::memcpy(data, text, length * unit);
    rep->mLength = static_cast<uint32_t>(length);
    rep->mCapacity = static_cast<uint32_t>(length);
    rep->mRefs = 1;
    rep->mWord = tpw::ArenaOffset(data);
    rep->mData = data;
    const uint32_t word = tpw::ArenaOffset(rep);
    Live().insert(word);
    return word;
}

const char16_t kEmptyWide[1] = {0};

}  // namespace

uint32_t MakeWide(const char16_t* text, size_t length) { return Make(text, length, sizeof(char16_t)); }
uint32_t MakeNarrow(const char* text, size_t length) { return Make(text, length, sizeof(char)); }

const char16_t* WideText(uint32_t word) {
    const Rep* rep = At(word);
    return rep != nullptr && rep->mData != nullptr ? static_cast<const char16_t*>(rep->mData) : kEmptyWide;
}

const char* NarrowText(uint32_t word) {
    const Rep* rep = At(word);
    return rep != nullptr && rep->mData != nullptr ? static_cast<const char*>(rep->mData) : "";
}

uint32_t Length(uint32_t word) {
    const Rep* rep = At(word);
    return rep != nullptr ? rep->mLength : 0;
}

bool IsLive(uint32_t word) { return word != 0 && Live().count(word) != 0; }

bool Release(void* slot) {
    if (slot == nullptr) return false;
    uint32_t word;
    std::memcpy(&word, slot, sizeof(word));
    if (!IsLive(word)) return false;
    Rep* rep = At(word);
    if (rep->mRefs == 0) rep->mRefs = 1;   // the Mac takes a count of 0 as one user
    if (--rep->mRefs == 0) {
        Live().erase(word);
        tpw::ArenaFree(rep->mData);
        tpw::ArenaFree(rep);
    }
    const uint32_t zero = 0;
    std::memcpy(slot, &zero, sizeof(zero));
    return true;
}

size_t LiveCount() { return Live().size(); }

}  // namespace tpw::slots

namespace {

uint32_t& Slot(void* self) { return *static_cast<uint32_t*>(self); }
uint32_t SlotWord(const void* other) { return other != nullptr ? *static_cast<const uint32_t*>(other) : 0; }

// The new representation is made before the old one goes: the text may be the old one's own characters.
void ReplaceWide(void* self, const char16_t* text, size_t length) {
    const uint32_t made = tpw::slots::MakeWide(text, length);
    tpw::slots::Release(self);
    Slot(self) = made;
}

void ReplaceNarrow(void* self, const char* text, size_t length) {
    const uint32_t made = tpw::slots::MakeNarrow(text, length);
    tpw::slots::Release(self);
    Slot(self) = made;
}

// The part [pos, pos + count) of the wide string in slot `other` (count past the end, npos, is the rest; pos past the end is nothing).
void CopyWidePart(void* self, const void* other, uint32_t pos, uint32_t count, bool replace) {
    const uint32_t word = SlotWord(other);
    const uint32_t length = tpw::slots::Length(word);
    if (pos > length) pos = length;
    if (count > length - pos) count = length - pos;
    const char16_t* text = tpw::slots::WideText(word) + pos;
    if (replace) ReplaceWide(self, text, count);
    else Slot(self) = tpw::slots::MakeWide(text, count);
}

// The same from the host's string (the caller keeps the source as a std::u16string).
void CopyHostPart(void* self, const std::u16string* other, uint32_t pos, uint32_t count, bool replace) {
    const size_t length = other->size();
    const size_t first = pos < length ? pos : length;
    const size_t n = count < length - first ? count : length - first;
    if (replace) ReplaceWide(self, other->data() + first, n);
    else Slot(self) = tpw::slots::MakeWide(other->data() + first, n);
}

}  // namespace

namespace online_callee {

void WStringCtor(void* self) { Slot(self) = tpw::slots::MakeWide(nullptr, 0); }                          // 0x101e0b84
void WStringCtorFrom(void* self, const char16_t* text) {                                                // 0x10146a14
    Slot(self) = tpw::slots::MakeWide(text, text != nullptr ? std::char_traits<char16_t>::length(text) : 0);
}
void WStringAssign(void* self, const char16_t* text, uint32_t n) { ReplaceWide(self, text, n); }       // 0x101b2510
void WStringCopy(void* self, const void* other, uint32_t pos, uint32_t count) {                         // 0x101f7424
    CopyWidePart(self, other, pos, count, false);
}
void WStringCopy(void* self, const std::u16string* other, uint32_t pos, uint32_t count) {               // 0x101f7424
    CopyHostPart(self, other, pos, count, false);
}
void StringCtor(void* self) { Slot(self) = tpw::slots::MakeNarrow(nullptr, 0); }                        // 0x1007363c
void StringDtor(void* self, int16_t /*flag*/) { tpw::slots::Release(self); }                            // 0x1000221c
void StringAssign(void* self, const std::string& other, uint32_t pos, int32_t count) {                  // 0x10001c80
    const size_t length = other.size();
    if (pos > length) pos = static_cast<uint32_t>(length);
    size_t n = count < 0 ? length - pos : static_cast<size_t>(count);
    if (n > length - pos) n = length - pos;
    ReplaceNarrow(self, other.data() + pos, n);
}

}  // namespace online_callee

namespace init_callee {
void StringFromChars(void* self, const char* text) {                                                    // 0x10002338
    Slot(self) = tpw::slots::MakeNarrow(text, text != nullptr ? std::strlen(text) : 0);
}
}  // namespace init_callee

namespace online_parse_callee {
void WStringAssignRange(void* self, const void* src, uint32_t pos, uint32_t count) {                     // 0x101f7014
    CopyWidePart(self, src, pos, count, true);
}
void WStringAssignRange(void* self, const std::u16string* src, uint32_t pos, uint32_t count) {          // 0x101f7014
    CopyHostPart(self, src, pos, count, true);
}
}  // namespace online_parse_callee
