// online_signup_ui_tests.cpp: the online sign-up's and password reset's popups (platform/online_signup_ui.cpp, the fix
// `signup`) with the game's window calls as fakes, the unit tests' way (tests/unit_CreateOnlineLoginPanel.cpp): the windows
// are blocks with a fake virtual table, Dialog builds them from the window script it is given (each window a child of the
// one it is in), closing a window (message 4) gives its handler 0x14, and the message functions the popups hand the game
// are driven as the game would. The script is one made here in the login panel's form (its ids and records, not its
// words). Checks: the login panel's script and its two buttons; the sign-up popup's windows, texts, masked boxes and tick
// box; every refusal before anything is sent and the box each gives the keyboard back to; the wait and its answer (a
// refusal, the account made over a fake connection); the notice filling the login panel's boxes and wiping the popup's;
// the reset: the code request, its notice opening the new password popup, a wrong and a dead code, the password set; Cancel,
// Escape, Enter, Tab and a close while the request is out.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "FS_Online.h"
#include "InterfaceWindow.h"
#include "TPMain.h"
#include "Text.h"
#include "UI_Base.h"
#include "platform/online_boss.h"
#include "platform/online_signup.h"
#include "ui_DrawObject.h"
#include "ui_ErrorDisplay.h"
#include "ui_controls.h"
#include "ui_main.h"

namespace signup = tpw::online::signup;
namespace boss = tpw::online::boss;
using signup::Text;

#ifdef TPW_PORT_FIX

namespace {

int gFailures = 0;
int gChecks = 0;

void Check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::fprintf(stderr, "FAILED: %s\n", what.c_str());
    }
}

std::u16string Text16(const char16_t* text) { return text == nullptr ? std::u16string() : std::u16string(text); }

// The game's shared text buffer (gUIMainConvertBuffer): UIMain_GetString answers its strings in it, and a masked box's text
// object fills it with its stars.
char16_t gGameText[0x40] = {};

// ---- The windows -----------------------------------------------------------------------------------------------------------

struct Win {
    int32_t id = 0;
    int16_t kind = 0;
    int16_t rect[4] = {};
    InterfaceWindow* parent = nullptr;
    std::vector<InterfaceWindow*> children;
    InterfaceWindowCallback callback = nullptr;
    char16_t* area = nullptr;          // an edit box's text (SetStaticTextArea)
    int32_t capacity = 0;
    int32_t value = -1;                // SetValue's
    std::string client;                // the client draw object's kind (its constructor's)
    std::u16string text;               // SetText's, as it was then
    char16_t* clientText = nullptr;    // SetClientDrawText's
    std::u16string drawn;              // and its text as it was then (a label's text object draws it at once)
    InterfaceDrawObject* drawObject = nullptr;
    int captured = 0;                  // CaptureKb calls
    int32_t captureOn = -1;            // the last CaptureKb's argument (0: Tab's, select all; 1: a click's)
    int16_t cursor[3] = {0, 0, 0};     // the last MoveCursor's position, show and selection
    int defaults = 0;                  // CallDefaultWindowHandler calls
    bool closed = false;
};

std::map<InterfaceWindow*, Win> gWins;
std::vector<std::string> gLog;
void* gVtable[64];

Win& W(InterfaceWindow* window) { return gWins[window]; }

int Count(const std::string& event) {
    int n = 0;
    for (const std::string& e : gLog) n += e == event;
    return n;
}

int32_t FakeSetText(InterfaceWindow* self, char16_t* text) {
    Win& w = W(self);
    w.text = Text16(text);
    if (w.client == "password") {
        size_t n = 0;
        for (; text != nullptr && text[n] != 0 && n + 1 < std::size(gGameText); ++n) gGameText[n] = u'*';
        gGameText[n] = 0;
    }
    if (w.area != nullptr) {   // an edit box keeps it in its own text
        size_t n = 0;
        for (; text != nullptr && text[n] != 0 && n < static_cast<size_t>(w.capacity); ++n) w.area[n] = text[n];
        w.area[n] = 0;
    }
    return 1;
}
int32_t FakeSetValue(InterfaceWindow* self, int32_t value) {
    W(self).value = value;
    return 1;
}
intptr_t FakeOther(void*, intptr_t) {
    gLog.push_back("other virtual");
    return 0;
}

// Which slot of the table a virtual is in: a table of probes, each saying its own number.
int gProbe = -1;
template <int N>
intptr_t Probe(void*, intptr_t) {
    gProbe = N;
    return 0;
}
template <int... N>
void FillProbes(std::integer_sequence<int, N...>) {
    ((gVtable[N] = reinterpret_cast<void*>(&Probe<N>)), ...);
}

InterfaceWindow* NewWindow() {
    void** block = static_cast<void**>(operator new(0x400));
    std::memset(block, 0, 0x400);
    block[0] = gVtable;
    InterfaceWindow* window = reinterpret_cast<InterfaceWindow*>(block);
    gWins[window] = Win{};
    return window;
}

void MakeVtable() {
    FillProbes(std::make_integer_sequence<int, 64>{});
    InterfaceWindow* probe = NewWindow();
    gProbe = -1;
    probe->SetText(nullptr);
    const int text = gProbe;
    gProbe = -1;
    probe->SetValue(0);
    const int value = gProbe;
    Check(text >= 0 && value >= 0 && text != value, "the virtual table's SetText and SetValue slots");
    for (void*& entry : gVtable) entry = reinterpret_cast<void*>(&FakeOther);
    if (text >= 0) gVtable[text] = reinterpret_cast<void*>(&FakeSetText);
    if (value >= 0) gVtable[value] = reinterpret_cast<void*>(&FakeSetValue);
}

