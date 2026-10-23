// postcard_dispatch.cpp: the calls of SendPostcard (0x101b0f9c) and AddressesValid (0x101b0d88) that unit v5884 declared as free
// functions so that its tests could record them (include/ui_Postcard.h, namespace sendpostcard_callee), and the e-mail callees on the
// way from there to the outbox (include/email.h: email_callee, emailcb_callee, outbox_callee, sendbody_callee, outboxwrite_callee).
// In the launcher they are the calls themselves: the translated constructors, Postcard::save, the window's SetText, the C library.
//
// The MSL strings and lists of the e-mail. Email and its Attachment keep the Mac's layout as 32-bit words (include/email.h), and the
// translated readers take a string word as the arena offset of a 16-byte representation (len +0, cap +4, refs +8, the arena offset
// of the characters +0xc) and a list as three words (+0 size, +4 the sentinel's prev, +8 its next; a node is prev +0, next +4, the
// string word +8). The functions below build those in the arena. The library's own translations (src/TPMain_std_basic_string_
// basic_string.cpp, src/game/MslSupport.cpp) work on native-pointer bodies,
// so they cannot be called on these words. A representation here is never shared (the copies copy the characters).
//
// One caller passes host objects instead: Email::SendToOutbox fills the Outbox, whose strings and address list are std::string and
// std::list<std::string> (include/email.h), through the same assign, erase and insert. The only Outbox is the one in gUserInfo, so
// an address inside it is taken as a host object, any other as a word.
//
// MSL's wcstok, wcschr and wcscmp are imports the test libraries fake; the launcher's are here (not in platform/msl.cpp, which those
// test libraries link). MSL's wcstok keeps its place in a static: FillUpgradeBox passes a stale third argument
// (include/ui_controls.h). wcscmp compares the characters unsigned (AddToAddressField, the list sort, InterfaceTextBox's ==).
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <new>
#include <string>

#include <unistd.h>

#include "FS_Online.h"
#include "FS_UserInfo.h"
#include "InterfaceWindow.h"
#include "Postcard.h"
#include "email.h"
#include "platform/arena.h"
#include "rsse.h"
#include "ui_Postcard.h"
#include "ui_controls.h"
#include "ui_main.h"
#include "log_file.h"   // the failure reports go to the log file too

