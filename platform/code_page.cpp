// code_page.cpp: the code page of the game's narrow text. The game narrows a name it uses as a file name (a player's
// folder, "save:users:1Tess") with WideCharToMultiByte(CP_ACP) and widens it again when it lists the folders. The PC
// editions ran on a Windows of their own language, so CP_ACP was 1255 for the Hebrew one and 932, 949, 950 or 936 for
// the Japanese, Korean and Chinese ones; the Mac converted through its Latin script. Here code page 0 stands for that
// Latin-1 of the shim (macdoze_misc.cpp) and is what every language but those five keeps.
//
// The conversions are the host's: CoreFoundation's on the Mac, Windows' own on Windows (posix_win.cpp), iconv elsewhere.
#include <cstring>
#include <mutex>
#include <unordered_map>

#if defined(__APPLE__) && !defined(TPW_CODE_PAGE_ICONV)
#define Rect MacTypesRect   // MacTypes.h's Rect, which CoreFoundation brings in, is not the game's (interfacelib.h)
#include <CoreFoundation/CoreFoundation.h>
#undef Rect
#elif !defined(_WIN32)
#include <iconv.h>
#endif

#include "platform.h"
#include "platform/macdoze.h"

namespace tpw {

namespace {
uint32_t gCodePage = 0;

// The host's conversion of `bytes` in `codePage` to UTF-16 and back, as lenient as the host is: the strict forms below
// check that the text comes back to the same bytes.
#if defined(__APPLE__) && !defined(TPW_CODE_PAGE_ICONV)
bool HostDecode(uint32_t codePage, const std::string& bytes, std::u16string& text) {
    const CFStringEncoding encoding = CFStringConvertWindowsCodepageToEncoding(codePage);
    if (encoding == kCFStringEncodingInvalidId) return false;
    CFStringRef string = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(bytes.data()),
                                                 static_cast<CFIndex>(bytes.size()), encoding, false);
    if (string == nullptr) return false;
    text.resize(static_cast<size_t>(CFStringGetLength(string)));
    CFStringGetCharacters(string, CFRangeMake(0, static_cast<CFIndex>(text.size())), reinterpret_cast<UniChar*>(text.data()));
    CFRelease(string);
    return true;
}

bool HostEncode(uint32_t codePage, const std::u16string& text, std::string& bytes) {
    const CFStringEncoding encoding = CFStringConvertWindowsCodepageToEncoding(codePage);
    if (encoding == kCFStringEncodingInvalidId) return false;
    CFStringRef string = CFStringCreateWithCharacters(nullptr, reinterpret_cast<const UniChar*>(text.data()),
                                                      static_cast<CFIndex>(text.size()));
    if (string == nullptr) return false;
    const CFRange range = CFRangeMake(0, static_cast<CFIndex>(text.size()));
    CFIndex size = 0;
    // No loss byte: a character the code page lacks stops the conversion short.
    bool whole = CFStringGetBytes(string, range, encoding, 0, false, nullptr, 0, &size) == range.length;
    if (whole) {
        bytes.resize(static_cast<size_t>(size));
        whole = CFStringGetBytes(string, range, encoding, 0, false, reinterpret_cast<UInt8*>(bytes.data()), size, &size) ==
                range.length;
    }
    CFRelease(string);
    return whole;
}
#elif defined(_WIN32)
bool HostDecode(uint32_t codePage, const std::string& bytes, std::u16string& text) {
    return tpw_win_decode(codePage, bytes, text);
}

bool HostEncode(uint32_t codePage, const std::u16string& text, std::string& bytes) {
    return tpw_win_encode(codePage, text, bytes);
}
#else
bool Iconv(const char* to, const char* from, const char* in, size_t inSize, std::string& out) {
    const iconv_t cd = iconv_open(to, from);
    if (cd == reinterpret_cast<iconv_t>(-1)) return false;
    out.assign(inSize * 4 + 4, '\0');
    char* source = const_cast<char*>(in);
    char* dest = out.data();
    size_t left = out.size();
    const bool whole = iconv(cd, &source, &inSize, &dest, &left) != static_cast<size_t>(-1) && inSize == 0;
    iconv_close(cd);
    out.resize(out.size() - left);
    return whole;
}

std::string IconvName(uint32_t codePage) { return "CP" + std::to_string(codePage); }

bool HostDecode(uint32_t codePage, const std::string& bytes, std::u16string& text) {
    std::string wide;
    if (!Iconv("UTF-16LE", IconvName(codePage).c_str(), bytes.data(), bytes.size(), wide)) return false;
    text.resize(wide.size() / 2);
    for (size_t i = 0; i < text.size(); ++i)
        text[i] = static_cast<char16_t>(static_cast<uint8_t>(wide[2 * i]) | static_cast<uint8_t>(wide[2 * i + 1]) << 8);
    return true;
}

bool HostEncode(uint32_t codePage, const std::u16string& text, std::string& bytes) {
    std::string wide;
    for (char16_t c : text) {
        wide.push_back(static_cast<char>(c & 0xff));
        wide.push_back(static_cast<char>(c >> 8));
    }
    return Iconv(IconvName(codePage).c_str(), "UTF-16LE", wide.data(), wide.size(), bytes);
}
#endif

// One character's conversions, kept: the game converts the same few characters again and again.
std::mutex gCacheLock;
std::unordered_map<uint32_t, char16_t> gWidened;      // (code page << 16 | bytes) to the character, 0 for none
std::unordered_map<uint64_t, std::string> gNarrowed;  // (code page << 16 | character) to its bytes, empty for none
}  // namespace

