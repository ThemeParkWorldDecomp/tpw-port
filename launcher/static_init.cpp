// static_init.cpp: the main program's static initialisers, run by the launcher before the game's main.
//
// The Mac's runtime (__start) ran every translation unit's __sinit_X_cpp before main, in link order, which is ascending code
// address. The translation gives each one a plain named function and nothing on the host calls it (the unit tests call theirs
// through tpw_sinit_run), so this table does: the 123 of the main program "SimTheme Park" that analysis/function-inventory.csv
// lists, in its address order. Each entry is one of two kinds:
//   - translated in src: called;
//   - "host statics": the file's objects are host globals with C++ initialisers (or constant initialisers) that the host's own
//     runtime has already run before main, so there is nothing to call; the entry keeps the place and names the file.
// Every initialiser of the main program is translated now, so none is left to the launcher's generated stand-ins.
// A translation that turns a "host statics" entry into a function must make its objects plain storage at the same time, or they
// are constructed twice. __sinit_HLSprite_cpp is translated but left out for that reason: gHLSpriteLists, gESPGlobalList and
// gESPFileInfo are host C++ globals already, and running it again cleared the lists (the weak __construct_array zeroes the array
// rather than constructing it), so ESP_InitialiseAndLoadSprites crashed on a NULL vtable in the lobby; the second CSpriteList
// constructor also registers gESPGlobalList twice in gLinkBases. __sinit_ui_ErrorDisplay_cpp is left out too: gUIErrorQueue is a
// host std::deque and gUIErrorMutex a host bthread_mutex, and its Mac deque copy constructor wrote the Mac's seven deque words
// over the host deque, whose destructor then freed a pointer it never allocated at exit (SIGABRT, exit 134, after
// 1100 passes). The others rebuild host objects too (strings, gUILight, ...),
// harmlessly: no constructor of theirs registers anything, and the arrays __construct_array zeroes are plain storage.
// __sinit_FS_MasterSave_cpp is translated but left out as well: it builds a TbDynamicStringTemplate<char16_t> in place in
// gMasterSaveString, which the launcher's generated data stand-in sizes as one byte, so the constructor wrote over the
// globals after it, gMessageCentre among them (CStaffHQ's Subscribe crashed at level setup, ~CMessageCentre at exit; v5L11).
// Nothing reads the string on the host.
// The 67 initialisers of the shared libraries (engine, sound, bullfrog, ...) belong to the libraries' load and are not here,
// except the ones in kLibraryInitialisers, which fill plain data the game reads and run first, as the libraries loaded before
// the program: __sinit_LbMaths_cpp sets LbMaths_ArcTan's angle scale (zero without it, every angle is a quadrant constant),
// and __sinit_AudioPlaceHolder_cpp the sounds' random seed (0 without it, so every launch made the same picks of sample,
// volume, pitch and gap; its clock is platform/sound_glue.cpp's).
// The destructors they register through __register_global_object are not recorded (its weak body in src records nothing), so
// none runs at exit.
#include "static_init.h"

#include <cstdint>

#include "platform.h"