namespace {

// The Mac allocates its own sizes (0xd4 for CFSUserInfo, 0x404 for CFSOnlineSystem); the host's objects are larger, so every block
// has room for the largest of them. Arena memory, because the e-mail's words hold the addresses (and the Email's own: its list
// sentinel is an arena offset). Zeroed, which operator new is not; nothing reads a field before it is written.
constexpr size_t kHostRoom = std::max<size_t>({0x400, sizeof(CFSUserInfo), sizeof(CFSOnlineSystem)});

void* MacNew(uint32_t size) {
    void* block = tpw::ArenaAllocZeroed(std::max<size_t>(size, kHostRoom));
    if (block == nullptr) {
        ::tpw::LogRawF("postcard_dispatch: the arena is used up (operator new of %u bytes)\n", static_cast<unsigned>(size));
        std::abort();
    }
    return block;
}

uint32_t& Word(uint32_t offset) { return *reinterpret_cast<uint32_t*>(tpw::gArenaBase + offset); }
uint32_t& Word(void* address) { return *static_cast<uint32_t*>(address); }

struct WordRep {
    uint32_t len;    // 0x00  characters
    uint32_t cap;    // 0x04
    uint32_t refs;   // 0x08
    uint32_t data;   // 0x0c  arena offset of the characters
};

WordRep* Rep(uint32_t word) { return reinterpret_cast<WordRep*>(tpw::gArenaBase + word); }
uint8_t* Data(const WordRep* rep) { return tpw::gArenaBase + rep->data; }

// A representation of n characters of `unit` bytes, terminated.
uint32_t NewRep(const void* chars, uint32_t n, uint32_t unit) {
    WordRep* rep = static_cast<WordRep*>(MacNew(sizeof(WordRep)));
    const uint32_t cap = std::max<uint32_t>(n, 3);
    uint8_t* data = static_cast<uint8_t*>(tpw::ArenaAllocZeroed((cap + 1) * unit));
    if (data == nullptr) {
        ::tpw::LogRawF("postcard_dispatch: the arena is used up (a string of %u characters)\n", static_cast<unsigned>(n));
        std::abort();
    }
    if (n != 0) std::memcpy(data, chars, static_cast<size_t>(n) * unit);
    rep->len = n;
    rep->cap = cap;
    rep->refs = 1;
    rep->data = tpw::ArenaOffset(data);
    return tpw::ArenaOffset(rep);
}

void FreeRep(uint32_t word) {
    if (word == 0) return;
    WordRep* rep = Rep(word);
    tpw::ArenaFree(Data(rep));
    tpw::ArenaFree(rep);
}

// Assign and append make the new representation before the old one goes, so the text may come from the string itself.
void Assign(void* str, const void* text, uint32_t n, uint32_t unit) {
    const uint32_t old = Word(str);
    Word(str) = NewRep(text, n, unit);
    FreeRep(old);
}

void Append(void* str, const char* text, uint32_t n) {
    const uint32_t old = Word(str);
    const WordRep* rep = Rep(old);
    std::string joined(reinterpret_cast<const char*>(Data(rep)), rep->len);
    joined.append(text, n);
    Word(str) = NewRep(joined.data(), static_cast<uint32_t>(joined.size()), 1);
    FreeRep(old);
}

const char* Chars(uint32_t word) { return reinterpret_cast<const char*>(Data(Rep(word))); }

// The list at `list`: the sentinel is the node at list + 4.
uint32_t Sentinel(void* list) { return tpw::ArenaOffset(list) + 4; }

void InitList(void* list) {
    Word(list) = 0;
    Word(Sentinel(list)) = Sentinel(list);
    Word(Sentinel(list) + 4) = Sentinel(list);
}

// A copy of the string goes into a new node before `pos`.
void ListInsertBefore(void* list, uint32_t pos, const void* str) {
    const uint32_t node = tpw::ArenaOffset(MacNew(12));
    const WordRep* rep = Rep(Word(const_cast<void*>(str)));
    Word(node + 8) = NewRep(Data(rep), rep->len, 1);
    const uint32_t prev = Word(pos);
    Word(prev + 4) = node;
    Word(node) = prev;
    Word(pos) = node;
    Word(node + 4) = pos;
    Word(list) += 1;
}

// The nodes from `first` up to `last` go, with their strings.
void ListEraseRange(void* list, uint32_t first, uint32_t last) {
    if (first == last) return;
    const uint32_t before = Word(first);
    Word(before + 4) = last;
    Word(last) = before;
    for (uint32_t node = first; node != last;) {
        const uint32_t next = Word(node + 4);
        FreeRep(Word(node + 8));
        tpw::ArenaFree(tpw::gArenaBase + node);
        Word(list) -= 1;
        node = next;
    }
}

// The substring [pos, pos + min(n, size - pos)) of the string whose word is at `src`, as a new representation. The Mac throws
// std::out_of_range for a pos beyond the end (0x101b2918, 0x10001c80); the throw is a log here and the substring is empty.
uint32_t NewSubRep(const void* src, uint32_t pos, uint32_t n) {
    const uint32_t word = Word(const_cast<void*>(src));
    if (word == 0) return NewRep(nullptr, 0, 1);
    const WordRep* rep = Rep(word);
    if (pos > rep->len) {
        ::tpw::LogRawF("postcard_dispatch: string substring: pos %u > length %u (the Mac throws out_of_range)\n",
                       static_cast<unsigned>(pos), static_cast<unsigned>(rep->len));
        pos = rep->len;
    }
    return NewRep(Data(rep) + pos, std::min<uint32_t>(n, rep->len - pos), 1);
}

bool InUserOutbox(const void* address) {
    if (gUserInfo == nullptr) return false;
    const uint8_t* outbox = reinterpret_cast<const uint8_t*>(&gUserInfo->mOutbox);
    const uint8_t* p = static_cast<const uint8_t*>(address);
    return p >= outbox && p < outbox + sizeof(Outbox);
}

void AssignText(void* str, const char* text, uint32_t length) {
    if (InUserOutbox(str)) {
        static_cast<std::string*>(str)->assign(text, length);
        return;
    }
    Assign(str, text, length, 1);
}

void MakeText(void* str, const char* text) { Word(str) = NewRep(text, static_cast<uint32_t>(std::strlen(text)), 1); }

// The flag is -1 at every call (the string is a member or a local, never deleted itself).
void DropText(void* str, int16_t flag) {
    (void)flag;
    FreeRep(Word(str));
    Word(str) = 0;
}

// Email::SendToOutbox inserts before the end of the Outbox's host list (where is the list itself); the e-mail inserts before its
// sentinel (where is the sentinel's address).
void InsertText(void* list, void* where, const void* str) {
    if (InUserOutbox(list)) {
        static_cast<std::list<std::string>*>(list)->push_back(Chars(Word(const_cast<void*>(str))));
        return;
    }
    ListInsertBefore(list, tpw::ArenaOffset(where), str);
}

}  // namespace

