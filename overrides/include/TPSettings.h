// TPSettings.cpp (main app): the player settings (the resources are in TPResource.h).
#pragma once
#include <cstdint>
#include "TbDynamicStringTemplate.h"

// The player's settings (0x9c bytes on the Mac): the options panel's (current and stored,
// to tell what it changed), the custom game options and what a change requires. The field
// names are the ones SerialiseLocal and SerialiseGlobal save them under.
class CPlayerSettings {
public:
    enum eRENDERER : int32_t {};             // 1 hardware, 0 software
    enum eRESOLUTION : int32_t {};
    enum eDETAIL : int32_t {};               // 0 low, 1 medium, 2 high (low/med/high.sam)
    enum eVIDEOCARD : int32_t {};
    enum eGAME_CONTINUE_STATE : int32_t {};  // 0 nothing, 1 refresh the park, 2 restart the game
    enum eGAME_REFRESH_STATE : int32_t {};   // bit 1: stop the sound effects on the refresh

    struct Volume {
        bool mOn;                // 0x00
        int32_t mVolume;         // 0x04
    };
    // What the options panel sets (0x3c bytes).
    struct Settings {
        eRENDERER mRenderer;     // 0x00  "Renderer"
        eRESOLUTION mResolution; // 0x04  "Resolution"; InitialiseGameStage3's fallback screen size: 0 512 x 384,
                                 //       1 640 x 480, 2 800 x 600, 3 1024 x 768, else 640 x 480
        eDETAIL mDetail;         // 0x08  "Detail"; 0 also makes the particle system low spec
        eVIDEOCARD mVideoCard;   // 0x0c  "VideoCard"; 1: reset to 0 when the display has fewer than two devices
        Volume mSfx;             // 0x10  "SFXVolume"
        Volume mMusic;           // 0x18  "MusicVolume"
        Volume mSpeech;          // 0x20  "SpeechVolume"
        Volume mMovie;           // 0x28  "MovieVolume"
        int32_t mSoundQuality;   // 0x30  "SoundQuality"; also turns sound occlusion on or off
        bool mAdvisorOn;         // 0x34
        bool mTutorialOn;        // 0x35
        bool mTooltipsOn;        // 0x36
        bool mConfirmDeleteOn;   // 0x37
        bool mRMBScrollOn;       // 0x38
        bool mRMBCancelOn;       // 0x39
        bool mIsometricOn;       // 0x3a
    };

    CPlayerSettings() { Init(); }            // inline in __sinit_TPSettings_cpp
    ~CPlayerSettings();                      // 0x10165ad0 (nonvirtual)
    // 0x10165b40: the defaults: a detail level and screen size by the machine's memory and
    // processor speed, the volumes from sound.sam, everything on but RMB scrolling.
    void Init();
    int32_t GetMusicVolume() const;          // 0x10165d9c
    int32_t GetSFXVolume() const;            // 0x10165ddc
    int32_t GetSpeechVolume() const;         // 0x10165e1c
    int32_t GetMovieVolume() const;          // 0x10165e5c
    // 0x10165e9c: applies the options panel's changes and saves Config.tcf.
    void UpdateSettings();
    void ResetSettings();                    // 0x10166158: back to the stored settings
    void StoreSettings();                    // 0x10166244
    void SetGameContinueState(eGAME_CONTINUE_STATE state);   // 0x10166334: only raises it
    void ResetGameContinueState();           // 0x101663ac
    void SetGameRefreshState(eGAME_REFRESH_STATE state);     // 0x101663f8: adds bits
    void ResetGameRefreshState();            // 0x1016646c: clears bits 0-2
    void SetSoundQuality(int32_t quality);   // 0x101664bc
    void SetRendererType(eRENDERER type);    // 0x101664fc
    void SetResolution(eRESOLUTION resolution);   // 0x10166558
    void SetVideoCard(eVIDEOCARD card);           // 0x101665c0
    void SetGraphicsDetail(eDETAIL detail);       // 0x10166628
    // 0x10166690: reads the detail's options file and asks for a restart when it changes
    // what needs one; 1 when the file loads.
    int32_t LoadGraphicsDetails(eDETAIL detail);
    bool HasTextureSizeChanged(int32_t now, int32_t before) const;   // 0x10166918
    bool HasNumKidsChanged(int32_t now, int32_t before) const;       // 0x10166970
    // 0x101669c4: the sound quality crossed sound.sam's MPEG threshold.
    bool HasMPEGRateChanged(int32_t now, int32_t before) const;
    void UpdateVolume();                     // 0x10166a40 (gPlayerSettings')
    // 0x10166b7c, 0x10166f7c: saves (save != 0) or loads the local (per user) or global
    // settings; 0 on success.
    int32_t SerialiseLocal(void* file, int32_t save, uint32_t version);
    int32_t SerialiseGlobal(void* file, int32_t save, uint32_t version);
    int32_t SaveGlobal(TbDynamicStringTemplate<char>& path);   // 0x101671ec, nonzero on success
    int32_t LoadGlobal(TbDynamicStringTemplate<char>& path);   // 0x10167348, nonzero on success

