// reagentlive loopback: the channel: ops header, multi-tick ops, timeouts, attach, hold budgets.
#include "support.h"

TEST_CASE("the header lists the agent's ops") {
    Client client = Attach();
    CHECK(client.Supports(op::Read));
    CHECK(client.Supports(op::Modules));
    CHECK(client.Supports(OpSlow));
    CHECK_FALSE(client.Supports(9999));
}

TEST_CASE("describe many pages refs in order and marks gone ones") {
    Client client = Attach();
    std::vector<ue::Ref> refs;
    for (uint64_t i = 0; i < 40; ++i) refs.push_back({0x10000 + i, -1, 0}); // Odd addresses are gone.
    const auto before = g_describeRequests;
    const auto objects = ue::DescribeMany(client, refs);
    REQUIRE(objects.has_value());
    CHECK(g_describeRequests - before == 2); // 32 + 8.
    REQUIRE(objects->size() == 40);
    REQUIRE((*objects)[0].has_value());
    CHECK((*objects)[0]->name == "Obj10000");
    CHECK_FALSE((*objects)[1].has_value());
    REQUIRE((*objects)[38].has_value());
    CHECK((*objects)[38]->name == "Obj10026");
    CHECK((*objects)[38]->className == "Thing");
    CHECK(ue::DescribeMany(client, {})->empty());
}

TEST_CASE("multi-tick handlers continue until done") {
    Client client = Attach();
    Request request{};
    request.op = OpSlow;
    request.length = 5;
    const auto response = client.Send(request);
    REQUIRE(response.has_value());
    CHECK(response->status == Status::Ok);
    CHECK(response->detail == 6); // First call plus five continuations.
}

TEST_CASE("unknown ops are unsupported") {
    Client client = Attach();
    Request request{};
    request.op = 9999;
    const auto response = client.Send(request);
    REQUIRE(response.has_value());
    CHECK(response->status == Status::Unsupported);
}

TEST_CASE("a stalled agent times out and the channel recovers") {
    Client client = Attach();
    std::vector<uint8_t> buffer(4, 9);
    TheAgent().paused = true;
    Request request{};
    request.op = op::Read;
    request.address = reinterpret_cast<uint64_t>(buffer.data());
    request.length = 4;
    const auto timedOut = client.Send(request, 50ms);
    REQUIRE_FALSE(timedOut.has_value());
    CHECK(timedOut.error().kind == Error::Kind::Timeout);
    TheAgent().paused = false;
    const auto bytes = client.Read(request.address, 4);
    REQUIRE(bytes.has_value());
    CHECK(*bytes == buffer);
}

TEST_CASE("status names the answering agent's build") {
    Client client = Attach();
    const auto build = client.AgentBuild();
    CHECK_FALSE(build.empty());
    CHECK(build.size() < MaxBuildText);
    CHECK(client.AgentTakeovers() == 0);
}

TEST_CASE("heartbeat and pid identify the agent") {
    Client client = Attach();
    const uint64_t before = client.Heartbeat();
    Sleep(20);
    CHECK(client.Heartbeat() > before);
    CHECK(client.AgentPid() == GetCurrentProcessId());
}

namespace {
// Another client on its own thread (the slot is a mutex, which belongs to a
// thread), holding the slot from `held` until `release`.
struct OtherHolder {
    std::atomic<bool> held{false}, release{false};
    std::thread thread;
    explicit OtherHolder(const char* caller = "") {
        thread = std::thread([this, caller] {
            auto other = Client::Attach(SectionName(), Layout, caller);
            if (!other) { held = true; return; } // The case's own requests then find the slot free and fail.
            const Client::Hold hold(*other);
            held = true;
            while (!release) Sleep(1);
        });
        while (!held) Sleep(1);
    }
    ~OtherHolder() {
        release = true;
        thread.join();
    }
};
// Whether a fresh client on another thread finds the slot taken within `wait`.
bool BusyFromAnotherThread(std::chrono::milliseconds wait) {
    bool busy = false;
    std::thread([&] {
        Client other = Attach();
        other.SetBusyWait(wait);
        const uint8_t byte = 7;
        const auto read = other.Read(reinterpret_cast<uint64_t>(&byte), 1);
        busy = !read && read.error().kind == Error::Kind::Busy;
    }).join();
    return busy;
}
}

TEST_CASE("clients attach together and take turns: a request waits out another client's hold") {
    Client client = Attach();
    const uint8_t buffer[4] = {1, 2, 3, 4};
    std::optional<OtherHolder> other(std::in_place);
    const auto before = client.Stats();
    std::thread releaser([&] { Sleep(100); other->release = true; });
    REQUIRE(client.Read(reinterpret_cast<uint64_t>(buffer), 4));
    CHECK(client.Stats().slotWaitMicroseconds - before.slotWaitMicroseconds >= 80000);
    releaser.join();
    other.reset();
    // Free slot: no wait.
    const auto free = client.Stats();
    REQUIRE(client.Read(reinterpret_cast<uint64_t>(buffer), 4));
    CHECK(client.Stats().slotWaitMicroseconds - free.slotWaitMicroseconds < 20000);
}

