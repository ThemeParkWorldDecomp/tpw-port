// online_boss_e2e.cpp: the online server client (platform/online_boss.cpp) against a running online server on this computer
// (decomp-onlinesrv's tpw-online), over real sockets: the server's details and news, a login, the city list, a city's parks and
// top 10, the server list's upload host, a park published, then listed in its city, then downloaded from the server's FTP service
// (platform/online_ftp.cpp) and compared with the file sent. Skips (77) unless TPW_ONLINE_SERVER names the server;
// TPW_ONLINE_NAME and TPW_ONLINE_PASSWORD are an account on it, TPW_ONLINE_PARK a park file it accepts (tpw-online
// make-test-park), TPW_ONLINE_RIDES the park's ride numbers ("1,2,3", hex as the request sends them), TPW_ONLINE_FTP_PORT the
// FTP service's port (21; a local server's listen.ftp). Never run against a host that is not the tester's own.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "FS_Online.h"
#include "FS_OnlineList.h"
#include "platform.h"
#include "platform/arena.h"
#include "platform/online_boss.h"
#include "platform/online_ftp.h"

namespace fs = std::filesystem;
namespace boss = tpw::online::boss;

namespace {

int gFailures = 0;
int gChecks = 0;

void Check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) ++gFailures;
    std::printf("%s: %s\n", ok ? "ok" : "FAILED", what.c_str());
}

const char* Env(const char* name, const char* fallback = nullptr) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != 0 ? value : fallback;
}

struct Got {
    int32_t code = 0;
    int32_t request = 0;
    std::vector<uint8_t> block;    // the account block, or a news or grant text
    std::vector<uint8_t> infos;    // a list's infos
    int32_t count = 0;
};

std::vector<Got> gGot;
std::vector<ccReceivedList<uint8_t>*> gHeld;   // the lists given, let go of at the end (their strings live until then)
size_t gStride = 0;   // the stride of the list the next answer gives

