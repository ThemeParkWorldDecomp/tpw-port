// SoundPoolLock.h: the port's lock over the sound library's pools of idle objects (the pipes, the codecs and the MPEG
// decoders). Not in the original; it exists only in the port's build.
#pragma once

// TPW_PORT_FIX soundpools SoundPoolLock | Mac: (none: the pools' free lists, of TbSoundSamplePipe, CMpegBase and the codecs of PoolNew and its kin in include/helpers/StreamableMpeg2Helpers.h, are not locked; a sample that ends goes back to them at interrupt time, which the Mac ran to the end before the program went on: an interrupt could land inside a take, but handing one object out twice needs two threads running at once) | Port: one lock, held by every take (operator new), give-back (operator delete), TopUp and ClearCache of those pools | Why: a sample that ends goes back from SDL's audio thread, in parallel with the game taking a codec for a new sound; a give-back that overlapped a take put the taken object back on the free list, the next sound got the same object as the playing one, and the playing one's end closed the new one's decoder under it (a crash in _TbSoundSampleMPEG2::FinishOpen as a sound started, in a European play)
#include <mutex>
#include <new>

// One lock for all the pools (an inline function's statics are one object in the whole program). It is never destroyed,
// since the pools' static cleanup objects (ClearCache) run at exit, after a static lock would be gone; and it is made in
// static storage, not on the heap, which the difftests' libraries count and free at the end of each case (tests/calllog.cpp).
inline std::mutex& SoundPoolLock() {
    alignas(std::mutex) static unsigned char storage[sizeof(std::mutex)];
    static std::mutex* lock = new (storage) std::mutex;
    return *lock;
}