TEST_CASE("past the busy wait a request is refused as busy, naming the holder") {
    Client client = Attach();
    client.SetBusyWait(30ms);
    const uint8_t byte = 1;
    {
        OtherHolder other("script");
        const auto read = client.Read(reinterpret_cast<uint64_t>(&byte), 1);
        REQUIRE_FALSE(read);
        CHECK(read.error().kind == Error::Kind::Busy);
        CHECK(read.error().message.find("a script client, pid " + std::to_string(GetCurrentProcessId())) != std::string::npos);
        const auto holder = Client::Holder(SectionName());
        REQUIRE(holder);
        CHECK(holder->caller == "script");
        CHECK(holder->since > 0);
    }
    CHECK(client.Read(reinterpret_cast<uint64_t>(&byte), 1));
}

TEST_CASE("a client that dies holding the slot frees it") {
    // Never destroyed: its thread ends owning the mutex, as a killed process does.
    std::thread([] {
        auto* lost = new Client(Attach());
        new Client::Hold(*lost);
    }).join();
    Client client = Attach();
    client.SetBusyWait(2s);
    const uint8_t byte = 1;
    CHECK(client.Read(reinterpret_cast<uint64_t>(&byte), 1));
}

TEST_CASE("a turn frees a lone hold's slot for another client, then takes it back") {
    Client client = Attach();
    const uint8_t byte = 1;
    std::optional<Client::Hold> hold(std::in_place, client);
    CHECK(BusyFromAnotherThread(20ms));
    {
        const Client::Turn turn(client);
        CHECK_FALSE(BusyFromAnotherThread(1s));
        // A request inside the turn takes the slot for itself.
        CHECK(client.Read(reinterpret_cast<uint64_t>(&byte), 1));
    }
    CHECK(BusyFromAnotherThread(20ms));
    hold.reset();
    CHECK_FALSE(BusyFromAnotherThread(1s));
}

TEST_CASE("a turn inside a nested hold keeps the slot: a batch keeps its moment") {
    Client client = Attach();
    const Client::Hold outer(client);
    const Client::Hold inner(client);
    const Client::Turn turn(client);
    CHECK(BusyFromAnotherThread(20ms));
}

TEST_CASE("long waits report whether the agent is ticking") {
    Client client = Attach();
    static int notices;
    static bool sawTicking;
    notices = 0;
    sawTicking = false;
    client.SetWaitNotice([](std::chrono::milliseconds, bool ticking) { ++notices; sawTicking |= ticking; });
    std::vector<uint8_t> buffer(4, 1);
    TheAgent().paused = true;
    const auto timedOut = client.Send(Request{op::Read, 0, reinterpret_cast<uint64_t>(buffer.data()), {}, 4}, 1500ms);
    TheAgent().paused = false;
    REQUIRE_FALSE(timedOut.has_value());
    CHECK(timedOut.error().message.find("not ticking") != std::string::npos);
    CHECK(notices >= 1);
    CHECK_FALSE(sawTicking);
    REQUIRE(client.Read(reinterpret_cast<uint64_t>(buffer.data()), 4).has_value()); // Channel recovers.
}

TEST_CASE("attach refuses another layout and missing sections") {
    TheAgent();
    Attach(); // Agent open.
    const auto wrong = Client::Attach(SectionName(), Layout + 1);
    REQUIRE_FALSE(wrong.has_value());
    CHECK(wrong.error().kind == Error::Kind::Attach);
    const auto missing = Client::Attach(L"Local\\reagentlive-test-missing", Layout);
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().kind == Error::Kind::Attach);
}

TEST_CASE("every response carries the agent tick, and plain requests take one tick each") {
    Client client = Attach();
    const uint8_t buffer[4] = {1, 2, 3, 4};
    const uint64_t first = ReadTick(client, buffer);
    const uint64_t second = ReadTick(client, buffer);
    CHECK(first > 0);
    CHECK(second > first);
}

TEST_CASE("held requests run in one agent tick") {
    Client client = Attach();
    const uint8_t buffer[4] = {1, 2, 3, 4};
    bool same = false;
    for (int attempt = 0; attempt < 5 && !same; ++attempt) { // A tick boundary can split the first try.
        const Client::Hold hold(client);
        for (int i = 0; i < 5; ++i) ReadTick(client, buffer);
        same = hold.SameTick() && hold.FirstTick() != 0;
    }
    CHECK(same);
}

TEST_CASE("hold is off when the host sets no budget") {
    Client client = Attach();
    const HoldBudget off(0, 1000);
    const uint8_t buffer[4] = {1, 2, 3, 4};
    const Client::Hold hold(client);
    for (int i = 0; i < 3; ++i) ReadTick(client, buffer);
    CHECK(hold.LastTick() >= hold.FirstTick() + 2);
}

TEST_CASE("the per-tick budget ends a long hold") {
    Client client = Attach();
    const HoldBudget tiny(200, 1);
    const uint8_t buffer[4] = {1, 2, 3, 4};
    const Client::Hold hold(client);
    for (int i = 0; i < 4; ++i) ReadTick(client, buffer);
    CHECK_FALSE(hold.SameTick());
}

