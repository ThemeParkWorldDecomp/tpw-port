// TpiParkEdge.h: the TPI Worlds and Rides mod's soft edge of the park's land (TPW_MOD_HOOK tpi_worlds, CHField::Execute).
// Land the park does not own is darker: RecalculateParkDarkening (src/game/park/TPHField.cpp) gives each height field cell
// over such land quad bit 0x400. The game shows it two ways. The meshes (the river, its banks, the scenery) are lit per
// vertex, each vertex darker (its normal scaled to 0.225) when the cell under it has the bit (UpdateHFieldDarkeningNormals),
// so across the edge the light fades over one triangle, about one cell wide. The height field's own ground is darkened per
// cell (CHField::Execute halves the colour of all four corners of a cell with the bit), so its edge is a hard step. In Theme
// Park World's worlds the park's hoarding stands on that step and hides it; the mod's worlds have no hoarding (TPW_MOD_HOOK
// tpi_worlds in LLCCM_CreateHoardingMesh), so the step showed as a sharp line, unlike the soft fade along the river beside
// it. In the mod's worlds the ground takes the meshes' rule: each corner is darkened from the cell under its grid point, the
// cell it is the first corner of (the last row's and column's points, which are no cell's first, take the cell before),
// as UpdateHFieldDarkeningNormals picks the cell under a vertex, and the colours between the corners are blended as the
// meshes' are, so the ground fades over one cell, beside the river's fade. Header only and pure, so the host test sees
// what the game does.
#pragma once


// TPW_PORT_FIX mods TpiParkEdgeCorners | Mac: no such function; a height field cell with quad bit 0x400 has all four corners darkened | Port: in the mod's worlds (TPW_MOD_HOOK tpi_worlds, CHField::Execute) each corner is darkened from the cell under its grid point, as the meshes' vertices are | Why: tpw-mods tpi-worlds; without the hoarding the edge of the park's land was a hard line on the ground

#include <cstdint>

namespace tpw {

// The quad bit of land the park does not own (RecalculateParkDarkening).
inline constexpr uint16_t kQuadUnowned = 0x400;

// The corners of cell (cell, row) of a height field `width` x `height` cells to darken, bit k for corner k in
// CHField::Execute's order: (cell, row), (cell, row + 1), (cell + 1, row + 1), (cell + 1, row). A corner is darkened when
// the cell under its grid point has kQuadUnowned in `quads[i].mF00` (row-major, `width` a row), the grid point's own cell,
// or the one before it on the last row or column. `quads` is the height field's quad array (sHFieldQuad in the game).
template <typename Quad>
inline uint32_t TpiParkEdgeCorners(const Quad* quads, uint32_t width, uint32_t height, uint32_t cell, uint32_t row) {
    if (quads == nullptr || width == 0 || height == 0) return 0;
    const uint32_t points[4][2] = {{cell, row}, {cell, row + 1}, {cell + 1, row + 1}, {cell + 1, row}};
    uint32_t mask = 0;
    for (uint32_t k = 0; k < 4; ++k) {
        const uint32_t x = points[k][0] < width ? points[k][0] : width - 1;
        const uint32_t z = points[k][1] < height ? points[k][1] : height - 1;
        if ((quads[x + z * width].mF00 & kQuadUnowned) != 0) mask |= 1u << k;
    }
    return mask;
}

}  // namespace tpw

