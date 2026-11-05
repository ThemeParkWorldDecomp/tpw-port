// online_containers.cpp: the online client's three name containers in COnlineControl's chat data, on the Mac's words (include/online.h
// v51126::TreeHeader3, TreeHeader4): the contact set (std::set<std::wstring>, +0xe28), the buddy map (std::map<nocase_string,
// BuddyInfo>, +0xe34) and the ignore set (std::set<nocase_string>, +0xe44). Before this the finds were stand-ins answering the end
// node and the inserts and erases did nothing, so a buddy or an ignored name never stayed and a contact leaving the park erased
// through a host std::u16string read as the Mac's string.
//
// The layout the client's own walks read (UpdateUserList, UpdateBuddyList, the list saves; ignore_callee::TreeIncrement):
//   the header: +0 the count, +4 the root (the end node is the header + 4, so the end node's left word is the root), then the first
//   node's word (+8 in a set, +0xc in the buddy map, whose +8 holds the compare functor's byte);
//   a node: +0 left, +4 right, +8 the parent with the colour in bit 0, +0xc the key (a string slot, include/platform/online_slots.h),
//   and in the buddy map +0x10 the BuddyInfo's direct byte and +0x11 its flag byte.
// Every link is an arena offset; the root's parent is the end node. The finds answer the node or the end node as a host pointer.
//
// The trees are plain binary search trees: the Metrowerks library rebalances them as red-black trees, which changes only their
// shape, not the order a walk visits (the lists are a player's buddies and the park's visitors: a few dozen names at most). The colour
// bit is always 0 here. Only the arena, the slots and the Mac's case folding are used, so the host tests link this file without the
// game.
#include <algorithm>
#include <cstring>
#include <string>

#include "online.h"
#include "platform/arena.h"
#include "platform/macdoze_system.h"
#include "platform/online_slots.h"

namespace {

enum class Compare { kNocase, kPlain };

struct Tree {
    uint8_t* header;
    uint32_t beginAt;     // the first node's word: +8 in a set, +0xc in the buddy map
    uint32_t nodeSize;    // 0x10 in a set, 0x14 in the buddy map
    Compare compare;

