// sound_module.cpp: CSprocketsModule and CSprocketsChannel, the host sound device module (unit p-sound, "sound symbols
// shared", file CSprocketsModule.cpp). A PLATFORM unit: the Mac's Sound Sprockets calls are replaced by SDL3 audio streams
// (platform/sound_device.cpp) and nothing is compared bit for bit. The original's listings are the contract
//; every function below names its address.
//
// How the Mac worked and what the host does instead.
//  - Each of the mBuffers channels owned a Sound Sprockets channel and a double buffer of 2 x mChunk bytes (mChunk 0x1200 for
//    a mono sample, 0x2400 for stereo: the pipe's service buffer is 0x1200 bytes). StreamData pulled the first chunk with
//    TbSoundPipeChannel::StreamerCallback(buffer, mChunk, pipe) and played it; the Sound Manager's completion callback
//    (CSprocketsChannel::Callback, 0x1002a4cc) pulled the next chunk each time one had been consumed, and a pull that answered
//    false (the pipe is over: GetDataChunk answered 1 and ReleasePipe ran) ended the stream without playing that chunk.
//    Here the channel is an SDL audio stream: StreamData pulls and queues the first chunk and binds the stream to the one
//    device, and SDL's get callback (on its audio thread) pulls and queues further chunks while the device needs data, with the
//    same end rule.
//  - Commands the Mac sent to a channel are done at once on the stream (volume is the stream's gain, stop unbinds and clears,
//    pause unbinds and keeps the queue, restart binds again). The 3D commands (position, velocity, listener) are only kept:
//    the first version plays every 3D sound centred, at its volume. The sound type, CPU load, listener scale, sound distance,
//    preroll and speaker type commands have nothing to do on SDL.
//  - Bounds: the Mac indexed mChannels with whatever the pipe said; the host answers -1 (a NULL channel) for an index outside
//    the array or a module that is not initialised.
//  - Byte order: the translated decoders write native-endian 16-bit samples (notes/w71a.md: the host's frame buffer is native
//    shorts), so a 16-bit sample is SDL_AUDIO_S16 (little-endian on this host), 8-bit is unsigned.
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <unordered_map>

#include "CMpegBase.h"
#include "CSprocketsModule.h"
#include "TbSoundBank.h"
#include "TbSoundPipeChannel.h"
#include "platform.h"
#include "sound_device.h"
#include "sound_module.h"

// ---------------------------------------------------------------- CSprocketsChannel

CSprocketsChannel::CSprocketsChannel(tpw::SoundDevice* device, uint32_t index)
    : mDevice(device), mIndex(index), mPipe(nullptr), mChunk(0), mIs3D(0), mLocation{0.0f, 0.0f, 0.0f}, mVelocity{0.0f, 0.0f, 0.0f} {}

// The Mac's destructor ran Cleanup (the stream is stopped and the buffer freed).
CSprocketsChannel::~CSprocketsChannel() {
    Cleanup();
}

// 0x1002a3f0: stop the channel (the three commands 0x2e, 4 and 3: flush, quiet) and free the buffer and its size.
void CSprocketsChannel::Cleanup() {
    mDevice->ClearSource(mIndex);
    mChunk = 0;
}

// 0x1002a99c: the Mac answered the description's address, so a test of it is a test for "has a sample format".
bool CSprocketsChannel::GetDescription() const {
    return mDevice->HasSource(mIndex);
}

// 0x1002a9f8: the chunk is one or two 0x1200-byte units for a mono or stereo sample; the description is built from the
// header (rate, bits, 'NONE' compression, channels) and loaded into the channel. A failure to allocate logged and cleaned up;
// here a format SDL cannot take (bits other than 8, 16 or 32, rate 0) does the same.
void CSprocketsChannel::SetDescription(const TbSampleHeader& header) {
    Cleanup();
    const uint32_t channels = (header.mFlags & 1) == 0 ? 1 : 2;
    mChunk = channels * 0x1200;
    tpw::SoundSource source;
    source.rate = header.mRate;
    source.bits = header.mBits;
    source.channels = channels;
    if (!mDevice->SetSource(mIndex, source, mChunk)) {
        tpw::Log("sound: channel %u cannot play a sample of %u Hz, %u bits, %u channels", mIndex, source.rate, source.bits,
                 source.channels);
        Cleanup();
    }
}

