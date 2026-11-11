// reagentlive loopback: pin, trial and watch, and trials surviving a takeover.
#include "support.h"

TEST_CASE("pin rewrites a value every tick until it expires") {
    Client client = Attach();
    REQUIRE(client.PinClear());
    std::atomic<uint32_t> value{7};
    auto pin = client.PinAdd(PinOf(&value, 42, 300));
    REQUIRE(pin);
    CHECK(pin->stop == PinStop::Active);
    CHECK(value == 42u);
    value = 1; // The game changes it; the next tick puts it back.
    CHECK(WaitFor([&] { return value == 42u; }));
    CHECK(WaitFor([&] {
        auto list = client.PinList();
        return list && list->size() == 1 && (*list)[0].stop == PinStop::Expired;
    }));
    value = 5; // Expired: no more writes.
    Sleep(20);
    CHECK(value == 5u);
    auto list = client.PinList();
    REQUIRE(list);
    CHECK((*list)[0].writes > 1);
    CHECK(client.PinClear(pin->id).value_or(0) == 1);
    CHECK(client.PinList()->empty());
}

TEST_CASE("pin stops when its guard changes and refuses unexpected memory") {
    Client client = Attach();
    REQUIRE(client.PinClear());
    std::atomic<uint32_t> value{3};
    uint64_t identity = 0x1234;
    PinSpec spec = PinOf(&value, 9, 5000);
    GuardOn(spec.guard, &identity, sizeof(identity));
    auto pin = client.PinAdd(spec);
    REQUIRE(pin);
    CHECK(value == 9u);
    identity = 0x9999; // The object was replaced.
    CHECK(WaitFor([&] {
        auto list = client.PinList();
        return list && list->size() == 1 && (*list)[0].stop == PinStop::GuardChanged;
    }));
    value = 4;
    Sleep(20);
    CHECK(value == 4u);

    spec = PinOf(&value, 11, 5000);
    spec.hasExpected = 1;
    const uint32_t stale = 77;
    memcpy(spec.expected, &stale, 4);
    auto refused = client.PinAdd(spec);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().status == Status::Refused);
    CHECK(value == 4u);
    CHECK(client.PinClear().value_or(0) == 2);
}

TEST_CASE("pin refuses a ninth active pin and bad specs") {
    Client client = Attach();
    REQUIRE(client.PinClear());
    std::atomic<uint32_t> values[MaxPins + 1]{};
    for (size_t i = 0; i < MaxPins; ++i) REQUIRE(client.PinAdd(PinOf(&values[i], 1, 5000)));
    auto ninth = client.PinAdd(PinOf(&values[MaxPins], 1, 5000));
    REQUIRE_FALSE(ninth);
    CHECK(ninth.error().status == Status::Refused);
    CHECK(client.PinClear().value_or(0) == MaxPins);
    PinSpec bad = PinOf(&values[0], 1, MaxPinMilliseconds + 1);
    CHECK_FALSE(client.PinAdd(bad));
}

TEST_CASE("a trial writes once and restores on expiry") {
    Client client = Attach();
    REQUIRE(client.TrialClear());
    auto owner = std::make_unique<TrialOwner>();
    auto trial = client.TrialAdd(TrialOf(*owner, owner->value, 42, 7, 150));
    REQUIRE(trial);
    CHECK(trial->stop == TrialStop::Active);
    CHECK(owner->value == 42u);
    CHECK(WaitFor([&] { return TrialStopOf(client, trial->id) == TrialStop::Restored; }));
    CHECK(owner->value == 7u);
    auto list = client.TrialList();
    REQUIRE(list);
    REQUIRE(list->size() == 1); // Finished trials stay listed until cleared.
    CHECK((*list)[0].original[0] == 7);
    auto cleared = client.TrialClear(trial->id);
    REQUIRE(cleared);
    CHECK(cleared->size() == 1);
    CHECK(client.TrialList()->empty());
}

TEST_CASE("clearing an active trial restores it and returns its end state") {
    Client client = Attach();
    REQUIRE(client.TrialClear());
    auto owner = std::make_unique<TrialOwner>();
    auto trial = client.TrialAdd(TrialOf(*owner, owner->native.field, 9, 3, 30000, true));
    REQUIRE(trial);
    CHECK(owner->native.field == 9u);
    auto cleared = client.TrialClear();
    REQUIRE(cleared);
    REQUIRE(cleared->size() == 1);
    CHECK((*cleared)[0].stop == TrialStop::Restored);
    CHECK(owner->native.field == 3u);
}

