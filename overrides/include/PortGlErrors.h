// PortGlErrors.h: OpenGL errors left by earlier calls taken before a texture is bound, a switch for tpw-port #125
// (COpenGLTexture::Select). Select binds the texture and then reads glGetError, which answers the oldest error of any
// call since the last read, not only the bind's: one left by an earlier call turns texturing off (glDisable), and the
// draws after it, with that texture and the next ones until an untextured draw (COpenGLDrawPrimitive::SetTexture), come
// out in their bare vertex colours, on the park's ground the light's, near white. A driver that answers an error where the Mac's
// answers none would then draw bright patches that come and go, which fits tpw-port #125 (NVIDIA on Windows 10), but no
// Mac shows it, so the switch is off unless the player sets it (launcher.ini's gl_error_drain=1 or TPW_GL_ERROR_DRAIN=1,
// read once at the start); on, the pending errors are taken first and the first kLogged of them written to the launcher
// log, so the log tells whether a driver answers any; off, the Mac's. Header only and pure, so the host test
// (tests/host_gl_errors.cpp) sees what the game runs.
#pragma once


// TPW_PORT_FIX glerrors PortGlErrors | Mac: no such header | Port: the switch for taking the OpenGL errors earlier calls left before a texture is bound, and the log of the first of them | Why: tpw-port #125, bright rectangles flickering on the ground around rides and shops on NVIDIA cards

#include <cstdint>

namespace tpw::glerrors {

// Whether Select takes the pending errors first. Set once by the launcher at the start of a game.
inline bool drain = false;

// How many errors the launcher log is given, and the most taken before one bind (a lost context can answer an error
// for ever).
inline constexpr int32_t kLogged = 8;
inline constexpr int32_t kMostPerBind = 16;

// The launcher log's line for an error taken (the error, and how many have been taken so far); NULL, none.
inline void (*log)(uint32_t error, int32_t taken) = nullptr;
inline int32_t taken = 0;

// Takes the errors pending from `getError` (glGetError), at most kMostPerBind; answers how many.
template<class GetError>
inline int32_t Drain(GetError getError) {
    int32_t count = 0;
    while (count < kMostPerBind) {
        const uint32_t error = static_cast<uint32_t>(getError());
        if (error == 0) break;
        ++count;
        ++taken;
        if (log != nullptr && taken <= kLogged) log(error, taken);
    }
    return count;
}

}  // namespace tpw::glerrors

