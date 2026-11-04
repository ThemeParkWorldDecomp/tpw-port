// online_library.cpp: the Mac's "online" shared library (Auth, News, City and Upload, include/FS_Online.h), which main imports and
// the port does not have. The classes hand every request to the online backend (include/platform/online_backend.h) and give its
// answers back the way the library did; notes/online-screens.md maps the screens and the formats.
//
// With no backend (the launcher sets none) and no online server set, every Connect answers 0, as the Mac's
// library did when no server answered: CFSOnlineSystem::DoLogin (0x10171234) reports error 0x4f and the player sees the game's own
// "cannot connect" dialog over the login panel, whose Cancel goes back to the islands. Nothing else is reached then.
//
// With a backend:
// - Auth::Login answers at once (the login is done when TickLogin next looks).
// - News, City and Upload calls answer a request id; the game keeps the id only after the call returns, so the answer is queued and
//   given from GetCurrentMessage(0), which the game's tickers call every frame while a request is out.
// - The counted blocks the game frees when their use count reaches 0 (the login account, the news and upload contexts, the received
//   lists) are given with a count of 2 and kept here, so the game never frees what it did not allocate. A received list the game
//   has let go of (its count back to 1) is freed here; the game copies every string into its own records first.
// - A park list answer also puts each park's file where a visit looks for it (LoggedInRequestFTPDownload 0x10176418), so a visit
//   loads the park without the FTP client, whose sockets stay stand-ins.
// With an online server set by the launcher (tpw::online::boss::Active, include/platform/online_boss.h: the player's, the one
// tpw-port's online.txt names, or the built-in one), every call goes to the online server client instead, which speaks the
// original protocol to that server.
// No network call is made here (devtools/check_offline.py): the server client's sockets are platform/online_boss_socket.cpp's.
// Not in platform/msl.cpp, which the test libraries link (their Auth is the test's own fake).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "FS_Online.h"
#include "FS_OnlineList.h"
#include "FS_UserInfo.h"
#include "SaveLoad.h"
#include "TPGlobals.h"
#include "TPString.h"
#include "TbITable.h"
#include "TbResource.h"
#include "Text.h"
#include "platform.h"
#include "online_lists.h"
#include "platform/arena.h"
#include "platform/online_backend.h"
#include "platform/online_boss.h"
#include "platform/online_ftp.h"

namespace fs = std::filesystem;

namespace tpw::online {
namespace {
std::unique_ptr<Backend>& BackendSlot() {
    static std::unique_ptr<Backend> backend;
    return backend;
}
}  // namespace

void SetBackend(std::unique_ptr<Backend> backend) { BackendSlot() = std::move(backend); }

Backend* CurrentBackend() { return BackendSlot().get(); }

}  // namespace tpw::online