TEST_CASE("a silent held client costs one wait and the agent keeps ticking") {
    Client client = Attach();
    const uint8_t buffer[4] = {1, 2, 3, 4};
    {
        const Client::Hold hold(client);
        ReadTick(client, buffer);
    }
    const uint64_t before = client.Heartbeat();
    Sleep(30);
    CHECK(client.Heartbeat() > before + 5);
    CHECK(ReadTick(client, buffer) > before);
}

TEST_CASE("a multi-tick op ends the hold for the ticks it runs") {
    Client client = Attach();
    const uint8_t buffer[4] = {1, 2, 3, 4};
    const Client::Hold hold(client);
    const uint64_t first = ReadTick(client, buffer);
    Request slow{};
    slow.op = OpSlow;
    slow.length = 2;
    const auto response = client.Send(slow);
    REQUIRE(response.has_value());
    CHECK(response->tick >= first + 2);
    CHECK_FALSE(hold.SameTick());
}

TEST_CASE("a handler that throws is refused and the agent keeps answering") {
    Client client = Attach();
    const uint8_t buffer[4] = {1, 2, 3, 4};
    for (uint32_t later = 0; later < 2; ++later) { // On the first call, then on a continued tick.
        Request request{};
        request.op = OpThrow;
        request.length = later;
        const auto response = client.Send(request);
        REQUIRE(response.has_value());
        CHECK(response->status == Status::Refused);
        CHECK(std::string(response->reason) == "handler exception");
        CHECK(response->length == 0);
        CHECK(ReadTick(client, buffer) > response->tick);
    }
}

TEST_CASE("the client counts trips and waits without the 15.6 ms timer") {
    Client client = Attach();
    const uint8_t buffer[4] = {1, 2, 3, 4};
    const auto before = client.Stats();
    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) ReadTick(client, buffer);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const auto after = client.Stats();
    CHECK(after.trips - before.trips == 20);
    CHECK(after.waitMicroseconds > before.waitMicroseconds);
    CHECK(elapsed < 20 * 8ms); // A 1 ms agent tick; polling at 15.6 ms would take over 300 ms.
}

TEST_CASE("held requests stay in one tick after a request that waited past the client's short spin") {
    Client client = Attach();
    const uint8_t buffer[4] = {1, 2, 3, 4};
    const Client::Hold hold(client);
    Request slow{};
    slow.op = OpSlow;
    slow.length = 5; // Several 1 ms agent ticks: past the 2 ms spin of a plain request.
    const auto response = client.Send(slow);
    REQUIRE(response.has_value());
    uint64_t first = 0, last = 0;
    for (int i = 0; i < 5; ++i) {
        last = ReadTick(client, buffer);
        if (!first) first = last;
    }
    CHECK(first == response->tick);
    CHECK(last == first);
}

TEST_CASE("a span inside a hold reports its own first tick") {
    Client client = Attach();
    const uint8_t buffer[4] = {1, 2, 3, 4};
    Client::Hold hold(client);
    hold.BeginSpan();
    CHECK(hold.SpanFirstTick() == 0);
    const uint64_t first = ReadTick(client, buffer);
    CHECK(hold.SpanFirstTick() == first);
    hold.BeginSpan();
    Request slow{};
    slow.op = OpSlow;
    slow.length = 2;
    const auto response = client.Send(slow);
    REQUIRE(response.has_value());
    CHECK(hold.SpanFirstTick() == response->tick);
    CHECK(hold.FirstTick() == first);
    CHECK(hold.LastTick() >= first + 2);
}

TEST_CASE("a request the client is still writing is never run") {
    Client client = Attach();
    REQUIRE(client.Read(reinterpret_cast<uint64_t>(&g_staticTarget), 4).has_value());
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, SectionName().c_str());
    REQUIRE(mapping);
    auto* section = static_cast<Section*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Section)));
    REQUIRE(section);
    std::atomic_ref requestSeq(section->requestSeq), responseSeq(section->responseSeq);
    const uint32_t handled = requestSeq.load();
    REQUIRE(responseSeq.load() == handled);
    // Mid-write: the marker stands in requestSeq while the slot holds a new request.
    requestSeq.store(RequestWriting);
    section->request.op = op::Read;
    section->request.address = reinterpret_cast<uint64_t>(&g_staticTarget);
    section->request.length = 4;
    Sleep(30); // Many agent ticks.
    CHECK(responseSeq.load() == handled); // Not run, not answered.
    requestSeq.store(handled);            // The write never finished: back to the last handled number.
    UnmapViewOfFile(section);
    CloseHandle(mapping);
    CHECK(client.Read(reinterpret_cast<uint64_t>(&g_staticTarget), 4).has_value()); // The channel still works.
}

TEST_CASE("request numbers skip the write marker") {
    CHECK(NextRequestSeq(5) == 6);
    CHECK(NextRequestSeq(UINT32_MAX) == 1);
    CHECK_FALSE(RequestPending(RequestWriting, 4));
    CHECK_FALSE(RequestPending(4, 4));
    CHECK(RequestPending(5, 4));
}
