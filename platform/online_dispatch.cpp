// online_dispatch.cpp: the callees of the online client (COnlineControl, the buddy map's tree, the login getters, the rank text object)
// that the translations declare in per-file namespaces so that their unit tests can fake them (include/online.h, include/std_basic_filebuf.h,
// include/FS_Online.h, include/ui_DrawObject.h, include/ui_publishParkPanel.h) and that the launcher had stand-ins for. In the launcher they
// are the calls themselves: the runtime glue (operator new and delete, strlen, strcpy, memmove, __throw) on the host's runtime
// (platform/msl_runtime.cpp), the translated game functions at the Mac addresses in the comments, and the UI's online members.
//
// Only the callees whose caller and target agree on the object they pass are here. The strings, sets, deques, lists and mutexes that
// COnlineControl, OnlineChatData and the parser hold as 32-bit words (a 4 byte slot per object) are not: the translated library code
// that would run on them keeps native 8 byte pointers or arena offsets, so it cannot be called on those slots (see the wire-w1 report).
// Not in platform/msl.cpp, which the test libraries link.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "FS_LoadSave.h"
#include "FS_MasterSave.h"
#include "FS_Online.h"
#include "FS_UserInfo.h"
#include "MslSubscriptionTree.h"
#include "TPString.h"
#include "ThingReceivers.h"
#include "UIInterface.h"
#include "lobby.h"
#include "online.h"
#include "std_basic_filebuf.h"
#include "std_basic_istream.h"
#include "ui_DrawObject.h"
#include "ui_ime.h"
#include "ui_mainmenu.h"
#include "ui_publishParkPanel.h"

namespace {

// The host objects built on a block the Mac sized at `macSize`: IME::MslNew grants twice the size plus 32 bytes, zeroed.
constexpr size_t Granted(size_t macSize) { return macSize * 2 + 32; }
static_assert(sizeof(CFSOnlineSystem) <= Granted(0x404), "CFSOnlineSystem fits the block online_callee::OperatorNew grants for 0x404 bytes");
static_assert(sizeof(CFSGlobalMasterSave) <= Granted(0x4c), "CFSGlobalMasterSave fits the block granted for 0x4c bytes");
static_assert(sizeof(InterfaceRankTextObject) <= Granted(0x19c), "InterfaceRankTextObject fits the block granted for 0x19c bytes");

}  // namespace

// 0x10219af4 (src/game/online/online.cpp): the member is declared void, the translation answers whether the thing is an online person.
bool OnlineControlTeleportThing(tag_IDX_TYPE<IDXTYPEUniqueNameThingId, uint16_t> thing, MapPos x, MapPos y);

// 0x102384b8 operator new and 0x10238218 operator delete, the glue every callee namespace below names under its own spelling.
namespace online_callee {

void* OperatorNew(uint32_t size) { return IME::MslNew(size); }
void OperatorDelete(void* p) { IME::MslDelete(p); }

}  // namespace online_callee

namespace online_config_callee {
void OperatorDelete(void* p) { IME::MslDelete(p); }
}  // namespace online_config_callee

namespace slc_callee {
void OperatorDelete(void* p) { IME::MslDelete(p); }
}  // namespace slc_callee

namespace rank_callee {
void Delete(void* self) { IME::MslDelete(self); }
}  // namespace rank_callee

// 0x102386c8 memmove (the answer is not used).
namespace conn_callee {

int32_t Memmove(void* dest, const void* src, int32_t n) {
    if (n > 0) std::memmove(dest, src, static_cast<size_t>(n));
    return 0;
}

}  // namespace conn_callee