TEST_CASE("a value the game changes is a conflict and is left alone") {
    Client client = Attach();
    REQUIRE(client.TrialClear());
    auto owner = std::make_unique<TrialOwner>();
    auto trial = client.TrialAdd(TrialOf(*owner, owner->value, 42, 7, 30000));
    REQUIRE(trial);
    owner->value = 99; // The game wrote its own value.
    CHECK(WaitFor([&] { return TrialStopOf(client, trial->id) == TrialStop::Conflict; }));
    auto cleared = client.TrialClear();
    REQUIRE(cleared);
    CHECK((*cleared)[0].stop == TrialStop::Conflict);
    CHECK(owner->value == 99u); // Neither the trial bytes nor the original were written back.
}

TEST_CASE("a replaced owner or a changed vtable abandons the trial without writing") {
    Client client = Attach();
    REQUIRE(client.TrialClear());
    auto owner = std::make_unique<TrialOwner>();
    auto trial = client.TrialAdd(TrialOf(*owner, owner->value, 42, 7, 30000));
    REQUIRE(trial);
    owner->identity = 0x9999; // Freed and reused by another object.
    CHECK(WaitFor([&] { return TrialStopOf(client, trial->id) == TrialStop::Abandoned; }));
    auto list = client.TrialList();
    REQUIRE(list);
    CHECK((*list)[0].guard == TrialGuard::Owner);
    REQUIRE(client.TrialClear());
    CHECK(owner->value == 42u); // Not restored: the memory may belong to something else.

    owner = std::make_unique<TrialOwner>();
    trial = client.TrialAdd(TrialOf(*owner, owner->native.field, 9, 3, 30000, true));
    REQUIRE(trial);
    owner->native.vtable = 0x1234; // The attribute was removed; its memory holds something else.
    CHECK(WaitFor([&] { return TrialStopOf(client, trial->id) == TrialStop::Abandoned; }));
    list = client.TrialList();
    REQUIRE(list);
    CHECK((*list)[0].guard == TrialGuard::Object);
    REQUIRE(client.TrialClear());
    CHECK(owner->native.field == 9u);
}

TEST_CASE("trials refuse module targets, out-of-range targets, stale memory and non-vtables") {
    Client client = Attach();
    REQUIRE(client.TrialClear());
    auto owner = std::make_unique<TrialOwner>();
    // A static variable lives in the exe's image.
    TrialSpec spec = TrialOf(*owner, owner->value, 42, 7, 1000);
    spec.address = reinterpret_cast<uint64_t>(&g_staticTarget);
    spec.ownerAddress = spec.address; // In range, so only the module check refuses it.
    auto refused = client.TrialAdd(spec);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().status == Status::Refused);
    CHECK(g_staticTarget == 7u);
    // Outside the owner with no --object; beyond the object's range.
    spec = TrialOf(*owner, owner->value, 42, 7, 1000);
    spec.ownerSize = 4;
    CHECK_FALSE(TrialSpecValid(spec));
    CHECK_FALSE(client.TrialAdd(spec));
    spec = TrialOf(*owner, owner->native.field, 9, 3, 1000, true);
    spec.address = spec.object + MaxTrialObjectRange;
    CHECK_FALSE(TrialSpecValid(spec));
    // Memory that no longer holds --expect.
    refused = client.TrialAdd(TrialOf(*owner, owner->value, 42, 8, 1000));
    REQUIRE_FALSE(refused);
    CHECK(owner->value == 7u);
    // An object whose first 8 bytes do not point into a module.
    owner->native.vtable = 0x1234;
    refused = client.TrialAdd(TrialOf(*owner, owner->native.field, 9, 3, 1000, true));
    REQUIRE_FALSE(refused);
    CHECK(owner->native.field == 3u);
    CHECK(client.TrialList()->empty());
}

