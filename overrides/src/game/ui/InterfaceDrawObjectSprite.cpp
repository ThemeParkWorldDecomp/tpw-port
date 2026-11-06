// InterfaceDrawObjectSprite.cpp: guessed name (the binary has no __sinit for this file): a contiguous range of InterfaceDrawObjectSprite code, named after it.

#include "MapWho.h"
#include "ui_DrawObject.h"
// TPW_PORT_FIX resslider LiveSprites | Mac: (none) | Port: each surface made, with the size in units its first rectangle asked for and the last text drawn on it | Why: see include/ui_DrawObject.h
#include "TPSettings.h"
#include <string>
#include <unordered_map>
#include <vector>

// InterfaceDrawObjectSprite (main app, 0x1018c978..0x1018d0bc; the inventory files it under ui_textmanager.cpp): the draw object that is
// a surface of the engine's sprite system. See include/ui_DrawObject.h for the class. The engine's sprite surfaces (SSpriteEngineData,
// SSpriteGameData, CSpriteList) are in the engine library; the map's lists are CMapWho's.

namespace {
struct LiveSprite {
    int32_t mUnitsWide;
    int32_t mUnitsHigh;
    bool mHasText;
    std::u16string mText;
};
std::unordered_map<InterfaceDrawObjectSprite*, LiveSprite>& LiveSprites() {
    static auto* live = new std::unordered_map<InterfaceDrawObjectSprite*, LiveSprite>;   // (never destroyed: windows outlive the statics at exit)
    return *live;
}
}  // namespace

// The list of the map the sprite lists of the interface are linked into while shown.
namespace {
constexpr int32_t kSpriteMapList = 0x30;
}

// The members are made in the order the Mac's constructor makes them (the sprite's game data, the surface, the list). The flags
// 0x30000 and the scale 1.0 are the game data's words at Mac +0x10 and +0x2c; the sprite is the list's only instance.
InterfaceDrawObjectSprite::InterfaceDrawObjectSprite(void* data) : InterfaceDrawObject(data) {
    mCreated = 0;
    mInstances = mList.Init(1);
    mSprite.mF10 = 0x30000;
    mSprite.mF2C = 1.0f;
    mSprite.mSprite = &mEngineData;
    mSprite.SetUsedPosition(0, 0);
    mInstances[0] = &mSprite;
    mList.mCount = 1;
    mPreDraw = nullptr;
    mF50 = 0;
}

// (The list and the surface are destroyed by the members' destructors, in that order, before the base.)
// TPW_PORT_FIX resslider ~InterfaceDrawObjectSprite | Mac: nothing | Port: the surface leaves the list PortFollowScreen walks | Why: see include/ui_DrawObject.h
InterfaceDrawObjectSprite::~InterfaceDrawObjectSprite() { if (gPortLiveResolution) LiveSprites().erase(this); }

void InterfaceDrawObjectSprite::UpdateTextures(char16_t*) { mEngineData.RefreshPartialTexture(); }

// Makes the surface the size Change worked out, once.
void InterfaceDrawObjectSprite::CreateTextures(char16_t*) {
    if (mCreated == 0) {
        mEngineData.Init(mPixelWidth, mPixelHeight);
        mSprite.SetUsedSize(mPixelWidth, mPixelHeight);
        mCreated = 1;
    }
}

void InterfaceDrawObjectSprite::Refresh() { mEngineData.RefreshPartialTexture(); }

void InterfaceDrawObjectSprite::Draw(int16_t, CoolRect*, int16_t) {}

void InterfaceDrawObjectSprite::SetPreDrawCallback(int32_t (*callback)(TbRenderArea&)) { mPreDraw = callback; }