// The window script read as the game builds it: record 0 is a window (kind, attributes, id, rectangle), 1, 2, 17 and 18
// take 3 words, 3 takes 5, 5 ends a level.
bool Build(const int16_t* w, size_t& i, InterfaceWindow* parent, std::vector<InterfaceWindow*>& made) {
    while (i < 4096) {
        const int16_t record = w[i];
        if (record == 5) {
            ++i;
            return true;
        }
        if (record == 0) {
            InterfaceWindow* window = NewWindow();
            Win& win = W(window);
            win.kind = w[i + 1];
            win.id = static_cast<int32_t>(static_cast<uint16_t>(w[i + 4]) | static_cast<uint32_t>(w[i + 5]) << 16);
            for (int k = 0; k < 4; ++k) win.rect[k] = w[i + 6 + k];
            win.parent = parent;
            if (parent != nullptr) W(parent).children.push_back(window);
            made.push_back(window);
            i += 10;
            if (!Build(w, i, window, made)) return false;
            continue;
        }
        if (record == 1 || record == 2 || record == 17 || record == 18) i += 3;
        else if (record == 3) i += 5;
        else return false;
    }
    return false;
}

// The windows of a script, the top one first.
std::vector<InterfaceWindow*> BuildScript(const int16_t* script) {
    std::vector<InterfaceWindow*> made;
    size_t i = 0;
    Check(Build(script, i, nullptr, made), "a script the game can read");
    return made;
}

InterfaceWindow* Child(InterfaceWindow* window, int32_t id) {
    if (window == nullptr) return nullptr;
    for (InterfaceWindow* child : W(window).children)
        if (W(child).id == id) return child;
    return nullptr;
}

// What the game does with a window's messages: 4 closes it (its handler hears 0x14), the rest go to its handler.
std::deque<std::vector<intptr_t>> gPosted;

int32_t Deliver(InterfaceWindow* window, int32_t message, int32_t a, int32_t b) {
    Win& w = W(window);
    if (w.closed) return 0;
    if (message == 4) {
        w.closed = true;
        if (w.callback != nullptr) w.callback(window, 0x14, 0, 0);
        return 1;
    }
    return w.callback != nullptr ? w.callback(window, message, a, b) : 0;
}

void Pump() {
    while (!gPosted.empty()) {
        const std::vector<intptr_t> m = gPosted.front();
        gPosted.pop_front();
        Deliver(reinterpret_cast<InterfaceWindow*>(m[0]), static_cast<int32_t>(m[1]), static_cast<int32_t>(m[2]),
                static_cast<int32_t>(m[3]));
    }
}

// ---- What the fakes saw ----------------------------------------------------------------------------------------------------

struct DialogCall {
    InterfaceWindow* window;
    InterfaceWindowCallback handler;
    int32_t modal;
};
std::vector<DialogCall> gDialogs;
std::vector<std::u16string> gBoxTexts;     // UI_DialogBox's, in order
std::vector<int32_t> gBoxButtons;          // and their one button
std::vector<InterfaceWindow*> gBoxes;
std::u16string gBarText;
int16_t gBarZ = 0;
alignas(16) unsigned char gObjectBytes[4][16];   // FindDrawObject's answers, by name
InterfaceDrawObject* Object(int which) { return reinterpret_cast<InterfaceDrawObject*>(gObjectBytes[which]); }
std::vector<std::string> gObjectNames;

}  // namespace

// ---- The game's calls, faked -----------------------------------------------------------------------------------------------

InterfaceWindow* gUIProgressBar = nullptr;
OnlineConfig gOnlineConfig;
OnlineConfig::OnlineConfig() : mMailPort(0), mChatPort(0), mLoginPort(0), mCityPort(0), mNewsPort(0), mVotePort(0) {}
uint32_t CText::GetLanguageId() const { return 9; }

InterfaceWindow* Dialog(int16_t* script, int32_t (*handler)(InterfaceWindow*, int32_t, int32_t, int32_t), int32_t modal) {
    const std::vector<InterfaceWindow*> made = BuildScript(script);
    if (made.empty()) return nullptr;
    W(made[0]).callback = handler;
    gDialogs.push_back({made[0], handler, modal});
    gLog.push_back("Dialog");
    return made[0];
}

int32_t DefaultDialogCallback(InterfaceWindow*, int32_t message, int32_t, int32_t) {
    gLog.push_back("DefaultDialogCallback " + std::to_string(message));
    return 0;
}

int32_t DialogBoxCallback(InterfaceWindow*, int32_t message, int32_t, int32_t) {
    gLog.push_back("DialogBoxCallback " + std::to_string(message));
    return 0;
}

InterfaceWindow* UI_DialogBox(dlgboxparams& params, dlgbutton button1, dlgcallback callback1, dlgbutton, dlgcallback,
                              dlgbutton, dlgcallback, bool flag) {
    Check(params.mRed == 0xff && params.mGreen == 0xff && params.mBlue == 0xff && params.mValue8 == 4,
          "the message box's colour and form, as the game's errors");
    Check((static_cast<int32_t>(button1) == 1 || static_cast<int32_t>(button1) == 2) && callback1 == nullptr && !flag,
          "the message box's one button closes it");
    gBoxTexts.push_back(Text16(params.mText));
    gBoxButtons.push_back(static_cast<int32_t>(button1));
    InterfaceWindow* box = NewWindow();
    gBoxes.push_back(box);
    return box;
}

void CreateUIProgressBar(char16_t* text) {
    gLog.push_back("CreateUIProgressBar");
    gBarText = Text16(text);
    gUIProgressBar = NewWindow();
}

void FuckOffUIProgressBar() {
    gLog.push_back("FuckOffUIProgressBar");
    gUIProgressBar = nullptr;
}

int32_t UI_GetHighestZ(InterfaceWindow*, CoolRect*) { return 40; }
int32_t UI_SendMessage(InterfaceWindow* window, int32_t message, int32_t a, int32_t b) {
    return Deliver(window, message, a, b);
}
int32_t UI_PostMessage(InterfaceWindow* window, int32_t message, int32_t a, int32_t b) {
    gPosted.push_back({reinterpret_cast<intptr_t>(window), message, a, b});
    return 1;
}

