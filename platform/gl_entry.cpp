// gl_entry.cpp: the OpenGL calls of the Linux build. The game (include/platform/opengl.h) and the platform layer (the
// cursor overlay, the films, the OpenGL backend's framebuffers) call OpenGL 1.1 by name; on a Mac and on Windows those
// names are the system's OpenGL library. On Linux nothing of OpenGL is linked: each name is defined here and goes to the
// port's Vulkan renderer (platform/vulkan/) when that is the backend in use (platform/gfx.h), else to the system's
// OpenGL, which SDL opens with the window (SDL_WINDOW_OPENGL) and whose entry points are taken once a context exists
// (gfx::OnGLContextMade, which the OpenGL backend calls, platform/gl_backend.cpp). A call before either exists does
// nothing. So the build needs no OpenGL development package and the launcher no OpenGL library to start: the driver is
// opened at run time, as SDL opens the window system and the sound.
//
// Two of the game's calls name the window's buffers (glDrawBuffer(GL_BACK), glReadBuffer(GL_BACK)) while, on the OpenGL
// backend, its framebuffer object is bound, where those would be errors: platform/offscreen.cpp answers them from the
// object's one colour buffer (as it does on the other systems by standing in front of the system library). GLU's one
// call, gluBuild2DMipmaps, is not in the OpenGL library and SDL cannot find it: its chain is built here.
#ifdef __linux__
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "gfx.h"
#include "gl_names.h"
#include "offscreen.h"
#include "vulkan/renderer.h"

namespace {

using namespace tpw::glnames;
namespace vk = tpw::vkr;

// The system's OpenGL entry points, taken when a context exists (NULL before, and for an extension the driver lacks).
struct System {
    void (*glPushAttrib)(uint32_t) = nullptr;
    void (*glPopAttrib)() = nullptr;
    void (*glPushClientAttrib)(uint32_t) = nullptr;
    void (*glPopClientAttrib)() = nullptr;
    void (*glEnable)(uint32_t) = nullptr;
    void (*glDisable)(uint32_t) = nullptr;
    void (*glPixelStorei)(uint32_t, int32_t) = nullptr;
    void (*glRasterPos3f)(float, float, float) = nullptr;
    void (*glWindowPos2i)(int32_t, int32_t) = nullptr;
    void (*glPixelZoom)(float, float) = nullptr;
    void (*glDrawPixels)(int32_t, int32_t, uint32_t, uint32_t, const void*) = nullptr;
    uint32_t (*glGetError)() = nullptr;
    const uint8_t* (*glGetString)(uint32_t) = nullptr;
    void (*glGetIntegerv)(uint32_t, int32_t*) = nullptr;
    void (*glClearColor)(float, float, float, float) = nullptr;
    void (*glClear)(uint32_t) = nullptr;
    void (*glDepthMask)(uint8_t) = nullptr;
    void (*glDepthFunc)(uint32_t) = nullptr;
    void (*glColorMask)(uint8_t, uint8_t, uint8_t, uint8_t) = nullptr;
    void (*glEnableClientState)(uint32_t) = nullptr;
    void (*glViewport)(int32_t, int32_t, int32_t, int32_t) = nullptr;
    void (*glMatrixMode)(uint32_t) = nullptr;
    void (*glLoadIdentity)() = nullptr;
    void (*glPushMatrix)() = nullptr;
    void (*glPopMatrix)() = nullptr;
    void (*glOrtho)(double, double, double, double, double, double) = nullptr;
    void (*glFrontFace)(uint32_t) = nullptr;
    void (*glShadeModel)(uint32_t) = nullptr;
    void (*glHint)(uint32_t, uint32_t) = nullptr;
    void (*glAlphaFunc)(uint32_t, float) = nullptr;
    void (*glBlendFunc)(uint32_t, uint32_t) = nullptr;
    void (*glFlush)() = nullptr;
    void (*glFinish)() = nullptr;
    void (*glGenTextures)(int32_t, uint32_t*) = nullptr;
    void (*glDeleteTextures)(int32_t, const uint32_t*) = nullptr;
    void (*glBindTexture)(uint32_t, uint32_t) = nullptr;
    void (*glTexEnvf)(uint32_t, uint32_t, float) = nullptr;
    void (*glTexEnvi)(uint32_t, uint32_t, int32_t) = nullptr;
    void (*glTexParameteri)(uint32_t, uint32_t, int32_t) = nullptr;
    void (*glTexImage2D)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void*) = nullptr;
    void (*glTexSubImage2D)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void*) = nullptr;
    void (*glVertexPointer)(int32_t, uint32_t, int32_t, const void*) = nullptr;
    void (*glTexCoordPointer)(int32_t, uint32_t, int32_t, const void*) = nullptr;
    void (*glColorPointer)(int32_t, uint32_t, int32_t, const void*) = nullptr;
    void (*glLockArraysEXT)(int32_t, int32_t) = nullptr;
    void (*glUnlockArraysEXT)() = nullptr;
    void (*glDrawElements)(uint32_t, int32_t, uint32_t, const void*) = nullptr;
    void (*glDrawBuffer)(uint32_t) = nullptr;
    void (*glReadBuffer)(uint32_t) = nullptr;
    void (*glReadPixels)(int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void*) = nullptr;
    void (*glBegin)(uint32_t) = nullptr;
    void (*glEnd)() = nullptr;
    void (*glColor4ubv)(const uint8_t*) = nullptr;
    void (*glColor4f)(float, float, float, float) = nullptr;
    void (*glColor4dv)(const double*) = nullptr;
    void (*glTexCoord2f)(float, float) = nullptr;
    void (*glVertex2f)(float, float) = nullptr;
    void (*glVertex4f)(float, float, float, float) = nullptr;
    void (*glVertex2d)(double, double) = nullptr;
    void (*glDeleteLists)(uint32_t, int32_t) = nullptr;
} gl;