namespace {

// The compiler's code for dividing the rectangle's width in the real screen's pixels: (screen width * the width) / 2048, truncated
// toward zero, in 32 bits.
int16_t ScaledWidth(int32_t screen, int32_t width) {
    const int32_t product = screen * width;
    int32_t quotient = product >> 11;
    if (product < 0 && (product & 0x7ff) != 0) quotient += 1;
    return static_cast<int16_t>(quotient);
}

// (screen height * the height) / 1536 as the compiler's multiply-high and shifts do it, truncated toward zero.
int16_t ScaledHeight(int32_t screen, int32_t height) {
    const int32_t product = screen * height;
    const int32_t high = static_cast<int32_t>((static_cast<int64_t>(0x2aaaaaab) * product) >> 32);
    int32_t quotient = high >> 8;
    quotient += static_cast<int32_t>(static_cast<uint32_t>(quotient) >> 31);
    return static_cast<int16_t>(quotient);
}

}  // namespace

// Nothing for a NULL record or no flags. Flag 0x10 takes the rectangle from the record (the first time, also the surface's size
// in real pixels and the surface itself); before that nothing else is done. Flag 2 sets the depth (at least 1), flag 4 the word
// at 0x2c; any of those three places the sprite: x from the left, y from the BOTTOM, z from the depth (only when flag 0x10 or 2 is
// among them: flag 4 alone stores nothing).
void InterfaceDrawObjectSprite::Change(uint32_t flags, DOChange* change) {
    if (change == nullptr || flags == 0) return;
    const bool newRect = (flags & 0x10) != 0;
    if (newRect && mCreated == 0) {
        mRect.mLeft = change->mRect->mLeft;
        mRect.mTop = change->mRect->mTop;
        mRect.mRight = change->mRect->mRight;
        mRect.mBottom = change->mRect->mBottom;
        mPixelWidth = ScaledWidth(gUIRealScreenWidth, mRect.mRight - mRect.mLeft);
        mPixelHeight = ScaledHeight(gUIRealScreenHeight, mRect.mBottom - mRect.mTop);
        CreateTextures(nullptr);
        // TPW_PORT_FIX resslider InterfaceDrawObjectSprite::Change | Mac: the first rectangle sizes the surface once | Port: it is kept for PortFollowScreen | Why: see include/ui_DrawObject.h
        if (gPortLiveResolution) LiveSprites()[this] = LiveSprite{mRect.mRight - mRect.mLeft, mRect.mBottom - mRect.mTop, false, {}};
    } else if (mCreated == 0) {
        return;
    }
    bool placed = false;
    if (newRect) {
        placed = true;
        mRect.mLeft = change->mRect->mLeft;
        mRect.mTop = change->mRect->mTop;
        mRect.mRight = change->mRect->mRight;
        mRect.mBottom = change->mRect->mBottom;
    }
    if ((flags & 2) != 0) {
        mDepth = change->mDepth;
        if (mDepth < 1) mDepth = 1;
        placed = true;
    }
    if ((flags & 4) != 0) {
        placed = true;
        mF2C = change->mF08;
    }
    if (placed && (flags & 0x12) != 0) {
        const float left = static_cast<float>(mRect.mLeft);
        const float bottom = static_cast<float>(mRect.mBottom);
        const float depth = static_cast<float>(mDepth);
        // TPW_PORT_FIX v5L26 InterfaceDrawObjectSprite::Change | Mac: the left edge over 2048 units | Port: over the unit space's width (2048 on a 4:3 screen) | Why: a widescreen port; the sprite sits where its window is
        mSprite.mPosition.x = (2.0f * left) / static_cast<float>(gUIUnitWidth) - 1.0f;
        // TPW_PORT_FIX uiscale InterfaceDrawObjectSprite::Change | Mac: the sprite's bottom edge over 1536 units | Port: over the unit space's height | Why: a smaller interface shows more of the park on a large screen, with the panels still at its edges; contributed by samueleagle91 (tpw-port PR #24)
        mSprite.mPosition.y = 1.0f - (2.0f * bottom) / static_cast<float>(gUIUnitHeight);
        mSprite.mPosition.z = 1.0f - depth / 40.0f;
    }
}

