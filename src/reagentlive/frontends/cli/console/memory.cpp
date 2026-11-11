// reagentlive CLI console: Memory commands: status, read decoding, pin and watch.
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
#include "reagentlive/frontends/cli/values/layout_json.h"
#include "reagentlive/frontends/cli/state/cache.h"
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/frontends/cli/values/format.h"
#include "reagentlive/frontends/cli/values/json_output.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/core/client.h"
#include "reagentlive/ue/client_ue.h"
#include "reagentlive/version.h"

#include "reagentlive/frontends/cli/console/internal.h"

namespace reagentlive::cli::detail {

void PrintStatus(Session& session, std::string_view jsonCommand) {
    Client& client = session.client;
    constexpr auto MinInterval = std::chrono::milliseconds(100);
    if (const auto since = Clock::now() - session.lastBeat.at; since < MinInterval)
        std::this_thread::sleep_for(MinInterval - since);
    const Beat now{client.Heartbeat(), Clock::now()};
    const double seconds = std::chrono::duration<double>(now.at - session.lastBeat.at).count();
    const double rate = seconds > 0 ? static_cast<double>(now.ticks - session.lastBeat.ticks) / seconds : 0;
    const bool ticking = now.ticks != session.lastBeat.ticks;
    session.lastBeat = now;
    const HostThread thread = client.AgentHostThread();
    Json result = {{"pid", client.AgentPid()}, {"build", client.AgentBuild()}, {"version", Version},
        {"replaced_agents", client.AgentTakeovers()}, {"ticks", now.ticks},
        {"ticks_per_second", static_cast<uint64_t>(rate + 0.5)}, {"interval_ms", static_cast<uint64_t>(seconds * 1000)},
        {"ticking", ticking}, {"host_thread", HostThreadText(thread)}};
    if (thread != HostThread::Engine)
        result["tick_note"] = "this host does not tick on the engine's thread: a tick is a host tick (a frame), "
            "not a moment of the game, so values read in one tick need not line up";
    // What watch --follow can follow.
    if (client.Supports(op::Watch))
        if (auto roots = client.WatchRoots(); roots) result["roots"] = *roots;
    JsonResult(session, jsonCommand, std::move(result));
}

// read --as: the bytes as an array of one element type.
Json DecodeValues(const std::vector<uint8_t>& bytes, std::string_view type) {
    Json values = Json::array();
    const size_t size = ElementSize(type);
    for (size_t at = 0; size && at + size <= bytes.size(); at += size)
        values.push_back(layout::Element(type, std::span(bytes.data() + at, size)));
    return values;
}

namespace {
const char* PinStopText(PinStop stop) {
    switch (stop) {
    case PinStop::Active: return "active";
    case PinStop::Expired: return "expired";
    case PinStop::GuardChanged: return "object changed";
    case PinStop::NotWritable: return "not writable";
    case PinStop::Unexpected: return "memory differed from --expect";
    case PinStop::Cleared: return "cleared";
    case PinStop::Faulted: return "access faulted";
    }
    return "unknown";
}

Json PinJson(const PinState& pin) {
    return {{"id", pin.id}, {"address", format_detail::Pointer(pin.spec.address)},
        {"bytes", format_detail::Hex(std::span(pin.spec.bytes, pin.spec.length), MaxPinBytes)},
        {"guarded", pin.spec.guard.address != 0}, {"state", PinStopText(pin.stop)}, {"writes", pin.writes},
        {"remaining_ms", pin.remaining}};
}

// Guards an address given relative to an object on that object's identity,
// read now, so a pin or watch stops when the object is freed or replaced.
Result<Guard> ObjectGuard(Session& session, uint64_t object) {
    Client& client = session.client;
    Guard guard{};
    if (!object) return guard;
    auto bytes = client.Read(object + ue::ObjectIdentityOffset, ue::ObjectIdentityLength);
    if (!bytes) return std::unexpected(bytes.error());
    guard.address = object + ue::ObjectIdentityOffset;
    guard.length = ue::ObjectIdentityLength;
    memcpy(guard.bytes, bytes->data(), bytes->size());
    return guard;
}
}

void RunPin(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(op::Pin)) return JsonError(session, jsonCommand, "unsupported", "agent does not support pin");
    const Request& request = command.request;
    switch (static_cast<PinAction>(request.flags)) {
    case PinAction::List: {
        auto pins = client.PinList();
        if (!pins) return CommandError(session, jsonCommand, pins.error());
        Json rows = Json::array();
        for (const auto& pin : *pins) rows.push_back(PinJson(pin));
        return JsonResult(session, jsonCommand, {{"pins", std::move(rows)}});
    }
    case PinAction::Clear: {
        auto cleared = client.PinClear(static_cast<uint32_t>(request.args[0]));
        if (!cleared) return CommandError(session, jsonCommand, cleared.error());
        return JsonResult(session, jsonCommand, {{"cleared", *cleared}});
    }
    default: break;
    }
    PinSpec spec{};
    spec.address = request.address;
    spec.length = request.length;
    spec.milliseconds = command.milliseconds;
    memcpy(spec.bytes, request.data, request.length);
    spec.hasExpected = request.args[1] ? 1 : 0;
    if (spec.hasExpected) memcpy(spec.expected, request.data + MaxPinBytes, request.length);
    auto guard = ObjectGuard(session, command.guardObjects.empty() ? 0 : command.guardObjects[0]);
    if (!guard) return CommandError(session, jsonCommand, guard.error());
    spec.guard = *guard;
    auto pin = client.PinAdd(spec);
    if (!pin) return CommandError(session, jsonCommand, pin.error());
    JsonResult(session, jsonCommand, PinJson(*pin));
}

