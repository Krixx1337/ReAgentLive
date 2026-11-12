// reagentlive CLI console: snapshots (the dump --changed baseline), record and replay.
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
#include "reagentlive/frontends/cli/state/ue/cache_ue.h"
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/frontends/cli/values/ue/format_ue.h"
#include "reagentlive/frontends/cli/values/ue/json_output.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/core/client.h"
#include "reagentlive/ue/client_ue.h"

#include "reagentlive/frontends/cli/console/ue/internal_ue.h"

namespace reagentlive::cli::detail {
namespace {
constexpr std::string_view CaptureHeader = "reagentlive capture 1";

// The readable name alone where known, else the reflected one.
std::string ShortName(const Names& names, const ue::Field& field) {
    const auto name = names.Find(field.owner, field.offset, field.size, field.boolMask);
    return name.empty() ? field.name : std::string(name);
}

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

// The baseline dump --changed compares against; with a file, also saved there for a later run.
void Snap(Session& session, const Ref& object, const std::string& path, std::optional<Snapshot>& baseline,
          std::string_view jsonCommand) {
    Client& client = session.client;
    auto fields = ObjectFields(session, object);
    if (!fields) return CommandError(session, jsonCommand, fields.error());
    auto sorted = SortedByOffset(std::move(*fields));
    // Native members past the last reflected field, so dump --changed can compare them.
    size_t end = DumpEnd(sorted);
    for (const auto& check : NativeMembers(session, object, sorted, true).shown)
        end = std::max(end, std::min<size_t>(size_t{check.member->field.offset} + check.member->field.Size(), MaxDumpBytes));
    std::vector<uint8_t> bytes;
    bytes.reserve(end);
    while (bytes.size() < end) {
        auto chunk = ue::ReadObject(client, object, static_cast<uint32_t>(bytes.size()),
            static_cast<uint32_t>(std::min(MaxRead, end - bytes.size())));
        if (!chunk) return CommandError(session, jsonCommand, chunk.error());
        bytes.insert(bytes.end(), chunk->begin(), chunk->end());
    }
    baseline = Snapshot{object, std::move(sorted), std::move(bytes)};
    if (!path.empty() && !SaveSnapshot(path, *baseline))
        return JsonError(session, jsonCommand, "snapshot", "cannot write snapshot file " + path);
    Emit(session, jsonCommand, shape::Snap{ReusableRef(object), end,
        path.empty() ? std::nullopt : std::optional<std::string>(path)});
}

void Record(Session& session, const Ref& object, const Command& command, StructCache& structs, std::string_view jsonCommand) {
    Client& client = session.client;
    const auto fail = [&](std::string message) {
        JsonError(session, jsonCommand, "capture", message);
    };
    auto fields = ObjectFields(session, object);
    if (!fields) return CommandError(session, jsonCommand, fields.error());
    std::vector<ue::Field> selected;
    uint32_t begin = ue::MaxObjectBytes, end = 0;
    for (const auto& name : command.fields) {
        const auto matches = [&](const ue::Field& field) {
            const auto colon = name.find(':');
            if (colon == std::string::npos) return session.names.Matches(field, name);
            return name.substr(0, colon) == field.owner && session.names.Matches(field, std::string_view(name).substr(colon + 1));
        };
        const auto field = std::ranges::find_if(*fields, matches);
        if (field == fields->end() || std::ranges::find_if(std::next(field), fields->end(), matches) != fields->end()) {
            return fail("field missing or ambiguous: " + name + " (use Owner:Field to disambiguate)");
        }
        if (!field->size || field->size > 64 || uint64_t{field->offset} + field->size > ue::MaxObjectBytes) {
            return fail("field outside capture limits: " + name);
        }
        begin = std::min(begin, field->offset);
        end = std::max(end, field->offset + field->size);
        selected.push_back(*field);
    }
    if (end - begin > MaxRead) {
        return fail("selected fields must fit in one 4 KB read");
    }
    std::ofstream file(command.file, std::ios::out | std::ios::noreplace);
    if (!file) return fail("cannot create capture file (use a new path)");
    file << CaptureHeader << '\n' << "object\t" << client.AgentPid() << '\t' << format_detail::Pointer(object.address)
         << '\t' << object.index << '\t' << object.serial << '\n';
    file << "columns\tms\ttick_before\ttick_after";
    for (const auto& field : selected) file << '\t' << field.owner << ':' << ShortName(session.names, field);
    file << '\n';
    const auto start = Clock::now();
    std::optional<Error> readError;
    size_t samples = 0;
    auto readSample = [&](Clock::time_point origin) {
        const auto before = client.Heartbeat();
        auto bytes = ue::ReadObject(client, object, begin, end - begin);
        const auto after = client.Heartbeat();
        if (!bytes) {
            readError = bytes.error();
            file << "error\t" << bytes.error().message << '\n';
            return false;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - origin).count();
        file << "sample\t" << elapsed << '\t' << before << '\t' << after;
        for (const auto& field : selected) {
            const auto value = std::span(*bytes).subspan(field.offset - begin, field.size);
            file << '\t' << FieldValue(field, value, structs);
            if (field.type != "BoolProperty") file << " [" << format_detail::Hex(value, value.size()) << ']';
        }
        file << '\n';
        if (file) ++samples;
        return static_cast<bool>(file);
    };
    bool complete = readSample(start);
    if (complete) {
        for (int remaining = 3; remaining > 0; --remaining) {
            std::fprintf(stderr, "  record starts in %d\n", remaining);
            std::fflush(stderr);
            Sleep(1000);
        }
        file << "marker\t" << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count()
             << '\t' << client.Heartbeat() << "\tstart\n";
        std::fputs("  RECORDING NOW\n", stderr);
        std::fflush(stderr);
        const auto deadline = Clock::now() + std::chrono::seconds(command.request.length);
        do {
            complete = readSample(start);
        } while (complete && Clock::now() < deadline);
    }
    file << (complete ? "end\tcomplete\n" : "end\tincomplete\n");
    file.close();
    if (!file) return fail("capture file write failed");
    if (readError) return CommandError(session, jsonCommand, *readError);
    Emit(session, jsonCommand, shape::Record{ReusableRef(object), command.file, samples});
}

int Replay(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::string line, previous;
    if (!std::getline(file, line) || line != CaptureHeader) {
        std::puts("Cannot open a ReAgentLive capture.");
        return 1;
    }
    bool complete = false;
    while (std::getline(file, line)) {
        complete = line == "end\tcomplete";
        if (line.starts_with("sample\t")) {
            auto rest = std::string_view(line);
            for (int i = 0; i < 4 && !rest.empty(); ++i) {
                const auto tab = rest.find('\t');
                rest = tab == std::string_view::npos ? std::string_view{} : rest.substr(tab + 1);
            }
            if (!rest.empty() && rest == previous) continue;
            previous = rest;
        }
        std::puts(line.c_str());
    }
    if (file.eof() && complete) return 0;
    std::puts("Capture incomplete.");
    return 1;
}
}