// The buddy map's tree (BuddyTree, native pointers on the host): the allocator and the runtime glue its translated members call.
namespace buddy_tree_callee {

uint32_t Strlen(const char* s) { return static_cast<uint32_t>(IME::MslStrlen(s)); }          // 0x10238650
void* NewArray(uint32_t size) { return IME::MslNewArray(size); }                             // 0x10238668
void* New(uint32_t size) { return IME::MslNew(size); }                                       // 0x102384b8
char* Strcpy(char* to, const char* from) { return IME::MslStrcpy(to, from); }                // 0x10238410
void Throw(const char* type, void* object, const void* dtor) { IME::MslThrow(type, object, dtor); }   // 0x10238680
void Delete(void* p) { IME::MslDelete(p); }                                                  // 0x10238218

// 0x10153b24 __red_black_tree::balance_insert: the buddy map's nodes start with the common node base (left, right, parent and colour).
void BalanceInsert(void* node, void* root) {
    MslRedBlackTree::balance_insert(static_cast<MslRedBlackTree::node_base*>(node), static_cast<MslRedBlackTree::node_base*>(root));
}

// 0x102229f8 std::allocator<tree node>::deallocate: the count is unused, the block goes back by operator delete.
void Deallocate(void* allocator, void* node, uint32_t count) {
    (void)allocator;
    (void)count;
    IME::MslDelete(node);
}

}  // namespace buddy_tree_callee

namespace buddy_callee {
void Throw(const char* typeName, void* object, void* destructorVector) { IME::MslThrow(typeName, object, destructorVector); }   // 0x10238680
}  // namespace buddy_callee

// 0x1021eed0 and 0x1021f000: the buddy map's value_compare, in src/game/MslSupport.cpp.
namespace buddy_erase_callee {

bool ValueCompareKeyNode(void* compare, const void* key, const void* pair) { return BuddyMapValueCompare_KeyPair(compare, key, pair); }
bool ValueCompareNodeKey(void* compare, const void* pair, const void* key) { return BuddyMapValueCompare_PairKey(compare, pair, key); }

}  // namespace buddy_erase_callee

// The login getters of the online system (src/game/online/FS_Online.cpp) and the objects COnlineControl makes on first use.
namespace online_callee {

const char16_t* LoginCustomerName(CFSOnlineSystem* sys) { return sys->GetLoginCustomerName(); }   // 0x10171834
const char16_t* LoginPassword(CFSOnlineSystem* sys) { return sys->GetLoginPassword(); }           // 0x10171878
int32_t LoginPlayerID(CFSOnlineSystem* sys) { return sys->GetLoginPlayerID(); }                   // 0x101718fc
int32_t LoginCustomerID(CFSOnlineSystem* sys) { return sys->GetLoginCustomerID(); }               // 0x1017193c
uint32_t AvatarID(CFSOnlineSystem* sys) { return sys->GetAvatarID(); }                            // 0x101719bc

void MasterSaveCtor(void* self) { new (self) CFSGlobalMasterSave(); }   // 0x101694dc, the block was granted for 0x4c bytes

// 0x1016f3c8 IPSystem::Startup and 0x1016f448 Shutdown (src/game/online/FS_Online.cpp) on the IPSystem object of the caller's stack; the
// answer is whether it failed. 0x1020ca80 IPBase's constructor (src/game/online/FTPClient.cpp) on the same four words.
void IPBaseCtor(void* self) { v51070::IPBaseCtor(static_cast<uint32_t*>(self)); }
int32_t IPSystemStartup(void* self) { return static_cast<IPSystem*>(self)->Startup() ? 1 : 0; }
void IPSystemShutdown(void* self) { static_cast<IPSystem*>(self)->Shutdown(); }

// 0x10214a0c ChatCallbackFn (src/game/online/online.cpp), the chat connection's callback.
void ChatCallbackFn(int32_t a, void* b, void* c) { ::ChatCallbackFn(a, b, c); }

// 0x1021e068 OnlineChatData's constructor (src/game/online/online.cpp) on the block inside COnlineControl.
void ChatDataCtor(void* self) { OnlineChatData::Construct(static_cast<OnlineChatData*>(self)); }

// 0x1022462c std::deque<std::vector<Argument>> tear_down (src/game/ui/UIInterface.cpp) on the deque's six words, and 0x1016985c
// std::list<std::wstring>::~list (src/game/ui/InterfaceFont.cpp) on its three words: both keep the Mac's 32-bit words, as the slots are.
void DequeTearDown(void* self) { v51126::ArgumentDequeTearDown(static_cast<MslArgumentDeque*>(self)); }
void ListDtor(void* self, int16_t flag) { v51118::WStringListDtor(static_cast<MacWStringList*>(self), flag); }

// 0x10219a10 COnlineControl::Teleport(thing, cell): the cell number minus one splits into x (7 bits) and y (the rest), each with the
// offset byte 0x80; it answers whether the thing is an online person (OnlineControlTeleportThing, 0x10219af4).
bool TeleportCell(COnlineControl* self, tag_IDX_TYPE<IDXTYPEUniqueNameThingId, uint16_t> thing,
                  tag_IDX_TYPE<IDXTYPEUniqueNameMapId, uint16_t> cell) {
    (void)self;
    const uint32_t v = static_cast<uint32_t>(cell.mIndex) - 1u;
    const MapPos x{static_cast<uint8_t>(v & 0x7f), 0x80};
    const MapPos y{static_cast<uint8_t>(v >> 7), 0x80};
    return OnlineControlTeleportThing(thing, x, y);
}

// UIInterface's online members (gUIInterface, 0x10396440): the Mac passes the char pointer of a wide string, which the members read.
void UIOnlineReceiveStr(void* ui, const char16_t* text, int32_t colour) {   // 0x101f0978
    static_cast<UIInterface*>(ui)->OnlineReceiveStr(const_cast<char16_t*>(text), colour);
}
void UIOnlineAddContact(void* ui, const char16_t* text, int32_t flag) {   // 0x101f09dc
    static_cast<UIInterface*>(ui)->OnlineAddContact(const_cast<char16_t*>(text), flag);
}
void UIOnlineRemoveContact(void* ui, const char16_t* text) {   // 0x101f0a40
    static_cast<UIInterface*>(ui)->OnlineRemoveContact(const_cast<char16_t*>(text));
}
void UIOnlineUpdateContact(void* ui, const char16_t* text, int32_t data) {   // 0x101f0aa0
    static_cast<UIInterface*>(ui)->OnlineUpdateContact(const_cast<char16_t*>(text), data);
}
void UIOnlineAddBuddy(void* ui, const char16_t* text, int32_t data) {   // 0x101f0b08
    static_cast<UIInterface*>(ui)->OnlineAddBuddy(const_cast<char16_t*>(text), data);
}
void UIOnlineRemoveBuddy(void* ui, const char16_t* text) {   // 0x101f0b68
    static_cast<UIInterface*>(ui)->OnlineRemoveBuddy(const_cast<char16_t*>(text));
}

}  // namespace online_callee