uint32_t CodePageOfLanguage(uint32_t languageId) {
    switch (languageId) {
        case 0x40d: return 1255;   // Hebrew
        case 0x11: return 932;     // Japanese
        case 0x412: return 949;    // Korean
        case 0x404: return 950;    // TraditionalChinese
        case 0x804: return 936;    // SimplifiedChinese
        default: return 0;
    }
}

void UseCodePageOfLanguage(uint32_t languageId) { SetGameCodePage(CodePageOfLanguage(languageId)); }
void SetGameCodePage(uint32_t codePage) { gCodePage = codePage; }
uint32_t GameCodePage() { return gCodePage; }

bool IsLeadByte(uint32_t codePage, uint8_t byte) {
    switch (codePage) {
        case 932: return (byte >= 0x81 && byte <= 0x9f) || (byte >= 0xe0 && byte <= 0xfc);
        case 936:
        case 949:
        case 950: return byte >= 0x81 && byte <= 0xfe;
        default: return false;
    }
}

bool IsGameLeadByte(uint8_t byte) { return IsLeadByte(gCodePage, byte); }

bool DecodeCodePage(uint32_t codePage, const std::string& bytes, std::u16string& text) {
    if (codePage == 0) {
        text.assign(bytes.begin(), bytes.end());
        for (size_t i = 0; i < bytes.size(); ++i) text[i] = static_cast<uint8_t>(bytes[i]);
        return true;
    }
    std::string back;
    return HostDecode(codePage, bytes, text) && HostEncode(codePage, text, back) && back == bytes;
}

bool EncodeCodePage(uint32_t codePage, const std::u16string& text, std::string& bytes) {
    if (codePage == 0) {
        bytes.clear();
        for (char16_t c : text) {
            if (c > 0xff) return false;
            bytes.push_back(static_cast<char>(c));
        }
        return true;
    }
    std::u16string back;
    return HostEncode(codePage, text, bytes) && HostDecode(codePage, bytes, back) && back == text;
}

std::string NarrowGameText(const char16_t* text, size_t length, char defaultChar, bool& usedDefault) {
    std::string out;
    usedDefault = false;
    for (size_t i = 0; i < length; ++i) {
        const char16_t c = text[i];
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
            continue;
        }
        const uint64_t key = static_cast<uint64_t>(gCodePage) << 16 | c;
        std::string bytes;
        {
            std::lock_guard<std::mutex> hold(gCacheLock);
            const auto found = gNarrowed.find(key);
            if (found != gNarrowed.end()) {
                bytes = found->second;
            } else {
                if (!EncodeCodePage(gCodePage, std::u16string(1, c), bytes)) bytes.clear();
                gNarrowed[key] = bytes;
            }
        }
        if (bytes.empty()) {
            out.push_back(defaultChar);
            usedDefault = true;
        } else {
            out += bytes;
        }
    }
    return out;
}

std::u16string WidenGameText(const char* text, size_t length) {
    std::u16string out;
    for (size_t i = 0; i < length; ++i) {
        const uint8_t byte = static_cast<uint8_t>(text[i]);
        if (byte < 0x80) {
            out.push_back(byte);
            continue;
        }
        // A lead byte takes the next as its trail; a lead byte at the end, or before the NUL, is a character of its own.
        const bool pair = IsGameLeadByte(byte) && i + 1 < length && text[i + 1] != 0;
        const std::string bytes = pair ? std::string(text + i, 2) : std::string(1, static_cast<char>(byte));
        uint32_t key = static_cast<uint32_t>(byte);
        if (pair) key = key << 8 | static_cast<uint8_t>(text[i + 1]);
        char16_t c = 0;
        {
            std::lock_guard<std::mutex> hold(gCacheLock);
            const uint32_t cacheKey = (gCodePage & 0xffff) << 16 | key;
            const auto found = gWidened.find(cacheKey);
            if (found != gWidened.end()) {
                c = found->second;
            } else {
                std::u16string wide;
                if (DecodeCodePage(gCodePage, bytes, wide) && wide.size() == 1) c = wide[0];
                gWidened[cacheKey] = c;
            }
        }
        if (c == 0) {
            out.push_back(u'?');   // a byte the code page has no character for
        } else {
            out.push_back(c);
            if (pair) ++i;
        }
    }
    return out;
}

