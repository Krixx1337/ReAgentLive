// reagentlive loopback: the native call recorder (NativeCallsStart) through
// the agent, on fixture functions in this test's image: called through a
// vtable entry or a heap cell, each forwarded exactly by the generic thunk.
#include "support.h"
#include "reagentlive/core/vtable_copy.h"

namespace {
// Fixture functions: never inlined, distinct bodies (no identical folding).
struct Big { int64_t a, b, c, d; };
using IntFn = int64_t (*)(void* self, int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f);
using FloatFn = double (*)(void* self, double a, float b, double c, double d, float e);
using BigFn = Big (*)(void* self, int64_t seed);
using HalfFn = float (*)(float value);
using PeekFn = int64_t (*)(void* self, const uint8_t* bytes);

__declspec(noinline) int64_t Mix(void* self, int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f) {
    return reinterpret_cast<int64_t>(self) + a * 2 + b * 3 + c * 5 + d * 7 + e * 11 + f * 13;
}
__declspec(noinline) int64_t Other(void*, int64_t a, int64_t, int64_t, int64_t, int64_t, int64_t) { return -a; }
__declspec(noinline) double FMix(void*, double a, float b, double c, double d, float e) {
    return a + b * 2 + c * 4 + d * 8 + e * 16;
}
__declspec(noinline) Big MakeBig(void*, int64_t seed) { return {seed, seed + 1, seed * 2, seed * 3}; }
__declspec(noinline) float Half(float value) { return value / 2; }
__declspec(noinline) int64_t Sum(void*, const uint8_t* bytes) {
    int64_t sum = 0;
    for (int i = 0; i < 16; ++i) sum += bytes[i];
    return sum;
}

// A polymorphic object whose table lives in this image's read-only data.
struct Shape {
    virtual ~Shape() = default;
    virtual int64_t Area(int64_t scale, int64_t a, int64_t b, int64_t c, int64_t d) const;
    virtual Big Corners(int64_t seed) const;
    virtual double Length(double a, float b, double c, double d, float e) const;
    int64_t side = 3;
};
__declspec(noinline) int64_t Shape::Area(int64_t scale, int64_t a, int64_t b, int64_t c, int64_t d) const {
    return side * side * scale + a + b * 10 + c * 100 + d * 1000;
}
__declspec(noinline) Big Shape::Corners(int64_t seed) const { return {seed, side, seed + side, seed * side}; }
__declspec(noinline) double Shape::Length(double a, float b, double c, double d, float e) const {
    return side * (a + b + c + d + e);
}
// Vtable entries (MSVC: the destructor is entry 0).
constexpr uint32_t AreaEntry = 1, CornersEntry = 2, LengthEntry = 3;

// A heap cell holding a code pointer, as an object's field would.
template<class Fn> struct Cell { Fn volatile fn; };

NativeCallsSpec CellSpec(const void* cell, uint32_t milliseconds = 10000) {
    NativeCallsSpec spec{};
    spec.address = reinterpret_cast<uint64_t>(cell);
    spec.target = static_cast<uint32_t>(NativeTarget::Cell);
    spec.milliseconds = milliseconds;
    return spec;
}
NativeCallsSpec VtableSpec(const void* object, uint32_t entry, uint32_t milliseconds = 10000) {
    NativeCallsSpec spec{};
    spec.address = reinterpret_cast<uint64_t>(object);
    spec.target = static_cast<uint32_t>(NativeTarget::Vtable);
    spec.index = entry;
    spec.milliseconds = milliseconds;
    return spec;
}

// Starts, waiting out a slot's reuse grace when every slot is just stopped.
Result<NativeCallsStarted> Start(Client& client, const NativeCallsSpec& spec) {
    for (int attempt = 0;; ++attempt) {
        auto started = client.NativeCallsStart(spec);
        if (started || attempt == 300 || started.error().reason.find("busy") == std::string::npos) return started;
        Sleep(10);
    }
}

NativeCall Registers(const RecorderCall& call) {
    NativeCall out{};
    REQUIRE(call.payload.size() >= sizeof(out));
    memcpy(&out, call.payload.data(), sizeof(out));
    return out;
}
uint64_t Bits(double value) { uint64_t out; memcpy(&out, &value, sizeof(out)); return out; }
uint32_t Bits(float value) { uint32_t out; memcpy(&out, &value, sizeof(out)); return out; }
}