namespace response_callee {

void UIOnlineUpdateBuddy(void* ui, const char16_t* name, int32_t flags) {   // 0x101f0bc8
    static_cast<UIInterface*>(ui)->OnlineUpdateBuddy(const_cast<char16_t*>(name), flags);
}
void UIOnlineUpdateContact(void* ui, const char16_t* name, int32_t flags) {   // 0x101f0aa0
    static_cast<UIInterface*>(ui)->OnlineUpdateContact(const_cast<char16_t*>(name), flags);
}

}  // namespace response_callee

// 0x1020e898 COnlineParser::InitTokenList (src/game/online/OnlineParser.cpp), on the COnlineParser the caller holds.
namespace online_parser_callee {
void InitTokenList(void* parser) { static_cast<COnlineParser*>(parser)->InitTokenList(); }
}  // namespace online_parser_callee

// OnlineChatData's members, the Mac's words (include/online.h v51126::TreeHeader3, TreeHeader4; src/game/MslSupport.cpp; the list is
// MacWStringList, src/game/ui/InterfaceFont.cpp). The constructors take the compare functor's byte, which the Mac never reads back.
namespace chatdata_callee {

void TreeWStringCtor(void* tree) {   // 0x10223c74
    const uint8_t compare = 0;
    v51126::WStringTreeCtor(static_cast<v51126::TreeHeader3*>(tree), &compare);
}
void TreeBuddyCtor(void* tree) {   // 0x10222fe0
    const uint8_t compare = 0;
    v51126::BuddyTreeCtor(static_cast<v51126::TreeHeader4*>(tree), &compare);
}
void TreeNocaseCtor(void* tree) {   // 0x10224540
    const uint8_t compare = 0;
    v51126::NocaseTreeCtor(static_cast<v51126::TreeHeader3*>(tree), &compare);
}
void ListCtor(void* list) { v51118::WStringListCtor(static_cast<MacWStringList*>(list)); }   // 0x1016d63c
void ListClear(void* list) { WStringListClear(static_cast<MacWStringList*>(list)); }          // 0x1016d4e8

}  // namespace chatdata_callee

