// online_signup.cpp: the online sign-up's and password reset's requests, replies and rules (include/platform/online_signup.h),
// and their texts.
#include "platform/online_signup.h"

#include "platform/online_boss.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>

#include "platform.h"

namespace tpw::online::signup {

namespace {

constexpr size_t kHeader = boss::kHeader;   // the BOSS header

void Put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}
void Put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (24 - 8 * i));
}
uint16_t Get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t Get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}

// A text as a field of `units` UTF-16LE units, NUL padded (the caller checked it fits).
void PutField(uint8_t* p, const std::u16string& text, size_t units) {
    for (size_t i = 0; i < text.size() && i + 1 < units; ++i) {
        p[2 * i] = static_cast<uint8_t>(text[i]);
        p[2 * i + 1] = static_cast<uint8_t>(text[i] >> 8);
    }
}
// A field's units up to its NUL; false when there is no NUL.
bool GetField(const uint8_t* p, std::u16string& out) {
    out.clear();
    for (size_t i = 0; i < kFieldUnits; ++i) {
        const char16_t unit = static_cast<char16_t>(p[2 * i] | p[2 * i + 1] << 8);
        if (unit == 0) return true;
        out += unit;
    }
    return false;
}

bool Digit(char16_t c) { return c >= u'0' && c <= u'9'; }
bool Separator(char16_t c) { return c == u' ' || c == u'-' || c == u'_'; }
// A letter the game's fonts draw: ASCII's, and Latin-1's (ª µ º, the accented capitals and small letters).
bool Letter(char16_t c) {
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || c == 0xaa || c == 0xb5 || c == 0xba ||
           (c >= 0xc0 && c <= 0xd6) || (c >= 0xd8 && c <= 0xf6) || (c >= 0xf8 && c <= 0xfe);
}

// The server's key for the reserved check, for ASCII: lower case without the separators, the digits read as the letters
// they stand for (server/internal/names: 0 o, 1 i, 3 e, 4 a, 5 s, 7 t, 8 b, 9 g).
std::u16string ReservedKey(const std::u16string& name) {
    std::u16string key;
    for (char16_t c : name) {
        if (Separator(c)) continue;
        if (c >= u'A' && c <= u'Z') c = static_cast<char16_t>(c - u'A' + u'a');
        switch (c) {
            case u'0': c = u'o'; break;
            case u'1': c = u'i'; break;
            case u'3': c = u'e'; break;
            case u'4': c = u'a'; break;
            case u'5': c = u's'; break;
            case u'7': c = u't'; break;
            case u'8': c = u'b'; break;
            case u'9': c = u'g'; break;
            default: break;
        }
        key += c;
    }
    return key;
}

}  // namespace

Outcome CheckName(const std::u16string& name) {
    bool alnum = false;
    for (char16_t c : name) {
        if (Letter(c) || Digit(c)) alnum = true;
        else if (!Separator(c)) return Outcome::BadChars;
    }
    if (name.size() < kNameMin || name.size() > kNameMax) return Outcome::BadLength;
    if (name.front() == u' ' || name.back() == u' ' || !alnum) return Outcome::BadChars;
    // server/internal/names' reserved names
    static const char16_t* const kReserved[] = {u"admin", u"administrator", u"moderator", u"mod", u"staff", u"server",
                                                u"system", u"ea", u"eagames", u"bullfrog", u"tester", u"tester2"};
    const std::u16string key = ReservedKey(name);
    for (const char16_t* reserved : kReserved)
        if (key == ReservedKey(reserved)) return Outcome::Reserved;
    return Outcome::Made;
}

bool PasswordOk(const std::u16string& password) {
    if (password.size() < kPasswordMin || password.size() > kPasswordMax) return false;
    for (char16_t c : password)
        if (c < 0x21 || c > 0x7e) return false;
    return true;
}

