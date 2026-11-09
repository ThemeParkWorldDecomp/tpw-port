// online_chat_glue.cpp: Chat, the chat connection of the park being visited (the Mac "online" library's Chat over ChatClient), as the
// callees COnlineControl (src/game/online/online.cpp: InitClient, InitGameWorld, ProcessInitResponse, DoCommand, the moves and the
// ignore list) call it, on platform/online_boss.cpp's chat connection (ChatConnect, ChatRequest). Before this the launcher had
// stand-ins that did nothing, so a visit never left "connecting".
//
// The Chat object is the game's 0xc byte block (online_callee::OperatorNew): +0 the Mac's Client pointer (0 here), +4 "connected".
// The game's callback, which the Mac kept at +8, is kept here by the block's address, since a host pointer does not fit the word.
// The callees that take the Chat as a word map it back with online_callee::HandleToPointer, as COnlineControl stores it.
//
// The strings the callees are given are 32-bit slots (include/platform/online_slots.h); a word that does not name a representation in
// the arena reads as the empty string and is logged. Each reply or push is handed to the game as Chat::internalCallback2 does: a
// vector of Argument, the first {"", "", event}, then {text, "", atoi(text)} for each narrow argument and {"", text, 0} for each wide
// one, given to the callback with the code 100 and the request's context; -4 and -5 come with an empty vector.
// The lobby's satellites use the same connection with no login (chat_satellite_callee, ProcessGetChatSatellitePlayers).
// Not in the host tests' link: it calls the game. Only tpw_host_chat_satellites links it, whole, as the launcher does.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "InterfaceWindow.h"
#include "email.h"
#include "online.h"
#include "platform.h"
#include "platform/arena.h"
#include "platform/online_boss.h"
#include "platform/online_slots.h"
#include "std_basic_filebuf.h"

namespace {

namespace boss = tpw::online::boss;
using boss::ChatToken;
using GameCallback = void (*)(int32_t, void*, void*);

std::map<const void*, GameCallback>& Callbacks() {
    static std::map<const void*, GameCallback> callbacks;
    return callbacks;
}

void* ChatOf(uint32_t word) { return online_callee::HandleToPointer(word); }

uint32_t& ChatWord(void* chat, size_t offset) { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(chat) + offset); }

// The wide text of the slot at `slot`.
ChatToken Wide(const void* slot) {
    uint32_t word = 0;
    if (slot != nullptr) std::memcpy(&word, slot, sizeof word);
    if (word == 0 || !tpw::InArena(tpw::gArenaBase + word)) {
        if (word != 0) tpw::Log("online: chat: a string word (0x%08x) that names no representation, sent as empty", word);
        return ChatToken::Wide({});
    }
    return ChatToken::Wide(std::u16string(tpw::slots::WideText(word), tpw::slots::Length(word)));
}

ChatToken Narrow(const void* slot) {
    uint32_t word = 0;
    if (slot != nullptr) std::memcpy(&word, slot, sizeof word);
    if (word == 0 || !tpw::InArena(tpw::gArenaBase + word)) {
        if (word != 0) tpw::Log("online: chat: a string word (0x%08x) that names no representation, sent as empty", word);
        return ChatToken::Narrow({});
    }
    return ChatToken::Narrow(std::string(tpw::slots::NarrowText(word), tpw::slots::Length(word)));
}

void Send(void* chat, uint32_t command, const std::vector<ChatToken>& tokens, void* context) {
    if (chat == nullptr || ChatWord(chat, 4) == 0) return;   // Chat's members do nothing before Connect
    if (boss::ChatRequest(chat, command, tokens, context) == 0)
        tpw::Log("online: chat: command 0x%x not sent: the connection is gone", command);
}

void SendWord(uint32_t chat, uint32_t command, const std::vector<ChatToken>& tokens, void* context) {
    Send(ChatOf(chat), command, tokens, context);
}

// Chat::internalCallback2's vector, given to the game's callback, then torn down. The Argument copies push_back makes are the
// translated library's strings; the ones made here are given up after it.
void ToGame(const void* chat, int32_t code, int32_t event, const std::vector<ChatToken>& tokens, void* context) {
    auto it = Callbacks().find(chat);
    if (it == Callbacks().end()) return;
    const GameCallback callback = it->second;
    alignas(ArgumentVector) unsigned char raw[sizeof(ArgumentVector)] = {};   // the vector's three words, empty
    ArgumentVector* vector = reinterpret_cast<ArgumentVector*>(raw);
    auto add = [&](uint32_t narrow, uint32_t wide, int32_t command) {
        Argument argument;
        argument.mString = narrow;
        argument.mWString = wide;
        argument.mCommand = static_cast<uint32_t>(command);
        vector->push_back(argument);
        tpw::slots::Release(&argument.mString);
        tpw::slots::Release(&argument.mWString);
    };
    if (code == 100) {
        add(tpw::slots::MakeNarrow(nullptr, 0), tpw::slots::MakeWide(nullptr, 0), event);
        for (const ChatToken& t : tokens) {
            if (t.wide)
                add(tpw::slots::MakeNarrow(nullptr, 0), tpw::slots::MakeWide(t.text.data(), t.text.size()), 0);
            else
                add(tpw::slots::MakeNarrow(t.narrow.data(), t.narrow.size()), tpw::slots::MakeWide(nullptr, 0), std::atoi(t.narrow.c_str()));
        }
    }
    callback(code, context, vector);
    online_callee::ArgumentVectorTearDown(vector);
}

}  // namespace

