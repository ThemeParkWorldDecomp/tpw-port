// online_signup_tests.cpp: the online sign-up's and password reset's requests, replies and rules (platform/online_signup.cpp,
// include/platform/online_signup.h) with no network. The connection is a fake (online_boss.h Transport) that keeps what was
// sent and answers what the test says: the made account, the code request's answer, the password set, every refusal, a
// server without sign-up (it closes the connection), junk, silence, an answer in pieces. Then the name, password, e-mail
// and code pre-checks, the job on its own thread, and the texts.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "platform/online_boss.h"
#include "platform/online_signup.h"

using namespace tpw::online::signup;
namespace boss = tpw::online::boss;
using boss::Transport;

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

std::string Narrow(const std::u16string& s) {
    std::string out;
    for (char16_t c : s) out += c < 0x80 ? static_cast<char>(c) : '?';
    return out;
}

void CheckEq(const std::u16string& got, const std::u16string& want, const std::string& what) {
    Check(got == want, what + ": \"" + Narrow(got) + "\", not \"" + Narrow(want) + "\"");
}

uint16_t Get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
uint32_t Get32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}
void Put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}
void Put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (24 - 8 * i));
}
std::u16string Field(const uint8_t* p) {
    std::u16string s;
    for (size_t i = 0; i < kFieldUnits && (p[2 * i] | p[2 * i + 1]); ++i)
        s += static_cast<char16_t>(p[2 * i] | p[2 * i + 1] << 8);
    return s;
}
void PutField(uint8_t* p, const std::u16string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        p[2 * i] = static_cast<uint8_t>(s[i]);
        p[2 * i + 1] = static_cast<uint8_t>(s[i] >> 8);
    }
}

// A reply message as the server sends it: a made account's (or a password set's) payload is the name and the id.
std::vector<uint8_t> Reply(uint16_t type, uint16_t status, uint32_t seq, const std::u16string& name = u"", uint32_t id = 0,
                           bool payload = true) {
    const size_t size = status == 0 && payload && type != kTypeResetCodeReply ? kAccountSize : 0;
    std::vector<uint8_t> m(0x14 + size, 0);
    m[0] = 'B';
    m[1] = 's';
    Put16(&m[2], type);
    Put16(&m[6], status);
    Put32(&m[8], static_cast<uint32_t>(size));
    Put32(&m[16], seq);
    if (size) {
        PutField(&m[0x14], name);
        Put32(&m[0x14 + 0x40], id);
    }
    return m;
}

// What the fake connection does.
struct Script {
    bool open = true;                // Open succeeds
    std::vector<uint8_t> answer;     // given once the whole request arrived
    size_t piece = 0;                // the answer in pieces of this size (0: whole)
    bool closeAfter = false;         // Receive answers 0 once the answer is given (or at once when there is none)
    std::vector<uint8_t> sent;       // what the client sent
    std::string host;
    uint16_t port = 0;
    bool closed = false;
    int opened = 0;
};

class FakeTransport : public Transport {
public:
    explicit FakeTransport(Script& s) : s_(s) {}
    bool Open(const std::string& host, uint16_t port, const std::atomic<bool>&) override {
        ++s_.opened;
        s_.host = host;
        s_.port = port;
        return s_.open;
    }
    int Wait(bool wantWrite, int) override {
        int ready = wantWrite ? 2 : 0;
        if (Requested() && (given_ < s_.answer.size() || s_.closeAfter)) ready |= 1;
        if (ready == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));   // silence, not a busy loop
        return ready;
    }
    long Send(const uint8_t* bytes, size_t size) override {
        const size_t n = size > 7 ? 7 : size;   // a few bytes at a time, as a busy socket takes them
        s_.sent.insert(s_.sent.end(), bytes, bytes + n);
        return static_cast<long>(n);
    }
    long Receive(uint8_t* bytes, size_t size) override {
        if (!Requested()) return kWouldBlock;
        if (given_ >= s_.answer.size()) return s_.closeAfter ? 0 : kWouldBlock;
        size_t n = s_.answer.size() - given_;
        if (s_.piece && n > s_.piece) n = s_.piece;
        if (n > size) n = size;
        std::copy(s_.answer.begin() + static_cast<long>(given_), s_.answer.begin() + static_cast<long>(given_ + n), bytes);
        given_ += n;
        return static_cast<long>(n);
    }
    void Close() override { s_.closed = true; }
    std::string Failure() const override { return s_.open ? "" : "the connection was refused"; }

