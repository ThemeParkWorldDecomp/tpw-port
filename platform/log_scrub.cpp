// log_scrub.cpp: what identifies this computer and its player, gathered for log_scrub.h.
#include "log_scrub.h"

#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <pwd.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace tpw {

namespace {

#ifdef _WIN32
// A UTF-16 text as UTF-8, or in the ANSI code page (what narrow path strings are on Windows: an accented name is then not
// the UTF-8 one).
std::string Narrow(const wchar_t* wide, UINT codePage) {
    if (wide == nullptr || wide[0] == L'\0') return "";
    const int size = WideCharToMultiByte(codePage, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return "";
    std::string text(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(codePage, 0, wide, -1, text.data(), size, nullptr, nullptr);
    text.resize(static_cast<size_t>(size - 1));
    return text;
}

std::wstring EnvWide(const wchar_t* name) {
    const wchar_t* value = _wgetenv(name);
    return value != nullptr ? value : L"";
}

void AddBoth(std::vector<std::string>& into, const std::wstring& wide) {
    for (UINT page : {static_cast<UINT>(CP_UTF8), static_cast<UINT>(CP_ACP)}) {
        const std::string text = Narrow(wide.c_str(), page);
        if (!text.empty()) into.push_back(text);
    }
}

bool Under(const std::string& path, const std::vector<std::string>& homes) {
    for (const std::string& home : homes) {
        if (path.size() < home.size()) continue;
        bool same = true;
        for (size_t k = 0; k < home.size() && same; ++k) {
            const unsigned char a = static_cast<unsigned char>(path[k]), b = static_cast<unsigned char>(home[k]);
            same = a == b || (scrub_detail::IsSep(a) && scrub_detail::IsSep(b)) || scrub_detail::Fold(a) == scrub_detail::Fold(b);
        }
        if (same && (path.size() == home.size() || scrub_detail::IsSep(static_cast<unsigned char>(path[home.size()])))) return true;
    }
    return false;
}
#endif

}  // namespace

ScrubInputs CollectScrubInputs() {
    ScrubInputs in;
#ifdef _WIN32
    in.windows = true;
    AddBoth(in.homes, EnvWide(L"USERPROFILE"));
    AddBoth(in.homes, EnvWide(L"HOMEDRIVE") + EnvWide(L"HOMEPATH"));
    // A folder moved out of the home (another drive, OneDrive's own folder) is named by its variable.
    const struct {
        const wchar_t* variable;
        const char* replacement;
    } kMoved[] = {{L"APPDATA", "%APPDATA%"},
                  {L"LOCALAPPDATA", "%LOCALAPPDATA%"},
                  {L"TEMP", "%TEMP%"},
                  {L"TMP", "%TEMP%"},
                  {L"OneDrive", "%OneDrive%"},
                  {L"OneDriveConsumer", "%OneDrive%"},
                  {L"OneDriveCommercial", "%OneDrive%"}};
    for (const auto& moved : kMoved) {
        std::vector<std::string> spellings;
        AddBoth(spellings, EnvWide(moved.variable));
        for (const std::string& spelling : spellings)
            if (!Under(spelling, in.homes)) in.roots.push_back({spelling, moved.replacement});
    }
    AddBoth(in.users, EnvWide(L"USERNAME"));
    wchar_t name[256];
    DWORD size = 256;
    if (GetUserNameW(name, &size)) AddBoth(in.users, name);
    AddBoth(in.hosts, EnvWide(L"COMPUTERNAME"));
    for (COMPUTER_NAME_FORMAT format : {ComputerNameNetBIOS, ComputerNameDnsHostname, ComputerNameDnsFullyQualified}) {
        wchar_t host[256];
        size = 256;
        if (GetComputerNameExW(format, host, &size)) AddBoth(in.hosts, host);
    }
#else
    const auto env = [](const char* variable) {
        const char* value = std::getenv(variable);
        return std::string(value != nullptr ? value : "");
    };
    in.homes.push_back(env("HOME"));
    std::vector<char> buffer(4096);
    passwd entry{};
    passwd* found = nullptr;
    if (getpwuid_r(getuid(), &entry, buffer.data(), buffer.size(), &found) == 0 && found != nullptr) {
        if (found->pw_dir != nullptr) in.homes.push_back(found->pw_dir);
        if (found->pw_name != nullptr) in.users.push_back(found->pw_name);
    }
    for (const char* variable : {"USER", "LOGNAME", "USERNAME"}) in.users.push_back(env(variable));
    // The home as the file system names it (a link, a private/ prefix).
    for (size_t k = in.homes.size(); k-- > 0;) {
        std::error_code ec;
        const fs::path real = fs::canonical(in.homes[k], ec);
        if (!ec) in.homes.push_back(real.string());
    }
    char host[256] = {0};
    if (gethostname(host, sizeof host - 1) == 0) in.hosts.push_back(host);
    in.hosts.push_back(env("HOSTNAME"));
#if defined(__APPLE__)
    // The mounted volumes' names: a name the player gave a drive is not for a log.
    if (DIR* volumes = opendir("/Volumes")) {
        while (const dirent* volume = readdir(volumes)) {
            const std::string name = volume->d_name;
            if (name != "." && name != "..") in.roots.push_back({"/Volumes/" + name, "/Volumes/<volume>"});
        }
        closedir(volumes);
    }
#endif
#endif
    return in;
}

void EnsureScrubIdentity() {
    static std::once_flag once;
    std::call_once(once, [] {
        if (gScrubIdentity.load() != nullptr) return;   // a test's own
        gScrubIdentity.store(new ScrubIdentity(MakeScrubIdentity(CollectScrubInputs())));
    });
}

}  // namespace tpw
