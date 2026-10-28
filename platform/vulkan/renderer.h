// renderer.h: the port's Vulkan renderer (platform/vulkan/): the backend of platform/gfx.h on Linux, and the OpenGL 1.1
// calls the game makes (include/platform/opengl.h), each as a function of the same shape, which the Linux entry points
// (platform/gl_entry.cpp) call when this renderer is the one in use.
//
// How it draws: the game's picture is an image of the game's own resolution (the fit, platform/fit.h; the window's size
// without it) with a depth buffer, drawn into through one render pass that is begun at the first drawing call of a frame
// and ended when something else is needed (an upload, a read, the present). Every vertex the game gives, whether through
// its vertex arrays, glBegin/glEnd or glDrawPixels, becomes one vertex of one layout (position, colour, texture
// coordinate) in a buffer the frame fills, and every drawing call is one Vulkan draw with a pipeline chosen by the
// OpenGL state that is pipeline state (blending, the depth test, the colour mask, the primitive) and push constants for
// the rest (the matrices, texturing, the alpha test). Textures are RGBA8 images with their own mip chain; a texture page
// uploaded while a frame is being drawn goes into the same command buffer, after the draws recorded before it and before
// those after, so the picture is the one OpenGL would give. A read of the pixels ends the frame so far, runs it and
// waits. The present copies the game's picture to a second image of the same size (the cursor is drawn on the copy),
// then scales that into the swapchain's image, black around it, as many times as the swap interval asks (the vsync
// pacing of platform/window.cpp: each presentation of a FIFO swapchain waits one refresh).
//
// What is not reproduced: fog, lighting, culling, the scissor and stencil tests, dithering, and the hints (the game
// switches them all off or never uses them); the mask of glPushAttrib is honoured by group.
#pragma once
#include <cstdint>

#include "fit.h"

struct SDL_Window;

namespace tpw::vkr {

// ---- The backend (platform/gfx.h's calls)
bool Probe(int& largest);
// The device Probe settled on: its name, and whether it draws in software (VK_PHYSICAL_DEVICE_TYPE_CPU: Mesa's lavapipe,
// SwiftShader), which PickDevice takes only when no other device can draw to the window, and TPW_VULKAN_DEVICE did not
// name it. platform/gfx.cpp then weighs the system's OpenGL against it.
const char* ProbedDevice();
bool ProbedSoftware();
uint32_t WindowFlags();
bool Create(SDL_Window* window, bool fit, int wide, int high);
void Destroy();
bool Created();
bool Fit(bool fit, int wide, int high);
bool FitReady();
bool SetSwapInterval(int interval);
void BeginPresent();
bool ReadPresent(uint8_t* rgb, int width, int height);
void EndPresent(const fit::PixelBox& box, int windowWide, int windowHigh);
const char* Description();

// ---- The OpenGL calls (gl.h's shapes)
void PushAttrib(uint32_t mask);
void PopAttrib();
void PushClientAttrib(uint32_t mask);
void PopClientAttrib();
void Enable(uint32_t cap);
void Disable(uint32_t cap);
void PixelStorei(uint32_t name, int32_t value);
void RasterPos3f(float x, float y, float z);
void WindowPos2i(int32_t x, int32_t y);
void PixelZoom(float x, float y);
void DrawPixels(int32_t width, int32_t height, uint32_t format, uint32_t type, const void* pixels);
uint32_t GetError();
const uint8_t* GetString(uint32_t name);
void GetIntegerv(uint32_t name, int32_t* values);
void ClearColor(float red, float green, float blue, float alpha);
void Clear(uint32_t mask);
void DepthMask(uint8_t flag);
void DepthFunc(uint32_t func);
void ColorMask(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);
void EnableClientState(uint32_t array);
void Viewport(int32_t x, int32_t y, int32_t width, int32_t height);
void MatrixMode(uint32_t mode);
void LoadIdentity();
void PushMatrix();
void PopMatrix();
void Ortho(double left, double right, double bottom, double top, double near, double far);
void FrontFace(uint32_t mode);
void ShadeModel(uint32_t mode);
void Hint(uint32_t target, uint32_t mode);
void AlphaFunc(uint32_t func, float ref);
void BlendFunc(uint32_t source, uint32_t dest);
void Flush();
void Finish();
void GenTextures(int32_t n, uint32_t* textures);
void DeleteTextures(int32_t n, const uint32_t* textures);
void BindTexture(uint32_t target, uint32_t texture);
void TexEnvi(uint32_t target, uint32_t name, int32_t value);
void TexParameteri(uint32_t target, uint32_t name, int32_t value);
void TexImage2D(uint32_t target, int32_t level, int32_t internalFormat, int32_t width, int32_t height, int32_t border,
                uint32_t format, uint32_t type, const void* pixels);
void TexSubImage2D(uint32_t target, int32_t level, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format,
                   uint32_t type, const void* pixels);
int32_t BuildMipmaps(uint32_t target, int32_t internalFormat, int32_t width, int32_t height, uint32_t format, uint32_t type,
                     const void* pixels);
void VertexPointer(int32_t size, uint32_t type, int32_t stride, const void* pointer);
void TexCoordPointer(int32_t size, uint32_t type, int32_t stride, const void* pointer);
void ColorPointer(int32_t size, uint32_t type, int32_t stride, const void* pointer);
void LockArrays(int32_t first, int32_t count);
void UnlockArrays();
void DrawElements(uint32_t mode, int32_t count, uint32_t type, const void* indices);
void DrawBuffer(uint32_t mode);
void ReadBuffer(uint32_t mode);
void ReadPixels(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format, uint32_t type, void* pixels);
void Begin(uint32_t mode);
void End();
void Color4ub(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);
void Color4f(float red, float green, float blue, float alpha);
void TexCoord2f(float s, float t);
void Vertex4f(float x, float y, float z, float w);

}  // namespace tpw::vkr
