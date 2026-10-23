// sound_device.h: the SDL3 side of the host sound module (unit p-sound), in its own file so that
// the part that talks to SDL knows nothing of the game's classes and the test program can drive
// it with plain types. platform/sound_module.cpp (CSprocketsModule, CSprocketsChannel) sits on top.
//
// One SoundDevice holds one SDL audio device (the default playback device, opened as 16-bit stereo
// at the module's rate) and one SDL audio stream per game channel. A stream is not bound to the
// device until a sample plays; SDL pulls from it, and the stream's get callback calls the channel's
// pull function (the game's TbSoundPipeChannel::StreamerCallback) for one chunk at a time, so the
// game's pipes are drained exactly as fast as the speakers play. The callback runs on SDL's audio
// thread. Each chunk goes into its stream as 16-bit stereo at the sample's rate, with the stream's
// pan applied (SetPan); the counts below (Feed, Queued) are still in the sample's own format.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace tpw {

// What a sample is made of: samples per second, bits per sample (8: unsigned, 16: signed in the
// host's byte order, 32: signed) and channels (1 or 2).
struct SoundSource {
    uint32_t rate = 0;
    uint32_t bits = 0;
    uint32_t channels = 0;
};

// Fills `size` bytes at `buffer` with the next piece of the sample. True while the sample goes on,
// false when this was the end (the buffer then holds nothing worth playing, as the Mac's callback
// did not play it either).
using SoundPull = std::function<bool(void* buffer, uint32_t size)>;

class SoundDevice {
public:
    SoundDevice();
    ~SoundDevice();
    SoundDevice(const SoundDevice&) = delete;
    SoundDevice& operator=(const SoundDevice&) = delete;

    // Initialises SDL audio once (never quits it), opens the default playback device (S16, two
    // channels, `rate`) and makes `streams` unbound streams at gain 1. With --mute the SDL "dummy"
    // driver is asked for, so that the game's sounds still run their course in real time, unheard.
    // False (and a log line) when SDL cannot do that.
    bool Open(uint32_t rate, uint32_t streams);
    // Unbinds and destroys every stream and closes the device.
    void Close();
    bool IsOpen() const { return mDevice != 0; }
    uint32_t StreamCount() const { return static_cast<uint32_t>(mStreams.size()); }
    // SDL audio streams created and not yet destroyed in the whole process.
    static int LiveStreams();

    // The sample the stream is about to play. Stops the stream first. False when SDL cannot take
    // the format (bits other than 8, 16 or 32, a rate of 0, channels other than 1 or 2).
    // `chunk` is the number of bytes one pull asks for.
    bool SetSource(uint32_t stream, const SoundSource& source, uint32_t chunk);
    // Stops the stream and forgets its sample.
    void ClearSource(uint32_t stream);
    bool HasSource(uint32_t stream) const;
    uint32_t Chunk(uint32_t stream) const;

    // Pulls the first chunk now and, when it carries data, puts it in the stream and binds the
    // stream to the device (unless the stream is paused: it is bound by Restart). False when the
    // stream has no source, or the first pull said the sample was over (nothing plays).
    bool Play(uint32_t stream, SoundPull pull);
    // Unbinds the stream, drops what it had queued and forgets the pull function. The paused state is
    // kept: a paused channel stays paused until Restart, as on the Mac.
    void Stop(uint32_t stream);
    // Unbinds a playing stream and keeps its queued data; Restart binds it again.
    void Pause(uint32_t stream);
    void Restart(uint32_t stream);
    bool IsPaused(uint32_t stream) const;

    // The gain of the stream (SDL_SetAudioStreamGain), 1.0 at first.
    bool SetGain(uint32_t stream, float gain);
    float Gain(uint32_t stream) const;

    // How much of the sample each speaker plays (0..1), for a sound placed in the park; 1 and 1
    // at first and after SetSource. A pull plays its chunk moving from the last chunk's pair to
    // this one, so a sound that moves does not click.
    void SetPan(uint32_t stream, float left, float right);
    void Pan(uint32_t stream, float* left, float* right) const;

    // State, for the tests and the log.
    bool IsBound(uint32_t stream) const;       // the stream is bound to the device
    bool IsEnded(uint32_t stream) const;       // a pull answered false: nothing more is pulled
    uint32_t Pulled(uint32_t stream) const;    // chunks put in the stream since the source was set
    int Queued(uint32_t stream) const;         // bytes queued in the stream, in the source format
    // What SDL's get callback does, called by hand: pulls chunks until `bytes` source bytes are
    // delivered. Answers the number of chunks put in the stream.
    uint32_t Feed(uint32_t stream, int bytes);
    // Pauses or resumes the whole device (the test freezes SDL's own pulling with it).
    void SetDevicePaused(bool paused);

    // Every chunk put in a stream is appended to the file TPW_SOUND_CAPTURE names (raw samples in
    // the stream's source format) and described in a line of "<file>.log"; set when Open ran.
    static bool CaptureActive();

    struct Stream;

private:
    uint32_t mDevice = 0;
    std::vector<std::unique_ptr<Stream>> mStreams;
};

// The bytes of one sample in the formats the stream takes: 1 for 8 bits, 2 for 16, 4 for 32, 0 for
// a width SDL cannot play.
uint32_t SoundBytesPerSample(uint32_t bits);

// `bytes` of `source` samples as 16-bit stereo frames in `out`, each frame's left and right scaled
// by gains that move in a straight line from `from` (left, right) to `to` over the chunk, reaching
// `to` on its last frame. 8 bits become 16 as SDL widens them; 32 keep their top 16. A mono sample
// plays the same sample on both sides.
void SoundToStereo(const void* in, uint32_t bytes, const SoundSource& source, const float from[2], const float to[2],
                   std::vector<int16_t>& out);

}  // namespace tpw
