// tgq_tests.cpp: checks that the FFmpeg built from third_party/ffmpeg plays the PC CDs' films (EA .TGQ files: the ea
// demuxer, EA TQI pictures, EA ADPCM sound) through the calls platform/quicktime.cpp makes for any film. The film is a
// tiny one made here, byte by byte from the format's description, never a game file: two 32 x 16 pictures and 28 stereo
// samples. No game, no window. Run by ctest ("tgq") when TPW_MOVIES is on; exits nonzero on the first failure.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

namespace {
int gChecks = 0;

#define CHECK(condition)                                                                \
    do {                                                                                \
        ++gChecks;                                                                      \
        if (!(condition)) {                                                             \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            std::exit(1);                                                               \
        }                                                                               \
    } while (0)

using Bytes = std::vector<uint8_t>;

void Le16(Bytes& out, unsigned v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

void Le32(Bytes& out, uint32_t v) {
    Le16(out, v & 0xffff);
    Le16(out, v >> 16);
}

// A chunk of the EA file: its tag, its size (the 8 bytes of tag and size included, little-endian) and its body.
void Chunk(Bytes& out, const char* tag, const Bytes& body) {
    out.insert(out.end(), tag, tag + 4);
    Le32(out, static_cast<uint32_t>(body.size() + 8));
    out.insert(out.end(), body.begin(), body.end());
}

// MPEG-1 bits, most significant first.
struct Bits {
    Bytes bytes;
    int used = 0;
    void Put(uint32_t value, int count) {
        for (int i = count - 1; i >= 0; --i) {
            if (used % 8 == 0) bytes.push_back(0);
            if ((value >> i) & 1) bytes.back() |= static_cast<uint8_t>(0x80 >> (used % 8));
            ++used;
        }
    }
};

// An intra block that holds only its DC difference (MPEG-1's DC size codes, tables B.12 and B.13, then the difference
// in that many bits, a negative one less one's complement), then the end of block ("10").
void DcOnlyBlock(Bits& bits, bool chroma, int diff) {
    static const uint16_t kLumCode[] = {0x4, 0x0, 0x1, 0x5, 0x6, 0xe, 0x1e, 0x3e, 0x7e};
    static const uint8_t kLumBits[] = {3, 2, 2, 3, 3, 4, 5, 6, 7};
    static const uint16_t kChromaCode[] = {0x0, 0x1, 0x2, 0x6, 0xe, 0x1e, 0x3e, 0x7e, 0xfe};
    static const uint8_t kChromaBits[] = {2, 2, 2, 3, 4, 5, 6, 7, 8};
    const int magnitude = diff < 0 ? -diff : diff;
    int size = 0;
    while ((1 << size) <= magnitude) ++size;
    bits.Put(chroma ? kChromaCode[size] : kLumCode[size], chroma ? kChromaBits[size] : kLumBits[size]);
    if (size > 0) bits.Put(static_cast<uint32_t>(diff > 0 ? diff : diff + (1 << size) - 1), size);
    bits.Put(0x2, 2);
}

// One TQI picture of 16 x 16 macroblocks in a row, each given as its four luma DCs and its Cb and Cr DCs. A DC-only
// block decodes to its DC at every pixel (the matrix's DC step is 16, EA's IDCT adds 4 and takes off 4 bits). The
// packet: width, height, the quantizer, 3 bytes more, then the bits as 32-bit words stored byte-swapped.
Bytes Picture(const std::vector<std::vector<int>>& macroblocks, int quant) {
    Bits bits;
    int last[3] = {0, 0, 0};   // the DC predictors, reset at each picture
    for (const std::vector<int>& mb : macroblocks) {
        for (int n = 0; n < 6; ++n) {
            const int component = n < 4 ? 0 : n - 3;
            DcOnlyBlock(bits, component != 0, mb[n] - last[component]);
            last[component] = mb[n];
        }
    }
    bits.Put(0, 32);   // the reader looks ahead
    while (bits.bytes.size() % 4 != 0) bits.bytes.push_back(0);
    Bytes packet;
    Le16(packet, static_cast<unsigned>(16 * macroblocks.size()));
    Le16(packet, 16);
    packet.push_back(static_cast<uint8_t>(quant));
    packet.insert(packet.end(), {0, 0, 0});
    for (size_t i = 0; i < bits.bytes.size(); i += 4)
        packet.insert(packet.end(), {bits.bytes[i + 3], bits.bytes[i + 2], bits.bytes[i + 1], bits.bytes[i]});
    return packet;
}

// EA ADPCM, stereo: the sample count, each side's two starting samples, then blocks of 28 samples: a byte of
// predictor indexes (left in the high nibble), a byte of shifts (20 less each nibble), 28 bytes of 4-bit codes.
const int kLeftPredictor = 1, kRightPredictor = 0;   // the table's (240, 0) and (0, 0)
const int kShift = 8;                                // 20 - 12
int Code(int i, bool right) { return right ? (i % 16) - 8 : 7 - (i % 15); }

Bytes Sound() {
    Bytes out;
    Le32(out, 28);
    for (int i = 0; i < 4; ++i) Le16(out, 0);
    out.push_back(static_cast<uint8_t>(kLeftPredictor << 4 | kRightPredictor));
    out.push_back(static_cast<uint8_t>((20 - kShift) << 4 | (20 - kShift)));
    for (int i = 0; i < 28; ++i)
        out.push_back(static_cast<uint8_t>((Code(i, false) & 0xf) << 4 | (Code(i, true) & 0xf)));
    return out;
}

// What the format says the samples are: each code shifted up, plus the last two samples times the predictor's pair,
// rounded and taken down 8 bits, held to 16 bits.
std::vector<int16_t> ExpectedSound() {
    static const int kPairs[][2] = {{0, 0}, {240, 0}, {460, -208}, {392, -220}};
    std::vector<int16_t> samples;
    int current[2] = {0, 0}, previous[2] = {0, 0};
    const int predictor[2] = {kLeftPredictor, kRightPredictor};
    for (int i = 0; i < 28; ++i) {
        for (int side = 0; side < 2; ++side) {
            int next = (Code(i, side == 1) * (1 << kShift) + current[side] * kPairs[predictor[side]][0] +
                        previous[side] * kPairs[predictor[side]][1] + 0x80) >> 8;
            next = next < -32768 ? -32768 : next > 32767 ? 32767 : next;
            previous[side] = current[side];
            current[side] = next;
            samples.push_back(static_cast<int16_t>(next));
        }
    }
    return samples;
}

// The film as the PC CDs' are laid out: SCHl (the "PT" header: 30 pictures a second, stereo EA ADPCM at the default
// 22050 Hz), SCCl (the count of the chunks that follow), the pictures (pIQT) and the sound (SCDl), then SCEl.
Bytes Film(const Bytes& first, const Bytes& second) {
    Bytes header = {'P', 'T', 0, 0, 0x1b, 1, 30, 0xfd, 0x82, 1, 2, 0x83, 1, 7, 0x85, 1, 28, 0x8a, 1, 0, 0xff};
    while (header.size() % 4 != 0) header.push_back(0);
    Bytes film;
    Chunk(film, "SCHl", header);
    Bytes count;
    Le32(count, 3);
    Chunk(film, "SCCl", count);
    Chunk(film, "pIQT", first);
    Chunk(film, "SCDl", Sound());
    Chunk(film, "pIQT", second);
    Chunk(film, "SCEl", {});
    return film;
}

AVCodecContext* OpenDecoder(const AVStream* stream) {   // as quicktime.cpp's OpenDecoder()
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) return nullptr;
    AVCodecContext* context = avcodec_alloc_context3(codec);
    if (avcodec_parameters_to_context(context, stream->codecpar) < 0 || avcodec_open2(context, codec, nullptr) < 0)
        avcodec_free_context(&context);
    return context;
}

int Pixel(const AVFrame* frame, int plane, int x, int y) { return frame->data[plane][y * frame->linesize[plane] + x]; }
}  // namespace

