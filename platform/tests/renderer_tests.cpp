// renderer_tests.cpp: the port's Vulkan renderer (platform/vulkan/) against the system's OpenGL, through the OpenGL
// calls the game makes (platform/gl_entry.cpp): a frame drawn as the game draws (indexed vertex arrays in the engine's
// vertex layout, the depth test, the alpha test, blending, a texture page, glDrawPixels at a zoom, the cursor's ortho
// quad in immediate mode), read back with glReadPixels and checked pixel by pixel against what OpenGL 1.1 says; then
// the two renderers' pictures compared. Run by ctest ("renderer") on Linux; it needs a display (CI gives it Xvfb) and a
// Vulkan device (lavapipe, the Mesa software one, will do) and skips (exit 77) without them. It asks gfx.h for each
// renderer (Request), so a software Vulkan device draws here even beside a hardware OpenGL, which the launcher's default
// would choose instead.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gfx.h"
#include "gl_names.h"

// The OpenGL calls the scene makes (gl.h's shapes; on Linux they are platform/gl_entry.cpp's).
extern "C" {
void glViewport(int32_t x, int32_t y, int32_t width, int32_t height);
void glClearColor(float red, float green, float blue, float alpha);
void glClear(uint32_t mask);
void glEnable(uint32_t cap);
void glDisable(uint32_t cap);
void glDepthFunc(uint32_t func);
void glDepthMask(uint8_t flag);
void glEnableClientState(uint32_t array);
void glShadeModel(uint32_t mode);
void glAlphaFunc(uint32_t func, float ref);
void glBlendFunc(uint32_t source, uint32_t dest);
void glMatrixMode(uint32_t mode);
void glLoadIdentity();
void glPushMatrix();
void glPopMatrix();
void glOrtho(double left, double right, double bottom, double top, double near, double far);
void glPushAttrib(uint32_t mask);
void glPopAttrib();
void glGenTextures(int32_t n, uint32_t* textures);
void glDeleteTextures(int32_t n, const uint32_t* textures);
void glBindTexture(uint32_t target, uint32_t texture);
void glTexEnvf(uint32_t target, uint32_t name, float value);
void glTexEnvi(uint32_t target, uint32_t name, int32_t value);
void glTexParameteri(uint32_t target, uint32_t name, int32_t value);
void glTexImage2D(uint32_t target, int32_t level, int32_t internalFormat, int32_t width, int32_t height, int32_t border,
                  uint32_t format, uint32_t type, const void* pixels);
void glTexSubImage2D(uint32_t target, int32_t level, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format,
                     uint32_t type, const void* pixels);
void glPixelStorei(uint32_t name, int32_t value);
void glVertexPointer(int32_t size, uint32_t type, int32_t stride, const void* pointer);
void glTexCoordPointer(int32_t size, uint32_t type, int32_t stride, const void* pointer);
void glColorPointer(int32_t size, uint32_t type, int32_t stride, const void* pointer);
void glLockArraysEXT(int32_t first, int32_t count);
void glUnlockArraysEXT();
void glDrawElements(uint32_t mode, int32_t count, uint32_t type, const void* indices);
void glRasterPos3f(float x, float y, float z);
void glPixelZoom(float x, float y);
void glDrawPixels(int32_t width, int32_t height, uint32_t format, uint32_t type, const void* pixels);
void glBegin(uint32_t mode);
void glEnd();
void glColor4f(float red, float green, float blue, float alpha);
void glTexCoord2f(float s, float t);
void glVertex2f(float x, float y);
void glReadBuffer(uint32_t mode);
void glReadPixels(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format, uint32_t type, void* pixels);
uint32_t glGetError();
void glFinish();
int32_t gluBuild2DMipmaps(uint32_t target, int32_t internalFormat, int32_t width, int32_t height, uint32_t format, uint32_t type,
                          const void* pixels);
}

