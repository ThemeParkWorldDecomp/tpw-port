// backend.h: what the launcher screen needs from the importer, as an interface, so the screenshot test can drive every
// screen from a scripted feed with no disc. ImporterBackend is the real one over assets::Importer (assets/importer.h).
// Threading is the importer's: Start() runs a drop on its worker thread and the screen drains progress every frame;
// VerifyEdition() rehashes a whole edition, so the screen calls it from a thread of its own.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "importer.h"

namespace tpw::ui {

class ImportBackend {
public:
    virtual ~ImportBackend() = default;
    virtual bool Start(const std::vector<std::string>& paths, std::string& err) = 0;
    virtual bool Running() const = 0;
    virtual void Cancel() = 0;
    virtual std::vector<assets::ImportProgress> Drain() = 0;
    virtual assets::ImportProgress Latest() const = 0;
    virtual assets::ImportReport Wait() = 0;
    // An edition's state. The real backend counts the US Mac program as one more required file of the European
    // edition, from the US Mac CD, so its tile says that CD is still needed until the program is in place.
    virtual assets::EditionStatus Status(assets::Edition edition) const = 0;
    // Where the verified files are kept.
    virtual std::string DataFolder() const = 0;
    // Uses `folder` from now on (saved in launcher.ini). Files in the old folder stay there.
    virtual bool ChangeDataFolder(const std::string& folder, std::string& err) = 0;
    virtual long VerifyEdition(assets::Edition edition, std::string& err) = 0;
    virtual bool RemoveEdition(assets::Edition edition, std::string& err) = 0;
    virtual std::string LastChoice() const = 0;
    virtual bool SetLastChoice(const std::string& choice, std::string& err) = 0;
    // What the startup check removed, as the home screen's notice ("3 files of the European edition had changed ..."),
    // or empty (assets/importer.h OpenCheck).
    virtual std::string StartupNotice() const { return ""; }
    // One of the launcher's own settings in launcher.ini (kSkipIntroKey): its value, empty when it has none; and keeping
    // a new value there.
    virtual std::string Setting(const std::string& key) const { return std::string(); }
    virtual bool SaveSetting(const std::string& key, const std::string& value, std::string& err) { return true; }
};

class ImporterBackend : public ImportBackend {
public:
    // `prefPath`: the folder launcher.ini lives in (the SDL pref path); the data folder is what it names, or itself.
    explicit ImporterBackend(std::string prefPath);
    // Opens the importer (assets/importer.h Open()); call once before anything else.
    bool Open(std::string& err);
    assets::Importer* importer() { return importer_.get(); }
    // The US Mac program in the data folder (assets/us/Theme Park Data/SimTheme Park): both editions read the game's
    // tables from it (platform/game_tables.cpp), the European one included, whose PC CD has no Mac program. HasProgram:
    // a file of an accepted size is there (what the importer counts as present). Empty or false before Open.
    std::string ProgramPath() const;
    bool HasProgram() const;
    // The folder launcher.ini is in.
    const std::string& PrefPath() const { return prefPath_; }
    // One of launcher.ini's keys kept for later units (LauncherIni::extra): its value, empty when there is none.
    std::string IniExtra(const std::string& key) const;
    // Sets one such key (replacing its old value) in the launcher.ini of `prefPath`, straight in the file: for after the
    // backend is gone (an open importer keeps its own copy of the file and would write it back over this).
    static bool SaveIniExtra(const std::string& prefPath, const std::string& key, const std::string& value,
                             std::string& err);

    bool Start(const std::vector<std::string>& paths, std::string& err) override;
    bool Running() const override;
    void Cancel() override;
    std::vector<assets::ImportProgress> Drain() override;
    assets::ImportProgress Latest() const override;
    assets::ImportReport Wait() override;
    assets::EditionStatus Status(assets::Edition edition) const override;
    std::string DataFolder() const override;
    bool ChangeDataFolder(const std::string& folder, std::string& err) override;
    long VerifyEdition(assets::Edition edition, std::string& err) override;
    bool RemoveEdition(assets::Edition edition, std::string& err) override;
    std::string LastChoice() const override;
    bool SetLastChoice(const std::string& choice, std::string& err) override;
    std::string StartupNotice() const override;
    // Kept straight in the file (SaveIniExtra), and remembered here: the open importer's copy of the file does not see it.
    std::string Setting(const std::string& key) const override;
    bool SaveSetting(const std::string& key, const std::string& value, std::string& err) override;

private:
    std::string IniPath() const;
    assets::EditionStatus WithProgram(assets::EditionStatus s) const;
    std::string prefPath_;
    std::unique_ptr<assets::Importer> importer_;
    std::map<std::string, std::string> saved_;   // the settings SaveSetting wrote
};

}  // namespace tpw::ui