    uint32_t End() const { return tpw::ArenaOffset(header + 4); }
};

Tree ContactSet(void* tree) { return Tree{static_cast<uint8_t*>(tree), 8, 0x10, Compare::kPlain}; }
Tree IgnoreSet(void* tree) { return Tree{static_cast<uint8_t*>(tree), 8, 0x10, Compare::kNocase}; }
Tree BuddyMap(void* tree) { return Tree{static_cast<uint8_t*>(tree), 0xc, 0x14, Compare::kNocase}; }

uint32_t Rd(uint32_t node, uint32_t at) {
    uint32_t word;
    std::memcpy(&word, tpw::gArenaBase + node + at, 4);
    return word;
}
void Wr(uint32_t node, uint32_t at, uint32_t word) { std::memcpy(tpw::gArenaBase + node + at, &word, 4); }
uint32_t Left(uint32_t node) { return Rd(node, 0); }
uint32_t Right(uint32_t node) { return Rd(node, 4); }
uint32_t Parent(uint32_t node) { return Rd(node, 8) & ~1u; }
void SetParent(uint32_t node, uint32_t parent) { Wr(node, 8, parent); }

uint32_t HeaderWord(const Tree& tree, uint32_t at) {
    uint32_t word;
    std::memcpy(&word, tree.header + at, 4);
    return word;
}
void SetHeaderWord(const Tree& tree, uint32_t at, uint32_t word) { std::memcpy(tree.header + at, &word, 4); }

struct Key {
    const char16_t* text;
    size_t length;
};
Key KeyOf(uint32_t node) {
    const uint32_t slot = Rd(node, 0xc);
    return Key{tpw::slots::WideText(slot), tpw::slots::Length(slot)};
}
Key KeyOf(const std::u16string& text) { return Key{text.data(), text.size()}; }

// nocase_wchar_traits::compare (the Mac's case folding, NS_MacDoze::_wcsnicmp) or the plain one, then the shorter first.
bool Less(Compare compare, const Key& a, const Key& b) {
    const size_t common = std::min(a.length, b.length);
    if (compare == Compare::kNocase) {
        for (size_t i = 0; i < common; ++i) {
            const int32_t order = NS_MacDoze::_wcsnicmp(a.text + i, b.text + i, 1);
            if (order != 0) return order < 0;
        }
    } else {
        for (size_t i = 0; i < common; ++i) {
            if (a.text[i] != b.text[i]) return a.text[i] < b.text[i];
        }
    }
    return a.length < b.length;
}

// The node holding the key, or 0.
uint32_t FindNode(const Tree& tree, const Key& key) {
    uint32_t node = HeaderWord(tree, 4);
    uint32_t candidate = 0;
    while (node != 0) {
        if (!Less(tree.compare, KeyOf(node), key)) {
            candidate = node;
            node = Left(node);
        } else {
            node = Right(node);
        }
    }
    if (candidate != 0 && !Less(tree.compare, key, KeyOf(candidate))) return candidate;
    return 0;
}

void* FindAnswer(const Tree& tree, const Key& key) {
    const uint32_t node = FindNode(tree, key);
    return node != 0 ? static_cast<void*>(tpw::gArenaBase + node) : static_cast<void*>(tree.header + 4);
}

// The next node in order (ignore_callee::TreeIncrement's walk); after the last, the end node.
uint32_t Next(uint32_t node) {
    if (Right(node) != 0) {
        node = Right(node);
        while (Left(node) != 0) node = Left(node);
        return node;
    }
    for (;;) {
        const uint32_t parent = Parent(node);
        const bool cameUpLeft = Left(parent) == node;
        node = parent;
        if (cameUpLeft) return node;
    }
}

// The new node for a key not in the tree yet (its key a new slot); 0 when the key is there already (*existing is that node).
uint32_t InsertUnique(const Tree& tree, const std::u16string& text, uint32_t* existing) {
    const Key key = KeyOf(text);
    const uint32_t end = tree.End();
    uint32_t parent = end;
    uint32_t node = HeaderWord(tree, 4);
    bool left = true;
    while (node != 0) {
        parent = node;
        if (Less(tree.compare, key, KeyOf(node))) {
            left = true;
            node = Left(node);
        } else if (Less(tree.compare, KeyOf(node), key)) {
            left = false;
            node = Right(node);
        } else {
            if (existing != nullptr) *existing = node;
            return 0;
        }
    }
    uint8_t* block = static_cast<uint8_t*>(tpw::ArenaAllocZeroed(tree.nodeSize));
    if (block == nullptr) return 0;
    const uint32_t made = tpw::ArenaOffset(block);
    Wr(made, 0xc, tpw::slots::MakeWide(text.data(), text.size()));
    SetParent(made, parent);
    if (parent == end) {
        SetHeaderWord(tree, 4, made);
    } else {
        Wr(parent, left ? 0 : 4, made);
    }
    const uint32_t begin = HeaderWord(tree, tree.beginAt);
    if (begin == end || Less(tree.compare, key, KeyOf(begin))) SetHeaderWord(tree, tree.beginAt, made);
    SetHeaderWord(tree, 0, HeaderWord(tree, 0) + 1);
    return made;
}

// The link that names `from` in its parent (the end node's left word is the root) now names `to`.
void Transplant(uint32_t from, uint32_t to) {
    const uint32_t parent = Parent(from);
    Wr(parent, Left(parent) == from ? 0 : 4, to);
    if (to != 0) SetParent(to, parent);
}

void FreeNode(uint32_t node) {
    tpw::slots::Release(tpw::gArenaBase + node + 0xc);
    tpw::ArenaFree(tpw::gArenaBase + node);
}

void Erase(const Tree& tree, uint32_t node) {
    if (node == 0 || node == tree.End()) return;
    if (HeaderWord(tree, tree.beginAt) == node) SetHeaderWord(tree, tree.beginAt, Next(node));
    if (Left(node) == 0) {
        Transplant(node, Right(node));
    } else if (Right(node) == 0) {
        Transplant(node, Left(node));
    } else {
        uint32_t successor = Right(node);
        while (Left(successor) != 0) successor = Left(successor);
        if (Parent(successor) != node) {
            Transplant(successor, Right(successor));
            Wr(successor, 4, Right(node));
            SetParent(Right(successor), successor);
        }
        Transplant(node, successor);
        Wr(successor, 0, Left(node));
        SetParent(Left(successor), successor);
    }
    SetHeaderWord(tree, 0, HeaderWord(tree, 0) - 1);
    FreeNode(node);
}

void Destroy(uint32_t node) {
    while (node != 0) {
        Destroy(Right(node));
        const uint32_t left = Left(node);
        FreeNode(node);
        node = left;
    }
}

uint32_t NodeOf(const void* node) { return tpw::ArenaOffset(node); }

}  // namespace

