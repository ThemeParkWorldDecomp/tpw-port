#include <jni.h>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

#include "importer.h"
#include "content_overlay.h"
#include "mods.h"
#include "tpi_content.h"
#include "ui/launcher_ui.h"

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
                             (mod.content == "tpi" && !FindTpiWorlds(content).empty());
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

namespace {
jobjectArray StringArray(JNIEnv* env, const std::vector<std::string>& strings) {
    jclass type = env->FindClass("java/lang/String");
    if (type == nullptr) return nullptr;
    jobjectArray array = env->NewObjectArray(static_cast<jsize>(strings.size()), type, nullptr);
    if (array == nullptr) return nullptr;
    for (size_t i = 0; i < strings.size(); ++i) {
        jstring value = env->NewStringUTF(strings[i].c_str());
        if (value == nullptr) return nullptr;
        env->SetObjectArrayElement(array, static_cast<jsize>(i), value);
        env->DeleteLocalRef(value);
    }
    return array;
}
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_org_tpwport_android_SetupActivity_nativeImport(
    JNIEnv* env, jclass, jstring dataFolder, jobjectArray inputs) {
    using namespace tpw::assets;
    try {
        ImporterConfig config;
        config.dataRoot = Path(env, dataFolder);
        if (env->ExceptionCheck()) return nullptr;
        Importer importer(config);
        std::string error;
        if (!importer.Open(error)) return StringArray(env, {"open_error", "", "", "0", "", "", "", "", ""});

        std::vector<std::string> paths;
        for (jsize i = 0; i < env->GetArrayLength(inputs); ++i) {
            auto input = static_cast<jstring>(env->GetObjectArrayElement(inputs, i));
            paths.push_back(Path(env, input));
            env->DeleteLocalRef(input);
            if (env->ExceptionCheck()) return nullptr;
        }
        const ImportReport report = paths.empty() ? ImportReport{} : importer.Import(paths);

        std::string editions;
        for (const Edition edition : {kEditionUs, kEditionUsPc, kEditionEur, kEditionEurPc}) {
            const EditionStatus status = importer.Status(edition);
            if (status.unlocked) {
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
        const bool recognized = report.matched != 0 || report.playStationConverted || report.themeParkIncConverted;
        const char* result = report.aborted || !report.errors.empty() ? "import_error" :
                             !paths.empty() && !recognized ? "unrecognized" : "ok";
        return StringArray(env, {result, editions, europeanNames, std::to_string(report.written),
                                 americanNames, europeanLabels, americanLabels,
                                 gold(tpw::ui::kGoldUsKey), gold(tpw::ui::kGoldEurKey)});
    } catch (...) {
        return StringArray(env, {"import_error", "", "", "0", "", "", "", "", ""});
    }
}
