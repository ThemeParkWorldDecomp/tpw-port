// pending_manual.cpp: definitions the stub generator cannot write by itself (see
// gen_stubs.py): variables whose type it cannot zero-initialise, and the key functions of
// classes whose vtables are needed before the class is translated (a vtable is emitted
// with the class's first non-inline virtual function, so that one has to be C++).
// Each entry names where the real definition will come from; delete the entry when it
// lands (the duplicate-symbol link error says which).
#include "CStaticBuffer.h"
#include "Debug.h"
#include "TPEngine.h"
#include "TPCoaster.h"
#include "TPCamera.h"
#include "ExitErr.h"
#include "FS_Online.h"
#include "InterfaceWindow.h"
#include "LevelInit.h"
#include "Light.h"
#include "TbAudioSystem.h"
#include "AudioPlaceHolder.h"
#include "TbGraphicFile.h"
#include "AdvisorQueue.h"
#include "ThingReceivers.h"
#include "World.h"
#include "WorldSavvy.h"
#include "analyser.h"
#include "bfl_FileSystem.h"
#include "bfl_WadFile.h"
#include "lobby.h"
#include "sprite_engine.h"
#include "stubs.h"
#include "ui_DrawObject.h"
#include "TbVTag.h"
#include "platform/msl.h"
#include "platform/online_slots.h"
#include "ui_controls.h"
#include "ui_ime.h"
#include "ui_textmanager.h"
#include "ui_ErrorDisplay.h"
#include "keyboard.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unordered_set>
#include <vector>
#include "Wavelet.h"
#include "tb_particle.h"
#include "tb_printf_opengl.h"
#include "tb_hfield.h"
#include "tb_mesh.h"
#include "tb_rain.h"
#include "tb_texturecache.h"
#include "tb_starfield.h"
#include "tb_resource_display.h"

namespace {
// Reports a pending function like a generated stand-in does (once per call site).
void Pending(const char* name, bool& reported) {
    tpw_stub_called(name, false, !reported);
    reported = true;
}
}  // namespace

#define TPW_PENDING(name)            \
    do {                             \
        static bool reported = false; \
        Pending(name, reported);     \
    } while (0)

// The symbol a C name has in an object file.
#ifdef __APPLE__
#define TPW_SYMBOL(name) "_" name
#else
#define TPW_SYMBOL(name) name
#endif

// GameSys.cpp range, initialised data 0x102a460c: a reference to the world pointer.
CWorld*& gGameWorldRef = gGameWorld;

// bullfrog: the library's global palette (TbPalette is still an incomplete type). Zeroed
// storage of a generous size under the variable's symbol.
extern unsigned char gPendingGlobalPalette[0x1000] __asm__(TPW_SYMBOL("_lbGlobalPalette"));
alignas(16) unsigned char gPendingGlobalPalette[0x1000] = {};

