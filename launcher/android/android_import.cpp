#include <jni.h>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

#include "asset_packs.h"
#include "importer.h"
#include "content_overlay.h"
#include "mods.h"
#include "log_export.h"
#include "net/net.h"
#include "ps2_convert.h"
#include "tpi_content.h"
#include "ui/launcher_ui.h"
#include "ui/unofficial_language.h"
#include "ui/texts.h"
#include "ui/updater.h"

namespace {
jobjectArray StringArray(JNIEnv* env, const std::vector<std::string>& strings);

auto FindOption(tpw::assets::LauncherIni& ini, const std::string& key) {
    return std::find_if(ini.extra.begin(), ini.extra.end(), [&](const auto& pair) { return pair.first == key; });
}

std::string Path(JNIEnv* env, jstring string) {
    const jchar* chars = env->GetStringChars(string, nullptr);
    if (chars == nullptr) return {};
    std::u16string path;
    const jsize length = env->GetStringLength(string);
    for (jsize i = 0; i < length; ++i) path.push_back(static_cast<char16_t>(chars[i]));
    env->ReleaseStringChars(string, chars);
    return std::filesystem::path(path).u8string();
}
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_org_tpwport_android_SetupActivity_nativeOptions(
    JNIEnv* env, jclass, jstring dataFolder, jboolean mods, jstring changedKey, jboolean enabled) {
    using namespace tpw;
    try {
        const auto root = std::filesystem::u8path(Path(env, dataFolder));
        const std::string key = Path(env, changedKey);
        if (env->ExceptionCheck()) return nullptr;
        assets::LauncherIni ini;
        std::string error;
        const auto iniPath = (root / "launcher.ini").u8string();
        if (!assets::LoadLauncherIni(iniPath, ini, error)) return nullptr;
        std::vector<std::string> rows;
        bool changed = false;
        auto row = [&](const std::string& id, const std::string& title, bool defaultOn, bool available, const std::string& note) {
            auto found = FindOption(ini, id);
            bool on = found == ini.extra.end() ? defaultOn : found->second == "1";
            if (id == key && available) {
                on = enabled;
                if (found == ini.extra.end()) ini.extra.emplace_back(id, on ? "1" : "0");
                else found->second = on ? "1" : "0";
                changed = true;
            }
            rows.insert(rows.end(), {id, title, on ? "1" : "0", available ? "1" : "0", note});
        };
        if (mods) {
            for (const tpw::mods::Mod& mod : tpw::mods::Catalog()) {
                const auto content = root / "assets" / mod.content;
                bool ready = mod.content.empty() || (mod.content == "ps1" && !FindContentRides(content).empty()) ||
                             (mod.content == "tpi" && !FindTpiWorlds(content).empty()) ||
                             (mod.content == "ps2" && assets::ReadPs2ConverterStamp(root) == assets::kPs2ConverterVersion);
                const std::string details = ready ? mod.text : mod.content;
                row(tpw::mods::Key(mod), mod.name, false, ready, details);
            }
        } else if (key == ui::kGoldUsKey || key == ui::kGoldEurKey) {
            row(key, "", true, true, "");
        } else {
            row(ui::kSmoothZoomKey, "", false, true, "");
            row(ui::kFarZoomKey, "", false, true, "");
            row(ui::kPcSoundPicksKey, "", true, true, "");
            row(ui::kSoundPanKey, "", false, true, "");
            row(ui::kRideBadgesKey, "", true, true, "");
#ifdef TPW_ANDROID_MOVIES
            row(ui::kSkipIntroKey, "", false, true, "");
#endif
        }
        if (!key.empty() && (!changed || !assets::SaveLauncherIni(iniPath, ini, error))) return nullptr;
        return StringArray(env, rows);
    } catch (...) {
        return nullptr;
    }
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_org_tpwport_android_SetupActivity_nativeGraphicsOptions(
    JNIEnv* env, jclass, jstring dataFolder, jstring changedKey, jint selection) {
    using namespace tpw;
    try {
        const auto root = std::filesystem::u8path(Path(env, dataFolder));
        const std::string key = Path(env, changedKey);
        if (env->ExceptionCheck()) return nullptr;
        assets::LauncherIni ini;
        std::string error;
        const auto iniPath = (root / "launcher.ini").u8string();
        if (!assets::LoadLauncherIni(iniPath, ini, error)) return nullptr;

        auto value = [&](const std::string& id) {
            const auto found = FindOption(ini, id);
            return found == ini.extra.end() ? std::string() : found->second;
        };
        std::vector<std::string> rows;
        bool changed = false;
        auto row = [&](const std::string& id, const std::vector<std::string>& labels,
                       const std::vector<std::string>& values, std::string current) {
            if (id == key && selection >= 0 && static_cast<size_t>(selection) < values.size()) {
                const auto found = FindOption(ini, id);
                if (found == ini.extra.end()) ini.extra.emplace_back(id, values[selection]);
                else found->second = values[selection];
                current = labels[selection];
                changed = true;
            }
            std::string choices;
            for (const auto& label : labels) {
                if (!choices.empty()) choices += '\n';
                choices += label;
            }
            // Each row has a setting ID, its current label and a newline-delimited list of choices.
            rows.insert(rows.end(), {id, current, choices});
        };
        const int fps = ui::FrameRateSetting(value(ui::kFrameRateKey));
        row(ui::kFrameRateKey, {"Auto", "30 FPS", "60 FPS"}, {"0", "30", "60"},
            fps == 0 ? "Auto" : std::to_string(fps) + " FPS");

        const std::vector<std::string> distanceLabels{"Original", "Farther", "Whole world"};
        const int distance = ui::DrawDistanceSetting(value(ui::kDrawDistanceKey));
        row(ui::kDrawDistanceKey, distanceLabels,
            {std::begin(ui::kDrawDistanceValues), std::end(ui::kDrawDistanceValues)}, distanceLabels[distance]);
        for (const auto& option : ui::SamOptions()) {
            std::vector<std::string> labels{"Default"}, values{"default"};
            labels.insert(labels.end(), option.shown.begin(), option.shown.end());
            for (const int entry : option.values) values.push_back(std::to_string(entry));
            row(option.key, labels, values, labels[ui::SamChoice(option, value(option.key)) + 1]);
        }

        if (!key.empty() && (!changed || !assets::SaveLauncherIni(iniPath, ini, error))) return nullptr;
        return StringArray(env, rows);
    } catch (...) {
        return nullptr;
    }
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_org_tpwport_android_SetupActivity_nativeOnlineServer(
    JNIEnv* env, jclass, jstring dataFolder, jstring host) {
    using namespace tpw;
    try {
        const auto root = std::filesystem::u8path(Path(env, dataFolder));
        const std::string wanted = host == nullptr ? std::string() : Path(env, host);
        if (env->ExceptionCheck()) return nullptr;
        assets::LauncherIni ini;
        std::string error;
        const auto iniPath = (root / "launcher.ini").u8string();
        if (!assets::LoadLauncherIni(iniPath, ini, error)) return nullptr;

        const auto found = FindOption(ini, ui::kOnlineServerFoundKey);
        const auto automatic = ui::ChooseOnlineServer("", "", found == ini.extra.end() ? "" : found->second);
        const auto setting = FindOption(ini, ui::kOnlineServerKey);
        std::string saved = setting == ini.extra.end() ? "" : setting->second;
        if (host != nullptr && !wanted.empty() && !net::ValidHost(wanted)) {
            return StringArray(env, {"invalid", saved, automatic.host});
        }
        if (host != nullptr && wanted != saved) {
            if (setting != ini.extra.end()) setting->second = wanted;
            else ini.extra.emplace_back(ui::kOnlineServerKey, wanted);
            if (!assets::SaveLauncherIni(iniPath, ini, error)) return nullptr;
            saved = wanted;
        }
        return StringArray(env, {"ok", saved, automatic.host});
    } catch (...) {
        return nullptr;
    }
}

namespace {
jobjectArray StringArray(JNIEnv* env, const std::vector<std::string>& strings) {
    jclass type = env->FindClass("java/lang/String");
    if (type == nullptr) return nullptr;
    jobjectArray array = env->NewObjectArray(static_cast<jsize>(strings.size()), type, nullptr);
    if (array == nullptr) return nullptr;
    for (size_t i = 0; i < strings.size(); ++i) {
        const std::u16string utf16 = std::filesystem::u8path(strings[i]).u16string();
        jstring value = env->NewString(reinterpret_cast<const jchar*>(utf16.data()), static_cast<jsize>(utf16.size()));
        if (value == nullptr) return nullptr;
        env->SetObjectArrayElement(array, static_cast<jsize>(i), value);
        env->DeleteLocalRef(value);
    }
    return array;
}
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_org_tpwport_android_SetupActivity_nativeImport(
    JNIEnv* env, jclass, jstring dataFolder, jobjectArray inputs, jint operation, jstring editionCode) {
    using namespace tpw::assets;
    try {
        ImporterConfig config;
        config.dataRoot = Path(env, dataFolder);
        const std::string code = Path(env, editionCode);
        if (env->ExceptionCheck()) return nullptr;
#ifdef TPW_PORT_FIX
        ConfigureUnofficialLanguages(config);
#endif
        Importer importer(config);
        std::string error;
        if (!importer.Open(error)) return nullptr;

        long affected = 0;
        if (operation != 0) {
            Edition edition;
            if (!EditionFromName(code.c_str(), edition) || edition == kEditionLanguages) return nullptr;
            if (operation == 1) affected = importer.VerifyEdition(edition, error);
            else if (operation == 2) affected = importer.RemoveEdition(edition, error) ? 0 : -1;
            else return nullptr;
        }

        std::vector<std::string> paths;
        for (jsize i = 0; i < env->GetArrayLength(inputs); ++i) {
            auto input = static_cast<jstring>(env->GetObjectArrayElement(inputs, i));
            paths.push_back(Path(env, input));
            env->DeleteLocalRef(input);
            if (env->ExceptionCheck()) return nullptr;
        }
        const ImportReport report = paths.empty() ? ImportReport{} : importer.Import(paths);

        std::string editions;
        std::string installed;
        for (const Edition edition : {kEditionUs, kEditionUsPc, kEditionEur, kEditionEurPc, kEditionGold}) {
            const EditionStatus status = importer.Status(edition);
            if (status.present > 0) {
                if (!installed.empty()) installed += '\n';
                installed += std::string(EditionName(edition)) + ":" + std::to_string(status.requiredPresent) + ":" +
                             std::to_string(status.required);
            }
            if (status.unlocked && edition != kEditionGold) {
                if (!editions.empty()) editions += ',';
                editions += EditionName(edition);
            }
        }
        const auto languages = importer.Languages();
        auto languageRows = [&](Edition edition, std::string& names, std::string& labels) {
            for (const auto& choice : tpw::ui::LanguageChoices(edition, languages)) {
                if (!names.empty()) {
                    names += '\n';
                    labels += '\n';
                }
                names += choice.name;
                labels += choice.shown;
            }
        };
        std::string europeanNames, europeanLabels, americanNames, americanLabels;
        languageRows(kEditionEur, europeanNames, europeanLabels);
        languageRows(kEditionUs, americanNames, americanLabels);
        const auto ini = importer.Ini();
        const bool goldReady = importer.Status(kEditionGold).unlocked;
        auto gold = [&](const char* key) {
            if (!goldReady) return "";
            for (const auto& [name, value] : ini.extra) {
                if (name == key && value == "0") return "0";
            }
            return "1";
        };
        std::string unnamed;
        if (!report.translations.empty()) {
            for (const auto& item : languages) {
                if (item.state == LanguageStatus::State::Available && item.unofficial &&
                    item.description == tpw::ui::kUnofficialUnknown) {
                    if (!unnamed.empty()) unnamed += '\n';
                    unnamed += item.name;
                }
            }
        }
        const bool recognized = report.matched != 0 || report.playStationConverted || report.themeParkIncConverted ||
                                report.playStation2Converted ||
                                !report.translations.empty() || report.themeParkIncAddOnAdded || report.themeParkIncAddOnAlready;
        const char* result = affected < 0 || report.aborted || !report.errors.empty() ? "import_error" :
                             report.playStation2Converted ? "ps2_ready" :
                             !report.playStation2.empty() ? "ps2_import_error" :
                             !paths.empty() && !recognized ? "unrecognized" : "ok";
        return StringArray(env, {result, editions, europeanNames, std::to_string(operation == 0 ? report.written : affected),
                                 americanNames, europeanLabels, americanLabels,
                                 gold(tpw::ui::kGoldUsKey), gold(tpw::ui::kGoldEurKey), installed, unnamed});
    } catch (...) {
        return StringArray(env, {"import_error", "", "", "0", "", "", "", "", ""});
    }
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_org_tpwport_android_SetupActivity_nativeTranslationLanguages(
    JNIEnv* env, jclass) {
    try {
        std::vector<std::string> names;
#ifdef TPW_PORT_FIX
        for (const auto& language : tpw::assets::TranslationLanguages()) names.emplace_back(language.name);
#endif
        return StringArray(env, names);
    } catch (...) {
        return nullptr;
    }
}

extern "C" JNIEXPORT jboolean JNICALL Java_org_tpwport_android_SetupActivity_nativeNameTranslation(
    JNIEnv* env, jclass, jstring dataFolder, jstring id, jstring language) {
    try {
#ifdef TPW_PORT_FIX
        const std::string assets = Path(env, dataFolder) + "/assets";
        const std::string oldId = Path(env, id), name = Path(env, language);
        std::string newId, error;
        return !env->ExceptionCheck() && tpw::assets::NameUnofficialLanguage(assets, oldId, name, newId, error);
#else
        return false;
#endif
    } catch (...) {
        return false;
    }
}

extern "C" JNIEXPORT jstring JNICALL Java_org_tpwport_android_AndroidFiles_nativeExportLog(
    JNIEnv* env, jclass, jstring dataFolder, jstring tempFolder) {
    try {
        const auto root = std::filesystem::u8path(Path(env, dataFolder));
        const auto temp = std::filesystem::u8path(Path(env, tempFolder));
        if (env->ExceptionCheck()) return nullptr;
        const tpw::ExportProgram program{TPW_LAUNCHER_VERSION,
            std::string(tpw::ui::kProductNameWithVersion) + " (build: " + TPW_LAUNCHER_BUILD + ")"};
        const auto exported = tpw::ExportLogs(root / "logs" / "launcher.log", temp, std::time(nullptr), program);
        return exported.ok ? env->NewStringUTF(exported.file.filename().u8string().c_str()) : nullptr;
    } catch (...) {
        return nullptr;
    }
}

extern "C" JNIEXPORT jboolean JNICALL Java_org_tpwport_android_AndroidFiles_nativeValidateAssetPack(
    JNIEnv* env, jclass, jstring archive, jstring tempFolder) {
    try {
        const auto zip = std::filesystem::u8path(Path(env, archive));
        const auto temp = std::filesystem::u8path(Path(env, tempFolder));
        std::string error;
        return !env->ExceptionCheck() && tpw::launcher::ValidateAssetPack(zip, temp, error);
    } catch (...) {
        return false;
    }
}
