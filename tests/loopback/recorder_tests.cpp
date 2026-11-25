// reagentlive loopback: the core's pointer-swap recorder slots, through a test
// kind whose thunks record an int argument and the result.
#include "support.h"

namespace {
using Fn = int (*)(void* self, int x);
constexpr uint32_t TestKind = op::GameFirst + 0x10;
constexpr uint8_t TestTag = 7;

int Twice(void*, int x) { return x * 2; }
int Thrice(void*, int x) { return x * 3; }

template<unsigned I> int Thunk(void* self, int x) {
    recorder::Slot& slot = recorder::g_slots[I];
    const uint64_t object = reinterpret_cast<uint64_t>(self);
    int64_t now = 0;
    const bool armed = recorder::Enter(slot, TestKind, object) && recorder::InTime(slot, now);
    const int result = reinterpret_cast<Fn>(slot.original)(self, x);
    if (armed) {
        const recorder::Pending row = recorder::Claim(slot, object, now);
        const int pair[2] = {x, result};
        memcpy(recorder::Payload(row), pair, sizeof(pair));
        recorder::Publish(row, sizeof(pair), TestTag);
        recorder::Count(slot, 0);
    }
    recorder::Leave(slot);
    return result;
}
template<unsigned... I> constexpr auto MakeThunks(std::integer_sequence<unsigned, I...>) {
    return std::array<Fn, sizeof...(I)>{&Thunk<I>...};
}
constexpr auto Thunks = MakeThunks(std::make_integer_sequence<unsigned, MaxRecorderSlots>{});

// A heap cell holding a code pointer, as an object's field would.
struct Cell { Fn volatile fn = &Twice; };

uint32_t Arm(Cell& cell, uint64_t milliseconds, uint64_t filter = 0) {
    uint32_t index = MaxRecorderSlots;
    WaitFor([&] { return (index = recorder::Pick()) < MaxRecorderSlots; }, 3000); // Past a slot's reuse grace.
    REQUIRE(index < MaxRecorderSlots);
    const recorder::Arm arm{TestKind, 8, reinterpret_cast<void* volatile*>(const_cast<Fn*>(&cell.fn)),
        reinterpret_cast<void*>(cell.fn), reinterpret_cast<void*>(Thunks[index]), 0x1234, filter, milliseconds};
    REQUIRE(recorder::Start(index, arm) == recorder::Started::Armed);
    return index;
}
}

TEST_CASE("a finished recorder slot is reused only after its grace, oldest first") {
    using State = RecorderState;
    constexpr int64_t now = 10000;
    RecorderSlotView slots[4]{{State::Recording, 0, 0}, {State::Finished, 0, now - 200},
                              {State::Replaced, 0, 0}, {State::Finished, 0, now - 5000}};
    CHECK(PickRecorderSlot(slots, 4, now) == 3);                     // Past the grace.
    slots[3].restoredMs = now - 100;
    CHECK(PickRecorderSlot(slots, 4, now) == 4);                     // Both too recent.
    slots[1].restoredMs = now - RecorderReuseGraceMs;
    slots[3].restoredMs = now - RecorderReuseGraceMs - 1;
    CHECK(PickRecorderSlot(slots, 4, now) == 3);                     // The oldest restore.
    slots[3].inFlight = 1;
    CHECK(PickRecorderSlot(slots, 4, now) == 1);                     // A call still inside.
    slots[2].state = State::Free;
    CHECK(PickRecorderSlot(slots, 4, now) == 2);                     // A free slot first.
}

TEST_CASE("a recording swaps the cell, records each call, and stop restores it") {
    Client client = Attach();
    Cell cell;
    int self = 0;
    const uint32_t index = Arm(cell, 10000);
    CHECK(cell.fn == Thunks[index]);
    for (int i = 1; i <= 3; ++i) CHECK(cell.fn(&self, i) == i * 2); // Forwarded exactly.
    auto read = client.RecorderRead();
    REQUIRE(read);
    CHECK(read->status.slot == index);
    CHECK(read->status.kind == TestKind);
    CHECK(read->status.target == 0x1234);
    CHECK(read->status.original == reinterpret_cast<uint64_t>(&Twice));
    CHECK(read->status.state == static_cast<uint32_t>(RecorderState::Recording));
    CHECK(read->status.counts[0] == 3);
    REQUIRE(read->calls.size() == 3);
    for (int i = 0; i < 3; ++i) {
        const auto& call = read->calls[static_cast<size_t>(i)];
        CHECK(call.row.sequence == static_cast<uint64_t>(i + 1));
        CHECK(call.row.object == reinterpret_cast<uint64_t>(&self));
        CHECK(call.row.thread == GetCurrentThreadId());
        CHECK(call.row.tag == TestTag);
        REQUIRE(call.payload.size() == 8);
        int pair[2];
        memcpy(pair, call.payload.data(), sizeof(pair));
        CHECK(pair[0] == i + 1);
        CHECK(pair[1] == (i + 1) * 2);
    }
    // The cursor: only newer rows.
    cell.fn(&self, 9);
    auto later = client.RecorderRead(index, read->last);
    REQUIRE(later);
    REQUIRE(later->calls.size() == 1);
    CHECK(later->calls[0].row.sequence == 4);
    auto stopped = client.RecorderStop(index);
    REQUIRE(stopped);
    CHECK(stopped->state == static_cast<uint32_t>(RecorderState::Finished));
    CHECK(cell.fn == &Twice);
}