    Settings mCurrent;                       // 0x00
    Settings mStored;                        // 0x3c
    // The custom options InitialiseGameStage1 restores when mCustomOptions is set:
    uint32_t mGameOptions;                   // 0x78  "MAINGameOptions", gGameOptions
    uint32_t mExtraEngineOptions;            // 0x7c  "ExtraEngineOptions", gTPDialogOptions
    uint32_t mDebugGameOptions;              // 0x80  "DebugGameOptions"
    int32_t mCurrentTheme;                   // 0x84  "CURRENTTheme", passed to ConverttoCurrentTheme
    uint32_t mDebugGuardUInts;               // 0x88  "DEBUGGuardUInts"
    uint32_t mTextureCacheSize;              // 0x8c  "TextureCacheSize", gTPDialogTextureMegabytes
    int32_t mCustomOptions;                  // 0x90  1: the options dialog's own settings (gTPDialogOptions);
                                             //       else a preset options file (mDetail)
    eGAME_CONTINUE_STATE mGameContinueState; // 0x94
    uint32_t mGameRefreshState;              // 0x98
};
extern CPlayerSettings gPlayerSettings;   // 0x1038071c, original name unknown (src/game/TPSettings.cpp, src/game/TPResource.cpp)
// The strings __sinit_TPSettings_cpp constructs before it (the same set as TPMain.cpp's).
extern TbDynamicStringTemplate<char> gTPSettingsLanguages[16];   // 0x10380804
extern TbDynamicStringTemplate<char> gTPSettingsTitle;           // 0x103807e4
extern TbDynamicStringTemplate<char> gTPSettingsLanguage;        // 0x103807c4