private:
    // the whole request: its header, then the length its header says
    bool Requested() const { return s_.sent.size() >= 0x14 && s_.sent.size() >= 0x14 + Get32(&s_.sent[8]); }
    Script& s_;
    size_t given_ = 0;
};

Result Run(Script& s, const Request& request, int seconds = 2) {
    boss::SetTransportMaker([&s] { return std::make_unique<FakeTransport>(s); });
    std::atomic<bool> stopping{false};
    Result r = Exchange("127.0.0.1", 7598, request, 0, stopping, seconds);
    boss::SetTransportMaker(nullptr);
    return r;
}

// Made-up passwords for the tests (no account has them).
const std::u16string kPassword = u"Wh33l-Spin!";
const std::u16string kNewPassword = u"L00p+Loop";

Request Good() {
    Request r;
    r.kind = Kind::Signup;
    r.name = u"Coaster Kid";
    r.password = kPassword;
    r.email = u"kid@example.org";
    r.adult = true;
    return r;
}

Request GoodCode() {
    Request r;
    r.kind = Kind::ResetCode;
    r.name = u"Coaster Kid";
    r.email = u"Kid@Example.org";
    return r;
}

Request GoodSet() {
    Request r;
    r.kind = Kind::ResetSet;
    r.name = u"Coaster Kid";
    r.code = u"042917";
    r.password = kNewPassword;
    return r;
}

std::u16string Units(const uint8_t* p, size_t units) {
    std::u16string s;
    for (size_t i = 0; i < units && (p[2 * i] | p[2 * i + 1]); ++i) s += static_cast<char16_t>(p[2 * i] | p[2 * i + 1] << 8);
    return s;
}

bool Zero(const std::vector<uint8_t>& m, size_t from, size_t to) {
    for (size_t i = from; i < to; ++i)
        if (m[i] != 0) return false;
    return true;
}