namespace sendpostcard_callee {

void* New(uint32_t size) { return MacNew(size); }

// MSL's sprintf with the conversions the game's format strings use; long is 32 bits on the Mac, and %ls takes the game's 16-bit
// characters (each one byte of text, as MSL's C locale converts them).
int32_t Sprintf(char* dst, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    std::string out;
    for (const char* p = format; *p != 0;) {
        if (*p != '%') {
            out += *p++;
            continue;
        }
        const char* spec = p++;
        while (*p != 0 && std::strchr("-+ #0123456789.", *p) != nullptr) ++p;
        std::string flags(spec, static_cast<size_t>(p - spec));
        bool wide = false;
        while (*p == 'l' || *p == 'h') wide = *p++ == 'l';
        const char conversion = *p;
        if (conversion == 0) break;
        ++p;
        const std::string piece = flags + conversion;
        char buffer[512];
        switch (conversion) {
            case '%':
                out += '%';
                break;
            case 's': {
                std::string text;
                if (wide) {
                    for (const char16_t* w = va_arg(ap, const char16_t*); *w != 0; ++w) text += static_cast<char>(*w);
                } else {
                    text = va_arg(ap, const char*);
                }
                std::snprintf(buffer, sizeof buffer, piece.c_str(), text.c_str());
                out += buffer;
                break;
            }
            case 'd':
            case 'i':
            case 'c':
                std::snprintf(buffer, sizeof buffer, piece.c_str(), va_arg(ap, int32_t));
                out += buffer;
                break;
            case 'u':
            case 'x':
            case 'X':
            case 'o':
                std::snprintf(buffer, sizeof buffer, piece.c_str(), va_arg(ap, uint32_t));
                out += buffer;
                break;
            case 'f':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
                std::snprintf(buffer, sizeof buffer, piece.c_str(), va_arg(ap, double));
                out += buffer;
                break;
            default:
                out += piece;
                break;
        }
    }
    va_end(ap);
    std::memcpy(dst, out.c_str(), out.size() + 1);
    return static_cast<int32_t>(out.size());
}

void PostcardSave(Postcard* card, char* path) { card->save(path); }

void EmailCtor(void* email) { Email::Construct(static_cast<Email*>(email)); }

void StringAssign(void* str, const char* text, uint32_t length) { AssignText(str, text, length); }
void StringCtor(void* str, const char* text) { MakeText(str, text); }
void ListInsert(void* list, void* where, const void* str) { InsertText(list, where, str); }
void StringDtor(void* str, int16_t flag) { DropText(str, flag); }

void OnlineSystemCtor(void* system) { new (system) CFSOnlineSystem(); }
void UserInfoCtor(void* info) { new (info) CFSUserInfo(); }

void WindowSetText(InterfaceWindow* window, char16_t* text) { window->SetText(text); }

}  // namespace sendpostcard_callee

namespace email_callee {

void* OperatorNew(uint32_t size) { return MacNew(size); }
void OperatorDelete(void* p) { tpw::ArenaFree(p); }

void AttachmentCtor(void* self, const char* path) { Email::Attachment::Construct(static_cast<Email::Attachment*>(self), path); }
void AttachmentDtor(void* self, int16_t flag) { Email::Attachment::Destruct(static_cast<Email::Attachment*>(self), flag); }

void StringAssign(void* str, const char* text, uint32_t length) { AssignText(str, text, length); }
void StringDtor(void* str, int16_t flag) { DropText(str, flag); }
void WStringDtor(void* str, int16_t flag) { DropText(str, flag); }

// 0x1023baa8 __ptr_glue: calls the function whose TVector is `tvector` (the truncated address of the table the Email keeps in mCallback)
// with the two arguments. The postcard panel's table is IllCallYouBack's (0x10267cf8, ui_Postcard.cpp). The outbox panel's table
// (0x10268500) belongs to OutboxCallback 0x101e4124, which is not translated, so that call is logged and does nothing.
void PtrGlue(uint32_t tvector, int32_t a, int32_t b) {
    if (tvector == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(kIllCallYouBackVector))) {
        IllCallYouBack(static_cast<EmailCompletionCode>(a), static_cast<EmailErrorCode>(b));
        return;
    }
    ::tpw::LogRawF("email_callee::PtrGlue: no translation behind the callback table 0x%x (OutboxCallback 0x101e4124)\n",
                   static_cast<unsigned>(tvector));
}

