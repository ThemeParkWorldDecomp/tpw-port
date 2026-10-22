// gdi_text.cpp: the CoreText side of gdi.cpp (see there for how the Mac shim behaved). Fonts
// are found among the files AddFontResource registered, then among the system fonts, and text is
// laid out without kerning or ligatures (QuickDraw's StdText has neither).
//
// What differs from QuickDraw: its metrics came from the font's FOND tables as integers; here
// the ascent is CoreText's rounded up and the average width is the 92-character reference string's
// width truncated to whole pixels and divided by 92. A face with no bold or italic variant is
// drawn regular (the Mac synthesized the style).
#include "gdi_text.h"
#include "resource_fork.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <vector>

namespace tpw {
void Log(const char* format, ...) __attribute__((format(printf, 1, 2)));   // platform.h
}

namespace tpw {
namespace gdi_text {

namespace {

// Owns one retained CoreFoundation object.
template<class T> class Ref {
public:
    Ref() = default;
    explicit Ref(T ref) : mRef(ref) {}
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    Ref(Ref&& other) noexcept : mRef(other.mRef) { other.mRef = nullptr; }
    Ref& operator=(Ref&& other) noexcept {
        if (this != &other) {
            reset(other.mRef);
            other.mRef = nullptr;
        }
        return *this;
    }
    ~Ref() { reset(nullptr); }
    void reset(T ref) {
        if (mRef != nullptr) CFRelease(mRef);
        mRef = ref;
    }
    T get() const { return mRef; }
    explicit operator bool() const { return mRef != nullptr; }

private:
    T mRef = nullptr;
};

std::string ToStd(CFTypeRef value) {
    if (value == nullptr || CFGetTypeID(value) != CFStringGetTypeID()) return std::string();
    char buffer[512];
    if (!CFStringGetCString(static_cast<CFStringRef>(value), buffer, sizeof buffer, kCFStringEncodingUTF8)) return std::string();
    return buffer;
}

std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// The text the game passes is Mac Roman bytes (snprintf "%ls" of the sign's UTF-16).
Ref<CFStringRef> MacRomanString(const char* text, int32_t length) {
    if (text == nullptr || length < 0) length = 0;
    Ref<CFStringRef> string(CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(text ? text : ""), length,
                                                    kCFStringEncodingMacRoman, false));
    if (!string) string.reset(CFStringCreateWithCString(nullptr, "", kCFStringEncodingASCII));
    return string;
}

// A one-line layout of the text in the font, without kerning or ligatures.
Ref<CTLineRef> MakeLine(CTFontRef font, const char* text, int32_t length) {
    Ref<CFStringRef> string = MacRomanString(text, length);
    const int zero = 0;
    Ref<CFNumberRef> none(CFNumberCreate(nullptr, kCFNumberIntType, &zero));
    // The glyphs take the context's fill colour (CoreText's default would be black).
    const void* keys[] = {kCTFontAttributeName, kCTKernAttributeName, kCTLigatureAttributeName,
                          kCTForegroundColorFromContextAttributeName};
    const void* values[] = {font, none.get(), none.get(), kCFBooleanTrue};
    Ref<CFDictionaryRef> attributes(CFDictionaryCreate(nullptr, keys, values, 4, &kCFTypeDictionaryKeyCallBacks,
                                                       &kCFTypeDictionaryValueCallBacks));
    Ref<CFAttributedStringRef> attributed(CFAttributedStringCreate(nullptr, string.get(), attributes.get()));
    return Ref<CTLineRef>(CTLineCreateWithAttributedString(attributed.get()));
}

double LineWidth(CTLineRef line) { return CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr); }

// ---- Font files -----------------------------------------------------------------------------

struct Face {
    std::string family;       // kCTFontFamilyNameAttribute
    std::string fullName;     // kCTFontDisplayNameAttribute
    std::string postScript;   // kCTFontNameAttribute
    Ref<CTFontDescriptorRef> descriptor;
    Ref<CFDataRef> data;      // the sfnt of a face made from memory, kept alive with it
};

// Every face of every font file added so far, in the order added; the files by host path.
std::vector<Face>& Faces() {
    static auto* faces = new std::vector<Face>;
    return *faces;
}

std::map<std::string, int32_t>& AddedFiles() {
    static auto* files = new std::map<std::string, int32_t>;
    return *files;
}

bool NameIs(const std::string& wanted, CTFontRef font) {
    Ref<CFStringRef> family(CTFontCopyFamilyName(font));
    Ref<CFStringRef> full(CTFontCopyFullName(font));
    Ref<CFStringRef> postScript(CTFontCopyPostScriptName(font));
    return Lower(ToStd(family.get())) == wanted || Lower(ToStd(full.get())) == wanted ||
           Lower(ToStd(postScript.get())) == wanted;
}

// The 92 characters of the Mac shim's reference string (0x100125c4); 0xa3 is the pound sign.
const char kReference[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@\xa3$%^&*()_+-=[]{}()|;':?/<>,.";

}  // namespace

void LogOnce(const std::string& key, const std::string& message) {
    static auto* seen = new std::set<std::string>;
    if (seen->insert(key).second) Log("gdi: %s", message.c_str());
}

namespace {

// Keeps the face of one descriptor: the three names are logged and the face joins the lookup.
// what says where it came from, for the log.
void AddFace(CTFontDescriptorRef descriptor, const std::string& hostPath, const std::string& what, Ref<CFDataRef> data) {
    Face face;
    Ref<CFTypeRef> family(CTFontDescriptorCopyAttribute(descriptor, kCTFontFamilyNameAttribute));
    Ref<CFTypeRef> full(CTFontDescriptorCopyAttribute(descriptor, kCTFontDisplayNameAttribute));
    Ref<CFTypeRef> postScript(CTFontDescriptorCopyAttribute(descriptor, kCTFontNameAttribute));
    face.family = ToStd(family.get());
    face.fullName = ToStd(full.get());
    face.postScript = ToStd(postScript.get());
    CFRetain(descriptor);
    face.descriptor.reset(descriptor);
    face.data = std::move(data);
    Log("gdi: font file %s%s: family \"%s\", name \"%s\", PostScript \"%s\"", hostPath.c_str(), what.c_str(),
        face.family.c_str(), face.fullName.c_str(), face.postScript.c_str());
    Faces().push_back(std::move(face));
}

// Reads up to limit bytes of the file; false when it cannot be opened or is longer than limit.
bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& bytes, size_t limit) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;
    bytes.clear();
    uint8_t chunk[65536];
    size_t got;
    while (bytes.size() <= limit && (got = std::fread(chunk, 1, std::min(sizeof chunk, limit + 1 - bytes.size()), file)) > 0) {
        bytes.insert(bytes.end(), chunk, chunk + got);
    }
    std::fclose(file);
    return bytes.size() <= limit;
}

