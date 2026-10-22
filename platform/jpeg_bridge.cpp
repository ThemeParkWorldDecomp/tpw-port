// jpeg_bridge.cpp: the game's libjpeg calls (include/lobby.h, include/Postcard.h) on the port's libjpeg 6b (third_party/libjpeg-6b,
// reached through platform/jpeg_native.h).
//
// The Mac game calls the IJG library (jpeg_* glue 0x1006bae4 to 0x1006bb8c and 0x1023a090 to 0x1023a150) with its own 32-bit
// structures: a jpeg_decompress_struct of 0x1d0 bytes, a jpeg_compress_struct of 0x174 and an error manager of 0x84, which the
// game keeps on its stack and reads and writes by field. The host's library structures hold 64-bit pointers, so each Mac
// structure gets a Bridge: the library's own object (a jpeg::Context) and the copies of the fields the game reads, written back
// into the Mac structure after every call (offsets below, checked against the library's header compiled for 32-bit pointers).
//
// The game's pointers in these calls are host pointers cut to 32 bits (cinfo.err is the address jpeg_std_error returned), so
// jpeg_std_error keeps the manager it was given under that cut address and jpeg_Create* finds it again from cinfo.err.
//
// Errors. The Mac libjpeg calls the manager's error_exit (word 0). The stock one prints through output_message and exits the
// program; the outbox's loader (read_JPEG_fileChecked, 0x101e538c) installs my_error_exit (0x101e5310), which prints through the
// manager's word at +8 and longjmps to the jump buffer it keeps at manager + 0x84 (JpegSetJmp, the Mac's setjmp, is a stand-in
// here that never returns twice). When the game installed my_error_exit the bridge builds the manager that function expects
// (in the arena, because it reads it through a Ptr32): ErrorOutput prints the library's message and JpegLongJmp leaves the library
// call that failed. The call returns, the Mac structure shows no output scanlines, and later calls do nothing, so the loader's
// scanline loops end and its cleanup runs. Without a handler of the game's the library's message is printed and the call is left
// the same way (the Mac would have ended the program).
//
// The source of jpeg_stdio_src is a FILE* for the outbox, but the Bullfrog loaders (TbJPEGSaver.cpp: ReadHeaderInfo, ReadData,
// ValidImage) pass their CFile where the Mac passes the stream; the bridge tells the two apart (a CFile starts with its vtable
// pointer, inside a loaded image) and reads a CFile through its Read.
#include <dlfcn.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "LbSCBack.h"
#include "Postcard.h"
#include "bfl_FileSystem.h"
#include "lobby.h"
#include "platform/arena.h"
#include "jpeg_native.h"