int main() {
    av_log_set_level(AV_LOG_ERROR);   // not swscale's note that it has no faster path for so small a picture
    // Picture 1: a grey macroblock, then one with a different DC in each luma block and in each chroma plane (some
    // differences negative). Picture 2: one value everywhere, its predictors starting again from 0.
    const Bytes first = Picture({{100, 100, 100, 100, 128, 128}, {200, 150, 150, 40, 64, 192}}, 10);
    const Bytes second = Picture({{16, 16, 16, 16, 128, 128}, {16, 16, 16, 16, 128, 128}}, 10);
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("tpw_host_tgq_" + std::to_string(std::rand()) + ".tgq");
    {
        const Bytes film = Film(first, second);
        std::FILE* file = std::fopen(path.string().c_str(), "wb");
        CHECK(file != nullptr);
        CHECK(std::fwrite(film.data(), 1, film.size(), file) == film.size());
        std::fclose(file);
    }

    // quicktime.cpp's NewMovieFromFile(): the format is found from the file's first bytes, not its name.
    AVFormatContext* format = nullptr;
    CHECK(avformat_open_input(&format, path.string().c_str(), nullptr, nullptr) == 0);
    CHECK(avformat_find_stream_info(format, nullptr) >= 0);
    CHECK(std::string(format->iformat->name) == "ea");
    const int videoIndex = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int audioIndex = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, videoIndex, nullptr, 0);
    CHECK(videoIndex >= 0 && audioIndex >= 0);
    AVCodecContext* video = OpenDecoder(format->streams[videoIndex]);
    AVCodecContext* audio = OpenDecoder(format->streams[audioIndex]);
    CHECK(video != nullptr && std::string(video->codec->name) == "eatqi");
    CHECK(audio != nullptr && std::string(audio->codec->name) == "adpcm_ea");
    CHECK(audio->sample_rate == 22050 && audio->ch_layout.nb_channels == 2 && audio->sample_fmt == AV_SAMPLE_FMT_S16);
    const AVRational base = format->streams[videoIndex]->time_base;
    CHECK(base.num == 1 && base.den == 30);

    // quicktime.cpp's DecodeNext(), run to the end.
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    std::vector<AVFrame*> pictures;
    std::vector<int16_t> samples;
    bool flushed = false;
    for (;;) {
        const int got = avcodec_receive_frame(video, frame);
        if (got == 0) {
            pictures.push_back(av_frame_clone(frame));
            av_frame_unref(frame);
            continue;
        }
        if (got != AVERROR(EAGAIN)) break;
        if (av_read_frame(format, packet) < 0) {
            if (flushed) break;
            avcodec_send_packet(video, nullptr);
            flushed = true;
            continue;
        }
        if (packet->stream_index == videoIndex) {
            CHECK(avcodec_send_packet(video, packet) == 0);
        } else if (packet->stream_index == audioIndex) {
            CHECK(avcodec_send_packet(audio, packet) == 0);
            while (avcodec_receive_frame(audio, frame) == 0) {
                const auto* data = reinterpret_cast<const int16_t*>(frame->data[0]);
                samples.insert(samples.end(), data, data + frame->nb_samples * 2);
                av_frame_unref(frame);
            }
        }
        av_packet_unref(packet);
    }

    CHECK(pictures.size() == 2);
    const AVFrame* one = pictures[0];
    CHECK(one->width == 32 && one->height == 16 && one->format == AV_PIX_FMT_YUV420P);
    CHECK(one->best_effort_timestamp == 0 && pictures[1]->best_effort_timestamp == 1);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 32; ++x) {
            const int luma = x < 16 ? 100 : y < 8 ? (x < 24 ? 200 : 150) : (x < 24 ? 150 : 40);
            CHECK(Pixel(one, 0, x, y) == luma);
            CHECK(Pixel(pictures[1], 0, x, y) == 16);
        }
    }
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 16; ++x) {
            CHECK(Pixel(one, 1, x, y) == (x < 8 ? 128 : 64));
            CHECK(Pixel(one, 2, x, y) == (x < 8 ? 128 : 192));
            CHECK(Pixel(pictures[1], 1, x, y) == 128 && Pixel(pictures[1], 2, x, y) == 128);
        }
    }
    CHECK(samples == ExpectedSound());

    // The RGB24 picture quicktime.cpp's Convert() draws: grey stays grey.
    SwsContext* scale = sws_getContext(32, 16, AV_PIX_FMT_YUV420P, 32, 16, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr,
                                       nullptr);
    CHECK(scale != nullptr);
    std::vector<uint8_t> rgb(32 * 16 * 3);
    uint8_t* out[4] = {rgb.data(), nullptr, nullptr, nullptr};
    int strides[4] = {32 * 3, 0, 0, 0};
    CHECK(sws_scale(scale, pictures[1]->data, pictures[1]->linesize, 0, 16, out, strides) == 16);
    CHECK(rgb[0] == rgb[1] && rgb[1] == rgb[2] && rgb[0] <= 2);   // Y 16, no colour: black
    sws_freeContext(scale);

    for (AVFrame*& picture : pictures) av_frame_free(&picture);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&video);
    avcodec_free_context(&audio);
    avformat_close_input(&format);
    std::filesystem::remove(path);
    std::printf("tgq: %d checks passed\n", gChecks);
    return 0;
}
