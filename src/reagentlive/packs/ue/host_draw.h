// reagentlive Unreal pack, for hosts (reagentlive::ue-draw): a game-thread
// tick through the game viewport's Draw, so a host that ticks elsewhere (its
// own thread, a frame hook) can still make engine calls. Off until asked:
// the `game-thread on` command (op GameThread) or the host's own Arm().
//
// No per-build addresses: the viewport client is found with
// FirstLiveInstance, and its Draw interface (UObject +0x28, Draw at entry 4,
// stock UE5) gets a private copy of its vtable with that entry swapped. One
// pointer in the viewport object is written; the game's code and tables are
// not. The hook forwards every call first, then ticks the host only for that
// viewport and only on the process's first thread (Unreal's game thread on
// Windows). Confirm entry 4 per game before the first `on` (the skill's
// references/unreal/new-game.md, "Calls in a new game").
#pragma once
#include <stdint.h>
#include "reagentlive/core/agent.h"
#include "reagentlive/packs/ue/protocol_ue.h"

namespace reagentlive::ue::draw {
using TickFn = void (*)();
using LogFn = void (*)(LogLevel level, unsigned status, const char* text);

// Once, before Poll: the host's tick (called from Draw on the game thread,
// at most once per frame) and where state changes are logged (may be null).
void Configure(TickFn tick, LogFn log);
// Arms the install; Poll does it. Clears a refused install, so it is tried again.
void Arm();
// Every host tick, before the agent ticks, on the host's thread: installs
// once armed and the viewport is found (the pack must have located the
// object table: any Unreal command does). Cheap when there is nothing to do.
void Poll();
// A Draw tick ran in the last 500 ms: the host should not tick elsewhere.
bool Live();
// Inside the Draw tick on the game thread: the host's Context::gameThread.
bool OnGameThread();
// Puts the viewport's table back if it still holds the copy; Arm arms
// again. The copy stays allocated: a Draw inside the hook keeps forwarding.
void Disable();
// Disable, for good: the host is stopping.
void Shutdown();
GameThreadState State();

// The GameThread op, for the host's command table.
Progress GameThread(const Request&, Response&, bool first, const Host&);
}