// ---- Host file names ------------------------------------------------------------------------------------------------

namespace {
bool HasHighByte(const std::string& text) {
    for (char c : text)
        if (static_cast<uint8_t>(c) >= 0x80) return true;
    return false;
}

#ifndef _WIN32
std::string Utf8Of(const std::u16string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t c = text[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < text.size() && text[i + 1] >= 0xdc00 && text[i + 1] < 0xe000)
            c = 0x10000 + ((c - 0xd800) << 10) + (text[++i] - 0xdc00);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xc0 | c >> 6));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xe0 | c >> 12));
            out.push_back(static_cast<char>(0x80 | (c >> 6 & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | c >> 18));
            out.push_back(static_cast<char>(0x80 | (c >> 12 & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (c >> 6 & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        }
    }
    return out;
}

// The UTF-16 of UTF-8 `text`; false when it is not well-formed UTF-8.
bool Utf16Of(const std::string& text, std::u16string& out) {
    out.clear();
    for (size_t i = 0; i < text.size();) {
        const uint8_t lead = static_cast<uint8_t>(text[i]);
        int extra = -1;
        if (lead < 0x80) extra = 0;
        else if (lead >= 0xc2 && lead < 0xe0) extra = 1;
        else if (lead >= 0xe0 && lead < 0xf0) extra = 2;
        else if (lead >= 0xf0 && lead < 0xf5) extra = 3;
        if (extra < 0 || text.size() - i <= static_cast<size_t>(extra)) return false;   // not UTF-8, or cut short
        uint32_t c = extra == 0 ? lead : lead & (0x3f >> extra);
        for (int k = 1; k <= extra; ++k) {
            const uint8_t next = static_cast<uint8_t>(text[i + k]);
            if ((next & 0xc0) != 0x80) return false;
            c = c << 6 | (next & 0x3f);
        }
        static const uint32_t kSmallest[] = {0, 0x80, 0x800, 0x10000};
        if (c < kSmallest[extra] || c > 0x10ffff || (c >= 0xd800 && c < 0xe000)) return false;
        if (c >= 0x10000) {
            out.push_back(static_cast<char16_t>(0xd800 + ((c - 0x10000) >> 10)));
            out.push_back(static_cast<char16_t>(0xdc00 + ((c - 0x10000) & 0x3ff)));
        } else {
            out.push_back(static_cast<char16_t>(c));
        }
        i += static_cast<size_t>(extra) + 1;
    }
    return true;
}
#endif

// The composed form (NFC) of a name: the Mac's HFS+ gives names back decomposed, a Hangul syllable or a kana with its
// voicing mark as two characters that the code page has only as one.
void Compose(std::u16string& text) {
#if defined(__APPLE__) && !defined(TPW_CODE_PAGE_ICONV)
    CFMutableStringRef string = CFStringCreateMutable(nullptr, 0);
    CFStringAppendCharacters(string, reinterpret_cast<const UniChar*>(text.data()), static_cast<CFIndex>(text.size()));
    CFStringNormalize(string, kCFStringNormalizationFormC);
    text.resize(static_cast<size_t>(CFStringGetLength(string)));
    CFStringGetCharacters(string, CFRangeMake(0, static_cast<CFIndex>(text.size())), reinterpret_cast<UniChar*>(text.data()));
    CFRelease(string);
#else
    (void)text;
#endif
}
}  // namespace

std::filesystem::path HostNameOf(const std::string& gameName) {
    std::u16string text;
    if (gCodePage == 0 || !HasHighByte(gameName) || !DecodeCodePage(gCodePage, gameName, text))
        return std::filesystem::path(gameName);
#ifdef _WIN32
    return std::filesystem::path(std::wstring(text.begin(), text.end()));
#else
    return std::filesystem::path(Utf8Of(text));
#endif
}

std::string GameNameOf(const std::filesystem::path& hostName) {
    if (gCodePage == 0) return hostName.string();
    std::u16string text;
#ifdef _WIN32
    const std::wstring& native = hostName.native();
    bool high = false;
    for (wchar_t c : native) high = high || c >= 0x80;
    if (!high) return hostName.string();
    text.assign(native.begin(), native.end());
#else
    const std::string& native = hostName.native();
    if (!HasHighByte(native) || !Utf16Of(native, text)) return native;
#endif
    Compose(text);
    std::string bytes;
    return EncodeCodePage(gCodePage, text, bytes) ? bytes : hostName.string();
}

}  // namespace tpw