namespace {

using tpw::online::Backend;
using tpw::online::CurrentBackend;
using Callback = void (*)(int32_t, int32_t, void*, void*);
namespace boss = tpw::online::boss;
using boss::Kind;

// A City request's query text (CityClient's snprintf into 0x80 bytes).
std::string QueryText(const char* format, int32_t a, int32_t b = 0) {
    char text[0x80];
    std::snprintf(text, sizeof text, format, a, b);
    return text;
}

// One open connection: the game's callback and the answers waiting for its next GetCurrentMessage.
struct Connection {
    Callback callback = nullptr;
    std::deque<std::function<void(Callback)>> answers;
};

std::map<const void*, Connection>& Connections() {
    static std::map<const void*, Connection> connections;
    return connections;
}

// Request ids: never 0 (no request) and never 0xffffffff (the game's "nothing asked").
uint32_t NextRequest() {
    static uint32_t last = 0;
    do {
        ++last;
    } while (last == 0 || last == 0xffffffffu);
    return last;
}

int32_t Open(const void* object, Callback callback) {
    if (CurrentBackend() == nullptr || callback == nullptr) return 0;
    Connections()[object].callback = callback;
    return 1;
}

void Close(const void* object) { Connections().erase(object); }

void Queue(const void* object, std::function<void(Callback)> answer) {
    auto it = Connections().find(object);
    if (it != Connections().end()) it->second.answers.push_back(std::move(answer));
}

// The name the player logged in with (a published park's owner).
std::u16string& LoginName() {
    static std::u16string name;
    return name;
}

// ---- Received lists (platform/online_lists.h) ---------------------------------------------------------------------------------

using tpw::online::lists::InfoWriter;
using tpw::online::lists::Sweep;

ccReceivedList<ccCityInfo>* CityList() {
    const std::vector<tpw::online::City> cities = CurrentBackend()->Cities();
    InfoWriter w(cities.size(), 0x30);
    for (size_t i = 0; i < cities.size(); ++i) {
        const tpw::online::City& city = cities[i];
        w.Word(i, 0x00, static_cast<uint32_t>(city.id));
        w.Wide(i, 0x04, city.name);
        w.Wide(i, 0x08, city.text08);
        w.Float(i, 0x0c, city.x);
        w.Float(i, 0x10, city.z);
        w.Float(i, 0x14, city.y);
        w.Word(i, 0x18, static_cast<uint32_t>(city.parkCount));
        w.Wide(i, 0x1c, city.text1C);
        w.Word(i, 0x20, static_cast<uint32_t>(city.f20));
        w.Word(i, 0x24, static_cast<uint32_t>(city.f24));
        w.Word(i, 0x28, static_cast<uint32_t>(city.closed));
        w.Word(i, 0x2c, static_cast<uint32_t>(city.room));
    }
    return w.Give<ccCityInfo>(static_cast<int32_t>(cities.size()));
}

// Where a visit looks for a park's file: <save folder>:online:<the file name's first 0x13 characters>:<the rest>.LAYS, as
// LoggedInRequestFTPDownload (0x10176418) builds it. The park's file is copied there (the host's user folder) when it is not
// there yet or is older. kVisitsOpen: the park then opens in online mode, with the in-park client (COnlineControl: chat, the
// visitor and buddy lists, votes); false makes a visit fail with the game's own error, as before that client was wired.
constexpr bool kVisitsOpen = true;

void PlaceParkFile(const tpw::online::Park& park) {
    if (!kVisitsOpen || gUserInfo == nullptr || park.file.empty()) return;
    TbDynamicStringTemplate<char16_t> online(gUserInfo->ReturnOnlinePath());
    online += gTPGlobals.mWideSeparator;
    TbDynamicStringTemplate<char> path;
    UniStringToString(path, online);
    std::string gamePath(path.mData);
    gamePath += park.fileName.substr(0, 0x13);
    gamePath += ":";
    gamePath += park.fileName.size() > 0x13 ? park.fileName.substr(0x13) : std::string();
    gamePath += gTPGlobals.mF4484.mData;
    const fs::path target = tpw::HostPathForWriting(gamePath);
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    fs::copy_file(park.file, target, fs::copy_options::update_existing, ec);
    if (ec) tpw::Log("online: could not place park %d for a visit: %s", park.id, ec.message().c_str());
}

ccReceivedList<ccParkInfo>* ParkList(const std::vector<tpw::online::Park>& parks) {
    InfoWriter w(parks.size(), 0x66);
    for (size_t i = 0; i < parks.size(); ++i) {
        const tpw::online::Park& park = parks[i];
        w.Word(i, 0x00, static_cast<uint32_t>(park.id));
        w.Wide(i, 0x04, park.name);
        w.Wide(i, 0x08, park.description);   // the park info panel's text box
        w.Wide(i, 0x0c, park.owner);
        w.Word(i, 0x10, static_cast<uint32_t>(park.city));
        w.Word(i, 0x14, static_cast<uint32_t>(park.theme));
        w.Word(i, 0x18, static_cast<uint32_t>(park.visits));
        w.Word(i, 0x1c, static_cast<uint32_t>(park.votes));
        // 0x20..0x3f: the ride bits, none (a park needs no ride file from the server)
        w.Word(i, 0x40, static_cast<uint32_t>(park.flagged));
        w.Narrow(i, 0x44, park.fileName);
        w.Wide(i, 0x4c, park.email);         // its "Email" line (string 0x114; empty shows string 0x11c)
        // the day it was published, the park info panel's three numbers (string 0x1c0)
        w.Half(i, 0x50, static_cast<uint16_t>(park.day));
        w.Half(i, 0x52, static_cast<uint16_t>(park.month));
        w.Half(i, 0x54, static_cast<uint16_t>(park.year));
        w.Word(i, 0x5e, park.rank > 0 ? static_cast<uint32_t>(park.rank) : 0x7fffffffu);   // 0x7fffffff: no rank
        PlaceParkFile(park);
    }
    return w.Give<ccParkInfo>(static_cast<int32_t>(parks.size()));
}

ccReceivedList<ccThemeInfo>* ThemeList() {
    const std::vector<tpw::online::Theme> themes = CurrentBackend()->Themes();
    InfoWriter w(themes.size(), sizeof(ccThemeInfo));
    for (size_t i = 0; i < themes.size(); ++i) {
        w.Word(i, 0x00, static_cast<uint32_t>(themes[i].id));
        w.Wide(i, 0x04, themes[i].name);
        w.Wide(i, 0x08, themes[i].name);
        w.Wide(i, 0x0c, themes[i].name);
    }
    return w.Give<ccThemeInfo>(static_cast<int32_t>(themes.size()));
}

ccReceivedList<ccServerInfo>* ServerList() {
    const std::vector<tpw::online::Server> servers = CurrentBackend()->Servers();
    InfoWriter w(servers.size(), 0x26);
    for (size_t i = 0; i < servers.size(); ++i) {
        const tpw::online::Server& server = servers[i];
        w.Word(i, 0x00, static_cast<uint32_t>(server.id));
        w.Word(i, 0x04, static_cast<uint32_t>(server.type));
        w.Wide(i, 0x08, server.host);
        w.Wide(i, 0x0c, server.host);   // the host the game connects to (CFSOnlineServer's mText24)
        w.Half(i, 0x14, server.port);
        w.Wide(i, 0x16, u"");
        w.Wide(i, 0x1a, u"");
        w.Wide(i, 0x22, u"");
    }
    return w.Give<ccServerInfo>(static_cast<int32_t>(servers.size()));
}

ccReceivedList<ccRideInfo>* RideList() {
    InfoWriter w(0, 0x34);
    return w.Give<ccRideInfo>(0);
}

// A City request: its id now, its answer (code 2 to 6 with the list made then) at the next GetCurrentMessage.
template <class MAKE>
int32_t AskCity(const City* city, int32_t code, MAKE make) {
    const uint32_t id = NextRequest();
    Queue(city, [id, code, make](Callback callback) { callback(code, static_cast<int32_t>(id), nullptr, make()); });
    return static_cast<int32_t>(id);
}

// GetCurrentMessage(0): the waiting answers are given, then the lists the game let go of are freed. Nothing is ever "in flight"
// afterwards, so the answer is 0.
uint32_t Poll(const void* object, int32_t which) {
    if (which != 0) return 0;
    auto it = Connections().find(object);
    if (it != Connections().end()) {
        const Callback callback = it->second.callback;
        std::deque<std::function<void(Callback)>> answers;
        answers.swap(it->second.answers);
        for (auto& answer : answers) {
            if (Connections().find(object) == Connections().end()) break;   // released by an answer
            answer(callback);
        }
    }
    Sweep();
    return 0;
}

void NoProgress(int32_t* done, int32_t* total) {
    if (done != nullptr) *done = 0;
    if (total != nullptr) *total = 0;
}

std::u16string Bounded(const char16_t* text, size_t most) {
    std::u16string out;
    for (size_t i = 0; text != nullptr && i < most && text[i] != 0; ++i) out += text[i];
    return out;
}

// What UploadParkRequest was given, for the UploadParkFile that follows.
std::map<const void*, tpw::online::Publish>& PendingPublish() {
    static std::map<const void*, tpw::online::Publish> pending;
    return pending;
}

// A counted context (the news texts and the upload's grant): given with a count of 2, so the game never frees it.
OnlineUploadContext* Context(OnlineUploadContext& context, void* buffer) {
    context.mBuffer = buffer;
    context.mRefCount = 2;
    return &context;
}

}  // namespace

