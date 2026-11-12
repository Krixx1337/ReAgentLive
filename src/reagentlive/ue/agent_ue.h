// reagentlive Unreal pack: find, fields and call handlers over a Dumper-7 SDK.
#pragma once
#include "reagentlive/core/agent.h"
#include "reagentlive/ue/protocol_ue.h"
namespace SDK { class UObject; }

// Compiled inside the host against the host's Dumper-7 SDK (SDK/ include path).
// Every handler reads engine state, so every one first asks the host's
// gameThread check; a context without one is refused, never assumed to be
// on the right thread.
//
// From the SDK the pack takes engine layouts only (object, class, field and
// table types), never addresses: it finds the object table and the name pool
// at run time (globals.h) and checks them before its first command, so a
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
    // fields, object reads, container entries, names) when gameThread()
    // says no, as on a borrowed thread. Best effort: each runs under a
    // fault guard, an object's identity is checked before and after its
    // read, and values may still be torn. Calls and scans that run engine
    // code stay refused.
    bool offThreadReads = false;
};

// An object check for hosts without their own (Context::object): a readable
// header whose index names its own slot in the located object table. False
// until the pack has located the table.
bool ObjectInTable(const SDK::UObject* object);

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
// Every native function's exec entry (multi-tick scan, cached pages).
Progress ExecEntries(const Request&, Response&, bool first, const Host&);
}