namespace {

using namespace tpw::glnames;

constexpr int kWide = 256, kHigh = 192;
int gChecks = 0, gFailures = 0;

void Check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

// The engine's vertex (tb_drawprimitive's 0x1c4 format): position, colour, specular, texture coordinate; 0x20 bytes.
struct DrawVertex {
    float x, y, z, w;
    uint8_t colour[4];
    uint32_t specular;
    float u, v;
};

// A quad from (x0, y0) to (x1, y1) in clip space at depth z, as the engine sends one (two triangles, six indices).
void Quad(float x0, float y0, float x1, float y1, float z, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
    DrawVertex vertices[4] = {
        {x0, y0, z, 1.0f, {red, green, blue, alpha}, 0, 0.0f, 0.0f},
        {x1, y0, z, 1.0f, {red, green, blue, alpha}, 0, 1.0f, 0.0f},
        {x1, y1, z, 1.0f, {red, green, blue, alpha}, 0, 1.0f, 1.0f},
        {x0, y1, z, 1.0f, {red, green, blue, alpha}, 0, 0.0f, 1.0f},
    };
    const uint16_t indices[6] = {0, 2, 1, 0, 3, 2};
    glVertexPointer(4, GL_FLOAT, sizeof(DrawVertex), vertices);
    glTexCoordPointer(2, GL_FLOAT, sizeof(DrawVertex), &vertices[0].u);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(DrawVertex), vertices[0].colour);
    glLockArraysEXT(0, 4);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
    glUnlockArraysEXT();
}

// The frame: what each part of the picture must show is listed by Expect below.
void DrawScene() {
    glViewport(0, 0, kWide, kHigh);
    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // As COpenGL::InitRenderStates and COpenGLDrawPrimitive::Init leave the state.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(1);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glShadeModel(GL_SMOOTH);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GEQUAL, 1.0f);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // 1. A red quad over the left half at depth 0.5, then a green one over the middle at depth 0.7: the red stays where
    // they overlap (the depth test), the green shows where they do not.
    Quad(-1.0f, -1.0f, 0.0f, 1.0f, 0.5f, 255, 0, 0, 255);
    Quad(-0.5f, -0.5f, 0.5f, 0.5f, 0.7f, 0, 255, 0, 255);

    // 2. A texture page: 2 x 2 texels (white, black / blue, yellow) made as the engine makes one (GL_RGBA4, glTexImage2D
    // then glTexSubImage2D), nearest filtering, over the top right quarter, modulated by a half-bright vertex colour.
    uint32_t page = 0;
    glGenTextures(1, &page);
    glBindTexture(GL_TEXTURE_2D, page);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, static_cast<float>(GL_MODULATE));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    const uint8_t zeros[16] = {0};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA4, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, zeros);
    const uint8_t texels[16] = {255, 255, 255, 255, 0, 0, 0, 255, 0, 0, 255, 255, 255, 255, 0, 255};
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    Quad(0.0f, 0.0f, 1.0f, 1.0f, 0.5f, 128, 128, 128, 255);
    glDisable(GL_TEXTURE_2D);

    // 3. The alpha test as the message boxes use it (GL_GEQUAL 1.0): a quad of alpha 0x40 over the bottom right is not
    // drawn; one of alpha 0xff beside it is.
    glEnable(GL_ALPHA_TEST);
    Quad(0.0f, -1.0f, 0.5f, -0.5f, 0.5f, 255, 0, 255, 0x40);
    Quad(0.5f, -1.0f, 1.0f, -0.5f, 0.5f, 255, 0, 255, 0xff);
    glDisable(GL_ALPHA_TEST);

    // 4. Blending by source alpha: a half-transparent blue quad over the red's lower part.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    Quad(-1.0f, -1.0f, -0.5f, -0.5f, 0.5f, 0, 0, 255, 128);
    glDisable(GL_BLEND);

    // 5. glDrawPixels as the splash draws its picture: a 4 x 2 RGB picture (red row, then green row; rows from the
    // bottom) at the top left, zoomed 8 x 8, so 32 x 16 pixels: red at the bottom of the box, green above it.
    glPushAttrib(GL_ENABLE_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const uint8_t picture[4 * 2 * 3] = {255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 255, 0};
    glRasterPos3f(-1.0f, 2.0f * (kHigh - 16) / kHigh - 1.0f, 0.5f);
    glPixelZoom(8.0f, 8.0f);
    glDrawPixels(4, 2, GL_RGB, GL_UNSIGNED_BYTE, picture);
    glPopAttrib();

    // 6. The cursor overlay's way: an orthographic projection in pixels from the top left, a textured quad in immediate
    // mode with GL_REPLACE, the state pushed and popped round it: the page's texels at 8 pixels each, at (200, 8).
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, kWide, kHigh, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, page);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor4f(0.25f, 0.25f, 0.25f, 1.0f);   // (replaced by the texel)
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(200.0f, 8.0f);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(216.0f, 8.0f);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(216.0f, 24.0f);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(200.0f, 24.0f);
    glEnd();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
    glDeleteTextures(1, &page);

    // 7. A mipmapped page, as the texture cache makes one with mipmaps on (gluBuild2DMipmaps, GL_LINEAR_MIPMAP_NEAREST): a
    // 4 x 4 checker of black and white, drawn into 2 x 2 pixels at the bottom middle, so its second level (every texel
    // the mean of 2 x 2, grey) is the one sampled. glFinish in the middle of the frame, as the engine's flag 1 asks for.
    glFinish();
    uint32_t chain = 0;
    glGenTextures(1, &chain);
    glBindTexture(GL_TEXTURE_2D, chain);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    uint8_t checker[4 * 4 * 4];
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const uint8_t v = ((x + y) & 1) != 0 ? 255 : 0;
            checker[(y * 4 + x) * 4] = checker[(y * 4 + x) * 4 + 1] = checker[(y * 4 + x) * 4 + 2] = v;
            checker[(y * 4 + x) * 4 + 3] = 255;
        }
    gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA4, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, checker);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    Quad(-2.0f / kWide, -1.0f, 2.0f / kWide, -1.0f + 4.0f / kHigh, 0.5f, 255, 255, 255, 255);
    glDisable(GL_TEXTURE_2D);
    glDeleteTextures(1, &chain);
}