namespace {
const char* TrialStopText(TrialStop stop) {
    switch (stop) {
    case TrialStop::Active: return "active";
    case TrialStop::Restored: return "restored";
    case TrialStop::Conflict: return "conflict";
    case TrialStop::Abandoned: return "abandoned";
    }
    return "unknown";
}
const char* TrialGuardText(TrialGuard guard) {
    switch (guard) {
    case TrialGuard::None: return nullptr;
    case TrialGuard::Owner: return "owner replaced or freed";
    case TrialGuard::Object: return "object vtable changed";
    case TrialGuard::Unreadable: return "target unreadable";
    }
    return "unknown";
}
Json TrialJson(const TrialState& trial) {
    const auto& spec = trial.spec;
    Json row = {{"id", trial.id}, {"address", format_detail::Pointer(spec.address)},
        {"bytes", format_detail::Hex(std::span(spec.bytes, spec.length), MaxTrialBytes)},
        {"original", format_detail::Hex(std::span(trial.original, spec.length), MaxTrialBytes)},
        {"owner", format_detail::Pointer(spec.ownerAddress)},
        {"object", spec.object ? Json(format_detail::Pointer(spec.object)) : Json(nullptr)},
        {"state", TrialStopText(trial.stop)}, {"changed_tick", trial.changedTick}, {"remaining_ms", trial.remaining}};
    if (const char* guard = TrialGuardText(trial.guard)) row["guard"] = guard;
    return row;
}
Json TrialRows(const std::vector<TrialState>& trials) {
    Json rows = Json::array();
    for (const auto& trial : trials) rows.push_back(TrialJson(trial));
    return rows;
}
// Trials need a live actor or component as owner: not a class default object,
// archetype or asset. Returns the owner's size, or an error text: its class
// size from the agent, which covers native members past the last reflected
// field, else the end of its reflected fields.
std::expected<uint32_t, std::string> TrialOwnerSize(Session& session, const Ref& owner) {
    Client& client = session.client;
    auto object = ue::Describe(client, owner);
    if (!object) return std::unexpected(object.error().message);
    if (object->name.starts_with("Default__") || object->name.ends_with("_GEN_VARIABLE"))
        return std::unexpected("owner is a class default object or an archetype");
    auto fields = ObjectFields(session, owner);
    if (!fields) return std::unexpected(fields.error().message);
    const bool live = std::ranges::any_of(*fields, [](const ue::Field& field) {
        return field.owner == "Actor" || field.owner == "ActorComponent";
    });
    if (!live) return std::unexpected("owner is not an actor or a component (" + object->className + ")");
    const size_t reflected = DumpEnd(*fields);
    if (!reflected) return std::unexpected("owner has no reflected fields");
    // A class size smaller than its own reflected fields is not believed.
    return object->size >= reflected ? object->size : static_cast<uint32_t>(reflected);
}
}