TEST_CASE("native-calls-start specs are checked before the agent reads anything") {
    alignas(8) uint64_t cell = 0;
    NativeCallsSpec spec = CellSpec(&cell);
    CHECK(NativeCallsSpecValid(spec));
    spec.address += 4;
    CHECK_FALSE(NativeCallsSpecValid(spec));            // A cell is 8-byte aligned.
    spec = CellSpec(&cell);
    spec.index = 1;
    CHECK_FALSE(NativeCallsSpecValid(spec));            // A cell has no entry.
    spec = VtableSpec(&cell, 3);
    spec.offset = 4;
    CHECK_FALSE(NativeCallsSpecValid(spec));
    spec.offset = 8;
    CHECK(NativeCallsSpecValid(spec));
    spec.copyRegister = static_cast<uint32_t>(NativeRegister::Rdx);
    CHECK_FALSE(NativeCallsSpecValid(spec));            // A register needs a length.
    spec.copyBytes = MaxNativeCopy + 1;
    CHECK_FALSE(NativeCallsSpecValid(spec));
    spec.copyBytes = MaxNativeCopy;
    CHECK(NativeCallsSpecValid(spec));
    spec.milliseconds = MaxRecorderMilliseconds + 1;
    CHECK_FALSE(NativeCallsSpecValid(spec));
    Request request{};
    request.op = op::RecorderRead;
    request.args[0] = RecorderLatest;
    CHECK(Check(request) == Status::Ok);
    request.args[0] = RecorderLatest + 1;
    CHECK(Check(request) == Status::BadRequest);
    request = {};
    request.op = op::RecorderStop;
    request.address = 0x1000;
    CHECK(Check(request) == Status::BadRequest);
}

TEST_CASE("a cell recording forwards int, stack, float, struct and float-return calls exactly") {
    Client client = Attach();
    auto* ints = new Cell<IntFn>{&Mix};
    auto* floats = new Cell<FloatFn>{&FMix};
    auto* bigs = new Cell<BigFn>{&MakeBig};
    auto* halves = new Cell<HalfFn>{&Half};
    const auto intRun = Start(client, CellSpec(ints));
    const auto floatRun = Start(client, CellSpec(floats));
    const auto bigRun = Start(client, CellSpec(bigs));
    const auto halfRun = Start(client, CellSpec(halves));
    REQUIRE(intRun);
    REQUIRE(floatRun);
    REQUIRE(bigRun);
    REQUIRE(halfRun);
    CHECK(reinterpret_cast<uint64_t>(ints->fn) == intRun->thunk);
    CHECK(intRun->original == reinterpret_cast<uint64_t>(&Mix));
    int self = 0;
    CHECK(ints->fn(&self, 1, 2, 3, 4, 5, 6) == Mix(&self, 1, 2, 3, 4, 5, 6));
    CHECK(floats->fn(&self, 1.5, 2.25f, 3.5, 4.75, 5.5f) == FMix(&self, 1.5, 2.25f, 3.5, 4.75, 5.5f));
    const Big big = bigs->fn(&self, 7);
    CHECK((big.a == 7 && big.b == 8 && big.c == 14 && big.d == 21));
    CHECK(halves->fn(9.0f) == 4.5f);

    auto read = client.RecorderRead(intRun->slot);
    REQUIRE(read);
    CHECK(read->status.kind == NativeCallsKind);
    CHECK(read->status.target == reinterpret_cast<uint64_t>(ints));
    REQUIRE(read->calls.size() == 1);
    const auto& row = read->calls[0].row;
    CHECK(row.object == reinterpret_cast<uint64_t>(&self));
    CHECK(row.thread == GetCurrentThreadId());
    CHECK(row.microseconds >= 0);
    CHECK(row.microseconds < 10'000'000);
    const NativeCall call = Registers(read->calls[0]);
    CHECK(call.args[0] == reinterpret_cast<uint64_t>(&self));
    CHECK((call.args[1] == 1 && call.args[2] == 2 && call.args[3] == 3));
    CHECK(call.returnAddress != 0);
    CHECK(vtable::InCode(reinterpret_cast<void*>(call.returnAddress))); // Back into this test.

    read = client.RecorderRead(floatRun->slot);
    REQUIRE(read);
    REQUIRE(read->calls.size() == 1);
    const NativeCall floatCall = Registers(read->calls[0]);
    CHECK(floatCall.xmm[1] == Bits(1.5));
    CHECK(static_cast<uint32_t>(floatCall.xmm[2]) == Bits(2.25f));
    CHECK(floatCall.xmm[3] == Bits(3.5));

    read = client.RecorderRead(bigRun->slot);
    REQUIRE(read);
    REQUIRE(read->calls.size() == 1);
    const NativeCall bigCall = Registers(read->calls[0]);
    CHECK(bigCall.args[1] == reinterpret_cast<uint64_t>(&self)); // After the hidden result pointer in rcx.
    CHECK(bigCall.args[2] == 7);

    read = client.RecorderRead(halfRun->slot);
    REQUIRE(read);
    REQUIRE(read->calls.size() == 1);
    CHECK(static_cast<uint32_t>(Registers(read->calls[0]).xmm[0]) == Bits(9.0f));

    for (const auto* run : {&intRun, &floatRun, &bigRun, &halfRun}) {
        auto stopped = client.RecorderStop((*run)->slot);
        REQUIRE(stopped);
        CHECK(stopped->state == static_cast<uint32_t>(RecorderState::Finished));
    }
    CHECK(ints->fn == &Mix);
    CHECK(floats->fn == &FMix);
    CHECK(bigs->fn == &MakeBig);
    CHECK(halves->fn == &Half);
    CHECK(ints->fn(&self, 1, 1, 1, 1, 1, 1) == Mix(&self, 1, 1, 1, 1, 1, 1));
}

