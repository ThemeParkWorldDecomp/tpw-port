// heldpark.cpp: the port's held park (no Mac code; the port's fix heldpark, include/SaveLoad.h). A park that needs rides the
// game has not got (the Gold rides or a mod's content rides switched off) is not loaded, on the Mac as here; this tells the
// player which rides, in the game's own error dialog (AddUIError, src/game/ui/ui_ErrorDisplay.cpp), and holds the park so
// that GAMESYSTEM_Tick leaves it before its first frame, saves nothing and goes back to the menu, where the message opens.

#include "FS_Online.h"
#include "platform/missing_rides.h"
#include "SaveLoad.h"
#include "TbITable.h"
#include "TbResource.h"
#include "Text.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// `text` as the game's text table of one string, as tpw::online::boss::UnreachableTable makes its (platform/online_library.cpp):
// the record is the word 1 | length << 8 and the text, NUL padded to four bytes, then a zero word; the text in the language's
// code page (text resource 1), which the error dialog turns back into Unicode. The dialog reads the table when it shows the
// message, so it lives on; a later message replaces it.
TbITable* MessageTable(const std::string& text) {
    static std::vector<unsigned char> record;
    static const unsigned char* strings[1];
    static TbITable* table = new TbITable();
    const std::u16string wide(text.begin(), text.end());
    std::vector<unsigned char> bytes(text.size() * 2 + 2, 0);
    if (auto* converter = static_cast<TbIUnicodeToMB*>(gText.mResourcePointers[1]))
        converter->ConvertString(bytes.data(), wide.c_str(), static_cast<uint16_t>(bytes.size()));
    else
        std::memcpy(bytes.data(), text.data(), text.size());
    const uint32_t length = static_cast<uint32_t>(std::strlen(reinterpret_cast<const char*>(bytes.data())) + 1);
    record.assign(4 + ((length + 3) & ~3u) + 4, 0);
    const uint32_t word = 1 | (length << 8);
    std::memcpy(record.data(), &word, 4);
    std::memcpy(record.data() + 4, bytes.data(), length);
    strings[0] = record.data();
    table->mCount = 1;
    table->mStrings = strings;
    return table;
}

}  // namespace

// TPW_PORT_FIX heldpark PortTellMissingRides | Mac: (none): nothing says a park needs rides the game has not got | Port: the game's error dialog names them (MissingRidesText) and, for a park the player is put in, holds it until GAMESYSTEM_Tick has left it | Why: the player knows why the park did not load and what to turn on
void PortTellMissingRides(int32_t place, bool hold) {
    const std::string text = tpw::MissingRidesText(gPortMissingRides, static_cast<tpw::MissingRidesPlace>(place));
    std::fprintf(stderr, "port: %s%s\n", hold ? "park held: " : "park not loaded: ", text.c_str());
    if (hold) gPortHeldPark = 1;
    AddUIError(0, MessageTable(text));
}