template <typename F> void Take(F& pointer, const char* name) { pointer = reinterpret_cast<F>(SDL_GL_GetProcAddress(name)); }

bool Vulkan() { return tpw::gfx::Chosen() == tpw::gfx::Api::Vulkan; }

bool NamesWindowBuffer(uint32_t mode) { return mode == GL_FRONT || mode == GL_BACK || mode == GL_FRONT_AND_BACK; }

// gluBuild2DMipmaps for the system's OpenGL: the picture and every smaller level, each a 2 x 2 mean of the one before
// (GLU's box filter for the powers of two the game's pages are), given to glTexImage2D as RGBA8.
int32_t BuildMipmapsWithGL(uint32_t target, int32_t width, int32_t height, uint32_t format, uint32_t type, const void* pixels) {
    if (gl.glTexImage2D == nullptr || pixels == nullptr || width < 1 || height < 1) return GL_INVALID_VALUE;
    if (type != GL_UNSIGNED_BYTE || (format != GL_RGBA && format != GL_RGB)) return GL_INVALID_ENUM;
    const size_t bpp = format == GL_RGB ? 3 : 4;
    std::vector<uint8_t> level(static_cast<size_t>(width) * height * 4);
    const uint8_t* from = static_cast<const uint8_t*>(pixels);
    for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
        level[i * 4] = from[i * bpp];
        level[i * 4 + 1] = from[i * bpp + 1];
        level[i * 4 + 2] = from[i * bpp + 2];
        level[i * 4 + 3] = bpp == 4 ? from[i * bpp + 3] : 255;
    }
    int32_t wide = width, high = height;
    for (int32_t index = 0;; ++index) {
        gl.glTexImage2D(target, index, GL_RGBA8, wide, high, 0, GL_RGBA, GL_UNSIGNED_BYTE, level.data());
        if (wide == 1 && high == 1) break;
        int32_t nextWide = 0, nextHigh = 0;
        level = tpw::gfx::HalfSizeRGBA(level, wide, high, nextWide, nextHigh);
        wide = nextWide;
        high = nextHigh;
    }
    return 0;
}

}  // namespace

