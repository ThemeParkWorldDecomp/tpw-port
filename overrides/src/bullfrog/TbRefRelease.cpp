// TbRefRelease.cpp: guessed name (the binary has no __sinit for this file): a contiguous range of TbRefRelease code, named after it.

#include "TbRefRelease.h"

// TbRefRelease.cpp (decompiled from the bullfrog library of SimTheme Park 2.04b3,
// functions at 0x10015538..0x10015680; original file name unknown, see TbRefRelease.h).

TbRefRelease::TbRefRelease() : mRefCount(1) {}

TbRefRelease::~TbRefRelease() {}

int32_t TbRefRelease::AddRef() {
    // TPW_PORT_FIX refcount TbRefRelease::AddRef | Mac: ++mRefCount, a plain load, add and store | Port: one atomic add | Why: a sound bank's count is raised by the game as a sample starts (CRefManager, the codec's reference on its bank) and lowered by SDL's audio thread as one ends; on the Mac the lowering ran at interrupt time, so at worst it was lost inside the game's add (a bank never freed), but two threads in parallel also lose the game's add, and the bank is then deleted while it is still registered and its samples still play
    return __atomic_add_fetch(&mRefCount, 1, __ATOMIC_ACQ_REL);
}

int32_t TbRefRelease::Release() {
    // TPW_PORT_FIX refcount TbRefRelease::Release | Mac: --mRefCount, a plain load, subtract and store, then mRefCount read again for the answer | Port: one atomic subtract, its result the answer | Why: see AddRef; the answer is the count this call left, as another thread may change mRefCount at once
    const int32_t count = __atomic_sub_fetch(&mRefCount, 1, __ATOMIC_ACQ_REL);
    if (count == 0) {
        delete this;
        return 0;
    }
    return count;
}