// first is the word of the first node to go (Email::Reset passes the list's next); last the address of the node that stays.
// SendToOutbox empties the Outbox's host list (first 0, last the list).
void ListErase(void* list, uint32_t first, void* last) {
    if (InUserOutbox(list)) {
        static_cast<std::list<std::string>*>(list)->clear();
        return;
    }
    ListEraseRange(list, first, tpw::ArenaOffset(last));
}

void ListClear(void* list) {
    ListEraseRange(list, Word(Sentinel(list) + 4), Sentinel(list));
    InitList(list);
}

}  // namespace email_callee

namespace emailcb_callee {

void StringInit(void* str) { Word(str) = NewRep(nullptr, 0, 1); }
void WStringInit(void* str) { Word(str) = NewRep(nullptr, 0, 2); }
void ListInit(void* list) { InitList(list); }

// 0x101b2918 std::string(const std::string&, pos, n) on a word that holds nothing yet, and 0x10001c80 assign(const std::string&, pos, n),
// which lets go of the old representation after the new one is made (the source may be the string itself).
void StringSubCtor(void* dst, const void* src, uint32_t pos, uint32_t n) { Word(dst) = NewSubRep(src, pos, n); }
void StringSubAssign(void* dst, const void* src, uint32_t pos, uint32_t n) {
    const uint32_t old = Word(dst);
    Word(dst) = NewSubRep(src, pos, n);
    FreeRep(old);
}

}  // namespace emailcb_callee

// 0x102394d8 unlink (c/c++_shared).
namespace outboxdel_callee {
void Unlink(const char* path) { ::unlink(path); }
}  // namespace outboxdel_callee

namespace sendbody_callee {

void StringAppend(void* str, const char* text, uint32_t length) { Append(str, text, length); }

}  // namespace sendbody_callee

namespace outbox_callee {

void EmailCtor(void* email) { Email::Construct(static_cast<Email*>(email)); }
void OnlineSystemCtor(void* system) { new (system) CFSOnlineSystem(); }

const char16_t* LoginEmailAddress(CFSOnlineSystem* system) { return system->GetLoginEmailAddress(); }
const char16_t* LoginCustomerName(CFSOnlineSystem* system) { return system->GetLoginCustomerName(); }
const char16_t* LoginPassword(CFSOnlineSystem* system) { return system->GetLoginPassword(); }
int32_t LoginPlayerID(CFSOnlineSystem* system) { return system->GetLoginPlayerID(); }
int32_t LoginCustomerID(CFSOnlineSystem* system) { return system->GetLoginCustomerID(); }

// The attachment's base64 block (Email::Attachment::Destruct); attach_callee::NewArray makes it.
void OperatorDeleteArray(void* p) { tpw::ArenaFree(p); }

void WStringAssign(void* str, const char16_t* text, uint32_t length) { Assign(str, text, length, 2); }
void StringCtor(void* str, const char* text) { MakeText(str, text); }
void ListInsert(void* list, void* where, const void* str) { InsertText(list, where, str); }

}  // namespace outbox_callee

namespace outboxwrite_callee {

int32_t Time() { return processinstruction_callee::Time(nullptr); }
const int32_t* LocalTime(const int32_t* seconds) { return processinstruction_callee::LocalTime(seconds); }

}  // namespace outboxwrite_callee

char16_t* wcstok(char16_t* string, const char16_t* delimiters, char16_t** state) {
    (void)state;
    static char16_t* saved;
    char16_t* p = string != nullptr ? string : saved;
    if (p == nullptr) return nullptr;
    auto isDelimiter = [delimiters](char16_t c) {
        for (const char16_t* d = delimiters; *d != 0; ++d)
            if (*d == c) return true;
        return false;
    };
    while (*p != 0 && isDelimiter(*p)) ++p;
    if (*p == 0) {
        saved = nullptr;
        return nullptr;
    }
    char16_t* token = p;
    while (*p != 0 && !isDelimiter(*p)) ++p;
    if (*p == 0) {
        saved = nullptr;
    } else {
        *p = 0;
        saved = p + 1;
    }
    return token;
}

char16_t* wcschr(const char16_t* text, char16_t c) {
    for (;; ++text) {
        if (*text == c) return const_cast<char16_t*>(text);
        if (*text == 0) return nullptr;
    }
}

int32_t wcscmp(const char16_t* a, const char16_t* b) {
    while (*a != 0 && *a == *b) {
        ++a;
        ++b;
    }
    return static_cast<int32_t>(*a) - static_cast<int32_t>(*b);
}