TEST_CASE("watch samples ranges every tick and stops when a guarded object changes") {
    Client client = Attach();
    std::atomic<uint32_t> counter{0};
    uint64_t identity = 0xabc;
    double steady = 2.5;
    WatchSpec spec{};
    spec.count = 2;
    spec.milliseconds = 20000; // Stopped once the writer is done.
    spec.ranges[0] = {reinterpret_cast<uint64_t>(&counter), 4, 0, {}};
    spec.ranges[1] = {reinterpret_cast<uint64_t>(&steady), 8, 0, {}};
    GuardOn(spec.ranges[1].guard, &identity, sizeof(identity));
    auto capacity = client.WatchStart(spec);
    REQUIRE(capacity);
    CHECK(*capacity > 1000u);
    std::thread writer([&] {
        for (int i = 1; i <= 50; ++i) { counter = i; Sleep(4); }
        identity = 0xdef; // Guard changes part way: the capture stops with its object.
    });
    writer.join();
    REQUIRE(WaitFor([&] { auto info = client.WatchStatus(); return info && info->stop == WatchEnd::ObjectReplaced; }));
    auto rows = client.WatchRead(spec);
    REQUIRE(rows);
    CHECK_FALSE(rows->running);
    REQUIRE(rows->rows.size() > 20);
    uint32_t last = 0, seenMax = 0;
    uint64_t lastTick = 0, lastTime = 0;
    for (const auto& row : rows->rows) {
        CHECK(row.header.tick > lastTick);
        CHECK(row.header.microseconds >= lastTime);
        lastTick = row.header.tick;
        lastTime = row.header.microseconds;
        REQUIRE(row.ranges[0].size() == 4);
        uint32_t value;
        memcpy(&value, row.ranges[0].data(), 4);
        CHECK(value >= last); // The writer only counts up.
        last = value;
        seenMax = std::max(seenMax, value);
        REQUIRE(row.ranges[1].size() == 8); // Every row was taken while the object held.
        double read;
        memcpy(&read, row.ranges[1].data(), 8);
        CHECK(read == 2.5);
    }
    CHECK(seenMax >= 49u);
    // Stop ends a running watch early; rows stay readable.
    spec.milliseconds = 10000;
    GuardOn(spec.ranges[1].guard, &identity, sizeof(identity));
    REQUIRE(client.WatchStart(spec));
    Sleep(30);
    auto stopped = client.WatchStop();
    REQUIRE(stopped);
    auto after = client.WatchRead(spec);
    REQUIRE(after);
    CHECK_FALSE(after->running);
    CHECK(after->rows.size() == *stopped);
    CHECK(client.WatchStatus()->stop == WatchEnd::Stopped);
}

TEST_CASE("an interval capture samples on its interval and can be read while it runs") {
    Client client = Attach();
    std::atomic<uint32_t> counter{0};
    WatchSpec spec{};
    spec.count = 1;
    spec.milliseconds = 400;
    spec.interval = 40;
    spec.ranges[0] = {reinterpret_cast<uint64_t>(&counter), 4, 0, {}};
    REQUIRE(client.WatchStart(spec));
    Sleep(150);
    // A later client reads it from the agent's own record of the spec.
    auto info = client.WatchStatus();
    REQUIRE(info);
    CHECK(info->spec.interval == 40);
    CHECK(info->stop == WatchEnd::Running);
    auto partial = client.WatchRead(info->spec);
    REQUIRE(partial);
    CHECK(partial->running);
    CHECK(partial->rows.size() >= 2);
    REQUIRE(WaitFor([&] { auto now = client.WatchStatus(); return now && now->stop == WatchEnd::Deadline; }));
    auto rows = client.WatchRead(spec);
    REQUIRE(rows);
    CHECK(rows->rows.size() >= 8);
    CHECK(rows->rows.size() <= 11);
    for (size_t i = 1; i < rows->rows.size(); ++i)
        CHECK(rows->rows[i].header.microseconds - rows->rows[i - 1].header.microseconds >= 30000);
}

TEST_CASE("a capture that does not fit the buffer is refused with the longest that would") {
    Client client = Attach();
    static uint8_t block[512];
    WatchSpec spec{};
    spec.count = 3;
    spec.interval = 1;
    spec.milliseconds = MaxWatchCaptureMilliseconds;
    for (uint32_t i = 0; i < 3; ++i) spec.ranges[i] = {reinterpret_cast<uint64_t>(block), 512, 0, {}};
    const auto refused = client.WatchStart(spec);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().detail == WatchLongestFit(spec));
    CHECK(WatchLongestFit(spec) == uint64_t{WatchMaxRows(spec) - 1});
    spec.milliseconds = static_cast<uint32_t>(WatchLongestFit(spec));
    CHECK(WatchFits(spec));
    REQUIRE(client.WatchStart(spec));
    REQUIRE(client.WatchStop());
}

