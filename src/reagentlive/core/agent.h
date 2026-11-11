// reagentlive core agent: runs client requests inside the target process.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <span>
#include "reagentlive/core/protocol.h"

// The agent never hooks and never starts threads: the host calls Tick() from a
// thread it trusts (for engines, usually the game thread). Constant-initialised
// state and fixed buffers only, so it works in mapped images without static TLS
// or dynamic initialisation.
namespace reagentlive {
enum class LogLevel { Info, Warning };
struct Host;

// Done: the response is sent. Continue: call again next tick with first=false
// (long scans); a newer request abandons it.
enum class Progress { Done, Continue };
using Handler = Progress (*)(const Request& request, Response& response, bool first, const Host& host);
struct Command { uint32_t op; const char* name; Handler run; bool logSuccess = true; };

struct Host {
    const wchar_t* sectionName;               // e.g. L"Local\\mygame-dev-<pid>"
    uint32_t layout;                          // LayoutCore + each pack's LayoutPart
    bool (*readable)(const void*, size_t);    // Required: page preflight for reads.
    void (*log)(LogLevel, unsigned status, const char* text); // Optional.
    void* context;                            // For pack handlers.
    // Required: the thread Tick runs on (protocol.h). The agent refuses to
    // open while it is Unset, so the claim is always deliberate. Clients
    // read it from the section; nothing checks it is true.
    HostThread thread = HostThread::Unset;
    // Opt-in: lets `search --background` run on one agent-owned worker thread
    // (started on first use, kept for the process). Off: the agent never
    // starts threads and such searches are refused.
    bool backgroundSearch = false;
    // Hold (Request::hold): after answering a held request the agent spins
    // on this thread for up to holdWaitMicroseconds for the next request and
    // runs it in the same tick, until holdTickMicroseconds (waits and
    // handlers after the tick's first request) are spent. Zero turns it off.
    uint32_t holdWaitMicroseconds = 200;
    uint32_t holdTickMicroseconds = 1000;
    // Optional: named roots a watch can follow across replacements (a
    // respawned player object). The core knows no names. `rootNames` lists
    // them, each NUL-terminated, ending with an empty one ("pawn\0\0");
    // `root` gives the named object's current address and an identity guard
    // for it, false while there is none. Called on the Tick thread.
    const char* rootNames = nullptr;
    bool (*root)(const Host& host, const char* name, uint64_t& address, Guard& guard) = nullptr;
};

// Opens the section on the first call (once; a failure is final). Afterwards
// one compare per tick while idle. `commands`: core handlers plus packs and
// game ops, a constant array owned by the host.
void Tick(const Host& host, std::span<const Command> commands);

// memcpy that catches access faults only (a preflighted range can still be
// freed before the copy); false on a fault. For packs and hosts too.
bool GuardedCopy(void* destination, const void* source, size_t bytes);

// Core handlers, for the host's command table.
Progress Read(const Request&, Response&, bool first, const Host&);
Progress ReadBatch(const Request&, Response&, bool first, const Host&);
Progress Write(const Request&, Response&, bool first, const Host&);
Progress Modules(const Request&, Response&, bool first, const Host&);
// Pin and Watch also do work at the start of every Tick (writes, samples),
// whether or not a request is in flight.
Progress Pin(const Request&, Response&, bool first, const Host&);
Progress Watch(const Request&, Response&, bool first, const Host&);
// A write undone by the agent on expiry or clear, guarded every tick (see
// protocol.h). Checked at the start of every Tick like pins.
Progress Trial(const Request&, Response&, bool first, const Host&);
// Multi-tick: scans for at most SearchSliceMicroseconds per host tick, or,
// with SearchBackground and a host that allows it, on the worker thread
// while the host tick only polls.
Progress Search(const Request&, Response&, bool first, const Host&);
constexpr int64_t SearchSliceMicroseconds = 2000;
// What the memory at an address is (protocol.h): one VirtualQuery.
Progress Region(const Request&, Response&, bool first, const Host&);
// Relative references to an address (protocol.h): Search's machinery with a
// computed compare at every byte.
Progress Xrefs(const Request&, Response&, bool first, const Host&);
}
