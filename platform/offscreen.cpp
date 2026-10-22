// offscreen.cpp: the game's framebuffer object and the scaled copy to the window (see offscreen.h). The context is the
// legacy (2.1) one SDL makes for the game, so the EXT_framebuffer_object / EXT_framebuffer_blit entry points are used,
// found at run time with SDL_GL_GetProcAddress (the macOS OpenGL library exports them, but a failure to find them is
// then a message and the old 1:1 window, not a crash).
//
// The game itself never names a framebuffer. Two of its calls name the window's buffer, though: glDrawBuffer(GL_BACK)
// (COpenGLDrawPrimitive::DrawDetectPoly) and glReadBuffer(GL_BACK) (GetPixel). With a framebuffer object bound those
// are errors, and the game takes any error as failure, so they are defined here, ahead of the system library's, and
// answer from the object's one colour buffer while the game's framebuffer is bound.
#include <SDL3/SDL.h>
#include <dlfcn.h>

#include <algorithm>
#include <cstring>

#include "offscreen.h"
#include "platform.h"

extern "C" {
void glClearColor(float red, float green, float blue, float alpha);
void glClear(unsigned int mask);
void glViewport(int x, int y, int width, int height);
void glPushAttrib(unsigned int mask);
void glPopAttrib();
void glDisable(unsigned int capability);
void glColorMask(unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha);
unsigned int glGetError();
const unsigned char* glGetString(unsigned int name);
void glGetIntegerv(unsigned int name, int* values);
}

namespace {
constexpr unsigned int kFramebuffer = 0x8D40, kReadFramebuffer = 0x8CA8, kDrawFramebuffer = 0x8CA9;
constexpr unsigned int kRenderbuffer = 0x8D41, kColorAttachment0 = 0x8CE0, kDepthAttachment = 0x8D00;
constexpr unsigned int kFramebufferComplete = 0x8CD5, kRgba8 = 0x8058, kDepth24 = 0x81A6;
constexpr unsigned int kColorBufferBit = 0x00004000, kNearest = 0x2600, kLinear = 0x2601;
constexpr unsigned int kExtensions = 0x1F03, kScissorTest = 0x0C11, kAllAttribBits = 0xffffffff;
constexpr unsigned int kFront = 0x0404, kBack = 0x0405, kFrontAndBack = 0x0408;

struct Api {
    void (*genFramebuffers)(int, unsigned int*) = nullptr;
    void (*bindFramebuffer)(unsigned int, unsigned int) = nullptr;
    void (*deleteFramebuffers)(int, const unsigned int*) = nullptr;
    void (*genRenderbuffers)(int, unsigned int*) = nullptr;
    void (*bindRenderbuffer)(unsigned int, unsigned int) = nullptr;
    void (*deleteRenderbuffers)(int, const unsigned int*) = nullptr;
    void (*renderbufferStorage)(unsigned int, unsigned int, int, int) = nullptr;
    void (*framebufferRenderbuffer)(unsigned int, unsigned int, unsigned int, unsigned int) = nullptr;
    unsigned int (*checkFramebufferStatus)(unsigned int) = nullptr;
    void (*blitFramebuffer)(int, int, int, int, int, int, int, int, unsigned int, unsigned int) = nullptr;
};
Api gApi;

struct Target {
    unsigned int framebuffer = 0;
    unsigned int color = 0;
    unsigned int depth = 0;
};
Target gGame, gPresent;
int gWide = 0, gHigh = 0;
bool gReady = false;
bool gGameBound = false;
bool gErrorLogged = false;

bool HasExtension(const char* extensions, const char* name) {
    if (extensions == nullptr) return false;
    const size_t length = std::strlen(name);
    for (const char* at = extensions; (at = std::strstr(at, name)) != nullptr; at += length) {
        const bool starts = at == extensions || at[-1] == ' ';
        const bool ends = at[length] == ' ' || at[length] == '\0';
        if (starts && ends) return true;
    }
    return false;
}

template <typename Function>
bool Load(Function& pointer, const char* name) {
    pointer = reinterpret_cast<Function>(SDL_GL_GetProcAddress(name));
    return pointer != nullptr;
}

bool LoadApi() {
    if (gApi.blitFramebuffer != nullptr) return true;
    Api api;
    const bool all = Load(api.genFramebuffers, "glGenFramebuffersEXT") && Load(api.bindFramebuffer, "glBindFramebufferEXT") &&
                     Load(api.deleteFramebuffers, "glDeleteFramebuffersEXT") &&
                     Load(api.genRenderbuffers, "glGenRenderbuffersEXT") && Load(api.bindRenderbuffer, "glBindRenderbufferEXT") &&
                     Load(api.deleteRenderbuffers, "glDeleteRenderbuffersEXT") &&
                     Load(api.renderbufferStorage, "glRenderbufferStorageEXT") &&
                     Load(api.framebufferRenderbuffer, "glFramebufferRenderbufferEXT") &&
                     Load(api.checkFramebufferStatus, "glCheckFramebufferStatusEXT") &&
                     Load(api.blitFramebuffer, "glBlitFramebufferEXT");
    if (!all) return false;
    gApi = api;
    return true;
}

// One framebuffer of the given size with a colour renderbuffer and, if asked, a 24-bit depth one.
bool MakeTarget(Target& target, int width, int height, bool withDepth) {
    gApi.genFramebuffers(1, &target.framebuffer);
    gApi.genRenderbuffers(1, &target.color);
    gApi.bindRenderbuffer(kRenderbuffer, target.color);
    gApi.renderbufferStorage(kRenderbuffer, kRgba8, width, height);
    gApi.bindFramebuffer(kFramebuffer, target.framebuffer);
    gApi.framebufferRenderbuffer(kFramebuffer, kColorAttachment0, kRenderbuffer, target.color);
    if (withDepth) {
        gApi.genRenderbuffers(1, &target.depth);
        gApi.bindRenderbuffer(kRenderbuffer, target.depth);
        gApi.renderbufferStorage(kRenderbuffer, kDepth24, width, height);
        gApi.framebufferRenderbuffer(kFramebuffer, kDepthAttachment, kRenderbuffer, target.depth);
    }
    gApi.bindRenderbuffer(kRenderbuffer, 0);
    return gApi.checkFramebufferStatus(kFramebuffer) == kFramebufferComplete;
}

void FreeTarget(Target& target) {
    if (target.framebuffer != 0) gApi.deleteFramebuffers(1, &target.framebuffer);
    if (target.color != 0) gApi.deleteRenderbuffers(1, &target.color);
    if (target.depth != 0) gApi.deleteRenderbuffers(1, &target.depth);
    target = Target();
}
}  // namespace

