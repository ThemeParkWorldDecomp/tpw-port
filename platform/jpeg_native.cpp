// jpeg_native.cpp: the vendored libjpeg 6b (third_party/libjpeg-6b) behind plain host types (platform/jpeg_native.h).
//
// The Mac game linked the IJG library with 32-bit structures; the host's are wider, so every decompressor or compressor the game
// makes gets a Context here: the library's own object, its error manager (the library's standard one, with an error_exit that
// leaves the call in progress by longjmp), the jump buffer, and a read manager for a source that is not a C stream (the Bullfrog
// loaders pass a CFile where the Mac passes the FILE* the library expects).
//
// An error leaves the call that raised it: the library's error_exit runs the hook (the game's own handler, when it installed
// one), prints the message through the manager (as the stock error_exit does) and jumps to the entry of the call, which answers
// 0. The stock error_exit would end the program (exit); a bad image file should not end the player's game, so the port returns.
// The Context is then Failed: every call but DeleteContext does nothing.
#include "jpeg_native.h"

#include <csetjmp>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "jpeglib.h"
#include "jerror.h"
}

#include "log_file.h"   // the failure reports go to the log file too

namespace tpw {
namespace jpeg {

struct Context {
    bool compress = false;
    bool failed = false;
    bool active = false;                  // a call is in progress, so `jump` holds a place to leave to
    bool created = false;
    std::jmp_buf jump;
    ErrorHook hook = nullptr;
    void* hookUser = nullptr;
    jpeg_error_mgr error;
    jpeg_decompress_struct decompressor;
    jpeg_compress_struct compressor;
    // The read manager for a source that is not a C stream (SetReadSource).
    jpeg_source_mgr source;
    ReadSource read = nullptr;
    void* readUser = nullptr;
    JOCTET buffer[4096];
};

namespace {

j_common_ptr Common(Context* c) {
    return c->compress ? reinterpret_cast<j_common_ptr>(&c->compressor) : reinterpret_cast<j_common_ptr>(&c->decompressor);
}

// The stock output_message's text (format_message, then a newline on stderr), through the log file too (log_file.h).
void OutputMessage(j_common_ptr common) {
    char text[JMSG_LENGTH_MAX];
    (*common->err->format_message)(common, text);
    ::tpw::LogRaw(text);
}

void ErrorExit(j_common_ptr common) {
    Context* c = static_cast<Context*>(common->client_data);
    if (c->hook != nullptr) c->hook(c, c->hookUser);
    (*common->err->output_message)(common);
    if (!Jump(c)) {
        ::tpw::LogRawF("libjpeg: error outside of a call, stopping\n");
        std::abort();
    }
}

// The entry of a call: nothing is done once the context failed; an error inside lands here a second time with a non-zero answer.
#define TPW_JPEG_ENTER(c, failAnswer)          \
    do {                                       \
        if ((c)->failed) return failAnswer;    \
        (c)->active = true;                    \
        if (setjmp((c)->jump) != 0) {          \
            (c)->active = false;               \
            (c)->failed = true;                \
            return failAnswer;                 \
        }                                      \
    } while (0)
#define TPW_JPEG_LEAVE(c) ((c)->active = false)

Context* NewContext(bool compress, ErrorHook hook, void* user) {
    Context* c = new Context();
    c->compress = compress;
    c->hook = hook;
    c->hookUser = user;
    std::memset(&c->decompressor, 0, sizeof c->decompressor);
    std::memset(&c->compressor, 0, sizeof c->compressor);
    std::memset(&c->source, 0, sizeof c->source);
    jpeg_std_error(&c->error);
    c->error.error_exit = ErrorExit;
    c->error.output_message = OutputMessage;
    j_common_ptr common = Common(c);
    common->err = &c->error;
    common->client_data = c;
    c->active = true;
    if (setjmp(c->jump) != 0) {
        c->active = false;
        c->failed = true;
        return c;
    }
    if (compress) {
        jpeg_create_compress(&c->compressor);
    } else {
        jpeg_create_decompress(&c->decompressor);
    }
    c->created = true;
    c->active = false;
    return c;
}

void NoSourceAction(j_decompress_ptr) {}

boolean FillBuffer(j_decompress_ptr cinfo) {
    Context* c = static_cast<Context*>(cinfo->client_data);
    long count = c->read != nullptr ? c->read(c->readUser, c->buffer, sizeof c->buffer) : 0;
    if (count <= 0) {
        // The file ended early: the library gets an end of image marker, with its warning (the stock stdio source does the same).
        WARNMS(cinfo, JWRN_JPEG_EOF);
        c->buffer[0] = static_cast<JOCTET>(0xFF);
        c->buffer[1] = static_cast<JOCTET>(JPEG_EOI);
        count = 2;
    }
    c->source.next_input_byte = c->buffer;
    c->source.bytes_in_buffer = static_cast<size_t>(count);
    return TRUE;
}

void SkipData(j_decompress_ptr cinfo, long count) {
    Context* c = static_cast<Context*>(cinfo->client_data);
    if (count <= 0) return;
    while (count > static_cast<long>(c->source.bytes_in_buffer)) {
        count -= static_cast<long>(c->source.bytes_in_buffer);
        FillBuffer(cinfo);
    }
    c->source.next_input_byte += count;
    c->source.bytes_in_buffer -= static_cast<size_t>(count);
}

}  // namespace

Context* NewDecompressor(ErrorHook hook, void* user) { return NewContext(false, hook, user); }
Context* NewCompressor(ErrorHook hook, void* user) { return NewContext(true, hook, user); }

void DeleteContext(Context* c) {
    if (c == nullptr) return;
    if (c->created) {
        c->active = true;
        if (setjmp(c->jump) == 0) {
            if (c->compress) {
                jpeg_destroy_compress(&c->compressor);
            } else {
                jpeg_destroy_decompress(&c->decompressor);
            }
        }
        c->active = false;
    }
    delete c;
}

bool Failed(Context* c) { return c->failed; }

void PrintMessage(Context* c) {
    j_common_ptr common = Common(c);
    (*common->err->output_message)(common);
}

bool Jump(Context* c) {
    if (!c->active) return false;
    std::longjmp(c->jump, 1);
}

void SetFileSource(Context* c, std::FILE* file) {
    TPW_JPEG_ENTER(c, );
    jpeg_stdio_src(&c->decompressor, file);
    TPW_JPEG_LEAVE(c);
}

void SetReadSource(Context* c, ReadSource read, void* user) {
    TPW_JPEG_ENTER(c, );
    c->read = read;
    c->readUser = user;
    c->source.init_source = NoSourceAction;
    c->source.fill_input_buffer = FillBuffer;
    c->source.skip_input_data = SkipData;
    c->source.resync_to_restart = jpeg_resync_to_restart;
    c->source.term_source = NoSourceAction;
    c->source.bytes_in_buffer = 0;
    c->source.next_input_byte = nullptr;
    c->decompressor.src = &c->source;
    TPW_JPEG_LEAVE(c);
}

int ReadHeader(Context* c, int requireImage) {
    TPW_JPEG_ENTER(c, 0);
    int answer = jpeg_read_header(&c->decompressor, requireImage ? TRUE : FALSE);
    TPW_JPEG_LEAVE(c);
    return answer;
}

int StartDecompress(Context* c, int quantizeColors) {
    TPW_JPEG_ENTER(c, 0);
    c->decompressor.quantize_colors = quantizeColors ? TRUE : FALSE;
    int answer = jpeg_start_decompress(&c->decompressor) ? 1 : 0;
    TPW_JPEG_LEAVE(c);
    return answer;
}

unsigned ReadScanlines(Context* c, uint8_t** rows, unsigned maxLines) {
    TPW_JPEG_ENTER(c, 0);
    unsigned answer = jpeg_read_scanlines(&c->decompressor, rows, maxLines);
    TPW_JPEG_LEAVE(c);
    return answer;
}

int FinishDecompress(Context* c) {
    TPW_JPEG_ENTER(c, 0);
    int answer = jpeg_finish_decompress(&c->decompressor) ? 1 : 0;
    TPW_JPEG_LEAVE(c);
    return answer;
}

DecodeInfo GetDecodeInfo(Context* c) {
    DecodeInfo info;
    std::memset(&info, 0, sizeof info);
    if (c->failed || !c->created) return info;
    const jpeg_decompress_struct& d = c->decompressor;
    info.imageWidth = d.image_width;
    info.imageHeight = d.image_height;
    info.quantizeColors = d.quantize_colors;
    info.outputWidth = d.output_width;
    info.outputHeight = d.output_height;
    info.outputComponents = d.output_components;
    info.colorCount = d.actual_number_of_colors;
    info.outputScanline = d.output_scanline;
    if (d.colormap != nullptr && d.out_color_components <= 3) {
        for (int i = 0; i < d.out_color_components; i++) info.colorTable[i] = d.colormap[i];
    }
    return info;
}

void SetFileDestination(Context* c, std::FILE* file) {
    TPW_JPEG_ENTER(c, );
    jpeg_stdio_dest(&c->compressor, file);
    TPW_JPEG_LEAVE(c);
}

void SetImage(Context* c, uint32_t width, uint32_t height, int components, int colorSpace) {
    c->compressor.image_width = width;
    c->compressor.image_height = height;
    c->compressor.input_components = components;
    c->compressor.in_color_space = static_cast<J_COLOR_SPACE>(colorSpace);
}

void SetDefaults(Context* c) {
    TPW_JPEG_ENTER(c, );
    jpeg_set_defaults(&c->compressor);
    TPW_JPEG_LEAVE(c);
}

void SetQuality(Context* c, int quality, int forceBaseline) {
    TPW_JPEG_ENTER(c, );
    jpeg_set_quality(&c->compressor, quality, forceBaseline ? TRUE : FALSE);
    TPW_JPEG_LEAVE(c);
}

void StartCompress(Context* c, int writeAllTables) {
    TPW_JPEG_ENTER(c, );
    jpeg_start_compress(&c->compressor, writeAllTables ? TRUE : FALSE);
    TPW_JPEG_LEAVE(c);
}

unsigned WriteScanlines(Context* c, uint8_t** rows, unsigned lines) {
    TPW_JPEG_ENTER(c, 0);
    unsigned answer = jpeg_write_scanlines(&c->compressor, rows, lines);
    TPW_JPEG_LEAVE(c);
    return answer;
}

void FinishCompress(Context* c) {
    TPW_JPEG_ENTER(c, );
    jpeg_finish_compress(&c->compressor);
    TPW_JPEG_LEAVE(c);
}

uint32_t NextScanline(Context* c) { return c->compressor.next_scanline; }

}  // namespace jpeg
}  // namespace tpw
