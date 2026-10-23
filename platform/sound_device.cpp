// sound_device.cpp: the SDL3 audio streams behind the host sound module (unit p-sound). See sound_device.h.
//
// Locking. Each stream has a mutex for its plain state (the pull function, the flags, a generation
// number that Stop and SetSource advance). The mutex is never held across a call into SDL or a pull:
// SDL runs a stream's get callback with the stream's lock and the device's lock held, so taking a
// stream mutex there and an SDL lock under the same mutex elsewhere would be a lock-order inversion.
// A pull runs without any lock of ours (the game's pipes take their own critical section) into a
// per-thread scratch buffer, and its chunk is put in the stream only if the generation is the one the
// pull started under; Stop unbinds first (which waits for a callback in progress) and clears after,
// so a chunk of a stopped sample never reaches the next one.
//
// A pull can run game code (the pipe's completion handlers), and that code may command the very stream
// being pulled (a stop from a completion handler). Unbinding a stream from inside its own callback is
// not safe, so then the unbind is only noted (unbindPending) and the stream, already marked unbound
// and without a pull function, simply stops asking for data; the next command from another thread
// (or a bind) settles it.
#include "sound_device.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "platform.h"

namespace tpw {

namespace {

std::atomic<int> gLiveStreams{0};
bool gAudioInitialised = false;   // SDL_InitSubSystem(SDL_INIT_AUDIO) ran once; never undone

// The stream whose pull the calling thread is inside, if any.
thread_local const void* gPulling = nullptr;

// TPW_SOUND_CAPTURE: the raw samples of every chunk, and a line per chunk saying whose they are.
std::mutex gCaptureLock;
FILE* gCapture = nullptr;
FILE* gCaptureLog = nullptr;

void CaptureOpen() {
    std::lock_guard<std::mutex> guard(gCaptureLock);
    if (gCapture != nullptr) return;
    const char* path = std::getenv("TPW_SOUND_CAPTURE");
    if (path == nullptr || path[0] == '\0') return;
    gCapture = std::fopen(path, "ab");
    if (gCapture == nullptr) {
        tpw::Log("sound: cannot write the capture file %s", path);
        return;
    }
    const std::string logPath = std::string(path) + ".log";
    gCaptureLog = std::fopen(logPath.c_str(), "ab");
    tpw::Log("sound: capturing the chunks to %s", path);
}

void CaptureClose() {
    std::lock_guard<std::mutex> guard(gCaptureLock);
    if (gCapture != nullptr) std::fclose(gCapture);
    if (gCaptureLog != nullptr) std::fclose(gCaptureLog);
    gCapture = nullptr;
    gCaptureLog = nullptr;
}

void CaptureChunk(uint32_t stream, const SoundSource& source, const void* data, uint32_t bytes) {
    std::lock_guard<std::mutex> guard(gCaptureLock);
    if (gCapture == nullptr) return;
    std::fwrite(data, 1, bytes, gCapture);
    std::fflush(gCapture);
    if (gCaptureLog != nullptr) {
        std::fprintf(gCaptureLog, "stream %u rate %u bits %u channels %u bytes %u\n", stream, source.rate, source.bits,
                     source.channels, bytes);
        std::fflush(gCaptureLog);
    }
}

// The bytes of one frame of the source (one sample per channel), and of the 16-bit stereo frame it becomes.
uint32_t FrameBytes(const SoundSource& source) {
    return SoundBytesPerSample(source.bits) * source.channels;
}
constexpr uint32_t kStereoFrameBytes = 4;

}  // namespace

uint32_t SoundBytesPerSample(uint32_t bits) {
    switch (bits) {
    case 8: return 1;
    case 16: return 2;
    case 32: return 4;
    default: return 0;
    }
}

void SoundToStereo(const void* in, uint32_t bytes, const SoundSource& source, const float from[2], const float to[2],
                   std::vector<int16_t>& out) {
    const uint32_t width = SoundBytesPerSample(source.bits);
    const uint32_t frameBytes = FrameBytes(source);
    const uint32_t frames = frameBytes == 0 ? 0 : bytes / frameBytes;
    out.resize(static_cast<size_t>(frames) * 2);
    const uint8_t* bytesIn = static_cast<const uint8_t*>(in);
    auto sample = [&](uint32_t index) -> int32_t {
        const uint8_t* at = bytesIn + static_cast<size_t>(index) * width;
        switch (width) {
        case 1: return (static_cast<int32_t>(at[0]) - 0x80) * 0x100;
        case 2: { int16_t s; std::memcpy(&s, at, 2); return s; }
        default: { int32_t s; std::memcpy(&s, at, 4); return s >> 16; }
        }
    };
    for (uint32_t i = 0; i < frames; ++i) {
        const float step = static_cast<float>(i + 1) / static_cast<float>(frames);
        const float gains[2] = {from[0] + (to[0] - from[0]) * step, from[1] + (to[1] - from[1]) * step};
        for (uint32_t side = 0; side < 2; ++side) {
            const int32_t s = sample(i * source.channels + (source.channels == 2 ? side : 0));
            const float scaled = static_cast<float>(s) * gains[side];
            out[i * 2 + side] = static_cast<int16_t>(scaled > 32767.0f ? 32767.0f : scaled < -32768.0f ? -32768.0f : scaled);
        }
    }
}

struct SoundDevice::Stream {
    SoundDevice* owner = nullptr;
    uint32_t index = 0;
    SDL_AudioStream* sdl = nullptr;

