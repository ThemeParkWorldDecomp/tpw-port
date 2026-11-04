// fuzz_boss_replies.cpp: libFuzzer target for what the online server client (platform/online_boss.cpp) does with the server's
// bytes. The client's file is compiled in here, so its internal readers are reached without a thread or a socket. The first byte
// picks the path:
// - 0: one reply (type, status, body) given to each owner's answer (Auth, News, City with its rowset, Upload); the callback reads
//      every block it is given (the account block, a text context, a list's infos and their strings);
// - 1: a byte stream read by ReadSome through a fake connection in chunks the input sizes, as replies to requests the input
//      numbers, then each reply read given to the owners' answers as above;
// - 2: a server details text (type 7) read by NoteAdvertised, then the host each kind of connection would go to.
// The lists given are let go of and freed after each input, so that a leak shows.
#include "../../online_boss.cpp"

namespace {

using namespace tpw::online::boss;
namespace boss = tpw::online::boss;
using Bytes = std::vector<uint8_t>;

volatile uint32_t gSink = 0;

void Touch(const void* bytes, size_t size) {
    const auto* p = static_cast<const uint8_t*>(bytes);
    uint32_t sum = 0;
    for (size_t i = 0; i < size; ++i) sum += p[i];
    gSink = gSink + sum;
}

// The list each sub-id gives, its stride and its string words (arena offsets), so that the callback can read them all.
struct ListShape {
    int32_t code;
    size_t stride;
    std::vector<size_t> strings;
};

const ListShape* ShapeOf(int32_t code) {
    static const ListShape kShapes[] = {
        {2, 0x30, {0x04, 0x08, 0x1c}},
        {3, 0x66, {0x04, 0x08, 0x0c, 0x44, 0x4c}},
        {4, 0x26, {0x08, 0x0c, 0x16, 0x1a, 0x22}},
        {5, 0x10, {0x04, 0x08, 0x0c}},
        {6, 0x34, {0x04, 0x08, 0x24}},
        {9, 8, {0x00, 0x04}},
    };
    for (const ListShape& s : kShapes)
        if (s.code == code) return &s;
    return nullptr;
}

void TouchArenaString(uint32_t offset) {
    if (offset == 0) return;
    const uint8_t* p = tpw::ArenaBase() + offset;
    Touch(p, tpw::ArenaSize(p));
}

// Reads whatever the client gives with the code; the context names the owner's kind.
void Callback(int32_t code, int32_t, void* context, void* block) {
    const auto kind = static_cast<boss::Kind>(reinterpret_cast<uintptr_t>(context));
    if (block == nullptr) return;
    if (kind == boss::Kind::Auth && code == 0x3c) {
        Touch(block, 0x6c);
    } else if ((kind == boss::Kind::News && (code == 0x32 || code == 0x33)) || (kind == boss::Kind::Upload && code == 0x50)) {
        const auto* c = static_cast<const OnlineUploadContext*>(block);
        if (c->mBuffer != nullptr) Touch(c->mBuffer, std::strlen(static_cast<const char*>(c->mBuffer)));
    } else if (kind == boss::Kind::City) {
        const ListShape* shape = ShapeOf(code);
        if (shape == nullptr) __builtin_trap();   // a block with a code that gives none
        // Every list has the same layout whatever its infos' type.
        const auto* list = static_cast<const ccReceivedList<uint8_t>*>(block);
        const auto* infos = reinterpret_cast<const uint8_t*>(list->mInfos);
        if (list->mCount < 0) __builtin_trap();
        Touch(infos, static_cast<size_t>(list->mCount) * shape->stride);
        for (int32_t i = 0; i < list->mCount; ++i)
            for (size_t at : shape->strings) {
                uint32_t word = 0;
                std::memcpy(&word, infos + static_cast<size_t>(i) * shape->stride + at, 4);
                TouchArenaString(word);
            }
    }
}

void AnswerAll(const Reply& r) {
    for (int k = 0; k < 4; ++k) {
        Owner o;
        o.kind = static_cast<boss::Kind>(k);
        o.callback = Callback;
        void* context = reinterpret_cast<void*>(static_cast<uintptr_t>(k));
        switch (o.kind) {
        case boss::Kind::Auth: AuthReply(o, r, context); break;
        case boss::Kind::News: NewsReply(o, r, context); break;
        case boss::Kind::City: CityAnswer(o, r, context); break;
        case boss::Kind::Upload: UploadReply(o, r, context); break;
        }
    }
}

uint16_t Take16(const uint8_t*& data, size_t& size) {
    uint16_t v = 0;
    if (size >= 2) {
        v = static_cast<uint16_t>(data[0] << 8 | data[1]);
        data += 2;
        size -= 2;
    }
    return v;
}

// A connection that gives the stream in the chunk sizes the input names, then closes.
class FakeTransport final : public Transport {
public:
    FakeTransport(const uint8_t* bytes, size_t size, uint8_t chunk) : mBytes(bytes), mSize(size), mChunk(chunk) {}
    bool Open(const std::string&, uint16_t, const std::atomic<bool>&) override { return true; }
    int Wait(bool, int) override { return 1; }
    long Send(const uint8_t*, size_t size) override { return static_cast<long>(size); }
    long Receive(uint8_t* bytes, size_t size) override {
        if (mAt == mSize) return 0;
        size_t n = std::min({size, mSize - mAt, static_cast<size_t>(mChunk == 0 ? 1 : mChunk)});
        std::memcpy(bytes, mBytes + mAt, n);
        mAt += n;
        return static_cast<long>(n);
    }
    void Close() override {}

private:
    const uint8_t* mBytes;
    size_t mSize;
    size_t mAt = 0;
    uint8_t mChunk;
};

void ReadStream(const uint8_t* data, size_t size) {
    if (size < 2) return;
    const uint8_t chunk = data[0];
    const uint8_t requests = data[1] % 8;
    data += 2;
    size -= 2;
    Line line;
    line.host = "fuzz";
    for (uint8_t i = 0; i < requests; ++i) line.infos.push_back(Info{10u + i, 4, 0, 0});
    FakeTransport t(data, size, chunk);
    Reading reading;
    for (int steps = 0; steps < 100000 && ReadSome(line, t, reading); ++steps) {}
    for (const Reply& r : line.replies) AnswerAll(r);
}

void LetGo() {
    for (auto& held : tpw::online::lists::HeldLists()) *held.refCount = 1;
    tpw::online::lists::Sweep();
    Advertised().clear();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;
    const uint8_t mode = data[0] % 3;
    ++data;
    --size;
    if (mode == 0) {
        Reply r;
        r.type = Take16(data, size);
        r.status = Take16(data, size);
        r.seq = 10;
        r.body.assign(data, data + size);
        AnswerAll(r);
    } else if (mode == 1) {
        ReadStream(data, size);
    } else {
        NoteAdvertised(data, size);
        for (int k = 0; k < 4; ++k) gSink = gSink + static_cast<uint32_t>(HostFor(static_cast<boss::Kind>(k), "fuzz.host").size());
    }
    LetGo();
    return 0;
}