namespace tpw::offscreen {

bool Create(int width, int height) {
    Destroy();
    if (width <= 0 || height <= 0) return false;
    const char* extensions = reinterpret_cast<const char*>(glGetString(kExtensions));
    if (!HasExtension(extensions, "GL_EXT_framebuffer_object") || !HasExtension(extensions, "GL_EXT_framebuffer_blit") ||
        !LoadApi()) {
        tpw::Log("fit: the OpenGL driver has no framebuffer objects with blit; the window stays 1:1");
        return false;
    }
    glGetError();   // (anything pending is not ours)
    int largest = 0;
    glGetIntegerv(0x84E8, &largest);   // GL_MAX_RENDERBUFFER_SIZE_EXT
    if (largest > 0 && (width > largest || height > largest)) {
        tpw::Log("fit: %dx%d is larger than the driver's %d-pixel framebuffers; pick a smaller --resolution", width, height,
                 largest);
        return false;
    }
    if (!MakeTarget(gGame, width, height, true) || !MakeTarget(gPresent, width, height, false) || glGetError() != 0) {
        tpw::Log("fit: could not make a %dx%d framebuffer; the window stays 1:1", width, height);
        FreeTarget(gGame);
        FreeTarget(gPresent);
        gApi.bindFramebuffer(kFramebuffer, 0);
        glGetError();
        return false;
    }
    gWide = width;
    gHigh = height;
    gReady = true;
    BindGame();
    // A new framebuffer has the context's viewport of the window; the game sets its own, but until it does this is
    // the right one. Both buffers start black.
    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(kColorBufferBit | 0x00000100);   // colour and depth
    BindPresent();
    glClear(kColorBufferBit);
    BindGame();
    return true;
}

void Destroy() {
    if (!gReady) return;
    gApi.bindFramebuffer(kFramebuffer, 0);
    FreeTarget(gGame);
    FreeTarget(gPresent);
    gReady = false;
    gGameBound = false;
    gWide = gHigh = 0;
}

bool Ready() { return gReady; }

int LargestSize() {
    int renderbuffer = 0;
    int texture = 0;
    glGetIntegerv(0x84E8, &renderbuffer);   // GL_MAX_RENDERBUFFER_SIZE_EXT
    glGetIntegerv(0x0D33, &texture);        // GL_MAX_TEXTURE_SIZE
    glGetError();
    if (renderbuffer <= 0) return texture > 0 ? texture : 0;
    return texture > 0 ? std::min(renderbuffer, texture) : renderbuffer;
}
bool GameBound() { return gReady && gGameBound; }

void BindGame() {
    if (!gReady) return;
    gApi.bindFramebuffer(kFramebuffer, gGame.framebuffer);
    gGameBound = true;
}

// The window's own framebuffer. SDL_GL_SwapWindow presents nothing on macOS while a framebuffer object is bound
// (found 2026-10-13: the back buffer held the picture, the screen stayed black), so the swap is made with this bound.
void BindDefault() {
    if (!gReady) return;
    gApi.bindFramebuffer(kFramebuffer, 0);
    gGameBound = false;
}

void BindPresent() {
    if (!gReady) return;
    gApi.bindFramebuffer(kFramebuffer, gPresent.framebuffer);
    gGameBound = false;
}

void CopyGameToPresent() {
    if (!gReady) return;
    glPushAttrib(kAllAttribBits);
    glDisable(kScissorTest);
    gApi.bindFramebuffer(kReadFramebuffer, gGame.framebuffer);
    gApi.bindFramebuffer(kDrawFramebuffer, gPresent.framebuffer);
    gApi.blitFramebuffer(0, 0, gWide, gHigh, 0, 0, gWide, gHigh, kColorBufferBit, kNearest);
    // (the attribute stack restores the draw buffer of whichever framebuffer is bound at the pop: the one of the push)
    gApi.bindFramebuffer(kFramebuffer, gGame.framebuffer);
    glPopAttrib();
}

void ScaleToWindow(const fit::PixelBox& box, int windowWide, int windowHigh) {
    if (!gReady) return;
    glPushAttrib(kAllAttribBits);
    glDisable(kScissorTest);
    glColorMask(1, 1, 1, 1);
    gApi.bindFramebuffer(kDrawFramebuffer, 0);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(kColorBufferBit);
    gApi.bindFramebuffer(kReadFramebuffer, gPresent.framebuffer);
    // OpenGL counts rows from the bottom.
    gApi.blitFramebuffer(0, 0, gWide, gHigh, box.left, windowHigh - box.bottom, box.right, windowHigh - box.top,
                         kColorBufferBit, box.right - box.left == gWide && box.bottom - box.top == gHigh ? kNearest : kLinear);
    gApi.bindFramebuffer(kFramebuffer, gPresent.framebuffer);
    glPopAttrib();
    (void)windowWide;
}

void DrainErrors(const char* where) {
    for (int i = 0; i < 16; ++i) {
        const unsigned int error = glGetError();
        if (error == 0) return;
        if (!gErrorLogged) {
            gErrorLogged = true;
            tpw::Log("fit: OpenGL error 0x%x after %s (further ones are not logged)", error, where);
        }
    }
}

}  // namespace tpw::offscreen

// ---- The game's two calls that name the window's buffers ------------------------------------------
namespace {
bool NamesWindowBuffer(unsigned int mode) { return mode == kFront || mode == kBack || mode == kFrontAndBack; }

// The system library's own glDrawBuffer (draw true) or glReadBuffer, found past this executable's.
void CallSystem(bool draw, unsigned int mode) {
    using Function = void (*)(unsigned int);
    static Function drawBuffer = reinterpret_cast<Function>(dlsym(RTLD_NEXT, "glDrawBuffer"));
    static Function readBuffer = reinterpret_cast<Function>(dlsym(RTLD_NEXT, "glReadBuffer"));
    Function target = draw ? drawBuffer : readBuffer;
    if (target != nullptr) target(mode);
}
}  // namespace

extern "C" {
void glDrawBuffer(unsigned int mode) {
    if (tpw::offscreen::GameBound() && NamesWindowBuffer(mode)) return;   // the framebuffer's colour attachment is it
    CallSystem(true, mode);
}

void glReadBuffer(unsigned int mode) {
    if (tpw::offscreen::GameBound() && NamesWindowBuffer(mode)) return;
    CallSystem(false, mode);
}
}