bool EmailOk(const std::u16string& email) {
    if (email.size() > kEmailMax) return false;
    const size_t at = email.find(u'@');
    if (at == std::u16string::npos || at == 0) return false;
    static const char16_t kLocal[] = u".!#$%&'*+/=?^_`{|}~-";
    for (size_t i = 0; i < at; ++i) {
        const char16_t c = email[i];
        if (!(Letter(c) && c < 0x80) && !Digit(c) && std::u16string(kLocal).find(c) == std::u16string::npos) return false;
    }
    if (email[0] == u'.' || email[at - 1] == u'.' || email.find(u"..") < at) return false;
    // the domain: labels of letters, digits and inner hyphens, two or more
    size_t labels = 0, begin = at + 1;
    while (true) {
        size_t end = email.find(u'.', begin);
        if (end == std::u16string::npos) end = email.size();
        if (end == begin || email[begin] == u'-' || email[end - 1] == u'-') return false;
        for (size_t i = begin; i < end; ++i) {
            const char16_t c = email[i];
            if (!(Letter(c) && c < 0x80) && !Digit(c) && c != u'-') return false;
        }
        ++labels;
        if (end == email.size()) break;
        begin = end + 1;
    }
    return labels >= 2;
}

bool CodeOk(const std::u16string& code) {
    if (code.size() != kCodeDigits) return false;
    for (char16_t c : code)
        if (!Digit(c)) return false;
    return true;
}

Outcome Check(const Request& request) {
    const Outcome name = CheckName(request.name);
    switch (request.kind) {
        case Kind::Signup:
            if (name != Outcome::Made) return name;
            if (!PasswordOk(request.password)) return Outcome::BadPassword;
            if (!EmailOk(request.email)) return Outcome::BadEmail;
            if (!request.adult) return Outcome::NoAge;
            return Outcome::Made;
        case Kind::ResetCode:
            // (a reserved name is the server's to answer as any other: the same answer as for a name that is not there)
            if (name == Outcome::BadLength || name == Outcome::BadChars) return name;
            if (!EmailOk(request.email)) return Outcome::BadEmail;
            return Outcome::Made;
        case Kind::ResetSet:
            if (name == Outcome::BadLength || name == Outcome::BadChars) return name;
            if (!CodeOk(request.code)) return Outcome::BadCode;
            if (!PasswordOk(request.password)) return Outcome::BadPassword;
            return Outcome::Made;
    }
    return Outcome::Malformed;
}

namespace {

struct Types {
    uint16_t request, reply;
    size_t size;
};
Types TypesOf(Kind kind) {
    switch (kind) {
        case Kind::ResetCode: return {kTypeResetCode, kTypeResetCodeReply, kResetCodeSize};
        case Kind::ResetSet: return {kTypeResetSet, kTypeResetSetReply, kResetSetSize};
        default: return {kTypeSignup, kTypeSignupReply, kSignupSize};
    }
}

// A reply's status as an outcome; false for a status the reply cannot have.
bool StatusOutcome(Kind kind, uint16_t status, Outcome& out) {
    if (kind == Kind::Signup) {
        if (status > static_cast<uint16_t>(Outcome::EmailFull)) return false;
        out = static_cast<Outcome>(status);
        return true;
    }
    switch (status) {
        case 0: out = Outcome::Made; return true;
        case 1: out = Outcome::NoReset; return true;
        case 11: out = Outcome::Malformed; return true;
        default: break;
    }
    if (kind == Kind::ResetCode) {
        if (status != 2) return false;
        out = Outcome::CodeLimit;
        return true;
    }
    switch (status) {
        case 3: out = Outcome::BadCode; return true;
        case 4: out = Outcome::DeadCode; return true;
        case 5: out = Outcome::BadPassword; return true;
        case 6: out = Outcome::WeakPassword; return true;
        case 12: out = Outcome::ServerError; return true;
        default: return false;
    }
}

}  // namespace