TEST_CASE("a vtable recording swaps the entry in a copy, forwards every call and gives the vptr back") {
    Client client = Attach();
    auto shape = std::make_unique<Shape>();
    Shape* volatile object = shape.get();
    void* const table = *reinterpret_cast<void**>(object);
    const int64_t area = object->Area(2, 1, 2, 3, 4);
    const Big corners = object->Corners(5);
    const double length = object->Length(1.5, 2.5f, 3.5, 4.5, 5.5f);
    auto dry = client.NativeCallsStart([&] { auto spec = VtableSpec(object, AreaEntry); spec.flags = NativeCallsDry; return spec; }());
    REQUIRE(dry);
    CHECK(*reinterpret_cast<void**>(object) == table); // Nothing written.
    CHECK(dry->table == reinterpret_cast<uint64_t>(table));
    const auto areaRun = Start(client, VtableSpec(object, AreaEntry));
    const auto cornersRun = Start(client, VtableSpec(object, CornersEntry));
    const auto lengthRun = Start(client, VtableSpec(object, LengthEntry));
    REQUIRE(areaRun);
    REQUIRE(cornersRun);
    REQUIRE(lengthRun);
    CHECK(*reinterpret_cast<void**>(object) != table); // The private copy.
    CHECK(static_cast<void**>(table)[AreaEntry] == reinterpret_cast<void*>(areaRun->original)); // The table untouched.
    CHECK(object->Area(2, 1, 2, 3, 4) == area);
    const Big again = object->Corners(5);
    CHECK((again.a == corners.a && again.b == corners.b && again.c == corners.c && again.d == corners.d));
    CHECK(object->Length(1.5, 2.5f, 3.5, 4.5, 5.5f) == length);
    // Another instance shares the table but not the copy: never recorded.
    Shape other;
    Shape* volatile second = &other;
    CHECK(second->Area(2, 1, 2, 3, 4) == area);

    auto read = client.RecorderRead(areaRun->slot);
    REQUIRE(read);
    REQUIRE(read->calls.size() == 1);
    NativeCall call = Registers(read->calls[0]);
    CHECK(call.args[0] == reinterpret_cast<uint64_t>(shape.get()));
    CHECK((call.args[1] == 2 && call.args[2] == 1 && call.args[3] == 2));
    read = client.RecorderRead(cornersRun->slot);
    REQUIRE(read);
    REQUIRE(read->calls.size() == 1);
    call = Registers(read->calls[0]);
    CHECK(call.args[0] == reinterpret_cast<uint64_t>(shape.get())); // this, then the hidden result pointer.
    CHECK(call.args[2] == 5);
    read = client.RecorderRead(lengthRun->slot);
    REQUIRE(read);
    REQUIRE(read->calls.size() == 1);
    CHECK(Registers(read->calls[0]).xmm[1] == Bits(1.5));

    // The same entry again: already recorded.
    auto twice = client.NativeCallsStart(VtableSpec(object, AreaEntry));
    REQUIRE_FALSE(twice);
    CHECK(twice.error().reason.find("already recorded") != std::string::npos);

    REQUIRE(client.RecorderStop(areaRun->slot));
    REQUIRE(client.RecorderStop(cornersRun->slot));
    CHECK(*reinterpret_cast<void**>(object) != table); // One entry still taken: the copy stays.
    REQUIRE(client.RecorderStop(lengthRun->slot));
    CHECK(*reinterpret_cast<void**>(object) == table); // The last one gave the vptr back.
    CHECK(object->Area(2, 1, 2, 3, 4) == area);
}