namespace {

// The Mac layouts the bridge reads and writes (jpeg_decompress_struct in include/lobby.h, jpeg_compress_struct in
// include/Postcard.h). The named fields are the ones the game uses; output_components lies in the unnamed bytes at +0x7c.
static_assert(sizeof(jpeg_error_mgr) == 0x84, "jpeg_error_mgr layout");
static_assert(sizeof(jpeg_decompress_struct) == 0x1d0, "jpeg_decompress_struct layout");
static_assert(offsetof(jpeg_decompress_struct, image_width) == 0x1c, "image_width");
static_assert(offsetof(jpeg_decompress_struct, image_height) == 0x20, "image_height");
static_assert(offsetof(jpeg_decompress_struct, quantize_colors) == 0x54, "quantize_colors");
static_assert(offsetof(jpeg_decompress_struct, output_width) == 0x70, "output_width");
static_assert(offsetof(jpeg_decompress_struct, output_height) == 0x74, "output_height");
static_assert(offsetof(jpeg_decompress_struct, f78) == 0x78 && sizeof(jpeg_decompress_struct::f78) == 0x0c, "output_components lies at +0x7c");
static_assert(offsetof(jpeg_decompress_struct, actual_number_of_colors) == 0x84, "actual_number_of_colors");
static_assert(offsetof(jpeg_decompress_struct, colormap) == 0x88, "colormap");
static_assert(offsetof(jpeg_decompress_struct, output_scanline) == 0x8c, "output_scanline");
static_assert(sizeof(jpeg_compress_struct) == 0x174, "jpeg_compress_struct layout");
static_assert(offsetof(jpeg_compress_struct, mImageWidth) == 0x1c, "image_width");
static_assert(offsetof(jpeg_compress_struct, mImageHeight) == 0x20, "image_height");
static_assert(offsetof(jpeg_compress_struct, mInputComponents) == 0x24, "input_components");
static_assert(offsetof(jpeg_compress_struct, mInColorSpace) == 0x28, "in_color_space");
static_assert(offsetof(jpeg_compress_struct, mNextScanline) == 0xdc, "next_scanline");

const size_t kOutputComponentsOffset = 0x7c;
const uint32_t kLibraryVersion = 0x3e;           // JPEG_LIB_VERSION 62, what the game passes to jpeg_Create*
const size_t kManagerSize = 0x84;
const size_t kManagerJumpBufferOffset = 0x84;    // my_error_exit's jump buffer, behind the manager (0x11c bytes on the Mac)
const size_t kManagerJumpBufferSize = 0x11c;
const size_t kManagerOutputWordOffset = 8;       // the manager's output_message word, which my_error_exit hands to ErrorOutput
const uint32_t kOutputMessageToken = 0x4a504547; // stands for the library's own output_message in that word

struct Bridge {
    tpw::jpeg::Context* native = nullptr;
    uint8_t* mac = nullptr;                // the game's structure
    bool compress = false;
    uint8_t* manager = nullptr;            // the game's error manager, found from the structure's err word
    uint8_t* shadow = nullptr;             // the manager my_error_exit reads, in the arena (made at the first error)
    uint8_t* colormap = nullptr;           // the arena copy of the library's colormap
    const uint8_t* colormapSource = nullptr;
    int32_t colormapCount = 0;
};

std::mutex gLock;
std::unordered_map<uint32_t, uint8_t*> gManagers;     // jpeg_std_error: the 32-bit cut of the address, and the address
std::unordered_map<const void*, Bridge*> gBridges;    // the game's structure, and its bridge
Bridge* gErrorBridge = nullptr;                       // the bridge whose error my_error_exit is handling

uint32_t Cut(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }

uint32_t Get32(const uint8_t* base, size_t offset) {
    uint32_t value;
    std::memcpy(&value, base + offset, 4);
    return value;
}
void Put32(uint8_t* base, size_t offset, uint32_t value) { std::memcpy(base + offset, &value, 4); }

Bridge* Find(const void* mac) {
    std::lock_guard<std::mutex> hold(gLock);
    auto it = gBridges.find(mac);
    return it == gBridges.end() ? nullptr : it->second;
}

void FreeBlocks(Bridge* b) {
    tpw::ArenaFree(b->shadow);
    tpw::ArenaFree(b->colormap);
    b->shadow = nullptr;
    b->colormap = nullptr;
}

void Forget(Bridge* b) {
    if (gErrorBridge == b) gErrorBridge = nullptr;
    tpw::jpeg::DeleteContext(b->native);
    FreeBlocks(b);
    delete b;
}

// The library's error_exit, before it prints and leaves: runs the game's handler when it installed my_error_exit in word 0 of its
// manager (read now, the game writes it after jpeg_std_error).
void ErrorHook(tpw::jpeg::Context*, void* user) {
    Bridge* b = static_cast<Bridge*>(user);
    if (b->manager == nullptr || Get32(b->manager, 0) != Cut(reinterpret_cast<const void*>(&my_error_exit))) return;
    if (b->shadow == nullptr) {
        b->shadow = static_cast<uint8_t*>(tpw::ArenaAllocZeroed(kManagerJumpBufferOffset + kManagerJumpBufferSize));
        if (b->shadow == nullptr) return;
        Put32(b->shadow, kManagerOutputWordOffset, kOutputMessageToken);
    }
    jpeg_common_struct common;
    common.err = b->shadow;
    gErrorBridge = b;
    my_error_exit(&common);   // leaves through JpegLongJmp
}

uint8_t* FindManager(uint32_t err) {
    std::lock_guard<std::mutex> hold(gLock);
    auto it = gManagers.find(err);
    return it == gManagers.end() ? nullptr : it->second;
}

Bridge* Make(void* mac, bool compress, uint32_t expectedSize, uint32_t version, uint32_t size) {
    if (version != kLibraryVersion || size != expectedSize) {
        std::fprintf(stderr, "libjpeg: the game made a structure of version %u, size 0x%x (expected %u, 0x%x)\n", version, size,
                     kLibraryVersion, expectedSize);
    }
    if (Bridge* old = Find(mac)) {
        std::lock_guard<std::mutex> hold(gLock);
        gBridges.erase(mac);
        Forget(old);
    }
    Bridge* b = new Bridge();
    b->mac = static_cast<uint8_t*>(mac);
    b->compress = compress;
    b->manager = FindManager(Get32(b->mac, 0));
    // The library clears the structure but for the error manager word (jpeg_CreateDecompress, jpeg_CreateCompress).
    uint32_t err = Get32(b->mac, 0);
    std::memset(b->mac, 0, expectedSize);
    Put32(b->mac, 0, err);
    b->native = compress ? tpw::jpeg::NewCompressor(ErrorHook, b) : tpw::jpeg::NewDecompressor(ErrorHook, b);
    std::lock_guard<std::mutex> hold(gLock);
    gBridges[mac] = b;
    return b;
}

void Destroy(void* mac) {
    Bridge* b = Find(mac);
    if (b == nullptr) return;
    {
        std::lock_guard<std::mutex> hold(gLock);
        gBridges.erase(mac);
    }
    Forget(b);
}

// What the game reads of the decompressor after a call. A failed call shows no output at all, so the game's loops end.
void SyncDecoder(Bridge* b) {
    if (gErrorBridge == b) gErrorBridge = nullptr;
    tpw::jpeg::DecodeInfo i = tpw::jpeg::GetDecodeInfo(b->native);
    uint8_t* mac = b->mac;
    Put32(mac, offsetof(jpeg_decompress_struct, image_width), i.imageWidth);
    Put32(mac, offsetof(jpeg_decompress_struct, image_height), i.imageHeight);
    Put32(mac, offsetof(jpeg_decompress_struct, quantize_colors), static_cast<uint32_t>(i.quantizeColors));
    Put32(mac, offsetof(jpeg_decompress_struct, output_width), i.outputWidth);
    Put32(mac, offsetof(jpeg_decompress_struct, output_height), i.outputHeight);
    Put32(mac, kOutputComponentsOffset, static_cast<uint32_t>(i.outputComponents));
    Put32(mac, offsetof(jpeg_decompress_struct, actual_number_of_colors), static_cast<uint32_t>(i.colorCount));
    Put32(mac, offsetof(jpeg_decompress_struct, output_scanline), i.outputScanline);
    if (i.colorTable[0] == nullptr || i.colorCount <= 0) {
        Put32(mac, offsetof(jpeg_decompress_struct, colormap), 0);
        return;
    }
    if (b->colormap == nullptr || b->colormapSource != i.colorTable[0] || b->colormapCount != i.colorCount) {
        // The game indexes colormap[channel][entry]: three 32-bit offsets, then the three channel arrays, in the arena.
        tpw::ArenaFree(b->colormap);
        size_t count = static_cast<size_t>(i.colorCount);
        b->colormap = static_cast<uint8_t*>(tpw::ArenaAllocZeroed(12 + 3 * count));
        if (b->colormap == nullptr) {
            Put32(mac, offsetof(jpeg_decompress_struct, colormap), 0);
            return;
        }
        for (int channel = 0; channel < 3; channel++) {
            uint8_t* row = b->colormap + 12 + channel * count;
            if (i.colorTable[channel] != nullptr) std::memcpy(row, i.colorTable[channel], count);
            Put32(b->colormap, static_cast<size_t>(channel) * 4, tpw::ArenaOffset(row));
        }
        b->colormapSource = i.colorTable[0];
        b->colormapCount = i.colorCount;
    }
    Put32(mac, offsetof(jpeg_decompress_struct, colormap), tpw::ArenaOffset(b->colormap));
}

// What the game reads of the compressor: next_scanline. A failed call ends the game's loop by reporting the image written.
void SyncEncoder(Bridge* b) {
    if (gErrorBridge == b) gErrorBridge = nullptr;
    jpeg_compress_struct* mac = reinterpret_cast<jpeg_compress_struct*>(b->mac);
    mac->mNextScanline = tpw::jpeg::Failed(b->native) ? mac->mImageHeight : tpw::jpeg::NextScanline(b->native);
}

void PushImage(Bridge* b) {
    const jpeg_compress_struct* mac = reinterpret_cast<const jpeg_compress_struct*>(b->mac);
    tpw::jpeg::SetImage(b->native, mac->mImageWidth, mac->mImageHeight, mac->mInputComponents, mac->mInColorSpace);
}

// A CFile starts with its vtable pointer, which lies inside a loaded image; a C stream starts with a pointer into the heap (or null).
bool IsCFile(const void* file) {
    const void* first;
    std::memcpy(&first, file, sizeof first);
    Dl_info info;
    return first != nullptr && dladdr(first, &info) != 0;
}

long ReadCFile(void* user, uint8_t* dest, size_t size) {
    return static_cast<bfl::FileSystem::CFile*>(user)->Read(dest, static_cast<uint32_t>(size));
}

}  // namespace