namespace tpw::gfx {
void OnGLContextMade() {
#define TPW_TAKE(name) Take(gl.name, #name);
    TPW_TAKE(glPushAttrib) TPW_TAKE(glPopAttrib) TPW_TAKE(glPushClientAttrib) TPW_TAKE(glPopClientAttrib) TPW_TAKE(glEnable)
    TPW_TAKE(glDisable) TPW_TAKE(glPixelStorei) TPW_TAKE(glRasterPos3f) TPW_TAKE(glWindowPos2i) TPW_TAKE(glPixelZoom)
    TPW_TAKE(glDrawPixels) TPW_TAKE(glGetError) TPW_TAKE(glGetString) TPW_TAKE(glGetIntegerv) TPW_TAKE(glClearColor)
    TPW_TAKE(glClear) TPW_TAKE(glDepthMask) TPW_TAKE(glDepthFunc) TPW_TAKE(glColorMask) TPW_TAKE(glEnableClientState)
    TPW_TAKE(glViewport) TPW_TAKE(glMatrixMode) TPW_TAKE(glLoadIdentity) TPW_TAKE(glPushMatrix) TPW_TAKE(glPopMatrix)
    TPW_TAKE(glOrtho) TPW_TAKE(glFrontFace) TPW_TAKE(glShadeModel) TPW_TAKE(glHint) TPW_TAKE(glAlphaFunc)
    TPW_TAKE(glBlendFunc) TPW_TAKE(glFlush) TPW_TAKE(glFinish) TPW_TAKE(glGenTextures) TPW_TAKE(glDeleteTextures)
    TPW_TAKE(glBindTexture) TPW_TAKE(glTexEnvf) TPW_TAKE(glTexEnvi) TPW_TAKE(glTexParameteri) TPW_TAKE(glTexImage2D)
    TPW_TAKE(glTexSubImage2D) TPW_TAKE(glVertexPointer) TPW_TAKE(glTexCoordPointer) TPW_TAKE(glColorPointer)
    TPW_TAKE(glLockArraysEXT) TPW_TAKE(glUnlockArraysEXT) TPW_TAKE(glDrawElements) TPW_TAKE(glDrawBuffer)
    TPW_TAKE(glReadBuffer) TPW_TAKE(glReadPixels) TPW_TAKE(glBegin) TPW_TAKE(glEnd) TPW_TAKE(glColor4ubv)
    TPW_TAKE(glColor4f) TPW_TAKE(glColor4dv) TPW_TAKE(glTexCoord2f) TPW_TAKE(glVertex2f) TPW_TAKE(glVertex4f)
    TPW_TAKE(glVertex2d) TPW_TAKE(glDeleteLists)
#undef TPW_TAKE
}
}  // namespace tpw::gfx

// Each call: the Vulkan renderer's, or the system's when it has been taken.
#define TPW_GL(name, call)                  \
    if (Vulkan()) return vk::call;          \
    if (gl.name != nullptr) return gl.name

