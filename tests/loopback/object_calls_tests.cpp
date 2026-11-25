// reagentlive loopback: the Unreal pack's object recorder (object_calls.h)
// through the agent, on a fake UObject: its table lives in this test's image
// with ProcessEvent at the stock layout's slot, and every call goes through
// the object's vptr as the engine's would.
#include "support.h"

namespace {
const uint32_t PeIndex = static_cast<uint32_t>(ue::layout::StockUE5.processEventSlot);
const int32_t ParmsAt = ue::layout::StockUE5.parmsSize;

// The block of Triple: one input, one output.
struct TripleParams { int32_t in, out; };
struct FakeFunction { alignas(8) uint8_t bytes[0x100]{}; };
FakeFunction g_triple, g_wide, g_reader;

std::atomic<int> g_calls{0};
__declspec(noinline) void FakeProcessEvent(void*, void* function, void* params) {
    ++g_calls;
    if (function == &g_triple && params) {
        auto* block = static_cast<TripleParams*>(params);
        block->out = block->in * 3;
    }
}
__declspec(noinline) int64_t Filler(void* self) { return reinterpret_cast<int64_t>(self) + 1; }

int g_marker = 1; // Stands for the RTTI locator before the table.
int g_data = 2;   // Data in the image, not code: ends the copy's run.
// [0] the slot before the table; the object points at [1]. Filled at run
// time: code up to and with ProcessEvent, then data.
void* g_table[1 + 0x4C + 2];
void* g_dataSlot[1 + 0x4C + 2]; // ProcessEvent's slot holds data.

void Fill() {
    static bool filled = false;
    if (filled) return;
    filled = true;
    REQUIRE(PeIndex + 2 < std::size(g_table));
    for (auto* table : {g_table, g_dataSlot}) {
        table[0] = &g_marker;
        for (uint32_t i = 0; i < PeIndex; ++i) table[1 + i] = reinterpret_cast<void*>(&Filler);
        table[1 + PeIndex] = reinterpret_cast<void*>(&FakeProcessEvent);
        table[2 + PeIndex] = &g_data;
    }
    g_dataSlot[1 + PeIndex] = &g_data;
    const auto parms = [](FakeFunction& function, uint16_t size) { memcpy(function.bytes + ParmsAt, &size, sizeof(size)); };
    parms(g_triple, sizeof(TripleParams));
    parms(g_wide, 2000);
    parms(g_reader, 16);
}

struct FakeObject {
    void** volatile vptr;
    uint64_t rest[7]{};
};
using ProcessEventFn = void (*)(void* self, void* function, void* params);
void ProcessEvent(FakeObject* object, void* function, void* params) {
    reinterpret_cast<ProcessEventFn>(object->vptr[PeIndex])(object, function, params);
}

Result<ue::ObjectCallsStarted> Start(Client& client, const void* object, bool dry = false) {
    for (int attempt = 0;; ++attempt) {
        Request request{};
        request.op = OpObjectCalls;
        request.address = reinterpret_cast<uint64_t>(object);
        request.args[1] = 10000;
        request.flags = dry ? ue::CallsDry : 0;
        auto response = client.Send(request);
        REQUIRE(response);
        if (response->status == Status::Ok) {
            ue::ObjectCallsStarted started{};
            REQUIRE(response->length == sizeof(started));
            memcpy(&started, response->data, sizeof(started));
            return started;
        }
        const Error error = AgentError(*response);
        if (attempt == 300 || error.reason.find("busy") == std::string::npos) return std::unexpected(error);
        Sleep(10);
    }
}

ue::ObjectCall Head(const RecorderCall& call) {
    ue::ObjectCall out{};
    REQUIRE(call.payload.size() >= sizeof(out));
    memcpy(&out, call.payload.data(), sizeof(out));
    return out;
}
}

TEST_CASE("calls-start on an object checks its request shape") {
    Request request{};
    request.op = ue::op::CallsStart;
    request.address = 0x1000;
    request.args[1] = 1000;
    request.flags = ue::CallsObject;
    CHECK(ue::Check(request) == Status::Ok);
    strcpy_s(request.text, "Fire");
    CHECK(ue::Check(request) == Status::BadRequest);  // An object target names no function.
    request.text[0] = 0;
    request.length = 8;
    CHECK(ue::Check(request) == Status::BadRequest);  // Nor a filter: the object is the filter.
    request.length = 0;
    request.flags = 4;
    CHECK(ue::Check(request) == Status::BadRequest);
}