// ---- Auth: the login --------------------------------------------------------------------------------------------------------

Auth::Auth() {}

Auth::~Auth() {
    boss::Release(this);
    Close(this);
}

void Auth::Release() {
    boss::Release(this);
    Close(this);
}

int32_t Auth::Connect(char* server, char* unused, uint16_t port, void (*callback)(int32_t, int32_t, void*, void*)) {
    (void)unused;
    if (boss::Active()) return boss::Connect(this, Kind::Auth, server, port, callback);
    (void)server;
    (void)port;
    return Open(this, callback);
}

// The answer is given at once: 0x3c with the account block (LoginConnectionCallback 0x101715a0), or -2 (the game's "wrong name or
// password") when the backend refuses. The password goes to the backend only; nothing here keeps it.
void Auth::Login(uint16_t language, char16_t* name, char16_t* password, void* context) {
    if (boss::Active()) return boss::Login(this, language, name, password, context);
    (void)language;
    (void)context;
    auto it = Connections().find(this);
    if (it == Connections().end()) return;
    const Callback callback = it->second.callback;
    tpw::online::Account account;
    const std::u16string loginName = Bounded(name, 0x100);
    if (!CurrentBackend()->Login(loginName, Bounded(password, 0x100), &account)) {
        tpw::Log("online: the login was refused");
        callback(-2, 0, nullptr, nullptr);
        return;
    }
    LoginName() = loginName;
    static uint8_t block[0x6c];
    std::memset(block, 0, sizeof(block));
    std::memcpy(block + 0x00, &account.playerID, 4);
    std::memcpy(block + 0x04, &account.customerID, 4);
    const std::u16string email = account.email.substr(0, 19);
    std::memcpy(block + 0x30, email.c_str(), (email.size() + 1) * sizeof(char16_t));
    std::memcpy(block + 0x60, &account.childLock, 4);
    std::memcpy(block + 0x64, &account.avatar, 4);
    const uint32_t uses = 2;
    std::memcpy(block + 0x68, &uses, 4);
    tpw::Log("online: logged in");
    callback(0x3c, 0, nullptr, block);
}

