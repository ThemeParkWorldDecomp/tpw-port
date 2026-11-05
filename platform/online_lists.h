// online_lists.h: the received lists the port's online library gives the game (ccReceivedList, include/FS_OnlineList.h), shared by
// its two answerers: a backend (platform/online_library.cpp) and the online server client (platform/online_boss.cpp).
//
// A list is given with a count of 2: one reference for the game, one held here until the game lets go of its own (its count back
// to 1), when Sweep frees it. The game copies every string into its own records first, and it never frees what it did not
// allocate (its count never reaches 0). The infos are raw bytes at the Mac's offsets; their strings are arena blocks, kept as
// 32-bit offsets as the Mac's pointers were.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "FS_OnlineList.h"
#include "platform/arena.h"

namespace tpw::online::lists {

// A list given to the game: the list itself, its infos and the arena strings they point to.
struct HeldList {
    int32_t* refCount = nullptr;
    std::function<void()> free;
};

inline std::vector<HeldList>& HeldLists() {
    static std::vector<HeldList> lists;
    return lists;
}

// The lists the game has let go of (their count back to the one held here) are freed.
inline void Sweep() {
    std::vector<HeldList>& lists = HeldLists();
    for (size_t i = 0; i < lists.size();) {
        if (*lists[i].refCount <= 1) {
            lists[i].free();
            lists.erase(lists.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

// The infos of one list being built: `count` records of `stride` bytes, filled by Word, Half, Float, Bytes and the strings.
class InfoWriter {
public:
    InfoWriter(size_t count, size_t stride) : mStride(stride), mBytes(count * stride + 8, 0) {}
    void Word(size_t index, size_t offset, uint32_t value) { std::memcpy(At(index, offset), &value, 4); }
    void Half(size_t index, size_t offset, uint16_t value) { std::memcpy(At(index, offset), &value, 2); }
    void Float(size_t index, size_t offset, float value) { std::memcpy(At(index, offset), &value, 4); }
    void Bytes(size_t index, size_t offset, const void* bytes, size_t size) { std::memcpy(At(index, offset), bytes, size); }
    uint8_t* Record(size_t index) { return At(index, 0); }
    void Wide(size_t index, size_t offset, const std::u16string& text) {
        auto* block = static_cast<char16_t*>(Keep((text.size() + 1) * sizeof(char16_t)));
        std::memcpy(block, text.c_str(), (text.size() + 1) * sizeof(char16_t));
        Word(index, offset, tpw::ArenaOffset(block));
    }
    void Narrow(size_t index, size_t offset, const std::string& text) {
        auto* block = static_cast<char*>(Keep(text.size() + 1));
        std::memcpy(block, text.c_str(), text.size() + 1);
        Word(index, offset, tpw::ArenaOffset(block));
    }

    // The list, with a count of 2 (see above); `extra` is the word the list keeps beside its infos (mF08).
    template <class INFO>
    ccReceivedList<INFO>* Give(int32_t count, int32_t extra = 0) {
        auto* list = new ccReceivedList<INFO>{};
        auto bytes = std::make_shared<std::vector<uint8_t>>(std::move(mBytes));
        auto strings = std::make_shared<std::vector<void*>>(std::move(mStrings));
        list->mCount = count;
        list->mInfos = reinterpret_cast<INFO*>(bytes->data());
        list->mF08 = extra;
        list->mRefCount = 2;
        HeldLists().push_back({&list->mRefCount, [list, bytes, strings]() {
                                   for (void* block : *strings) tpw::ArenaFree(block);
                                   delete list;
                               }});
        return list;
    }

private:
    uint8_t* At(size_t index, size_t offset) { return mBytes.data() + index * mStride + offset; }
    void* Keep(size_t size) {
        void* block = tpw::ArenaAllocZeroed(size);
        mStrings.push_back(block);
        return block;
    }

    size_t mStride;
    std::vector<uint8_t> mBytes;
    std::vector<void*> mStrings;
};

}  // namespace tpw::online::lists