void __sinit_3DFont_cpp();
void __sinit_Advisor_cpp();
void __sinit_AdvisorScoring_cpp();
void __sinit_HoardingsBalance_cpp();
void __sinit_TPCoaster_cpp();
void __sinit_TPRide_cpp();
void __sinit_Debug_cpp();
void __sinit_intrface_cpp();
void __sinit_LLCellMouse_cpp();
void __sinit_LLCellRide_cpp();
void __sinit_lobby_cpp();
void __sinit_lobby_globe_cpp();
void __sinit_lobby_parser_cpp();
void __sinit_lobby_screen_cpp();
void __sinit_BlockSave_cpp();
void __sinit_GoldenTickets_cpp();
void __sinit_guard_cpp();
void __sinit_MechanicHQ_cpp();
void __sinit_Peep_cpp();
void __sinit_Person_cpp();
void __sinit_PersonQueue_cpp();
void __sinit_ResearchLab_cpp();
void __sinit_StaffHQ_cpp();
void __sinit_Thing_cpp();
void __sinit_TrainingCentre_cpp();
void __sinit_vector2d_cpp();
void __sinit_VehicleCallbacks_cpp();
void __sinit_World_cpp();
void __sinit_ui_visitorStatus_cpp();
void __sinit_ActionRecorder_cpp();
void __sinit_InGame_cpp();
void __sinit_ExitErr_cpp();
void __sinit_keyboard_cpp();
void __sinit_RideDB_cpp();
void __sinit_FS_MasterSave_cpp();
void sinit_FS_Online_cpp();
void sinit_FS_OnlineList_cpp();
void __sinit_ui_main_cpp();
void __sinit_ui_utils_cpp();
void __sinit_ui_textmanager_cpp();
void __sinit_ui_StatusPanel_cpp();
void __sinit_ui_allKidsPanel_cpp();
void __sinit_ui_allStaffPanel_cpp();
void __sinit_ui_bogStatus_cpp();
void __sinit_ui_entryPricePanel_cpp();
void __sinit_ui_FeatureStatus_cpp();
void __sinit_ui_FinancePanel_cpp();
void __sinit_ui_mainpanel2_cpp();
void __sinit_ui_parkStatus_cpp();
void __sinit_ui_playerLogin_cpp();
void __sinit_ui_Postcard_cpp();
void __sinit_ui_ridepanel_cpp();
void __sinit_ui_RideStatus_cpp();
void __sinit_ui_shopStatus_cpp();
void __sinit_ui_StaffCostsPanel_cpp();
void __sinit_ui_staffPanel_cpp();
void __sinit_ui_showStatus_cpp();
void __sinit_ui_staffRoomStatus_cpp();
void __sinit_ui_staffStatus_cpp();
void __sinit_ui_upgradeStatus_cpp();
void __sinit_UI_Base_cpp();
void __sinit_ui_cityLobbyController_cpp();
void __sinit_ui_islandLobbyController_cpp();
void __sinit_ui_onlineCityInfoOverlay_cpp();
void __sinit_ui_onlineLogin_cpp();
void __sinit_ui_onlineNewsPanel_cpp();
void __sinit_ui_onlineOutbox_cpp();
void __sinit_ui_onlineParkFindPanel_cpp();
void __sinit_ui_onlineParkInfoPanel_cpp();
void __sinit_ui_onlineParkListPanel_cpp();
void __sinit_ui_publishParkPanel_cpp();
void __sinit_ui_IME_cpp();
void __sinit_ui_mainmenu_cpp();
void __sinit_FS_LoadSave_cpp();
void __sinit_TPEmbed_cpp();
void __sinit_email_cpp();
void __sinit_OnlinePerson_cpp();
void __sinit_online_cpp();
void __sinit_ui_worldLobbyController_cpp();
extern "C" void __sinit_ui_2dmap_cpp();
void __sinit_TPGlobals_cpp();
void __sinit_LbMaths_cpp();   // bullfrog library
void __sinit_AudioPlaceHolder_cpp();   // sound library