TEST_CASE("an object's ProcessEvent recording keeps each function and its block, only for that object") {
    Fill();
    Client client = Attach();
    auto object = std::make_unique<FakeObject>();
    object->vptr = g_table + 1;
    auto other = std::make_unique<FakeObject>();
    other->vptr = g_table + 1;

    auto dry = Start(client, object.get(), true);
    REQUIRE(dry);
    CHECK(object->vptr == g_table + 1); // Nothing written.
    CHECK(dry->entry == PeIndex);
    CHECK(dry->original == reinterpret_cast<uint64_t>(&FakeProcessEvent));

    const auto run = Start(client, object.get());
    REQUIRE(run);
    CHECK(object->vptr != g_table + 1);  // The private copy.
    CHECK(g_table[1 + PeIndex] == reinterpret_cast<void*>(&FakeProcessEvent)); // The table untouched.
    CHECK(run->table == reinterpret_cast<uint64_t>(g_table + 1));
    CHECK(run->object == reinterpret_cast<uint64_t>(object.get()));

    const int before = g_calls;
    TripleParams triple{7, 0};
    ProcessEvent(object.get(), &g_triple, &triple);
    CHECK(triple.out == 21);              // Forwarded.
    std::vector<uint8_t> wide(2000, 0x5A);
    ProcessEvent(object.get(), &g_wide, wide.data());
    void* page = VirtualAlloc(nullptr, 4096, MEM_RESERVE, PAGE_NOACCESS);
    REQUIRE(page);
    ProcessEvent(object.get(), &g_reader, page);
    // The same table, another object: never through the copy, never recorded.
    TripleParams elsewhere{1, 0};
    ProcessEvent(other.get(), &g_triple, &elsewhere);
    CHECK(elsewhere.out == 3);
    // Through the copy with another self: forwarded, not recorded.
    reinterpret_cast<ProcessEventFn>(object->vptr[PeIndex])(other.get(), &g_triple, &elsewhere);
    CHECK(g_calls == before + 5);

    auto read = client.RecorderRead(run->slot);
    REQUIRE(read);
    CHECK(read->status.kind == ue::ObjectCallsKind);
    CHECK(read->status.target == reinterpret_cast<uint64_t>(object.get()));
    CHECK(read->status.counts[ue::ObjectTruncatedCount] == 1);
    CHECK(read->status.counts[ue::ObjectFaultedCount] == 1);
    REQUIRE(read->calls.size() == 3);
    const auto& first = read->calls[0];
    CHECK(first.row.object == reinterpret_cast<uint64_t>(object.get()));
    CHECK(Head(first).function == reinterpret_cast<uint64_t>(&g_triple));
    CHECK(first.row.tag == 0);
    REQUIRE(first.payload.size() == sizeof(ue::ObjectCall) + sizeof(TripleParams));
    TripleParams copied{};
    memcpy(&copied, first.payload.data() + sizeof(ue::ObjectCall), sizeof(copied));
    CHECK((copied.in == 7 && copied.out == 21)); // Read after the call: the output is there.
    const auto& second = read->calls[1];
    CHECK(Head(second).function == reinterpret_cast<uint64_t>(&g_wide));
    CHECK(second.row.tag == ue::ObjectCallTruncated);
    CHECK(second.payload.size() == MaxRecorderPayload);
    CHECK(second.payload.back() == 0x5A);
    const auto& third = read->calls[2];
    CHECK(Head(third).function == reinterpret_cast<uint64_t>(&g_reader));
    CHECK(third.row.tag == ue::ObjectCallFaulted);
    CHECK(third.payload.size() == sizeof(ue::ObjectCall));

    // Recorded already: a second start refuses.
    auto twice = Start(client, object.get());
    REQUIRE_FALSE(twice);
    CHECK(twice.error().reason.find("already recorded") != std::string::npos);

    auto stopped = client.RecorderStop(run->slot);
    REQUIRE(stopped);
    CHECK(stopped->state == static_cast<uint32_t>(RecorderState::Finished));
    CHECK(object->vptr == g_table + 1);  // The entry given back, and with it the vptr.
    ProcessEvent(object.get(), &g_triple, &triple);
    CHECK(triple.out == 21);
    VirtualFree(page, 0, MEM_RELEASE);
}

TEST_CASE("an object recorder refuses a ProcessEvent slot that is not code and a vptr outside an image") {
    Fill();
    Client client = Attach();
    auto data = std::make_unique<FakeObject>();
    data->vptr = g_dataSlot + 1;
    auto notCode = Start(client, data.get());
    REQUIRE_FALSE(notCode);
    CHECK(notCode.error().reason.find("not code") != std::string::npos);
    CHECK(data->vptr == g_dataSlot + 1);

    // A table on the heap (another owner's copy): its entries are code, the table is not in an image.
    std::vector<void*> heap(std::begin(g_table), std::end(g_table));
    auto foreign = std::make_unique<FakeObject>();
    foreign->vptr = heap.data() + 1;
    auto refused = Start(client, foreign.get());
    REQUIRE_FALSE(refused);
    CHECK(refused.error().reason.find("outside a loaded module") != std::string::npos);
    CHECK(foreign->vptr == heap.data() + 1);

    auto unreadable = Start(client, reinterpret_cast<void*>(0x10));
    REQUIRE_FALSE(unreadable);
    CHECK(unreadable.error().reason.find("cannot be read") != std::string::npos);
}