// 0x1002a8a0: with a pipe, pull the first chunk; when it carries data the Mac queued UCommandPlay (immediate), here the
// device queues the chunk and binds the stream. The later chunks come from the device's callback.
void CSprocketsChannel::Play() {
    if (mPipe == nullptr) return;
    TbSoundSamplePipe* pipe = mPipe;
    mDevice->Play(mIndex, [pipe](void* buffer, uint32_t size) {
        return TbSoundPipeChannel::StreamerCallback(buffer, size, pipe);
    });
}

bool CSprocketsChannel::Preroll() const {
    return GetDescription();
}

bool CSprocketsChannel::SetVolume(float gain) {
    return mDevice->SetGain(mIndex, gain);
}

// UCommandStop, immediate: the stream is unbound and emptied, and the pipe is forgotten (the game releases it after a stop,
// so a later pull would touch a freed chain).
void CSprocketsChannel::Stop() {
    mDevice->Stop(mIndex);
    mPipe = nullptr;
}

void CSprocketsChannel::Pause() {
    mDevice->Pause(mIndex);
}

void CSprocketsChannel::Restart() {
    mDevice->Restart(mIndex);
}

void CSprocketsChannel::SetPosition(const float location[3], const float velocity[3]) {
    std::memcpy(mLocation, location, sizeof mLocation);
    std::memcpy(mVelocity, velocity, sizeof mVelocity);
}

// ---------------------------------------------------------------- CSprocketsModule

namespace {

// What the Mac kept in the module object after the base (mChannels, +0x14) and in the commands it sent to channel 0. The
// object itself must stay 0x18 bytes (see include/CSprocketsModule.h), so the record is found by the module's address.
struct Host {
    CSprocketsChannel** channels = nullptr;   // mChannels: mBuffers channels
    tpw::SoundDevice device;
    CSprocketsListener listener;
    const TbSpeaker::Config* speakerConfig = nullptr;   // SetSpeakerConfig's last argument (the Mac sent its type to channel 0)
};

// Never destroyed: a module still alive at exit must not close SDL streams from a static destructor after SDL_Quit.
std::mutex& gHostsLock = *new std::mutex;
std::unordered_map<const CSprocketsModule*, std::unique_ptr<Host>>& gHosts =
    *new std::unordered_map<const CSprocketsModule*, std::unique_ptr<Host>>;

// The record of a module, made at its first use (the constructor makes it; a module built in place by a test does not).
Host& HostOf(const CSprocketsModule* module) {
    std::lock_guard<std::mutex> guard(gHostsLock);
    std::unique_ptr<Host>& host = gHosts[module];
    if (!host) host.reset(new Host);
    return *host;
}

// The volume the game keeps as 0..127 becomes the stream's gain 0.0..1.0 (the Mac converted the unsigned word through a
// double and divided by 127.0f in single precision before the UCommandVolume took it as a double).
float GainOf(uint32_t volume) {
    return static_cast<float>(static_cast<double>(volume)) / 127.0f;
}

void Copy3(float to[3], const TbSoundPosition& from) {
    to[0] = from.mX;
    to[1] = from.mY;
    to[2] = from.mZ;
}

}  // namespace

// 0x1002b234 (108): the base, then a CSprocketsSpeaker (4 bytes on the Mac: a vtable) that TbSpeaker::Config::SetModuleSpeaker
// (0x100149e8) stores in the shared pointer the base's AutoDelete frees. Nothing reads it back on the host; the block is
// there so that the base's destructor frees what the Mac's did.
CSprocketsModule::CSprocketsModule() {
    HostOf(this);
    void* speaker = ::operator new(sizeof(void*));
    std::memset(speaker, 0, sizeof(void*));
    TbSpeaker::AutoDelete::_mpSpeaker = speaker;
}

// 0x1002b374: the base's destructor. The channels were deleted by Close (TbSoundCore::Close calls it before the delete); a
// module that is deleted without it is closed here so that no SDL stream outlives it.
CSprocketsModule::~CSprocketsModule() {
    Close();
    std::lock_guard<std::mutex> guard(gHostsLock);
    gHosts.erase(this);
}

