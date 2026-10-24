// ddraw.cpp: the DirectDraw objects the engine's display start-up (tb_ddraw.cpp) creates.
// On the Mac these came from indirectx; under its OpenGL implementation the picture is
// drawn with OpenGL and DirectDraw only frames it: a display mode, a primary surface with
// a back buffer, a clipper. So the surfaces here are plain memory the engine can lock (it
// does for screenshots), and nothing is presented from them: the frame reaches the screen
// through UOpenGL::Swap.
//
// What is reported to the engine (pixel formats, capability words, video memory) is this
// layer's choice, not a reproduction of indirectx: see the notes on each call.
#include <SDL3/SDL.h>

#include <cstring>
#include <unordered_map>
#include <vector>

#include "platform.h"
#include "platform/indirectx.h"
#include "platform/interfacelib.h"
#include "platform/macdoze.h"
#include "WinScrn.h"

namespace {
const int32_t DD_OK = 0;
const int32_t E_NOINTERFACE = static_cast<int32_t>(0x80004002);
const int32_t DDERR_INVALIDPARAMS = static_cast<int32_t>(0x80070057);
const int32_t DDERR_NOTFOUND = static_cast<int32_t>(0x887600ff);
const int32_t E_FAIL = static_cast<int32_t>(0x80004005);   // what indirectx answers when it has no surface to act on

// Reference counts of the COM-style objects (IUnknown has no room for one).
std::unordered_map<NS_MacDoze::IUnknown*, uint32_t>& RefCounts() {
    static auto& counts = *new std::unordered_map<NS_MacDoze::IUnknown*, uint32_t>;   // (never destroyed: see file_handles.cpp)
    return counts;
}
void RetainObject(NS_MacDoze::IUnknown* object) { ++RefCounts()[object]; }

// The display mode the game set, or the window's size before it sets one.
struct DisplayMode {
    uint32_t width = 640;
    uint32_t height = 480;
    uint32_t depth = 32;
};
DisplayMode gMode;

void FillPixelFormat(DDPIXELFORMAT& format, uint32_t depth) {
    std::memset(&format, 0, sizeof format);
    format.dwSize = sizeof format;
    format.dwFlags = DDPF_RGB;
    format.dwRGBBitCount = depth;
    if (depth == 16) {   // 5-6-5
        format.dwRBitMask = 0xf800;
        format.dwGBitMask = 0x07e0;
        format.dwBBitMask = 0x001f;
    } else {             // 8-8-8-8
        format.dwRBitMask = 0x00ff0000;
        format.dwGBitMask = 0x0000ff00;
        format.dwBBitMask = 0x000000ff;
        format.dwRGBAlphaBitMask = 0xff000000;
    }
}

class Surface : public IDirectDrawSurface4 {
public:
    Surface(uint32_t width, uint32_t height, uint32_t depth, uint32_t caps)
        : mWidth(width), mHeight(height), mDepth(depth), mCaps(caps),
          mPixels(static_cast<size_t>(width) * height * (depth / 8)) {}
    ~Surface() override {
        if (mBack != nullptr) mBack->Release();
    }
    void Describe(DDSURFACEDESC2* desc) const {
        std::memset(desc, 0, sizeof *desc);
        desc->dwSize = sizeof *desc;
        desc->dwFlags = 0x100f;   // caps, height, width, pitch, pixel format
        desc->dwHeight = mHeight;
        desc->dwWidth = mWidth;
        desc->lPitch = static_cast<int32_t>(mWidth * (mDepth / 8));
        FillPixelFormat(desc->ddpfPixelFormat, mDepth);
        desc->ddsCaps.dwCaps = mCaps;
    }
    uint32_t mWidth, mHeight, mDepth, mCaps;
    std::vector<uint8_t> mPixels;
    Surface* mBack = nullptr;          // a flipping primary's back buffer
    IDirectDrawClipper* mClipper = nullptr;
};

class Clipper : public IDirectDrawClipper {
public:
    GrafPort* mWindow = nullptr;
};

class DirectDraw : public IDirectDraw4 {
public:
    int32_t QueryInterface(const NS_MacDoze::_GUID&, void** out) override {
        // IDirectDraw2 and IDirectDraw4 are this same object.
        RetainObject(this);
        *out = this;
        return DD_OK;
    }
};

Surface* AsSurface(NS_MacDoze::IUnknown* object) { return static_cast<Surface*>(object); }

// The original DDSURFACEDESC (0x6c bytes on the Mac: DDSURFACEDESC2 without the DDSCAPS2 tail, one caps word) and the
// DX6 one share every field the engine reads, so the DDSURFACEDESC calls convert and use the DX6 ones' bodies.
void ToDesc2(const DDSURFACEDESC& from, DDSURFACEDESC2* to) {
    std::memset(to, 0, sizeof *to);
    to->dwSize = sizeof *to;
    to->dwFlags = from.dwFlags;
    to->dwHeight = from.dwHeight;
    to->dwWidth = from.dwWidth;
    to->lPitch = from.lPitch;
    to->dwBackBufferCount = from.dwBackBufferCount;
    to->lpSurface = from.lpSurface;
    to->ddpfPixelFormat = from.ddpfPixelFormat;
    to->ddsCaps.dwCaps = from.dwCaps;
}
void FromDesc2(const DDSURFACEDESC2& from, DDSURFACEDESC* to) {
    std::memset(to, 0, sizeof *to);
    to->dwSize = sizeof *to;
    to->dwFlags = from.dwFlags;
    to->dwHeight = from.dwHeight;
    to->dwWidth = from.dwWidth;
    to->lPitch = from.lPitch;
    to->dwBackBufferCount = from.dwBackBufferCount;
    to->lpSurface = from.lpSurface;
    to->ddpfPixelFormat = from.ddpfPixelFormat;
    to->dwCaps = from.ddsCaps.dwCaps;
}

// A primary surface (caps 0x200) is the size of the display mode; with back buffers asked
// for (the full-screen flipping chain) one back buffer is attached. Any other surface has
// the size in the description.
int32_t MakeSurface(const DDSURFACEDESC2* desc, NS_MacDoze::IUnknown** out) {
    if (desc == nullptr || out == nullptr) return DDERR_INVALIDPARAMS;
    const uint32_t caps = (desc->dwFlags & 1) != 0 ? desc->ddsCaps.dwCaps : 0;
    const bool primary = (caps & 0x200) != 0;
    const uint32_t width = primary || (desc->dwFlags & 4) == 0 ? gMode.width : desc->dwWidth;
    const uint32_t height = primary || (desc->dwFlags & 2) == 0 ? gMode.height : desc->dwHeight;
    auto* surface = new Surface(width, height, gMode.depth, caps);
    RetainObject(surface);
    if (primary && (desc->dwFlags & 0x20) != 0 && desc->dwBackBufferCount != 0) {
        surface->mBack = new Surface(width, height, gMode.depth, 4);   // DDSCAPS_BACKBUFFER
        RetainObject(surface->mBack);
    }
    *out = surface;
    return DD_OK;
}

// Only what the engine's start-up asks for: a colour fill of the whole surface (flag
// 0x400, the fill colour in DDBLTFX's last word) and whole-surface copies between surfaces
// of the same size. Nothing is drawn to the screen from here.
int32_t BltSurface(Surface* dest, const Surface* from, uint32_t flags, DDBLTFX* fx) {
    if ((flags & 0x400) != 0 && fx != nullptr) {
        const uint32_t colour = fx->words[23];
        if (dest->mDepth == 16) {
            auto* p = reinterpret_cast<uint16_t*>(dest->mPixels.data());
            for (size_t i = 0, n = dest->mPixels.size() / 2; i < n; ++i) p[i] = static_cast<uint16_t>(colour);
        } else {
            auto* p = reinterpret_cast<uint32_t*>(dest->mPixels.data());
            for (size_t i = 0, n = dest->mPixels.size() / 4; i < n; ++i) p[i] = colour;
        }
        return DD_OK;
    }
    if (from != nullptr && from->mPixels.size() == dest->mPixels.size())
        std::memcpy(dest->mPixels.data(), from->mPixels.data(), dest->mPixels.size());
    return DD_OK;
}
}  // namespace

