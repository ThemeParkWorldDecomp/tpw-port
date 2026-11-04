// online_signup_ui.cpp: the online sign-up's and password reset's popups (include/platform/online_signup.h), behind
// TPW_PORT_FIX (the fix `signup`).
//
// Every window is the game's own: the popups are built from the login panel's window script (its frame, its edit boxes, its
// OK and Cancel buttons, its title), moved about and copied, with the password boxes masked as the login panel's and the
// game's tick box ("i_boxtick"); the texts use the login panel's fonts and colours, the wait the game's progress bar and a
// refusal or a notice the game's message box (UI_DialogBox, as the game's errors). The script's words are the image's
// (WindowScripts.inc): they are read and rearranged here at run time, never written down.
#include "platform/online_signup.h"

#ifdef TPW_PORT_FIX

#include "platform/online_boss.h"

#include "FS_Online.h"
#include "FS_OnlineLoginPanel.h"
#include "InterfaceWindow.h"
#include "TPMain.h"
#include "Text.h"
#include "UI_Base.h"
#include "ui_DrawObject.h"
#include "ui_ErrorDisplay.h"
#include "ui_controls.h"
#include "ui_main.h"

#include <cstring>
#include <memory>
#include <vector>

namespace tpw::online::signup {

namespace {

// ---- The window script, as a tree ------------------------------------------------------------------------------------------

// A window of a script (record 0) and what its level holds, in order: its own records (a draw object, a client rectangle,
// the help text words) and its children. Only the records the login panel has are known; anything else and the script is
// left as it is.
struct Node;
struct Item {
    std::vector<int16_t> words;   // a record, its number first; empty for a child
    std::vector<Node> child;      // the child, for record 0
};
struct Node {
    int16_t kind = 0;
    int32_t attributes = 0, id = 0;
    int16_t rect[4] = {};
    std::vector<Item> items;
};

int32_t Long(const int16_t* w) { return static_cast<int32_t>(static_cast<uint16_t>(w[0]) | static_cast<uint32_t>(w[1]) << 16); }

// One level, up to its end record (5). False for a record this does not know or a script that runs out first.
bool ParseLevel(const int16_t* w, size_t size, size_t& i, std::vector<Item>& items) {
    while (i < size) {
        const int16_t record = w[i];
        if (record == 5) {
            ++i;
            return true;
        }
        size_t words = 0;
        switch (record) {
            case 0: {
                if (i + 10 > size) return false;
                Node node;
                node.kind = w[i + 1];
                node.attributes = Long(w + i + 2);
                node.id = Long(w + i + 4);
                for (int k = 0; k < 4; ++k) node.rect[k] = w[i + 6 + k];
                i += 10;
                if (!ParseLevel(w, size, i, node.items)) return false;
                Item item;
                item.child.push_back(std::move(node));
                items.push_back(std::move(item));
                continue;
            }
            case 1:     // the draw object
            case 2:     // the client draw object
            case 17:    // the help text
            case 18: words = 3; break;
            case 3: words = 5; break;   // the client rectangle
            default: return false;
        }
        if (i + words > size) return false;
        items.push_back(Item{std::vector<int16_t>(w + i, w + i + words), {}});
        i += words;
    }
    return false;
}

void Write(const std::vector<Item>& items, std::vector<int16_t>& out) {
    for (const Item& item : items) {
        if (item.child.empty()) {
            out.insert(out.end(), item.words.begin(), item.words.end());
            continue;
        }
        const Node& node = item.child[0];
        out.push_back(0);
        out.push_back(node.kind);
        out.push_back(static_cast<int16_t>(node.attributes));
        out.push_back(static_cast<int16_t>(node.attributes >> 16));
        out.push_back(static_cast<int16_t>(node.id));
        out.push_back(static_cast<int16_t>(node.id >> 16));
        out.insert(out.end(), node.rect, node.rect + 4);
        Write(node.items, out);
        out.push_back(5);
    }
}

std::vector<int16_t> Script(const std::vector<Item>& top) {
    std::vector<int16_t> out;
    Write(top, out);
    out.push_back(5);
    return out;
}

Node* Find(std::vector<Item>& items, int32_t id) {
    for (Item& item : items) {
        if (item.child.empty()) continue;
        if (item.child[0].id == id) return &item.child[0];
        if (Node* found = Find(item.child[0].items, id)) return found;
    }
    return nullptr;
}

// Moves a window, its client rectangle and its children down by `dy` units.
void Shift(Node& node, int16_t dy) {
    node.rect[1] = static_cast<int16_t>(node.rect[1] + dy);
    node.rect[3] = static_cast<int16_t>(node.rect[3] + dy);
    for (Item& item : node.items) {
        if (!item.child.empty()) {
            Shift(item.child[0], dy);
        } else if (item.words[0] == 3) {
            item.words[2] = static_cast<int16_t>(item.words[2] + dy);
            item.words[4] = static_cast<int16_t>(item.words[4] + dy);
        }
    }
}

// Takes out a window's own draw object (record 1).
void DropDrawObject(Node& node) {
    std::vector<Item> kept;
    for (Item& item : node.items)
        if (!(item.child.empty() && item.words[0] == 1)) kept.push_back(std::move(item));
    node.items = std::move(kept);
}

// A child put in just before the panel's own draw object (the frame, its last record), as the script's children are.
void Insert(Node& panel, Node child) {
    size_t at = panel.items.size();
    for (size_t k = 0; k < panel.items.size(); ++k)
        if (panel.items[k].child.empty() && panel.items[k].words[0] == 1) at = k;
    Item item;
    item.child.push_back(std::move(child));
    panel.items.insert(panel.items.begin() + static_cast<std::ptrdiff_t>(at), std::move(item));
}

// A plain window (kind 1) or an image (kind 6) with the help text words the login panel's windows have.
Node Plain(int16_t kind, int32_t id, int16_t left, int16_t top, int16_t right, int16_t bottom, const Node& like) {
    Node node;
    node.kind = kind;
    node.attributes = like.attributes;
    node.id = id;
    node.rect[0] = left;
    node.rect[1] = top;
    node.rect[2] = right;
    node.rect[3] = bottom;
    for (const Item& item : like.items)
        if (item.child.empty() && (item.words[0] == 17 || item.words[0] == 18)) node.items.push_back(item);
    return node;
}

// The login panel's windows (the ids its code uses).
constexpr int32_t kPanel = 0x48, kNameBox = 0x49, kNameLabel = 0x4a, kPasswordBox = 0x4b, kPasswordLabel = 0x4c, kTitle = 0x4d;
constexpr int32_t kOk = -1, kCancel = -2;
// The popups' own.
constexpr int32_t kTick = 0x4e, kTickLabel = 0x4f, kNote = 0x50, kConfirmBox = 0x51, kConfirmLabel = 0x52, kEmailBox = 0x53,
                  kEmailLabel = 0x54, kCodeBox = 0x55, kCodeLabel = 0x56, kAction = 0x57;

// The login panel's script as read (its top level), once LoginPanelScript has seen it in the form expected.
std::vector<Item>& Original() {
    static std::vector<Item> original;
    return original;
}

bool Expected(std::vector<Item>& top) {
    if (top.size() != 1 || top[0].child.empty() || top[0].child[0].id != kPanel) return false;
    for (int32_t id : {kOk, kCancel, kNameBox, kPasswordBox, kTitle})
        if (Find(top, id) == nullptr) return false;
    Node* name = Find(top, kNameBox);
    bool client = false;
    for (const Item& item : name->items) client = client || (item.child.empty() && item.words[0] == 3);
    return client && Find(top, kNameLabel) != nullptr && Find(top, kPasswordLabel) != nullptr;
}

// The scripts made, kept for as long as the game runs (a window script is read while the window is built only, but the
// game's own are static too).
std::vector<int16_t>& Kept(int which) {
    static std::vector<int16_t> scripts[4];
    return scripts[which];
}

// Puts a window at a new place (its children and client rectangle as they were).
void Move(Node& node, int16_t left, int16_t top, int16_t right, int16_t bottom) {
    node.rect[0] = left;
    node.rect[1] = top;
    node.rect[2] = right;
    node.rect[3] = bottom;
}

// Puts an edit box of the login panel's (its stretching frame, its client rectangle and its label, record 3 and its one
// child) at a new place: the label from `labelLeft` to `labelRight` and the text from `textLeft` to the box's right less the
// original's margin, both as tall as the original's and centred in the box.
void Place(Node& box, int16_t left, int16_t top, int16_t right, int16_t bottom, int16_t labelLeft, int16_t labelRight,
           int16_t textLeft) {
    int16_t* client = nullptr;
    for (Item& item : box.items)
        if (item.child.empty() && item.words[0] == 3) client = &item.words[1];
    const int16_t margin = static_cast<int16_t>(box.rect[2] - client[2]);
    const int16_t height = static_cast<int16_t>(client[3] - client[1]);
    const int16_t textTop = static_cast<int16_t>(top + (bottom - top - height) / 2);
    Move(box, left, top, right, bottom);
    client[0] = textLeft;
    client[1] = textTop;
    client[2] = static_cast<int16_t>(right - margin);
    client[3] = static_cast<int16_t>(textTop + height);
    for (Item& item : box.items)
        if (!item.child.empty()) Move(item.child[0], labelLeft, textTop, labelRight, static_cast<int16_t>(textTop + height));
}

// A copy of an edit box with new ids for it and its label.
Node CopyBox(const Node& box, int32_t id, int32_t labelId) {
    Node copy = box;
    copy.id = id;
    for (Item& item : copy.items)
        if (!item.child.empty()) item.child[0].id = labelId;
    return copy;
}

// A labelled button in the game's style: the OK button's kind of window with the toggle's art (Decorate).
Node Button(const std::vector<Item>& top, int32_t id, int16_t left, int16_t top_, int16_t right, int16_t bottom) {
    Node button = *Find(const_cast<std::vector<Item>&>(top), kOk);
    DropDrawObject(button);
    button.id = id;
    Move(button, left, top_, right, bottom);
    return button;
}

// ---- The popups' state -----------------------------------------------------------------------------------------------------

char16_t gEmpty[1] = {};
char16_t gName[kNameMax + 1] = {};             // the popups' boxes
char16_t gPassword[kPasswordMax + 1] = {};
char16_t gConfirm[kPasswordMax + 1] = {};
char16_t gEmail[kEmailMax + 1] = {};
char16_t gCode[kCodeDigits + 1] = {};
char16_t gDoneName[kFieldUnits] = {};          // the account's name from the server, for the login panel's box
bool gAdult = false;
bool gWaiting = false;
Kind gKind = Kind::Signup;                     // the open popup's
InterfaceWindow* gForm = nullptr;              // the popup, while it is open
InterfaceWindow* gNotice = nullptr;            // a message box of the popups', while it is open
int16_t gFormDepth = 0;                        // the depth of a popup opened over the login panel
std::unique_ptr<Job> gJob;

// What a popup's message box does once it closes.
enum class After { Focus, OpenSet, FillLogin };
After gAfter = After::Focus;
int32_t gAfterBox = kNameBox;                  // for Focus: the popup's box that gets the keyboard

void WipeAll() {
    Wipe(gPassword, kPasswordMax + 1);
    Wipe(gConfirm, kPasswordMax + 1);
    Wipe(gEmail, kEmailMax + 1);
    Wipe(gCode, kCodeDigits + 1);
    Wipe(gDoneName, kFieldUnits);
}

void Label(InterfaceWindow* window, const char16_t* text) {
    window->SetClientDrawObject(new InterfaceTextObjectSprite(nullptr));
    window->SetFont(GetFontTableEntry(7));
    window->SetClientDrawText(const_cast<char16_t*>(text));
}

void Title(InterfaceWindow* window, Text text) {
    window->SetClientDrawObject(new InterfaceTitleTextObject(nullptr));
    window->SetFont(GetFontTableEntry(5));
    window->SetFontJust(1, 1);
    window->SetText(const_cast<char16_t*>(GetText(text)));
    UI_TITLE_COLOUR(window);
}

// A line of white words in the labels' font, wrapped.
void Words(InterfaceWindow* window, Text text, int32_t centred) {
    window->SetClientDrawObject(new InterfaceTextObjectSprite(nullptr));
    window->SetFont(GetFontTableEntry(7));
    window->SetFontColour(0xff, 0xff, 0xff, 0xff);
    window->SetFontJust(centred, 1);
    window->SetFontWrap(1, 1);
    window->SetClientDrawText(const_cast<char16_t*>(GetText(text)));
}

// As the login panel's "Create account": the toggle's art and a centred black label in font 6 (the news panel's tabs).
void Decorate(InterfaceWindow* button, Text text) {
    if (button == nullptr) return;
    button->SetDrawObject(DrawObjectLibrary::FindDrawObject(const_cast<char*>("b_toggle")));
    button->SetClientDrawObject(new InterfaceTextObjectSprite(nullptr));
    button->SetFontJust(1, 1);
    button->SetFont(GetFontTableEntry(6));
    button->SetFontColour(0, 0, 0, 0xff);
    button->SetClientDrawText(nullptr);
    button->SetText(const_cast<char16_t*>(GetText(text)));
}

void Focus(InterfaceWindow* panel, int32_t id) {
    if (panel == nullptr) return;
    if (InterfaceWindow* box = panel->GetWindow(id)) box->CaptureKb(0);
}

// The keyboard for a box with the caret after its text and nothing selected (as a click gives it, CaptureKb(1)), so the next
// key adds to the text instead of replacing it (Focus's CaptureKb(0), the game's Tab, selects it all).
void FocusAtEnd(InterfaceWindow* panel, int32_t id) {
    if (panel == nullptr) return;
    InterfaceWindow* box = panel->GetWindow(id);
    if (box == nullptr) return;
    box->CaptureKb(1);
    static_cast<InterfaceEditBox*>(box)->MoveCursor(-5, 1, -1);
}

// The popups' boxes in their order (Tab and Enter go down it).
std::vector<int32_t> Order(Kind kind) {
    switch (kind) {
        case Kind::ResetCode: return {kNameBox, kEmailBox};
        case Kind::ResetSet: return {kCodeBox, kPasswordBox, kConfirmBox};
        default: return {kNameBox, kPasswordBox, kConfirmBox, kEmailBox};
    }
}

// The box a refusal is about, which gets the keyboard once its message box closes (the text kept to be corrected).
int32_t BoxFor(Outcome outcome) {
    switch (outcome) {
        case Outcome::BadPassword:
        case Outcome::WeakPassword: return kPasswordBox;
        case Outcome::Mismatch: return kConfirmBox;
        case Outcome::BadEmail:
        case Outcome::EmailFull: return kEmailBox;
        case Outcome::BadCode:
        case Outcome::DeadCode: return kCodeBox;
        default: return Order(gKind).front();
    }
}

void OpenSet();

// What a popup's message box leaves to do once it closes.
void AfterNotice() {
    gNotice = nullptr;
    switch (gAfter) {
        case After::Focus: FocusAtEnd(gForm, gAfterBox); break;
        case After::OpenSet: OpenSet(); break;
        case After::FillLogin:
            if (InterfaceWindow* panel = gPortOnlineLoginPanel) {
                if (InterfaceWindow* box = panel->GetWindow(kNameBox)) box->SetText(gDoneName);
                if (InterfaceWindow* box = panel->GetWindow(kPasswordBox)) box->SetText(gPassword);
                Focus(panel, kPasswordBox);   // (Enter there logs in)
            }
            WipeAll();
            break;
    }
}

// The popups' message boxes: the game's (UI_DialogBox, as its errors), one button, a refusal's the exit sign and a notice's
// the OK; once one closes, what gAfter says.
int32_t NoticeMsg(InterfaceWindow* window, int32_t message, int32_t a, int32_t b) {
    const int32_t answer = DialogBoxCallback(window, message, a, b);
    if (message == 0x14 && window == gNotice) AfterNotice();
    return answer;
}

void Notice(const char16_t* text, bool refusal, After after) {
    dlgboxparams params;
    params.mText = text;
    params.mRed = 0xff;
    params.mGreen = 0xff;
    params.mBlue = 0xff;
    params.mValue8 = 4;
    params.mValueC = 0;
    gAfter = after;
    gNotice = UI_DialogBox(params, dlgbutton(refusal ? 2 : 1), nullptr, dlgbutton(0), nullptr, dlgbutton(0), nullptr, false);
    if (gNotice != nullptr) gNotice->SetCallbackFunction(NoticeMsg);
    else AfterNotice();
}

void Refuse(Outcome outcome) {
    gAfterBox = BoxFor(outcome);
    Notice(RefusalText(outcome), true, After::Focus);
}

void SetTick(bool on) {
    gAdult = on;
    if (gForm == nullptr) return;
    if (InterfaceWindow* tick = gForm->GetWindow(kTick)) tick->SetValue(on ? 1 : 0);
}

// The tick box and its words: a press of the left button ticks or unticks it.
int32_t TickMsg(InterfaceWindow* window, int32_t message, int32_t a, int32_t b) {
    if (message == 0x10005 && a == 0 && !gWaiting) SetTick(!gAdult);
    return window->CallDefaultWindowHandler(message, a, b);
}

bool Same(const char16_t* a, const char16_t* b) {
    for (; *a != 0 || *b != 0; ++a, ++b)
        if (*a != *b) return false;
    return true;
}

// The popup's button (OK, or its labelled one): the checks, then the request.
void Send() {
    Request request;
    request.kind = gKind;
    request.name = gName;
    request.adult = gAdult;
    if (gKind != Kind::ResetSet) request.email = gEmail;
    if (gKind != Kind::ResetCode) request.password = gPassword;
    if (gKind == Kind::ResetSet) request.code = gCode;
    Outcome outcome = Check(request);
    // the two passwords: after the rule, before the rest
    if (gKind != Kind::ResetCode && !Same(gPassword, gConfirm)) {
        if (outcome == Outcome::Made || outcome == Outcome::BadEmail || outcome == Outcome::NoAge) outcome = Outcome::Mismatch;
    }
    if (outcome == Outcome::Made && !boss::Active()) outcome = Outcome::Unreachable;
    if (outcome != Outcome::Made) {
        Wipe(request);
        Refuse(outcome);
        return;
    }
    gJob = std::make_unique<Job>();
    gJob->Start(boss::Server(), static_cast<uint16_t>(gOnlineConfig.mLoginPort), request,
                static_cast<uint16_t>(gText.GetLanguageId()));
    Wipe(request);
    gWaiting = true;
    const Text bar = gKind == Kind::Signup ? Text::Waiting : gKind == Kind::ResetCode ? Text::Sending : Text::Setting;
    CreateUIProgressBar(const_cast<char16_t*>(GetText(bar)));
    // above the popup, which is above everything (the bar's own depth is under a modal dialog's)
    if (gUIProgressBar != nullptr) gUIProgressBar->setZ(static_cast<int16_t>(UI_GetHighestZ(nullptr, nullptr) + 1));
}

// Once a frame while the request is out: its answer, when it came.
void Poll(InterfaceWindow* window) {
    if (!gWaiting || gJob == nullptr || !gJob->Done()) return;
    gWaiting = false;
    FuckOffUIProgressBar();
    Result result = gJob->Take();
    gJob.reset();
    if (result.outcome != Outcome::Made) {
        Refuse(result.outcome);
        return;
    }
    switch (gKind) {
        case Kind::ResetCode:
            Wipe(gEmail, kEmailMax + 1);
            Notice(GetText(Text::Sent), false, After::OpenSet);
            break;
        case Kind::Signup:
        case Kind::ResetSet:
            Wipe(gDoneName, kFieldUnits);
            for (size_t k = 0; k < result.name.size() && k + 1 < kFieldUnits; ++k) gDoneName[k] = result.name[k];
            Notice(GetText(gKind == Kind::Signup ? Text::Made : Text::PasswordSet), false, After::FillLogin);
            break;
    }
    // (posted: the frame walk is in this window's handler, and a close now would delete it under the walk)
    UI_PostMessage(window, 4, 0, 0);
}

// The popups' message function, the login panel's in form: the action (OK or the labelled button), Cancel, Enter to the
// next box or the action, Tab to the next box, Escape for Cancel; while the request is out only the wait.
int32_t FormMsg(InterfaceWindow* window, int32_t message, int32_t a, int32_t b) {
    switch (message) {
        case 0x1e:
            Poll(window);
            break;
        case 0x14: {
            gForm = nullptr;
            gJob.reset();   // a request still out is let go of (its thread gives up)
            if (gWaiting) FuckOffUIProgressBar();
            gWaiting = false;
            const int32_t answer = DefaultDialogCallback(window, message, a, b);
            if (gNotice == nullptr) {
                WipeAll();
                Focus(gPortOnlineLoginPanel, kNameBox);
            }
            return answer;
        }
        case 0x100:
            if (gWaiting) return 0;
            if (a == kOk || a == kAction) {
                Send();
                return 0;
            }
            if (a == kCancel) {
                UI_SendMessage(window, 4, 0, 0);
                return 0;
            }
            break;
        case 0x802:
        case 0x803: {
            if (gWaiting) return 0;
            const std::vector<int32_t> order = Order(gKind);
            size_t at = 0;
            while (at < order.size() && order[at] != a) ++at;
            if (message == 0x802 && at + 1 >= order.size()) UI_PostMessage(window, 0x100, kOk, 0);
            else Focus(window, order[(at + 1) % order.size()]);
            return 0;
        }
        case 0x804:
            if (gWaiting) return 0;
            UI_SendMessage(window, 4, 0, 0);
            return 0;
        default: break;
    }
    return DefaultDialogCallback(window, message, a, b);
}

// The popups' scripts, made from the login panel's (its frame, boxes, title, OK and Cancel), the boxes moved and copied.
// The sign-up: the name, the password, its confirmation, the warning, the e-mail and the tick box, one under the other (the
// boxes less tall than the login panel's to fit).
int16_t* SignupScript() {
    std::vector<int16_t>& kept = Kept(1);
    if (kept.empty()) {
        std::vector<Item> top = Original();
        Node& panel = *Find(top, kPanel);
        Node& title = *Find(top, kTitle);
        Node& name = *Find(top, kNameBox);
        Node& password = *Find(top, kPasswordBox);
        Node email = CopyBox(name, kEmailBox, kEmailLabel);
        Node confirm = CopyBox(password, kConfirmBox, kConfirmLabel);
        Move(title, title.rect[0], 262, title.rect[2], 318);
        Place(name, 632, 324, 1307, 396, 656, 946, 953);
        Place(password, 632, 402, 1307, 474, 656, 946, 953);
        Place(confirm, 632, 480, 1307, 552, 656, 946, 953);
        Place(email, 632, 610, 1307, 682, 656, 900, 908);
        Insert(panel, std::move(confirm));
        Insert(panel, Plain(1, kNote, 600, 556, 1340, 606, title));
        Insert(panel, std::move(email));
        Insert(panel, Plain(6, kTick, 656, 690, 716, 750, title));
        Insert(panel, Plain(1, kTickLabel, 730, 685, 1300, 755, title));
        kept = Script(top);
    }
    return kept.data();
}

// The code request: the name and the e-mail, the "Send code" button.
int16_t* CodeScript() {
    std::vector<int16_t>& kept = Kept(2);
    if (kept.empty()) {
        std::vector<Item> top = Original();
        Node& panel = *Find(top, kPanel);
        Node email = CopyBox(*Find(top, kNameBox), kEmailBox, kEmailLabel);
        std::vector<Item> items;   // without the password box
        for (Item& item : panel.items)
            if (item.child.empty() || item.child[0].id != kPasswordBox) items.push_back(std::move(item));
        panel.items = std::move(items);
        Node& title = *Find(top, kTitle);
        Move(title, title.rect[0], 290, title.rect[2], 360);
        Place(*Find(top, kNameBox), 632, 380, 1307, 473, 656, 946, 953);
        Place(email, 632, 490, 1307, 583, 656, 900, 908);
        Insert(panel, std::move(email));
        Insert(panel, Button(top, kAction, 808, 630, 1132, 700));
        kept = Script(top);
    }
    return kept.data();
}

// The new password: the code, the password and its confirmation, the warning, the "Set password" button.
int16_t* SetScript() {
    std::vector<int16_t>& kept = Kept(3);
    if (kept.empty()) {
        std::vector<Item> top = Original();
        Node& panel = *Find(top, kPanel);
        Node& title = *Find(top, kTitle);
        Node& name = *Find(top, kNameBox);
        Node& password = *Find(top, kPasswordBox);
        Node confirm = CopyBox(password, kConfirmBox, kConfirmLabel);
        name.id = kCodeBox;
        for (Item& item : name.items)
            if (!item.child.empty()) item.child[0].id = kCodeLabel;
        Move(title, title.rect[0], 268, title.rect[2], 328);
        Place(name, 632, 334, 1307, 417, 656, 946, 953);
        Place(password, 632, 423, 1307, 506, 656, 946, 953);
        Place(confirm, 632, 512, 1307, 595, 656, 946, 953);
        Insert(panel, std::move(confirm));
        Insert(panel, Plain(1, kNote, 600, 598, 1340, 656, title));
        Insert(panel, Button(top, kAction, 808, 664, 1132, 734));
        kept = Script(top);
    }
    return kept.data();
}

void EditBox(InterfaceWindow* window, char16_t* buffer, size_t capacity, bool masked) {
    InterfaceEditBox* box = static_cast<InterfaceEditBox*>(window);
    box->SetStaticTextArea(buffer, static_cast<int32_t>(capacity));
    if (masked) box->SetClientDrawObject(new InterfacePasswordTextObject(nullptr));
    else box->SetClientDrawObject(new InterfaceCursorPosTextObjectSprite(nullptr));
    box->SetFont(GetFontTableEntry(7));
    box->SetFontColour(0xff, 0xff, 0xff, 0xff);
    box->SetFontJust(0, 0);
    box->SetCursorDrawObject(DrawObjectLibrary::FindDrawObject(const_cast<char*>("hilight")));
    box->SetText(gEmpty);
}

// A popup's box and its label. The label is drawn first: a game string (UIMain_GetString) is in the game's shared text
// buffer, which a masked box's text object fills with its stars.
void Box(InterfaceWindow* form, int32_t id, int32_t labelId, char16_t* buffer, size_t capacity, bool masked,
         const char16_t* label) {
    InterfaceWindow* box = form->GetWindow(id);
    Label(box->GetWindow(labelId), label);
    EditBox(box, buffer, capacity, masked);
}

InterfaceWindow* OpenForm(Kind kind, int16_t* script) {
    if (gForm != nullptr || gNotice != nullptr || Original().empty() || script == nullptr) return nullptr;
    gForm = Dialog(script, FormMsg, 1);
    if (gForm == nullptr) return nullptr;
    // The new password popup opens as the code request's message box closes, which still counts then, so it takes the code
    // request popup's depth: opened above that message box, the message boxes after it were never seen.
    if (InterfaceWindow* holder = gForm->mParent) {
        if (kind == Kind::ResetSet) holder->setZ(gFormDepth);
        else gFormDepth = holder->mZ;
    }
    gKind = kind;
    gWaiting = false;
    return gForm;
}

void OpenSet() {
    Wipe(gPassword, kPasswordMax + 1);
    Wipe(gConfirm, kPasswordMax + 1);
    Wipe(gCode, kCodeDigits + 1);
    InterfaceWindow* form = OpenForm(Kind::ResetSet, SetScript());
    if (form == nullptr) return;
    Title(form->GetWindow(kTitle), Text::SetTitle);
    Box(form, kCodeBox, kCodeLabel, gCode, kCodeDigits, false, GetText(Text::Code));
    Box(form, kPasswordBox, kPasswordLabel, gPassword, kPasswordMax, true, GetText(Text::NewPassword));
    Box(form, kConfirmBox, kConfirmLabel, gConfirm, kPasswordMax, true, GetText(Text::Confirm));
    Words(form->GetWindow(kNote), Text::Warning, 1);
    Decorate(form->GetWindow(kAction), Text::SetButton);
    form->GetWindow(kCodeBox)->CaptureKb(0);
}

}  // namespace

int16_t* LoginPanelScript(int16_t* original, size_t words) {
    std::vector<int16_t>& kept = Kept(0);
    if (!kept.empty()) return kept.data();
    std::vector<Item> top;
    size_t i = 0;
    if (!ParseLevel(original, words, i, top) || !Expected(top)) return original;
    Original() = top;
    // The boxes go up to make room for the two buttons under them, side by side across the bottom of the frame.
    Node& panel = *Find(top, kPanel);
    Shift(*Find(top, kNameBox), -40);
    Shift(*Find(top, kPasswordBox), -40);
    Insert(panel, Button(top, kCreateButtonId, 632, 664, 962, 734));
    Insert(panel, Button(top, kForgotButtonId, 977, 664, 1307, 734));
    kept = Script(top);
    return kept.data();
}

void DecorateLoginPanel(InterfaceWindow* panel) {
    if (panel == nullptr || panel->GetWindow(kCreateButtonId) == nullptr) return;
    Decorate(panel->GetWindow(kCreateButtonId), Text::Button);
    Decorate(panel->GetWindow(kForgotButtonId), Text::ForgotButton);
}

int32_t OpenPopup(InterfaceWindow* panel) {
    (void)panel;
    InterfaceWindow* form = OpenForm(Kind::Signup, Original().empty() ? nullptr : SignupScript());
    if (form == nullptr) return 1;
    WipeAll();
    Title(form->GetWindow(kTitle), Text::Title);
    Box(form, kNameBox, kNameLabel, gName, kNameMax, false, UIMain_GetString(0xed));
    Box(form, kPasswordBox, kPasswordLabel, gPassword, kPasswordMax, true, UIMain_GetString(0xee));
    Box(form, kConfirmBox, kConfirmLabel, gConfirm, kPasswordMax, true, GetText(Text::Confirm));
    Box(form, kEmailBox, kEmailLabel, gEmail, kEmailMax, false, GetText(Text::Email));
    Words(form->GetWindow(kNote), Text::Warning, 1);
    InterfaceWindow* tick = form->GetWindow(kTick);
    tick->SetDrawObject(DrawObjectLibrary::FindDrawObject(const_cast<char*>("i_boxtick")));
    tick->SetCallbackFunction(TickMsg);
    InterfaceWindow* words = form->GetWindow(kTickLabel);
    Words(words, Text::Adult, 0);
    words->SetCallbackFunction(TickMsg);
    SetTick(false);
    form->GetWindow(kNameBox)->CaptureKb(0);
    return 1;
}

int32_t OpenResetPopup(InterfaceWindow* panel) {
    (void)panel;
    InterfaceWindow* form = OpenForm(Kind::ResetCode, Original().empty() ? nullptr : CodeScript());
    if (form == nullptr) return 1;
    WipeAll();
    Title(form->GetWindow(kTitle), Text::ForgotTitle);
    Box(form, kNameBox, kNameLabel, gName, kNameMax, false, UIMain_GetString(0xed));
    Box(form, kEmailBox, kEmailLabel, gEmail, kEmailMax, false, GetText(Text::Email));
    Decorate(form->GetWindow(kAction), Text::SendCode);
    form->GetWindow(kNameBox)->CaptureKb(0);
    return 1;
}

}  // namespace tpw::online::signup

#endif  // TPW_PORT_FIX
