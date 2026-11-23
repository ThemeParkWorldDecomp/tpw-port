// Procedural texture cache, Mac engine 0x1001275c..0x10013238.
#pragma once
#include <cstdint>
#include "landscape.h"
class CTextureEntry;
struct SProcTexture {
    SProcTexture* mPrevious;   // 0x00
    SProcTexture* mNext;       // 0x04
    SProcTexture** mSlot;      // 0x08, pointer into the tile lookup table
    CTextureEntry* mTexture;   // 0x0c, one-element array
};
union PrepareEntry {
    uint32_t mTile;
    CTextureEntry* mTexture;
};
class CProcTextureHandler {
public:
    CProcTextureHandler();
    ~CProcTextureHandler();
    int32_t Init(int32_t count, int32_t size);
    int32_t Close();
    bool Load(char* name);
    int32_t PrepareList(PrepareEntry* entries, sTileOverlay* overlays, int32_t count);
    int32_t CreateProcTexture(CTextureEntry* entry, int32_t x, int32_t y, sTileOverlay* overlay);

    Landscape* mLandscape;             // 0x00
    int32_t mSize;                     // 0x04
    SProcTexture* mTiles[16384];        // 0x08, index = (tile >> 8) * 128 + (tile & 255)
    SProcTexture* mCyclic;             // 0x10008
    SProcTexture* mCurrent;            // 0x1000c, not set by the constructor
    int32_t mCount;                    // 0x10010
};
extern uint8_t* gProcTextureBuffer;    // engine data 0x10052b00
void ConvertSquareFrom32(void* dest, void* source, int32_t size);
// TPW_PORT_FIX groundpool PortGroundRun | Mac: the procedural textures come only from the handler's ring of tiles | Port: a pool of procedural textures for the cells of the window in view that the ring does not take (CHField::Prepare when gPortProcCapped), each made at the ring's size and shrunk to the size the cell covers on the screen (include/PortGroundPool.h), a few each frame, in src/engine/tb_init.cpp | Why: the far ground had the mesh's plain texture past the ring's nearest cells, a visible circle at the Free camera's far zoom and long Draw distances
// Set while the pool has tiles to make or make again; CMapWho::Render then tells the frame the view changed, so Prepare
// runs and goes on making them while the camera is still.
inline bool gPortGroundPending = false;
// The pool's tile for a tile of the window ((row << 8) | column, as PrepareEntry::mTile), or null.
CTextureEntry* PortGroundTexture(uint32_t tile);
// The ground of a tile changed (its cell's regenerate flag): the pool's tile is made again.
void PortGroundDirty(uint32_t tile);
// A cell of the window this frame wants the pool's tile, covering `pixels` across on the screen, with its overlay
// (mData the mesh's texture entry, or null) as the ring's list has it.
void PortGroundAsk(uint32_t tile, float pixels, const sTileOverlay& overlay);
// Makes the tiles the cells asked for (nearest first, for at most kPortGroundMillis), lets go of the ones not seen when
// over the budget, and forgets this frame's asks.
void PortGroundRun(CProcTextureHandler* handler);
// Lets go of every tile (the handler's Close).
void PortGroundClear();