uint32_t Word(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

std::string ArenaText(const uint8_t* word) {
    const uint32_t offset = Word(word);
    std::string out;
    if (offset == 0) return out;
    for (const auto* c = reinterpret_cast<const char16_t*>(tpw::ArenaBase() + offset); *c != 0; ++c)
        out += *c < 0x80 ? static_cast<char>(*c) : '?';
    return out;
}

void Record(int32_t code, int32_t request, void*, void* block) {
    Got g{code, request, {}, {}, 0};
    if (code == 0x3c && block != nullptr) {
        const auto* p = static_cast<const uint8_t*>(block);
        g.block.assign(p, p + 0x6c);
    } else if ((code == 0x33 || code == 0x32 || code == 0x50) && block != nullptr) {
        const auto* p = static_cast<const uint8_t*>(static_cast<OnlineUploadContext*>(block)->mBuffer);
        size_t size = 0;
        if (code == 0x32) {
            while (p[size] != 0 || p[size + 1] != 0) size += 2;
        } else {
            size = std::strlen(reinterpret_cast<const char*>(p));
        }
        g.block.assign(p, p + size);
    } else if (block != nullptr && code >= 2 && code <= 9) {
        auto* list = static_cast<ccReceivedList<uint8_t>*>(block);
        g.count = list->mCount;
        g.infos.assign(list->mInfos, list->mInfos + static_cast<size_t>(list->mCount) * gStride);
        gHeld.push_back(list);
    }
    gGot.push_back(g);
}

// The next answer, pumping as the game's frame loop does; code -999 when none came in ten seconds.
Got Next() {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (gGot.empty() && std::chrono::steady_clock::now() < end) {
        boss::Pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (gGot.empty()) {
        Got none;
        none.code = -999;
        return none;
    }
    Got g = gGot.front();
    gGot.erase(gGot.begin());
    return g;
}

Got Query(const void* owner, uint32_t sub, const std::string& query, size_t stride, uint32_t arg = 0) {
    gStride = stride;
    boss::CityQuery(owner, nullptr, sub, arg, 0, query);
    return Next();
}

std::string ArenaNarrow(const uint8_t* word) {
    const uint32_t offset = Word(word);
    return offset == 0 ? std::string() : std::string(reinterpret_cast<const char*>(tpw::ArenaBase() + offset));
}

std::vector<uint8_t> ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The FTP service's answers: the state and result of each command, and the last progress.
std::vector<std::pair<int32_t, int32_t>> gFtpDone;
int32_t gFtpBytes = 0;

std::pair<int32_t, int32_t> NextFtp() {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (gFtpDone.empty() && std::chrono::steady_clock::now() < end) {
        boss::Pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (gFtpDone.empty()) return {-999, -999};
    const auto answer = gFtpDone.front();
    gFtpDone.erase(gFtpDone.begin());
    return answer;
}

std::u16string Wide(const std::string& text) { return std::u16string(text.begin(), text.end()); }

std::string Value(const std::string& text, const std::string& key) {
    const size_t at = text.find("OnlineInfo." + key);
    if (at == std::string::npos) return "";
    size_t start = text.find_first_not_of(" \t", at + 11 + key.size());
    const size_t end = text.find_first_of("\r\n", start);
    std::string value = text.substr(start, end - start);
    if (!value.empty() && value.front() == '"') value = value.substr(1, value.find('"', 1) - 1);
    return value;
}

int gNews, gAuth, gCity, gUpload, gFtp;

}  // namespace

int main() {
    const char* server = Env("TPW_ONLINE_SERVER");
    if (server == nullptr) {
        std::printf("online_boss_e2e: skipped (TPW_ONLINE_SERVER names no server)\n");
        return 77;
    }
    const std::string name = Env("TPW_ONLINE_NAME", "");
    const std::string password = Env("TPW_ONLINE_PASSWORD", "");
    const fs::path parkFile = Env("TPW_ONLINE_PARK", "");
    const std::string rides = Env("TPW_ONLINE_RIDES", "");
    const uint16_t language = static_cast<uint16_t>(std::atoi(Env("TPW_ONLINE_LANGUAGE", "0")));
    boss::SetServer(server);

    // The server's details (onlineoverride.sam) and its news.
    static const uint8_t kGameGuid[16] = {0x5c, 0x20, 0x0a, 0x41, 0x29, 0x81, 0x11, 0xd3,
                                          0xad, 0xf4, 0x00, 0xe0, 0x18, 0x90, 0x4f, 0xd5};
    Check(boss::Connect(&gNews, boss::Kind::News, server, 7597, Record) == 1, "news: connecting");
    boss::News(&gNews, 7, language, kGameGuid, nullptr);
    Got g = Next();
    const std::string details(g.block.begin(), g.block.end());
    Check(g.code == 0x33 && details.find("OnlineInfo.CityServer") != std::string::npos, "news: the server's details (0x33)");
    std::printf("%s", details.c_str());
    boss::News(&gNews, 5, language, kGameGuid, nullptr);
    g = Next();
    Check(g.code == 0x32, "news: the game news (0x32, " + std::to_string(g.block.size() / 2) + " characters)");
    boss::News(&gNews, 4, language, nullptr, nullptr);
    g = Next();
    Check(g.code == 0x32, "news: the server news (0x32)");
    boss::Release(&gNews);

    // The login.
    const std::string loginHost = Value(details, "LoginServer");
    const int loginPort = std::atoi(Value(details, "LoginPort").c_str());
    Check(boss::Connect(&gAuth, boss::Kind::Auth, loginHost.c_str(), static_cast<uint16_t>(loginPort != 0 ? loginPort : 7598),
                        Record) == 1,
          "login: connecting");
    const std::u16string wideName = Wide(name), widePassword = Wide(password);
    boss::Login(&gAuth, language, wideName.c_str(), widePassword.c_str(), nullptr);
    g = Next();
    Check(g.code == 0x3c, "login: logged in (0x3c), answer " + std::to_string(g.code));
    if (g.code != 0x3c) {
        std::printf("online_boss_e2e: %d checks, %d failures\n", gChecks, gFailures);
        return 1;
    }
    const int32_t player = static_cast<int32_t>(Word(g.block.data()));
    const int32_t customer = static_cast<int32_t>(Word(g.block.data() + 4));
    std::printf("player %d, customer %d, child lock 0x%x\n", player, customer, Word(g.block.data() + 0x60));
    boss::Release(&gAuth);

    // The city lobby.
    const std::string cityHost = Value(details, "CityServer");
    const int cityPort = std::atoi(Value(details, "CityPort").c_str());
    Check(boss::Connect(&gCity, boss::Kind::City, cityHost.c_str(), static_cast<uint16_t>(cityPort), Record) == 1,
          "city: connecting to " + cityHost + ":" + std::to_string(cityPort));
    boss::SetSessionInfo(&gCity, language, player, customer, widePassword.c_str());
    g = Query(&gCity, 1, "", 0x30);
    Check(g.code == 2 && g.count > 0, "city: the city list (" + std::to_string(g.count) + " cities)");
    if (g.count <= 0) return 1;
    const int32_t city = static_cast<int32_t>(Word(g.infos.data()));
    const std::string cityFtp = ArenaText(g.infos.data() + 0x1c);
    std::printf("city %d: %s\n", city, ArenaText(g.infos.data() + 4).c_str());
    char query[0x80];
    std::snprintf(query, sizeof query, "CITYID=%d", city);
    g = Query(&gCity, 2, query, 0x66);
    const int32_t before = g.code == 3 ? g.count : -1;
    Check(g.code == 3, "city: the city's parks (" + std::to_string(before) + ")");
    g = Query(&gCity, 3, query, 0x66);
    Check(g.code == 3, "city: the city's top 10 (" + std::to_string(g.count) + ")");
    g = Query(&gCity, 7, "LOGICALID=1", 0x26, 0);
    std::string uploadHost, ftpRoot;
    uint16_t uploadPort = 0;
    bool ftpListed = false;
    for (int32_t i = 0; g.code == 4 && i < g.count; ++i) {
        const uint8_t* row = g.infos.data() + static_cast<size_t>(i) * 0x26;
        uint16_t port;
        std::memcpy(&port, row + 0x14, 2);
        if (Word(row + 4) == 0x64) {
            uploadHost = ArenaText(row + 0xc);
            uploadPort = port;
        } else if (Word(row + 4) == 1) {
            ftpRoot = ArenaText(row + 0x22);
            ftpListed = true;
        }
    }
    Check(g.code == 4 && uploadPort != 0, "city: the server list names the upload server (" + uploadHost + ":" +
                                              std::to_string(uploadPort) + ")");

    // A park published, then listed in its city.
    if (!parkFile.empty() && uploadPort != 0) {
        tpw::config().userDir = parkFile.parent_path();
        Check(boss::Connect(&gUpload, boss::Kind::Upload, uploadHost.c_str(), uploadPort, Record) == 1, "upload: connecting");
        boss::SetSessionInfo(&gUpload, language, player, customer, widePassword.c_str());
        ccParkFileInfo info{};
        info.mF00 = city;
        info.mThemeID = 1;
        info.mFlag = 1;
        const std::u16string parkName = u"E2E Park", parkDesc = u"Published by the port's online client test";
        std::memcpy(info.mName, parkName.c_str(), (parkName.size() + 1) * 2);
        std::memcpy(info.mDescription, parkDesc.c_str(), (parkDesc.size() + 1) * 2);
        size_t n = 0;
        for (size_t at = 0; at < rides.size() && n < 255;) {
            const size_t comma = rides.find(',', at);
            info.mWords[n++] = static_cast<uint16_t>(std::strtoul(rides.substr(at, comma - at).c_str(), nullptr, 16));
            at = comma == std::string::npos ? rides.size() : comma + 1;
        }
        const std::string file = parkFile.filename().string();
        boss::UploadParkRequest(&gUpload, file.c_str(), &info, nullptr);
        g = Next();
        Check(g.code == 0x50 && std::string(g.block.begin(), g.block.end()) == "granted", "upload: granted (0x50)");
        if (g.code == 0x50) {
            boss::UploadParkFile(&gUpload, file.c_str(), nullptr);
            g = Next();
            Check(g.code == 0, "upload: the park kept (0), answer " + std::to_string(g.code));
        }
        boss::Release(&gUpload);
        g = Query(&gCity, 2, query, 0x66);
        bool listed = false;
        std::string guid;
        for (int32_t i = 0; g.code == 3 && i < g.count; ++i) {
            const uint8_t* row = g.infos.data() + static_cast<size_t>(i) * 0x66;
            if (ArenaText(row + 4) != "E2E Park") continue;
            listed = true;
            guid = ArenaNarrow(row + 0x44);   // the newest is listed first
            if (!guid.empty()) break;
        }
        Check(g.code == 3 && g.count == before + 1 && listed, "city: the published park is listed (" + std::to_string(g.count) +
                                                                  " parks)");

        // The park downloaded as a visit asks for it: <root>/<city ftp name>/<GUID text>, saved as <first 19>/<last 17>.LAYS.
        Check(ftpListed && guid.size() == 36, "ftp: the server list names the FTP service (root " + ftpRoot + "), the park its GUID " +
                                                  guid);
        if (ftpListed && guid.size() == 36) {
            const std::vector<uint8_t> sent = ReadFile(parkFile);
            boss::ftp::LegalTexts legal;   // the game's text is the park's own here (its language's 0x500 bytes)
            for (size_t at = 5; at + 1 < 5 + 0x500 && at + 1 < sent.size() && (sent[at] != 0 || sent[at + 1] != 0); at += 2)
                legal.text[sent.size() > 4 && sent[4] < 7 ? sent[4] : 0] += static_cast<char16_t>(sent[at] | sent[at + 1] << 8);
            boss::ftp::SetLegalTexts(legal);
            boss::ftp::SetPort(static_cast<uint16_t>(std::atoi(Env("TPW_ONLINE_FTP_PORT", "21"))));
            boss::ftp::SetReports(boss::ftp::Reports{[](void*, int32_t done, int32_t) { gFtpBytes = done; },
                                                     [](void*, int32_t state, int32_t result) {
                                                         gFtpDone.push_back({state, result});
                                                     }});
            const fs::path folder = parkFile.parent_path() / "e2e-download";
            const fs::path local = folder / guid.substr(0, 19) / (guid.substr(19) + ".LAYS");
            std::error_code ec;
            fs::remove_all(folder, ec);
            fs::create_directories(local.parent_path(), ec);
            boss::ftp::Connect(&gFtp, "anonymous", name + "@tpw.test");
            auto answer = NextFtp();
            Check(answer == std::make_pair(boss::ftp::kConnect, 0), "ftp: logged in (" + std::to_string(answer.second) + ")");
            boss::ftp::Download(&gFtp, ftpRoot + "/" + cityFtp + "/" + guid, local.string());
            answer = NextFtp();
            Check(answer == std::make_pair(boss::ftp::kDownload, 0), "ftp: the park downloaded (" + std::to_string(answer.second) +
                                                                         ", " + std::to_string(gFtpBytes) + " bytes)");
            Check(ReadFile(local) == sent, "ftp: the file is the one published (" + std::to_string(sent.size()) + " bytes)");
            boss::ftp::Download(&gFtp, ftpRoot + "/" + cityFtp + "/00000000-0000-0000-0000-000000000000",
                                (folder / "00000000-0000-0000" / "-000000000000.LAYS").string());
            answer = NextFtp();
            Check(answer == std::make_pair(boss::ftp::kDownload, boss::ftp::kErrorNoFile),
                  "ftp: a park the server does not have (0x44), answer " + std::to_string(answer.second));
            boss::ftp::Quit(&gFtp);
            answer = NextFtp();
            Check(answer == std::make_pair(boss::ftp::kQuit, 0), "ftp: quit (" + std::to_string(answer.second) + ")");
            boss::ftp::End(&gFtp);
            fs::remove_all(folder, ec);
        }
    }
    boss::Release(&gCity);
    for (auto* list : gHeld) list->mRefCount = 1;   // let go, as the game does once it copied the lists
    boss::Pump();
    std::printf("online_boss_e2e: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