void TestMessages() {
    Request r = Good();
    std::vector<uint8_t> m = BuildRequest(r, 3, 42);
    Check(m.size() == 0x14 + 0x184, "the sign-up is a header and 0x184 bytes");
    Check(m[0] == 'B' && m[1] == 'c', "the request starts \"Bc\"");
    Check(Get16(&m[2]) == 20, "the sign-up is type 20");
    Check(Get16(&m[4]) == 3, "the request carries the language");
    Check(Get16(&m[6]) == 0, "the request's status is 0");
    Check(Get32(&m[8]) == 0x184, "the request's length is its payload's");
    Check(Get32(&m[12]) == 0, "the request's context word is 0");
    Check(Get32(&m[16]) == 42, "the request carries its sequence word");
    CheckEq(Field(&m[0x14]), u"Coaster Kid", "the name field");
    Check(m[0x14 + 1] == 0, "the name is UTF-16LE");
    CheckEq(Field(&m[0x14 + 0x40]), kPassword, "the password field");
    CheckEq(Units(&m[0x14 + 0x80], kEmailUnits), u"kid@example.org", "the e-mail field at +0x80");
    Check(Get32(&m[0x14 + 0x180]) == 1, "the 13 or older flag at +0x180");
    Check(Zero(m, 0x14 + 0x80 + 2 * 15, 0x14 + 0x180), "the e-mail field is NUL padded");
    r.adult = false;
    r.name = u"Renée";
    r.email = std::u16string(88, u'a') + u"@example.org";   // 100 characters
    m = BuildRequest(r, 0, 1);
    Check(Get32(&m[0x14 + 0x180]) == 0, "no flag without 13 or older");
    CheckEq(Field(&m[0x14]), u"Renée", "a Latin-1 name as its UTF-16 units");
    CheckEq(Units(&m[0x14 + 0x80], kEmailUnits), r.email, "a 100-character e-mail");
    Check(Zero(m, 0x14 + 2 * r.name.size(), 0x14 + 0x40), "the name field is NUL padded");

    m = BuildRequest(GoodCode(), 2, 9);
    Check(m.size() == 0x14 + 0x140 && Get16(&m[2]) == 22 && Get32(&m[8]) == 0x140, "the code request: type 22, 0x140 bytes");
    CheckEq(Field(&m[0x14]), u"Coaster Kid", "the code request's name");
    CheckEq(Units(&m[0x14 + 0x40], kEmailUnits), u"Kid@Example.org", "the code request's e-mail at +0x40");

    m = BuildRequest(GoodSet(), 2, 9);
    Check(m.size() == 0x14 + 0x90 && Get16(&m[2]) == 24 && Get32(&m[8]) == 0x90, "the new password: type 24, 0x90 bytes");
    CheckEq(Field(&m[0x14]), u"Coaster Kid", "the new password's name");
    CheckEq(Units(&m[0x14 + 0x40], kCodeUnits), u"042917", "the code at +0x40");
    Check(m[0x14 + 0x40 + 12] == 0 && m[0x14 + 0x40 + 13] == 0, "the code's NUL");
    CheckEq(Field(&m[0x14 + 0x50]), kNewPassword, "the new password at +0x50");
}

