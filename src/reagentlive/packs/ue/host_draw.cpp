// reagentlive Unreal pack: the Draw tick for hosts (host_draw.h).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <cstring>
#include "reagentlive/packs/ue/host_draw.h"
#include "reagentlive/core/vtable_copy.h"
#include "reagentlive/packs/ue/agent_ue.h"
#include "reagentlive/packs/ue/request_ue.h"

namespace reagentlive::ue::draw {
namespace {
using DrawFn = void (*)(void* self, void* viewport, void* canvas);
// Stock UE5 (engine_layout.h): the viewport client's Draw interface and Draw's entry.
constexpr uint32_t InterfaceOffset = layout::ViewportInterfaceOffset, DrawIndex = layout::ViewportDrawIndex;
// A Draw tick this recent keeps the host's other tick out (ms).
constexpr uint64_t LiveMilliseconds = 500;

volatile LONG g_mode; // GameThreadMode.
volatile LONG g_armed, g_shutdown, g_inTick;
volatile LONG64 g_lastTick;
TickFn g_tick;
LogFn g_log;
DWORD g_gameThread;
void** g_interface;   // viewport + InterfaceOffset: where the vtable pointer lives.
void** g_table;       // The game's vtable.
vtable::Entry g_entry; // Draw in the table copy, while on.
DrawFn g_original;
const void* g_viewport;
const char* g_text = "off: the host does not tick on the game thread";
uint64_t g_imageBase, g_imageEnd;

GameThreadMode Mode() { return static_cast<GameThreadMode>(InterlockedCompareExchange(&g_mode, 0, 0)); }

bool InImage(const void* pointer) {
    const auto value = reinterpret_cast<uint64_t>(pointer);
    return value >= g_imageBase && value < g_imageEnd;
}

void Enter(GameThreadMode mode, const char* text, unsigned status = 0) {
    g_text = text;
    if (InterlockedExchange(&g_mode, static_cast<LONG>(mode)) == static_cast<LONG>(mode)) return;
    if (g_log) g_log(mode == GameThreadMode::Conflict ? LogLevel::Warning : LogLevel::Info, status, text);
}

// Unreal runs its game loop on the process's first thread (GuardedMain on
// the WinMain thread): the earliest-created thread of this process.
DWORD FirstThread() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    const DWORD process = GetCurrentProcessId();
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    DWORD best = 0;
    ULONGLONG bestTime = ~0ull;
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != process) continue;
        const HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
        if (!thread) continue;
        FILETIME created{}, exited{}, kernel{}, user{};
        if (GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
            const ULONGLONG time = (ULONGLONG{created.dwHighDateTime} << 32) | created.dwLowDateTime;
            if (time < bestTime) {
                bestTime = time;
                best = entry.th32ThreadID;
            }
        }
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
    return best;
}

void OnDraw(void* self, void* viewport, void* canvas) {
    g_original(self, viewport, canvas);
    if (Mode() != GameThreadMode::On || self != g_interface || GetCurrentThreadId() != g_gameThread) return;
    InterlockedExchange64(&g_lastTick, static_cast<LONG64>(GetTickCount64()));
    InterlockedExchange(&g_inTick, 1);
    if (g_tick) g_tick();
    InterlockedExchange(&g_inTick, 0);
}

void Install() {
    const auto* viewport = reinterpret_cast<const uint8_t*>(FirstLiveInstance("GameViewportClient"));
    if (!viewport) return Enter(GameThreadMode::Waiting, "waiting for the game viewport (any Unreal command locates the engine)");
    g_viewport = viewport;
    auto* object = const_cast<uint8_t*>(viewport);
    void** table = nullptr;
    const bool copied = GuardedCopy(&table, object + InterfaceOffset, sizeof(table)) &&
        vtable::InCopy(reinterpret_cast<uintptr_t>(table));
    if (!copied && (!InImage(table) || !InImage(table - 1)))
        return Enter(GameThreadMode::Conflict, "the viewport's interface table is not in the game's image; not installed");
    // A private copy of the table (core/vtable_copy.h) with the viewport's
    // interface pointed at it (or the copy another entry already uses); Draw
    // is then swapped in the copy.
    vtable::Entry entry{};
    switch (vtable::Take(object, InterfaceOffset, DrawIndex, entry)) {
    case vtable::Taken::Ok: break;
    case vtable::Taken::Foreign:
        return Enter(GameThreadMode::Conflict, "the viewport's interface table is not in the game's image; not installed");
    case vtable::Taken::NotCode:
        return Enter(GameThreadMode::Conflict, "the interface table has no Draw entry in the game's code; not installed");
    case vtable::Taken::Unreadable:
        return Enter(GameThreadMode::Conflict, "the viewport's interface table is unreadable; not installed");
    case vtable::Taken::TooLong:
        return Enter(GameThreadMode::Conflict, "the interface table is longer than its copy can hold; not installed");
    case vtable::Taken::Busy:
        return Enter(GameThreadMode::Conflict, "the viewport's Draw entry is taken already; not installed");
    case vtable::Taken::Changed:
        return Enter(GameThreadMode::Conflict, "the viewport's table changed while installing; not installed");
    case vtable::Taken::Full: return Enter(GameThreadMode::Conflict, "no table copy free; not installed");
    case vtable::Taken::NoMemory: return Enter(GameThreadMode::Conflict, "no memory for the table copy");
    }
    g_table = entry.table;
    if (!InImage(entry.table) || !InImage(entry.table - 1)) {
        vtable::Give(entry);
        return Enter(GameThreadMode::Conflict, "the viewport's interface table is not in the game's image; not installed");
    }
    g_original = reinterpret_cast<DrawFn>(entry.original);
    g_interface = reinterpret_cast<void**>(object + InterfaceOffset);
    g_entry = entry;
    // On before the swap: the first Draw through the copy may come at once.
    Enter(GameThreadMode::On, "on: the host ticks on the game thread (Draw); engine calls work", g_gameThread);
    if (!vtable::Swap(entry, reinterpret_cast<void*>(&OnDraw))) {
        vtable::Give(entry);
        return Enter(GameThreadMode::Conflict, "the viewport's Draw entry changed while installing; not installed");
    }
}
}

