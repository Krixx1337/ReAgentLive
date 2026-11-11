// reagentlive own-thread example, end to end: the real DLL loaded into this
// process, its thread ticking the core, a real client attached by pid.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstring>
#include <string>
#include "reagentlive/core/client.h"
#include "reagentlive/ue/protocol_ue.h"
#include "doctest.h"

using namespace reagentlive;
using namespace std::chrono_literals;

namespace {
const uint64_t g_known = 0x1122334455667788ull;
std::atomic<uint32_t> g_target{7};

template<class Predicate> bool WaitFor(Predicate done, int milliseconds = 2000) {
    for (int waited = 0; waited < milliseconds; waited += 2) {
        if (done()) return true;
        Sleep(2);
    }
    return done();
}

// Loads the DLL once; its thread opens the section on its first tick.
Client Attach() {
    static const HMODULE dll = LoadLibraryW(REAGENTLIVE_OWN_THREAD_DLL);
    REQUIRE(dll != nullptr);
    const std::wstring name = L"Local\\reagentlive-" + std::to_wstring(GetCurrentProcessId());
    Result<Client> client = std::unexpected(Error{});
    WaitFor([&] { return (client = Client::Attach(name, LayoutCore)).has_value(); });
    REQUIRE(client);
    return std::move(*client);
}

PinSpec PinOf(const void* target, uint32_t value, uint32_t milliseconds) {
    PinSpec spec{};
    spec.address = reinterpret_cast<uint64_t>(target);
    spec.length = 4;
    spec.milliseconds = milliseconds;
    memcpy(spec.bytes, &value, 4);
    return spec;
}
}

TEST_CASE("the own-thread host answers in any process") {
    Client client = Attach();
    CHECK(client.AgentPid() == GetCurrentProcessId());
    CHECK(client.AgentHostThread() == HostThread::Own);
    const uint64_t beat = client.Heartbeat();
    CHECK(WaitFor([&] { return client.Heartbeat() > beat + 2; }));

    auto bytes = client.Read(reinterpret_cast<uint64_t>(&g_known), 8);
    REQUIRE(bytes);
    uint64_t read = 0;
    memcpy(&read, bytes->data(), 8);
    CHECK(read == g_known);

    auto modules = client.Modules();
    REQUIRE(modules);
    CHECK(!modules->empty());
}

TEST_CASE("the own-thread host writes only over expected bytes") {
    Client client = Attach();
    g_target = 7;
    const uint32_t wrong = 9, expected = 7, value = 42;
    const auto as = [](const uint32_t& v) { return std::span(reinterpret_cast<const uint8_t*>(&v), 4); };
    CHECK(!client.WriteIf(reinterpret_cast<uint64_t>(&g_target), as(value), as(wrong)));
    CHECK(g_target == 7u);
    CHECK(client.WriteIf(reinterpret_cast<uint64_t>(&g_target), as(value), as(expected)));
    CHECK(g_target == 42u);
}

TEST_CASE("the own-thread host pins until the pin expires") {
    Client client = Attach();
    REQUIRE(client.PinClear());
    g_target = 7;
    auto pin = client.PinAdd(PinOf(&g_target, 42, 200));
    REQUIRE(pin);
    g_target = 1;
    CHECK(WaitFor([&] { return g_target == 42u; }));
    CHECK(WaitFor([&] {
        auto list = client.PinList();
        return list && list->size() == 1 && (*list)[0].stop == PinStop::Expired;
    }));
    g_target = 5;
    Sleep(20);
    CHECK(g_target == 5u);
    CHECK(client.PinClear(pin->id).value_or(0) == 1);
}

TEST_CASE("the own-thread host has no engine pack") {
    Client client = Attach();
    CHECK(client.Supports(op::Read));
    CHECK(!client.Supports(ue::op::Find));
    Request request{};
    request.op = ue::op::Find;
    auto response = client.Send(request);
    REQUIRE(response);
    CHECK(response->status == Status::Unsupported);
}
