// film_frame.h: a film's frame on the screen (platform/quicktime.cpp). The frame stays at the film's own size (320 x 352
// for the game's films) in one texture, made by the first frame and updated in place by each one after it, and is
// drawn as a quad over the movie's box, so the graphics card scales it (bilinear, as FFmpeg's scaler did on the
// processor before, tpw-port issue #77). It takes only the OpenGL 1.1 calls the game makes, so it draws with whichever
// backend is in use (platform/gfx.h): the system's OpenGL, or on Linux the port's Vulkan renderer, where the texture is
// one image updated by a copy from a staging buffer (platform/vulkan/draw.cpp, TexSubImage2D).
#pragma once
#include <cstdint>

namespace tpw::film {

// The texture a film's frames go into (0: not made yet), and the size it was made at.
struct Texture {
    uint32_t name = 0;
    int width = 0, height = 0;
};

// Where the frame goes: the box in the surface's pixels, from its top left. It may lie partly outside the surface (a
// widescreen film fills the window and crops its overflow, src/game/Movie.cpp); only the part inside is drawn.
struct Box {
    int x = 0, top = 0, width = 0, height = 0;
};

// The surface (surfaceWide x surfaceHigh pixels: the back buffer, or the game's framebuffer with the fit) cleared to
// black, and the frame (rgb: width x height pixels, 3 bytes a pixel, top row first) drawn into the box. The OpenGL
// state the game set is put back as it was.
void Draw(Texture& texture, const uint8_t* rgb, int width, int height, const Box& box, int surfaceWide, int surfaceHigh);
// Gives the texture back (when the film is disposed of).
void Release(Texture& texture);

}  // namespace tpw::film