void TestParse() {
    // sign-up
    std::vector<uint8_t> m = Reply(21, 0, 7, u"Coaster Kid", 1234);
    Result r = ParseReply(Kind::Signup, m.data(), m.size(), 7);
    Check(r.outcome == Outcome::Made && r.name == u"Coaster Kid" && r.id == 1234, "a made account's reply");
    for (uint16_t status = 1; status <= 15; ++status) {
        m = Reply(21, status, 7);
        r = ParseReply(Kind::Signup, m.data(), m.size(), 7);
        Check(r.outcome == static_cast<Outcome>(status), "sign-up status " + std::to_string(status));
    }
    Check(ParseReply(Kind::Signup, Reply(21, 13, 7).data(), 0x14, 7).outcome == Outcome::BadPassword, "13: the password rule");
    Check(ParseReply(Kind::Signup, Reply(21, 14, 7).data(), 0x14, 7).outcome == Outcome::WeakPassword, "14: too weak");
    Check(ParseReply(Kind::Signup, Reply(21, 15, 7).data(), 0x14, 7).outcome == Outcome::EmailFull, "15: the e-mail's accounts");
    m = Reply(21, 16, 7);
    Check(ParseReply(Kind::Signup, m.data(), m.size(), 7).outcome == Outcome::BadReply, "an unknown status is a bad reply");
    m = Reply(21, 0, 8, u"Coaster Kid", 1);
    Check(ParseReply(Kind::Signup, m.data(), m.size(), 7).outcome == Outcome::BadReply, "a reply to another request");
    m = Reply(9, 1, 7);
    Check(ParseReply(Kind::Signup, m.data(), m.size(), 7).outcome == Outcome::BadReply, "a login reply is not a sign-up reply");
    m = Reply(25, 0, 7, u"Coaster Kid", 1);
    Check(ParseReply(Kind::Signup, m.data(), m.size(), 7).outcome == Outcome::BadReply, "a reset reply is not a sign-up reply");
    m = Reply(21, 0, 7, u"Coaster Kid", 1);
    m[1] = 'c';
    Check(ParseReply(Kind::Signup, m.data(), m.size(), 7).outcome == Outcome::BadReply, "a reply not from the server");
    m = Reply(21, 0, 7, u"Coaster Kid", 1);
    Check(ParseReply(Kind::Signup, m.data(), m.size() - 1, 7).outcome == Outcome::BadReply, "a short payload");
    m = Reply(21, 0, 7, u"", 0, false);
    Check(ParseReply(Kind::Signup, m.data(), m.size(), 7).outcome == Outcome::BadReply, "a made account without its payload");
    m = Reply(21, 1, 7);
    Put32(&m[8], 4);
    m.resize(m.size() + 4);
    Check(ParseReply(Kind::Signup, m.data(), m.size(), 7).outcome == Outcome::Taken, "a refusal with a payload is still the refusal");
    m = Reply(21, 0, 7, u"x", 1);
    Check(ParseReply(Kind::Signup, m.data(), m.size(), 7).outcome == Outcome::BadReply, "a made account whose name is not a name");
    m = Reply(21, 0, 7, u"Renée", 1);
    r = ParseReply(Kind::Signup, m.data(), m.size(), 7);
    Check(r.outcome == Outcome::Made && r.name == u"Renée", "a made account with a Latin-1 name");
    m = Reply(21, 0, 7, u"Coaster Kid", 1);
    for (size_t i = 0; i < kFieldUnits; ++i) m[0x14 + 2 * i] = 'a';   // no NUL
    Check(ParseReply(Kind::Signup, m.data(), m.size(), 7).outcome == Outcome::BadReply, "a name field without its NUL");
    Check(ParseReply(Kind::Signup, m.data(), 3, 7).outcome == Outcome::BadReply, "a reply shorter than its header");

    // the code request: OK has no payload, and says nothing of the account
    m = Reply(23, 0, 7);
    Check(ParseReply(Kind::ResetCode, m.data(), m.size(), 7).outcome == Outcome::Made, "the code request answered");
    const struct {
        uint16_t status;
        Outcome outcome;
    } codes[] = {{1, Outcome::NoReset}, {2, Outcome::CodeLimit}, {11, Outcome::Malformed}};
    for (const auto& c : codes) {
        m = Reply(23, c.status, 7);
        Check(ParseReply(Kind::ResetCode, m.data(), m.size(), 7).outcome == c.outcome,
              "code request status " + std::to_string(c.status));
    }
    for (uint16_t status : {3, 4, 5, 6, 12, 13}) {
        m = Reply(23, status, 7);
        Check(ParseReply(Kind::ResetCode, m.data(), m.size(), 7).outcome == Outcome::BadReply,
              "code request status " + std::to_string(status) + " is not one of its");
    }
    m = Reply(21, 0, 7, u"Coaster Kid", 1);
    Check(ParseReply(Kind::ResetCode, m.data(), m.size(), 7).outcome == Outcome::BadReply, "a sign-up reply to a code request");

    // the new password
    m = Reply(25, 0, 7, u"Coaster Kid", 88);
    r = ParseReply(Kind::ResetSet, m.data(), m.size(), 7);
    Check(r.outcome == Outcome::Made && r.name == u"Coaster Kid" && r.id == 88, "the password set");
    const struct {
        uint16_t status;
        Outcome outcome;
    } sets[] = {{1, Outcome::NoReset},     {3, Outcome::BadCode},       {4, Outcome::DeadCode}, {5, Outcome::BadPassword},
                {6, Outcome::WeakPassword}, {11, Outcome::Malformed}, {12, Outcome::ServerError}};
    for (const auto& c : sets) {
        m = Reply(25, c.status, 7);
        Check(ParseReply(Kind::ResetSet, m.data(), m.size(), 7).outcome == c.outcome,
              "new password status " + std::to_string(c.status));
    }
    for (uint16_t status : {2, 7, 13}) {
        m = Reply(25, status, 7);
        Check(ParseReply(Kind::ResetSet, m.data(), m.size(), 7).outcome == Outcome::BadReply,
              "new password status " + std::to_string(status) + " is not one of its");
    }
    m = Reply(25, 0, 7, u"", 0, false);
    Check(ParseReply(Kind::ResetSet, m.data(), m.size(), 7).outcome == Outcome::BadReply, "a password set without its payload");
}

