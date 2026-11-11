// reagentlive own-thread host: the fast way in. An ordinary DLL with no game
// code and no engine pack: on load it starts one thread that ticks the core
// every couple of milliseconds, so it works in any x64 process unchanged.
//
// What it gives: every memory command and what the console builds on them
// (read, write, search, pin, watch, peek, disasm, xrefs, sig, vtable, region).
// What it does not: object commands and engine calls (no pack), or tick
// accuracy (its thread is not the game's, so a read can be torn and a write
// can race the game). A real host ticks on the engine's thread instead
// (docs/hosting.md). It is never unloaded.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <iterator>
#include "reagentlive/core/agent.h"

namespace {
using namespace reagentlive;

// Section name: SectionPrefix + pid. The generic console looks for this prefix.
constexpr const wchar_t* SectionPrefix = L"Local\\reagentlive-";
constexpr DWORD TickMilliseconds = 2;

// Read-only ops skip success logs: a log line runs inside the tick.
constexpr Command Commands[] = {
    {op::Read, "read", &Read, false},
    {op::ReadBatch, "read-batch", &ReadBatch, false},
    {op::Write, "write", &Write},
    {op::Modules, "modules", &Modules, false},
    {op::Search, "search", &Search},
    {op::Xrefs, "xrefs", &Xrefs, false},
    {op::Region, "region", &Region, false},
    {op::Pin, "pin", &Pin},
    {op::Trial, "trial", &Trial},
    {op::Watch, "watch", &Watch, false},
};

wchar_t g_section[64];

bool Readable(const void* pointer, size_t bytes) {
    auto cursor = reinterpret_cast<uintptr_t>(pointer);
    const uintptr_t end = cursor + bytes;
    if (end < cursor) return false;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !info.Protect) return false;
        cursor = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    }
    return true;
}

void Log(LogLevel level, unsigned status, const char* text) {
    char line[512];
    std::snprintf(line, sizeof(line), "reagentlive %s [%u] %s\n", level == LogLevel::Warning ? "warning" : "info",
                  status, text);
    OutputDebugStringA(line);
}

DWORD WINAPI Run(void*) {
    std::swprintf(g_section, std::size(g_section), L"%ls%lu", SectionPrefix, GetCurrentProcessId());
    const Host host{.sectionName = g_section,
                    .layout = LayoutCore,
                    .readable = &Readable,
                    .log = &Log,
                    .context = nullptr,
                    .thread = HostThread::Own,
                    .backgroundSearch = true};
    // A high-resolution waitable timer gives short ticks without raising the
    // process's timer resolution; Sleep is the fallback.
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    for (;;) {
        Tick(host, Commands);
        if (timer) {
            LARGE_INTEGER due{};
            due.QuadPart = -static_cast<LONGLONG>(TickMilliseconds) * 10'000;
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer, INFINITE);
                continue;
            }
        }
        Sleep(TickMilliseconds);
    }
}
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(instance);
    // Pinned: the thread runs this image until the process exits.
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&Run), &self);
    if (HANDLE thread = CreateThread(nullptr, 0, &Run, nullptr, 0, nullptr)) CloseHandle(thread);
    return TRUE;
}