TEST_CASE("a filtered recording keeps only calls on its object; the ring keeps the newest rows") {
    Client client = Attach();
    Cell cell;
    int mine = 0, other = 0;
    const uint32_t index = Arm(cell, 10000, reinterpret_cast<uint64_t>(&mine));
    for (int i = 0; i < 300; ++i) cell.fn(&mine, i);
    cell.fn(&other, 1); // Forwarded, not recorded.
    auto read = client.RecorderRead(index);
    REQUIRE(read);
    CHECK(read->status.written == 300);
    REQUIRE(read->calls.size() == MaxRecorderRows);
    CHECK(read->calls.front().row.sequence == 300 - MaxRecorderRows + 1);
    CHECK(read->calls.back().row.sequence == 300);
    for (const auto& call : read->calls) CHECK(call.row.object == reinterpret_cast<uint64_t>(&mine));
    REQUIRE(client.RecorderStop(index));
    CHECK(cell.fn == &Twice);
}

TEST_CASE("a cell something else swapped since is left alone: replaced") {
    Client client = Attach();
    Cell cell;
    int self = 0;
    const uint32_t index = Arm(cell, 10000);
    cell.fn = &Thrice; // Another owner.
    auto stopped = client.RecorderStop(index);
    REQUIRE(stopped);
    CHECK(stopped->state == static_cast<uint32_t>(RecorderState::Replaced));
    CHECK(cell.fn == &Thrice);
    // The thunk still forwards to the original for anyone holding it.
    CHECK(Thunks[index](&self, 5) == 10);
    CHECK(recorder::Pick() != index); // Never reused.
}

TEST_CASE("a recording past its time stops recording and the sweep restores it") {
    Client client = Attach();
    Cell cell;
    int self = 0;
    const uint32_t index = Arm(cell, 1);
    Sleep(20);
    CHECK(Thunks[index](&self, 2) == 4); // Past its time: forwarded, not recorded.
    CHECK(WaitFor([&] { return cell.fn == &Twice; })); // The agent's tick restored it.
    auto read = client.RecorderRead(index);
    REQUIRE(read);
    CHECK(read->status.state == static_cast<uint32_t>(RecorderState::Finished));
    CHECK(read->calls.empty());
}

TEST_CASE("concurrent callers each get their own row") {
    Client client = Attach();
    Cell cell;
    const uint32_t index = Arm(cell, 10000);
    constexpr int Threads = 4, Each = 50;
    std::vector<std::thread> callers;
    std::atomic<int> wrong{0};
    for (int t = 0; t < Threads; ++t)
        callers.emplace_back([&, t] {
            for (int i = 0; i < Each; ++i)
                if (cell.fn(nullptr, t * 1000 + i) != (t * 1000 + i) * 2) ++wrong;
        });
    for (auto& caller : callers) caller.join();
    CHECK(wrong == 0);
    auto read = client.RecorderRead(index);
    REQUIRE(read);
    CHECK(read->status.written == Threads * Each);
    CHECK(read->status.counts[0] == Threads * Each);
    REQUIRE(read->calls.size() == Threads * Each);
    std::vector<int> seen;
    for (const auto& call : read->calls) {
        int pair[2];
        memcpy(pair, call.payload.data(), sizeof(pair));
        CHECK(pair[1] == pair[0] * 2);
        seen.push_back(pair[0]);
    }
    std::ranges::sort(seen);
    CHECK(std::ranges::adjacent_find(seen) == seen.end());
    REQUIRE(client.RecorderStop(index));
    CHECK(cell.fn == &Twice);
}

TEST_CASE("an unknown slot is not found") {
    Client client = Attach();
    auto read = client.RecorderRead(MaxRecorderSlots - 1);
    // The last slot is free unless a case above took every other one.
    if (recorder::g_slots[MaxRecorderSlots - 1].state == static_cast<LONG>(RecorderState::Free)) {
        REQUIRE_FALSE(read);
        CHECK(read.error().status == Status::NotFound);
    }
}
