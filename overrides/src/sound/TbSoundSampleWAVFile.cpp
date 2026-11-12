// TbSoundSampleWAVFile.cpp: name proven by __sinit_TbSoundSampleWAVFile_cpp in the sound library.

#include "CSprocketsModule.h"
#include "helpers/TbSoundSampleWAVHelpers.h"
#include "snd_handles.h"
#include "TbDynamicStringTemplate.h"
#include "TbGraphicFile.h"
#include "TbSoundBank.h"
#include "TbSoundCodecs.h"
#include "TbSoundCore.h"
#include "TbSoundPipeChannel.h"
#include "TbSoundSampleStore.h"
#include "TbSoundSystemModule.h"
#include <cstring>
#include <new>

// The sound library's four small codecs (unit w80, program "sound symbols shared"): _TbSoundSampleWAV and
// _TbSoundSampleWAVFile (TbSoundSampleWAV.cpp, LbMpeg.cpp), _TbSoundCodecLoop (TbSoundCodecLoop.cpp) and TbTimerCodec
// (TimerCodec.cpp), with CMemoryCopy::Copy. See include/TbSoundCodecs.h for the layouts and the slot order.

namespace {

struct WAVFilePool {
    using Object = _TbSoundSampleWAVFile;
    static Object*& first() { return Object::mFirst_TbSoundSampleWAVFile; }
    static int32_t& current() { return Object::mCurrent_TbSoundSampleWAVFile; }
    static int32_t& low() { return Object::mLow_TbSoundSampleWAVFile; }
    static int32_t& high() { return Object::mHigh_TbSoundSampleWAVFile; }
    static int32_t& total() { return Object::mTotal_TbSoundSampleWAVFile; }
    static int32_t& level() { return Object::mLevel_TbSoundSampleWAVFile; }
    static Object*& link(Object* object) { return object->mWAVFilePoolLink; }
};

}  // namespace

void _TbSoundSampleWAVFile::ClearCache() { PoolClearCache<WAVFilePool>(); }

// ---------------------------------------------------------------- _TbSoundSampleWAVFile

void* _TbSoundSampleWAVFile::operator new(std::size_t size) { return PoolNew<WAVFilePool>(size); }
void _TbSoundSampleWAVFile::operator delete(void* object, std::size_t size) { PoolDelete<WAVFilePool>(object, size); }
void _TbSoundSampleWAVFile::TopUp() { PoolTopUp<WAVFilePool>(); }
void _TbSoundSampleWAVFile::SetLevel(int32_t level) { mLevel_TbSoundSampleWAVFile = level; }
// The file, then the start (the arguments of the Mac constructor are file, header, start, owner).
_TbSoundSampleWAVFile::_TbSoundSampleWAVFile(bfl::FileSystem::CFile* file, TbSampleHeader* header, uint32_t start,
                                             TbRefRelease* owner)
    : _TbSoundSampleWAV(header, owner) {
    mFile = file;
    mStart = start;
    mPortRead = 0;   // TPW_PORT_FIX wavfileseek _TbSoundSampleWAVFile::_TbSoundSampleWAVFile | Mac: no such member | Port: nothing of the sample is read yet | Why: see GetDataChunk
}

// Like the memory codec, but the position is the file's: the bytes left are mDataSize - (file position - mStart), read with
// the file's Read (slot +0x10, after GetPos, slot +0x20), and the end of the data is tested by asking GetPos again (a signed
// compare, unlike the memory codec's). The timer gets the bytes just read, without the memory codec's + 0x1200.
int32_t _TbSoundSampleWAVFile::GetDataChunk(void* buffer, int32_t size, int32_t& done, bool stopped) {
    if (Timed() != 0) {
        done = 0;
        return IsTimeComplete();
    }
    int32_t position = mFile->GetPos();
    // TPW_PORT_FIX wavfileseek _TbSoundSampleWAVFile::GetDataChunk | Mac: reads from wherever the bank's shared file happens to be (nothing seeks to the sample before its first read), and a position outside the sample makes the count negative, which Read takes as a huge one | Port: the codec counts the bytes it has asked for (mPortRead); when the file is not at the start + that count it is moved there first, as _CFileAccess::Read does for the MPEG files (src/sound/FileAccess.cpp), the count is what is left of the sample and never negative, and the data ends when the count reaches the size; a file that is where the codec left it reads as on the Mac | Why: every bank streams (CBankFactory::ConstructBank makes a TbMMFileBank) and TbMMFileBank::RegisterBank leaves the file near its end, so a PCM sample (the TPI worlds' rides have them) read on to the end of the file into the 4608-byte pull buffer (heap corruption) and answered a negative chunk, and the sentence's callback pipe then chained its samples without end (the crash in Land of Invention, 2026-11-11)
    const uint32_t wanted = mStart + mPortRead;
    if (static_cast<uint32_t>(position) != wanted) mFile->Seek(wanted, 0);
    const uint32_t remaining = mPortRead < mDataSize ? LB_MIN<uint32_t>(mDataSize - mPortRead, 0x7fffffffu) : 0;
    done = size > 0 ? static_cast<int32_t>(LB_MIN<uint32_t>(static_cast<uint32_t>(size), remaining)) : 0;
    mFile->Read(buffer, static_cast<uint32_t>(done));
    mPortRead += static_cast<uint32_t>(done);
    int32_t after = static_cast<int32_t>(static_cast<uint32_t>(mFile->GetPos()) - mStart);
    if (after >= static_cast<int32_t>(mDataSize) || mPortRead >= mDataSize) {
        if (!SetTime(stopped, static_cast<uint32_t>(done))) return 1;
    }
    return 0;
}

void _TbSoundSampleWAVFile::Restart() {
    mPortRead = 0;   // TPW_PORT_FIX wavfileseek _TbSoundSampleWAVFile::Restart | Mac: only the seek | Port: the count of the bytes read starts again too | Why: see GetDataChunk
    mFile->Seek(mStart, 0);
}
_TbSoundSampleWAVFile::~_TbSoundSampleWAVFile() {}

// Unit v51075: small functions of the sound library ("sound symbols shared"): the speaker module's conversions, the bank handle's
// forwards, the static cleanup objects and their __sinit_*_cpp, and a few tiny helpers.

extern "C" void __register_global_object(void* object, void* destructor, void* node);   // runtime glue 0x1002eefc here

uint32_t gCS_TbSoundSampleWAVFileObject;   // 0x1003f2f0
uint32_t gCS_TbSoundSampleWAVFileNode;     // 0x1003f2f4

void __sinit_TbSoundSampleWAVFile_cpp() {
    __register_global_object(&gCS_TbSoundSampleWAVFileObject, reinterpret_cast<void*>(&_CS__TbSoundSampleWAVFile::Destruct),
                             &gCS_TbSoundSampleWAVFileNode);
}

// ---- math ------------------------------------------------------------------------------------------------------------------
namespace tbstd {
float powf(float a, float b) { return static_cast<float>(MslPow(a, b)); }   // frsp of the runtime's pow
float fabs(float x) { return fabsf(x); }
float fabsf(float x) { return static_cast<float>(fabs(static_cast<double>(x))); }
double fabs(double x) { return __builtin_fabs(x); }
}  // namespace tbstd