struct Picture {
    std::vector<uint8_t> rgba;   // kWide x kHigh, rows from the bottom (as glReadPixels gives them)
    const uint8_t* At(int x, int y) const { return rgba.data() + (static_cast<size_t>(y) * kWide + x) * 4; }
};

// A pixel of the picture (x from the left, y from the bottom) is near the colour.
void Expect(const Picture& picture, const char* renderer, int x, int y, int red, int green, int blue, const char* what,
            int tolerance = 2) {
    const uint8_t* p = picture.At(x, y);
    const bool ok = std::abs(p[0] - red) <= tolerance && std::abs(p[1] - green) <= tolerance && std::abs(p[2] - blue) <= tolerance;
    char text[256];
    std::snprintf(text, sizeof text, "%s: pixel (%d, %d) is %d %d %d, wanted %d %d %d: %s", renderer, x, y, p[0], p[1], p[2], red,
                  green, blue, what);
    Check(ok, text);
}

void ExpectScene(const Picture& picture, const char* renderer) {
    // The clear colour where nothing was drawn: the right half between the page quad and the alpha test's quads.
    Expect(picture, renderer, 224, 60, 26, 51, 77, "the clear colour");
    // 1. The red quad over the left half, the green in the middle, the red where they overlap (depth).
    Expect(picture, renderer, 16, 96, 255, 0, 0, "the red quad");
    Expect(picture, renderer, 100, 96, 255, 0, 0, "the red quad under the green one (the depth test)");
    Expect(picture, renderer, 160, 72, 0, 255, 0, "the green quad where the red is not");
    // 2. The texture page over the top right quarter: s runs right, t runs up (the vertex at the bottom left has t 0),
    // modulated by 128: texel (0, 0) white -> 128 grey at the bottom left, (1, 0) black, (0, 1) blue, (1, 1) yellow.
    Expect(picture, renderer, 160, 120, 128, 128, 128, "the page's white texel, modulated");
    Expect(picture, renderer, 224, 120, 0, 0, 0, "the page's black texel");
    Expect(picture, renderer, 160, 168, 0, 0, 128, "the page's blue texel, modulated");
    Expect(picture, renderer, 224, 168, 128, 128, 0, "the page's yellow texel, modulated");
    // 3. The alpha test: the left quad was not drawn (the green under it shows at the top of that box, the clear
    // colour lower), the right one was.
    Expect(picture, renderer, 160, 24, 26, 51, 77, "the quad the alpha test dropped");
    Expect(picture, renderer, 224, 24, 255, 0, 255, "the quad the alpha test kept");
    // 4. Blending: blue at 128/255 over red.
    Expect(picture, renderer, 32, 24, 127, 0, 128, "the blended quad", 3);
    // 5. glDrawPixels: red at the bottom of its box, green at the top, the clear colour to its right.
    Expect(picture, renderer, 8, kHigh - 12, 255, 0, 0, "the drawn pixels' first row");
    Expect(picture, renderer, 8, kHigh - 4, 0, 255, 0, "the drawn pixels' second row");
    Expect(picture, renderer, 40, kHigh - 8, 255, 0, 0, "beside the drawn pixels (the red quad again)");
    // 6. The ortho quad: texel (0, 0) white at the top left of the quad (t 0 at y 8 from the top), yellow at the bottom
    // right, over the page quad's pixels.
    Expect(picture, renderer, 204, kHigh - 12, 255, 255, 255, "the ortho quad's white texel (GL_REPLACE)");
    Expect(picture, renderer, 212, kHigh - 20, 255, 255, 0, "the ortho quad's yellow texel");
    // 7. The mipmapped page minified: its second level's grey (the mean of black and white, 127 or 128).
    Expect(picture, renderer, kWide / 2 - 1, 0, 128, 128, 128, "the mipmapped page's second level", 3);
    Expect(picture, renderer, kWide / 2, 1, 128, 128, 128, "the mipmapped page's second level, the other pixel", 3);
}