// True when the first four bytes are an sfnt magic: 00 01 00 00, 'true', 'OTTO' or 'ttcf'.
bool LooksLikeSfnt(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 4) return false;
    const uint32_t magic = static_cast<uint32_t>(bytes[0]) << 24 | static_cast<uint32_t>(bytes[1]) << 16 |
                           static_cast<uint32_t>(bytes[2]) << 8 | bytes[3];
    return magic == 0x00010000 || magic == ForkType("true") || magic == ForkType("OTTO") || magic == ForkType("ttcf");
}

// The sfnt resources of a resource fork, registered for the process from memory.
int32_t AddForkFonts(const std::string& hostPath, const std::vector<uint8_t>& fork) {
    int32_t faces = 0;
    for (const ForkResource& resource : FindResources(fork.data(), fork.size(), ForkType("sfnt"))) {
        Ref<CFDataRef> data(CFDataCreate(nullptr, resource.data, static_cast<CFIndex>(resource.size)));
        if (!data) continue;
        // The faces of the memory block as descriptors; they need no registration to be instantiated,
        // and each keeps the data alive.
        Ref<CFArrayRef> descriptors(CTFontManagerCreateFontDescriptorsFromData(data.get()));
        const CFIndex count = descriptors ? CFArrayGetCount(descriptors.get()) : 0;
        if (count == 0) {
            LogOnce("badsfnt:" + hostPath, "AddFontResource: sfnt resource " + std::to_string(resource.id) + " of " + hostPath +
                                               " is not a font CoreText can read");
            continue;
        }
        for (CFIndex i = 0; i < count; ++i) {
            auto descriptor = static_cast<CTFontDescriptorRef>(const_cast<void*>(CFArrayGetValueAtIndex(descriptors.get(), i)));
            Ref<CFDataRef> keep(static_cast<CFDataRef>(CFRetain(data.get())));
            AddFace(descriptor, hostPath, " (sfnt " + std::to_string(resource.id) + " of its resource fork)", std::move(keep));
            ++faces;
        }
    }
    return faces;
}

}  // namespace

