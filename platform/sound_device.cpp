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
//
// With no device the watcher is the audio thread. SDL_UnbindAudioStream still waits for a pull in progress (it takes the stream's
// lock, which SDL_GetAudioStreamData holds across the callback), and a pull that starts after a Pause or a Stop finds the stream
// paused or without its pull function and asks the game for nothing. `bound` then says only that the stream is meant to play;
// nothing is bound in SDL until Adopt, which runs on the watcher when no pull can be in progress.
#include "sound_device.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "platform.h"

namespace tpw {

namespace {

std::atomic<int> gLiveStreams{0};
bool gAudioInitialised = false;   // SDL_InitSubSystem(SDL_INIT_AUDIO) ran once; never undone. On the main thread only.
std::atomic<bool> gDriverUp{false};   // gAudioInitialised, for the watcher

// How often the watcher pulls with no device, tries for one, and looks at whether SDL has followed the default.
constexpr std::chrono::milliseconds kPumpStep{20};
constexpr std::chrono::milliseconds kRetryDevice{2000};
constexpr std::chrono::milliseconds kRetryDriver{10000};
constexpr std::chrono::milliseconds kCheckDefault{2000};
constexpr uint32_t kPumpLongest = 250;   // the milliseconds one round makes up for, after the computer has slept

std::mutex gWatchingLock;
std::vector<SoundDevice*> gWatching;

// SDL_InitSubSystem is for the main thread, so a driver that did not start is tried again there: SDL runs this at its next
// event pump. A rare case (Windows has two drivers to try), and the only retry the game's own thread ever does.
void SDLCALL StartDriverOnMainThread(void*) {
    if (gAudioInitialised) return;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return;
    gAudioInitialised = true;
    gDriverUp = true;
}

// The default device opened. TPW_SOUND_NO_DEVICE: this fails as it does on a computer whose sound device will not open, which
// SDL's dummy driver never does (tests/host_no_sound_device.cpp, and a play through run_launcher.sh). "after:N" ends that N
// seconds after the first try, for a device that arrives during a play.
SDL_AudioDeviceID OpenDefault(uint32_t rate) {
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = static_cast<int>(rate);
    if (const char* hook = SDL_getenv("TPW_SOUND_NO_DEVICE")) {
        static const std::chrono::steady_clock::time_point first = std::chrono::steady_clock::now();
        const bool after = std::strncmp(hook, "after:", 6) == 0;
        if (!after || std::chrono::steady_clock::now() - first < std::chrono::seconds(std::atoi(hook + 6))) {
            SDL_SetError("TPW_SOUND_NO_DEVICE is set");
            return 0;
        }
    }
    return SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
}

void LogDevice(SDL_AudioDeviceID device, uint32_t rate) {
    SDL_AudioSpec actual;
    int frames = 0;
    if (SDL_GetAudioDeviceFormat(device, &actual, &frames)) {
        tpw::Log("sound: %s, %d Hz, %d channels, %d frames per buffer, streams of 16-bit stereo at %u Hz",
                 SDL_GetAudioDeviceName(device), actual.freq, actual.channels, frames, rate);
    }
}

// The stream whose pull the calling thread is inside, if any.
thread_local const void* gPulling = nullptr;

// TPW_SOUND_CAPTURE: the raw samples of every chunk, and a line per chunk saying whose they are. With gPortSoundPan, beside
// them, "<file>.stereo" keeps each chunk as it went into its stream (16-bit stereo, the pan applied) and "<file>.stereo.log"
// a line per chunk with its stream, rate, frames, the milliseconds since the capture opened and the pan it moved from and to.
std::mutex gCaptureLock;
FILE* gCapture = nullptr;
FILE* gCaptureLog = nullptr;
FILE* gCaptureStereo = nullptr;
FILE* gCaptureStereoLog = nullptr;
std::chrono::steady_clock::time_point gCaptureStart;

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
    if (gPortSoundPan) {   // the stereo chunks exist only with the pan
        gCaptureStereo = std::fopen((std::string(path) + ".stereo").c_str(), "ab");
        gCaptureStereoLog = std::fopen((std::string(path) + ".stereo.log").c_str(), "ab");
    }
    gCaptureStart = std::chrono::steady_clock::now();
    tpw::Log("sound: capturing the chunks to %s", path);
}

void CaptureClose() {
    std::lock_guard<std::mutex> guard(gCaptureLock);
    if (gCapture != nullptr) std::fclose(gCapture);
    if (gCaptureLog != nullptr) std::fclose(gCaptureLog);
    if (gCaptureStereo != nullptr) std::fclose(gCaptureStereo);
    if (gCaptureStereoLog != nullptr) std::fclose(gCaptureStereoLog);
    gCapture = nullptr;
    gCaptureLog = nullptr;
    gCaptureStereo = nullptr;
    gCaptureStereoLog = nullptr;
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

void CaptureStereoChunk(uint32_t stream, const SoundSource& source, const std::vector<int16_t>& stereo, const float from[2],
                        const float to[2]) {
    std::lock_guard<std::mutex> guard(gCaptureLock);
    if (gCaptureStereo == nullptr) return;
    std::fwrite(stereo.data(), sizeof(int16_t), stereo.size(), gCaptureStereo);
    std::fflush(gCaptureStereo);
    if (gCaptureStereoLog != nullptr) {
        const long long ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - gCaptureStart).count();
        std::fprintf(gCaptureStereoLog, "stream %u rate %u frames %u ms %lld from %.4f %.4f to %.4f %.4f\n", stream, source.rate,
                     static_cast<uint32_t>(stereo.size() / 2), ms, from[0], from[1], to[0], to[1]);
        std::fflush(gCaptureStereoLog);
    }
}

SDL_AudioFormat FormatOf(uint32_t bits) {
    switch (bits) {
    case 8: return SDL_AUDIO_U8;
    case 16: return SDL_AUDIO_S16;
    case 32: return SDL_AUDIO_S32;
    default: return SDL_AUDIO_UNKNOWN;
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
        default: {   // as SDL: to a float by 2^-31, back by 2^15 rounded to the nearest, below
            int32_t s;
            std::memcpy(&s, at, 4);
            return static_cast<int32_t>(std::lrintf(static_cast<float>(s) * (1.0f / 65536.0f)));
        }
        }
    };
    for (uint32_t i = 0; i < frames; ++i) {
        const float step = static_cast<float>(i + 1) / static_cast<float>(frames);
        const float gains[2] = {from[0] + (to[0] - from[0]) * step, from[1] + (to[1] - from[1]) * step};
        for (uint32_t side = 0; side < 2; ++side) {
            const int32_t s = sample(i * source.channels + (source.channels == 2 ? side : 0));
            const float scaled = std::nearbyint(static_cast<float>(s) * gains[side]);   // to the nearest, as SDL's F32 to S16
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
    bool panned = false;              // gPortSoundPan when the source was set: chunks go in as 16-bit stereo; else as they are
    std::atomic<uint32_t> pulled{0};

    enum class Result { Delivered, Over, Stale };

    // One pull and, when it carries data, its chunk into the SDL stream: as it is, or with gPortSoundPan as 16-bit stereo with
    // the pan. A paused stream is not pulled by SDL's callback (`firstChunk` is the one pull that Play makes whatever the
    // pause). `bytes` gets the size of what went into the stream.
    Result PullOnce(bool firstChunk, uint32_t* bytes) {
        SoundPull fn;
        SoundSource src;
        uint32_t size;
        uint64_t gen;
        bool stereoOut;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (!hasSource || !pull || ended || (paused && !firstChunk)) return Result::Stale;
            fn = pull;
            src = source;
            size = chunk;
            gen = generation;
            stereoOut = panned;
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
        const void* data = scratch.data();
        int putBytes = static_cast<int>(size);
        thread_local std::vector<int16_t> stereo;
        if (stereoOut) {
            SoundToStereo(scratch.data(), size, src, from, to, stereo);
            CaptureStereoChunk(index, src, stereo, from, to);
            data = stereo.data();
            putBytes = static_cast<int>(stereo.size() * sizeof(int16_t));
        }
        if (!SDL_PutAudioStreamData(sdl, data, putBytes)) {
            tpw::Log("sound: stream %u refused a chunk (%s)", index, SDL_GetError());
        }
        ++pulled;
        if (bytes != nullptr) *bytes = static_cast<uint32_t>(putBytes);
        return Result::Delivered;
    }

    // SDL's get callback: `additional` bytes (in the format the chunks go in) are needed that the stream does not
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

    // The stream joins the device (`bound` is already true, no lock held). A stream that never left SDL has nothing to do, nor
    // has one with no device yet: the watcher pulls it, and Settle binds it when the device comes.
    bool Attach(SDL_AudioDeviceID device) {
        {
            std::lock_guard<std::mutex> guard(lock);
            unbindPending = false;
        }
        if (device == 0) return true;
        const SDL_AudioDeviceID at = SDL_GetAudioStreamDevice(sdl);
        if (at != device) {
            if (at != 0) {
                if (gPulling == this) return true;   // left on a device given up, in its own callback: Settle has it
                SDL_UnbindAudioStream(sdl);
            }
            if (!SDL_BindAudioStream(device, sdl)) {
                const SDL_AudioDeviceID won = SDL_GetAudioStreamDevice(sdl);
                if (won == 0) return false;
                if (won != device) return Attach(owner->mDevice.load());   // the watcher's Settle got there first, with a newer device
            }
        }
        const SDL_AudioDeviceID now = owner->mDevice.load();   // the watcher gave that device up as this came to it
        return now == 0 || now == device || Attach(now);
    }

    // The watcher, with a new device: the stream is bound to it if it is meant to play, and to nothing if not. A command from the game's thread can
    // cross this, so what SDL has is compared with what is meant until they agree.
    void Settle(SDL_AudioDeviceID device) {
        bool refused = false;
        for (int tries = 0; tries < 4; ++tries) {
            bool meant;
            {
                std::lock_guard<std::mutex> guard(lock);
                meant = bound;
            }
            const SDL_AudioDeviceID at = SDL_GetAudioStreamDevice(sdl);
            if (at == (meant ? device : 0)) return;
            if (at != 0) SDL_UnbindAudioStream(sdl);
            refused = meant && !SDL_BindAudioStream(device, sdl) && SDL_GetAudioStreamDevice(sdl) != device;
        }
        if (refused) tpw::Log("sound: stream %u cannot be bound to the new device (%s)", index, SDL_GetError());
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
    if (mOpen) return true;
    const bool mute = tpw::config().mute;
    if (!gAudioInitialised) {
        // --mute: SDL's dummy driver takes the samples at the speed of a real device and drops them.
        if (mute && !SDL_WasInit(SDL_INIT_AUDIO)) SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
        if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            gAudioInitialised = true;
            gDriverUp = true;
        } else {
            tpw::Log("sound: SDL audio failed to start (%s)", SDL_GetError());
            if (mute) return false;
        }
    }
    if (gAudioInitialised) {
        const SDL_AudioDeviceID device = OpenDefault(rate);
        mDevice = device;
        if (device == 0) {
            tpw::Log("sound: no audio device (%s)", SDL_GetError());
            if (mute) return false;
        } else {
            LogDevice(device, rate);
        }
    }
    if (mDevice.load() == 0) tpw::Log("sound: the game plays silently until a sound device opens");
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = static_cast<int>(rate);
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
    mOpen = true;
    if (!mute) {
        mStopWatching = false;
        mWatcher = std::thread([this, rate] { Watch(rate); });
        std::lock_guard<std::mutex> guard(gWatchingLock);
        gWatching.push_back(this);
    }
    return true;
}

void SoundDevice::StopWatcher() {
    if (!mWatcher.joinable()) return;
    {
        std::lock_guard<std::mutex> guard(mWatchLock);
        mStopWatching = true;
    }
    mWatchWake.notify_all();
    mWatcher.join();
    std::lock_guard<std::mutex> guard(gWatchingLock);
    gWatching.erase(std::remove(gWatching.begin(), gWatching.end(), this), gWatching.end());
}

void SoundDevice::StopWatchers() {
    std::vector<SoundDevice*> devices;
    {
        std::lock_guard<std::mutex> guard(gWatchingLock);
        devices = gWatching;
    }
    for (SoundDevice* device : devices) device->StopWatcher();
}

// The watcher's thread. No log line of its own while nothing changes: "no audio device" has been said once by Open.
void SoundDevice::Watch(uint32_t rate) {
    using Clock = std::chrono::steady_clock;
    Clock::time_point lastPump = Clock::now();
    Clock::time_point nextDevice = lastPump + kRetryDevice;
    Clock::time_point nextDriver = lastPump + kRetryDriver;
    uint32_t framesOver = 0;
    int staleChecks = 0;
    bool staleLogged = false;
    std::unique_lock<std::mutex> lock(mWatchLock);
    while (!mStopWatching) {
        const bool silent = mDevice.load() == 0;
        if (silent) {
            mWatchWake.wait_for(lock, kPumpStep);
        } else {
            mWatchWake.wait_for(lock, kCheckDefault);
        }
        if (mStopWatching) break;
        lock.unlock();
        const Clock::time_point now = Clock::now();
        if (silent) {
            // SDL's get callback, as a device would run it: what has been played since the last round is taken and dropped.
            const uint64_t ms = std::min<uint64_t>(
                kPumpLongest, std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPump).count());
            lastPump = now;
            const uint64_t scaled = ms * rate + framesOver;
            framesOver = static_cast<uint32_t>(scaled % 1000);
            if (!mDevicePaused.load()) Pump(static_cast<uint32_t>(scaled / 1000) * kStereoFrameBytes);
            if (gDriverUp.load()) {
                if (now >= nextDevice) {
                    nextDevice = now + kRetryDevice;
                    const SDL_AudioDeviceID device = OpenDefault(rate);
                    if (device != 0) {
                        tpw::Log("sound: a sound device has opened");
                        Adopt(device);
                        LogDevice(device, rate);
                        lastPump = Clock::now();
                    }
                }
            } else if (now >= nextDriver) {
                nextDriver = now + kRetryDriver;
                // SDL_RunOnMainThread runs it here, on this thread, when SDL's events are not up yet: then it waits.
                if (SDL_WasInit(SDL_INIT_EVENTS)) SDL_RunOnMainThread(StartDriverOnMainThread, nullptr, false);
            }
        } else {
            FollowDefault(rate, staleChecks, staleLogged);
        }
        lock.lock();
    }
}

void SoundDevice::Pump(uint32_t bytes) {
    if (bytes == 0) return;
    thread_local std::vector<uint8_t> drain;
    drain.resize(bytes);
    for (auto& stream : mStreams) {
        {
            std::lock_guard<std::mutex> guard(stream->lock);
            if (!stream->bound) continue;
        }
        SDL_GetAudioStreamData(stream->sdl, drain.data(), static_cast<int>(bytes));
    }
}

// The watcher, with the streams on no device: from here they play on this one. Their gains and pauses stay as the game set them.
void SoundDevice::Adopt(uint32_t device) {
    if (mDevicePaused.load()) SDL_PauseAudioDevice(device);
    mDevice = device;
    if (!mDevicePaused.load()) SDL_ResumeAudioDevice(device);   // a resume that crossed the store found no device to resume
    for (auto& stream : mStreams) stream->Settle(device);
}

// SDL moves a device opened as the default to the new default itself, but when the new one refuses to open it leaves it on the
// old (SDL_DefaultAudioDeviceChanged), which may be gone, with no second try: silence for the rest of the play. A difference of
// names on two looks in a row is that (a move in progress is over by the second); the default is then opened afresh. The same
// name on a device that came back is not noticed. TPW_SOUND_DEFAULT_NAME stands in for the default's name in the test.
void SoundDevice::FollowDefault(uint32_t rate, int& staleChecks, bool& staleLogged) {
    const SDL_AudioDeviceID device = mDevice.load();
    const char* mine = SDL_GetAudioDeviceName(device);
    const char* hook = SDL_getenv("TPW_SOUND_DEFAULT_NAME");
    const char* current = hook != nullptr ? hook : SDL_GetAudioDeviceName(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
    if (mine == nullptr || current == nullptr || std::strcmp(mine, current) == 0) {
        staleChecks = 0;
        staleLogged = false;
        return;
    }
    if (++staleChecks < 2) return;
    const SDL_AudioDeviceID fresh = OpenDefault(rate);
    if (fresh == 0) {
        if (!staleLogged) tpw::Log("sound: the default device changed and will not open yet (%s)", SDL_GetError());
        staleLogged = true;
        return;
    }
    tpw::Log("sound: the default device changed, and is opened afresh");
    staleChecks = 0;
    staleLogged = false;
    // The old one is only paused, so that no pulls run on it, and stays open until Close: a command on the game's thread may be
    // calling SDL with its id right now, and SDL frees a logical device under a call that is waiting for it (a crash in
    // ObtainLogicalAudioDevice, seen with the "stress" test). So few are given up in a play that the cost is nothing.
    mDevice = 0;
    SDL_PauseAudioDevice(device);
    mGivenUp.push_back(device);
    Adopt(fresh);
    LogDevice(fresh, rate);
}

void SoundDevice::Close() {
    StopWatcher();
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
    for (const uint32_t givenUp : mGivenUp) SDL_CloseAudioDevice(givenUp);
    mGivenUp.clear();
    const SDL_AudioDeviceID device = mDevice.exchange(0);
    if (device != 0) SDL_CloseAudioDevice(device);
    if (mOpen || device != 0) CaptureClose();
    mOpen = false;
}

bool SoundDevice::SetSource(uint32_t index, const SoundSource& source, uint32_t chunk) {
    if (index >= mStreams.size()) return false;
    Stream& stream = *mStreams[index];
    Stop(index);
    const uint32_t width = SoundBytesPerSample(source.bits);
    if (width == 0 || source.rate == 0 || source.channels < 1 || source.channels > 2 || chunk == 0) return false;
    const bool panned = gPortSoundPan;   // with it, the chunks go in as 16-bit stereo at the sample's rate (PullOnce)
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = panned ? SDL_AUDIO_S16 : FormatOf(source.bits);
    spec.channels = panned ? 2 : static_cast<int>(source.channels);
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
    stream.panned = panned;
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
    // The Mac's play and restart are the same command (UCommandPlay, flags 1), so a play resumes a paused stream.
    bool attach = false;
    {
        std::lock_guard<std::mutex> guard(stream.lock);
        stream.started = true;
        stream.paused = false;
        if (!stream.bound) {
            stream.bound = true;
            attach = true;
        }
    }
    if (attach && !stream.Attach(mDevice.load())) {
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
    if (attach && !stream.Attach(mDevice.load())) {
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

// A panned stream holds 16-bit stereo: its counts are turned back into the source's bytes.
int SoundDevice::Queued(uint32_t index) const {
    if (index >= mStreams.size()) return 0;
    uint32_t frameBytes;
    bool panned;
    {
        std::lock_guard<std::mutex> guard(mStreams[index]->lock);
        frameBytes = FrameBytes(mStreams[index]->source);
        panned = mStreams[index]->panned;
    }
    const int queued = SDL_GetAudioStreamQueued(mStreams[index]->sdl);
    if (!panned) return queued;
    return queued <= 0 ? queued : queued / static_cast<int>(kStereoFrameBytes) * static_cast<int>(frameBytes);
}

uint32_t SoundDevice::Feed(uint32_t index, int bytes) {
    if (index >= mStreams.size()) return 0;
    uint32_t frameBytes;
    bool panned;
    {
        std::lock_guard<std::mutex> guard(mStreams[index]->lock);
        frameBytes = FrameBytes(mStreams[index]->source);
        panned = mStreams[index]->panned;
    }
    if (!panned) return mStreams[index]->Feed(bytes);
    if (frameBytes == 0) return 0;
    const int frames = (bytes + static_cast<int>(frameBytes) - 1) / static_cast<int>(frameBytes);
    return mStreams[index]->Feed(frames * static_cast<int>(kStereoFrameBytes));
}

uint32_t SoundDevice::StreamDevice(uint32_t index) const {
    if (index >= mStreams.size()) return 0;
    return SDL_GetAudioStreamDevice(mStreams[index]->sdl);
}

void SoundDevice::SetDevicePaused(bool paused) {
    mDevicePaused = paused;
    const SDL_AudioDeviceID device = mDevice.load();
    if (device == 0) return;
    if (paused) {
        SDL_PauseAudioDevice(device);
    } else {
        SDL_ResumeAudioDevice(device);
    }
}

}  // namespace tpw