TEST_CASE("a followed root continues on its replacement, marking the gap") {
    Client client = Attach();
    auto roots = client.WatchRoots();
    REQUIRE(roots);
    CHECK(*roots == std::vector<std::string>{"thing"});
    RootThing first{0x1111, {5}}, second{0x2222, {9}};
    g_root = &first;
    WatchSpec spec{};
    spec.count = 1;
    spec.milliseconds = 2000;
    spec.interval = 5;
    strcpy_s(spec.root, "thing");
    spec.ranges[0] = {offsetof(RootThing, value), 4, 0, {}};
    REQUIRE(client.WatchStart(spec));
    Sleep(40);
    g_root = nullptr;
    first.identity = 0xdead;      // Replaced: the guard fails, and there is no new one yet.
    Sleep(40);
    g_root = &second;
    Sleep(40);
    REQUIRE(client.WatchStop());
    auto rows = client.WatchRead(spec);
    REQUIRE(rows);
    size_t lost = 0, resumed = 0;
    uint32_t before = 0, after = 0;
    for (const auto& row : rows->rows) {
        if (row.header.flags & WatchRowLost) { ++lost; CHECK(row.header.root == 0); continue; }
        if (row.header.flags & WatchRowResumed) { ++resumed; CHECK(row.header.root == reinterpret_cast<uint64_t>(&second)); }
        REQUIRE(row.ranges[0].size() == 4);
        uint32_t value;
        memcpy(&value, row.ranges[0].data(), 4);
        (row.header.root == reinterpret_cast<uint64_t>(&first) ? before : after) = value;
    }
    CHECK(lost == 1);   // One row marks the gap, however long it lasts.
    CHECK(resumed == 1);
    CHECK(before == 5);
    CHECK(after == 9);
    g_root = nullptr;
}

TEST_CASE("a root that stays absent keeps the capture lost; unknown roots and hosts without them are refused") {
    Client client = Attach();
    g_root = nullptr;
    WatchSpec spec{};
    spec.count = 1;
    spec.milliseconds = 60;
    spec.interval = 5;
    strcpy_s(spec.root, "thing");
    spec.ranges[0] = {0, 4, 0, {}};
    REQUIRE(client.WatchStart(spec));
    REQUIRE(WaitFor([&] { auto info = client.WatchStatus(); return info && info->stop == WatchEnd::Deadline; }));
    auto rows = client.WatchRead(spec);
    REQUIRE(rows);
    REQUIRE(rows->rows.size() == 1);
    CHECK(rows->rows[0].header.flags == WatchRowLost);
    strcpy_s(spec.root, "other");
    auto unknown = client.WatchStart(spec);
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().reason == std::string("the host offers no root by that name"));
    TheAgent().roots = false;
    Sleep(5);
    strcpy_s(spec.root, "thing");
    auto none = client.WatchStart(spec);
    TheAgent().roots = true;
    REQUIRE_FALSE(none);
    CHECK(none.error().reason == std::string("the host offers no roots"));
    CHECK(client.WatchRoots()->size() == 1);
}

// The takeover leaves this process's agent dormant for good; other cases are
// unaffected because ctest runs each case in its own process.
TEST_CASE("a replaced agent restores its active trials once, under their guards") {
    Client client = Attach();
    REQUIRE(client.TrialClear());
    auto kept = std::make_unique<TrialOwner>();
    auto replaced = std::make_unique<TrialOwner>();
    REQUIRE(client.TrialAdd(TrialOf(*kept, kept->value, 42, 7, 30000)));
    REQUIRE(client.TrialAdd(TrialOf(*replaced, replaced->value, 43, 7, 30000)));
    CHECK(kept->value == 42u);
    CHECK(replaced->value == 43u);
    // Between two ticks: one owner is replaced and a newer agent takes the section.
    TheAgent().paused = true;
    Sleep(5);
    replaced->identity = 0x6262;
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, SectionName().c_str());
    REQUIRE(mapping);
    auto* section = static_cast<Section*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Section)));
    REQUIRE(section);
    std::atomic_ref(section->owner).store(section->owner ^ 0x10000, std::memory_order_release);
    TheAgent().paused = false;
    CHECK(WaitFor([&] { return kept->value == 7u; }));
    Sleep(20);
    CHECK(replaced->value == 43u); // Guard failed: nothing written.
    UnmapViewOfFile(section);
    CloseHandle(mapping);
}
