// language_pack.h: the chosen language, laid over the game folder. The original's installer copied one language's
// files over the base game (its text and fonts into data/Language/<Name>, its speech into data/global/speech and each
// level's speech folder, its splash and welcome pictures into data/Init/<resolution>), and the game takes the first
// folder data:Language: lists as its language (FS_ReadLanguageRegistry, src/game/save/FS_Registry.cpp). The launcher
// keeps each language apart, its files at their game paths in a folder lang/<Name>/ of the language root (the data
// folder's assets/), and this overlay shows them to the game as if the installer had copied them: a lookup (files.cpp's
// FindHostPath) finds a file there after the user folder and before the asset packs, the Gold rides and the game folder,
// and a listing (ListGameFolder) lists the folder there in the same place, so data:Language: lists <Name> first and the
// language's speech files take the edition's own ones' places. Only those folders are overlaid: data/Language/<Name>/,
// data/global/speech/, data/levels/<world>/speech/ and data/Init/, each with everything inside it. With the overlay off
// (no language chosen) the game sees the game folder alone, as before.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace tpw {

// `name` as the original spells it, one of its 16 language names (FS_Registry.cpp's, English to American), matched
// ignoring case; empty if it is none of them.
std::string LanguageName(const std::string& name);

// Lay the language `name` (any case) over the game folder from `root`/lang/<Name>; an empty `name` takes the overlay
// away. False, with the overlay away, when `name` is not one of the 16 or the folder is not there.
bool UseLanguagePack(const std::filesystem::path& root, const std::string& name);

// The overlaid language as the original spells it (empty when off), and its folder (lang/<Name> in the root).
const std::string& LanguagePackName();
const std::filesystem::path& LanguagePackFolder();

// Whether the game path split into `parts` (SplitGamePath) is one of the overlaid folders or inside one, matched ignoring
// case: data/Language/<Name> and below, data/global/speech, data/levels/<world>/speech and data/Init. False when off.
bool LanguagePackCovers(const std::vector<std::string>& parts);

// Whether a listing of the game folder `parts` shows the overlay's entry `name`: any entry of a folder it covers, and in
// data/Language only <Name> itself. False when off.
bool LanguagePackLists(const std::vector<std::string>& parts, const std::string& name);

}  // namespace tpw
