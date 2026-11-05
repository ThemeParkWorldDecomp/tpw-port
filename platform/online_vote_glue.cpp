// online_vote_glue.cpp: Vote, the vote connection of the park being visited (the Mac "online" library's Vote over VotingClient), as
// COnlineControl calls it (src/game/online/online.cpp: DoCommand's vote, case 0x15, and Update, which lets it go once the answer
// came), on platform/online_boss.cpp's vote connection (Kind::Vote, boss::Vote; PROTOCOL.md 8). Before this the launcher had
// stand-ins that did nothing, so a vote was never sent.
//
// The Vote object is the game's 0xc byte block (online_callee::OperatorNew): +0 the Mac's Client pointer (0 here), +4 "connected".
// The game's callback, which the Mac kept at +8, is kept by online_boss.cpp's connection, since a host pointer does not fit the word.
// The answer reaches the callback as Vote::internalCallback2 gives it: the code, the request's sequence word, the context, no block.
// Not in the host tests' link: it calls the game.
#include <cstdint>

#include "online.h"
#include "platform/online_boss.h"

namespace {

namespace boss = tpw::online::boss;

uint32_t& VoteWord(void* vote, size_t offset) { return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(vote) + offset); }

bool Connected(void* vote) { return vote != nullptr && VoteWord(vote, 4) != 0; }

}  // namespace

namespace docommand_callee {

// 0x1023ad38 Vote::Vote: the client pointer and the connected word.
void VoteCtor(void* self) {
    VoteWord(self, 0) = 0;
    VoteWord(self, 4) = 0;
}

// 0x1023ad50 Vote::Connect: a connection already made is let go first; 1 when the new one was started.
int32_t VoteConnect(void* vote, char* server, char* extra, uint16_t port, void (*callback)(int32_t, int32_t, void*, void*)) {
    (void)extra;
    if (vote == nullptr) return 0;
    if (Connected(vote)) online_callee::VoteRelease(vote);
    if (boss::Connect(vote, boss::Kind::Vote, server, port, callback) == 0) return 0;
    VoteWord(vote, 4) = 1;
    return 1;
}

// 0x1023ad68 Vote::SetSessionInfo, when connected.
void VoteSetSessionInfo(void* vote, int16_t kind, int32_t playerId, int32_t customerId, const char16_t* password) {
    if (!Connected(vote)) return;
    boss::SetSessionInfo(vote, static_cast<uint16_t>(kind), playerId, customerId, password);
}

// 0x1023ad80 Vote::SendVote, when connected: the GUID is the game's four words, copied as the Mac's memory holds them (big endian).
void VoteSendVote(void* vote, int32_t id, void* data, int32_t count, void* extra) {
    if (!Connected(vote) || data == nullptr) return;
    uint8_t guid[16];
    const auto* words = static_cast<const uint32_t*>(data);
    for (int i = 0; i < 4; ++i)
        for (int b = 0; b < 4; ++b) guid[4 * i + b] = static_cast<uint8_t>(words[i] >> (24 - 8 * b));
    boss::Vote(vote, id, guid, count, extra);
}

}  // namespace docommand_callee

namespace online_callee {

// 0x1023b200 Vote::Release: the connection let go of (the Mac clears the client pointer and leaves the connected word).
void VoteRelease(void* vote) {
    if (!Connected(vote)) return;
    boss::Release(vote);
    VoteWord(vote, 0) = 0;
}

// 0x1023b218 Vote::~Vote, the block freed when the flag is > 0. The connection kept by the block's address goes with it (the Mac's
// destructor left the client to Release, which the game calls first).
void VoteDtor(void* vote, int16_t flag) {
    if (vote == nullptr) return;
    boss::Release(vote);
    if (flag > 0) OperatorDelete(vote);
}

}  // namespace online_callee
