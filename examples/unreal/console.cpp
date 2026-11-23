// reagentlive: the console for the Unreal example's host (host.cpp): the
// generic console with the Unreal pack's layout and the example's section
// prefix. With no pid it attaches to the one process that has this agent;
// with none or several it answers help and usage only.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/core/protocol.h"
#include "reagentlive/packs/ue/protocol_ue.h"
#include "reagentlive/frontends/cli/console/console.h"

namespace {
constexpr const wchar_t* SectionPrefix = L"Local\\reagentlive-ue-";
constexpr uint32_t Layout = reagentlive::LayoutCore + reagentlive::ue::LayoutPart;

bool IsNumber(std::wstring_view text) {
    return !text.empty() && std::ranges::all_of(text, [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}

// True when the arguments already name a pid. The values of
// the console's flags are skipped: a session id may be all digits.
bool HasTarget(int argc, wchar_t** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        if (arg == L"-c" || arg == L"--names" || arg == L"--caller" || arg == L"--session") { ++i; continue; }
        if (IsNumber(arg)) return true;
    }
    return false;
}

// Processes whose Unreal example agent has opened its section.
std::vector<DWORD> AgentProcesses() {
    std::vector<DWORD> found;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return found;
    PROCESSENTRY32W entry{sizeof(entry)};
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        const std::wstring name = SectionPrefix + std::to_wstring(entry.th32ProcessID);
        if (HANDLE section = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str())) {
            CloseHandle(section);
            found.push_back(entry.th32ProcessID);
        }
    }
    CloseHandle(snapshot);
    return found;
}
}

int wmain(int argc, wchar_t** argv) {
    std::vector<wchar_t*> args(argv, argv + argc);
    std::wstring pid;
    if (!HasTarget(argc, argv)) {
        // With several, none is picked: the console still answers help and
        // usage, and refuses the rest until a pid is given.
        const auto processes = AgentProcesses();
        if (processes.size() > 1) {
            std::fprintf(stderr, "Several processes have an agent; pass one pid:");
            for (const DWORD process : processes) std::fprintf(stderr, " %lu", process);
            std::fprintf(stderr, "\n");
        }
        if (processes.size() == 1) {
            pid = std::to_wstring(processes[0]);
            args.insert(args.begin() + 1, pid.data());
        }
    }
    return reagentlive::cli::RunConsole({SectionPrefix, L"", Layout}, static_cast<int>(args.size()),
                                        args.data());
}
