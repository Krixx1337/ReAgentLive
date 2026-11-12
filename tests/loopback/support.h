// reagentlive loopback tests: one real agent per process, ticking on its own
// thread, and the helpers the cases share. Inline so every case file sees
// the same agent and the same test state.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "reagentlive/core/agent.h"
#include "reagentlive/core/client.h"
#include "reagentlive/ue/client_ue.h"
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/frontends/cli/syntax/parse_internal.h"
#include "reagentlive/frontends/cli/code/xrefs.h"
#include "doctest.h"

using namespace reagentlive;
using namespace std::chrono_literals;

namespace loopback {
// Test-only ops in the game range.
constexpr uint32_t OpSlow = 4096, OpThrow = 4097;
constexpr uint32_t Layout = LayoutCore + 7;

inline std::atomic<bool> g_permissive; // Readable lies "yes" to force a guarded fault.

inline bool Readable(const void* pointer, size_t bytes) {
    if (g_permissive) return true;
    auto cursor = reinterpret_cast<uintptr_t>(pointer);
    const uintptr_t end = cursor + bytes;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        cursor = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    }
    return true;
}

// Answers after `length` extra ticks; detail = ticks it saw.
inline int g_slowLeft;
inline uint64_t g_slowSeen;
inline Progress Slow(const Request& request, Response& response, bool first, const Host&) {
    if (first) { g_slowLeft = static_cast<int>(request.length); g_slowSeen = 0; }
    ++g_slowSeen;
    if (g_slowLeft-- > 0) return Progress::Continue;
    response.detail = g_slowSeen;
    return Progress::Done;
}

// Throws on its first call (length 0) or on the tick after it (length 1).
inline Progress Throw(const Request& request, Response& response, bool first, const Host&) {
    response.length = 4; // Must not reach the client with the refusal.
    if (first && request.length) return Progress::Continue;
    throw std::bad_alloc();
}

// Stands in for the Unreal pack's describe many: an even address is an object
// named after it, an odd one is gone (zeroed entry). detail = requests seen.
inline uint64_t g_describeRequests;
inline Progress FakeDescribeMany(const Request& request, Response& response, bool, const Host&) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const size_t count = request.length / sizeof(ue::Ref);
    for (size_t i = 0; i < count; ++i) {
        ue::Ref ref{};
        memcpy(&ref, request.data + i * sizeof(ref), sizeof(ref));
        ue::FindEntry entry{};
        if (!(ref.address & 1)) {
            entry.object = {ref.address, static_cast<int32_t>(i), 1};
            snprintf(entry.name, sizeof(entry.name), "Obj%llx", static_cast<unsigned long long>(ref.address));
            strcpy_s(entry.className, "Thing");
        }
        memcpy(response.data + i * sizeof(entry), &entry, sizeof(entry));
    }
    response.length = static_cast<uint32_t>(count * sizeof(ue::FindEntry));
    response.detail = ++g_describeRequests;
    return Progress::Done;
}

// A host root for watch --follow: the object g_root points at, guarded on its identity.
struct RootThing {
    uint64_t identity;
    std::atomic<uint32_t> value;
};
inline std::atomic<RootThing*> g_root;
inline bool TestRoot(const Host&, const char* name, uint64_t& address, Guard& guard) {
    RootThing* thing = g_root;
    if (strcmp(name, "thing") || !thing) return false;
    address = reinterpret_cast<uint64_t>(thing);
    guard.address = reinterpret_cast<uint64_t>(&thing->identity);
    guard.length = sizeof(thing->identity);
    memcpy(guard.bytes, &thing->identity, sizeof(thing->identity));
    return true;
}

constexpr Command Commands[] = {
    {ue::op::DescribeMany, "describe many", &FakeDescribeMany},
    {op::Read, "read", &Read},
    {op::ReadBatch, "read-batch", &ReadBatch},
    {op::Write, "write", &Write},
    {op::Modules, "modules", &Modules},
    {op::Search, "search", &Search},
    {op::Xrefs, "xrefs", &Xrefs},
    {op::Region, "region", &Region},
    {op::Pin, "pin", &Pin},
    {op::Trial, "trial", &Trial},
    {op::Watch, "watch", &Watch},
    {OpSlow, "slow", &Slow},
    {OpThrow, "throw", &Throw},
};

inline std::wstring SectionName() { return L"Local\\reagentlive-test-" + std::to_wstring(GetCurrentProcessId()); }

