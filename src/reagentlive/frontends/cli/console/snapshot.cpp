// reagentlive CLI console: mem-snap and mem-diff, a raw range kept in a file
// beside the cache and compared later, element by element.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/results/mem_json.h"
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/core/client.h"

#include "reagentlive/frontends/cli/console/internal.h"

namespace reagentlive::cli::detail {
namespace {
// %TEMP%\reagentlive-<pid>.memsnap, beside the cache file.
std::filesystem::path SnapPath(uint32_t pid) {
    return std::filesystem::path(CachePath(pid)).replace_extension(L".memsnap");
}

std::optional<memsnap::Snapshot> LoadSnap(uint32_t pid) {
    std::ifstream in(SnapPath(pid), std::ios::binary);
    return in ? memsnap::Load(in) : std::nullopt;
}

bool SaveSnap(uint32_t pid, const memsnap::Snapshot& snapshot) {
    std::ofstream out(SnapPath(pid), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    memsnap::Save(out, snapshot);
    return static_cast<bool>(out);
}
}

void RunMemSnap(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    const Client::Hold hold(client);
    memsnap::Snapshot snapshot;
    snapshot.address = command.request.address;
    snapshot.owner = client.AgentOwner();
    auto bytes = client.ReadRange(snapshot.address, static_cast<size_t>(command.range));
    if (!bytes) return CommandError(session, jsonCommand, bytes.error());
    snapshot.tick = hold.LastTick();
    snapshot.bytes = std::move(*bytes);
    if (!SaveSnap(client.AgentPid(), snapshot))
        return JsonError(session, jsonCommand, "file", "cannot write " + SnapPath(client.AgentPid()).string());
    shape::MemSnap out;
    out.address = snapshot.address;
    out.where = Locate(snapshot.address, LoadedModules(session));
    out.length = snapshot.bytes.size();
    out.tick = snapshot.tick;
    Emit(session, jsonCommand, std::move(out));
}

void RunMemDiff(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    auto before = LoadSnap(client.AgentPid());
    if (!before) return JsonError(session, jsonCommand, "validation", "no mem-snap for this process: take one first");
    if (before->owner != client.AgentOwner())
        return JsonError(session, jsonCommand, "validation", "the mem-snap is from another agent instance: take a new one");
    const size_t size = ElementSize(command.view);
    if (before->narrowed && before->type != command.view)
        return JsonError(session, jsonCommand, "validation", "the candidates were narrowed as " + before->type +
            "; diff as " + before->type + " or take a new mem-snap");
    const Client::Hold hold(client);
    auto now = client.ReadRange(before->address, before->bytes.size());
    if (!now) return CommandError(session, jsonCommand, now.error());
    const auto test = *memsnap::TestNamed(command.target);
    const auto diff = memsnap::Compare(*before, *now, command.view, size, test);
    auto out = MakeMemDiff(*before, *now, command.view, size, command.target, diff, command.limit, hold.LastTick(),
        LoadedModules(session));
    if (command.all) {
        memsnap::Snapshot kept = std::move(*before);
        kept.bytes = std::move(*now);
        kept.tick = out.tick;
        kept.type = command.view;
        kept.candidates = diff.offsets;
        kept.narrowed = true;
        if (!SaveSnap(client.AgentPid(), kept))
            return JsonError(session, jsonCommand, "file", "cannot write " + SnapPath(client.AgentPid()).string());
        out.kept = true;
    }
    Emit(session, jsonCommand, std::move(out));
}
}