std::vector<uint8_t> BuildRequest(const Request& request, uint16_t language, uint32_t seq) {
    const Types types = TypesOf(request.kind);
    std::vector<uint8_t> out(kHeader + types.size, 0);
    uint8_t* h = out.data();
    h[0] = 'B';
    h[1] = 'c';
    Put16(h + 2, types.request);
    Put16(h + 4, language);
    Put16(h + 6, 0);
    Put32(h + 8, static_cast<uint32_t>(types.size));
    Put32(h + 12, 0);
    Put32(h + 16, seq);
    uint8_t* p = h + kHeader;
    PutField(p, request.name, kFieldUnits);
    switch (request.kind) {
        case Kind::Signup:
            PutField(p + 0x40, request.password, kFieldUnits);
            PutField(p + 0x80, request.email, kEmailUnits);
            Put32(p + 0x180, request.adult ? kFlagAdult : 0);
            break;
        case Kind::ResetCode: PutField(p + 0x40, request.email, kEmailUnits); break;
        case Kind::ResetSet:
            PutField(p + 0x40, request.code, kCodeUnits);
            PutField(p + 0x50, request.password, kFieldUnits);
            break;
    }
    return out;
}

Result ParseReply(Kind kind, const uint8_t* bytes, size_t size, uint32_t seq) {
    Result r;
    r.outcome = Outcome::BadReply;
    if (size < kHeader) {
        r.why = "a reply shorter than its header";
        return r;
    }
    const Types types = TypesOf(kind);
    const uint16_t type = Get16(bytes + 2), status = Get16(bytes + 6);
    const uint32_t length = Get32(bytes + 8);
    Outcome outcome = Outcome::BadReply;
    if (bytes[0] != 'B' || bytes[1] != 's') {
        r.why = "a reply that does not start \"Bs\"";
    } else if (type != types.reply) {
        r.why = "a reply of type " + std::to_string(type) + ", not " + std::to_string(types.reply) +
                " (a server without sign-up and password reset)";
    } else if (Get32(bytes + 16) != seq) {
        r.why = "a reply to another request";
    } else if (length != size - kHeader) {
        r.why = "a reply whose length is not its payload's";
    } else if (!StatusOutcome(kind, status, outcome)) {
        r.why = "an unknown status " + std::to_string(status);
    } else if (outcome != Outcome::Made || kind == Kind::ResetCode) {
        r.outcome = outcome;   // (a refusal's payload, if any, is not read)
    } else if (length != kAccountSize) {
        r.why = "an account's reply of " + std::to_string(length) + " bytes";
    } else {
        const uint8_t* p = bytes + kHeader;
        std::u16string name;
        const Outcome nameRule = GetField(p, name) ? CheckName(name) : Outcome::BadChars;
        if (nameRule == Outcome::BadChars || nameRule == Outcome::BadLength) {
            r.why = "an account's name that is not a name";
        } else {
            r.outcome = Outcome::Made;
            r.name = std::move(name);
            r.id = Get32(p + 0x40);
        }
    }
    return r;
}

Result Exchange(const std::string& host, uint16_t port, const Request& request, uint16_t language,
                const std::atomic<bool>& stopping, int seconds) {
    Result r;
    const Outcome checked = Check(request);
    if (checked != Outcome::Made) {
        r.outcome = checked;
        return r;
    }
    std::unique_ptr<boss::Transport> t = boss::MakeTransport();
    if (!t) {
        r.why = "no connection to make";
        return r;
    }
    if (!t->Open(host, port, stopping)) {
        r.why = t->Failure().empty() ? "the connection failed" : t->Failure();
        return r;
    }
    const uint32_t seq = 10;   // the client's first, as the game's library counts
    std::vector<uint8_t> message = BuildRequest(request, language, seq);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    auto late = [&] { return stopping || std::chrono::steady_clock::now() >= deadline; };
    auto fail = [&](std::string why) {
        t->Close();
        std::memset(message.data(), 0, message.size());
        r.why = std::move(why);
        return r;
    };
    size_t sent = 0;
    while (sent < message.size()) {
        if (late()) return fail(stopping ? "stopped" : "no time to send the request");
        const int ready = t->Wait(true, 100);
        if (ready < 0) return fail("the connection failed while sending");
        if ((ready & 2) == 0) continue;
        const long n = t->Send(message.data() + sent, message.size() - sent);
        if (n == boss::Transport::kWouldBlock) continue;
        if (n <= 0) return fail("the connection failed while sending");
        sent += static_cast<size_t>(n);
    }
    std::memset(message.data(), 0, message.size());
    // The header, then as much payload as it says (at most an account's reply).
    std::vector<uint8_t> reply(kHeader);
    size_t got = 0;
    while (got < reply.size()) {
        if (late()) return fail(stopping ? "stopped" : "no reply in " + std::to_string(seconds) + " seconds");
        const int ready = t->Wait(false, 100);
        if (ready < 0) return fail("the connection failed while reading");
        if ((ready & 1) == 0) continue;
        const long n = t->Receive(reply.data() + got, reply.size() - got);
        if (n == boss::Transport::kWouldBlock) continue;
        if (n <= 0)
            return fail(n == 0 ? "the server closed the connection before its reply (a server without sign-up?)"
                               : "the connection failed while reading");
        got += static_cast<size_t>(n);
        if (got == kHeader) {
            const uint32_t length = Get32(reply.data() + 8);
            if (length > kAccountSize) {
                fail("a reply of " + std::to_string(length) + " bytes");
                r.outcome = Outcome::BadReply;
                return r;
            }
            reply.resize(kHeader + length);
        }
    }
    t->Close();
    r = ParseReply(request.kind, reply.data(), reply.size(), seq);
    return r;
}