CSprocketsChannel** CSprocketsModule::Channels() const {
    return HostOf(this).channels;
}

// Unit v5L24: only the translated Init (src/sound/CSprocketsModule.cpp, weak) calls it; the launcher links the Init above.
void CSprocketsModule::SetChannels(CSprocketsChannel** channels) {
    HostOf(this).channels = channels;
}

tpw::SoundDevice* CSprocketsModule::Device() const {
    return &HostOf(this).device;
}

const CSprocketsListener& CSprocketsModule::Listener() const {
    return HostOf(this).listener;
}

CSprocketsChannel* CSprocketsModule::ChannelAt(uint32_t index) const {
    CSprocketsChannel** channels = HostOf(this).channels;
    if (channels == nullptr || index >= mBuffers) return nullptr;
    return channels[index];
}

// 0x1002b408: the base's Init stores the buffer count; the channel array is cleared memory; the first channel got
// UCommandCPULoad (the info's double at +0xc) and UCommandListenerScale(0.01, 0, 0), every channel UCommandSoundDistance(400.0);
// the speaker config is Stereo; the MPEG file player pool is set to be kept full (UPooled<CMpegMacFilePlayerUnpooled, 10>::
// SetPoolKeepFull(1), 0x1002b5ec) and the pool maintenance runs (Tick). -1 when the array cannot be made. Host: the device is opened
// at mRate (22050, the base's constructor value; the info has no rate field) as 16-bit stereo, with one stream per channel;
// no device is also -1 (the game goes on without sound, as on a Mac without a sound card).
TbError CSprocketsModule::Init(const TbSoundSystemInfo& info) {
    Host& host = HostOf(this);
    if (host.channels != nullptr) Close();
    TbSoundSystemModule::Init(info);
    host.channels = new (std::nothrow) CSprocketsChannel*[mBuffers]();
    if (host.channels == nullptr) return TbError(-1);
    if (!host.device.Open(mRate, mBuffers)) {
        delete[] host.channels;
        host.channels = nullptr;
        return TbError(-1);
    }
    host.listener = CSprocketsListener();
    for (uint32_t i = 0; i < mBuffers; ++i) host.channels[i] = new CSprocketsChannel(&host.device, i);
    host.speakerConfig = nullptr;   // TbSpeaker::Stereo
    SamsUtilities::UPooled<CMpegMacFilePlayerUnpooled, 10>::SetPoolKeepFull(1);
    Tick();
    tpw::Log("sound: %u channels", mBuffers);
    return TbError(0);
}

// 0x1002b8c4: the Mac topped up the object pools of the codecs (_TbSoundSampleMPEG2, _TbSoundSampleCallback, CMpegBase,
// _TbSoundSampleMPEG2File, TbTimerCodec, TbSoundUserCallback, TbSoundSamplePipe, _TbSoundCodecLoop,
// _TbSoundSampleCompletedCallback) and maintained the MPEG file player pool (UPooled<CMpegMacFilePlayerUnpooled, 10>) so
// that the interrupt-time callbacks never allocated. The host pools of the codecs allocate on demand (their operator new falls
// back to the heap), so a missed top-up costs time, not correctness, and three of the TopUps are not on this base, so none is
// called. The file player pool has NO heap fallback (its operator new answers NULL when no player is idle), so its
// maintenance runs here, as the original's Tick ended with it.
// The maintenance walks the pool's queue and frees idle players while SDL's audio thread returns players to it, so the port's
// fixes hold the pool's lock (MpegPoolQueueLock, include/CMpegBase.h) across it; the Mac's interrupt-time callbacks could
// never run beside the walk (tpw-port issue #50).
void CSprocketsModule::Tick() {
#ifdef TPW_PORT_FIX
    std::lock_guard<std::recursive_mutex> guard(SamsUtilities::MpegPoolQueueLock());
#endif
    SamsUtilities::UPooled<CMpegMacFilePlayerUnpooled, 10>::MaintainPoolLevel();
}

// 0x1002bd90, 0x1002beb8, 0x1002d034: do nothing, answer 0.
TbError CSprocketsModule::SetOptions(uint32_t) {
    return TbError(0);
}

TbError CSprocketsModule::Set3DDSPFunctionality(uint32_t, uint32_t) {
    return TbError(0);
}