extern "C" {
void glPushAttrib(uint32_t mask) { TPW_GL(glPushAttrib, PushAttrib(mask))(mask); }
void glPopAttrib() { TPW_GL(glPopAttrib, PopAttrib())(); }
void glPushClientAttrib(uint32_t mask) { TPW_GL(glPushClientAttrib, PushClientAttrib(mask))(mask); }
void glPopClientAttrib() { TPW_GL(glPopClientAttrib, PopClientAttrib())(); }
void glEnable(uint32_t cap) { TPW_GL(glEnable, Enable(cap))(cap); }
void glDisable(uint32_t cap) { TPW_GL(glDisable, Disable(cap))(cap); }
void glPixelStorei(uint32_t name, int32_t value) { TPW_GL(glPixelStorei, PixelStorei(name, value))(name, value); }
void glRasterPos3f(float x, float y, float z) { TPW_GL(glRasterPos3f, RasterPos3f(x, y, z))(x, y, z); }
void glWindowPos2i(int32_t x, int32_t y) { TPW_GL(glWindowPos2i, WindowPos2i(x, y))(x, y); }
void glPixelZoom(float x, float y) { TPW_GL(glPixelZoom, PixelZoom(x, y))(x, y); }
void glDrawPixels(int32_t width, int32_t height, uint32_t format, uint32_t type, const void* pixels) {
    TPW_GL(glDrawPixels, DrawPixels(width, height, format, type, pixels))(width, height, format, type, pixels);
}
uint32_t glGetError() {
    TPW_GL(glGetError, GetError())();
    return GL_NO_ERROR;
}
const uint8_t* glGetString(uint32_t name) {
    TPW_GL(glGetString, GetString(name))(name);
    return nullptr;
}
void glGetIntegerv(uint32_t name, int32_t* values) { TPW_GL(glGetIntegerv, GetIntegerv(name, values))(name, values); }
void glClearColor(float red, float green, float blue, float alpha) {
    TPW_GL(glClearColor, ClearColor(red, green, blue, alpha))(red, green, blue, alpha);
}
void glClear(uint32_t mask) { TPW_GL(glClear, Clear(mask))(mask); }
void glDepthMask(uint8_t flag) { TPW_GL(glDepthMask, DepthMask(flag))(flag); }
void glDepthFunc(uint32_t func) { TPW_GL(glDepthFunc, DepthFunc(func))(func); }
void glColorMask(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
    TPW_GL(glColorMask, ColorMask(red, green, blue, alpha))(red, green, blue, alpha);
}
void glEnableClientState(uint32_t array) { TPW_GL(glEnableClientState, EnableClientState(array))(array); }
void glViewport(int32_t x, int32_t y, int32_t width, int32_t height) {
    TPW_GL(glViewport, Viewport(x, y, width, height))(x, y, width, height);
}
void glMatrixMode(uint32_t mode) { TPW_GL(glMatrixMode, MatrixMode(mode))(mode); }
void glLoadIdentity() { TPW_GL(glLoadIdentity, LoadIdentity())(); }
void glPushMatrix() { TPW_GL(glPushMatrix, PushMatrix())(); }
void glPopMatrix() { TPW_GL(glPopMatrix, PopMatrix())(); }
void glOrtho(double left, double right, double bottom, double top, double near, double far) {
    TPW_GL(glOrtho, Ortho(left, right, bottom, top, near, far))(left, right, bottom, top, near, far);
}
void glFrontFace(uint32_t mode) { TPW_GL(glFrontFace, FrontFace(mode))(mode); }
void glShadeModel(uint32_t mode) { TPW_GL(glShadeModel, ShadeModel(mode))(mode); }
void glHint(uint32_t target, uint32_t mode) { TPW_GL(glHint, Hint(target, mode))(target, mode); }
void glAlphaFunc(uint32_t func, float ref) { TPW_GL(glAlphaFunc, AlphaFunc(func, ref))(func, ref); }
void glBlendFunc(uint32_t source, uint32_t dest) { TPW_GL(glBlendFunc, BlendFunc(source, dest))(source, dest); }
void glFlush() { TPW_GL(glFlush, Flush())(); }
void glFinish() { TPW_GL(glFinish, Finish())(); }
void glGenTextures(int32_t n, uint32_t* textures) { TPW_GL(glGenTextures, GenTextures(n, textures))(n, textures); }
void glDeleteTextures(int32_t n, const uint32_t* textures) { TPW_GL(glDeleteTextures, DeleteTextures(n, textures))(n, textures); }
void glBindTexture(uint32_t target, uint32_t texture) { TPW_GL(glBindTexture, BindTexture(target, texture))(target, texture); }
void glTexEnvf(uint32_t target, uint32_t name, float value) {
    TPW_GL(glTexEnvf, TexEnvi(target, name, static_cast<int32_t>(value)))(target, name, value);
}
void glTexEnvi(uint32_t target, uint32_t name, int32_t value) { TPW_GL(glTexEnvi, TexEnvi(target, name, value))(target, name, value); }
void glTexParameteri(uint32_t target, uint32_t name, int32_t value) {
    TPW_GL(glTexParameteri, TexParameteri(target, name, value))(target, name, value);
}
void glTexImage2D(uint32_t target, int32_t level, int32_t internalFormat, int32_t width, int32_t height, int32_t border,
                  uint32_t format, uint32_t type, const void* pixels) {
    TPW_GL(glTexImage2D, TexImage2D(target, level, internalFormat, width, height, border, format, type, pixels))
    (target, level, internalFormat, width, height, border, format, type, pixels);
}
void glTexSubImage2D(uint32_t target, int32_t level, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format,
                     uint32_t type, const void* pixels) {
    TPW_GL(glTexSubImage2D, TexSubImage2D(target, level, x, y, width, height, format, type, pixels))
    (target, level, x, y, width, height, format, type, pixels);
}
void glVertexPointer(int32_t size, uint32_t type, int32_t stride, const void* pointer) {
    TPW_GL(glVertexPointer, VertexPointer(size, type, stride, pointer))(size, type, stride, pointer);
}
void glTexCoordPointer(int32_t size, uint32_t type, int32_t stride, const void* pointer) {
    TPW_GL(glTexCoordPointer, TexCoordPointer(size, type, stride, pointer))(size, type, stride, pointer);
}
void glColorPointer(int32_t size, uint32_t type, int32_t stride, const void* pointer) {
    TPW_GL(glColorPointer, ColorPointer(size, type, stride, pointer))(size, type, stride, pointer);
}
void glLockArraysEXT(int32_t first, int32_t count) { TPW_GL(glLockArraysEXT, LockArrays(first, count))(first, count); }
void glUnlockArraysEXT() { TPW_GL(glUnlockArraysEXT, UnlockArrays())(); }
void glDrawElements(uint32_t mode, int32_t count, uint32_t type, const void* indices) {
    TPW_GL(glDrawElements, DrawElements(mode, count, type, indices))(mode, count, type, indices);
}
void glDrawBuffer(uint32_t mode) {
    if (Vulkan()) return vk::DrawBuffer(mode);
    if (tpw::offscreen::GameBound() && NamesWindowBuffer(mode)) return;   // the framebuffer's colour attachment is it
    if (gl.glDrawBuffer != nullptr) gl.glDrawBuffer(mode);
}
void glReadBuffer(uint32_t mode) {
    if (Vulkan()) return vk::ReadBuffer(mode);
    if (tpw::offscreen::GameBound() && NamesWindowBuffer(mode)) return;
    if (gl.glReadBuffer != nullptr) gl.glReadBuffer(mode);
}
void glReadPixels(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format, uint32_t type, void* pixels) {
    TPW_GL(glReadPixels, ReadPixels(x, y, width, height, format, type, pixels))(x, y, width, height, format, type, pixels);
}
void glBegin(uint32_t mode) { TPW_GL(glBegin, Begin(mode))(mode); }
void glEnd() { TPW_GL(glEnd, End())(); }
void glColor4ubv(const uint8_t* c) { TPW_GL(glColor4ubv, Color4ub(c[0], c[1], c[2], c[3]))(c); }
void glColor4f(float red, float green, float blue, float alpha) { TPW_GL(glColor4f, Color4f(red, green, blue, alpha))(red, green, blue, alpha); }
void glColor4dv(const double* c) {
    TPW_GL(glColor4dv, Color4f(static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2]), static_cast<float>(c[3])))(c);
}
void glTexCoord2f(float s, float t) { TPW_GL(glTexCoord2f, TexCoord2f(s, t))(s, t); }
void glVertex2f(float x, float y) { TPW_GL(glVertex2f, Vertex4f(x, y, 0.0f, 1.0f))(x, y); }
void glVertex4f(float x, float y, float z, float w) { TPW_GL(glVertex4f, Vertex4f(x, y, z, w))(x, y, z, w); }
void glVertex2d(double x, double y) { TPW_GL(glVertex2d, Vertex4f(static_cast<float>(x), static_cast<float>(y), 0.0f, 1.0f))(x, y); }
void glDeleteLists(uint32_t list, int32_t range) {   // (COpenGLPrintf::Close: the game never makes a display list)
    if (Vulkan()) return;
    if (gl.glDeleteLists != nullptr) gl.glDeleteLists(list, range);
}
int32_t gluBuild2DMipmaps(uint32_t target, int32_t internalFormat, int32_t width, int32_t height, uint32_t format, uint32_t type,
                          const void* pixels) {
    if (Vulkan()) return vk::BuildMipmaps(target, internalFormat, width, height, format, type, pixels);
    // The system's OpenGL is given every level as RGBA8, which is what the game asks for (GL_RGBA, GL_RGBA8).
    return BuildMipmapsWithGL(target, width, height, format, type, pixels);
}
}  // extern "C"
#undef TPW_GL
#endif