namespace docommand_callee {
void OnlineSystemCtor(void* self) { new (self) CFSOnlineSystem(); }   // 0x1016f7d0, the block was granted for 0x404 bytes
}  // namespace docommand_callee

// 0x1018f93c InterfaceRankTextObject(NULL), on the block the online park list panels made for 0x19c bytes.
namespace parklist_callee {
void RankCtor(void* mem) { new (mem) InterfaceRankTextObject(nullptr); }
}  // namespace parklist_callee

// ---- wire-w1b: the FTP client, the upload request and the main menu's publish path (include/FS_Online.h, ui_mainmenu.h, FS_LoadSave.h)

// The runtime glue under the spellings the FTP, Winsock and list code use: operator delete 0x10238218, free 0x10238a70, atoi 0x10238590,
// fopen 0x102389b0, fclose 0x102388c0, fwrite 0x1023a5e8 and strncpy 0x10238500 (the Mac's results that no caller reads are dropped).
namespace ftp_callee {
void OperatorDelete(void* p) { IME::MslDelete(p); }
int32_t Atoi(const char* text) { return std::atoi(text); }
void* Fopen(const char* path, const char* mode) { return std::fopen(path, mode); }
void Fclose(void* file) { std::fclose(static_cast<std::FILE*>(file)); }
void Fwrite(const void* buffer, int32_t size, int32_t count, void* file) {
    std::fwrite(buffer, static_cast<size_t>(size), static_cast<size_t>(count), static_cast<std::FILE*>(file));
}
char* Strncpy(char* to, const char* from, int32_t n) { return std::strncpy(to, from, static_cast<size_t>(n)); }

// FTPClient::ParseFileSize 0x1020c744 and _CloseDSocket 0x1020c174 (src/game/online/FTPClient.cpp): host members of the host FTPClient, which
// is the object the caller passes.
int32_t ParseFileSize(FTPClient* self, char* text) { return self->ParseFileSize(text); }
void CloseDSocket(FTPClient* self, int32_t* sock) { self->_CloseDSocket(*sock); }
}  // namespace ftp_callee

namespace v5917_callee {
void Free(void* p) { std::free(p); }              // 0x10238a70
void OperatorDelete(void* p) { IME::MslDelete(p); }   // 0x10238218
// 0x10153b24 on the nodes of std::set<unsigned short>'s tree (UshortSetTree::node starts with the common node base).
void BalanceInsert(UshortSetTree::node* node, UshortSetTree::node* root) {
    MslRedBlackTree::balance_insert(reinterpret_cast<MslRedBlackTree::node_base*>(node), reinterpret_cast<MslRedBlackTree::node_base*>(root));
}
}  // namespace v5917_callee

namespace v51116_callee {
void OperatorDelete(void* block) { IME::MslDelete(block); }   // 0x10238218
}
namespace v51052_callee {
void OperatorDelete(void* block) { IME::MslDelete(block); }   // 0x10238218
}
namespace v51126_callee {
void OperatorDelete(void* p) { IME::MslDelete(p); }   // 0x10238218
}