TbError CSprocketsModule::SetSpeedOfSound(float) {
    return TbError(0);
}

// 0x1002bde8: clear the MPEG file player pool, delete every channel (virtually, flag 1) and free the array. Host: the device
// closes after the channels, and the array pointer is cleared so that later calls find no channels.
void CSprocketsModule::Close() {
    Host& host = HostOf(this);
    if (host.channels != nullptr) {
        for (uint32_t i = 0; i < mBuffers; ++i) {
            delete host.channels[i];
        }
        delete[] host.channels;
        host.channels = nullptr;
    }
    host.device.Close();
}

// 0x1002bf20: a NULL pipe answers 0; the channel is mChannels[pipe->GetIndex()] (ChannelAt), NULL answers -1; no description answers -1;
// else Play. (The Mac also asked the pipe for GetSampleFlags and dropped the answer.)
TbError CSprocketsModule::StreamData(TbSoundPipeChannel* pipe) {
    if (pipe != nullptr) {
        const uint32_t index = pipe->GetIndex();
        pipe->GetSampleFlags();
        CSprocketsChannel* channel = ChannelAt(index);
        if (channel == nullptr) return TbError(-1);
        if (!channel->GetDescription()) return TbError(-1);
        channel->Play();
    }
    return TbError(0);
}

// 0x1002c068: UCommandStop immediate; NULL channel -1; else the status through IfOk (0 or -1).
TbError CSprocketsModule::StopChannel(uint32_t index) {
    CSprocketsChannel* channel = ChannelAt(index);
    if (channel == nullptr) return TbError(-1);
    channel->Stop();
    return TbError(0);
}

// 0x1002c238, 0x1002c670, 0x1002c748: the same command over every channel that is not NULL; always 0.
TbError CSprocketsModule::StopAll() {
    for (uint32_t i = 0; i < mBuffers; ++i) {
        if (CSprocketsChannel* channel = ChannelAt(i)) channel->Stop();
    }
    return TbError(0);
}

TbError CSprocketsModule::PauseAll() {
    for (uint32_t i = 0; i < mBuffers; ++i) {
        if (CSprocketsChannel* channel = ChannelAt(i)) channel->Pause();
    }
    return TbError(0);
}

TbError CSprocketsModule::RestartAll() {
    for (uint32_t i = 0; i < mBuffers; ++i) {
        if (CSprocketsChannel* channel = ChannelAt(i)) channel->Restart();
    }
    return TbError(0);
}

// 0x1002c314 and 0x1002c51c: UCommandPause(1) and UCommandPlay(1), not immediate; NULL channel -1, else the status.
TbError CSprocketsModule::PauseChannel(uint32_t index) {
    CSprocketsChannel* channel = ChannelAt(index);
    if (channel == nullptr) return TbError(-1);
    channel->Pause();
    return TbError(0);
}

TbError CSprocketsModule::RestartChannel(uint32_t index) {
    CSprocketsChannel* channel = ChannelAt(index);
    if (channel == nullptr) return TbError(-1);
    channel->Restart();
    return TbError(0);
}