// The restart dialog UpdateSettings opens when a change needs the game restarted.
void RestartDialog();                    // 0x101675b8
int32_t RestartCallback_OK(void* data);  // 0x101674c0: the lobby quits, or the park saves the autosave
int32_t RestartCallback_Cancel(void* data);   // 0x10167574: the change waits
// TPW_PORT_FIX resslider gPortLiveResolution | Mac: the slider offers 640 x 480, 800 x 600 and 1024 x 768, kept in Config.tcf as 1 to 3; OK asks for a restart and the next start opens at that size | Port: the launcher sets this: the slider offers every size the port has, whatever the screen, the game changes to it while running (PortApplyGameSize, src/game/ui/ui_gamesize.cpp), OK saves it in Config.tcf as 0x100 plus the preset and the next start opens at it | Why: the port renders offscreen and scales the picture to the window, so changing size takes no restart and no display mode, and the launcher's preset list is far longer than the setting's three
// False unless the launcher sets it, so a program that does not (the unit and differential tests) keeps the Mac's panel.
inline bool gPortLiveResolution = false;
inline int32_t gPortLargestGameSize = 0;                 // this machine's framebuffer limit in pixels; 0 when unknown (not the screen)
inline constexpr int32_t kPortResolutionChosen = 0x100;  // this plus the preset: that preset at the shape the launcher finds
// The size the panel found running when it opened, and the size the slider asks for now (0 x 0: none yet).
inline int32_t gPortPanelWide = 0;
inline int32_t gPortPanelHigh = 0;
inline int32_t gPortWantedWide = 0;
inline int32_t gPortWantedHigh = 0;
// The setting for a preset at a shape of kUISizeShapes (two steps of kPortResolutionChosen up, the shape's place above that).
inline int32_t PortResolutionSetting(int32_t preset, int32_t shape) { return kPortResolutionChosen * (shape + 2) + preset; }
// The preset a saved setting means: one chosen on the slider, or `otherwise` for what an older version saved.
inline int32_t PortResolutionPreset(int32_t setting, int32_t otherwise) {
    const int32_t chosen = setting & 0xff;
    const int32_t step = setting >> 8;
    return step >= 1 && step <= 4 && chosen >= 1 && chosen <= 10 ? chosen : otherwise;
}
// The shape a saved setting names, or -1 when it names none (the launcher's stands).
inline int32_t PortResolutionShape(int32_t setting) {
    const int32_t shape = (setting >> 8) - 2;
    return PortResolutionPreset(setting, 0) != 0 && shape >= 0 && shape <= 2 ? shape : -1;
}
// The setting for an exact size, which is what the slider saves now (the two forms above still mean what they did): 15 bits
// each, under a bit neither of them reaches, so a version that knows only those takes it for one it never saved.
inline constexpr int32_t kPortResolutionExact = 0x40000000;
inline int32_t PortResolutionExact(int32_t width, int32_t height) { return kPortResolutionExact | width << 15 | height; }
// False when the setting is not one, or holds a size the game has no layout for (the launcher's size stands then).
inline bool PortResolutionSize(int32_t setting, int32_t& width, int32_t& height) {
    if (setting < 0 || (setting & kPortResolutionExact) == 0) return false;
    const int32_t wide = (setting >> 15) & 0x7fff;
    const int32_t high = setting & 0x7fff;
    if (wide < 512 || high < 384 || wide > 8192 || high > 8192 || wide * 4 < high * 5) return false;
    width = wide;
    height = high;
    return true;
}
// The options panel's three shape buttons (kUISizeShapes' order) and the text beside the slider.
inline constexpr int32_t kPortShapeButton = 0x1d4e1;
inline constexpr int32_t kPortSizeReadout = 0x1d4e4;
// The screen's own size in pixels (0 x 0: not known), which the slider offers; a start's size when it is off the presets
// (0 x 0: the preset's), from --resolution or a saved setting.
inline int32_t gPortScreenWide = 0;
inline int32_t gPortScreenHigh = 0;
inline int32_t gPortStartWide = 0;
inline int32_t gPortStartHigh = 0;
inline bool PortSizeFits(int32_t width, int32_t height) {
    return gPortLargestGameSize <= 0 || (width <= gPortLargestGameSize && height <= gPortLargestGameSize);
}
// At the top of the game's tick, between frames: once the thumb has stopped on a new size, changes to it (src/game/ui/ui_gamesize.cpp).
void PortApplyGameSize();
// Changes the running game's size. Everything that depends on it follows here.
void PortChangeGameSize(int32_t width, int32_t height);
// A shape button of the options panel (and the panel opening): the slider's stops at that shape.
void PortShowShape(int32_t shape);
class InterfaceFont;
void PortMakeSizeRow(InterfaceFont* font, int32_t fontIndex, int16_t z);  // the size readout and the shape buttons (ui_gamesize.cpp)
