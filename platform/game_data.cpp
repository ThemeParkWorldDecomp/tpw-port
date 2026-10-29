// game_data.cpp: data the translated code reads that nothing in src defines, given the values the Mac started with.
// Without these the launcher's generated stand-ins define them as zeroes. The scalars are the initialised words of the
// main program's data section at the addresses their headers give; MathLib::pi was a data import of the system's MathLib;
// gLbMathsAtanScale is the bullfrog library's storage that __sinit_LbMaths_cpp fills (launcher/static_init.cpp runs it).
// The host's LbMaths_ArcTan import forwards to the translated library function, as the Mac's cross-fragment glue
// 0x1023b1b8 did. Nothing here is a translation of game code.
#include <cstdint>

#include "AudioPlaceHolder.h"
#include "BMP_InsideArea.h"

uint32_t gCdequeMinCap = 1;      // 0x102b1420
int32_t gFlyMinHeight = 0x2ee0;  // 0x1029d2c0
uint32_t gLCFontIndex = 7;       // 0x102aed60
int32_t gStaffRoomMsg406 = 1;    // 0x102aed68

tabCOLLIST gColList;   // 0x102b56c0, the bump world's 0x3c bytes (BMP_InsideArea.h), zero at start like the Mac's

double gMathLibPi = 3.141592653589793;  // 0x1007561c, MathLib::pi
extern "C" double gLbMathsAtanScale;
double gLbMathsAtanScale = 0.0;  // 0x10074810, set by __sinit_LbMaths_cpp

int32_t LbMaths_ArcTan(int32_t x, int32_t y) { return bfl::maths::LbMaths_ArcTan(x, y); }   // 0x1023b1b8 (glue import)