void RunTrial(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(op::Trial)) return JsonError(session, jsonCommand, "unsupported", "agent does not support trials");
    const Request& request = command.request;
    switch (static_cast<TrialAction>(request.flags)) {
    case TrialAction::List: {
        auto trials = client.TrialList();
        if (!trials) return CommandError(session, jsonCommand, trials.error());
        return JsonResult(session, jsonCommand, {{"trials", TrialRows(*trials)}});
    }
    case TrialAction::Clear: {
        auto trials = client.TrialClear(static_cast<uint32_t>(request.args[0]));
        if (!trials) return CommandError(session, jsonCommand, trials.error());
        return JsonResult(session, jsonCommand, {{"cleared", trials->size()}, {"trials", TrialRows(*trials)}});
    }
    default: break;
    }
    auto ownerSize = TrialOwnerSize(session, command.owner);
    if (!ownerSize) return JsonError(session, jsonCommand, "validation", "trial refused: " + ownerSize.error());
    auto guard = ObjectGuard(session, command.owner.address);
    if (!guard) return CommandError(session, jsonCommand, guard.error());
    TrialSpec spec{};
    spec.address = request.address;
    spec.length = request.length;
    spec.milliseconds = command.milliseconds;
    memcpy(spec.bytes, request.data, request.length);
    memcpy(spec.expected, request.data + MaxTrialBytes, request.length);
    spec.owner = *guard;
    spec.ownerAddress = command.owner.address;
    spec.ownerSize = *ownerSize;
    spec.object = command.object;
    if (!TrialSpecValid(spec))
        return JsonError(session, jsonCommand, "validation", spec.object ?
            "trial refused: the target must lie within 0x1000 bytes after --object" :
            "trial refused: the target is outside the owner; give --object for a native object it points to");
    auto trial = client.TrialAdd(spec);
    if (!trial) return CommandError(session, jsonCommand, trial.error());
    JsonResult(session, jsonCommand, TrialJson(*trial));
}