namespace online_callee {

// 0x1023a9f0 Chat::Chat: the client pointer and the connected word.
void ChatCtor(void* self) {
    ChatWord(self, 0) = 0;
    ChatWord(self, 4) = 0;
}

// 0x1023aa08 Chat::Connect: a connection already made is let go first; 1 when the new one was started.
int32_t ChatConnect(void* chat, const char* server, uint16_t port, void (*callback)(int32_t, void*, void*), char* extra) {
    (void)extra;
    if (chat == nullptr) return 0;
    if (ChatWord(chat, 4) != 0) ChatRelease(chat);
    if (boss::ChatConnect(chat, server, port, ToGame) == 0) return 0;
    Callbacks()[chat] = callback;
    ChatWord(chat, 4) = 1;
    return 1;
}

}  // namespace online_callee

namespace chat_satellite_callee {

// 0x1023aa08 Chat::Connect, as the lobby's satellites call it: the callback takes the vector by reference, which is the same pointer.
int32_t ChatConnect(void* chat, const char* server, uint16_t port, ChatCallback callback, char* extra) {
    return online_callee::ChatConnect(chat, server, port, reinterpret_cast<GameCallback>(callback), extra);
}

// 0x1023aa38 Chat::RoomInfo: command 7 with the room as a narrow argument. The room is made by outbox_callee::StringCtor
// (platform/postcard_dispatch.cpp), which writes only the first word of the string: the arena offset of a representation with the
// length at +0 and its characters' arena offset at +0xc (and not the native pointer at +0x10 that tpw::slots reads).
void ChatRoomInfo(void* chat, const MslCharString& room, void* context) {
    uint32_t word = 0;
    std::memcpy(&word, &room, sizeof word);
    if (word == 0 || !tpw::InArena(tpw::gArenaBase + word)) {
        tpw::Log("online: chat: a room word (0x%08x) that names no representation, not sent", word);
        return;
    }
    uint32_t length = 0, chars = 0;
    std::memcpy(&length, tpw::gArenaBase + word, sizeof length);
    std::memcpy(&chars, tpw::gArenaBase + word + 0xc, sizeof chars);
    Send(chat, 7, {ChatToken::Narrow(std::string(reinterpret_cast<const char*>(tpw::gArenaBase + chars), length))}, context);
}

}  // namespace chat_satellite_callee

namespace online_callee {

// 0x1023aa50 Chat::Release: the connection let go of (the Mac clears the client pointer and leaves the connected word).
void ChatRelease(void* chat) {
    if (chat == nullptr || ChatWord(chat, 4) == 0) return;
    boss::Release(chat);
    Callbacks().erase(chat);
    ChatWord(chat, 0) = 0;
}

// 0x1023b320 Chat::~Chat, the block freed when the flag is > 0. What is kept here by the block's address goes with it.
void ChatDtor(void* chat, int16_t flag) {
    if (chat == nullptr) return;
    boss::Release(chat);
    Callbacks().erase(chat);
    if (flag > 0) OperatorDelete(chat);
}

// 0x1023b0c8 ChatClient::CreatePlayer (0): the name, the customer id, the password, the player id.
void ChatCreatePlayer(void* chat, const void* name, int32_t customerId, int32_t playerId, const void* password, void* context) {
    Send(chat, 0, {Wide(name), ChatToken::Number(customerId), Wide(password), ChatToken::Number(playerId)}, context);
}

// 0x1023ac18 ChatClient::SetHearingRange (2), printed with %d.
void ChatSetHearingRange(void* chat, uint32_t range, void* context) {
    Send(chat, 2, {ChatToken::Number(static_cast<int32_t>(range))}, context);
}

void ChatEnumPlayers(void* chat, void* context) { Send(chat, 8, {}, context); }   // 0x1023b158

// 0x10239178 ChatClient::MovePlayer (4): x, y and the mode.
void ChatMovePlayer(void* chat, int32_t x, int32_t y, int32_t mode, void* context) {
    Send(chat, 4, {ChatToken::Number(x), ChatToken::Number(y), ChatToken::Number(mode)}, context);
}

// The wide string library on the host's std::u16string objects (include/online.h): the object is constructed already, and its own
// destructor frees it, so the library's destructor only empties it.
void WStringCtorFrom(std::u16string* self, const char16_t* text) {   // 0x10146a14
    if (text != nullptr) self->assign(text);
    else self->clear();
}
void WStringCopy(std::u16string* self, const std::u16string* other, uint32_t pos, uint32_t count) {   // 0x101f7424
    self->assign(*other, std::min<size_t>(pos, other->size()), count);
}
void WStringDtor(std::u16string* self, int16_t /*flag*/) { self->clear(); }   // 0x101468f8
// The case-insensitive string is the same characters (only its comparisons differ).
void NocaseWStringCopy(std::u16string* self, const std::u16string* other, uint32_t pos, uint32_t count) {   // 0x1022213c
    WStringCopy(self, other, pos, count);
}
void NocaseWStringDtor(std::u16string* self, int16_t flag) { WStringDtor(self, flag); }   // 0x10222020
void NocaseWStringCopy(std::u16string* self, const uint32_t* slot, uint32_t pos, uint32_t count) {   // 0x1022213c
    const std::u16string other(tpw::slots::WideText(*slot), tpw::slots::Length(*slot));
    WStringCopy(self, &other, pos, count);
}

}  // namespace online_callee

