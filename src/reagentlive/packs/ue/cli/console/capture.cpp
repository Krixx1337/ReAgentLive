// reagentlive CLI console: snapshots, the get with `mode` `snap` baseline get with `mode` `changed` compares with.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/packs/ue/cli/state/cache_ue.h"
#include "reagentlive/frontends/cli/call/build.h"
#include "reagentlive/packs/ue/cli/values/format_ue.h"
#include "reagentlive/packs/ue/cli/values/json_output.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/core/client.h"
#include "reagentlive/packs/ue/client_ue.h"

#include "reagentlive/packs/ue/cli/console/internal_ue.h"

namespace reagentlive::cli::detail {
namespace {
constexpr std::string_view SnapshotHeader = "reagentlive snapshot 2";

// Tab-separated text: header, object ref, one line per field, then the bytes as hex.
bool SaveSnapshot(const std::string& path, const Snapshot& snapshot) {
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    file << SnapshotHeader << "\nobject\t" << RefText(snapshot.object) << '\n';
    for (const auto& f : snapshot.fields) file << FieldLine(f) << '\n';
    file << "bytes\t" << format_detail::Hex(snapshot.bytes, snapshot.bytes.size()) << '\n';
    return static_cast<bool>(file);
}
}

std::optional<Snapshot> LoadSnapshot(const std::string& path) {
    std::ifstream file(path);
    std::string line;
    if (!std::getline(file, line) || line != SnapshotHeader || !std::getline(file, line)) return std::nullopt;
    Snapshot snapshot;
    auto parts = cache_detail::SplitTabs(line);
    if (parts.size() != 2 || parts[0] != "object" || !text::LiteralRef(parts[1], snapshot.object)) return std::nullopt;
    while (std::getline(file, line)) {
        parts = cache_detail::SplitTabs(line);
        if (parts[0] == "bytes" && parts.size() == 2) {
            uint32_t length = 0;
            snapshot.bytes.resize(parts[1].size() / 3 + 1);
            if (!text::Bytes(parts[1], snapshot.bytes.data(), snapshot.bytes.size(), length)) return std::nullopt;
            snapshot.bytes.resize(length);
            return snapshot;
        }
        auto field = ParseFieldLine(line);
        if (!field) return std::nullopt;
        snapshot.fields.push_back(std::move(*field));
    }
    return std::nullopt;
}

// An object's first `end` bytes, each chunk read only while it is still the
// same object: one freed or reused mid-read fails instead of mixing bytes.
Result<std::vector<uint8_t>> ReadObjectBytes(Client& client, const Ref& object, size_t end) {
    std::vector<uint8_t> bytes;
    bytes.reserve(end);
    while (bytes.size() < end) {
        auto chunk = ue::ReadObject(client, object, static_cast<uint32_t>(bytes.size()),
            static_cast<uint32_t>(std::min(MaxRead, end - bytes.size())));
        if (!chunk) return std::unexpected(chunk.error());
        bytes.insert(bytes.end(), chunk->begin(), chunk->end());
    }
    return bytes;
}

// get with `mode` `snap`: the baseline get with `mode` `changed` compares against; with a file, also saved there for a later run.
void Snap(Session& session, const Ref& object, const std::string& path, std::optional<Snapshot>& baseline,
          std::string_view jsonCommand) {
    Client& client = session.client;
    auto fields = ObjectFields(session, object);
    if (!fields) return CommandError(session, jsonCommand, fields.error());
    auto sorted = SortedByOffset(std::move(*fields));
    // Native members past the last reflected field, so get with `mode` `changed` can compare them.
    size_t end = DumpEnd(sorted);
    for (const auto& check : NativeMembers(session, object, sorted, true).shown)
        end = std::max(end, std::min<size_t>(size_t{check.member->field.offset} + check.member->field.Size(), MaxDumpBytes));
    auto bytes = ReadObjectBytes(client, object, end);
    if (!bytes) return CommandError(session, jsonCommand, bytes.error());
    baseline = Snapshot{object, std::move(sorted), std::move(*bytes)};
    if (!path.empty() && !SaveSnapshot(path, *baseline))
        return JsonError(session, jsonCommand, ErrorKind::Snapshot, "cannot write snapshot file " + path);
    shape::Get result;
    result.ref = ReusableRef(object);
    result.snap = shape::SnapInfo{end, path.empty() ? std::nullopt : std::optional<std::string>(path)};
    Emit(session, jsonCommand, std::move(result));
}
}