namespace {
const char* WatchEndText(WatchEnd end) {
    switch (end) {
    case WatchEnd::Running: break;
    case WatchEnd::Deadline: return "deadline";
    case WatchEnd::Stopped: return "stopped";
    case WatchEnd::BufferFull: return "buffer full";
    case WatchEnd::ObjectReplaced: return "object replaced";
    }
    return nullptr;
}

Json WatchRangesJson(const WatchSpec& spec) {
    Json ranges = Json::array();
    for (uint32_t i = 0; i < spec.count; ++i) {
        Json range = {{"length", spec.ranges[i].length}, {"guarded", spec.ranges[i].guard.address != 0 || spec.root[0]}};
        if (spec.root[0]) range["offset"] = HexNumber(spec.ranges[i].address); // 0x0, not null.
        else range["address"] = format_detail::Pointer(spec.ranges[i].address);
        ranges.push_back(std::move(range));
    }
    return ranges;
}

// Rows as JSON; following a root, `lost` rows and `resumed` ones (with the
// new object's address) bound each gap, listed in `gaps`.
Json WatchSamplesJson(const WatchSpec& spec, const WatchRows& rows, Json& gaps) {
    Json samples = Json::array();
    gaps = Json::array();
    uint64_t lastRoot = 0;
    for (const auto& row : rows.rows) {
        Json sample = {{"tick", row.header.tick}, {"us", row.header.microseconds}};
        if (row.header.flags & WatchRowLost) {
            sample["lost"] = true;
            gaps.push_back({{"from_us", row.header.microseconds}, {"to_us", nullptr}});
            samples.push_back(std::move(sample));
            continue;
        }
        Json values = Json::array();
        for (const auto& bytes : row.ranges)
            values.push_back(bytes.empty() ? Json(nullptr) : Json(format_detail::Hex(bytes, bytes.size())));
        sample["values"] = std::move(values);
        if (spec.root[0] && row.header.root != lastRoot) sample["root"] = format_detail::Pointer(row.header.root);
        if (row.header.flags & WatchRowResumed) {
            sample["resumed"] = true;
            if (!gaps.empty() && gaps.back()["to_us"].is_null()) gaps.back()["to_us"] = row.header.microseconds;
        }
        lastRoot = row.header.root;
        samples.push_back(std::move(sample));
    }
    return samples;
}

// watch --read and --stop: the agent's record of the capture, so any console run can read it.
void PrintCapture(Session& session, std::string_view jsonCommand) {
    Client& client = session.client;
    auto info = client.WatchStatus();
    if (!info) return CommandError(session, jsonCommand, info.error());
    auto rows = client.WatchRead(info->spec);
    if (!rows) return CommandError(session, jsonCommand, rows.error());
    Json gaps;
    auto samples = WatchSamplesJson(info->spec, *rows, gaps);
    const char* stopped = WatchEndText(info->stop);
    Json result = {{"running", info->stop == WatchEnd::Running}, {"stopped", stopped ? Json(stopped) : Json(nullptr)},
        {"rows", rows->rows.size()}, {"capacity", info->maxRows}, {"ms", info->spec.milliseconds},
        {"interval_ms", info->spec.interval}, {"root", info->spec.root[0] ? Json(info->spec.root) : Json(nullptr)},
        {"ranges", WatchRangesJson(info->spec)}, {"samples", std::move(samples)}};
    if (info->spec.root[0]) result["gaps"] = std::move(gaps);
    JsonResult(session, jsonCommand, std::move(result));
}
}

