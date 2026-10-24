// sound_glue.cpp: the glue imports of the sound library (the 0x1002ea94 to 0x1002eddc block of the
// Mac program) that the translated sound code calls and the host can answer by itself: the
// allocation functions, the event calls and the small constructors. The Sound Manager channel
// layer behind the rest (USoundChannel, SendToChannel) is not here: the host plays sound through
// platform/sound_module.cpp, which replaces the translated CSprocketsModule.
#include <chrono>
#include <cstdint>
#include <new>

#include "CMpegBase.h"
#include "CSprocketsModule.h"
#include "TbSoundSampleMPEG2.h"
#include "platform.h"
#include "platform/macdoze.h"
#include "platform/macdoze_system.h"

// 0x1002eaac: the clock __sinit_AudioPlaceHolder_cpp seeds the sounds' random picks with, read once as the library
// loads. The Mac's clock counted from start-up of the machine, so each launch had its own seed; the weak body in src
// reads LbTime_GetClock, which here counts from the program's first call, 0 that early in every launch. The steady
// clock counts from start-up on Windows and macOS, as the Mac's did.
extern "C" int32_t MslSeedGlue1002eaac() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<int32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

// 0x1002ebfc: the buffer allocation, NULL when it fails.
void* AllocBuffer_(uint32_t bytes) { return new (std::nothrow) uint8_t[bytes]; }

// 0x1002ebcc: frees what AllocBuffer_ made.
void DisposeBuffer_(void* buffer) { delete[] static_cast<uint8_t*>(buffer); }

// 0x1002ea94: the library's operator delete, for the channel objects UAllocatorMake made.
void USoundChannel_Free(void* channel) { ::operator delete(channel); }

// 0x1002ecbc: USoundChannel::TickSafeChannels(). The host keeps no Sound Manager channels that need
// the ticks (platform/sams_core.cpp keeps only the safe-mode flag), so there is nothing to tick.
void SoundTickImport_() {}

// 0x1002edac and 0x1002eddc: the library's event calls are CloseHandle and SetEvent.
void USoundEvent_Free(void* event) { NS_MacDoze::CloseHandle(static_cast<NS_MacDoze::InternalHandle*>(event)); }

void USoundEvent_Set(void* event) { NS_MacDoze::SetEvent(static_cast<NS_MacDoze::InternalHandle*>(event)); }

namespace SamsUtilities {

// 0x1002eac4: the library's operator new; the answer may be 0.
void* UAllocatorMake(int32_t size) { return ::operator new(static_cast<size_t>(size), std::nothrow); }

// 0x1002ec8c: UAllocator::UAllocator() on the new object.
void UAllocatorInit(void* object) { new (object) UAllocator(); }

// 0x1002adfc (20): ++mCount.
void UReferenceCounted_AddReference(UReferenceCounted* object) { object->AddReference(); }

// 0x1002c468 (56) is the UCommand base constructor with the flags. UCommandPlay passes `on`
// (UCommand_ConstructOn); the other commands' translations call it without an argument (the
// register was never set up for it), which the flags 0 reproduce.
void UCommand_ConstructOn(UCommand* command, uint32_t on) { UCommand_ConstructFlags(command, on); }

void UCommand_ConstructBase(UCommand* command) { UCommand_ConstructFlags(command, 0); }

}  // namespace SamsUtilities