// ---- the decompressor (lobby.h)

// 0x1006bae4: fills the manager with the library's standard handlers. The game then writes its own error_exit into word 0.
jpeg_error_mgr* jpeg_std_error(jpeg_error_mgr* err) {
    std::memset(err, 0, kManagerSize);
    std::lock_guard<std::mutex> hold(gLock);
    gManagers[Cut(err)] = reinterpret_cast<uint8_t*>(err);
    return err;
}

void jpeg_CreateDecompress(jpeg_decompress_struct* cinfo, int32_t version, uint32_t size) {   // 0x1006bafc
    SyncDecoder(Make(cinfo, false, sizeof(jpeg_decompress_struct), static_cast<uint32_t>(version), size));
}

void jpeg_stdio_src(jpeg_decompress_struct* cinfo, void* file) {   // 0x1006bb14
    Bridge* b = Find(cinfo);
    if (b == nullptr || file == nullptr) return;
    if (IsCFile(file)) {
        tpw::jpeg::SetReadSource(b->native, ReadCFile, file);
    } else {
        tpw::jpeg::SetFileSource(b->native, static_cast<std::FILE*>(file));
    }
}

int32_t jpeg_read_header(jpeg_decompress_struct* cinfo, int32_t requireImage) {   // 0x1006bb2c
    Bridge* b = Find(cinfo);
    if (b == nullptr) return 0;
    int32_t answer = tpw::jpeg::ReadHeader(b->native, requireImage);
    SyncDecoder(b);
    return answer;
}

