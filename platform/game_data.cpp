// game_data.cpp: data the translated code reads that nothing in src defines, given the values the Mac started with.
// Without these the launcher's generated stand-ins define them as zeroes. The scalars are the initialised words of the
// main program's data section at the addresses their headers give; MathLib::pi was a data import of the system's MathLib;
// gLbMathsAtanScale is the bullfrog library's storage that __sinit_LbMaths_cpp fills (launcher/static_init.cpp runs it).
// The host's LbMaths_ArcTan import forwards to the translated library function, as the Mac's cross-fragment glue
// 0x1023b1b8 did. Nothing here is a translation of game code.
#include <cstdint>

#include "AudioPlaceHolder.h"

uint32_t gCdequeMinCap = 1;      // 0x102b1420
int32_t gFlyMinHeight = 0x2ee0;  // 0x1029d2c0
uint32_t gLCFontIndex = 7;       // 0x102aed60
int32_t gStaffRoomMsg406 = 1;    // 0x102aed68

// 0x102b56c0, gColList of BMP_InsideArea.h: the bump world is 0x3c bytes on the Mac (src/game/rides/BumperRide.cpp's view;
// BMP_Load takes link nodes from its free list at 0x34) where the header's tabCOLLIST covers 0x20, the size a generated stand-in has.
#ifdef _WIN32
alignas(16) unsigned char gColListStorage[0x40] __asm__("gColList");   // COFF on x86-64 puts no underscore before a name
#else
alignas(16) unsigned char gColListStorage[0x40] __asm__("_gColList");
#endif

double gMathLibPi = 3.141592653589793;  // 0x1007561c, MathLib::pi
extern "C" double gLbMathsAtanScale;
double gLbMathsAtanScale = 0.0;  // 0x10074810, set by __sinit_LbMaths_cpp

int32_t LbMaths_ArcTan(int32_t x, int32_t y) { return bfl::maths::LbMaths_ArcTan(x, y); }   // 0x1023b1b8 (glue import)