// 0x1002c820. The flags are the info's mFlags (+0x14); mField0C (+0x0c) is the sample flags. The Mac's UStatus keeps the status
// of the LAST command sent (operator= replaces it), and the answer is 0 only when that status is ok and every flag was
// consumed, so a flag nobody handles (not 2, 0x400000, 0x800000, 1, 0x200 or 0x20) answers -1.
TbError CSprocketsModule::SetChannelInfo(uint32_t index, TbSoundPipeChannel* pipe, TbSoundSampleInfo& info) {
    CSprocketsChannel* channel = ChannelAt(index);
    if (channel == nullptr) return TbError(-1);
    bool failed = false;
    if ((info.mField0C & 0x2000) != 0) {
        // A new sample: its pipe, its format, and 3D unless the sample flags say bit 0 (a 2D sound). The Mac dereferenced a
        // NULL pipe (the sample store can hand one over); the host fails the command instead.
        if (pipe == nullptr) {
            failed = true;
        } else {
            const uint32_t sampleFlags = pipe->GetSampleFlags();
            channel->SetPipe(pipe);
            channel->SetDescription(*pipe->GetSampleHeader());
            channel->Set3D((sampleFlags & 1) == 0 ? 1 : 0);
            failed = !channel->Preroll();
        }
    }
    if ((info.mFlags & 2) != 0) {
        failed = !channel->SetVolume(GainOf(info.mVolume));
        info.mFlags &= ~2u;
    }
    if ((info.mFlags & 0x400000) != 0) info.mFlags &= ~0x400000u;   // frequency: nothing done
    if ((info.mFlags & 0x800000) != 0 || (info.mFlags & 1) != 0) {
        // One UCommandPosition (a fresh one: zero location and velocity); the velocity (flag 0x800000) and the location
        // (flag 1) are read only for a 3D channel whose sample flags (mField0C) have bit 0x20. QUIRK kept from the listing:
        // flag 0x800000 is cleared BEFORE the location is handled, so its "velocity not given" test is always true and a
        // location zeroes the velocity the same command just set.
        float location[3] = {0.0f, 0.0f, 0.0f};
        float velocity[3] = {0.0f, 0.0f, 0.0f};
        const bool wanted = channel->Is3D() != 0 && (info.mField0C & 0x20) != 0;
        if ((info.mFlags & 0x800000) != 0) {
            if (wanted && info.mVelocity != nullptr) Copy3(velocity, *info.mVelocity);
            info.mFlags &= ~0x800000u;
        }
        if ((info.mFlags & 1) != 0) {
            if (wanted) {
                if (info.mPosition != nullptr) Copy3(location, *info.mPosition);
                velocity[0] = velocity[1] = velocity[2] = 0.0f;
            }
            info.mFlags &= ~1u;
        }
        channel->SetPosition(location, velocity);
        failed = false;
    }
    if ((info.mFlags & 0x200) != 0) info.mFlags &= ~0x200u;   // distance: nothing done
    if ((info.mFlags & 0x20) != 0) info.mFlags &= ~0x20u;     // pitch: nothing done
    return TbError(!failed && info.mFlags == 0 ? 0 : -1);
}

// 0x1002cfa8: -1 for a NULL channel, else 0; the position is NOT written (the Mac did not either).
TbError CSprocketsModule::GetPosition(uint32_t index, TbSoundPosition&) {
    return TbError(ChannelAt(index) == nullptr ? -1 : 0);
}

// 0x1002d090, 0x1002d220, 0x1002d4a4: a UCommandListenerPosition sent to channel 0 (NULL: -1). Kept for a later panning pass.
TbError CSprocketsModule::SetListenerPosition(const TbSoundPosition& position) {
    if (ChannelAt(0) == nullptr) return TbError(-1);
    Copy3(HostOf(this).listener.position, position);
    ++HostOf(this).listener.calls;
    return TbError(0);
}

TbError CSprocketsModule::SetListenerDirection(const TbSoundPosition& front, const TbSoundPosition& top) {
    if (ChannelAt(0) == nullptr) return TbError(-1);
    Copy3(HostOf(this).listener.front, front);
    Copy3(HostOf(this).listener.top, top);
    ++HostOf(this).listener.calls;
    return TbError(0);
}

TbError CSprocketsModule::SetListenerVelocity(const TbSoundPosition& velocity) {
    if (ChannelAt(0) == nullptr) return TbError(-1);
    Copy3(HostOf(this).listener.velocity, velocity);
    ++HostOf(this).listener.calls;
    return TbError(0);
}

// 0x1002d5b4: UCommandVolume(info.mVolume / 127.0) immediate on the channel (NULL: -1).
TbError CSprocketsModule::SetChannelVolume(uint32_t index, TbSoundSampleInfo& info) {
    CSprocketsChannel* channel = ChannelAt(index);
    if (channel == nullptr) return TbError(-1);
    return TbError(channel->SetVolume(GainOf(info.mVolume)) ? 0 : -1);
}

// 0x1002d6cc: UCommandSpeakerType(the config's type, 0) on channel 0 (NULL: -1). TbSpeaker::Config is opaque here (its
// virtuals are not translated), so the host keeps the pointer.
TbError CSprocketsModule::SetSpeakerConfig(const TbSpeaker::Config& config) {
    if (ChannelAt(0) == nullptr) return TbError(-1);
    HostOf(this).speakerConfig = &config;
    return TbError(0);
}