// The library functions v51070 (src/game/online/FTPClient.cpp, MslSupport.cpp) calls on the Mac's words: the runtime glue, 0x102109fc
// std::list<CToken>::clear (src/game/online/OnlineParser.cpp), which keeps the Mac's 32-bit words (Ptr32) like the list the caller holds,
// and the wide ostream's flush.
namespace v51070_callee {
void Delete(void* p) { IME::MslDelete(p); }                                                   // 0x10238218
void* New(uint32_t size) { return IME::MslNew(size); }                                        // 0x102384b8
void* NewArray(uint32_t size) { return IME::MslNewArray(size); }                              // 0x10238668
void Strcpy(char* to, const char* from) { IME::MslStrcpy(to, from); }                         // 0x10238410
uint32_t Strlen(const char* s) { return static_cast<uint32_t>(IME::MslStrlen(s)); }           // 0x10238650
void Throw(const char* type, void* object, const void* dtor) { IME::MslThrow(type, object, dtor); }   // 0x10238680
void CTokenListClear(void* list) { ::CTokenListClear(static_cast<CTokenList*>(list)); }       // 0x102109fc
// 0x102268f8 std::basic_ostream<wchar_t>::flush (src/game/MslSupport.cpp) on the stream the sentry's destructor (0x1021f7b0) found through
// its word: the stream starts with its ios word, an arena offset (Ptr32), which is what the translation reads.
void OstreamFlush(void* stream) { WostreamFlush(static_cast<WostreamW*>(stream)); }
}  // namespace v51070_callee

// 0x1020e2a0 Sock::Sock and 0x1020e2d4 Sock::~Sock (src/game/online/FTPClient.cpp): the Mac's layout (vtable word, socket word), which is what
// CSock and Connection hold from offset 0.
namespace csock_callee {
void SockCtor(void* self) { v51070::SockCtor(static_cast<uint32_t*>(self)); }
void SockDtor(void* self, int16_t flag) { v51070::SockDtor(static_cast<Sock*>(self), flag); }
}  // namespace csock_callee

// 0x10175748 FSOnlineUploadServerConnectionCallback (src/game/online/FS_Online.cpp), the function behind the TVector at 0x10267468; and
// 0x1017de54 CFSUserInfo::CFSUserInfo on the block the caller made with operator new.
namespace ftpreq_callee {
void UploadConnectionCallback(int32_t a, int32_t b, void* c, void* d) { ::FSOnlineUploadServerConnectionCallback(a, b, c, d); }
void UserInfoCtor(CFSUserInfo* self) { new (self) CFSUserInfo(); }
}  // namespace ftpreq_callee

// 0x101fd130 ~TbTList<TFSLoadSaveSaveFileDetails*, ...> called directly with the flag 0: it empties the list (the object is a global
// whose own destructor then finds it empty).
namespace v51056_callee {
void LoadSaveListDtor(CFSLoadSave* self) { self->RemoveAll(); }
}  // namespace v51056_callee

// The publish path of the main menu (MainGameMenuConfirmPublish_OK, src/game/ui/ui_mainmenu.cpp): CFSOnlineSystem::ReturnUploadFilename
// 0x10175608 builds the wide name in the caller's 0x40 byte temporary, UniStringToString 0x10005670 makes the narrow text whose address is
// the path of the file to upload (the Mac fills the global gOnlineUploadFile itself, whose word at 0x08 is that address; the host's global is
// only 16 bytes, so the text lives in a string of its own that the next call replaces), and 0x10238710 destroys the wide name.
namespace mainmenu_callee {

void ReturnUploadFilename(void* result, CFSOnlineSystem* system) {
    static_assert(sizeof(TbDynamicStringTemplate<char16_t>) <= 0x40, "the caller's temporary holds a wide string");
    new (result) TbDynamicStringTemplate<char16_t>(system->ReturnUploadFilename());
}

void UniStringToUploadPath(OnlineUploadFile* out, const void* wide) {
    static TbDynamicStringTemplate<char> narrow;
    UniStringToString(narrow, *static_cast<const TbDynamicStringTemplate<char16_t>*>(wide));
    out->mPath = narrow.mData;
}

void WideStringDtor(void* string, int16_t flag) {
    static_cast<TbDynamicStringTemplate<char16_t>*>(string)->~TbDynamicStringTemplate();
    if (flag > 0) IME::MslDelete(string);
}

// Slot 0x34 of the publish globe, CLBScreenComponentPublishGlobe::HandleOnlineBacktrack 0x100af094 (src/game/online/lobby_globe.cpp): the
// lobby menu's item while the publish globe shows (MainLobbyMenuCB) and the end of an upload request (PostRequestUploadCallbackFunction).
void PublishGlobeSlot34(void* screen) { static_cast<CLBScreenComponentOnlineGlobe*>(screen)->HandleOnlineBacktrack(); }

}  // namespace mainmenu_callee
