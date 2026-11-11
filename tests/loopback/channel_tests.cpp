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

TEST_CASE("a second client is refused as busy until the first detaches") {
    {
        Client first = Attach();
        const auto second = Client::Attach(SectionName(), Layout);
        REQUIRE_FALSE(second.has_value());
        CHECK(second.error().kind == Error::Kind::Busy);
        Client moved = std::move(first); // Moving keeps the hold.
        CHECK_FALSE(Client::Attach(SectionName(), Layout).has_value());
    }
    CHECK(Client::Attach(SectionName(), Layout).has_value());
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