// Draws the scene with the backend `api` and reads it back; false (and why) when the backend is not there.
bool Run(tpw::gfx::Api api, Picture& picture, Picture& presented, std::string& why) {
    tpw::gfx::Request(api);
    tpw::gfx::Probe();
    if (tpw::gfx::Chosen() != api) {
        why = std::string(tpw::gfx::Name(api)) + " cannot draw here";
        return false;
    }
    SDL_Window* window = SDL_CreateWindow("renderer test", 320, 240, tpw::gfx::WindowFlags() | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        why = std::string("no window: ") + SDL_GetError();
        return false;
    }
    if (!tpw::gfx::Create(window, true, kWide, kHigh)) {
        why = "no device on the window";
        SDL_DestroyWindow(window);
        return false;
    }
    std::printf("%s\n", tpw::gfx::Description());
    Check(tpw::gfx::FitReady(), std::string(tpw::gfx::Name(api)) + ": the game's picture of its own size");
    glGetError();
    DrawScene();
    Check(glGetError() == GL_NO_ERROR, std::string(tpw::gfx::Name(api)) + ": no OpenGL error from the scene");
    picture.rgba.assign(static_cast<size_t>(kWide) * kHigh * 4, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, kWide, kHigh, GL_RGBA, GL_UNSIGNED_BYTE, picture.rgba.data());
    Check(glGetError() == GL_NO_ERROR, std::string(tpw::gfx::Name(api)) + ": no OpenGL error from the read");
    // One pixel at an offset, as the engine's GetPixel reads it (x + 1, y + 1, the pack alignment 1).
    uint8_t one[4] = {0, 0, 0, 0};
    glReadPixels(16 + 1, 96 + 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, one);
    Check(one[0] == 255 && one[1] == 0 && one[2] == 0, std::string(tpw::gfx::Name(api)) + ": one pixel read at an offset is the red quad's");
    // A present, as the swap does it: the picture copied, read for the dump, scaled into the window.
    tpw::gfx::BeginPresent();
    std::vector<uint8_t> rgb(static_cast<size_t>(kWide) * kHigh * 3);
    Check(tpw::gfx::ReadPresent(rgb.data(), kWide, kHigh), std::string(tpw::gfx::Name(api)) + ": the present picture is read");
    presented.rgba.assign(static_cast<size_t>(kWide) * kHigh * 4, 255);
    for (size_t i = 0; i < static_cast<size_t>(kWide) * kHigh; ++i) std::memcpy(&presented.rgba[i * 4], &rgb[i * 3], 3);
    int pixelsWide = 0, pixelsHigh = 0;
    SDL_GetWindowSizeInPixels(window, &pixelsWide, &pixelsHigh);
    tpw::gfx::EndPresent(tpw::fit::PixelBox{0, 0, pixelsWide, pixelsHigh}, pixelsWide, pixelsHigh);
    // And the frame after it: the game's picture is still what was drawn.
    Picture again;
    again.rgba.assign(picture.rgba.size(), 0);
    glReadPixels(0, 0, kWide, kHigh, GL_RGBA, GL_UNSIGNED_BYTE, again.rgba.data());
    Check(again.rgba == picture.rgba, std::string(tpw::gfx::Name(api)) + ": the game's picture is kept across the present");
    // The window resized (Alt+Enter, a drag of the frame): the next present goes into a swapchain of the new size, with
    // the picture letterboxed; the game's picture is untouched by it.
    SDL_SetWindowSize(window, 400, 300);
    SDL_SyncWindow(window);
    SDL_GetWindowSizeInPixels(window, &pixelsWide, &pixelsHigh);
    tpw::gfx::BeginPresent();
    const tpw::fit::PixelBox box = tpw::fit::RoundBox(tpw::fit::Letterbox(pixelsWide, pixelsHigh, kWide, kHigh));
    tpw::gfx::EndPresent(box, pixelsWide, pixelsHigh);
    tpw::gfx::BeginPresent();
    tpw::gfx::EndPresent(box, pixelsWide, pixelsHigh);
    glReadPixels(0, 0, kWide, kHigh, GL_RGBA, GL_UNSIGNED_BYTE, again.rgba.data());
    Check(glGetError() == GL_NO_ERROR && again.rgba == picture.rgba,
          std::string(tpw::gfx::Name(api)) + ": the game's picture is kept across a resize and two more presents");
    tpw::gfx::Destroy();
    SDL_DestroyWindow(window);
    return true;
}