void jpeg_destroy_decompress(jpeg_decompress_struct* cinfo) {   // 0x1006bb44
    Destroy(cinfo);
}

int32_t jpeg_start_decompress(jpeg_decompress_struct* cinfo) {   // 0x1006bb5c
    Bridge* b = Find(cinfo);
    if (b == nullptr) return 0;
    int32_t answer = tpw::jpeg::StartDecompress(b->native, static_cast<int32_t>(cinfo->quantize_colors));
    SyncDecoder(b);
    return answer;
}

uint32_t jpeg_read_scanlines(jpeg_decompress_struct* cinfo, uint8_t** rows, uint32_t max) {   // 0x1006bb74
    Bridge* b = Find(cinfo);
    if (b == nullptr) return 0;
    uint32_t answer = tpw::jpeg::ReadScanlines(b->native, rows, max);
    SyncDecoder(b);
    return answer;
}

int32_t jpeg_finish_decompress(jpeg_decompress_struct* cinfo) {   // 0x1006bb8c
    Bridge* b = Find(cinfo);
    if (b == nullptr) return 0;
    int32_t answer = tpw::jpeg::FinishDecompress(b->native);
    SyncDecoder(b);
    return answer;
}

// ---- the compressor (Postcard.h)

void jpeg_CreateCompress(jpeg_compress_struct* cinfo, int32_t version, uint32_t size) {   // 0x1023a090
    SyncEncoder(Make(cinfo, true, sizeof(jpeg_compress_struct), static_cast<uint32_t>(version), size));
}

