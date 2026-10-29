// quicktime.cpp: QuickTime's Movie Toolbox, and the few Toolbox calls only the game's movie
// player makes.
//
// The game's films are QuickTime files (Sorenson video at half width, IMA ADPCM sound).
// They are decoded with FFmpeg (built from third_party/ffmpeg when TPW_MOVIES is on, the
// default): MoviesTask draws the frame that is due into the window's back buffer, scaled to
// the movie's box by the graphics card (platform/film_frame.h), and presents it, and the
// sound goes to an SDL audio stream. With TPW_MOVIES off, or with the launcher's
// --no-movies, OpenMovieFile answers that the routine is not implemented; the game's player
// reports the error and says the movie has finished, as it does when a file is missing.
#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "film_frame.h"
#include "platform.h"
#include "platform/macdoze.h"
#include "platform/quicktime.h"

#ifdef TPW_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#endif

namespace {
constexpr OSErr kUnimplemented = -4;                      // unimpErr
[[maybe_unused]] constexpr OSErr kFileNotFound = -43;     // fnfErr
[[maybe_unused]] constexpr OSErr kNoMovie = -2048;        // noMovieFound
bool gToldAboutMovies = false;

#ifdef TPW_HAVE_FFMPEG
using Clock = std::chrono::steady_clock;

// A decoded frame, kept as the decoder gave it: only the one that is shown is converted (Present), not those stepped
// over while catching up (MoviesTask).
struct Picture {
    AVFrame* frame = nullptr;
    double time = 0;            // seconds from the start of the movie
};
}  // namespace

// What a Movie handle leads to.
struct MovieRecord {
    MovieRecord* self = nullptr;        // the handle is the address of this field
    mutable bool boxLogged = false;     // the box was logged (once a film)
    AVFormatContext* format = nullptr;
    AVCodecContext* video = nullptr;
    AVCodecContext* audio = nullptr;
    int videoIndex = -1, audioIndex = -1;
    SwsContext* scale = nullptr;
    AVPacket* packet = nullptr;
    SDL_AudioStream* sound = nullptr;
    Picture shown, next;                // the frame on screen and the one decoded ahead of time
    std::vector<uint8_t> rgb;           // the shown frame at its own size, top row first, 3 bytes a pixel
    tpw::film::Texture texture;         // where it goes to be drawn
    bool haveShown = false, haveNext = false, ended = false;
    Rect box = {0, 0, 0, 0};            // where it plays, in the window's coordinates
    bool playing = false;
    double rate = 1.0;
    double position = 0;                // seconds played before `since`
    Clock::time_point since;
    int16_t volume = 0x100;
};