// ---- News -------------------------------------------------------------------------------------------------------------------

News::News() {}

News::~News() {
    boss::Release(this);
    Close(this);
}

void News::Release() {
    boss::Release(this);
    Close(this);
}

int32_t News::Connect(char* server, char* unused, uint16_t port, void (*callback)(int32_t, int32_t, void*, void*)) {
    (void)unused;
    if (boss::Active()) return boss::Connect(this, Kind::News, server, port, callback);
    (void)server;
    (void)port;
    return Open(this, callback);
}

uint32_t News::GetCurrentMessage(int32_t which) {
    if (boss::Active()) return boss::GetCurrentMessage(this, which);
    return Poll(this, which);
}

void News::MonitorMessage(uint32_t id, int32_t* done, int32_t* total) {
    if (boss::Active()) return boss::MonitorMessage(this, id, done, total);
    (void)id;
    NoProgress(done, total);
}

// 0x33: the server details, narrow text in online.sam's format (NewsConnectionCallback 0x10172030).
uint32_t News::GetGameServerDetails(uint16_t language, NS_MacDoze::_GUID* guid, void* context) {
    if (boss::Active()) return boss::News(this, 7, language, reinterpret_cast<const uint8_t*>(guid), context);
    (void)language;
    (void)guid;
    (void)context;
    const uint32_t id = NextRequest();
    Queue(this, [id](Callback callback) {
        static std::string text;
        static OnlineUploadContext held;
        text = CurrentBackend()->ServerDetails();
        callback(0x33, static_cast<int32_t>(id), nullptr, Context(held, text.data()));
    });
    return id;
}

// 0x32 with the request id: a news text, wide.
uint32_t News::GetGameNews(uint16_t language, void* guid, void* context) {
    if (boss::Active()) return boss::News(this, 5, language, static_cast<const uint8_t*>(guid), context);
    (void)guid;
    (void)context;
    const uint32_t id = NextRequest();
    Queue(this, [id, language](Callback callback) {
        static std::u16string text;
        static OnlineUploadContext held;
        text = CurrentBackend()->GameNews(language);
        callback(0x32, static_cast<int32_t>(id), nullptr, Context(held, text.data()));
    });
    return id;
}

uint32_t News::GetServerNews(uint16_t language, void* context) {
    if (boss::Active()) return boss::News(this, 4, language, nullptr, context);
    (void)context;
    const uint32_t id = NextRequest();
    Queue(this, [id, language](Callback callback) {
        static std::u16string text;
        static OnlineUploadContext held;
        text = CurrentBackend()->ServerNews(language);
        callback(0x32, static_cast<int32_t>(id), nullptr, Context(held, text.data()));
    });
    return id;
}

// ---- City: the world, its cities and parks ------------------------------------------------------------------------------------