const char16_t* UIMain_GetString(int32_t id) {
    const std::u16string text = id == 0xed ? u"Name" : id == 0xee ? u"Password" : u"";
    std::copy(text.begin(), text.end() + 1, gGameText);
    return gGameText;
}
InterfaceFont* GetFontTableEntry(uint32_t index) { return reinterpret_cast<InterfaceFont*>(static_cast<uintptr_t>(0x100 + index)); }
void UI_TITLE_COLOUR(InterfaceWindow*) { gLog.push_back("UI_TITLE_COLOUR"); }

InterfaceDrawObject* DrawObjectLibrary::FindDrawObject(char* name) {
    gObjectNames.push_back(name);
    const std::string n = name;
    return Object(n == "b_toggle" ? 0 : n == "i_boxtick" ? 1 : n == "hilight" ? 2 : 3);
}

InterfaceWindow* InterfaceWindow::GetWindow(int32_t id) { return Child(this, id); }
void InterfaceWindow::CaptureKb(int32_t on) {
    ++W(this).captured;
    W(this).captureOn = on;
}
static std::map<void*, std::string> gObjectKinds;   // the draw objects made, by their constructor
void InterfaceWindow::SetClientDrawObject(InterfaceDrawObject* object) { W(this).client = gObjectKinds[object]; }
void InterfaceWindow::SetDrawObject(InterfaceDrawObject* object) { W(this).drawObject = object; }
void InterfaceWindow::SetClientDrawText(char16_t* text) {
    W(this).clientText = text;
    W(this).drawn = Text16(text);
}
void InterfaceWindow::SetFont(InterfaceFont*) {}
void InterfaceWindow::SetFontColour(int32_t, int32_t, int32_t, int32_t) {}
void InterfaceWindow::SetFontJust(int32_t, int32_t) {}
void InterfaceWindow::SetFontWrap(int32_t, int32_t) {}
void InterfaceWindow::SetCallbackFunction(InterfaceWindowCallback callback) { W(this).callback = callback; }
int32_t InterfaceWindow::CallDefaultWindowHandler(int32_t, int32_t, int32_t) {
    ++W(this).defaults;
    return 0;
}
void InterfaceWindow::setZ(int16_t z) { gBarZ = z; }

int32_t InterfaceEditBox::SetStaticTextArea(char16_t* text, int32_t capacity) {
    W(this).area = text;
    W(this).capacity = capacity;
    return 0;
}
void InterfaceEditBox::SetCursorDrawObject(InterfaceDrawObject*) {}
void InterfaceEditBox::MoveCursor(int16_t position, int16_t show, int16_t selection) {
    Win& w = W(this);
    w.cursor[0] = position;
    w.cursor[1] = show;
    w.cursor[2] = selection;
}

// The draw objects' constructors, under their real symbols (no virtual table needed).
#define TPW_STR2(x) #x
#define TPW_STR(x) TPW_STR2(x)
#define TPW_SYMBOL(name) TPW_STR(__USER_LABEL_PREFIX__) name
extern "C" {
void FakeSpriteCtor(void* self, void* data) __asm__(TPW_SYMBOL("_ZN25InterfaceTextObjectSpriteC1EPv"));
void FakeTitleCtor(void* self, void* data) __asm__(TPW_SYMBOL("_ZN24InterfaceTitleTextObjectC1EPv"));
void FakeCursorCtor(void* self, void* data) __asm__(TPW_SYMBOL("_ZN34InterfaceCursorPosTextObjectSpriteC1EPv"));
void FakePasswordCtor(void* self, void* data) __asm__(TPW_SYMBOL("_ZN27InterfacePasswordTextObjectC1EPv"));
void FakeSpriteCtor(void* self, void*) { gObjectKinds[self] = "text"; }
void FakeTitleCtor(void* self, void*) { gObjectKinds[self] = "title"; }
void FakeCursorCtor(void* self, void*) { gObjectKinds[self] = "cursor"; }
void FakePasswordCtor(void* self, void*) { gObjectKinds[self] = "password"; }
// The game's text object (only CText::GetLanguageId, faked above, reads it).
alignas(16) unsigned char gTextBytes[sizeof(CText)] __asm__(TPW_SYMBOL("gText"));
}