namespace {
std::map<int16_t, std::filesystem::path>& OpenFiles() {
    static auto* files = new std::map<int16_t, std::filesystem::path>;
    return *files;
}
int16_t gNextFile = 100;

MovieRecord* Of(Movie movie) { return movie != nullptr ? *movie : nullptr; }

double Now(const MovieRecord& m) {
    if (!m.playing) return m.position;
    return m.position + std::chrono::duration<double>(Clock::now() - m.since).count() * m.rate;
}

// The size the movie's box has in the window's pixels (a high-density window has more
// pixels than points).
void BoxInPixels(const MovieRecord& m, int& x, int& top, int& width, int& height, int& windowHeight) {
    int pointsWide = 0, pointsHigh = 0, pixelsWide = 0, pixelsHigh = 0;
    tpw::WindowSizes(pointsWide, pointsHigh, pixelsWide, pixelsHigh);
    const double sx = pointsWide > 0 ? static_cast<double>(pixelsWide) / pointsWide : 1.0;
    const double sy = pointsHigh > 0 ? static_cast<double>(pixelsHigh) / pointsHigh : 1.0;
    x = static_cast<int>(m.box.left * sx);
    top = static_cast<int>(m.box.top * sy);
    width = static_cast<int>((m.box.right - m.box.left) * sx);
    height = static_cast<int>((m.box.bottom - m.box.top) * sy);
    windowHeight = pixelsHigh;
}

void QueueSound(MovieRecord& m, const AVFrame* frame) {
    if (m.sound == nullptr) return;
    const int channels = m.audio->ch_layout.nb_channels;
    const auto format = static_cast<AVSampleFormat>(frame->format);
    if (format == AV_SAMPLE_FMT_S16 || (format == AV_SAMPLE_FMT_S16P && channels == 1)) {
        SDL_PutAudioStreamData(m.sound, frame->data[0], frame->nb_samples * channels * 2);
    } else if (format == AV_SAMPLE_FMT_S16P) {
        std::vector<int16_t> mixed(static_cast<size_t>(frame->nb_samples) * channels);
        for (int c = 0; c < channels; ++c) {
            const auto* plane = reinterpret_cast<const int16_t*>(frame->data[c]);
            for (int i = 0; i < frame->nb_samples; ++i) mixed[static_cast<size_t>(i) * channels + c] = plane[i];
        }
        SDL_PutAudioStreamData(m.sound, mixed.data(), static_cast<int>(mixed.size() * 2));
    }
}

// The decoded frame as RGB at its own size (the graphics card scales it to the box).
void Convert(MovieRecord& m, const AVFrame* frame) {
    m.scale = sws_getCachedContext(m.scale, frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), frame->width,
                                   frame->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
    m.rgb.resize(static_cast<size_t>(frame->width) * frame->height * 3);
    uint8_t* planes[4] = {m.rgb.data(), nullptr, nullptr, nullptr};
    int strides[4] = {frame->width * 3, 0, 0, 0};
    if (m.scale != nullptr) sws_scale(m.scale, frame->data, frame->linesize, 0, frame->height, planes, strides);
}

// Reads the file on to the next video frame (sound met on the way is queued); false at
// the end of the movie.
bool DecodeNext(MovieRecord& m, Picture& out) {
    for (;;) {
        const int got = avcodec_receive_frame(m.video, out.frame);
        if (got == 0) {
            const AVRational base = m.format->streams[m.videoIndex]->time_base;
            const int64_t stamp = out.frame->best_effort_timestamp;
            out.time = stamp == AV_NOPTS_VALUE ? 0.0 : stamp * av_q2d(base);
            return true;
        }
        if (got == AVERROR_EOF) return false;
        if (got != AVERROR(EAGAIN)) return false;
        const int read = av_read_frame(m.format, m.packet);
        if (read < 0) {
            avcodec_send_packet(m.video, nullptr);   // the end: let the decoder give what it holds
            continue;
        }
        if (m.packet->stream_index == m.videoIndex) {
            avcodec_send_packet(m.video, m.packet);
        } else if (m.packet->stream_index == m.audioIndex && m.audio != nullptr) {
            if (avcodec_send_packet(m.audio, m.packet) == 0) {
                AVFrame* sound = av_frame_alloc();
                while (avcodec_receive_frame(m.audio, sound) == 0) {
                    QueueSound(m, sound);
                    av_frame_unref(sound);
                }
                av_frame_free(&sound);
            }
        }
        av_packet_unref(m.packet);
    }
}

// The picture into the back buffer at the movie's box, on black, and onto the screen. The
// game's own OpenGL state is put back as it was.
void Present(MovieRecord& m, const Picture& picture) {
    int x, top, width, height, windowHeight;
    BoxInPixels(m, x, top, width, height, windowHeight);
    if (windowHeight <= 0 || picture.frame == nullptr || picture.frame->width <= 0 || picture.frame->height <= 0) return;
    if (width <= 0 || height <= 0) {   // no box: the film at its own size
        width = picture.frame->width;
        height = picture.frame->height;
    }
    if (!m.boxLogged) {   // once a film: where it lands (a box off the surface is the first thing to look at)
        m.boxLogged = true;
        tpw::Log("movie: box (%d, %d) %dx%d pixels on a surface %d high", x, top, width, height, windowHeight);
    }
    Convert(m, picture.frame);
    int pointsWide, pointsHigh, pixelsWide, pixelsHigh;
    tpw::WindowSizes(pointsWide, pointsHigh, pixelsWide, pixelsHigh);
    tpw::film::Draw(m.texture, m.rgb.data(), picture.frame->width, picture.frame->height, tpw::film::Box{x, top, width, height},
                    pixelsWide, pixelsHigh);
    tpw::SwapBuffers();
}

void Free(MovieRecord* m) {
    if (m->sound != nullptr) SDL_DestroyAudioStream(m->sound);
    if (m->scale != nullptr) sws_freeContext(m->scale);
    tpw::film::Release(m->texture);
    if (m->shown.frame != nullptr) av_frame_free(&m->shown.frame);
    if (m->next.frame != nullptr) av_frame_free(&m->next.frame);
    if (m->packet != nullptr) av_packet_free(&m->packet);
    if (m->video != nullptr) avcodec_free_context(&m->video);
    if (m->audio != nullptr) avcodec_free_context(&m->audio);
    if (m->format != nullptr) avformat_close_input(&m->format);
    delete m;
}

AVCodecContext* OpenDecoder(const AVStream* stream) {
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) return nullptr;
    AVCodecContext* context = avcodec_alloc_context3(codec);
    if (context == nullptr) return nullptr;
    if (avcodec_parameters_to_context(context, stream->codecpar) < 0 || avcodec_open2(context, codec, nullptr) < 0) {
        avcodec_free_context(&context);
        return nullptr;
    }
    return context;
}
#endif
}  // namespace

