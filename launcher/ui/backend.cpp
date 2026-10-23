// backend.cpp: the launcher screen's importer interface over assets::Importer (backend.h).
#include "backend.h"

#include <filesystem>
#include <system_error>
#include <utility>

namespace tpw::ui {

using assets::Edition;

namespace {
// The manifest's US Mac program record (the program group has one).
const assets::ManifestRecord* ProgramRecord() {
    const assets::ManifestRecord* records = assets::ManifestRecords();
    for (size_t i = 0; i < assets::ManifestRecordCount(); ++i)
        if (records[i].group == assets::kGroupProgram && (records[i].editions & assets::kEditionUs)) return &records[i];
    return nullptr;
}
}  // namespace

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
    assets::ImportReport report = importer_->Wait();
    report.status[1] = WithProgram(report.status[1]);
    return report;
}

std::string ImporterBackend::ProgramPath() const {
    const assets::ManifestRecord* record = ProgramRecord();
    if (!importer_ || !record) return {};
    return importer_->DestinationPath(assets::kEditionUs, *record);
}

bool ImporterBackend::HasProgram() const {
    const assets::ManifestRecord* record = ProgramRecord();
    std::string path = ProgramPath();
    if (!record || path.empty()) return false;
    std::error_code ec;
    uint64_t size = std::filesystem::file_size(path, ec);
    if (ec) return false;
    for (uint8_t k = 0; k < record->hash_count; ++k)
        if (assets::ManifestHashes()[record->first_hash + k].size == size) return true;
    return false;
}

assets::EditionStatus ImporterBackend::WithProgram(assets::EditionStatus s) const {
    if (s.edition != assets::kEditionEur || s.required == 0 || HasProgram()) return s;
    // The European edition's tables come from the US Mac program, which the PC CD does not have: one more file to
    // find, on the US Mac CD.
    ++s.records;
    ++s.required;
    ++s.files;
    ++s.missing[assets::kSourceMacCd];
    ++s.missingFiles[assets::kSourceMacCd];
    s.unlocked = false;
    return s;
}

assets::EditionStatus ImporterBackend::Status(Edition edition) const {
    if (!importer_) {
        assets::EditionStatus s;
        s.edition = edition;
        return s;
    }
    return WithProgram(importer_->Status(edition));
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

long ImporterBackend::VerifyEdition(Edition edition, std::string& err) {
    if (!importer_) {
        err = "the data folder is not open";
        return -1;
    }
    return importer_->VerifyEdition(edition, err);
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

}  // namespace tpw::ui