// One agent per process (its state is process-wide), ticking every millisecond.
struct Agent {
    std::wstring name = SectionName();
    Host host{name.c_str(), Layout, &Readable, nullptr, nullptr, HostThread::Engine};
    std::atomic<bool> paused{false}, stop{false}, background{false}, roots{true};
    std::atomic<HostThread> kind{HostThread::Engine};
    std::atomic<uint32_t> holdWait{200}, holdTick{1000};
    std::thread thread;
    Agent() {
        thread = std::thread([this] {
            while (!stop) {
                host.backgroundSearch = background;
                host.thread = kind;
                host.holdWaitMicroseconds = holdWait;
                host.holdTickMicroseconds = holdTick;
                host.rootNames = "thing\0";
                host.root = roots ? &TestRoot : nullptr;
                if (!paused) Tick(host, Commands);
                Sleep(1);
            }
        });
    }
    ~Agent() { stop = true; thread.join(); }
};
inline Agent& TheAgent() {
    static Agent agent;
    return agent;
}

inline Client Attach() {
    TheAgent();
    for (int attempt = 0; attempt < 2000; ++attempt) {
        if (auto client = Client::Attach(SectionName(), Layout)) return std::move(*client);
        Sleep(1);
    }
    FAIL("agent never opened its section");
    throw 0; // Unreachable: FAIL throws.
}

inline PinSpec PinOf(const void* target, uint32_t value, uint32_t milliseconds) {
    PinSpec spec{};
    spec.address = reinterpret_cast<uint64_t>(target);
    spec.length = 4;
    spec.milliseconds = milliseconds;
    memcpy(spec.bytes, &value, 4);
    return spec;
}
inline void GuardOn(Guard& guard, const void* where, size_t length) {
    guard.address = reinterpret_cast<uint64_t>(where);
    guard.length = static_cast<uint32_t>(length);
    memcpy(guard.bytes, where, length);
}
template<class Predicate> bool WaitFor(Predicate done, int milliseconds = 2000) {
    for (int waited = 0; waited < milliseconds; waited += 2) {
        if (done()) return true;
        Sleep(2);
    }
    return done();
}

// A heap owner (trials refuse targets inside loaded modules): an identity the
// guard checks, a value inside it, and a native child object whose first 8
// bytes point into this test exe like a vtable.
struct TrialOwner {
    uint64_t identity = 0x5151;
    std::atomic<uint32_t> value{7};
    uint32_t pad = 0;
    struct Native { uint64_t vtable; std::atomic<uint32_t> field; } native{reinterpret_cast<uint64_t>(&Readable), 3};
};
inline TrialSpec TrialOf(TrialOwner& owner, std::atomic<uint32_t>& target, uint32_t value, uint32_t expected,
                  uint32_t milliseconds, bool native = false) {
    TrialSpec spec{};
    spec.address = reinterpret_cast<uint64_t>(&target);
    spec.length = 4;
    spec.milliseconds = milliseconds;
    memcpy(spec.bytes, &value, 4);
    memcpy(spec.expected, &expected, 4);
    GuardOn(spec.owner, &owner.identity, sizeof(owner.identity));
    spec.ownerAddress = reinterpret_cast<uint64_t>(&owner);
    spec.ownerSize = sizeof(TrialOwner);
    if (native) spec.object = reinterpret_cast<uint64_t>(&owner.native);
    return spec;
}
inline std::optional<TrialStop> TrialStopOf(Client& client, uint32_t id) {
    auto list = client.TrialList();
    if (!list) return std::nullopt;
    for (const auto& trial : *list) if (trial.id == id) return trial.stop;
    return std::nullopt;
}
inline uint32_t g_staticTarget = 7; // Inside the test exe's image.

inline uint64_t ReadTick(Client& client, const uint8_t* buffer) {
    Request request{};
    request.op = op::Read;
    request.address = reinterpret_cast<uint64_t>(buffer);
    request.length = 4;
    const auto response = client.Send(request);
    REQUIRE(response.has_value());
    REQUIRE(response->status == Status::Ok);
    return response->tick;
}
// Restores the agent's hold budgets when a test changes them.
struct HoldBudget {
    HoldBudget(uint32_t wait, uint32_t tick) { TheAgent().holdWait = wait; TheAgent().holdTick = tick; Sleep(5); }
    ~HoldBudget() { TheAgent().holdWait = 200; TheAgent().holdTick = 1000; Sleep(5); }
};
}
using namespace loopback;