    mutable std::mutex lock;   // guards everything below except pulled
    SoundSource source;
    uint32_t chunk = 0;
    bool hasSource = false;
    SoundPull pull;
    uint64_t generation = 0;
    bool started = false;         // a first chunk was delivered and not stopped since
    bool bound = false;           // the stream is meant to be bound to the device
    bool unbindPending = false;   // bound in SDL although `bound` is false: an unbind was asked from inside the callback
    bool paused = false;
    bool ended = false;
    float pan[2] = {1.0f, 1.0f};      // SetPan: what the next chunk moves to
    float played[2] = {1.0f, 1.0f};   // what the last chunk ended on
    bool panFresh = true;             // no chunk since the source was set: the first one starts at `pan`
    std::atomic<uint32_t> pulled{0};

    enum class Result { Delivered, Over, Stale };

    // One pull and, when it carries data, its chunk into the SDL stream as 16-bit stereo with the pan. A paused stream is not
    // pulled by SDL's callback (`firstChunk` is the one pull that Play makes whatever the pause). `bytes` gets the size of
    // what went into the stream.
    Result PullOnce(bool firstChunk, uint32_t* bytes) {
        SoundPull fn;
        SoundSource src;
        uint32_t size;
        uint64_t gen;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (!hasSource || !pull || ended || (paused && !firstChunk)) return Result::Stale;
            fn = pull;
            src = source;
            size = chunk;
            gen = generation;
        }
        thread_local std::vector<uint8_t> scratch;
        scratch.assign(size, src.bits == 8 ? 0x80 : 0);   // silence in the format
        const void* outer = gPulling;
        gPulling = this;
        const bool more = fn(scratch.data(), size);
        gPulling = outer;
        float from[2], to[2];
        {
            std::lock_guard<std::mutex> guard(lock);
            if (gen != generation) return Result::Stale;
            if (!more) ended = true;
            to[0] = pan[0];
            to[1] = pan[1];
            from[0] = panFresh ? pan[0] : played[0];
            from[1] = panFresh ? pan[1] : played[1];
            if (more) {
                played[0] = to[0];
                played[1] = to[1];
                panFresh = false;
            }
        }
        if (!more) {
            SDL_FlushAudioStream(sdl);   // what is queued plays to its end
            return Result::Over;
        }
        CaptureChunk(index, src, scratch.data(), size);
        thread_local std::vector<int16_t> stereo;
        SoundToStereo(scratch.data(), size, src, from, to, stereo);
        const int stereoBytes = static_cast<int>(stereo.size() * sizeof(int16_t));
        if (!SDL_PutAudioStreamData(sdl, stereo.data(), stereoBytes)) {
            tpw::Log("sound: stream %u refused a chunk (%s)", index, SDL_GetError());
        }
        ++pulled;
        if (bytes != nullptr) *bytes = static_cast<uint32_t>(stereoBytes);
        return Result::Delivered;
    }