// The answers' strings: an Argument's wide string (its slot at +4) copied whole into the host's object.
namespace response_callee {
void ArgumentBStrVal(std::u16string* result, const Argument* argument) {   // 0x10217764
    result->assign(tpw::slots::WideText(argument->mWString), tpw::slots::Length(argument->mWString));
}
}  // namespace response_callee

namespace init_callee {

void ChatSetPlayerData(uint32_t chat, int32_t avatar, void* context) { SendWord(chat, 1, {ChatToken::Number(avatar)}, context); }   // 0x1023b0f8

// 0x1023b110 ChatClient::CreateRoom (5): the park's name, the topic, the city's name, the park id (narrow) and the size.
void ChatCreateRoom(uint32_t chat, const void* name, const void* topic, const void* city, const void* password, int32_t size, void* context) {
    SendWord(chat, 5, {Wide(name), Wide(topic), Wide(city), Narrow(password), ChatToken::Number(size)}, context);
}

}  // namespace init_callee

namespace ignore_callee {
void ChatIgnore(uint32_t chat, const IgnoreWString* name, COnlineControl* context) { SendWord(chat, 0xd, {Wide(name)}, context); }   // 0x1023ac78
}  // namespace ignore_callee

// DoCommand's: ChatClient::Say (0x11, 0x12 when the flag is set), Tell (0x13, 0x14), RoomShout (0x16, 0x17) and the commands of one
// name or none.
namespace docommand_callee {

void ChatNotAFK(uint32_t chat, void* context) { SendWord(chat, 0x10, {}, context); }
void ChatSay(uint32_t chat, const void* text, bool flag, void* context) { SendWord(chat, flag ? 0x12 : 0x11, {Wide(text)}, context); }
void ChatTell(uint32_t chat, const void* name, const void* text, bool flag, void* context) {
    SendWord(chat, flag ? 0x14 : 0x13, {Wide(name), Wide(text)}, context);
}
void ChatRoomShout(uint32_t chat, const void* text, bool flag, void* context) { SendWord(chat, flag ? 0x17 : 0x16, {Wide(text)}, context); }
void ChatEarmuffsOff(uint32_t chat, void* context) { SendWord(chat, 0xc, {}, context); }
void ChatEarmuffsOn(uint32_t chat, void* context) { SendWord(chat, 0xb, {}, context); }
void ChatUnIgnore(uint32_t chat, const void* name, void* context) { SendWord(chat, 0xe, {Wide(name)}, context); }
void ChatBlackmark(uint32_t chat, const void* name, void* context) { SendWord(chat, 0x21, {Wide(name)}, context); }
void ChatRemoveBuddy(uint32_t chat, const void* name, void* context) { SendWord(chat, 10, {Wide(name)}, context); }
void ChatAddBuddy(uint32_t chat, const void* name, void* context) { SendWord(chat, 9, {Wide(name)}, context); }
void ChatPage(uint32_t chat, const void* name, void* context) { SendWord(chat, 0x1f, {Wide(name)}, context); }
void ChatAFK(uint32_t chat, void* context) { SendWord(chat, 0xf, {}, context); }
void ChatGoto(uint32_t chat, const void* name, void* context) { SendWord(chat, 0x52, {Wide(name)}, context); }
void ChatLocate(uint32_t chat, const void* name, void* context) { SendWord(chat, 0x20, {Wide(name)}, context); }

}  // namespace docommand_callee