void RunWatch(Session& session, const Command& command, std::span<const Ref> refs, std::span<const Module> modules,
              std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(op::Watch)) return JsonError(session, jsonCommand, "unsupported", "agent does not support watch");
    if (command.target == "read") return PrintCapture(session, jsonCommand);
    if (command.target == "stop") {
        if (auto stopped = client.WatchStop(); !stopped) return CommandError(session, jsonCommand, stopped.error());
        return PrintCapture(session, jsonCommand);
    }
    WatchSpec spec{};
    spec.count = static_cast<uint32_t>(command.reads.size());
    spec.milliseconds = command.milliseconds;
    spec.interval = command.interval;
    command.root.copy(spec.root, sizeof(spec.root) - 1);
    for (uint32_t i = 0; i < spec.count; ++i) {
        // Object-relative ranges are guarded on the object; a followed root brings its own guard.
        auto guard = command.root.empty() ? ObjectGuard(session, command.guardObjects[i]) : Result<Guard>(Guard{});
        if (!guard) return CommandError(session, jsonCommand, guard.error());
        spec.ranges[i] = {command.reads[i].address, static_cast<uint32_t>(command.reads[i].length), 0, *guard};
    }
    if (!WatchSpecValid(spec))
        return JsonError(session, jsonCommand, "syntax", "ranges exceed the row limit of " + std::to_string(MaxWatchRowBytes) + " bytes");
    if (!WatchFits(spec))
        return JsonError(session, jsonCommand, "validation", "the capture does not fit the agent's buffer: at most " +
            std::to_string(WatchLongestFit(spec) / 1000) + " s at this interval and row size");
    if (command.target == "start") {
        auto capacity = client.WatchStart(spec);
        if (!capacity) return CommandError(session, jsonCommand, capacity.error());
        return JsonResult(session, jsonCommand, {{"started", true}, {"capacity", *capacity}, {"ms", spec.milliseconds},
            {"interval_ms", spec.interval}, {"root", spec.root[0] ? Json(spec.root) : Json(nullptr)},
            {"ranges", WatchRangesJson(spec)}});
    }
    std::optional<Parsed> then;
    if (!command.then.empty()) {
        then = Parse(command.then, refs, modules);
        if (!*then || (*then)->request.op != ue::op::Call || (*then)->kind != Kind::Remote)
            return JsonError(session, jsonCommand, "syntax", "--then needs a raw call: call <object> <[Class:]Function> [hex params]");
    }
    const auto started = std::chrono::steady_clock::now();
    auto capacity = client.WatchStart(spec);
    if (!capacity) return CommandError(session, jsonCommand, capacity.error());
    Json thenJson = nullptr;
    if (then) {
        const Request& call = (**then).request;
        auto block = ue::Call(client, ue::GetRef(call), call.text, std::span(call.data, call.length));
        thenJson = block ? Json{{"ok", true}, {"hex", format_detail::Hex(*block, block->size())}} :
            Json{{"ok", false}, {"message", block.error().message}};
    }
    // Wait out the duration, then for the agent to finish.
    std::this_thread::sleep_until(started + std::chrono::milliseconds(command.milliseconds));
    Result<WatchRows> rows = std::unexpected(Error{});
    for (int attempt = 0; attempt < 100; ++attempt) {
        rows = client.WatchRead(spec);
        if (!rows || !rows->running) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!rows) return CommandError(session, jsonCommand, rows.error());
    const auto info = client.WatchStatus();
    const char* stopped = info ? WatchEndText(info->stop) : nullptr;
    Json gaps;
    auto samples = WatchSamplesJson(spec, *rows, gaps);
    JsonResult(session, jsonCommand, {{"rows", rows->rows.size()}, {"capacity", *capacity}, {"complete", !rows->running},
        {"stopped", stopped ? Json(stopped) : Json(nullptr)}, {"ranges", WatchRangesJson(spec)},
        {"then", std::move(thenJson)}, {"samples", std::move(samples)}});
}
}

