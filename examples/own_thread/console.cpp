// reagentlive: the generic console for the own-thread host (host.cpp). Core
// layout only, so Unreal commands are refused by the agent's op list. With no
// pid it attaches to the one process that has an own-thread agent.
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
#include "reagentlive/frontends/cli/console/console.h"

namespace {
constexpr const wchar_t* SectionPrefix = L"Local\\reagentlive-";

bool IsNumber(std::wstring_view text) {
    return !text.empty() && std::ranges::all_of(text, [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}

// True when the arguments already name a pid or replay a file.
bool HasTarget(int argc, wchar_t** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        if (arg == L"-c" || arg == L"--names") { ++i; continue; }
        if (arg == L"--replay" || IsNumber(arg)) return true;
    }
    return false;
}

// Processes whose own-thread agent has opened its section.
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
        const auto processes = AgentProcesses();
        if (processes.size() > 1) {
            std::fprintf(stderr, "Several processes have an agent; pass one pid:");
            for (const DWORD process : processes) std::fprintf(stderr, " %lu", process);
            std::fprintf(stderr, "\n");
            return 1;
        }
        if (processes.size() == 1) {
            pid = std::to_wstring(processes[0]);
            args.insert(args.begin() + 1, pid.data());
        }
    }
    return reagentlive::cli::RunConsole({SectionPrefix, L"", reagentlive::LayoutCore}, static_cast<int>(args.size()),
                                        args.data());
}