City::City() {}

City::~City() {
    boss::Release(this);
    Close(this);
}

void City::Release() {
    boss::Release(this);
    Close(this);
}

int32_t City::Connect(char* server, char* unused, uint16_t port, void (*callback)(int32_t, int32_t, void*, void*)) {
    (void)unused;
    if (boss::Active()) return boss::Connect(this, Kind::City, server, port, callback);
    (void)server;
    (void)port;
    return Open(this, callback);
}

void City::SetSessionInfo(uint16_t language, int32_t a, int32_t b, char16_t* password) {
    if (boss::Active()) return boss::SetSessionInfo(this, language, a, b, password);
    (void)language;
    (void)a;
    (void)b;
    (void)password;
}

uint32_t City::GetCurrentMessage(int32_t which) {
    if (boss::Active()) return boss::GetCurrentMessage(this, which);
    return Poll(this, which);
}

void City::MonitorMessage(uint32_t id, int32_t* done, int32_t* total) {
    if (boss::Active()) return boss::MonitorMessage(this, id, done, total);
    (void)id;
    NoProgress(done, total);
}

int32_t City::GetCityList(void* context, uint32_t id, int32_t flag) {
    if (boss::Active()) return boss::CityQuery(this, context, 1, id, flag, "");
    (void)context;
    (void)id;
    (void)flag;
    return AskCity(this, 2, [] { return static_cast<void*>(CityList()); });
}

int32_t City::GetParkInCityList(void* context, int32_t city, uint32_t arg, int32_t flag) {
    if (boss::Active()) return boss::CityQuery(this, context, 2, arg, flag, QueryText("CITYID=%d", city));
    (void)context;
    (void)arg;
    (void)flag;
    return AskCity(this, 3, [city] { return static_cast<void*>(ParkList(CurrentBackend()->ParksInCity(city))); });
}

int32_t City::GetParkInCityListTop10(void* context, int32_t city, uint32_t arg, int32_t flag) {
    if (boss::Active()) return boss::CityQuery(this, context, 3, arg, flag, QueryText("CITYID=%d", city));
    (void)context;
    (void)arg;
    (void)flag;
    return AskCity(this, 3, [city] { return static_cast<void*>(ParkList(CurrentBackend()->TopTen(city))); });
}

int32_t City::GetParkInWorldByName(void* context, char16_t* name, uint32_t arg, int32_t flag) {
    if (boss::Active()) return boss::CityQuery(this, context, 6, arg, flag, "OWNERNAME=" + boss::HexText(name, 0x200));
    (void)context;
    (void)arg;
    (void)flag;
    const std::u16string owner = Bounded(name, 0x100);
    return AskCity(this, 3, [owner] { return static_cast<void*>(ParkList(CurrentBackend()->ParksByOwner(owner))); });
}

int32_t City::GetParkByID(void* context, int32_t park, uint32_t arg, int32_t flag) {
    if (boss::Active()) return boss::CityQuery(this, context, 11, arg, flag, QueryText("PARKID=%d", park));
    (void)context;
    (void)arg;
    (void)flag;
    return AskCity(this, 3, [park] { return static_cast<void*>(ParkList(CurrentBackend()->ParkByID(park))); });
}

int32_t City::GetLogicalServerList(void* context, int32_t a, uint32_t b, int32_t c) {
    if (boss::Active()) return boss::CityQuery(this, context, 7, b, c, QueryText("LOGICALID=%d", a));
    (void)context;
    (void)a;
    (void)b;
    (void)c;
    return AskCity(this, 4, [] { return static_cast<void*>(ServerList()); });
}

int32_t City::GetThemeList(void* context, uint32_t arg, int32_t flag) {
    if (boss::Active()) return boss::CityQuery(this, context, 8, arg, flag, "");
    (void)context;
    (void)arg;
    (void)flag;
    return AskCity(this, 5, [] { return static_cast<void*>(ThemeList()); });
}

int32_t City::GetRideList(void* context, uint32_t id, int32_t flag) {
    if (boss::Active()) return boss::CityQuery(this, context, 9, id, flag, "");
    (void)context;
    (void)id;
    (void)flag;
    return AskCity(this, 6, [] { return static_cast<void*>(RideList()); });
}

