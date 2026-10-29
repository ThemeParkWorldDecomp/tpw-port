// film_frame.cpp: a film's frame on the screen (film_frame.h).
#include "film_frame.h"

#include <initializer_list>

#include "gl_names.h"

// The OpenGL calls a frame is drawn with (the game's own are in platform/opengl.h).
extern "C" {
void glPushAttrib(uint32_t mask);
void glPopAttrib();
void glPushClientAttrib(uint32_t mask);
void glPopClientAttrib();
void glEnable(uint32_t capability);
void glDisable(uint32_t capability);
void glDepthMask(uint8_t flag);
void glColorMask(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);
void glClearColor(float red, float green, float blue, float alpha);
void glClear(uint32_t mask);
void glViewport(int32_t x, int32_t y, int32_t width, int32_t height);
void glPixelStorei(uint32_t name, int32_t value);
void glDrawBuffer(uint32_t mode);
void glMatrixMode(uint32_t mode);
void glPushMatrix();
void glPopMatrix();
void glLoadIdentity();
void glOrtho(double left, double right, double bottom, double top, double near, double far);
void glGenTextures(int32_t n, uint32_t* textures);
void glDeleteTextures(int32_t n, const uint32_t* textures);
void glBindTexture(uint32_t target, uint32_t texture);
void glTexEnvi(uint32_t target, uint32_t name, int32_t value);
void glTexParameteri(uint32_t target, uint32_t name, int32_t value);
void glTexImage2D(uint32_t target, int32_t level, int32_t internalFormat, int32_t width, int32_t height, int32_t border,
                  uint32_t format, uint32_t type, const void* pixels);
void glTexSubImage2D(uint32_t target, int32_t level, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format,
                     uint32_t type, const void* pixels);
void glColor4f(float red, float green, float blue, float alpha);
void glBegin(uint32_t mode);
void glEnd();
void glTexCoord2f(float s, float t);
void glVertex2f(float x, float y);
}

namespace tpw::film {

using namespace tpw::glnames;

void Draw(Texture& texture, const uint8_t* rgb, int width, int height, const Box& box, int surfaceWide, int surfaceHigh) {
    if (surfaceWide <= 0 || surfaceHigh <= 0 || rgb == nullptr || width <= 0 || height <= 0) return;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    for (uint32_t capability : {GL_DEPTH_TEST, GL_BLEND, GL_ALPHA_TEST, GL_LIGHTING, GL_FOG, GL_SCISSOR_TEST, GL_STENCIL_TEST,
                                GL_CULL_FACE}) {
        glDisable(capability);
    }
    glDepthMask(0);
    glColorMask(1, 1, 1, 1);
    glDrawBuffer(GL_BACK);   // (with the fit, the game's framebuffer: platform/offscreen.cpp answers)
    glViewport(0, 0, surfaceWide, surfaceHigh);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    // The frame into the texture: the whole of it at the film's first frame (or a new size), in place after that. The
    // sampling is set at every frame (a bound texture's parameters are part of what glPushAttrib saves and restores).
    glEnable(GL_TEXTURE_2D);
    if (texture.name == 0) glGenTextures(1, &texture.name);
    glBindTexture(GL_TEXTURE_2D, texture.name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);   // (GL_CLAMP would blend in black at the edges)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (texture.width != width || texture.height != height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
        texture.width = width;
        texture.height = height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    }
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    // The box in the surface's pixels, rows going down from its top edge: the pixels it covers are those glDrawPixels
    // covered at the box's size before (a raster position at the box's top left, a zoom of -1 going down).
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(surfaceWide), static_cast<double>(surfaceHigh), 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    const auto x0 = static_cast<float>(box.x), y0 = static_cast<float>(box.top);
    const auto x1 = static_cast<float>(box.x + box.width), y1 = static_cast<float>(box.top + box.height);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f);
    glVertex2f(x0, y0);
    glTexCoord2f(1.0f, 0.0f);
    glVertex2f(x1, y0);
    glTexCoord2f(1.0f, 1.0f);
    glVertex2f(x1, y1);
    glTexCoord2f(0.0f, 1.0f);
    glVertex2f(x0, y1);
    glEnd();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();
}

void Release(Texture& texture) {
    if (texture.name != 0) glDeleteTextures(1, &texture.name);
    texture = Texture();
}

}  // namespace tpw::film