// Links the sprite list into the map's list while shown. Does nothing until the surface exists, and then only when the state changes
// (the state is kept in the base's mShown).
void InterfaceDrawObjectSprite::Show(int16_t shown) {
    if (mCreated != 0 && mShown != shown) {
        CMapLinkBase* link = &mList;
        if (shown != 0) {
            gei_MapWho->Link(link, kSpriteMapList);
        } else {
            gei_MapWho->Unlink(link, kSpriteMapList);
        }
        mShown = shown;
    }
}

int32_t InterfaceDrawObjectSprite::GetWritableDataArea(int32_t* a, int32_t* b) {
    if (mCreated == 0 || a == nullptr || b == nullptr) return 0;
    *a = mEngineData.mWords.mWidth;
    *b = mEngineData.mWords.mHeight;
    return static_cast<int32_t>(reinterpret_cast<intptr_t>(mEngineData.mWords.mPixels));
}

// TPW_PORT_FIX resslider InterfaceDrawObjectSprite::PortRememberText | Mac: (none) | Port: the text a text object has just been asked to draw | Why: see include/ui_DrawObject.h
void InterfaceDrawObjectSprite::PortRememberText(const char16_t* text) {
    if (!gPortLiveResolution) return;
    const auto found = LiveSprites().find(this);
    if (found == LiveSprites().end() || text == nullptr) return;
    found->second.mHasText = true;
    if (found->second.mText != text) found->second.mText = text;
}
// TPW_PORT_FIX resslider InterfaceDrawObjectSprite::PortFollowScreen | Mac: (none) | Port: see include/ui_DrawObject.h | Why: see include/ui_DrawObject.h
void InterfaceDrawObjectSprite::PortFollowScreen() {
    for (auto& [object, sprite] : LiveSprites()) {
        const int32_t oldWide = object->mEngineData.mWidth;
        const int32_t oldHigh = object->mEngineData.mHeight;
        const int32_t usedWide = object->mSprite.mUsedWidth;
        const int32_t usedHigh = object->mSprite.mUsedHeight;
        const int32_t usedX = object->mSprite.mUsedX;
        const int32_t usedY = object->mSprite.mUsedY;
        // (a window that spans the unit space was just given its new width: UI_FollowUnits; the others keep their first size)
        const int32_t across = object->mRect.mRight - object->mRect.mLeft;
        if (across == gUIUnitWidth - 1) sprite.mUnitsWide = across;
        const int32_t wide = ScaledWidth(gUIRealScreenWidth, sprite.mUnitsWide);
        const int32_t high = ScaledHeight(gUIRealScreenHeight, sprite.mUnitsHigh);
        if ((wide != oldWide || high != oldHigh) && object->mEngineData.PortResize(wide, high) != 0) {
            object->mPixelWidth = static_cast<int16_t>(wide);
            object->mPixelHeight = static_cast<int16_t>(high);
            // (a part of the surface in use stays the same part of it)
            object->mSprite.SetUsedPosition(oldWide > 0 ? usedX * wide / oldWide : 0, oldHigh > 0 ? usedY * high / oldHigh : 0);
            object->mSprite.SetUsedSize(oldWide > 0 ? usedWide * wide / oldWide : wide, oldHigh > 0 ? usedHigh * high / oldHigh : high);
        }
        // (as Change does it: a window that did not move still stands somewhere else in the new unit space)
        object->mSprite.mPosition.x = (2.0f * static_cast<float>(object->mRect.mLeft)) / static_cast<float>(gUIUnitWidth) - 1.0f;
        object->mSprite.mPosition.y = 1.0f - (2.0f * static_cast<float>(object->mRect.mBottom)) / static_cast<float>(gUIUnitHeight);
        if (sprite.mHasText) {
            std::u16string text = sprite.mText;
            object->UpdateTextures(text.data());
        }
    }
}
