// cursor.cpp: Sam's Utilities' cursor (UCursor.cpp of the shim library) and its OpenGL
// drawing (UCursorOpenGL, 0x1a590..0x1b0f4 of sams utils shared). The game loads each of
// its cursor pictures (data:UI:cursors, 32 x 32 frames side by side in a TGA) into one of
// these (src/game/ui/InterfaceBFCursor.cpp, src/game/ui/ui_main.cpp, src/game/ui/UIInterface.cpp), makes the current one current with Set and shows or hides it
// with Show and Hide. On the Mac the object drew its current frame over the back buffer at
// every buffer swap (its Swap member) while the toolbox arrow stayed hidden; here
// tpw::SwapBuffers calls DrawCursorOverlay, which draws the frame at the window's mouse
// position the same way. Written from the shim's names and the game's use of them, not
// from a listing: nothing here matches the Mac object's 0x84 bytes.
#include "platform/samsutils.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "platform.h"
#include "platform/opengl.h"
#include "platform/macdoze.h"
#include "platform/upixels.h"
#include "TbIRLE4bitFont.h"   // gTbFontScale

namespace SamsUtilities {
void UPixelsReadRGBA(UPixels& pixels, uint8_t* out, int32_t width, int32_t height);   // platform/upixels.cpp

namespace {
constexpr int32_t kFrameSize = 32;
UCursor* gCurrentCursor = nullptr;   // the cursor Set made current; NULL shows the host's arrow
int32_t gShowCount = 1;              // the library's count (0x100340ac, 1 at the start): above 0 the cursor is shown

// The host's arrow shows only while no game cursor is current and the cursor is shown.
void UpdateHostCursor() {
    if (gCurrentCursor == nullptr && gShowCount > 0) SDL_ShowCursor();
    else SDL_HideCursor();
}
}  // namespace

void UCursor::SetRate(const UMicroseconds& rate) { mRate = rate.mValue; }

void UCursor::Set() {
    gCurrentCursor = this;
    mFrame = 0;
    mFrameSince = SDL_GetTicks();
    UpdateHostCursor();
}

void UCursor::Reset() {
    if (gCurrentCursor == this) gCurrentCursor = nullptr;
    UpdateHostCursor();
}

// 0x10011950, 0x100119ec and 0x10011cd0 of the shim library, from the listings. The Mac calls the toolbox's ShowCursor
// when the count leaves 0 upwards and HideCursor when it reaches 0 (and tells the cursor in use); here the host arrow
// and the overlay follow the count. Unbalanced calls pile up as they do there: two Shows need two Hides.
int32_t UCursor::Show() {
    ++gShowCount;
    UpdateHostCursor();
    return gShowCount;
}

int32_t UCursor::Hide() {
    --gShowCount;
    UpdateHostCursor();
    return gShowCount;
}

uint8_t UCursor::IsVisible() { return gShowCount > 0 ? 1 : 0; }

// Steps to the frame the clock has reached (one every mRate microseconds; 0 never steps).
void UCursor::Animate() {
    const uint64_t period = mRate > 0 ? static_cast<uint64_t>(mRate / 1000) : 0;
    if (period == 0) return;
    const uint64_t now = SDL_GetTicks();
    while (now - mFrameSince >= period) {
        mFrameSince += period;
        ++mFrame;
    }
}

struct UCursorOpenGL::Pictures {
    std::vector<std::vector<uint8_t>> frames;   // red, green, blue, alpha; kFrameSize square, top row first
    std::vector<GLuint> textures;               // made at the first draw, when a context exists
};

UCursorOpenGL::UCursorOpenGL(UPoint& hotSpot, UPixels* frames, int32_t count, UOpenGL*) {
    mPictures = new Pictures;
    mHotX = hotSpot.x;
    mHotY = hotSpot.y;
    mDraw = [](UCursor* cursor) { static_cast<UCursorOpenGL*>(cursor)->DrawOverlay(); };
    for (int32_t i = 0; i < count; ++i) {
        std::vector<uint8_t> rgba(static_cast<size_t>(kFrameSize) * kFrameSize * 4);
        UPixelsReadRGBA(frames[i], rgba.data(), kFrameSize, kFrameSize);
        mPictures->frames.push_back(std::move(rgba));
    }
}

UCursorOpenGL::~UCursorOpenGL() {
    if (gCurrentCursor == this) {
        gCurrentCursor = nullptr;
        UpdateHostCursor();
    }
    if (!mPictures->textures.empty()) {
        glDeleteTextures(static_cast<GLsizei>(mPictures->textures.size()), mPictures->textures.data());
    }
    delete mPictures;
}

void UCursorOpenGL::SetHotspot(UPoint& hotSpot) {
    mHotX = hotSpot.x;
    mHotY = hotSpot.y;
}

void UCursorOpenGL::Swap(UOpenGL&) { DrawOverlay(); }

// The current frame as a textured square at the mouse, over whatever the frame drew, with the
// drawing state put back afterwards (the game's own state tracking must not see a change).
// With the mouse outside the window nothing is drawn, except in a frame-dumping run, where the
// window's centre stands in so the saved pictures show the cursor. With the window fitted to the
// screen the overlay is drawn in game pixels (tpw::CursorPlacement) onto the copy of the game's
// picture that is then scaled into the window. The cursors were drawn for 1024 x 768, so on a larger
// game picture the cursor grows with the UI, as its fonts do (gTbFontScale, the UI's pixels per unit
// over 1024 x 768's), by a whole number so that its pixels stay square.
void UCursorOpenGL::DrawOverlay() {
    const auto count = static_cast<int32_t>(mPictures->frames.size());
    if (count == 0 || gShowCount <= 0) return;
    Animate();
    const int32_t frame = ((mFrame % count) + count) % count;

    tpw::CursorPlace place;
    if (!tpw::CursorPlacement(place)) return;
    const float mouseX = place.x, mouseY = place.y, scaleX = place.scaleX, scaleY = place.scaleY;
    const int pixelsWide = place.surfaceWide, pixelsHigh = place.surfaceHigh;
    const float grow = std::max(1.0f, std::round(gTbFontScale));
    const float x0 = (mouseX - static_cast<float>(mHotX) * grow) * scaleX;
    const float y0 = (mouseY - static_cast<float>(mHotY) * grow) * scaleY;
    const float x1 = x0 + static_cast<float>(kFrameSize) * grow * scaleX;
    const float y1 = y0 + static_cast<float>(kFrameSize) * grow * scaleY;

    if (mPictures->textures.empty()) {
        mPictures->textures.assign(static_cast<size_t>(count), 0);
        glGenTextures(count, mPictures->textures.data());
        for (int32_t i = 0; i < count; ++i) {
            glBindTexture(GL_TEXTURE_2D, mPictures->textures[static_cast<size_t>(i)]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kFrameSize, kFrameSize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         mPictures->frames[static_cast<size_t>(i)].data());
        }
    }

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, static_cast<double>(pixelsWide), static_cast<double>(pixelsHigh), 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glViewport(0, 0, pixelsWide, pixelsHigh);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_LIGHTING);
    glDepthMask(0);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, mPictures->textures[static_cast<size_t>(frame)]);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
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
    glPopAttrib();
}

}  // namespace SamsUtilities

namespace NS_MacDoze {
// 0x100067f8 of macdoze (60 bytes, unit v5L06): keeps the cursor in its word (0x10012bf4; the Mac answered the old one, which
// the game drops) and makes it current through UCursor::Set. The game passes only NULL (WindowProc on activation). With NULL
// the Mac's Set does nothing while no cursor is current and otherwise calls through the NULL object, so here NULL leaves the
// current cursor as it is.
void SetCursor(SamsUtilities::UCursor* cursor) {
    if (cursor != nullptr) cursor->Set();
}
}  // namespace NS_MacDoze

namespace tpw {
void DrawCursorOverlay() {
    SamsUtilities::UCursor* cursor = SamsUtilities::gCurrentCursor;
    if (cursor != nullptr && cursor->mDraw != nullptr) cursor->mDraw(cursor);
}
}  // namespace tpw
