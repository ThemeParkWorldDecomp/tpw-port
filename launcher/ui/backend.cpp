// backend.cpp: the launcher screen's importer interface over assets::Importer (backend.h).
#include "backend.h"

#include <utility>

#include "launcher_ui.h"
#include "updater.h"

namespace tpw::ui {

using assets::Edition;

ImporterBackend::ImporterBackend(std::string prefPath) : prefPath_(std::move(prefPath)) {
    while (prefPath_.size() > 1 && prefPath_.back() == '/') prefPath_.pop_back();
}

std::string ImporterBackend::IniPath() const {
    return prefPath_ + "/launcher.ini";
}

bool ImporterBackend::Open(std::string& err) {
    assets::ImporterConfig config;
    config.dataRoot = assets::ResolveDataRoot(prefPath_);
    config.iniPath = IniPath();
    auto importer = std::make_unique<assets::Importer>(config);
    if (!importer->Open(err)) return false;
    importer_ = std::move(importer);
    return true;
}

bool ImporterBackend::Start(const std::vector<std::string>& paths, std::string& err) {
    if (!importer_) {
        err = "the data folder is not open";
        return false;
    }
    return importer_->Start(paths, err);
}

bool ImporterBackend::Running() const {
    return importer_ && importer_->Running();
}

void ImporterBackend::Cancel() {
    if (importer_) importer_->Cancel();
}

std::vector<assets::ImportProgress> ImporterBackend::Drain() {
    if (!importer_) return {};
    return importer_->Progress().Drain();
}

assets::ImportProgress ImporterBackend::Latest() const {
    if (!importer_) return {};
    return importer_->Progress().Latest();
}

assets::ImportReport ImporterBackend::Wait() {
    if (!importer_) return {};
    return importer_->Wait();
}

assets::EditionStatus ImporterBackend::Status(Edition edition) const {
    if (!importer_) {
        assets::EditionStatus s;
        s.edition = edition;
        return s;
    }
    return importer_->Status(edition);
}

std::string ImporterBackend::DataFolder() const {
    return importer_ ? importer_->Config().dataRoot : assets::ResolveDataRoot(prefPath_);
}

bool ImporterBackend::ChangeDataFolder(const std::string& folder, std::string& err) {
    if (Running()) {
        err = "wait for the check to finish first";
        return false;
    }
    // The old importer goes first: it keeps its own copy of launcher.ini and would write it back.
    importer_.reset();
    assets::LauncherIni ini;
    if (!assets::LoadLauncherIni(IniPath(), ini, err)) return false;
    std::string old = ini.dataFolder;
    ini.dataFolder = folder;
    if (!assets::SaveLauncherIni(IniPath(), ini, err)) return false;
    if (Open(err)) return true;
    // The new folder cannot be used: go back to the old one.
    std::string ignored;
    ini.dataFolder = old;
    assets::SaveLauncherIni(IniPath(), ini, ignored);
    Open(ignored);
    return false;
}

std::vector<assets::LanguageStatus> ImporterBackend::Languages() const {
    return importer_ ? importer_->Languages() : std::vector<assets::LanguageStatus>();
}

long ImporterBackend::VerifyEdition(Edition edition, std::string& err) {
    if (!importer_) {
        err = "the data folder is not open";
        return -1;
    }
    return importer_->VerifyEdition(edition, err);
}

std::vector<assets::EditedFile> ImporterBackend::EditedFiles(Edition edition) const {
    return importer_ ? importer_->EditedFiles(edition) : std::vector<assets::EditedFile>();
}

bool ImporterBackend::RestoreOriginal(Edition edition, const std::string& dest, std::string& err) {
    if (!importer_) {
        err = "the data folder is not open";
        return false;
    }
    return importer_->RestoreOriginal(edition, dest, err);
}

bool ImporterBackend::RemoveEdition(Edition edition, std::string& err) {
    if (!importer_) {
        err = "the data folder is not open";
        return false;
    }
    return importer_->RemoveEdition(edition, err);
}

std::string ImporterBackend::IniExtra(const std::string& key) const {
    if (!importer_) return std::string();
    for (const auto& kv : importer_->Ini().extra)
        if (kv.first == key) return kv.second;
    return std::string();
}

bool ImporterBackend::SaveIniExtra(const std::string& prefPath, const std::string& key, const std::string& value,
                                   std::string& err) {
    const std::string path = prefPath + "/launcher.ini";
    assets::LauncherIni ini;
    if (!assets::LoadLauncherIni(path, ini, err)) return false;
    bool found = false;
    for (auto& kv : ini.extra)
        if (kv.first == key) {
            kv.second = value;
            found = true;
        }
    if (!found) ini.extra.emplace_back(key, value);
    return assets::SaveLauncherIni(path, ini, err);
}

std::string ImporterBackend::Setting(const std::string& key) const {
    const auto it = saved_.find(key);
    return it != saved_.end() ? it->second : IniExtra(key);
}

bool ImporterBackend::SaveSetting(const std::string& key, const std::string& value, std::string& err) {
    if (!SaveIniExtra(prefPath_, key, value, err)) return false;
    saved_[key] = value;
    return true;
}

std::string ImporterBackend::StartupNotice() const {
    return importer_ ? importer_->LastOpenCheck().Notice() : std::string();
}

std::string ImporterBackend::LastChoice() const {
    return importer_ ? importer_->Ini().lastChoice : std::string();
}

bool ImporterBackend::SetLastChoice(const std::string& choice, std::string& err) {
    if (!importer_) {
        err = "the data folder is not open";
        return false;
    }
    return importer_->SetLastChoice(choice, err);
}

void KeepSettings(const ImporterBackend& backend, const std::string& prefPath, const std::vector<std::string>& more) {
    std::vector<std::string> keys = {kSkipIntroKey, kBackToLauncherKey, kSmoothZoomKey, kInterfaceSizeKey, kPcSoundPicksKey,
                                     kSoundPanKey, kSeveralKeysKey, kWasdCameraKey, kFarZoomKey, kFullScreenKey, kRendererKey,
                                     kFrameRateKey,
                                     kCheckUpdatesKey, kLanguageUsKey, kLanguageEurKey, kOnlineServerKey,
                                     kUpdateLastCheckKey, kUpdateFoundKey};
    for (const SamOption& option : SamOptions()) keys.push_back(option.key);   // the Graphics section's keys
    keys.insert(keys.end(), more.begin(), more.end());
    for (const std::string& key : keys) {
        const std::string value = backend.Setting(key);
        std::string ignored;
        if (!value.empty()) ImporterBackend::SaveIniExtra(prefPath, key, value, ignored);
    }
}

}  // namespace tpw::ui
