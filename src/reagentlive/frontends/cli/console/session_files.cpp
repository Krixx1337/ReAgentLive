// reagentlive CLI console: the files a run keeps beside the agent (the cache,
// the results folder) and the usage log's writes.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include "reagentlive/frontends/cli/results/budget.h"
#include "reagentlive/frontends/cli/state/cache.h"
#include "reagentlive/frontends/cli/state/usage.h"
#include "reagentlive/frontends/cli/console/internal.h"
#include "reagentlive/frontends/cli/console/pack.h"

namespace reagentlive::cli::detail {
std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return file ? std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()) : std::string();
}

std::filesystem::path CachePath(uint32_t pid) {
    wchar_t folder[MAX_PATH + 1];
    const DWORD length = GetTempPathW(MAX_PATH + 1, folder);
    if (!length || length > MAX_PATH) return {};
    return std::filesystem::path(folder) / (L"reagentlive-" + std::to_wstring(pid) + L".cache");
}

std::filesystem::path TemporaryBeside(const std::filesystem::path& path) {
    return std::filesystem::path(path).concat(L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp");
}

// %TEMP%\reagentlive-<pid>-<owner>.results: the whole results of lines that
// must not run again (output.cpp), per agent instance as the cache file is.
// The first run against an instance removes the same pid's earlier folders.
std::filesystem::path ResultsFolder(uint32_t pid, uint32_t owner) {
    const auto cache = CachePath(pid);
    if (cache.empty() || !owner) return {};
    const auto folder = cache.parent_path();
    const auto ascii = budget::ResultsPrefix(pid);
    const std::wstring prefix(ascii.begin(), ascii.end()); // Temp may hold names a narrow string cannot.
    const auto mine = folder / budget::ResultsName(pid, owner);
    std::error_code error;
    if (std::filesystem::exists(mine, error)) return mine;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        const auto name = entry.path().filename().wstring();
        if (name.starts_with(prefix) && name.ends_with(L".results")) std::filesystem::remove_all(entry.path(), error);
    }
    return mine;
}

// Appends whole usage lines with a single append-only write, so lines from
// consoles writing at once never interleave or overwrite each other.
void AppendUsage(const std::filesystem::path& path, const std::string& text) {
    if (text.empty()) return;
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
}

// The cache's handed-on usage session is kept only for the same agent
// instance; which run joins it is PickSession's (state/usage.h).
void LoadSessionCache(Session& session, const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return;
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto data = LoadCache(text);
    if (!data || data->owner != session.client.AgentOwner()) return; // Another agent instance: start fresh.
    session.usage.handoff = data->session;
    session.usage.handoffCaller = data->sessionCaller;
    session.modules = std::move(data->modules);
    session.watchFile = std::move(data->watchFile);
    session.searchText = data->searchText;
    for (auto& [base, layout] : data->layouts) session.layouts[base] = std::move(layout);
    LoadPackCache(session, data->packs);
}

void SaveSessionCache(const Session& session, const std::filesystem::path& path) {
    CacheData data;
    data.owner = session.client.AgentOwner();
    if (!data.owner) return;
    data.session = session.usage.handoff;
    data.sessionCaller = session.usage.handoffCaller;
    data.watchFile = session.watchFile;
    data.searchText = session.searchText;
    data.modules.assign(session.modules.begin(), session.modules.end());
    for (const auto& [base, layout] : session.layouts)
        if (std::ranges::any_of(session.modules, [base](const Module& m) { return m.base == base; }))
            data.layouts.emplace_back(base, layout);
    data.packs = SavePackCache(session);
    const auto temporary = TemporaryBeside(path);
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << SaveCache(data);
        if (!file) return;
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
}

// The log trimmed to its cap (state/usage.h), written whole then renamed.
void TrimUsageFile(const std::filesystem::path& path) {
    const auto trimmed = TrimUsage(ReadFile(path));
    if (!trimmed) return;
    const auto temporary = TemporaryBeside(path);
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << *trimmed;
        if (!file) return;
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
}
}