namespace reagentlive::cli::detail {
namespace {
// Reads 1-byte probes in batches of MaxBatchReads; true where readable.
std::vector<bool> Readable(Session& session, std::span<const uint64_t> addresses) {
    Client& client = session.client;
    std::vector<bool> readable(addresses.size(), false);
    for (size_t start = 0; start < addresses.size(); start += MaxBatchReads) {
        std::vector<ReadSpan> reads;
        for (size_t i = start; i < std::min(addresses.size(), start + MaxBatchReads); ++i) reads.push_back({addresses[i], 1});
        auto batch = client.ReadBatch(reads);
        if (!batch) break;
        for (size_t i = 0; i < batch->items.size(); ++i) readable[start + i] = batch->items[i].status == Status::Ok;
    }
    return readable;
}

}

// The image's PE timestamp, which with its size ties labels to one build.
std::optional<uint32_t> ImageTimestamp(Session& session, const Module& module) {
    Client& client = session.client;
    auto header = client.Read(module.base + 0x3c, 4);
    uint32_t peOffset = 0, timestamp = 0;
    if (!header || !format_detail::Load(*header, peOffset) || peOffset >= module.size) return std::nullopt;
    auto stamp = client.Read(module.base + peOffset + 8, 4);
    if (!stamp || !format_detail::Load(*stamp, timestamp)) return std::nullopt;
    return timestamp;
}

void RunPeek(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    const uint64_t start = command.request.address;
    const size_t count = command.limit;
    std::vector<PeekValue> values(count);
    for (size_t i = 0; i < count; ++i) values[i].address = start + i * 8;
    if (auto bytes = client.Read(start, command.request.length); bytes) {
        for (size_t i = 0; i < count; ++i) {
            values[i].read = true;
            memcpy(&values[i].value, bytes->data() + i * 8, 8);
        }
    } else { // Part of the range is unreadable: read qword by qword.
        for (size_t first = 0; first < count; first += MaxBatchReads) {
            std::vector<ReadSpan> reads;
            for (size_t i = first; i < std::min(count, first + MaxBatchReads); ++i) reads.push_back({values[i].address, 8});
            auto batch = client.ReadBatch(reads);
            if (!batch) return CommandError(session, jsonCommand, batch.error());
            for (size_t i = 0; i < batch->items.size(); ++i) {
                auto& value = values[first + i];
                value.read = batch->items[i].status == Status::Ok && format_detail::Load(batch->items[i].bytes, value.value);
            }
        }
    }

    const auto modules = LoadedModules(session);
    // Objects first: aligned, pointer-shaped values the agent identifies.
    if (client.Supports(ue::op::Describe)) {
        for (auto& value : values) {
            if (!value.read || !PointerShaped(value.value) || value.value & 7) continue;
            auto object = ue::Describe(client, ue::AddressRef(value.value));
            if (object) value.object = std::move(*object);
            else if (object.error().kind == Error::Kind::Timeout) break;
        }
    }
    std::vector<uint64_t> probes;
    std::vector<size_t> probed;
    for (size_t i = 0; i < count; ++i) {
        const auto& value = values[i];
        if (value.read && !value.object && PointerShaped(value.value) && !ModuleAt(value.value, modules)) {
            probes.push_back(value.value);
            probed.push_back(i);
        }
    }
    const auto readable = Readable(session, probes);
    for (size_t i = 0; i < probed.size(); ++i) {
        auto& value = values[probed[i]];
        value.readable = readable[i];
        if (value.readable)
            if (auto kind = RegionKind(session, value.value)) value.region = *kind;
    }
    // Name hints for value rows, in one lookup.
    if (client.Supports(ue::op::Names)) {
        std::vector<int32_t> indices;
        std::vector<size_t> named;
        for (size_t i = 0; i < count; ++i) {
            const auto& value = values[i];
            const auto index = static_cast<int32_t>(value.value & 0xffffffff);
            if (!value.read || value.object || ModuleAt(value.value, modules) ||
                (value.readable && PointerShaped(value.value)) || index <= 0) continue;
            indices.push_back(index);
            named.push_back(i);
        }
        if (!indices.empty())
            if (auto names = ue::Names(client, indices); names)
                for (size_t i = 0; i < named.size() && i < names->size(); ++i) values[named[i]].name = (*names)[i];
    }

    Json rows = Json::array();
    for (const auto& value : values) rows.push_back(PeekRowJson(value, modules));
    Json result = {{"address", format_detail::Pointer(start)}, {"count", count}, {"rows", std::move(rows)}};
    AddLocation(result, start, modules);
    if (!command.file.empty()) {
        const auto* module = ModuleAt(start, modules);
        if (!module) return JsonError(session, jsonCommand, "label", "only addresses inside a loaded module can be labelled");
        if (!values[0].read) return JsonError(session, jsonCommand, "label", "the qword is unreadable");
        std::string name = command.target;
        if (name.empty() && !values[0].name.empty() && !values[0].object && result["rows"][0]["label"] == "value")
            name = "FName_" + values[0].name;
        if (name.empty()) return JsonError(session, jsonCommand, "label", "give a name: the value has no name hint");
        const auto timestamp = ImageTimestamp(session, *module);
        if (!timestamp) return JsonError(session, jsonCommand, "label", "the module's PE header is unreadable");
        auto label = LabelJson(*module, *timestamp, start, name, "global");
        std::ofstream file(command.file, std::ios::binary | std::ios::app);
        file << label.dump() << '\n';
        if (!file) return JsonError(session, jsonCommand, "label", "cannot append to " + command.file);
        result["labelled"] = std::move(label);
    }
    JsonResult(session, jsonCommand, std::move(result));
}
}
