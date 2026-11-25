// reagentlive Unreal example: one DLL for any UE5 game, with no dump and no
// mod. On load it starts one thread that ticks the agent with the whole
// Unreal pack (reagentlive::ue-base: the stock UE5 layout, each group checked
// on known objects before its ops run). Reads answer from that thread under
// fault guards; `game-thread on` moves the tick to the game viewport's Draw
// (reagentlive::ue-draw), where engine calls work. It is never unloaded.
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
#include "reagentlive/packs/ue/agent_ue.h"
#include "reagentlive/packs/ue/host_draw.h"

namespace {
using namespace reagentlive;

// Section name: SectionPrefix + pid; the example's console looks for it.
constexpr const wchar_t* SectionPrefix = L"Local\\reagentlive-ue-";
constexpr uint32_t Layout = LayoutCore + ue::LayoutPart;
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
    {op::ImageCopy, "image copy", &ImageCopy, false},
    {op::Pin, "pin", &Pin},
    {op::Trial, "trial", &Trial},
    {op::Watch, "watch", &Watch, false},
    {ue::op::Find, "find", &ue::Find, false},
    {ue::op::ReadObject, "read object", &ue::ReadObject, false},
    {ue::op::Describe, "describe", &ue::Describe, false},
    {ue::op::DescribeMany, "describe many", &ue::DescribeMany, false},
    {ue::op::Names, "names", &ue::Names, false},
    {ue::op::Census, "census", &ue::Census, false},
    {ue::op::NameFind, "name-find", &ue::NameFind, false},
    {ue::op::Fields, "fields", &ue::Fields, false},
    {ue::op::Function, "func", &ue::Function, false},
    {ue::op::Functions, "funcs", &ue::Functions, false},
    {ue::op::Entries, "entries", &ue::Entries, false},
    {ue::op::PreviewArray, "preview array", &ue::PreviewArray, false},
    {ue::op::Enum, "enum", &ue::Enum, false},
    // Engine ops: answered only while the agent ticks on Draw.
    {ue::op::Call, "call", &ue::Call},
    {ue::op::Parameters, "params", &ue::Fields, false},
    {ue::op::Each, "each", &ue::Each, false},
    {ue::op::Script, "script", &ue::Script, false},
    {ue::op::Refs, "refs", &ue::Refs, false},
    {ue::op::Owner, "owner", &ue::Owner, false},
    {ue::op::Route, "route", &ue::Route, false},
    {ue::op::ExecEntries, "exec entries", &ue::ExecEntries, false},
    {ue::op::CallsStart, "calls-start", &ue::CallsStart},
    {op::NativeCallsStart, "native-calls-start", &NativeCallsStart},
    {op::RecorderRead, "calls-read", &RecorderRead, false},
    {op::RecorderStop, "calls-stop", &RecorderStop},
    {ue::op::GameThread, "game-thread", &ue::draw::GameThread},
};

wchar_t g_section[64];
// One tick at a time: this thread and Draw (the game thread) both tick.
volatile LONG g_ticking;

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

bool NotGameThread() { return false; }

void TickAgent(bool onDraw) {
    if (InterlockedCompareExchange(&g_ticking, 1, 0)) return;
    ue::Context pack{&ue::ObjectInTable, onDraw ? &ue::draw::OnGameThread : &NotGameThread};
    pack.offThreadReads = true;
    const Host host{.sectionName = g_section,
                    .layout = Layout,
                    .readable = &Readable,
                    .log = &Log,
                    .context = &pack,
                    .thread = onDraw ? HostThread::Engine : HostThread::Own,
                    .backgroundSearch = true};
    Tick(host, Commands);
    InterlockedExchange(&g_ticking, 0);
}

void TickOnDraw() { TickAgent(true); }

DWORD WINAPI Run(void*) {
    std::swprintf(g_section, std::size(g_section), L"%ls%lu", SectionPrefix, GetCurrentProcessId());
    ue::draw::Configure(&TickOnDraw, &Log);
    // A high-resolution waitable timer gives short ticks without raising the
    // process's timer resolution; Sleep is the fallback.
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    for (;;) {
        ue::draw::Poll();
        if (!ue::draw::Live()) TickAgent(false); // Draw ticks while it is live.
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