OSErr EnterMovies() { return 0; }
void ExitMovies() {}

#ifdef TPW_HAVE_FFMPEG

OSErr OpenMovieFile(const FSSpec* spec, int16_t* refNum, int8_t) {
    if (refNum != nullptr) *refNum = -1;
    if (!tpw::config().movies) {
        if (!gToldAboutMovies) tpw::Log("movies are switched off: the game skips them");
        gToldAboutMovies = true;
        return kUnimplemented;
    }
    const std::filesystem::path path = tpw::HostPathOfSpec(spec);
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_regular_file(path, ec)) return kFileNotFound;
    OpenFiles()[gNextFile] = path;
    if (refNum != nullptr) *refNum = gNextFile;
    ++gNextFile;
    return 0;
}

OSErr NewMovieFromFile(Movie* movie, int16_t refNum, int16_t*, unsigned char*, int16_t, bool*) {
    *movie = nullptr;
    const auto found = OpenFiles().find(refNum);
    if (found == OpenFiles().end()) return kFileNotFound;
    const std::filesystem::path path = found->second;
    OpenFiles().erase(found);
    auto* m = new MovieRecord;
    m->self = m;
    if (avformat_open_input(&m->format, path.string().c_str(), nullptr, nullptr) < 0 ||
        avformat_find_stream_info(m->format, nullptr) < 0) {
        tpw::Log("movie: cannot read %s", path.string().c_str());
        Free(m);
        return kNoMovie;
    }
    m->videoIndex = av_find_best_stream(m->format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    m->audioIndex = av_find_best_stream(m->format, AVMEDIA_TYPE_AUDIO, -1, m->videoIndex, nullptr, 0);
    if (m->videoIndex >= 0) m->video = OpenDecoder(m->format->streams[m->videoIndex]);
    if (m->video == nullptr) {
        tpw::Log("movie: no decoder for the picture of %s", path.string().c_str());
        Free(m);
        return kNoMovie;
    }
    if (m->audioIndex >= 0) m->audio = OpenDecoder(m->format->streams[m->audioIndex]);
    m->shown.frame = av_frame_alloc();
    m->next.frame = av_frame_alloc();
    m->packet = av_packet_alloc();
    if (m->audio != nullptr && !tpw::config().mute && SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_AudioSpec spec;
        spec.format = SDL_AUDIO_S16;
        spec.channels = m->audio->ch_layout.nb_channels;
        spec.freq = m->audio->sample_rate;
        m->sound = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (m->sound == nullptr) tpw::Log("movie: no sound (%s)", SDL_GetError());
    }
    tpw::Log("movie: %s", path.filename().string().c_str());
    *movie = &m->self;
    return 0;
}

void GetMovieBox(Movie movie, Rect* box) {
    const MovieRecord* m = Of(movie);
    *box = Rect{0, 0, static_cast<int16_t>(m != nullptr ? m->video->height : 0), static_cast<int16_t>(m != nullptr ? m->video->width : 0)};
}

void SetMovieBox(Movie movie, const Rect* box) {
    if (MovieRecord* m = Of(movie)) m->box = *box;
}

void SetMovieGWorld(Movie, GrafPort*, GDevice**) {}

void SetMovieVolume(Movie movie, int16_t volume) {
    MovieRecord* m = Of(movie);
    if (m == nullptr) return;
    m->volume = volume;
    if (m->sound != nullptr) SDL_SetAudioStreamGain(m->sound, volume <= 0 ? 0.0f : static_cast<float>(volume) / 256.0f);
}

// Shows the frame that is due. Before the movie is started that is its first frame. The game
// calls it in a loop while a film plays (src/game/GameSys.cpp, MOVIE_Tick), so when no frame is
// due yet it waits, a few milliseconds at most and never past a millisecond before the next
// frame is due (SDL_DelayPrecise wakes on time), rather than keep a core busy (tpw-port issue
// #77): each frame is shown when it was before.
void MoviesTask(Movie movie, int32_t) {
    MovieRecord* m = Of(movie);
    if (m == nullptr) return;
    const double now = Now(*m);
    bool changed = false;
    for (int guard = 0; guard < 60; ++guard) {
        if (!m->haveNext && !m->ended) {
            m->haveNext = DecodeNext(*m, m->next);
            if (!m->haveNext) m->ended = true;
        }
        if (!m->haveNext || (m->haveShown && m->next.time > now)) break;
        std::swap(m->shown, m->next);
        m->haveShown = true;
        m->haveNext = false;
        changed = true;
        if (!m->playing) break;
    }
    if (changed) {
        Present(*m, m->shown);
    } else if (m->playing && m->haveNext && m->rate > 0) {
        constexpr double kLongestWait = 0.005, kEarly = 0.001;   // seconds
        const double wait = std::min((m->next.time - now) / m->rate - kEarly, kLongestWait);
        if (wait > 0) SDL_DelayPrecise(static_cast<Uint64>(wait * 1e9));
    }
}

TimeValue GetMovieTime(Movie movie, void*) {
    const MovieRecord* m = Of(movie);
    return m != nullptr ? static_cast<TimeValue>(Now(*m) * 600.0) : 0;
}

Fixed GetMoviePreferredRate(Movie) { return 0x10000; }
OSErr PrerollMovie(Movie, TimeValue, Fixed) { return 0; }

void SetMovieRate(Movie movie, Fixed rate) {
    MovieRecord* m = Of(movie);
    if (m == nullptr) return;
    m->position = Now(*m);
    m->since = Clock::now();
    m->rate = static_cast<double>(rate) / 65536.0;
    m->playing = rate != 0;
    if (m->sound != nullptr) {
        if (m->playing) SDL_ResumeAudioStreamDevice(m->sound);
        else SDL_PauseAudioStreamDevice(m->sound);
    }
}

bool IsMovieDone(Movie movie) {
    const MovieRecord* m = Of(movie);
    return m == nullptr || (m->ended && !m->haveNext);
}

void StopMovie(Movie movie) {
    MovieRecord* m = Of(movie);
    if (m == nullptr) return;
    m->position = Now(*m);
    m->playing = false;
    if (m->sound != nullptr) SDL_PauseAudioStreamDevice(m->sound);
}

void DisposeMovie(Movie movie) {
    if (MovieRecord* m = Of(movie)) Free(m);
}

#else   // no FFmpeg: no movie is ever opened, and the rest never sees one

OSErr OpenMovieFile(const FSSpec*, int16_t* refNum, int8_t) {
    if (!gToldAboutMovies) tpw::Log("movies are not played (built without FFmpeg): the game skips them");
    gToldAboutMovies = true;
    if (refNum != nullptr) *refNum = -1;
    return kUnimplemented;
}

OSErr NewMovieFromFile(Movie* movie, int16_t, int16_t*, unsigned char*, int16_t, bool*) {
    if (movie != nullptr) *movie = nullptr;
    return kUnimplemented;
}

void GetMovieBox(Movie, Rect* box) { *box = Rect{0, 0, 0, 0}; }
void SetMovieBox(Movie, const Rect*) {}
void SetMovieGWorld(Movie, GrafPort*, GDevice**) {}
void SetMovieVolume(Movie, int16_t) {}
void MoviesTask(Movie, int32_t) {}
TimeValue GetMovieTime(Movie, void*) { return 0; }
Fixed GetMoviePreferredRate(Movie) { return 0x10000; }
OSErr PrerollMovie(Movie, TimeValue, Fixed) { return 0; }
void SetMovieRate(Movie, Fixed) {}
bool IsMovieDone(Movie) { return true; }
void StopMovie(Movie) {}
void DisposeMovie(Movie) {}

#endif

// ---- the Toolbox calls around a movie
// Everything the game names is a plain file: nothing is an alias.
OSErr ResolveAliasFile(FSSpec*, bool, bool* targetIsFolder, bool* wasAliased) {
    if (targetIsFolder != nullptr) *targetIsFolder = false;
    if (wasAliased != nullptr) *wasAliased = false;
    return 0;
}

// There is one window and it is always the current port; all of it is redrawn every frame.
void SetPort(GrafPort*) {}
void ValidRect(const Rect*) {}
GDevice** GetGWorldDevice(GrafPort*) { return nullptr; }
void ShowWindow(GrafPort* window) { NS_MacDoze::ShowWindow(window, 5); }