namespace {
struct StaticInitialiser {
    uint32_t address;   // the Mac's code address: the order __start ran them in
    void (*function)();
    const char* name;
};

const StaticInitialiser kLibraryInitialisers[] = {
    {0x1002971c, __sinit_LbMaths_cpp, "__sinit_LbMaths_cpp"},  // bullfrog library
    {0x1001bae4, __sinit_AudioPlaceHolder_cpp, "__sinit_AudioPlaceHolder_cpp"},  // sound library
};

const StaticInitialiser kStaticInitialisers[] = {
    {0x10003220, nullptr, "__sinit_TPMain_cpp"},  // host statics, src/game/TPMain.cpp
    {0x10003d28, nullptr, "__sinit_FS_Registry_cpp"},  // host statics, src/game/save/FS_Registry.cpp
    {0x100051e0, nullptr, "__sinit_FS_System_cpp"},  // host statics, src/game/save/FS_System.cpp
    {0x1000757c, __sinit_3DFont_cpp, "__sinit_3DFont_cpp"},
    {0x1000fff0, __sinit_Advisor_cpp, "__sinit_Advisor_cpp"},
    {0x1001c34c, __sinit_AdvisorScoring_cpp, "__sinit_AdvisorScoring_cpp"},
    {0x10021074, __sinit_HoardingsBalance_cpp, "__sinit_HoardingsBalance_cpp"},
    {0x10021258, nullptr, "__sinit_MainBalance_cpp"},  // host statics, src/game/balance/MainBalance.cpp
    {0x100213fc, nullptr, "__sinit_OnlineBalance_cpp"},  // host statics, src/game/balance/OnlineBalance.cpp
    {0x100215a0, nullptr, "__sinit_OptionsBalance_cpp"},  // host statics, src/game/balance/OptionsBalance.cpp
    {0x10021750, nullptr, "__sinit_SoundBalance_cpp"},  // host statics, src/game/balance/SoundBalance.cpp
    {0x1003228c, nullptr, "__sinit_TPAdvisor_cpp"},  // host statics, src/game/advisor/TPAdvisor.cpp, src/game/TPCamera.cpp
    {0x10036518, nullptr, "__sinit_TPCamera_cpp"},  // host statics, src/game/TPCamera.cpp
    {0x100459f4, __sinit_TPCoaster_cpp, "__sinit_TPCoaster_cpp"},
    {0x100599c8, nullptr, "__sinit_TPConfig_cpp"},  // host statics, src/game/TPConfig.cpp
    {0x1006c134, __sinit_TPRide_cpp, "__sinit_TPRide_cpp"},
    {0x1006fd80, __sinit_Debug_cpp, "__sinit_Debug_cpp"},
    {0x1007d53c, nullptr, "__sinit_HLSprite_cpp"},  // host statics, src/game/HLSprite.cpp (see the note above)
    {0x1007d8ac, nullptr, "__sinit_Colours_cpp"},  // host statics, src/game/ui/Colours.cpp
    {0x1007de28, __sinit_intrface_cpp, "__sinit_intrface_cpp"},
    {0x100815c8, nullptr, "__sinit_Mouse_cpp"},  // host statics, src/game/Mouse.cpp
    {0x10088ff4, __sinit_LLCellMouse_cpp, "__sinit_LLCellMouse_cpp"},
    {0x10090b2c, __sinit_LLCellRide_cpp, "__sinit_LLCellRide_cpp"},
    {0x10096f04, nullptr, "__sinit_LLCellSystem_cpp"},  // host statics, src/game/park/LLCellSystem.cpp
    {0x100a6be0, __sinit_lobby_cpp, "__sinit_lobby_cpp"},
    {0x100afe1c, __sinit_lobby_globe_cpp, "__sinit_lobby_globe_cpp"},
    {0x100b54f8, nullptr, "__sinit_lobby_island_cpp"},  // host statics, src/game/online/lobby_island.cpp, src/game/online/lobby_parser.cpp, src/game/online/LBFlyingMesh.cpp
    {0x100b6ae4, __sinit_lobby_parser_cpp, "__sinit_lobby_parser_cpp"},
    {0x100b7c9c, __sinit_lobby_screen_cpp, "__sinit_lobby_screen_cpp"},
    {0x100d85f0, nullptr, "__sinit_rsse_cpp"},  // host statics, src/game/rides/rsse.cpp
    {0x100f1fb0, nullptr, "__sinit_analyser_cpp"},  // host statics, src/game/park/analyser.cpp
    {0x100f29b0, nullptr, "__sinit_Arrival_cpp"},  // host statics, src/game/people/Arrival.cpp
    {0x100f4754, __sinit_BlockSave_cpp, "__sinit_BlockSave_cpp"},
    {0x100fbc84, nullptr, "__sinit_Entertainer_cpp"},  // host statics, src/game/people/Entertainer.cpp
    {0x100fda00, __sinit_GoldenTickets_cpp, "__sinit_GoldenTickets_cpp"},
    {0x100fef90, __sinit_guard_cpp, "__sinit_guard_cpp"},
    {0x1010084c, nullptr, "__sinit_Handyman_cpp"},  // host statics, src/game/people/Handyman.cpp
    {0x10103ec0, nullptr, "__sinit_MapDelta_cpp"},  // host statics, src/game/park/MapDelta.cpp
    {0x101051d8, nullptr, "__sinit_mechanic_cpp"},  // host statics, src/game/people/mechanic.cpp
    {0x10105d2c, __sinit_MechanicHQ_cpp, "__sinit_MechanicHQ_cpp"},
    {0x10111e94, nullptr, "__sinit_Object_cpp"},  // host statics, src/game/Object.cpp
    {0x10116418, __sinit_Peep_cpp, "__sinit_Peep_cpp"},
    {0x1011ba64, __sinit_Person_cpp, "__sinit_Person_cpp"},
    {0x10120bc8, __sinit_PersonQueue_cpp, "__sinit_PersonQueue_cpp"},
    {0x10121a88, nullptr, "__sinit_Researcher_cpp"},  // host statics, src/game/people/Researcher.cpp
    {0x101247c4, __sinit_ResearchLab_cpp, "__sinit_ResearchLab_cpp"},
    {0x10128350, nullptr, "__sinit_staff_cpp"},  // host statics, src/game/people/staff.cpp
    {0x1012a7b8, nullptr, "__sinit_StaffDB_cpp"},  // host statics, src/game/people/StaffDB.cpp, src/game/people/StaffInfo.cpp
    {0x1012c538, __sinit_StaffHQ_cpp, "__sinit_StaffHQ_cpp"},
    {0x1012ec48, __sinit_Thing_cpp, "__sinit_Thing_cpp"},
    {0x10130564, __sinit_TrainingCentre_cpp, "__sinit_TrainingCentre_cpp"},
    {0x10130e70, __sinit_vector2d_cpp, "__sinit_vector2d_cpp"},
    {0x10134f5c, nullptr, "__sinit_vehicle_cpp"},  // host statics, src/game/rides/vehicle.cpp
    {0x10136060, __sinit_VehicleCallbacks_cpp, "__sinit_VehicleCallbacks_cpp"},
    {0x10141c14, __sinit_World_cpp, "__sinit_World_cpp"},
    {0x101435c4, __sinit_ui_visitorStatus_cpp, "__sinit_ui_visitorStatus_cpp"},
    {0x10146e9c, __sinit_ActionRecorder_cpp, "__sinit_ActionRecorder_cpp"},
    {0x101476dc, nullptr, "__sinit_Clock_cpp"},  // host statics, src/game/park/Clock.cpp
    {0x1014b118, nullptr, "__sinit_Game_cpp"},  // host statics, src/game/TPGlobals.cpp
    {0x1014b5cc, __sinit_InGame_cpp, "__sinit_InGame_cpp"},
    {0x1014bf38, __sinit_ExitErr_cpp, "__sinit_ExitErr_cpp"},
    {0x101512fc, __sinit_keyboard_cpp, "__sinit_keyboard_cpp"},
    {0x10151834, nullptr, "__sinit_Main_Cpp"},  // host statics, src/game/Main.cpp
    {0x10157010, __sinit_RideDB_cpp, "__sinit_RideDB_cpp"},
    {0x1015b484, nullptr, "__sinit_SaveLoad_cpp"},  // host statics, src/game/save/SaveLoad.cpp, src/game/park/SkyObjects.cpp
    {0x1015ea80, nullptr, "__sinit_Text_cpp"},  // host statics, src/game/Text.cpp
    {0x1015f40c, nullptr, "__sinit_TPOptions_cpp"},  // host statics, src/game/TPOptions.cpp
    {0x101683d0, nullptr, "__sinit_TPSettings_cpp"},  // host statics, src/game/TPResource.cpp, src/game/TPSettings.cpp
    {0x10168830, nullptr, "__sinit_VanillaTime_cpp"},  // host statics, src/game/VanillaTime.cpp
    {0x1016f35c, nullptr, "__sinit_FS_MasterSave_cpp"},  // translated (v51117), left out: see the note above
    {0x10178308, sinit_FS_Online_cpp, "__sinit_FS_Online_cpp"},  // translated as sinit_FS_Online_cpp
    {0x1017ddb4, sinit_FS_OnlineList_cpp, "__sinit_FS_OnlineList_cpp"},  // translated as sinit_FS_OnlineList_cpp
    {0x1017fd58, nullptr, "__sinit_FS_UserInfo_cpp"},  // host statics, src/game/save/FS_UserInfo.cpp
    {0x101877b8, __sinit_ui_main_cpp, "__sinit_ui_main_cpp"},
    {0x1018bda8, __sinit_ui_utils_cpp, "__sinit_ui_utils_cpp"},
    {0x1018c8ec, nullptr, "__sinit_ui_tooltip_cpp"},  // host statics, src/game/ui/ui_tooltip.cpp
    {0x101902e8, __sinit_ui_textmanager_cpp, "__sinit_ui_textmanager_cpp"},
    {0x101946ac, __sinit_ui_StatusPanel_cpp, "__sinit_ui_StatusPanel_cpp"},
    {0x1019592c, __sinit_ui_allKidsPanel_cpp, "__sinit_ui_allKidsPanel_cpp"},
    {0x10199e84, __sinit_ui_allStaffPanel_cpp, "__sinit_ui_allStaffPanel_cpp"},
    {0x1019ad70, __sinit_ui_bogStatus_cpp, "__sinit_ui_bogStatus_cpp"},
    {0x1019e260, __sinit_ui_entryPricePanel_cpp, "__sinit_ui_entryPricePanel_cpp"},
    {0x1019eea0, __sinit_ui_FeatureStatus_cpp, "__sinit_ui_FeatureStatus_cpp"},
    {0x101a0dbc, __sinit_ui_FinancePanel_cpp, "__sinit_ui_FinancePanel_cpp"},
    {0x101a8df4, __sinit_ui_mainpanel2_cpp, "__sinit_ui_mainpanel2_cpp"},
    {0x101adbe4, __sinit_ui_parkStatus_cpp, "__sinit_ui_parkStatus_cpp"},
    {0x101afa90, __sinit_ui_playerLogin_cpp, "__sinit_ui_playerLogin_cpp"},
    {0x101b30c8, __sinit_ui_Postcard_cpp, "__sinit_ui_Postcard_cpp"},
    {0x101b8c8c, __sinit_ui_ridepanel_cpp, "__sinit_ui_ridepanel_cpp"},
    {0x101bc3ec, __sinit_ui_RideStatus_cpp, "__sinit_ui_RideStatus_cpp"},
    {0x101bdfb8, __sinit_ui_shopStatus_cpp, "__sinit_ui_shopStatus_cpp"},
    {0x101bf2dc, __sinit_ui_StaffCostsPanel_cpp, "__sinit_ui_StaffCostsPanel_cpp"},
    {0x101c0cdc, __sinit_ui_staffPanel_cpp, "__sinit_ui_staffPanel_cpp"},
    {0x101c2b10, __sinit_ui_showStatus_cpp, "__sinit_ui_showStatus_cpp"},
    {0x101c40f0, __sinit_ui_staffRoomStatus_cpp, "__sinit_ui_staffRoomStatus_cpp"},
    {0x101c5bec, __sinit_ui_staffStatus_cpp, "__sinit_ui_staffStatus_cpp"},
    {0x101c6738, __sinit_ui_upgradeStatus_cpp, "__sinit_ui_upgradeStatus_cpp"},
    {0x101cac94, __sinit_UI_Base_cpp, "__sinit_UI_Base_cpp"},
    {0x101e0cec, __sinit_ui_cityLobbyController_cpp, "__sinit_ui_cityLobbyController_cpp"},
    {0x101e1e3c, __sinit_ui_islandLobbyController_cpp, "__sinit_ui_islandLobbyController_cpp"},
    {0x101e2450, __sinit_ui_onlineCityInfoOverlay_cpp, "__sinit_ui_onlineCityInfoOverlay_cpp"},
    {0x101e2c78, __sinit_ui_onlineLogin_cpp, "__sinit_ui_onlineLogin_cpp"},
    {0x101e3628, __sinit_ui_onlineNewsPanel_cpp, "__sinit_ui_onlineNewsPanel_cpp"},
    {0x101e551c, __sinit_ui_onlineOutbox_cpp, "__sinit_ui_onlineOutbox_cpp"},
    {0x101ea5d8, __sinit_ui_onlineParkFindPanel_cpp, "__sinit_ui_onlineParkFindPanel_cpp"},
    {0x101ebae0, __sinit_ui_onlineParkInfoPanel_cpp, "__sinit_ui_onlineParkInfoPanel_cpp"},
    {0x101ec90c, __sinit_ui_onlineParkListPanel_cpp, "__sinit_ui_onlineParkListPanel_cpp"},
    {0x101ecfe8, __sinit_ui_publishParkPanel_cpp, "__sinit_ui_publishParkPanel_cpp"},
    {0x101f499c, nullptr, "__sinit_ui_ErrorDisplay_cpp"},  // host statics, src/game/ui/UI_Base.cpp, src/game/ui/InterfaceMessageQueue.cpp, src/game/ui/ui_ErrorDisplay.cpp, src/game/ui/InterfaceWindow.cpp, src/game/ui/ui_Postcard.cpp, src/game/ui/ui_main.cpp, src/game/ui/InterfaceTimer.cpp, src/game/ui/UIInterface.cpp (see the note above)
    {0x101f7c74, __sinit_ui_IME_cpp, "__sinit_ui_IME_cpp"},
    {0x101fa88c, nullptr, "__sinit_ui_iterators_cpp"},  // host statics, include/ui_iterators.h
    {0x101fc260, __sinit_ui_mainmenu_cpp, "__sinit_ui_mainmenu_cpp"},
    {0x101fd668, __sinit_FS_LoadSave_cpp, "__sinit_FS_LoadSave_cpp"},
    {0x10200bf0, __sinit_TPEmbed_cpp, "__sinit_TPEmbed_cpp"},
    {0x1020afc0, __sinit_email_cpp, "__sinit_email_cpp"},
    {0x10213548, __sinit_OnlinePerson_cpp, "__sinit_OnlinePerson_cpp"},
    {0x1022a06c, __sinit_online_cpp, "__sinit_online_cpp"},
    {0x1022bacc, __sinit_ui_worldLobbyController_cpp, "__sinit_ui_worldLobbyController_cpp"},
    {0x102319d4, __sinit_ui_2dmap_cpp, "__sinit_ui_2dmap_cpp"},
    {0x1023331c, __sinit_TPGlobals_cpp, "__sinit_TPGlobals_cpp"},
    {0x102374a0, nullptr, "__sinit_GameSys_cpp"},  // host statics, src/game/GameSys.cpp
    {0x1023779c, nullptr, "__sinit_ResolutionBalance_cpp"},  // host statics, src/game/balance/ResolutionBalance.cpp
    {0x10237a30, nullptr, "__sinit_TPDialog_cpp"},  // host statics, src/game/ui/TPDialog.cpp
};

const char* volatile gRunning = nullptr;
}  // namespace

void RunStaticInitialisers() {
    for (const StaticInitialiser& entry : kLibraryInitialisers) {
        tpw::Log("library initialiser 0x%08x %s", entry.address, entry.name);
        gRunning = entry.name;
        entry.function();
    }
    for (const StaticInitialiser& entry : kStaticInitialisers) {
        if (entry.function == nullptr) continue;
        tpw::Log("static initialiser 0x%08x %s", entry.address, entry.name);
        gRunning = entry.name;
        entry.function();
    }
    gRunning = nullptr;
}

const char* RunningStaticInitialiser() { return gRunning; }