void TestNames() {
    Check(CheckName(u"Bob") == Outcome::Made, "Bob");
    Check(CheckName(u"Coaster_Kid-99") == Outcome::Made, "letters, digits, '_' and '-'");
    Check(CheckName(u"Mr Big Top") == Outcome::Made, "inner spaces");
    Check(CheckName(u"abcdefghijklmnop") == Outcome::Made, "16 characters");
    Check(CheckName(u"Renée") == Outcome::Made, "a Latin-1 letter (the game's fonts draw them)");
    Check(CheckName(u"ab") == Outcome::BadLength, "2 characters");
    Check(CheckName(u"abcdefghijklmnopq") == Outcome::BadLength, "17 characters");
    Check(CheckName(u"") == Outcome::BadLength, "empty");
    Check(CheckName(u" Bob") == Outcome::BadChars, "a leading space");
    Check(CheckName(u"Bob ") == Outcome::BadChars, "a trailing space");
    Check(CheckName(u"---") == Outcome::BadChars, "no letter or digit");
    Check(CheckName(u"Bob!") == Outcome::BadChars, "'!'");
    Check(CheckName(u"Bo\tb") == Outcome::BadChars, "a tab");
    Check(CheckName(u"Bob×") == Outcome::BadChars, "the multiplication sign is no letter");
    Check(CheckName(u"BobŁ") == Outcome::BadChars, "a letter past Latin-1");
    Check(CheckName(u"Admin") == Outcome::Reserved, "Admin");
    Check(CheckName(u"ADM1N") == Outcome::Reserved, "ADM1N (a digit for a letter)");
    Check(CheckName(u"e-a") == Outcome::Reserved, "e-a");
    Check(CheckName(u"Mod") == Outcome::Reserved, "Mod");
    Check(CheckName(u"tester2") == Outcome::Reserved, "tester2");
    Check(CheckName(u"Bull Frog") == Outcome::Reserved, "Bull Frog");
    Check(CheckName(u"Modern") == Outcome::Made, "Modern is not Mod");
    Check(CheckName(u"Tester3") == Outcome::Made, "Tester3 is not reserved");
}