void Wipe(std::u16string& text) {
    std::fill(text.begin(), text.end(), u'\0');
    text.clear();
}
void Wipe(char16_t* text, size_t units) {
    for (size_t i = 0; i < units; ++i) text[i] = 0;
}
void Wipe(Request& request) {
    Wipe(request.password);
    Wipe(request.email);
    Wipe(request.code);
}

// ---- The job ---------------------------------------------------------------------------------------------------------------

struct Job::State {
    std::mutex mutex;
    std::atomic<bool> stopping{false};
    bool running = false;
    bool done = false;
    Result result;
};

Job::Job() : state_(std::make_shared<State>()) {}

Job::~Job() { state_->stopping = true; }

void Job::Start(const std::string& host, uint16_t port, const Request& request, uint16_t language) {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->running) return;
        state_->running = true;
        state_->done = false;
        state_->result = Result{};
    }
    std::string name(request.name.begin(), request.name.end());
    for (char& c : name)
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7e) c = '?';
    const char* what = request.kind == Kind::Signup ? "sign-up of" : request.kind == Kind::ResetCode ? "reset code for"
                                                                                                    : "new password for";
    tpw::Log("online: %s \"%s\" sent to %s:%u", what, name.c_str(), host.c_str(), static_cast<unsigned>(port));
    std::shared_ptr<State> state = state_;
    std::thread([state, host, port, request = Request(request), language, name, what]() mutable {
        Result r = Exchange(host, port, request, language, state->stopping);
        Wipe(request);
        if (r.outcome == Outcome::Made && request.kind == Kind::ResetCode)
            tpw::Log("online: %s \"%s\" answered", what, name.c_str());
        else if (r.outcome == Outcome::Made)
            tpw::Log("online: %s \"%s\" done, player %u", what, name.c_str(), static_cast<unsigned>(r.id));
        else
            tpw::Log("online: %s \"%s\" refused (%d)%s%s", what, name.c_str(), static_cast<int>(r.outcome),
                     r.why.empty() ? "" : ": ", r.why.c_str());
        std::lock_guard<std::mutex> lock(state->mutex);
        state->result = std::move(r);
        state->done = true;
        state->running = false;
    }).detach();
}

bool Job::Running() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->running;
}

bool Job::Done() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->done;
}

Result Job::Take() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    Result r = std::move(state_->result);
    state_->result = Result{};
    state_->done = false;
    return r;
}

// ---- The texts -------------------------------------------------------------------------------------------------------------