// ---- IUnknown ----------------------------------------------------------------------------
namespace NS_MacDoze {

IUnknown::~IUnknown() {}

int32_t IUnknown::QueryInterface(const _GUID&, void** out) {
    *out = nullptr;
    return E_NOINTERFACE;
}

uint32_t IUnknown::Release() {
    auto& counts = RefCounts();
    const auto it = counts.find(this);
    if (it == counts.end()) return 0;
    if (--it->second != 0) return it->second;
    counts.erase(it);
    delete this;
    return 0;
}

}  // namespace NS_MacDoze

// ---- DirectDraw ----------------------------------------------------------------------------
// The _WIN32 parts of this file: Windows port contributed by samueleagle91 (tpw-port PR #1).
#ifdef _WIN32
// SDL's DirectInput joystick code (SDL_windowsjoystick.c) defines the same two IIDs, with the same bytes, as selectany,
// and COFF takes a selectany definition only beside other selectany ones.
__attribute__((selectany)) const NS_MacDoze::_GUID IID_IDirectDraw2 = {
    {0xe0, 0xf3, 0xa6, 0xb3, 0x43, 0x2b, 0xcf, 0x11, 0xa2, 0xde, 0x00, 0xaa, 0x00, 0xb9, 0x33, 0x56}};
__attribute__((selectany)) const NS_MacDoze::_GUID IID_IDirectDraw4 = {
    {0x2a, 0x50, 0x59, 0x9c, 0xbd, 0x39, 0xd1, 0x11, 0x8c, 0x4a, 0x00, 0xc0, 0x4f, 0xd9, 0x30, 0xc5}};