void City::MarkParkVisited(void* context, int32_t park, int32_t arg) {
    if (boss::Active()) {
        boss::CityQuery(this, context, 101, 0, 0, QueryText("PLAYERID=%d;PARKID=%d", arg, park));
        return;
    }
    (void)context;
    (void)arg;
    if (CurrentBackend() != nullptr) CurrentBackend()->MarkVisited(park);
}

void City::UnpublishPark(void* context, int32_t park, int32_t user) {
    if (boss::Active()) {
        boss::CityQuery(this, context, 100, 0, 0, QueryText("PLAYERID=%d;PARKID=%d", user, park));
        return;
    }
    (void)context;
    if (CurrentBackend() != nullptr) CurrentBackend()->UnpublishPark(park, user);
}

// ---- Upload: publishing a park ------------------------------------------------------------------------------------------------

Upload::Upload() {}

// The game releases an upload connection and never deletes it (ReleaseUploadServerConnection).
void Upload::Release() {
    boss::Release(this);
    Close(this);
    PendingPublish().erase(this);
}

int32_t Upload::Connect(char* server, char* unused, uint16_t port, void (*callback)(int32_t, int32_t, void*, void*)) {
    (void)unused;
    if (boss::Active()) return boss::Connect(this, Kind::Upload, server, port, callback);
    (void)server;
    (void)port;
    return Open(this, callback);
}

void Upload::SetSessionInfo(int16_t language, int32_t a, int32_t b, char16_t* password) {
    if (boss::Active()) return boss::SetSessionInfo(this, static_cast<uint16_t>(language), a, b, password);
    (void)language;
    (void)a;
    (void)b;
    (void)password;
}

uint32_t Upload::GetCurrentMessage(int32_t which) {
    if (boss::Active()) return boss::GetCurrentMessage(this, which);
    return Poll(this, which);
}

void Upload::MonitorMessage(uint32_t id, int32_t* done, int32_t* total) {
    if (boss::Active()) return boss::MonitorMessage(this, id, done, total);
    (void)id;
    NoProgress(done, total);
}

// The request is granted (0x50 with a counted context, UploadServerConnectionCallback 0x101753dc); the game then sends the file.
void Upload::UploadParkRequest(char* file, ccParkFileInfo* info, void* context) {
    if (boss::Active()) return boss::UploadParkRequest(this, file, info, context);
    (void)file;
    (void)context;
    tpw::online::Publish publish;
    publish.city = info->mF00;
    publish.theme = info->mThemeID;
    publish.flag = info->mFlag;
    publish.name = Bounded(info->mName, 20);
    publish.description = Bounded(info->mDescription, 256);
    publish.owner = LoginName();
    PendingPublish()[this] = publish;
    Queue(this, [](Callback callback) {
        static OnlineUploadContext held;
        callback(0x50, 0, nullptr, Context(held, nullptr));
    });
}

// The file is kept by the backend: 0 ("file transfer ok") when it is, -1 when not.
void Upload::UploadParkFile(char* file, void* context) {
    if (boss::Active()) return boss::UploadParkFile(this, file, context);
    (void)context;
    auto it = PendingPublish().find(this);
    bool kept = false;
    if (it != PendingPublish().end() && file != nullptr) {
        it->second.file = tpw::FindHostPath(file);
        kept = !it->second.file.empty() && CurrentBackend() != nullptr && CurrentBackend()->PublishPark(it->second);
        if (!kept) tpw::Log("online: the park %s could not be published", file);
        PendingPublish().erase(it);
    }
    Queue(this, [kept](Callback callback) { callback(kept ? 0 : -1, 0, nullptr, nullptr); });
}

// The game's FTP client has no thread of its own on the port (its thread and sockets are stand-ins), so a park download that needs
// it fails at once (LoggedInRequestFTPDownload, TPW_PORT_FIX ftpthread) with the game's own error, unless an online server takes
// it. With an online server set, the server's park downloads (include/platform/online_ftp.h) do the client's work, once visits are
// open (kVisitsOpen); until then 0x40, "Couldn't connect to FTP server", as before. With a backend the file is not in its world,
// since every park file it lets be visited is placed before a visit (PlaceParkFile): 0x44, "Remote file doesn't exist". With
// neither no server can be reached: 0x40.
namespace ftpdl_callee {
int32_t PortFTPDownloadUnavailable() {
    if (boss::Active()) return kVisitsOpen ? 0 : 0x40;
    return CurrentBackend() != nullptr ? 0x44 : 0x40;
}
}  // namespace ftpdl_callee