// The popups' texts, English for every language: the game's string tables have none of them. A translation would be a
// column per language here (the game's languages, Text.h's language ids), picked by the game's language.
namespace {

const char16_t* const kTexts[] = {
    u"Create account",                                                                       // Button
    u"Forgot password?",                                                                     // ForgotButton
    u"Create an account",                                                                    // Title
    u"Confirm",                                                                              // Confirm
    u"E-mail (private)",                                                                     // Email
    u"Don't use a password you use anywhere else. This 1999 game sends it unencrypted.",     // Warning
    u"I am 13 or older",                                                                     // Adult
    u"Creating your account...",                                                             // Waiting
    u"Account created. You can log in now.",                                                 // Made
    u"Forgot your password?",                                                                // ForgotTitle
    u"Send code",                                                                            // SendCode
    u"Asking for a code...",                                                                 // Sending
    u"If that name and e-mail match, a code is on its way. Check your junk folder too.",     // Sent
    u"Set a new password",                                                                   // SetTitle
    u"Code",                                                                                 // Code
    u"New password",                                                                         // NewPassword
    u"Set password",                                                                         // SetButton
    u"Setting your password...",                                                             // Setting
    u"Your password is set. You can log in now.",                                            // PasswordSet
    u"That name is taken. Please choose another.",                                           // Taken
    u"A name has 3 to 16 characters.",                                                       // BadLength
    u"A name has only letters, digits, spaces, - and _, and no space at either end.",        // BadChars
    u"That name is not allowed. Please choose another.",                                     // NotAllowed
    u"A password has 6 to 16 characters: letters, digits and punctuation, no spaces.",       // BadPassword
    u"That password is too easy to guess. Please choose another.",                           // WeakPassword
    u"The passwords don't match.",                                                           // Mismatch
    u"Please give an e-mail address, such as name@example.com.",                             // BadEmail
    u"That e-mail address has the most accounts already.",                                   // EmailFull
    u"You must be 13 or older to make an account.",                                          // NoAge
    u"Too many accounts were made from here. Please try again later.",                       // TooMany
    u"Too many codes were asked for from here. Please try again later.",                     // TooManyCodes
    u"Too many accounts were made today. Please try again tomorrow.",                        // TooManyToday
    u"New accounts can't be made right now. Please try again later.",                        // Closed
    u"Password reset isn't available yet. Ask on the Discord.",                              // NoReset
    u"That code is wrong or has expired.",                                                   // BadCode
    u"Too many wrong tries. Please ask for a new code.",                                     // DeadCode
    u"That could not be done. Please try again later.",                                      // Failed
};
static_assert(sizeof(kTexts) / sizeof(kTexts[0]) == static_cast<size_t>(Text::Count), "a text for each Text");

}  // namespace

const char16_t* GetText(Text text) {
    const size_t i = static_cast<size_t>(text);
    return i < static_cast<size_t>(Text::Count) ? kTexts[i] : u"";
}

const char16_t* RefusalText(Outcome outcome) {
    switch (outcome) {
        case Outcome::Taken: return GetText(Text::Taken);
        case Outcome::BadLength: return GetText(Text::BadLength);
        case Outcome::BadChars: return GetText(Text::BadChars);
        case Outcome::Reserved:
        case Outcome::Blocked: return GetText(Text::NotAllowed);
        case Outcome::BadPassword: return GetText(Text::BadPassword);
        case Outcome::WeakPassword: return GetText(Text::WeakPassword);
        case Outcome::Mismatch: return GetText(Text::Mismatch);
        case Outcome::BadEmail: return GetText(Text::BadEmail);
        case Outcome::EmailFull: return GetText(Text::EmailFull);
        case Outcome::NoAge: return GetText(Text::NoAge);
        case Outcome::IpLimit: return GetText(Text::TooMany);
        case Outcome::CodeLimit: return GetText(Text::TooManyCodes);
        case Outcome::DayLimit: return GetText(Text::TooManyToday);
        case Outcome::Closed: return GetText(Text::Closed);
        case Outcome::NoReset: return GetText(Text::NoReset);
        case Outcome::BadCode: return GetText(Text::BadCode);
        case Outcome::DeadCode: return GetText(Text::DeadCode);
        case Outcome::Unreachable: {
            // kUnreachableText (ASCII) as the game's text, made once.
            static const std::u16string text = [] {
                std::u16string t;
                for (const char* c = boss::kUnreachableText; *c != '\0'; ++c) t += static_cast<char16_t>(*c);
                return t;
            }();
            return text.c_str();
        }
        default: return GetText(Text::Failed);   // Malformed, ServerError, BadReply (and Made, which is no refusal)
    }
}

}  // namespace tpw::online::signup