    // SDL's get callback: `additional` bytes (16-bit stereo, as the chunks go in) are needed that the stream does not
    // hold; pull chunks until that is met (at least one chunk when anything is needed).
    uint32_t Feed(int additional) {
        uint32_t delivered = 0;
        int bytes = 0;
        while (bytes < additional) {
            uint32_t size = 0;
            if (PullOnce(false, &size) != Result::Delivered) break;
            ++delivered;
            bytes += static_cast<int>(size);
        }
        return delivered;
    }

    // The stream leaves the device (`bound` is already false, no lock held). Inside its own callback the unbind is only
    // noted.
    void Detach() {
        if (gPulling == this) {
            std::lock_guard<std::mutex> guard(lock);
            unbindPending = true;
            return;
        }
        SDL_UnbindAudioStream(sdl);   // waits for a callback in progress
        std::lock_guard<std::mutex> guard(lock);
        unbindPending = false;
    }

    // The stream joins the device (`bound` is already true, no lock held). A stream that never left SDL has nothing to do.
    bool Attach(SDL_AudioDeviceID device) {
        {
            std::lock_guard<std::mutex> guard(lock);
            if (unbindPending) {
                unbindPending = false;
                return true;
            }
        }
        return SDL_BindAudioStream(device, sdl);
    }
};

namespace {

void SDLCALL OnGet(void* userdata, SDL_AudioStream*, int additional, int) {
    static_cast<SoundDevice::Stream*>(userdata)->Feed(additional);
}

}  // namespace

SoundDevice::SoundDevice() {}

SoundDevice::~SoundDevice() {
    Close();
}

int SoundDevice::LiveStreams() {
    return gLiveStreams.load();
}

bool SoundDevice::CaptureActive() {
    std::lock_guard<std::mutex> guard(gCaptureLock);
    return gCapture != nullptr;
}

bool SoundDevice::Open(uint32_t rate, uint32_t streams) {
    if (mDevice != 0) return true;
    if (!gAudioInitialised) {
        // --mute: SDL's dummy driver takes the samples at the speed of a real device and drops them.
        if (tpw::config().mute && !SDL_WasInit(SDL_INIT_AUDIO)) SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            tpw::Log("sound: SDL audio failed to start (%s)", SDL_GetError());
            return false;
        }
        gAudioInitialised = true;
    }
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = static_cast<int>(rate);
    mDevice = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (mDevice == 0) {
        tpw::Log("sound: no audio device (%s)", SDL_GetError());
        return false;
    }
    SDL_AudioSpec actual;
    int frames = 0;
    if (SDL_GetAudioDeviceFormat(mDevice, &actual, &frames)) {
        tpw::Log("sound: %s, %d Hz, %d channels, %d frames per buffer, streams of 16-bit stereo at %u Hz",
                 SDL_GetAudioDeviceName(mDevice), actual.freq, actual.channels, frames, rate);
    }
    for (uint32_t i = 0; i < streams; ++i) {
        std::unique_ptr<Stream> stream(new Stream);
        stream->owner = this;
        stream->index = i;
        stream->sdl = SDL_CreateAudioStream(&spec, &spec);
        if (stream->sdl == nullptr) {
            tpw::Log("sound: cannot make stream %u (%s)", i, SDL_GetError());
            Close();
            return false;
        }
        ++gLiveStreams;
        SDL_SetAudioStreamGetCallback(stream->sdl, OnGet, stream.get());
        mStreams.push_back(std::move(stream));
    }
    CaptureOpen();
    return true;
}

void SoundDevice::Close() {
    for (auto& stream : mStreams) {
        {
            std::lock_guard<std::mutex> guard(stream->lock);
            ++stream->generation;
            stream->pull = nullptr;
            stream->hasSource = false;
            stream->bound = false;
        }
        SDL_UnbindAudioStream(stream->sdl);
        SDL_DestroyAudioStream(stream->sdl);   // waits for a callback in progress
        stream->sdl = nullptr;
        --gLiveStreams;
    }
    mStreams.clear();
    if (mDevice != 0) {
        SDL_CloseAudioDevice(mDevice);
        mDevice = 0;
        CaptureClose();
    }
}