int32_t AddFontFile(const std::string& hostPath) {
    auto& added = AddedFiles();
    const auto done = added.find(hostPath);
    if (done != added.end()) return done->second;
    // The data disc's fonts are Macintosh suitcases: an empty data fork and the sfnt in the resource
    // fork, which the extractor wrote beside the file as "<name>.rsrc".
    std::vector<uint8_t> head;
    ReadFileBytes(hostPath, head, 4);
    std::error_code exists;
    const std::string forkPath = hostPath + ".rsrc";
    if (!LooksLikeSfnt(head) && std::filesystem::is_regular_file(forkPath, exists)) {
        std::vector<uint8_t> fork;
        int32_t faces = 0;
        if (ReadFileBytes(forkPath, fork, 64u << 20)) faces = AddForkFonts(hostPath, fork);
        if (faces == 0) LogOnce("nofaces:" + hostPath, "AddFontResource: no sfnt resource in " + forkPath);
        added[hostPath] = faces;
        return faces;
    }
    Ref<CFURLRef> url(CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(hostPath.c_str()),
                                                              static_cast<CFIndex>(hostPath.size()), false));
    if (!url) return 0;
    Ref<CFArrayRef> descriptors(CTFontManagerCreateFontDescriptorsFromURL(url.get()));
    const CFIndex count = descriptors ? CFArrayGetCount(descriptors.get()) : 0;
    if (count == 0) {
        LogOnce("nofaces:" + hostPath, "AddFontResource: no faces in " + hostPath);
        return 0;
    }
    // A file that is already registered (a system font) counts as success.
    CFErrorRef error = nullptr;
    if (!CTFontManagerRegisterFontsForURL(url.get(), kCTFontManagerScopeProcess, &error) && error != nullptr &&
        CFErrorGetCode(error) != kCTFontManagerErrorAlreadyRegistered) {
        LogOnce("noreg:" + hostPath, "AddFontResource: could not register " + hostPath);
    }
    if (error != nullptr) CFRelease(error);
    for (CFIndex i = 0; i < count; ++i) {
        auto descriptor = static_cast<CTFontDescriptorRef>(const_cast<void*>(CFArrayGetValueAtIndex(descriptors.get(), i)));
        AddFace(descriptor, hostPath, "", Ref<CFDataRef>());
    }
    added[hostPath] = static_cast<int32_t>(count);
    return static_cast<int32_t>(count);
}

// ---- Fonts ----------------------------------------------------------------------------------

struct Font::Impl {
    Ref<CTFontRef> font;
    bool underline = false;
    int32_t ascent = 0;
    int32_t avgWidth = 0;
};

Font::Font() : mImpl(new Impl) {}
Font::~Font() = default;

std::unique_ptr<Font> Font::Create(const Style& style) {
    const int32_t height = style.height < 0 ? -style.height : style.height;
    const CGFloat size = height == 0 ? 12 : static_cast<CGFloat>(height);   // TextSize(0) is the default size
    const std::string wanted = Lower(style.face);
    Ref<CTFontRef> base;
    // The game's own font files first: an exact full or PostScript name, then a family (its
    // regular face), so a file named like a system font is never beaten by the system's.
    const Face* match = nullptr;
    for (const Face& face : Faces()) {
        if (Lower(face.fullName) == wanted || Lower(face.postScript) == wanted) {
            match = &face;
            break;
        }
    }
    if (match == nullptr) {
        for (const Face& face : Faces()) {
            if (Lower(face.family) != wanted) continue;
            Ref<CTFontRef> candidate(CTFontCreateWithFontDescriptor(face.descriptor.get(), size, nullptr));
            const bool bold = candidate && (CTFontGetSymbolicTraits(candidate.get()) & kCTFontBoldTrait) != 0;
            if (match == nullptr || !bold) match = &face;
            if (!bold) break;
        }
    }
    if (match != nullptr) base.reset(CTFontCreateWithFontDescriptor(match->descriptor.get(), size, nullptr));
    if (!base) {
        // A system font of that name; CoreText answers Helvetica for a name it does not know.
        Ref<CFStringRef> name(CFStringCreateWithCString(nullptr, style.face.c_str(), kCFStringEncodingMacRoman));
        if (name) base.reset(CTFontCreateWithName(name.get(), size, nullptr));
        if (!base || !NameIs(wanted, base.get())) {
            LogOnce("face:" + wanted, "no font named \"" + style.face + "\", using Helvetica");
            Ref<CFStringRef> helvetica(CFStringCreateWithCString(nullptr, "Helvetica", kCFStringEncodingASCII));
            base.reset(CTFontCreateWithName(helvetica.get(), size, nullptr));
        }
    }
    if (!base) return nullptr;
    // Bold from lfWeight > 699 and italic from lfItalic, when the face has such a variant.
    CTFontSymbolicTraits traits = 0;
    if (style.weight > 699) traits |= kCTFontBoldTrait;
    if (style.italic) traits |= kCTFontItalicTrait;
    if (traits != 0) {
        Ref<CTFontRef> styled(CTFontCreateCopyWithSymbolicTraits(base.get(), 0.0, nullptr, traits, traits));
        if (styled) {
            base = std::move(styled);
        } else {
            LogOnce("style:" + wanted, "\"" + style.face + "\" has no bold or italic variant, drawn regular");
        }
    }
    std::unique_ptr<Font> font(new Font);
    font->mImpl->font = std::move(base);
    font->mImpl->underline = style.underline;
    font->mImpl->ascent = static_cast<int32_t>(std::ceil(CTFontGetAscent(font->mImpl->font.get())));
    Ref<CTLineRef> line = MakeLine(font->mImpl->font.get(), kReference, 92);
    if (line) font->mImpl->avgWidth = static_cast<int32_t>(LineWidth(line.get())) / 92;
    return font;
}