#else
const NS_MacDoze::_GUID IID_IDirectDraw2 = {{0xe0, 0xf3, 0xa6, 0xb3, 0x43, 0x2b, 0xcf, 0x11, 0xa2, 0xde, 0x00, 0xaa, 0x00,
                                             0xb9, 0x33, 0x56}};
const NS_MacDoze::_GUID IID_IDirectDraw4 = {{0x2a, 0x50, 0x59, 0x9c, 0xbd, 0x39, 0xd1, 0x11, 0x8c, 0x4a, 0x00, 0xc0, 0x4f,
                                             0xd9, 0x30, 0xc5}};
#endif

// One device: the display (the NULL GUID, DirectDraw's primary display driver).
int32_t DirectDrawEnumerateA(int32_t (*callback)(NS_MacDoze::_GUID*, char*, char*, void*), void* context) {
    char description[] = "Primary Display Driver";
    char name[] = "display";
    callback(nullptr, description, name, context);
    return DD_OK;
}

int32_t DirectDrawCreate(NS_MacDoze::_GUID*, IDirectDraw** out, NS_MacDoze::IUnknown*) {
    auto* object = new DirectDraw;
    RetainObject(object);
    *out = object;
    if (tpw::MainWindow() != nullptr) {
        int w = 0, h = 0;
        tpw::GameSize(w, h);   // the game's resolution (with the fit the window is another size)
        gMode.width = static_cast<uint32_t>(w);
        gMode.height = static_cast<uint32_t>(h);
    }
    return DD_OK;
}

// DDSCL_FULLSCREEN (bit 0) takes the screen for the window, which shows it: the engine's full-screen start-up
// (tb_ddraw.cpp) never calls ShowWindow itself, and without an intro film (--no-movies, or a film that does not
// open) nothing else would, so the game would run with its window hidden. Showing an already visible window
// changes nothing.
int32_t IDirectDraw::SetCooperativeLevel(GrafPort* window, uint32_t level) {
    if ((level & 1u) != 0 && window != nullptr && ::FrontWindow() != window) NS_MacDoze::ShowWindow(window, 5);
    return DD_OK;
}