bool SoundDevice::SetSource(uint32_t index, const SoundSource& source, uint32_t chunk) {
    if (index >= mStreams.size()) return false;
    Stream& stream = *mStreams[index];
    Stop(index);
    const uint32_t width = SoundBytesPerSample(source.bits);
    if (width == 0 || source.rate == 0 || source.channels < 1 || source.channels > 2 || chunk == 0) return false;
    SDL_AudioSpec spec;   // the chunks go in as 16-bit stereo at the sample's rate (PullOnce)
    SDL_zero(spec);
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = static_cast<int>(source.rate);
    if (!SDL_SetAudioStreamFormat(stream.sdl, &spec, nullptr)) {
        tpw::Log("sound: stream %u cannot take %u Hz, %u bits, %u channels (%s)", index, source.rate, source.bits,
                 source.channels, SDL_GetError());
        return false;
    }
    std::lock_guard<std::mutex> guard(stream.lock);
    stream.source = source;
    stream.chunk = chunk;
    stream.hasSource = true;
    stream.pulled = 0;
    stream.pan[0] = stream.pan[1] = 1.0f;
    stream.played[0] = stream.played[1] = 1.0f;
    stream.panFresh = true;
    return true;
}

void SoundDevice::ClearSource(uint32_t index) {
    if (index >= mStreams.size()) return;
    Stop(index);
    Stream& stream = *mStreams[index];
    std::lock_guard<std::mutex> guard(stream.lock);
    stream.hasSource = false;
    stream.chunk = 0;
    stream.source = SoundSource();
}

bool SoundDevice::HasSource(uint32_t index) const {
    if (index >= mStreams.size()) return false;
    std::lock_guard<std::mutex> guard(mStreams[index]->lock);
    return mStreams[index]->hasSource;
}

uint32_t SoundDevice::Chunk(uint32_t index) const {
    if (index >= mStreams.size()) return 0;
    std::lock_guard<std::mutex> guard(mStreams[index]->lock);
    return mStreams[index]->chunk;
}

bool SoundDevice::Play(uint32_t index, SoundPull pull) {
    if (index >= mStreams.size()) return false;
    Stream& stream = *mStreams[index];
    {
        std::lock_guard<std::mutex> guard(stream.lock);
        if (!stream.hasSource) return false;
        stream.pull = std::move(pull);
        stream.ended = false;
    }
    const Stream::Result result = stream.PullOnce(true, nullptr);
    if (result != Stream::Result::Delivered) return false;
    bool attach = false;
    {
        std::lock_guard<std::mutex> guard(stream.lock);
        stream.started = true;
        if (!stream.paused && !stream.bound) {
            stream.bound = true;
            attach = true;
        }
    }
    if (attach && !stream.Attach(mDevice)) {
        tpw::Log("sound: stream %u cannot be bound (%s)", index, SDL_GetError());
        std::lock_guard<std::mutex> guard(stream.lock);
        stream.bound = false;
        return false;
    }
    return true;
}

void SoundDevice::Stop(uint32_t index) {
    if (index >= mStreams.size()) return;
    Stream& stream = *mStreams[index];
    bool wasBound;
    {
        std::lock_guard<std::mutex> guard(stream.lock);
        ++stream.generation;
        stream.pull = nullptr;
        stream.started = false;
        stream.ended = false;
        wasBound = stream.bound || stream.unbindPending;
        stream.bound = false;
    }
    if (wasBound) stream.Detach();
    SDL_ClearAudioStream(stream.sdl);
}

void SoundDevice::Pause(uint32_t index) {
    if (index >= mStreams.size()) return;
    Stream& stream = *mStreams[index];
    bool wasBound;
    {
        std::lock_guard<std::mutex> guard(stream.lock);
        stream.paused = true;
        wasBound = stream.bound || stream.unbindPending;
        stream.bound = false;
    }
    if (wasBound) stream.Detach();
}