int32_t Font::Ascent() const { return mImpl->ascent; }
int32_t Font::AverageWidth() const { return mImpl->avgWidth; }

std::string Font::FamilyName() const {
    Ref<CFStringRef> family(CTFontCopyFamilyName(mImpl->font.get()));
    return ToStd(family.get());
}

double Font::Advance(const char* text, int32_t length) const {
    if (text == nullptr || length <= 0) return 0;
    Ref<CTLineRef> line = MakeLine(mImpl->font.get(), text, length);
    return line ? LineWidth(line.get()) : 0;
}

void Font::Draw(uint8_t* ink, int32_t width, int32_t height, int32_t x, int32_t baseline, double scale, const char* text,
                int32_t length) const {
    if (text == nullptr || length <= 0 || width <= 0 || height <= 0) return;
    Ref<CGColorSpaceRef> grey(CGColorSpaceCreateDeviceGray());
    Ref<CGContextRef> context(CGBitmapContextCreate(ink, static_cast<size_t>(width), static_cast<size_t>(height), 8,
                                                    static_cast<size_t>(width), grey.get(), kCGImageAlphaNone));
    if (!context) return;
    CGContextRef cg = context.get();
    CGContextSetShouldAntialias(cg, false);
    CGContextSetShouldSmoothFonts(cg, false);
    CGContextSetAllowsFontSmoothing(cg, false);
    CGContextSetShouldSubpixelPositionFonts(cg, false);
    CGContextSetShouldSubpixelQuantizeFonts(cg, false);
    CGContextSetGrayFillColor(cg, 1.0, 1.0);
    Ref<CTLineRef> line = MakeLine(mImpl->font.get(), text, length);
    if (!line) return;
    // CoreGraphics counts rows from the bottom; the buffer's first row is the top one.
    const CGFloat left = static_cast<CGFloat>(x);
    const CGFloat floor = static_cast<CGFloat>(height - baseline);
    // The horizontal scale goes through the CTM: CTLineDraw would not stretch the advances by the text matrix.
    CGContextSaveGState(cg);
    CGContextTranslateCTM(cg, left, floor);
    CGContextScaleCTM(cg, static_cast<CGFloat>(scale), 1);
    CGContextSetTextPosition(cg, 0, 0);
    CTLineDraw(line.get(), cg);
    CGContextRestoreGState(cg);
    if (mImpl->underline) {
        const CGFloat thickness = std::max<CGFloat>(1, std::round(CTFontGetUnderlineThickness(mImpl->font.get())));
        const CGFloat position = std::round(CTFontGetUnderlinePosition(mImpl->font.get()));
        const CGFloat extent = static_cast<CGFloat>(LineWidth(line.get()) * scale);
        CGContextFillRect(cg, CGRectMake(left, floor + position - thickness / 2, extent, thickness));
    }
}

}  // namespace gdi_text
}  // namespace tpw