void TestRules() {
    Check(PasswordOk(u"abcdef"), "6 characters");
    Check(PasswordOk(u"abcdefghijklmnop"), "16 characters");
    Check(PasswordOk(u"!~#$%&'()*+,-./:"), "punctuation, '!' to '~'");
    Check(!PasswordOk(u"abcde"), "5 characters");
    Check(!PasswordOk(u"abcdefghijklmnopq"), "17 characters (more than the login box holds)");
    Check(!PasswordOk(u""), "an empty password");
    Check(!PasswordOk(u"abc def"), "a space");
    Check(!PasswordOk(u"abc\tdef"), "a tab");
    Check(!PasswordOk(u"abcdeé"), "a Latin-1 letter (the login box's keys type ASCII)");
    Check(!PasswordOk(u"abcdef\x7f"), "DEL");

    Check(EmailOk(u"a@b.co"), "a short e-mail");
    Check(EmailOk(u"first.last+tpw@mail.example-isp.co.uk"), "dots, plus, hyphens, subdomains");
    Check(EmailOk(std::u16string(88, u'a') + u"@example.org"), "100 characters");
    Check(!EmailOk(std::u16string(89, u'a') + u"@example.org"), "101 characters");
    Check(!EmailOk(u""), "no e-mail (it is required)");
    Check(!EmailOk(u"kid"), "no '@'");
    Check(!EmailOk(u"@example.org"), "no local part");
    Check(!EmailOk(u"kid@localhost"), "a one-label domain");
    Check(!EmailOk(u"kid@example..org"), "an empty label");
    Check(!EmailOk(u"kid@-example.org"), "a label that starts with '-'");
    Check(!EmailOk(u"kid@example.org-"), "a label that ends with '-'");
    Check(!EmailOk(u".kid@example.org"), "a leading dot");
    Check(!EmailOk(u"kid.@example.org"), "a dot before '@'");
    Check(!EmailOk(u"k..id@example.org"), "two dots together");
    Check(!EmailOk(u"k id@example.org"), "a space");
    Check(!EmailOk(u"kid@exa_mple.org"), "'_' in the domain");
    Check(!EmailOk(u"kid@@example.org"), "two '@'");
    Check(!EmailOk(u"é@example.org"), "a non-ASCII e-mail");

    Check(CodeOk(u"000000") && CodeOk(u"042917"), "6 digits");
    Check(!CodeOk(u"04291") && !CodeOk(u"0429170"), "5 or 7 digits");
    Check(!CodeOk(u"04a917") && !CodeOk(u"04 917") && !CodeOk(u""), "not digits");

    // the pre-checks in the popups' order
    Request r = Good();
    Check(Check(r) == Outcome::Made, "a good sign-up");
    r.name = u"x";
    r.password = u"abc";
    Check(Check(r) == Outcome::BadLength, "the name comes first");
    r = Good();
    r.password = u"abc";
    r.email = u"kid";
    Check(Check(r) == Outcome::BadPassword, "then the password");
    r = Good();
    r.email = u"kid";
    r.adult = false;
    Check(Check(r) == Outcome::BadEmail, "then the e-mail");
    r = Good();
    r.adult = false;
    Check(Check(r) == Outcome::NoAge, "then 13 or older");
    r = Good();
    r.name = u"Administrator";
    Check(Check(r) == Outcome::Reserved, "a reserved name");
    r = GoodCode();
    Check(Check(r) == Outcome::Made, "a good code request");
    r.name = u"Administrator";
    Check(Check(r) == Outcome::Made, "a code request for a reserved name is the server's to answer");
    r.name = u"ab";
    Check(Check(r) == Outcome::BadLength, "a code request's name length");
    r = GoodCode();
    r.email = u"kid@";
    Check(Check(r) == Outcome::BadEmail, "a code request's e-mail");
    r = GoodSet();
    Check(Check(r) == Outcome::Made, "a good new password");
    r.code = u"12345";
    r.password = u"abc";
    Check(Check(r) == Outcome::BadCode, "the code before the password");
    r = GoodSet();
    r.password = u"abc";
    Check(Check(r) == Outcome::BadPassword, "the new password's rule");
}