// With an online server set the client's commands go to the server's park downloads (TPW_PORT_FIX ftpthread, FTPClient.cpp): the
// progress words the download's bar reads (TickFTPDownload) are written and each command's end goes to the client's control
// (CFSOnlineFTPControl::CallbackFn, which does not read `this`), on the game's thread (boss::Pump). A local path is the host's
// copy of the game path, in the user folder.
namespace {

void FtpProgress(void* owner, int32_t done, int32_t total) {
    auto* client = static_cast<FTPClient*>(owner);
    client->mTotal = total;
    client->mDone = done;
}

void FtpDone(void* owner, int32_t state, int32_t result) {
    reinterpret_cast<CFSOnlineFTPControl*>(static_cast<FTPClient*>(owner)->mControl)->CallbackFn(state, result);
}

bool FtpTakes(const FTPClient* client) { return boss::Active() || boss::ftp::Has(client); }

}  // namespace

namespace ftp_callee {
bool PortFTPConnect(FTPClient* self, const char* user, const char* password) {
    if (!FtpTakes(self)) return false;
    boss::ftp::LegalTexts legal;
    for (int i = 0; i < 7; ++i)
        for (const char16_t* p = gSaveLegalText[i].mData; p != nullptr && *p != 0; ++p) legal.text[i] += *p;
    boss::ftp::SetLegalTexts(legal);
    boss::ftp::SetReports(boss::ftp::Reports{FtpProgress, FtpDone});
    // A development aid: TPW_ONLINE_FTP_PORT names another port than the game's 21 for a local online server, which cannot
    // listen on 21 without root (the name platform/tests/online_boss_e2e.cpp reads for the same reason).
    if (const char* port = std::getenv("TPW_ONLINE_FTP_PORT"); port != nullptr && std::atoi(port) > 0 && std::atoi(port) < 65536)
        boss::ftp::SetPort(static_cast<uint16_t>(std::atoi(port)));
    boss::ftp::Connect(self, user != nullptr ? user : "", password != nullptr ? password : "");
    return true;
}

bool PortFTPDownload(FTPClient* self, const char* remote, const char* local) {
    if (!FtpTakes(self)) return false;
    boss::ftp::Download(self, remote != nullptr ? remote : "", local != nullptr ? tpw::HostPathForWriting(local).string() : "");
    return true;
}

bool PortFTPDisconnect(FTPClient* self) {
    if (!FtpTakes(self)) return false;
    boss::ftp::Quit(self);
    return true;
}

void PortFTPEnd(FTPClient* self) { boss::ftp::End(self); }
}  // namespace ftp_callee

// The online server client's message for a server that cannot be reached, as the game's text table of one string: TbITable's record
// is the word 1 | length << 8, the text and its NUL padded to four bytes, then a zero word (TbITable::GetConstString). The game's
// tables hold their text in the language's code page, which the error dialog turns back into Unicode (UIMain_ConvertMulti), so the
// text goes through the language's Unicode to multibyte converter (text resource 1) first; without one it is kept as it is.
TbITable* tpw::online::boss::UnreachableTable() {
    static TbITable* table = [] {
        const std::size_t count = std::strlen(kUnreachableText);
        const std::u16string wide(kUnreachableText, kUnreachableText + count);
        std::vector<unsigned char> text(count * 2 + 2, 0);
        if (auto* converter = static_cast<TbIUnicodeToMB*>(gText.mResourcePointers[1]))
            converter->ConvertString(text.data(), wide.c_str(), static_cast<uint16_t>(text.size()));
        else
            std::memcpy(text.data(), kUnreachableText, count);
        const uint32_t length = static_cast<uint32_t>(std::strlen(reinterpret_cast<const char*>(text.data())) + 1);
        static std::vector<unsigned char> record(4 + ((length + 3) & ~3u) + 4, 0);
        const uint32_t word = 1 | (length << 8);
        std::memcpy(record.data(), &word, 4);
        std::memcpy(record.data() + 4, text.data(), length);
        static const unsigned char* strings[1] = {record.data()};
        auto* made = new TbITable();
        made->mCount = 1;
        made->mStrings = strings;
        return made;
    }();
    return table;
}