void Configure(TickFn tick, LogFn log) {
    g_tick = tick;
    g_log = log;
}

void Arm() {
    if (InterlockedCompareExchange(&g_shutdown, 0, 0)) return;
    const auto mode = Mode();
    InterlockedExchange(&g_armed, 1);
    if (mode != GameThreadMode::On && mode != GameThreadMode::Waiting)
        Enter(GameThreadMode::Waiting, "armed: the host installs on its next tick");
}

void Poll() {
    if (!InterlockedCompareExchange(&g_armed, 0, 0) || InterlockedCompareExchange(&g_shutdown, 0, 0)) return;
    const auto mode = Mode();
    if (mode == GameThreadMode::On || mode == GameThreadMode::Conflict || mode == GameThreadMode::Stopped) return;
    if (!g_imageBase) {
        const auto* image = reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleW(nullptr));
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(reinterpret_cast<const uint8_t*>(image) + image->e_lfanew);
        g_imageBase = reinterpret_cast<uint64_t>(image);
        g_imageEnd = g_imageBase + nt->OptionalHeader.SizeOfImage;
    }
    if (!g_gameThread && !(g_gameThread = FirstThread()))
        return Enter(GameThreadMode::Conflict, "the process's first thread is unknown; not installed");
    Install();
}

bool Live() {
    const auto last = static_cast<uint64_t>(InterlockedCompareExchange64(&g_lastTick, 0, 0));
    return last && Mode() == GameThreadMode::On && GetTickCount64() - last < LiveMilliseconds;
}

bool OnGameThread() {
    return InterlockedCompareExchange(&g_inTick, 0, 0) && GetCurrentThreadId() == g_gameThread;
}

void Disable() {
    InterlockedExchange(&g_armed, 0);
    if (Mode() != GameThreadMode::On) {
        if (Mode() == GameThreadMode::Waiting) Enter(GameThreadMode::Off, "off: the host does not tick on the game thread");
        return;
    }
    // Draw back in the copy, then the game's table back only if the pointer
    // still holds the copy and nothing else is swapped in it.
    vtable::Unswap(g_entry, reinterpret_cast<void*>(&OnDraw));
    switch (vtable::Give(g_entry)) {
    case vtable::Given::Restored: Enter(GameThreadMode::Stopped, "off: the viewport's table restored"); break;
    case vtable::Given::Shared: Enter(GameThreadMode::Stopped, "off: Draw restored; the copy stays for its other entries"); break;
    case vtable::Given::Left:
        Enter(GameThreadMode::Stopped, "off: the viewport's table was replaced by someone else; left alone");
        break;
    }
    InterlockedExchange64(&g_lastTick, 0);
}

void Shutdown() {
    InterlockedExchange(&g_shutdown, 1);
    Disable();
}

GameThreadState State() {
    GameThreadState state{};
    state.mode = static_cast<uint32_t>(Mode());
    state.thread = g_gameThread;
    state.viewport = reinterpret_cast<uint64_t>(g_viewport);
    state.table = reinterpret_cast<uint64_t>(g_table);
    state.draw = reinterpret_cast<uint64_t>(g_original);
    const char* text = g_text;
    strncpy_s(state.text, text, _TRUNCATE);
    return state;
}

Progress GameThread(const Request& request, Response& response, bool, const Host&) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    switch (static_cast<GameThreadAction>(request.flags)) {
    case GameThreadAction::On: Arm(); break;
    case GameThreadAction::Off: Disable(); break;
    case GameThreadAction::Status: break;
    }
    const GameThreadState state = State();
    memcpy(response.data, &state, sizeof(state));
    response.length = sizeof(state);
    return Progress::Done;
}
}