// lobby.cpp and its neighbours: the island/globe screens are untranslated. They are classes
// with virtual functions that register with their owner as they are made, so their
// constructors and virtual members are C++ here (the translated base classes do the registering).
#define TPW_PENDING_SYSTEM(Class)                                                                  \
    Class::~Class() { TPW_PENDING(#Class "::~" #Class "()"); }                                     \
    void Class::Init() { TPW_PENDING(#Class "::Init()"); }                                         \
    void Class::Tick() { TPW_PENDING(#Class "::Tick()"); }                                         \
    void Class::Render() { TPW_PENDING(#Class "::Render()"); }                                     \
    void Class::Close() { TPW_PENDING(#Class "::Close()"); }                                       \
    void Class::WindowMessagesFn(int32_t, int32_t, int32_t) { TPW_PENDING(#Class "::WindowMessagesFn(long, long, long)"); }
// FS_OnlineList.cpp (unit w68): the online system is built for real now (src/game/online/FS_Online.cpp); the list family's and the park's
// virtual destructors are the classes' key functions, untranslated (0x1016fd48, 0x10170834, 0x101709b0, 0x10170b34, 0x10170ca8,
// 0x10170e20, 0x10179d28, 0x1017a908, 0x1017b4a4, 0x1017c464), so their vtables need a definition here; nothing destroys the lists in
// the launcher before the game closes (gOnlineSystem is deleted at shutdown, and CFSOnlineSystem::~CFSOnlineSystem is itself pending).
// 2026-10-20 (lead, v5L01): CFSOnlineSystemParkList's (0x1016fd48) and CFSOnlinePark's destructors are translated now (src/
// FS_Online_gametype_CFSOnlineSystemParkList_Dtor_CFSOnlineSystemPark.cpp, src/game/online/FS_OnlineList.cpp); their stand-ins went.
// Lead 2026-10-20 (v51065): CFSOnlineSystemCityList's destructor is translated in src/FS_Online_CFSOnlineSystemParkList_InWorldByPlayerName_
// Dtor_CFSOnlineSystemParkList_InCity_GetList_CFSOnlineSystemCityList_StoreReceivedList_CFSOnlineSystemCityList_ReturnCityInfo.cpp.
// Worker v5L17: CFSOnlineSystemParkList_InWorldByParkID's and CFSOnlineSystemParkList_InCityTop10's destructors and the city instance
// of the base's are translated in src/game/online/FS_Online.cpp; their stand-ins went.
// Lead 2026-10-20 (v51129): CFSOnlineSystemThemeList's destructor is translated in src/game/online/FS_OnlineList.cpp, src/game/MslSupport.cpp, src/game/ui/InterfaceDrawEngine.cpp.
// Lead 2026-10-20 (v51110): CFSOnlineSystemServerList's and CFSOnlineSystemRideList's destructors are translated in
// src/game/online/FS_OnlineList.cpp, src/game/online/FS_Online.cpp, src/game/ui/ui_main.cpp.

// The screens. A screen's Show and Hide stand-ins keep the base class's "visible" word, so
// that the screen system ticks the screens the lobby opened.
#define TPW_PENDING_VOID(Class, Member) \
    void Class::Member() { TPW_PENDING(#Class "::" #Member "()"); }
#define TPW_PENDING_CANCEL(Class)                                                             \
    uint32_t Class::ProcessCancel() {                                                         \
        TPW_PENDING(#Class "::ProcessCancel()");                                              \
        return 1;   /* "dealt with": 0 would open the main menu, which is not there yet */  \
    }
#define TPW_PENDING_SCREEN_INPUT(Class)                                                                                     \
    void Class::WindowMessagesFn(int32_t, int32_t, int32_t) { TPW_PENDING(#Class "::WindowMessagesFn(long, long, long)"); } \
    void Class::Show() {                                                                                                    \
        TPW_PENDING(#Class "::Show()");                                                                                     \
        mVisible = 1;                                                                                                       \
    }                                                                                                                       \
    void Class::Hide() {                                                                                                    \
        TPW_PENDING(#Class "::Hide()");                                                                                     \
        mVisible = 0;                                                                                                       \
    }
// The island screens: the base class, the theme islands and the islands themselves are translated
// (src/game/online/lobby_island.cpp, src/game/online/LBFlyingMesh.cpp, src/game/online/lobby_parser.cpp); the online islands
// are not. (The online globe's constructor and the download and publish globes' Init are translated: src/game/online/lobby_globe.cpp, unit w62.)
// 2026-10-20 (lead, v5L01): the online islands' Tick, Show and ProcessCancel and the download globe's destructor and Hide are
// translated (src/CLBScreenComponentOnlineIslands_GotoNextIsland*.cpp, src/lobby_globe_CLBScreenComponentDownloadGlobe_Dtor_*.cpp);
// the stand-ins of those five went, and the rest of each screen's stand-ins stay.
// Lead 2026-10-20 (v51106): the online islands' destructor, WindowMessagesFn and Hide and the download globe's Tick are translated in
// src/game/online/lobby_globe.cpp, src/game/online/lobby_island.cpp.
// Lead 2026-10-20 (v51031): its WindowMessagesFn is translated in src/game/online/lobby_globe.cpp.
// Lead 2026-10-20 (v51046): its Show and ProcessCancel are translated in
// src/game/HLSprite.cpp, src/game/online/lobby_parser.cpp, src/game/online/lobby_island.cpp, src/game/online/lobby_globe.cpp, src/game/ui/intrface.cpp.
// Lead 2026-10-20 (v51106): its destructor and Tick are translated in the same file.
// Lead 2026-10-20 (v5957): its WindowMessagesFn, Show and Hide are translated in src/game/online/lobby_globe.cpp

// The interface's controls (src/game/ui/InterfaceBar.cpp, src/game/ui/InterfaceButton.cpp, src/game/ui/InterfaceEditBox.cpp, src/game/ui/InterfaceImage.cpp, src/game/ui/InterfaceKnob.cpp, src/game/ui/InterfaceLightPanel.cpp, src/game/ui/InterfaceListBox.cpp, src/game/ui/InterfaceListControl2.cpp, src/game/ui/InterfaceTextBox.cpp, src/game/ui/InterfaceVaryBox.cpp, src/game/ui/InterfaceWindow.cpp, unit w31b): each constructor stores its class's vtable, which is emitted
// where the class's first virtual member is defined, so these are the key functions (the first override in the Mac's vtable
// order: DrawFunction for the bar, edit box, list box and text box, the destructor for the check buttons, image, knob, light panel
// and vary box, CalcClientArea for the list control). The four DrawFunctions are empty functions in the original (4 bytes);
// the rest of each vtable is generated stand-ins. The button's key function, its destructor, is translated (src/game/ui/InterfaceButton.cpp),
// and so are the image's (src/game/ui/InterfaceImage.cpp, unit w75) and the check buttons' (src/game/ui/InterfaceButton.cpp, unit w91).
// Lead 2026-10-20 (v51115): InterfaceBar::DrawFunction is translated in src/game/ui/InterfaceDrawEngine.cpp.
// Lead 2026-10-20 (v51111): InterfaceTextBox::DrawFunction and the knob's, light panel's and vary box's destructors are
// translated in src/game/ui/InterfaceVaryBox.cpp, src/game/ui/ui_upgradeStatus.cpp, src/game/ui/InterfaceKnob.cpp, src/game/ui/InterfaceLightPanel.cpp, src/game/ui/InterfaceWindow.cpp, src/game/ui/InterfaceBar.cpp, src/game/ui/InterfaceImage.cpp, src/game/ui/InterfaceTextBox.cpp, src/game/ui/UI_Base.cpp, src/game/ui/ui_progressBar.cpp, src/game/ui/ui_yearEndPanel.cpp.

// interim: the engine imports Random from the C library (0x1004c5e8 is its glue stub);
// the Mac's generator is not in the dump; 15-bit values like rand().
int32_t Random() {
    static uint32_t state = 1;
    state = state * 1103515245u + 12345u;
    return static_cast<int32_t>((state >> 16) & 0x7fff);
}

// Lead 2026-10-20 (v51112): bfl::maths::Random and RangedRandomSlow are translated in
// src/sound/PlaceHolderSentence.cpp, src/sound/AudioPlaceHolder.cpp, src/sound/TbSoundCodecLoop.cpp, src/sound/TbSoundSampleCallback.cpp, src/sound/TbSoundSampleMPEG2.cpp, src/sound/TbSoundSampleWAV.cpp, src/sound/TbSoundSamplePipe.cpp.

// IME::MslWideString (include/ui_ime.h; unit w92): the string library's constructor and destructor.
// interim: the MSL std::basic_string<wchar_t> instantiations 0x101e0b84 (allocates a 16-byte representation: capacity 3, refcount 1,
// buffer) and 0x101468f8 are not translated; gUIIME's static construction and destruction reach them (CIME::CIME builds three
// strings, CIME::~CIME destroys them). An empty pointer and a destructor that frees nothing keep that quiet. Delete this entry when a
// string unit lands (the duplicate-symbol link error says which).
IME::MslWideString::MslWideString() : mData(nullptr) {}
IME::MslWideString::~MslWideString() {}

namespace {
// The LCStrings alive (made by the constructor below), for online_callee::WStringDtor.
std::unordered_set<LCString*>& LiveLCStrings() {
    static std::unordered_set<LCString*> live;
    return live;
}
}  // namespace

// LCString (include/ui_controls.h; unit w93): the string library's constructor from wide text (0x10146a14) and assign (0x101b2510),
// the two std::basic_string<wchar_t> instantiations the list control's AddItem and ChangeItem build and change their cells with.
// interim: the MSL string library is not translated. The representation is the Mac's four words (length, capacity, count of users,
// pointer to the characters), which is what UpdateDisplay reads the text through; the characters are a copy of their own. Nothing is
// shared here (the destructor, 0x101468f8, is online_callee::WStringDtor below). Delete these two when a
// string unit lands (the duplicate-symbol link error says which).
// Lead 2026-10-20 (v51129): assign is translated in src/game/MslSupport.cpp (through the translated
// change_size, which keeps a representation of one user in place, as this constructor makes it); the constructor stays.
LCString::LCString(const char16_t* text) {
    size_t length = 0;
    while (text[length] != 0) ++length;
    mRep = new LCStringRep{static_cast<int32_t>(length), static_cast<int32_t>(length), 1, new char16_t[length + 1]};
    std::memcpy(mRep->mData, text, (length + 1) * sizeof(char16_t));
    LiveLCStrings().insert(this);
}

// online_callee::WStringDtor, the destructor 0x101468f8 (the list control's RemoveMe calls it with the flag 1 on each cell). The
// Mac gives up the share (a count of 0 is taken as 1); the last owner frees the characters and the representation, and a flag above
// 0 frees the string itself. Only the LCStrings made above are taken, and the online client's 32-bit slots
// (include/platform/online_slots.h), which tpw::slots::Release gives up; any other address is left alone, as the stand-in did.
// Delete this when a string unit lands (the duplicate-symbol link error says which).
namespace online_callee {
void WStringDtor(void* string, int16_t flag) {
    auto& live = LiveLCStrings();
    auto found = live.find(static_cast<LCString*>(string));
    if (found == live.end()) {
        tpw::slots::Release(string);
        return;
    }
    LCString* self = *found;
    LCStringRep* rep = self->mRep;
    if (rep != nullptr) {
        if (rep->mRefCount == 0) rep->mRefCount = 1;
        if (--rep->mRefCount == 0) {
            IME::MslDelete(rep->mData);
            IME::MslDelete(rep);
        }
        self->mRep = nullptr;
    }
    if (flag > 0) {
        live.erase(found);
        delete self;
    }
}
}  // namespace online_callee

// 2026-10-14: w122a and w122b replaced the ten singleton thing constructor interims.

// 2026-10-20 (lead, v5L01): the INTERIM w125 TbIntegerVTag::Format went; src/ltms/TbVTag.cpp translates it.