void SoundDevice::Restart(uint32_t index) {
    if (index >= mStreams.size()) return;
    Stream& stream = *mStreams[index];
    bool attach = false;
    {
        std::lock_guard<std::mutex> guard(stream.lock);
        stream.paused = false;
        if (stream.started && !stream.bound) {
            stream.bound = true;
            attach = true;
        }
    }
    if (attach && !stream.Attach(mDevice)) {
        tpw::Log("sound: stream %u cannot be bound again (%s)", index, SDL_GetError());
        std::lock_guard<std::mutex> guard(stream.lock);
        stream.bound = false;
    }
}

bool SoundDevice::IsPaused(uint32_t index) const {
    if (index >= mStreams.size()) return false;
    std::lock_guard<std::mutex> guard(mStreams[index]->lock);
    return mStreams[index]->paused;
}

bool SoundDevice::SetGain(uint32_t index, float gain) {
    if (index >= mStreams.size()) return false;
    return SDL_SetAudioStreamGain(mStreams[index]->sdl, gain);
}

float SoundDevice::Gain(uint32_t index) const {
    if (index >= mStreams.size()) return 0.0f;
    return SDL_GetAudioStreamGain(mStreams[index]->sdl);
}

void SoundDevice::SetPan(uint32_t index, float left, float right) {
    if (index >= mStreams.size()) return;
    std::lock_guard<std::mutex> guard(mStreams[index]->lock);
    mStreams[index]->pan[0] = left;
    mStreams[index]->pan[1] = right;
}

void SoundDevice::Pan(uint32_t index, float* left, float* right) const {
    float pan[2] = {0.0f, 0.0f};
    if (index < mStreams.size()) {
        std::lock_guard<std::mutex> guard(mStreams[index]->lock);
        pan[0] = mStreams[index]->pan[0];
        pan[1] = mStreams[index]->pan[1];
    }
    if (left != nullptr) *left = pan[0];
    if (right != nullptr) *right = pan[1];
}

bool SoundDevice::IsBound(uint32_t index) const {
    if (index >= mStreams.size()) return false;
    std::lock_guard<std::mutex> guard(mStreams[index]->lock);
    return mStreams[index]->bound;
}

bool SoundDevice::IsEnded(uint32_t index) const {
    if (index >= mStreams.size()) return false;
    std::lock_guard<std::mutex> guard(mStreams[index]->lock);
    return mStreams[index]->ended;
}

uint32_t SoundDevice::Pulled(uint32_t index) const {
    if (index >= mStreams.size()) return 0;
    return mStreams[index]->pulled.load();
}

// The stream holds 16-bit stereo: its counts are turned back into the source's bytes.
int SoundDevice::Queued(uint32_t index) const {
    if (index >= mStreams.size()) return 0;
    uint32_t frameBytes;
    {
        std::lock_guard<std::mutex> guard(mStreams[index]->lock);
        frameBytes = FrameBytes(mStreams[index]->source);
    }
    const int queued = SDL_GetAudioStreamQueued(mStreams[index]->sdl);
    return queued <= 0 ? queued : queued / static_cast<int>(kStereoFrameBytes) * static_cast<int>(frameBytes);
}

uint32_t SoundDevice::Feed(uint32_t index, int bytes) {
    if (index >= mStreams.size()) return 0;
    uint32_t frameBytes;
    {
        std::lock_guard<std::mutex> guard(mStreams[index]->lock);
        frameBytes = FrameBytes(mStreams[index]->source);
    }
    if (frameBytes == 0) return 0;
    const int frames = (bytes + static_cast<int>(frameBytes) - 1) / static_cast<int>(frameBytes);
    return mStreams[index]->Feed(frames * static_cast<int>(kStereoFrameBytes));
}

void SoundDevice::SetDevicePaused(bool paused) {
    if (mDevice == 0) return;
    if (paused) {
        SDL_PauseAudioDevice(mDevice);
    } else {
        SDL_ResumeAudioDevice(mDevice);
    }
}

}  // namespace tpw
