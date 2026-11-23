// reagentlive Unreal pack: lookup, reflection, container and call handlers.
#pragma once
#include "reagentlive/core/agent.h"
#include "reagentlive/packs/ue/protocol_ue.h"
namespace SDK { class UObject; }

// Compiled inside the host. Engine types are read at the offsets of the
// engine layout (engine_layout.h): the host's Dumper-7 SDK with
// reagentlive::ue, or the stock UE5 layout with reagentlive::ue-base, each
// layout group then checked on known objects before its ops run. Every
// handler reads engine state, so every one first asks the host's gameThread
// check; a context without one is refused, never assumed to be on the right
// thread.
//
// Never addresses from the SDK: the pack finds the object table and the name
// pool at run time (globals.h) and checks them before its first command, so a
// stale SDK after a game patch costs nothing but a scan.
namespace reagentlive::ue {
struct Context {
    bool (*object)(const SDK::UObject*);   // Required: readable object with a sane class/table slot.
    // Required: true on the engine's thread. A host that ticks there passes
    // a check that says so (one line), which makes the claim explicit.
    bool (*gameThread)();
    // Optional starting guesses (absolute addresses), usually the SDK's
    // offsets. Checked like a scan hit; a wrong one only costs the scan.
    uintptr_t objectsGuess = 0, namesGuess = 0;
    // Optional: the image whose writable sections are scanned when a guess
    // fails; 0 is the process's exe.
    uintptr_t image = 0;
    // Opt-in, off by default: answer lookups and reads (find, describe,
    // census, names, object reads, field tables, entries, enum, func, funcs) when
    // gameThread() says no, as on a borrowed thread. Best effort: each runs
    // under a fault guard, an object's identity is checked before and after
    // its read, and values may still be torn. Calls, params, each, script,
    // name-refs, exec entries and the recorder stay refused (docs/commands/unreal.md).
    bool offThreadReads = false;
    // Optional: microseconds of each tick an object-table scan (find with `cls`,
    // census, each, name-refs, exec entries) may spend; 0 is 1000. More is fewer
    // ticks per scan and a longer stall in each; at most MaxScanMicroseconds.
    uint32_t scanMicroseconds = 0;
};
constexpr uint32_t MaxScanMicroseconds = 50000;

// An object check for hosts without their own (Context::object): a readable
// header whose index names its own slot in the located object table. False
// until the pack has located the table.
bool ObjectInTable(const SDK::UObject* object);

// For hosts: the first live instance (no class default or archetype object)
// of a class or a subclass, by class name. Null until the pack has located
// the object table (its first command does), or when there is none. Walks
// the whole table on the calling thread: call it rarely, from the thread
// that ticks the agent.
const SDK::UObject* FirstLiveInstance(std::string_view className);

// Why a pack handler may not run now, or nullptr when it may.
inline const char* EngineThreadProblem(const Context& pack) {
    if (!pack.gameThread) return "host gave no engine-thread check";
    if (!pack.gameThread()) return "not on the game thread";
    return nullptr;
}

// For the host's command table. Host::context must point to a Context.
Progress Find(const Request&, Response&, bool first, const Host&);
Progress Call(const Request&, Response&, bool first, const Host&);
Progress Fields(const Request&, Response&, bool first, const Host&);
Progress ReadObject(const Request&, Response&, bool first, const Host&);
Progress Describe(const Request&, Response&, bool first, const Host&);
Progress DescribeMany(const Request&, Response&, bool first, const Host&);
Progress Function(const Request&, Response&, bool first, const Host&);
Progress Functions(const Request&, Response&, bool first, const Host&);
Progress PreviewArray(const Request&, Response&, bool first, const Host&);
Progress Names(const Request&, Response&, bool first, const Host&);
Progress Census(const Request&, Response&, bool first, const Host&);
Progress NameFind(const Request&, Response&, bool first, const Host&);
Progress Each(const Request&, Response&, bool first, const Host&);
// Blueprint bytecode: one function's confirmed names, calls and properties;
// every function whose bytecode names an FName (multi-tick scan).
Progress Script(const Request&, Response&, bool first, const Host&);
Progress Refs(const Request&, Response&, bool first, const Host&);
// One page of an array, set or map field's elements (header and elements in one tick).
Progress Entries(const Request&, Response&, bool first, const Host&);
// An enum's names and values, one page.
Progress Enum(const Request&, Response&, bool first, const Host&);
// Every native function's exec entry (multi-tick scan, cached pages).
Progress ExecEntries(const Request&, Response&, bool first, const Host&);
// The call recorder (protocol_ue.h): opt-in by listing these ops. Each
// recording swaps one UFunction's native pointer (heap data) for a fixed
// thunk and puts it back; nothing in code, vtables or imports is written.
Progress CallsStart(const Request&, Response&, bool first, const Host&);
Progress CallsRead(const Request&, Response&, bool first, const Host&);
Progress CallsStop(const Request&, Response&, bool first, const Host&);
// For a host that lists them: restore recordings past their time (game
// thread, once per tick is plenty), and restore all before the host stops.
// A pointer replaced by something else since is left alone; its thunk keeps
// forwarding, so the host's image must stay loaded (it never unloads).
void SweepCallRecordings();
void StopCallRecordings();
}