TEST_CASE("the this filter, a guarded copy at a register and a faulting copy") {
    Client client = Attach();
    auto* cell = new Cell<PeekFn>{&Sum};
    int mine = 0, other = 0;
    NativeCallsSpec spec = CellSpec(cell);
    spec.filter = reinterpret_cast<uint64_t>(&mine);
    spec.copyRegister = static_cast<uint32_t>(NativeRegister::Rdx);
    spec.copyBytes = 16;
    const auto run = Start(client, spec);
    REQUIRE(run);
    uint8_t bytes[16];
    for (int i = 0; i < 16; ++i) bytes[i] = static_cast<uint8_t>(i + 1);
    CHECK(cell->fn(&mine, bytes) == 136);
    CHECK(cell->fn(&other, bytes) == 136); // Forwarded, not recorded.
    auto read = client.RecorderRead(run->slot);
    REQUIRE(read);
    REQUIRE(read->calls.size() == 1);
    CHECK(read->calls[0].row.object == reinterpret_cast<uint64_t>(&mine));
    CHECK(read->calls[0].row.tag == NativeCopied);
    REQUIRE(read->calls[0].payload.size() == sizeof(NativeCall) + 16);
    CHECK(memcmp(read->calls[0].payload.data() + sizeof(NativeCall), bytes, 16) == 0);
    REQUIRE(client.RecorderStop(run->slot));
    CHECK(cell->fn == &Sum);

    auto* faulting = new Cell<IntFn>{&Other};
    NativeCallsSpec at = CellSpec(faulting);
    at.copyRegister = static_cast<uint32_t>(NativeRegister::Rdx);
    at.copyBytes = 8;
    // A pointer that faults: the copy is caught, the call still forwards (to a
    // function that does not read it).
    const int64_t bad = 0x10;
    const auto faultRun = Start(client, at);
    REQUIRE(faultRun);
    CHECK(faulting->fn(nullptr, bad, 0, 0, 0, 0, 0) == -0x10);
    read = client.RecorderRead(faultRun->slot);
    REQUIRE(read);
    REQUIRE(read->calls.size() == 1);
    CHECK(read->calls[0].row.tag == 0);
    CHECK(read->calls[0].payload.size() == sizeof(NativeCall));
    CHECK(read->status.counts[NativeFaultedCount] == 1);
    REQUIRE(client.RecorderStop(faultRun->slot));
}