// Full screen: the game keeps drawing at its own resolution, which it now names; the window follows
// (tpw::SetGameSize: re-fitted to the screen, or resized to it with --no-fit).
int32_t IDirectDraw::SetDisplayMode(uint32_t width, uint32_t height, uint32_t depth, uint32_t, uint32_t) {
    gMode.width = width;
    gMode.height = height;
    gMode.depth = depth == 16 ? 16 : 32;
    tpw::SetGameSize(static_cast<int>(width), static_cast<int>(height));
    return DD_OK;
}

int32_t IDirectDraw::RestoreDisplayMode() { return DD_OK; }

// The engine reads only dwVidMemTotal (tb_ddraw.cpp SetupCaps); the rest stays zero.
int32_t IDirectDraw::GetCaps(DDCAPS_DX6* driver, DDCAPS_DX6* emulation) {
    for (DDCAPS_DX6* caps : {driver, emulation}) {
        if (caps == nullptr) continue;
        const uint32_t size = caps->dwSize;
        std::memset(caps, 0, sizeof *caps);
        caps->dwSize = size;
        caps->dwVidMemTotal = 64u * 1024 * 1024;
    }
    return DD_OK;
}

int32_t IDirectDraw::CreateClipper(uint32_t, IDirectDrawClipper** out, NS_MacDoze::IUnknown*) {
    auto* clipper = new Clipper;
    RetainObject(clipper);
    *out = clipper;
    return DD_OK;
}

int32_t IDirectDrawClipper::SetHWnd(uint32_t, GrafPort* window) {
    static_cast<Clipper*>(this)->mWindow = window;
    return DD_OK;
}

int32_t IDirectDraw4::GetDeviceIdentifier(DDDEVICEIDENTIFIER* identifier, uint32_t) {
    std::memset(identifier, 0, sizeof *identifier);
    std::strncpy(identifier->driver, "tpw platform layer", sizeof identifier->driver - 1);
    std::strncpy(identifier->description, "OpenGL (SDL3)", sizeof identifier->description - 1);
    return DD_OK;
}

int32_t IDirectDraw4::GetDisplayMode(DDSURFACEDESC2* desc) {
    std::memset(desc, 0, sizeof *desc);
    desc->dwSize = sizeof *desc;
    desc->dwFlags = 0x100e;   // height, width, pitch, pixel format
    desc->dwHeight = gMode.height;
    desc->dwWidth = gMode.width;
    desc->lPitch = static_cast<int32_t>(gMode.width * (gMode.depth / 8));
    FillPixelFormat(desc->ddpfPixelFormat, gMode.depth);
    return DD_OK;
}

int32_t IDirectDraw4::CreateSurface(DDSURFACEDESC2* desc, IDirectDrawSurface4** out, NS_MacDoze::IUnknown*) {
    if (desc == nullptr || out == nullptr) return DDERR_INVALIDPARAMS;
    NS_MacDoze::IUnknown* surface = nullptr;
    const int32_t result = MakeSurface(desc, &surface);
    if (result == DD_OK) *out = static_cast<IDirectDrawSurface4*>(AsSurface(surface));
    return result;
}

// indirectx 0x10001594: the original descriptor takes the same road (the shim's IdxSurface is made from it).
int32_t IDirectDraw::CreateSurface(DDSURFACEDESC* desc, IDirectDrawSurface** out, NS_MacDoze::IUnknown*) {
    if (desc == nullptr || out == nullptr) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 desc2;
    ToDesc2(*desc, &desc2);
    NS_MacDoze::IUnknown* surface = nullptr;
    const int32_t result = MakeSurface(&desc2, &surface);
    if (result == DD_OK) *out = AsSurface(surface);
    return result;
}