void TestExchange() {
    {
        Script s;
        s.answer = Reply(21, 0, 10, u"Coaster Kid", 77);
        s.piece = 5;
        const Result r = Run(s, Good());
        Check(r.outcome == Outcome::Made && r.name == u"Coaster Kid" && r.id == 77, "made, the reply in pieces");
        Check(s.host == "127.0.0.1" && s.port == 7598, "the host and port it was given");
        Check(s.sent == BuildRequest(Good(), 0, 10), "the request sent whole");
        Check(s.closed, "the connection is closed after");
    }
    const struct {
        uint16_t status;
        Outcome outcome;
        const char* what;
    } refusals[] = {{1, Outcome::Taken, "taken"},          {10, Outcome::Closed, "closed"},
                    {8, Outcome::IpLimit, "too many from here"}, {13, Outcome::BadPassword, "the password rule"},
                    {14, Outcome::WeakPassword, "too weak"},     {15, Outcome::EmailFull, "the e-mail's accounts"},
                    {6, Outcome::BadEmail, "the e-mail's form"}};
    for (const auto& f : refusals) {
        Script s;
        s.answer = Reply(21, f.status, 10);
        Check(Run(s, Good()).outcome == f.outcome, f.what);
    }
    {
        Script s;
        s.answer = Reply(23, 0, 10);
        const Result r = Run(s, GoodCode());
        Check(r.outcome == Outcome::Made && r.name.empty(), "the code request answered");
        Check(s.sent == BuildRequest(GoodCode(), 0, 10), "the code request sent whole");
    }
    {
        Script s;
        s.answer = Reply(23, 1, 10);
        Check(Run(s, GoodCode()).outcome == Outcome::NoReset, "no reset on the server");
    }
    {
        Script s;
        s.answer = Reply(25, 0, 10, u"Coaster Kid", 5);
        s.piece = 3;
        const Result r = Run(s, GoodSet());
        Check(r.outcome == Outcome::Made && r.name == u"Coaster Kid" && r.id == 5, "the password set");
        Check(s.sent == BuildRequest(GoodSet(), 0, 10), "the new password sent whole");
    }
    {
        Script s;
        s.answer = Reply(25, 3, 10);
        Check(Run(s, GoodSet()).outcome == Outcome::BadCode, "a wrong code");
        s = Script();
        s.answer = Reply(25, 4, 10);
        Check(Run(s, GoodSet()).outcome == Outcome::DeadCode, "a dead code");
    }
    {
        Script s;
        s.open = false;
        const Result r = Run(s, Good());
        Check(r.outcome == Outcome::Unreachable && r.why == "the connection was refused", "a refused connection");
    }
    {   // a server from before sign-up: an unknown type closes the connection
        Script s;
        s.closeAfter = true;
        const Result r = Run(s, GoodSet());
        Check(r.outcome == Outcome::Unreachable && r.why.find("closed") != std::string::npos, "a server without reset");
    }
    {
        Script s;   // silence
        const Result r = Run(s, Good(), 1);
        Check(r.outcome == Outcome::Unreachable && r.why.find("no reply") != std::string::npos, "no reply in time");
    }
    {
        Script s;
        s.answer = Reply(21, 0, 10, u"Coaster Kid", 1);
        Put32(&s.answer[8], 0x10000);   // a length past what a reply has
        Check(Run(s, Good()).outcome == Outcome::BadReply, "an over-long reply");
    }
    {
        Script s;
        s.answer = {'j', 'u', 'n', 'k', ' ', 'n', 'o', 't', ' ', 'B', 'O', 'S', 'S', ' ', 'a', 't', ' ', 'a', 'l', 'l'};
        Check(Run(s, Good()).outcome == Outcome::BadReply, "junk");
    }
    {   // the pre-checks send nothing
        Script s;
        Request r = Good();
        r.adult = false;
        Check(Run(s, r).outcome == Outcome::NoAge && s.opened == 0, "not 13 or older: nothing sent");
        r = Good();
        r.password = u"short";
        Check(Run(s, r).outcome == Outcome::BadPassword && s.opened == 0, "a short password: nothing sent");
        r = Good();
        r.email.clear();
        Check(Run(s, r).outcome == Outcome::BadEmail && s.opened == 0, "no e-mail: nothing sent");
        r = GoodSet();
        r.code = u"12a456";
        Check(Run(s, r).outcome == Outcome::BadCode && s.opened == 0, "a code that is not digits: nothing sent");
    }
}