// How many pixels of the two pictures differ by more than `tolerance` in a channel.
size_t Differences(const Picture& a, const Picture& b, int tolerance) {
    size_t count = 0;
    for (size_t i = 0; i < static_cast<size_t>(kWide) * kHigh; ++i) {
        const uint8_t* p = a.rgba.data() + i * 4;
        const uint8_t* q = b.rgba.data() + i * 4;
        if (std::abs(p[0] - q[0]) > tolerance || std::abs(p[1] - q[1]) > tolerance || std::abs(p[2] - q[2]) > tolerance) ++count;
    }
    return count;
}

// The picture as a PPM, for a look by eye (TPW_RENDERER_PICTURES names the folder).
void Save(const Picture& picture, const char* name) {
    const char* folder = std::getenv("TPW_RENDERER_PICTURES");
    if (folder == nullptr || folder[0] == '\0') return;
    const std::string path = std::string(folder) + "/" + name + ".ppm";
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return;
    std::fprintf(file, "P6\n%d %d\n255\n", kWide, kHigh);
    for (int y = kHigh - 1; y >= 0; --y)
        for (int x = 0; x < kWide; ++x) std::fwrite(picture.At(x, y), 1, 3, file);
    std::fclose(file);
    std::printf("wrote %s\n", path.c_str());
}

}  // namespace

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("renderer tests: no video (%s): skipped\n", SDL_GetError());
        return 77;
    }
    Picture vulkan, vulkanPresented, opengl, openglPresented;
    std::string vulkanWhy, openglWhy;
    const bool haveVulkan = Run(tpw::gfx::Api::Vulkan, vulkan, vulkanPresented, vulkanWhy);
    const bool haveOpenGL = Run(tpw::gfx::Api::OpenGL, opengl, openglPresented, openglWhy);
    if (!haveVulkan) std::printf("renderer tests: %s\n", vulkanWhy.c_str());
    if (!haveOpenGL) std::printf("renderer tests: %s\n", openglWhy.c_str());
    if (!haveVulkan && !haveOpenGL) {
        std::printf("renderer tests: neither renderer can draw here: skipped\n");
        SDL_Quit();
        return 77;
    }
    if (haveVulkan) {
        ExpectScene(vulkan, "Vulkan");
        Save(vulkan, "vulkan");
        Check(Differences(vulkan, vulkanPresented, 0) == 0, "Vulkan: the present picture is the game's");
    }
    if (haveOpenGL) {
        ExpectScene(opengl, "OpenGL");
        Save(opengl, "opengl");
        Check(Differences(opengl, openglPresented, 0) == 0, "OpenGL: the present picture is the game's");
    }
    if (haveVulkan && haveOpenGL) {
        // The two renderers agree on all but the edges of the primitives, where rasterisation rules may differ by a pixel.
        const size_t different = Differences(vulkan, opengl, 8);
        std::printf("renderer tests: %zu of %d pixels differ between Vulkan and OpenGL\n", different, kWide * kHigh);
        Check(different < static_cast<size_t>(kWide * kHigh) / 200, "Vulkan and OpenGL draw the same picture (under 0.5% of pixels differ)");
    }
    SDL_Quit();
    if (gFailures != 0) {
        std::printf("renderer tests: %d of %d checks failed\n", gFailures, gChecks);
        return 1;
    }
    std::printf("renderer tests: %d checks passed (%s%s%s)\n", gChecks, haveVulkan ? "Vulkan" : "", haveVulkan && haveOpenGL ? " and " : "",
                haveOpenGL ? "OpenGL" : "");
    return 0;
}