namespace online_callee {

void* BuddyTreeFind(void* tree, const std::u16string* key) { return FindAnswer(BuddyMap(tree), KeyOf(*key)); }   // 0x1021f8d8
void* NocaseSetFind(void* tree, const std::u16string* key) { return FindAnswer(IgnoreSet(tree), KeyOf(*key)); }   // 0x1021fd4c

// 0x10222d9c insert_one: a name already there keeps its BuddyInfo.
void BuddyTreeInsert(void* tree, const BuddyKeyEntry* entry) {
    const uint32_t node = InsertUnique(BuddyMap(tree), entry->mKey, nullptr);
    if (node == 0) return;
    tpw::gArenaBase[node + 0x10] = entry->mDirect;
    tpw::gArenaBase[node + 0x11] = entry->mFlag;
}

void BuddyTreeEraseOne(void* tree, const std::u16string* key) {   // 0x1021e8e8
    const Tree buddies = BuddyMap(tree);
    Erase(buddies, FindNode(buddies, KeyOf(*key)));
}

void WStringSetInsertOne(void* tree, const std::u16string& key) { InsertUnique(ContactSet(tree), key, nullptr); }   // 0x10223a24

void WStringSetEraseOne(void* tree, const std::u16string* key) {   // 0x1021e664
    const Tree contacts = ContactSet(tree);
    Erase(contacts, FindNode(contacts, KeyOf(*key)));
}

bool NocaseWStringEqual(const std::u16string* a, const std::u16string* b) {   // 0x1021f254
    return !Less(Compare::kNocase, KeyOf(*a), KeyOf(*b)) && !Less(Compare::kNocase, KeyOf(*b), KeyOf(*a));
}

}  // namespace online_callee

namespace response_callee {

// 0x1021f130 operator==: the host's string against a slot (the player's name at COnlineControl + 8).
bool WStringEqual(const std::u16string& a, const void* b) {
    uint32_t slot;
    std::memcpy(&slot, b, 4);
    const size_t length = tpw::slots::Length(slot);
    return a.size() == length && std::equal(a.begin(), a.end(), tpw::slots::WideText(slot));
}

void* WStringSetFind(void* tree, const std::u16string* key) { return FindAnswer(ContactSet(tree), KeyOf(*key)); }   // 0x1021fae4
void NocaseSetInsertOne(void* tree, const std::u16string* key) { InsertUnique(IgnoreSet(tree), *key, nullptr); }     // 0x102243c0

// 0x1022419c erase(iterator): the node a find answered (the end node is left alone).
void NocaseSetErase(void* tree, void* node) {
    const Tree ignores = IgnoreSet(tree);
    if (node == ignores.header + 4) return;
    Erase(ignores, NodeOf(node));
}

}  // namespace response_callee

// The chat data's destructor frees each tree from its root (the header itself is the chat data's).
namespace chatdata_callee {
void TreeWStringDestroy(void* /*tree*/, uint32_t node) { Destroy(node); }   // 0x10223204
void TreeBuddyDestroy(void* /*tree*/, uint32_t node) { Destroy(node); }     // 0x10222564
void TreeNocaseDestroy(void* /*tree*/, uint32_t node) { Destroy(node); }    // 0x10223e64
}  // namespace chatdata_callee