TEST_CASE("cells that are recorded, read-only or not code are refused; a replaced one is left alone") {
    Client client = Attach();
    auto* cell = new Cell<IntFn>{&Mix};
    const auto run = Start(client, CellSpec(cell));
    REQUIRE(run);
    auto* copy = new Cell<IntFn>{cell->fn}; // Another cell holding the thunk.
    auto twice = client.NativeCallsStart(CellSpec(copy));
    REQUIRE_FALSE(twice);
    CHECK(twice.error().reason.find("already recorded") != std::string::npos);
    CHECK(twice.error().detail == run->slot);

    // Something else replaces the pointer: the stop leaves it alone.
    cell->fn = &Other;
    auto stopped = client.RecorderStop(run->slot);
    REQUIRE(stopped);
    CHECK(stopped->state == static_cast<uint32_t>(RecorderState::Replaced));
    CHECK(cell->fn == &Other);
    CHECK(reinterpret_cast<IntFn>(run->thunk)(nullptr, 1, 0, 0, 0, 0, 0) == Mix(nullptr, 1, 0, 0, 0, 0, 0)); // Still forwards.

    // A read-only page, never made writable.
    auto* page = static_cast<IntFn*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    REQUIRE(page);
    *page = &Mix;
    DWORD old = 0;
    REQUIRE(VirtualProtect(page, 4096, PAGE_READONLY, &old));
    auto readOnly = client.NativeCallsStart(CellSpec(page));
    REQUIRE_FALSE(readOnly);
    CHECK(readOnly.error().reason.find("read-only") != std::string::npos);
    CHECK(*page == &Mix);
    VirtualFree(page, 0, MEM_RELEASE);

    // A value that is no code: heap data.
    auto* data = new Cell<void*>{new int(5)};
    auto notCode = client.NativeCallsStart(CellSpec(data));
    REQUIRE_FALSE(notCode);
    CHECK(notCode.error().reason.find("not code") != std::string::npos);

    // An image's read-only data (this test's own vtable).
    Shape shape;
    void* const* table = *reinterpret_cast<void* const* const*>(&shape);
    auto rdata = client.NativeCallsStart(CellSpec(table + AreaEntry));
    REQUIRE_FALSE(rdata);
    CHECK(rdata.error().reason.find("read-only") != std::string::npos);
}

TEST_CASE("concurrent callers through a native recording each get their own row") {
    Client client = Attach();
    auto* cell = new Cell<IntFn>{&Mix};
    const auto run = Start(client, CellSpec(cell));
    REQUIRE(run);
    constexpr int Threads = 4, Each = 50;
    std::vector<std::thread> callers;
    std::atomic<int> wrong{0};
    for (int t = 0; t < Threads; ++t)
        callers.emplace_back([&, t] {
            for (int i = 0; i < Each; ++i) {
                const int64_t a = t * 1000 + i;
                if (cell->fn(nullptr, a, 1, 2, 3, 4, 5) != Mix(nullptr, a, 1, 2, 3, 4, 5)) ++wrong;
            }
        });
    for (auto& caller : callers) caller.join();
    CHECK(wrong == 0);
    auto read = client.RecorderRead(run->slot);
    REQUIRE(read);
    CHECK(read->status.written == Threads * Each);
    REQUIRE(read->calls.size() == Threads * Each);
    std::vector<uint64_t> seen;
    for (const auto& call : read->calls) seen.push_back(Registers(call).args[1]);
    std::ranges::sort(seen);
    CHECK(std::ranges::adjacent_find(seen) == seen.end());
    REQUIRE(client.RecorderStop(run->slot));
    CHECK(cell->fn == &Mix);
}

TEST_CASE("a stale thunk of another kind records nothing in a slot armed since") {
    Client client = Attach();
    auto* cell = new Cell<IntFn>{&Mix};
    const auto run = Start(client, CellSpec(cell));
    REQUIRE(run);
    recorder::Slot& slot = recorder::g_slots[run->slot];
    const uint32_t kind = slot.kind;
    slot.kind = op::GameFirst + 0x77; // As if another kind had armed the slot.
    CHECK(cell->fn(nullptr, 1, 2, 3, 4, 5, 6) == Mix(nullptr, 1, 2, 3, 4, 5, 6));
    CHECK(slot.written == 0);
    slot.kind = kind;
    REQUIRE(client.RecorderStop(run->slot));
}