// ---- Surfaces ----------------------------------------------------------------------------
int32_t IDirectDrawSurface4::GetAttachedSurface(DDSCAPS2* caps, IDirectDrawSurface4** out) {
    Surface* back = AsSurface(this)->mBack;
    if (back == nullptr || caps == nullptr || (caps->dwCaps & 4) == 0) {
        *out = nullptr;
        return DDERR_NOTFOUND;
    }
    RetainObject(back);
    *out = back;
    return DD_OK;
}

int32_t IDirectDrawSurface4::Lock(SamsUtilities::URectangle::LongRect*, DDSURFACEDESC2* desc, uint32_t,
                                 NS_MacDoze::InternalHandle*) {
    Surface* surface = AsSurface(this);
    surface->Describe(desc);
    desc->lpSurface = surface->mPixels.data();
    return DD_OK;
}

int32_t IDirectDrawSurface::Unlock(void*) { return DD_OK; }

int32_t IDirectDrawSurface::Restore() { return DD_OK; }

int32_t IDirectDrawSurface::SetClipper(IDirectDrawClipper* clipper) {
    AsSurface(this)->mClipper = clipper;
    return DD_OK;
}

int32_t IDirectDrawSurface3::GetSurfaceDesc(DDSURFACEDESC2* desc) {
    AsSurface(this)->Describe(desc);
    return DD_OK;
}

int32_t IDirectDrawSurface4::Blt(SamsUtilities::URectangle::LongRect*, IDirectDrawSurface4* source,
                                SamsUtilities::URectangle::LongRect*, uint32_t flags, DDBLTFX* fx) {
    return BltSurface(AsSurface(this), source != nullptr ? AsSurface(source) : nullptr, flags, fx);
}

// ---- The original (DDSURFACEDESC) spellings and the free-function forms -----------------------
// indirectx 0x100017f0, IDirectDraw::GetDisplayMode: fills the descriptor with the display mode and answers 0.
int32_t IDirectDraw_GetDisplayMode(IDirectDraw*, DDSURFACEDESC* desc) {
    if (desc == nullptr) return DDERR_INVALIDPARAMS;
    DDSURFACEDESC2 desc2;
    std::memset(&desc2, 0, sizeof desc2);
    desc2.dwSize = sizeof desc2;
    desc2.dwFlags = 0x100e;   // height, width, pitch, pixel format
    desc2.dwHeight = gMode.height;
    desc2.dwWidth = gMode.width;
    desc2.lPitch = static_cast<int32_t>(gMode.width * (gMode.depth / 8));
    FillPixelFormat(desc2.ddpfPixelFormat, gMode.depth);
    FromDesc2(desc2, desc);
    return DD_OK;
}

// indirectx 0x10003ca4: the surface's Lock, on the original descriptor.
int32_t IDirectDrawSurface::Lock(SamsUtilities::URectangle::LongRect* rect, DDSURFACEDESC* desc, uint32_t flags,
                                 NS_MacDoze::InternalHandle* event) {
    if (desc == nullptr) return E_FAIL;
    DDSURFACEDESC2 desc2;
    const int32_t result = static_cast<IDirectDrawSurface4*>(this)->Lock(rect, &desc2, flags, event);
    FromDesc2(desc2, desc);
    return result;
}

// indirectx 0x100040d4.
int32_t IDirectDrawSurface::GetSurfaceDesc(DDSURFACEDESC* desc) {
    if (desc == nullptr) return E_FAIL;
    DDSURFACEDESC2 desc2;
    AsSurface(this)->Describe(&desc2);
    FromDesc2(desc2, desc);
    return DD_OK;
}

// indirectx 0x1000426c: the original Blt reaches the same surface code as the DX6 one.
int32_t IDirectDrawSurface::Blt(SamsUtilities::URectangle::LongRect*, IDirectDrawSurface* source,
                                SamsUtilities::URectangle::LongRect*, uint32_t flags, DDBLTFX* fx) {
    return BltSurface(AsSurface(this), source != nullptr ? AsSurface(source) : nullptr, flags, fx);
}