void jpeg_stdio_dest(jpeg_compress_struct* cinfo, void* file) {   // 0x1023a0c0
    Bridge* b = Find(cinfo);
    if (b == nullptr || file == nullptr) return;
    tpw::jpeg::SetFileDestination(b->native, static_cast<std::FILE*>(file));
}

void jpeg_set_defaults(jpeg_compress_struct* cinfo) {   // 0x1023a0d8: reads image_width, image_height, input_components, in_color_space
    Bridge* b = Find(cinfo);
    if (b == nullptr) return;
    PushImage(b);
    tpw::jpeg::SetDefaults(b->native);
    SyncEncoder(b);
}

void jpeg_set_quality(jpeg_compress_struct* cinfo, int32_t quality, int32_t forceBaseline) {   // 0x1023a0f0
    Bridge* b = Find(cinfo);
    if (b == nullptr) return;
    tpw::jpeg::SetQuality(b->native, quality, forceBaseline);
    SyncEncoder(b);
}

void jpeg_start_compress(jpeg_compress_struct* cinfo, int32_t writeAllTables) {   // 0x1023a108
    Bridge* b = Find(cinfo);
    if (b == nullptr) return;
    PushImage(b);
    tpw::jpeg::StartCompress(b->native, writeAllTables);
    SyncEncoder(b);
}

uint32_t jpeg_write_scanlines(jpeg_compress_struct* cinfo, uint8_t** rows, uint32_t count) {   // 0x1023a120
    Bridge* b = Find(cinfo);
    if (b == nullptr) return 0;
    uint32_t answer = tpw::jpeg::WriteScanlines(b->native, rows, count);
    SyncEncoder(b);
    return answer;
}

void jpeg_finish_compress(jpeg_compress_struct* cinfo) {   // 0x1023a138
    Bridge* b = Find(cinfo);
    if (b == nullptr) return;
    tpw::jpeg::FinishCompress(b->native);
    SyncEncoder(b);
}

void jpeg_destroy_compress(jpeg_compress_struct* cinfo) {   // 0x1023a150
    Destroy(cinfo);
}

// ---- the error handler's callees (lobby.h, namespace outbox_callee)

namespace outbox_callee {

// 0x1023baa8, a call through the manager's word at +8 (output_message) with r3 = cinfo. The bridge's manager holds a token for
// the library's own output_message; any other word is a function pointer of the game's that the port cannot reach.
void ErrorOutput(uint32_t tvector, jpeg_common_struct*) {
    if (tvector == kOutputMessageToken && gErrorBridge != nullptr) {
        tpw::jpeg::PrintMessage(gErrorBridge->native);
        return;
    }
    std::fprintf(stderr, "libjpeg: the output_message word 0x%x is not one the port made\n", tvector);
}

// 0x10238018 (the C library's longjmp): my_error_exit leaves through the jump buffer behind the manager. The port leaves the
// library call in progress instead (its caller's setjmp never returns twice here, see JpegSetJmp).
void JpegLongJmp(void* buffer, int32_t) {
    Bridge* b = gErrorBridge;
    if (b != nullptr && b->shadow != nullptr && buffer == b->shadow + kManagerJumpBufferOffset && tpw::jpeg::Jump(b->native)) return;
    std::fprintf(stderr, "libjpeg: a jump to a buffer the port did not make (%p), stopping\n", buffer);
    std::abort();
}

}  // namespace outbox_callee

// ---- TbJPEGLoader (lobby.h): the constructor 0x10024228 and destructor 0x10024290 are the translations in TbJPEGSaver.cpp; the
// class declares them (the Mac's callers make the loader on the stack) and the test libraries fake them.

TbJPEGLoader::TbJPEGLoader() { TbJPEGLoader_CtorBody(this); }

TbJPEGLoader::~TbJPEGLoader() { TbJPEGLoader_DtorBody(this, 0); }
