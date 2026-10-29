// old_text_tables.cpp: CText::Init's fix for a language of the older build (include/platform/old_text_tables.h,
// TPW_PORT_FIX oldtables in src/game/Text.cpp). The edition's own language is opened beside the chosen one, and the
// strings and fonts the older build lacks are taken from it; the language's files are read as the disc has them.
#include "platform/old_text_tables.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "TbIRLE4bitFont.h"
#include "TbIResourceIndex.h"
#include "TbITable.h"
#include "TbTextResourceManager.h"
#include "platform/samsutils.h"

namespace {

// The edition's own language: English in the European game folder, American in the US one, whichever is there with
// the later build's index (one that names POSTCARD as 1052). Its multibyte converter is opened first, so a font taken
// from its index has one (TbIRLE4bitFont's constructor takes the index's).
struct BaseLanguage {
    TbTextResourceManager mManager;
    TbIMBToUnicode* mToUnicode = nullptr;
    const char* mName = nullptr;
    bool Open() {
        if (!mManager.Open("data:Language:").Succeeded()) return false;
        for (const char* name : {"English", "American"}) {
            if (!mManager.SetLanguage(name).Succeeded()) continue;
            if (mManager.mIndex->GetItem(kOldTextPostcardFont) == nullptr) continue;
            if (!mManager.GetResource(&mToUnicode, 1).Succeeded()) continue;
            mName = name;
            return true;
        }
        return false;
    }
    ~BaseLanguage() {
        if (mToUnicode != nullptr) mToUnicode->Release();
    }
};

// Opened by the first call that needs it, closed by OldTextDone. Never destroyed at exit: a language left open by a
// CText::Init that failed is closed by the next OldTextDone.
BaseLanguage* gBase = nullptr;
bool gBaseTried = false;

BaseLanguage* Base() {
    if (!gBaseTried) {
        gBaseTried = true;
        gBase = new BaseLanguage;
        if (!gBase->Open()) {
            delete gBase;
            gBase = nullptr;
        }
    }
    return gBase;
}

// A table as the later build has it: the strings of the language's older table with those the later build added, from
// the base language's table. It keeps both tables (their strings are theirs) and the language's converters.
class LaterBuildTable : public TbITable {
public:
    LaterBuildTable(TbITable* language, TbITable* base, const std::vector<const unsigned char*>& strings)
        : mLanguage(language), mBase(base) {
        mCount = static_cast<uint32_t>(strings.size());
        mOwnedStrings = new const unsigned char*[strings.size()];
        std::copy(strings.begin(), strings.end(), mOwnedStrings);
        mStrings = mOwnedStrings;
        mMBToUnicode = language->mMBToUnicode;
        if (mMBToUnicode != nullptr) mMBToUnicode->AddRef();
        mUnicodeToMB = language->mUnicodeToMB;
        if (mUnicodeToMB != nullptr) mUnicodeToMB->AddRef();
    }
    ~LaterBuildTable() override {
        mLanguage->Release();
        mBase->Release();
    }
    TbITable* mLanguage;
    TbITable* mBase;
};

}  // namespace

bool OldTextTable(TbTextResourceManager& language, void** table, uint32_t id, const char* name) {
    (void)language;
    auto* own = static_cast<TbITable*>(*table);
    if (own == nullptr || !IsOldTextTable(id, own->mCount)) return false;
    BaseLanguage* base = Base();
    if (base == nullptr) return false;
    TbITable* later = nullptr;
    if (!base->mManager.GetResource(&later, id).Succeeded()) return false;
    std::vector<const unsigned char*> strings;
    if (!MergeOldTextTable(id, own->mStrings, own->mCount, later->mStrings, later->mCount, strings)) {
        later->Release();
        return false;
    }
    char message[256];
    std::snprintf(message, sizeof(message), "the language's %s has the older build's %u strings: %u with %s's", name,
                  own->mCount, static_cast<uint32_t>(strings.size()), base->mName);
    SamsUtilities::UDebug(message);
    *table = new LaterBuildTable(own, later, strings);
    return true;
}

bool OldTextIndex(TbTextResourceManager& language) {
    if (language.mIndex == nullptr || language.mIndex->GetItem(kOldTextPostcardFont) != nullptr) return false;
    BaseLanguage* base = Base();
    if (base == nullptr) return false;
    char message[256];
    std::snprintf(message, sizeof(message), "the language's ResIdx.dat is the older build's: its fonts by %s's",
                  base->mName);
    SamsUtilities::UDebug(message);
    return true;
}

bool OldTextFont(TbTextResourceManager& language, TbIFont** out, uint32_t id) {
    BaseLanguage* base = Base();
    if (base == nullptr) return false;
    TbIResourceIndex* baseIndex = base->mManager.mIndex;
    auto* item = baseIndex->GetItem(id);
    if (item == nullptr || item->mType != 4) return false;
    const char* name = reinterpret_cast<const char*>(baseIndex->mData) + item->mFilenameOffset;
    TbIResourceIndex* index = language.mIndex;
    if (auto* own = FindOldTextResource(index->mFirst, index->mLast, index->mData, name, 4))
        return index->GetResource(own, reinterpret_cast<void**>(out)).Succeeded();
    return baseIndex->GetResource(item, reinterpret_cast<void**>(out)).Succeeded();
}

void OldTextDone() {
    delete gBase;
    gBase = nullptr;
    gBaseTried = false;
}