// indirectx stubs (8 bytes each, `li r3,0; blr`: IsLost 0x10005104, SetColorKey 0x1000519c, BltFast 0x10005030,
// CreatePalette 0x10001c64, EnumDisplayModes 0x10001cec, FlipToGDISurface 0x10001d54, WaitForVerticalBlank 0x10001d90,
// GetDC 0x100050ac, ReleaseDC 0x10005140, SetPalette 0x100051ec): the OpenGL implementation keeps no state behind
// these, so each answers DD_OK and does nothing. A surface is never "lost" and has no colour key or GDI device context.
int32_t IDirectDrawSurface::IsLost() { return DD_OK; }
int32_t IDirectDrawSurface::SetColorKey(uint32_t, DDCOLORKEY*) { return DD_OK; }
int32_t IDirectDrawSurface::BltFast(uint32_t, uint32_t, IDirectDrawSurface*,
                                    SamsUtilities::URectangle::LongRect*, uint32_t) { return DD_OK; }
int32_t IDirectDraw_CreatePalette(IDirectDraw*, uint32_t, const TbPaletteEntry*, IDirectDrawPalette**,
                                  NS_MacDoze::IUnknown*) { return DD_OK; }                     // no palette object is made
int32_t IDirectDraw_EnumDisplayModes(IDirectDraw*, uint32_t, DDSURFACEDESC*, void*,
                                     bool (*)(DDSURFACEDESC*, void*)) { return DD_OK; }       // no mode is reported
int32_t IDirectDraw_FlipToGDISurface(IDirectDraw*) { return DD_OK; }
int32_t IDirectDraw_WaitForVerticalBlank(IDirectDraw*, uint32_t, NS_MacDoze::InternalHandle*) { return DD_OK; }
int32_t IDirectDrawSurface_GetDC(IDirectDrawSurface*, NS_MacDoze::InternalHandle**) { return DD_OK; }
int32_t IDirectDrawSurface_ReleaseDC(IDirectDrawSurface*, NS_MacDoze::InternalHandle*) { return DD_OK; }
int32_t IDirectDrawSurface_SetPalette(IDirectDrawSurface*, IDirectDrawPalette*) { return DD_OK; }

// indirectx 0x10003f70 -> IdxSurface::Flip 0x10003718: the OpenGL surface's Flip always fails with E_FAIL; the frame reaches the
// screen through UOpenGL::Swap.
int32_t IDirectDrawSurface_Flip(IDirectDrawSurface*, IDirectDrawSurface*, uint32_t) { return E_FAIL; }

// indirectx 0x100041c0 -> IdxSurface::GetAttachedSurface 0x1000374c: caps 0x20 names the primary surface and 4 the back buffer;
// the surface found is AddRef'd and handed out, E_FAIL when there is none.
int32_t IDirectDrawSurface_GetAttachedSurface(IDirectDrawSurface* self, DDSCAPS* caps, IDirectDrawSurface** out) {
    Surface* found = nullptr;
    if (caps != nullptr) {
        if ((caps->dwCaps & 0x20) != 0) found = AsSurface(self);
        else if ((caps->dwCaps & 4) != 0) found = AsSurface(self)->mBack;
    }
    if (found == nullptr || out == nullptr) {
        if (out != nullptr) *out = nullptr;
        return E_FAIL;
    }
    RetainObject(found);
    *out = found;
    return DD_OK;
}

// macdoze 0x10001670, IUnknown::AddRef: the count is the word after the vtable; here it is the layer's table. An object
// the layer did not make has no count to raise.
uint32_t NS_MacDoze::IUnknown::AddRef() {
    auto& counts = RefCounts();
    const auto it = counts.find(this);
    return it == counts.end() ? 0 : ++it->second;
}