namespace {

// ---- The login panel's form, made here ------------------------------------------------------------------------------------

void AddWindow(std::vector<int16_t>& s, int16_t kind, int32_t id, int16_t l, int16_t t, int16_t r, int16_t b) {
    const int16_t words[] = {0, kind, 0x12, 0, static_cast<int16_t>(id), static_cast<int16_t>(id >> 16), l, t, r, b};
    s.insert(s.end(), std::begin(words), std::end(words));
}

// The panel (0x48) with OK (-1), Cancel (-2), the name box (0x49, its label 0x4a), the password box (0x4b, its label 0x4c)
// and the title (0x4d), the frame's draw object last. `broken`: an unknown record in the title, a form not expected.
std::vector<int16_t> LoginScript(bool broken = false) {
    std::vector<int16_t> s;
    AddWindow(s, 1, 0x48, 100, 100, 1100, 700);
    AddWindow(s, 2, -1, 700, 600, 800, 660);
    s.insert(s.end(), {1, 3, 0, 5});
    AddWindow(s, 2, -2, 850, 600, 950, 660);
    s.insert(s.end(), {1, 4, 0, 5});
    AddWindow(s, 3, 0x49, 400, 300, 1000, 360);
    s.insert(s.end(), {3, 400, 300, 1000, 360});
    AddWindow(s, 1, 0x4a, 150, 300, 390, 360);
    s.push_back(5);
    s.push_back(5);
    AddWindow(s, 3, 0x4b, 400, 400, 1000, 460);
    s.insert(s.end(), {3, 400, 400, 1000, 460});
    AddWindow(s, 1, 0x4c, 150, 400, 390, 460);
    s.push_back(5);
    s.push_back(5);
    AddWindow(s, 1, 0x4d, 150, 120, 1050, 200);
    s.insert(s.end(), {17, 1, 0, 18, 2, 0});
    if (broken) s.insert(s.end(), {9, 9, 9});
    s.push_back(5);
    s.insert(s.end(), {1, 1, 0});   // the frame
    s.push_back(5);
    s.push_back(5);
    return s;
}

std::vector<int16_t> gLoginScript;   // kept: LoginPanelScript keeps what it made from it for the next panel
InterfaceWindow* gLogin = nullptr;   // the login panel

struct Fake : boss::Transport {
    std::vector<uint8_t> answer;
    std::vector<uint8_t>* sent;
    size_t given = 0;
    Fake(std::vector<uint8_t> a, std::vector<uint8_t>* s) : answer(std::move(a)), sent(s) {}
    bool Open(const std::string&, uint16_t, const std::atomic<bool>&) override { return true; }
    bool Requested() const {
        const std::vector<uint8_t>& m = *sent;
        return m.size() >= 0x14 && m.size() >= 0x14 + (static_cast<size_t>(m[10]) << 8 | m[11]);
    }
    int Wait(bool wantWrite, int) override {
        if (wantWrite) return 2;
        if (Requested() && given < answer.size()) return 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return 0;
    }
    long Send(const uint8_t* bytes, size_t size) override {
        sent->insert(sent->end(), bytes, bytes + size);
        return static_cast<long>(size);
    }
    long Receive(uint8_t* bytes, size_t size) override {
        const size_t n = std::min(size, answer.size() - given);
        std::memcpy(bytes, answer.data() + given, n);
        given += n;
        return static_cast<long>(n);
    }
    void Close() override {}
};

std::vector<uint8_t> gSent;

// The server's answer to the next request, of reply `type`: a refusal, or OK (status 0; for a sign-up or a password set
// with the account's name and id).
void Answer(uint16_t type, uint16_t status, const std::u16string& name = u"") {
    const size_t payload = status == 0 && type != signup::kTypeResetCodeReply ? signup::kAccountSize : 0;
    std::vector<uint8_t> m(0x14 + payload, 0);
    m[0] = 'B';
    m[1] = 's';
    m[3] = static_cast<uint8_t>(type);
    m[7] = static_cast<uint8_t>(status);
    m[11] = static_cast<uint8_t>(payload);
    m[19] = 10;
    for (size_t k = 0; k < name.size(); ++k) m[0x14 + 2 * k] = static_cast<uint8_t>(name[k]);
    if (payload) m[0x14 + 0x43] = 7;
    gSent.clear();
    boss::SetTransportMaker([m] { return std::make_unique<Fake>(m, &gSent); });
}

std::u16string SentField(size_t offset, size_t units) {
    std::u16string s;
    for (size_t k = 0; k < units && 0x14 + offset + 2 * k + 1 < gSent.size(); ++k) {
        const char16_t c = static_cast<char16_t>(gSent[0x14 + offset + 2 * k] | gSent[0x14 + offset + 2 * k + 1] << 8);
        if (c == 0) break;
        s += c;
    }
    return s;
}

// Waits (as the game's frames do) until the popup has its answer.
void Wait(InterfaceWindow* form) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (gUIProgressBar != nullptr && std::chrono::steady_clock::now() < deadline) {
        Deliver(form, 0x1e, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Check(gUIProgressBar == nullptr, "the answer came");
}

void Type(InterfaceWindow* box, const std::u16string& text) {
    Win& w = W(box);
    size_t n = 0;
    for (; n < text.size() && n < static_cast<size_t>(w.capacity); ++n) w.area[n] = text[n];
    w.area[n] = 0;
}

std::u16string Area(InterfaceWindow* box) { return W(box).area == nullptr ? u"(no area)" : std::u16string(W(box).area); }

InterfaceWindow* OpenWith(int32_t (*open)(InterfaceWindow*)) {
    const size_t dialogs = gDialogs.size();
    Check(open(gLogin) == 1, "the button's message is taken");
    return gDialogs.size() > dialogs ? gDialogs.back().window : nullptr;
}
InterfaceWindow* Open() { return OpenWith(signup::OpenPopup); }
InterfaceWindow* OpenReset() { return OpenWith(signup::OpenResetPopup); }

// The popup's refusal for what is in its boxes now (its button, `action`): the last message box's text; the box closes and
// gives the box `focus` the keyboard, as a click gives it (CaptureKb(1)) with the caret after its text, nothing selected.
std::u16string Refusal(InterfaceWindow* form, int32_t focus, const std::string& what, int32_t action = -1) {
    const size_t boxes = gBoxTexts.size();
    Deliver(form, 0x100, action, 0);
    Wait(form);
    if (gBoxTexts.size() != boxes + 1) return u"(none)";
    Check(gBoxButtons.back() == 2, what + ": a refusal's box has the exit sign (as the game's errors)");
    InterfaceWindow* box = gBoxes.back();
    Win& target = W(Child(form, focus));
    const int before = target.captured;
    target.captureOn = -1;
    target.cursor[0] = target.cursor[1] = target.cursor[2] = 0;
    Check(W(box).callback != nullptr, what + ": the message box has the popup's handler");
    Deliver(box, 4, 0, 0);
    Check(target.captured == before + 1 && target.captureOn == 1 && target.cursor[0] == -5 && target.cursor[1] == 1 &&
              target.cursor[2] == -1,
          what + ": the box it is about has the keyboard, the caret after its text");
    return gBoxTexts.back();
}

bool At(InterfaceWindow* w, int16_t l, int16_t t, int16_t r, int16_t b) {
    return w != nullptr && W(w).rect[0] == l && W(w).rect[1] == t && W(w).rect[2] == r && W(w).rect[3] == b;
}

void TestLoginPanel() {
    signup::OpenPopup(nullptr);
    signup::OpenResetPopup(nullptr);
    Check(gDialogs.empty(), "no popup before the login panel had its buttons");

    std::vector<int16_t> broken = LoginScript(true);
    Check(signup::LoginPanelScript(broken.data(), broken.size()) == broken.data(), "a script not in the form expected is kept");
    std::vector<int16_t> shortScript = LoginScript();
    shortScript.resize(30);
    Check(signup::LoginPanelScript(shortScript.data(), shortScript.size()) == shortScript.data(), "a script cut short is kept");

    gLoginScript = LoginScript();
    int16_t* script = signup::LoginPanelScript(gLoginScript.data(), gLoginScript.size());
    Check(script != gLoginScript.data(), "the login panel's script is a new one");
    Check(signup::LoginPanelScript(gLoginScript.data(), gLoginScript.size()) == script, "and the same one next time");
    Check(gLoginScript == LoginScript(), "the original's script is left as it was");

    gLogin = Dialog(script, nullptr, 0);
    gPortOnlineLoginPanel = gLogin;
    InterfaceWindow* create = Child(gLogin, signup::kCreateButtonId);
    InterfaceWindow* forgot = Child(gLogin, signup::kForgotButtonId);
    InterfaceWindow* ok = Child(gLogin, -1);
    InterfaceWindow* name = Child(gLogin, 0x49);
    InterfaceWindow* password = Child(gLogin, 0x4b);
    Check(create != nullptr && forgot != nullptr && ok != nullptr && Child(gLogin, -2) != nullptr,
          "the two buttons beside OK and Cancel");
    if (create == nullptr || forgot == nullptr || name == nullptr || password == nullptr) return;
    Check(W(create).kind == W(ok).kind && W(forgot).kind == W(ok).kind, "the buttons are OK's kind of window");
    Check(At(create, 632, 664, 962, 734) && At(forgot, 977, 664, 1307, 734), "the buttons side by side under the boxes");
    Check(W(name).rect[1] == 260 && W(password).rect[1] == 360, "the boxes 40 units up");
    InterfaceWindow* label = Child(name, 0x4a);
    Check(label != nullptr && W(label).rect[1] == 260, "the name box's label with it");
    const std::vector<InterfaceWindow*>& kids = W(gLogin).children;
    Check(kids.size() >= 2 && kids[kids.size() - 2] == create && kids.back() == forgot,
          "the buttons after the panel's other windows");

    signup::DecorateLoginPanel(nullptr);
    InterfaceWindow* bare = NewWindow();
    signup::DecorateLoginPanel(bare);
    Check(gObjectNames.empty(), "nothing on a panel without the buttons");
    signup::DecorateLoginPanel(gLogin);
    Check(gObjectNames.size() == 2 && gObjectNames[0] == "b_toggle" && W(create).drawObject == Object(0) &&
              W(forgot).drawObject == Object(0),
          "the buttons have the toggle's art");
    Check(W(create).text == signup::GetText(Text::Button) && W(create).clientText == nullptr, "Create account's label");
    Check(W(forgot).text == signup::GetText(Text::ForgotButton) && W(forgot).clientText == nullptr,
          "Forgot password?'s label");
}

// The login panel's boxes filled by a notice's OK (the name the server gave, the password typed).
void CheckFilled(InterfaceWindow* form, int32_t passwordBox, const std::u16string& name, const std::u16string& password,
                 const std::string& what) {
    Check(gBoxButtons.back() == 1, what + ": the notice's one button is the OK");
    Check(Open() == nullptr && OpenReset() == nullptr, what + ": no popup while the notice is open");
    InterfaceWindow* loginPassword = Child(gLogin, 0x4b);
    const int keys = W(loginPassword).captured;
    Deliver(gBoxes.back(), 4, 0, 0);
    Check(W(Child(gLogin, 0x49)).text == name && W(loginPassword).text == password,
          what + ": OK fills the login panel's boxes");
    Check(W(loginPassword).captured == keys + 1, what + ": the login password box has the keyboard (Enter logs in)");
    Check(Area(Child(form, passwordBox)).empty(), what + ": the popup's password is wiped");
}

void TestSignup() {
    InterfaceWindow* form = Open();
    Check(form != nullptr && gDialogs.back().modal == 1, "the popup opens over the login panel");
    if (form == nullptr) return;
    Check(Open() == nullptr && OpenReset() == nullptr, "only one popup at a time");
    Check(W(Child(form, 0x4d)).text == signup::GetText(Text::Title) && Count("UI_TITLE_COLOUR") == 1, "the popup's title");
    InterfaceWindow* name = Child(form, 0x49);
    InterfaceWindow* password = Child(form, 0x4b);
    InterfaceWindow* confirm = Child(form, 0x51);
    InterfaceWindow* email = Child(form, 0x53);
    InterfaceWindow* note = Child(form, 0x50);
    InterfaceWindow* tick = Child(form, 0x4e);
    InterfaceWindow* words = Child(form, 0x4f);
    Check(Child(form, -1) != nullptr && Child(form, -2) != nullptr, "Create and Cancel");
    Check(Child(form, signup::kCreateButtonId) == nullptr && Child(form, signup::kForgotButtonId) == nullptr,
          "no login panel buttons in the popup");
    if (!name || !password || !confirm || !email || !note || !tick || !words) {
        Check(false, "the popup's boxes, note and tick box");
        return;
    }
    Check(W(name).capacity == 16 && W(password).capacity == 16 && W(confirm).capacity == 16 && W(email).capacity == 100,
          "the boxes hold 16, 16, 16 and 100 characters");
    Check(W(name).client == "cursor" && W(email).client == "cursor", "the name and e-mail as the login panel's name box");
    Check(W(password).client == "password" && W(confirm).client == "password",
          "both passwords masked as the login panel's password box");
    Check(W(confirm).kind == W(password).kind && W(email).kind == W(name).kind, "the new boxes are copies of the panel's");
    Check(At(name, 632, 324, 1307, 396) && At(password, 632, 402, 1307, 474) && At(confirm, 632, 480, 1307, 552) &&
              At(email, 632, 610, 1307, 682),
          "the boxes' places, one under the other");
    Check(Child(password, 0x4c) && Child(confirm, 0x52) && W(Child(password, 0x4c)).rect[0] == 656 &&
              W(Child(password, 0x4c)).rect[2] == 946 && W(Child(confirm, 0x52)).rect[2] == 946,
          "the password labels as wide as the name's");
    Check(At(note, 600, 556, 1340, 606), "the note under the password boxes");
    Check(W(tick).kind == 6 && W(tick).drawObject == Object(1) && W(tick).value == 0, "the game's tick box, not ticked");
    Check(W(Child(name, 0x4a)).drawn == u"Name", "the name box's label is the game's");
    Check(W(Child(password, 0x4c)).drawn == u"Password",
          "the password box's label is the game's (drawn before the masked box's stars fill the game's text buffer)");
    Check(Text16(W(Child(confirm, 0x52)).clientText) == signup::GetText(Text::Confirm), "the confirmation's label");
    Check(Text16(W(Child(email, 0x54)).clientText) == signup::GetText(Text::Email), "the e-mail box's label");
    Check(Text16(W(note).clientText) == signup::GetText(Text::Warning), "the warning");
    Check(Text16(W(words).clientText) == signup::GetText(Text::Adult), "the tick box's words");
    Check(W(name).captured == 1, "the name box has the keyboard");

    // the tick box and its words: a left press ticks and unticks, the right does nothing
    Deliver(tick, 0x10005, 0, 0);
    Check(W(tick).value == 1, "a press ticks");
    Deliver(words, 0x10005, 0, 0);
    Check(W(tick).value == 0, "a press on the words unticks");
    Deliver(tick, 0x10005, 1, 0);
    Check(W(tick).value == 0, "the right button does not");
    Check(W(tick).defaults == 2 && W(words).defaults == 1, "the game's own handling of each press");

    // every refusal before anything is sent, and the box it gives the keyboard back to
    boss::SetServer("");
    Type(name, u"x");
    Check(Refusal(form, 0x49, "short") == signup::GetText(Text::BadLength), "a short name");
    Type(name, u"Bob!");
    Check(Refusal(form, 0x49, "'!'") == signup::GetText(Text::BadChars), "a '!'");
    Type(name, u"Admin");
    Check(Refusal(form, 0x49, "reserved") == signup::GetText(Text::NotAllowed), "a reserved name");
    Type(name, u"Coaster Kid");
    Type(password, u"short");
    Check(Refusal(form, 0x4b, "rule") == signup::GetText(Text::BadPassword), "a short password");
    Type(password, u"has space");
    Check(Refusal(form, 0x4b, "space") == signup::GetText(Text::BadPassword), "a password with a space");
    Type(password, u"Wh33l-Spin!");
    Type(confirm, u"Wh33l-Spin?");
    Check(Refusal(form, 0x51, "mismatch") == u"The passwords don't match.", "the passwords differ");
    Type(confirm, u"Wh33l-Spin!");
    Type(email, u"");
    Check(Refusal(form, 0x53, "no e-mail") == signup::GetText(Text::BadEmail), "no e-mail (required)");
    Type(email, u"kid@localhost");
    Check(Refusal(form, 0x53, "e-mail form") == signup::GetText(Text::BadEmail), "an e-mail without a dotted domain");
    Type(email, u"kid@example.org");
    Check(Refusal(form, 0x49, "age") == signup::GetText(Text::NoAge), "not ticked");
    Deliver(tick, 0x10005, 0, 0);
    Check(Refusal(form, 0x49, "unreachable") == signup::RefusalText(signup::Outcome::Unreachable),
          "no online server set: the client's sentence");
    Check(Count("CreateUIProgressBar") == 0, "nothing sent for any of them");
    Check(Area(name) == u"Coaster Kid" && Area(password) == u"Wh33l-Spin!" && Area(email) == u"kid@example.org",
          "the boxes keep what was typed after a refusal");

    // the server's refusals
    boss::SetServer("127.0.0.1");
    gOnlineConfig.mLoginPort = 17598;
    Answer(signup::kTypeSignupReply, 1);
    Deliver(form, 0x100, -1, 0);
    Check(gUIProgressBar != nullptr && gBarText == signup::GetText(Text::Waiting), "the game's progress bar while it waits");
    Check(gBarZ == 41, "the bar above the popup");
    Deliver(form, 0x100, -1, 0);
    Deliver(form, 0x804, 0, 0);
    Deliver(form, 0x802, 0x49, 0);
    Check(Count("CreateUIProgressBar") == 1 && !W(form).closed, "Create, Escape and Enter do nothing while it waits");
    const size_t boxes = gBoxTexts.size();
    Wait(form);
    Check(gBoxTexts.size() == boxes + 1 && gBoxTexts.back() == signup::GetText(Text::Taken), "a name taken");
    Check(gSent.size() == 0x14 + signup::kSignupSize && gSent[3] == signup::kTypeSignup && gSent[5] == 9 &&
              gSent[0x14 + 0x183] == 1,
          "the request: type 20, 0x184 bytes, the game's language, 13 or older");
    Check(SentField(0, 0x20) == u"Coaster Kid" && SentField(0x40, 0x20) == u"Wh33l-Spin!" &&
              SentField(0x80, 0x80) == u"kid@example.org",
          "the request's name, password and e-mail");
    Deliver(gBoxes.back(), 4, 0, 0);
    Answer(signup::kTypeSignupReply, 14);
    Check(Refusal(form, 0x4b, "weak") == signup::GetText(Text::WeakPassword), "too weak, from the server");
    Answer(signup::kTypeSignupReply, 15);
    Check(Refusal(form, 0x53, "e-mail full") == signup::GetText(Text::EmailFull), "the e-mail's accounts, from the server");

    // Enter and Tab go down the boxes; Enter in the last is Create
    const int passwordKeys = W(password).captured, confirmKeys = W(confirm).captured, emailKeys = W(email).captured,
              nameKeys = W(name).captured;
    Deliver(form, 0x802, 0x49, 0);
    Check(W(password).captured == passwordKeys + 1, "Enter in the name box goes to the password box");
    Deliver(form, 0x803, 0x4b, 0);
    Check(W(confirm).captured == confirmKeys + 1, "Tab goes to the confirmation");
    Deliver(form, 0x802, 0x51, 0);
    Check(W(email).captured == emailKeys + 1, "then the e-mail box");
    Deliver(form, 0x803, 0x53, 0);
    Check(W(name).captured == nameKeys + 1, "Tab in the e-mail box goes round to the name box");
    Answer(signup::kTypeSignupReply, 10);
    Deliver(form, 0x802, 0x53, 0);
    Check(gPosted.size() == 1, "Enter in the e-mail box is Create");
    Pump();
    Wait(form);
    Check(gBoxTexts.back() == signup::GetText(Text::Closed), "sign-ups closed");
    Deliver(gBoxes.back(), 4, 0, 0);

    // the account made: the notice, then its OK fills the login panel's boxes
    Answer(signup::kTypeSignupReply, 0, u"Coaster Kid");
    const size_t dialogs = gDialogs.size();
    Deliver(form, 0x100, -1, 0);
    Wait(form);
    Check(gBoxTexts.back() == u"Account created. You can log in now.", "the account made");
    Check(gDialogs.size() == dialogs, "no other popup");
    Check(!W(form).closed && gPosted.size() == 1, "the popup's close is posted, not done under its own frame");
    Pump();
    Check(W(form).closed && Count("DefaultDialogCallback 20") >= 1, "the popup closed");
    CheckFilled(form, 0x4b, u"Coaster Kid", u"Wh33l-Spin!", "sign-up");
    Check(Area(confirm).empty() && Area(email).empty(), "the confirmation and the e-mail are wiped");
}

void TestReset() {
    InterfaceWindow* form = OpenReset();
    Check(form != nullptr && gDialogs.back().modal == 1, "the code request popup opens over the login panel");
    if (form == nullptr) return;
    Check(Open() == nullptr && OpenReset() == nullptr, "only one popup at a time");
    InterfaceWindow* name = Child(form, 0x49);
    InterfaceWindow* email = Child(form, 0x53);
    InterfaceWindow* send = Child(form, 0x57);
    Check(W(Child(form, 0x4d)).text == signup::GetText(Text::ForgotTitle), "its title");
    Check(Child(form, 0x4b) == nullptr && Child(form, 0x4e) == nullptr, "no password box and no tick box");
    if (!name || !email || !send) {
        Check(false, "its boxes and button");
        return;
    }
    Check(W(name).capacity == 16 && W(email).capacity == 100 && W(name).client == "cursor" && W(email).client == "cursor",
          "the name and e-mail boxes");
    Check(Area(name).empty(), "an empty name box");
    Check(At(name, 632, 380, 1307, 473) && At(email, 632, 490, 1307, 583) && At(send, 808, 630, 1132, 700),
          "the boxes' and the button's places");
    Check(W(send).kind == W(Child(form, -1)).kind && W(send).drawObject == Object(0) &&
              W(send).text == signup::GetText(Text::SendCode),
          "the Send code button, as the login panel's");
    Check(W(name).captured == 1, "the name box has the keyboard");

    Type(name, u"Coaster Kid");
    Type(email, u"kid@");
    Check(Refusal(form, 0x53, "code e-mail", 0x57) == signup::GetText(Text::BadEmail), "an e-mail's form");
    Type(email, u"Kid@Example.org");
    Answer(signup::kTypeResetCodeReply, 1);
    Check(Refusal(form, 0x49, "no reset", 0x57) == u"Password reset isn't available yet. Ask on the Discord.",
          "the server sends no codes");
    Check(gBarText == signup::GetText(Text::Sending), "the code request's wait");
    Check(gSent.size() == 0x14 + signup::kResetCodeSize && gSent[3] == signup::kTypeResetCode &&
              SentField(0, 0x20) == u"Coaster Kid" && SentField(0x40, 0x80) == u"Kid@Example.org",
          "the code request: type 22, 0x140 bytes, the name and e-mail");
    Answer(signup::kTypeResetCodeReply, 2);
    Check(Refusal(form, 0x49, "code limit") == signup::GetText(Text::TooManyCodes), "too many code requests (OK is Send code)");
    const int emailKeys = W(email).captured;
    Deliver(form, 0x802, 0x49, 0);
    Check(W(email).captured == emailKeys + 1, "Enter in the name box goes to the e-mail box");

    // the answer: the notice, then its OK opens the new password popup
    Answer(signup::kTypeResetCodeReply, 0);
    const size_t dialogs = gDialogs.size();
    Deliver(form, 0x802, 0x53, 0);
    Pump();
    Wait(form);
    Check(gBoxTexts.back() == u"If that name and e-mail match, a code is on its way. Check your junk folder too.",
          "the same answer whether or not the name is there");
    Check(gBoxButtons.back() == 1, "a notice's OK");
    Pump();
    Check(W(form).closed && gDialogs.size() == dialogs, "the code request popup closed");
    Check(Area(email).empty(), "its e-mail wiped");
    Deliver(gBoxes.back(), 4, 0, 0);
    Check(gDialogs.size() == dialogs + 1, "the notice's OK opens the new password popup");
    if (gDialogs.size() != dialogs + 1) return;
    InterfaceWindow* set = gDialogs.back().window;
    InterfaceWindow* code = Child(set, 0x55);
    InterfaceWindow* password = Child(set, 0x4b);
    InterfaceWindow* confirm = Child(set, 0x51);
    InterfaceWindow* button = Child(set, 0x57);
    InterfaceWindow* note = Child(set, 0x50);
    Check(W(Child(set, 0x4d)).text == signup::GetText(Text::SetTitle), "its title");
    Check(Child(set, 0x49) == nullptr, "no name box (the name is the one asked for)");
    if (!code || !password || !confirm || !button || !note) {
        Check(false, "its boxes, note and button");
        return;
    }
    Check(W(code).capacity == 6 && W(password).capacity == 16 && W(confirm).capacity == 16, "6, 16 and 16 characters");
    Check(W(code).client == "cursor" && W(password).client == "password" && W(confirm).client == "password",
          "the passwords masked");
    Check(Text16(W(Child(code, 0x56)).clientText) == signup::GetText(Text::Code) &&
              Text16(W(Child(password, 0x4c)).clientText) == signup::GetText(Text::NewPassword) &&
              Text16(W(Child(confirm, 0x52)).clientText) == signup::GetText(Text::Confirm),
          "its labels");
    Check(Text16(W(note).clientText) == signup::GetText(Text::Warning), "the warning");
    Check(W(button).text == signup::GetText(Text::SetButton), "the Set password button");
    Check(At(code, 632, 334, 1307, 417) && At(password, 632, 423, 1307, 506) && At(confirm, 632, 512, 1307, 595) &&
              At(button, 808, 664, 1132, 734),
          "its places");
    Check(W(code).captured == 1, "the code box has the keyboard");

    Type(code, u"12a456");
    Check(Refusal(set, 0x55, "code form", 0x57) == signup::GetText(Text::BadCode), "a code that is not 6 digits");
    Type(code, u"123456");
    Type(password, u"L00p+Loop");
    Type(confirm, u"L00p+Lo0p");
    Check(Refusal(set, 0x51, "set mismatch", 0x57) == u"The passwords don't match.", "the passwords differ");
    Type(confirm, u"L00p+Loop");
    Answer(signup::kTypeResetSetReply, 3);
    Check(Refusal(set, 0x55, "wrong code", 0x57) == signup::GetText(Text::BadCode), "a wrong code, from the server");
    Check(gBarText == signup::GetText(Text::Setting), "the new password's wait");
    Check(gSent.size() == 0x14 + signup::kResetSetSize && gSent[3] == signup::kTypeResetSet &&
              SentField(0, 0x20) == u"Coaster Kid" && SentField(0x40, 8) == u"123456" &&
              SentField(0x50, 0x20) == u"L00p+Loop",
          "the new password: type 24, 0x90 bytes, the name, code and password");
    Answer(signup::kTypeResetSetReply, 4);
    Check(Refusal(set, 0x55, "dead code", 0x57) == signup::GetText(Text::DeadCode), "a dead code");
    Answer(signup::kTypeResetSetReply, 6);
    Check(Refusal(set, 0x4b, "set weak", 0x57) == signup::GetText(Text::WeakPassword), "too weak");
    const int passwordKeys = W(password).captured, confirmKeys = W(confirm).captured;
    Deliver(set, 0x803, 0x55, 0);
    Deliver(set, 0x802, 0x4b, 0);
    Check(W(password).captured == passwordKeys + 1 && W(confirm).captured == confirmKeys + 1,
          "Tab and Enter go down its boxes");

    Answer(signup::kTypeResetSetReply, 0, u"Coaster Kid");
    Deliver(set, 0x802, 0x51, 0);
    Pump();
    Wait(set);
    Check(gBoxTexts.back() == signup::GetText(Text::PasswordSet), "the password set");
    Pump();
    Check(W(set).closed, "the new password popup closed");
    CheckFilled(set, 0x4b, u"Coaster Kid", u"L00p+Loop", "reset");
    Check(Area(code).empty() && Area(confirm).empty(), "the code and the confirmation are wiped");
}

void TestCancel() {
    InterfaceWindow* form = Open();
    Check(form != nullptr, "the popup opens again");
    if (form == nullptr) return;
    Check(W(Child(form, 0x4e)).value == 0, "not ticked again");
    Check(Area(Child(form, 0x4b)).empty() && Area(Child(form, 0x53)).empty(), "empty boxes again");
    Type(Child(form, 0x4b), u"Wh33l-Spin!");
    const int keys = W(Child(gLogin, 0x49)).captured;
    Deliver(form, 0x100, -2, 0);
    Check(W(form).closed && W(Child(gLogin, 0x49)).captured == keys + 1, "Cancel closes, the login name box has the keyboard");
    Check(Area(Child(form, 0x4b)).empty(), "and the password typed is wiped");

    form = Open();
    Deliver(form, 0x804, 0, 0);
    Check(form != nullptr && W(form).closed, "Escape closes");
    form = OpenReset();
    Deliver(form, 0x100, -2, 0);
    Check(form != nullptr && W(form).closed, "Cancel closes the code request popup");

    // closed while the request is out (the login panel going): the bar goes, the answer is let go of
    form = Open();
    Type(Child(form, 0x49), u"Late Comer");
    Type(Child(form, 0x4b), u"Wh33l-Spin!");
    Type(Child(form, 0x51), u"Wh33l-Spin!");
    Type(Child(form, 0x53), u"late@example.org");
    Deliver(Child(form, 0x4e), 0x10005, 0, 0);
    boss::SetTransportMaker([] { return std::make_unique<Fake>(std::vector<uint8_t>{}, &gSent); });   // silence
    Deliver(form, 0x100, -1, 0);
    Check(gUIProgressBar != nullptr, "waiting");
    const int gone = Count("FuckOffUIProgressBar");
    Deliver(form, 4, 0, 0);
    Check(gUIProgressBar == nullptr && Count("FuckOffUIProgressBar") == gone + 1, "the bar goes with the popup");
    Check(Open() != nullptr, "and a new popup opens");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));   // the request's thread sees it was let go of
    boss::SetTransportMaker(nullptr);
}

}  // namespace

int main() {
    MakeVtable();
    TestLoginPanel();
    TestSignup();
    TestReset();
    TestCancel();
    boss::SetServer("");
    std::printf("%d of %d sign-up popup checks passed\n", gChecks - gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}

#else

int main() {
    std::printf("the sign-up popup is a TPW_PORT_FIX fix: nothing to check in this build\n");
    return 0;
}

#endif  // TPW_PORT_FIX