// The job: its thread answers, Done turns true once, Take hands the result over.
void TestJob() {
    Script s;
    s.answer = Reply(21, 0, 10, u"Coaster Kid", 5);
    boss::SetTransportMaker([&s] { return std::make_unique<FakeTransport>(s); });
    {
        Job job;
        Check(!job.Running() && !job.Done(), "a job not started");
        job.Start("127.0.0.1", 17598, Good(), 2);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!job.Done() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        Check(job.Done() && !job.Running(), "the job answers");
        Result r = job.Take();
        Check(r.outcome == Outcome::Made && r.id == 5 && r.name == u"Coaster Kid", "the job's result");
        Check(!job.Done(), "taken once");
        Check(job.Take().name.empty(), "nothing left after Take");
        Check(s.port == 17598 && Get16(&s.sent[4]) == 2, "the job's port and language");
    }
    {   // let go of while it runs: the thread keeps its own share and never touches the job
        Script silent;
        boss::SetTransportMaker([&silent] { return std::make_unique<FakeTransport>(silent); });
        auto job = std::make_unique<Job>();
        job->Start("127.0.0.1", 17598, GoodSet(), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        job.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));   // the thread sees `stopping` and ends
        Check(true, "a job let go of while it runs");
    }
    boss::SetTransportMaker(nullptr);
    char16_t text[4] = {u'a', u'b', u'c', u'd'};
    Wipe(text, 4);
    Check(text[0] == 0 && text[3] == 0, "Wipe clears a text");
    std::u16string password = kPassword;
    Wipe(password);
    Check(password.empty(), "Wipe empties a password");
    Request r = Good();
    r.code = u"123456";
    Wipe(r);
    Check(r.password.empty() && r.email.empty() && r.code.empty() && r.name == u"Coaster Kid",
          "Wipe of a request: its password, e-mail and code");
}

void TestTexts() {
    for (int i = 0; i < static_cast<int>(Text::Count); ++i)
        Check(GetText(static_cast<Text>(i))[0] != 0, "text " + std::to_string(i) + " is not empty");
    CheckEq(GetText(Text::Warning), u"Don't use a password you use anywhere else. This 1999 game sends it unencrypted.",
            "the line under the password boxes");
    CheckEq(GetText(Text::Made), u"Account created. You can log in now.", "the account made");
    CheckEq(GetText(Text::Sent), u"If that name and e-mail match, a code is on its way. Check your junk folder too.",
            "the code request's answer");
    CheckEq(GetText(Text::NoReset), u"Password reset isn't available yet. Ask on the Discord.", "no reset");
    CheckEq(RefusalText(Outcome::Mismatch), u"The passwords don't match.", "the passwords differ");
    std::u16string unreachable;
    for (const char* c = boss::kUnreachableText; *c; ++c) unreachable += static_cast<char16_t>(*c);
    CheckEq(RefusalText(Outcome::Unreachable), unreachable, "unreachable is the online client's own sentence");
    const struct {
        Outcome outcome;
        Text text;
    } map[] = {{Outcome::Taken, Text::Taken},           {Outcome::Reserved, Text::NotAllowed},
               {Outcome::Blocked, Text::NotAllowed},    {Outcome::BadPassword, Text::BadPassword},
               {Outcome::WeakPassword, Text::WeakPassword}, {Outcome::BadEmail, Text::BadEmail},
               {Outcome::EmailFull, Text::EmailFull},   {Outcome::NoAge, Text::NoAge},
               {Outcome::IpLimit, Text::TooMany},       {Outcome::CodeLimit, Text::TooManyCodes},
               {Outcome::DayLimit, Text::TooManyToday}, {Outcome::Closed, Text::Closed},
               {Outcome::NoReset, Text::NoReset},       {Outcome::BadCode, Text::BadCode},
               {Outcome::DeadCode, Text::DeadCode},     {Outcome::ServerError, Text::Failed},
               {Outcome::Malformed, Text::Failed},      {Outcome::BadReply, Text::Failed}};
    for (const auto& m : map)
        CheckEq(RefusalText(m.outcome), GetText(m.text), "refusal " + std::to_string(static_cast<int>(m.outcome)));
    CheckEq(GetText(Text::Count), u"", "past the table");
}

}  // namespace

int main() {
    TestMessages();
    TestParse();
    TestNames();
    TestRules();
    TestExchange();
    TestJob();
    TestTexts();
    std::printf("%d of %d sign-up checks passed\n", gChecks - gFailures, gChecks);
    return gFailures == 0 ? 0 : 1;
}
