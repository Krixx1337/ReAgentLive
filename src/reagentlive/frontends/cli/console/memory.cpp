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
#include "reagentlive/frontends/cli/values/json.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/core/client.h"
#include "reagentlive/version.h"

#include "reagentlive/frontends/cli/console/internal.h"
#include "reagentlive/frontends/cli/console/pack.h"

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
    shape::Status result;
    result.pid = client.AgentPid();
    result.build = client.AgentBuild();
    result.version = Version;
    result.replacedAgents = client.AgentTakeovers();
    result.ticks = now.ticks;
    result.ticksPerSecond = static_cast<uint64_t>(rate + 0.5);
    result.intervalMs = static_cast<uint64_t>(seconds * 1000);
    result.ticking = ticking;
    result.hostThread = HostThreadText(thread);
    if (thread != HostThread::Engine)
        result.tickNote = "this host does not tick on the engine's thread: a tick is a host tick (a frame), "
            "not a moment of the game, so values read in one tick need not line up";
    // What watch-start --follow can follow.
    if (client.Supports(op::Watch))
        if (auto roots = client.WatchRoots(); roots) result.roots = std::move(*roots);
    result.packs = OfferedPacks(session);
    result.tableHash = session.tableHash;
    Emit(session, jsonCommand, std::move(result));
}

// read --type: the bytes as a list of one element type.
std::vector<Json> DecodeValues(const std::vector<uint8_t>& bytes, std::string_view type) {
    std::vector<Json> values;
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

shape::Pin MakePin(const PinState& pin) {
    return {pin.id, pin.spec.address, HexBytes(std::span(pin.spec.bytes, pin.spec.length)),
        pin.spec.guard.address != 0, PinStopText(pin.stop), pin.writes, pin.remaining};
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
        shape::PinList result;
        for (const auto& pin : *pins) result.rows.push_back(MakePin(pin));
        return Emit(session, jsonCommand, std::move(result));
    }
    case PinAction::Clear: {
        auto cleared = client.PinClear(static_cast<uint32_t>(request.args[0]));
        if (!cleared) return CommandError(session, jsonCommand, cleared.error());
        return Emit(session, jsonCommand, shape::PinClear{*cleared});
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
    Emit(session, jsonCommand, MakePin(*pin));
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
shape::Trial MakeTrial(const TrialState& trial) {
    const auto& spec = trial.spec;
    shape::Trial row;
    row.id = trial.id;
    row.address = spec.address;
    row.bytes = HexBytes(std::span(spec.bytes, spec.length));
    row.original = HexBytes(std::span(trial.original, spec.length));
    row.owner = spec.ownerAddress;
    row.object = NonZero(spec.object);
    row.state = TrialStopText(trial.stop);
    row.changedTick = trial.changedTick;
    row.remainingMs = trial.remaining;
    if (const char* guard = TrialGuardText(trial.guard)) row.guard = guard;
    return row;
}
std::vector<shape::Trial> TrialRows(const std::vector<TrialState>& trials) {
    std::vector<shape::Trial> rows;
    for (const auto& trial : trials) rows.push_back(MakeTrial(trial));
    return rows;
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
        return Emit(session, jsonCommand, shape::TrialList{TrialRows(*trials)});
    }
    case TrialAction::Clear: {
        auto trials = client.TrialClear(static_cast<uint32_t>(request.args[0]));
        if (!trials) return CommandError(session, jsonCommand, trials.error());
        return Emit(session, jsonCommand, shape::TrialClear{trials->size(), TrialRows(*trials)});
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
    Emit(session, jsonCommand, MakeTrial(*trial));
}

namespace {
std::optional<std::string> WatchEndText(WatchEnd end) {
    switch (end) {
    case WatchEnd::Running: break;
    case WatchEnd::Deadline: return "deadline";
    case WatchEnd::Stopped: return "stopped";
    case WatchEnd::BufferFull: return "buffer full";
    case WatchEnd::ObjectReplaced: return "object replaced";
    }
    return std::nullopt;
}

std::vector<shape::WatchRange> WatchRanges(const WatchSpec& spec) {
    std::vector<shape::WatchRange> ranges;
    for (uint32_t i = 0; i < spec.count; ++i) {
        shape::WatchRange range;
        range.length = spec.ranges[i].length;
        if (const auto type = WatchTypeName(spec.ranges[i].note); !type.empty()) {
            const size_t count = range.length / ElementSize(type);
            range.type = std::string(type) + (count > 1 ? "[" + std::to_string(count) + "]" : "");
        }
        range.guarded = spec.ranges[i].guard.address != 0 || spec.root[0];
        if (spec.root[0]) range.offset = HexNum(spec.ranges[i].address);
        else range.address = Addr(spec.ranges[i].address);
        ranges.push_back(std::move(range));
    }
    return ranges;
}

std::optional<std::string> WatchRoot(const WatchSpec& spec) {
    return spec.root[0] ? std::optional<std::string>(spec.root) : std::nullopt;
}

// Rows as samples; following a root, `lost` rows and `resumed` ones (with the
// new object's address) bound each gap, listed in `gaps`.
std::vector<shape::WatchSample> WatchSamples(const WatchSpec& spec, const WatchRows& rows, std::vector<shape::WatchGap>& gaps) {
    std::vector<shape::WatchSample> samples;
    gaps.clear();
    uint64_t lastRoot = 0;
    for (const auto& row : rows.rows) {
        shape::WatchSample sample;
        sample.tick = row.header.tick;
        sample.us = row.header.microseconds;
        if (row.header.flags & WatchRowLost) {
            sample.lost = true;
            gaps.push_back({row.header.microseconds, std::nullopt});
            samples.push_back(std::move(sample));
            continue;
        }
        sample.values.emplace();
        for (size_t i = 0; i < row.ranges.size(); ++i) {
            const auto& bytes = row.ranges[i];
            const auto type = i < spec.count ? WatchTypeName(spec.ranges[i].note) : std::string_view{};
            if (bytes.empty()) {
                sample.values->emplace_back(nullptr);
            } else if (type.empty()) {
                sample.values->emplace_back(HexBytes(bytes).text);
            } else {
                auto numbers = DecodeValues(bytes, type);
                if (numbers.size() == 1) sample.values->push_back(std::move(numbers.front()));
                else sample.values->emplace_back(std::move(numbers));
            }
        }
        if (spec.root[0] && row.header.root != lastRoot) sample.root = Addr(row.header.root);
        if (row.header.flags & WatchRowResumed) {
            sample.resumed = true;
            if (!gaps.empty() && !gaps.back().toUs) gaps.back().toUs = row.header.microseconds;
        }
        lastRoot = row.header.root;
        samples.push_back(std::move(sample));
    }
    return samples;
}

// watch-read and watch-stop: the agent's record of the capture, so any console run can read it.
void PrintCapture(Session& session, std::string_view jsonCommand) {
    Client& client = session.client;
    auto info = client.WatchStatus();
    if (!info) return CommandError(session, jsonCommand, info.error());
    auto rows = client.WatchRead(info->spec);
    if (!rows) return CommandError(session, jsonCommand, rows.error());
    std::vector<shape::WatchGap> gaps;
    shape::WatchCapture result;
    result.rows = WatchSamples(info->spec, *rows, gaps);
    result.running = info->stop == WatchEnd::Running;
    result.stopped = WatchEndText(info->stop);
    result.capacity = info->maxRows;
    result.ms = info->spec.milliseconds;
    result.intervalMs = info->spec.interval;
    result.root = WatchRoot(info->spec);
    result.ranges = WatchRanges(info->spec);
    if (info->spec.root[0]) result.gaps = std::move(gaps);
    if (auto summary = shape::SummariseWatch(result.ranges, result.rows); !summary.empty()) result.summary = std::move(summary);
    Emit(session, jsonCommand, std::move(result));
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
        spec.ranges[i] = {command.reads[i].address, static_cast<uint32_t>(command.reads[i].length),
            i < command.watchTypes.size() ? command.watchTypes[i] : 0, *guard};
    }
    if (!WatchSpecValid(spec))
        return JsonError(session, jsonCommand, "syntax", "ranges exceed the row limit of " + std::to_string(MaxWatchRowBytes) + " bytes");
    if (!WatchFits(spec))
        return JsonError(session, jsonCommand, "validation", "the capture does not fit the agent's buffer: at most " +
            std::to_string(WatchLongestFit(spec) / 1000) + " s at this interval and row size");
    if (command.target == "start") {
        auto capacity = client.WatchStart(spec);
        if (!capacity) return CommandError(session, jsonCommand, capacity.error());
        return Emit(session, jsonCommand,
            shape::WatchStart{*capacity, spec.milliseconds, spec.interval, WatchRoot(spec), WatchRanges(spec)});
    }
    std::optional<Parsed> then;
    if (!command.then.empty()) {
        then = Parse(session.verbs, command.then, refs, modules);
        if (!*then || !PackCall(**then))
            return JsonError(session, jsonCommand, "syntax", "--then needs a raw call: call <object> <[Class:]Function> [hex params]");
    }
    const auto started = std::chrono::steady_clock::now();
    auto capacity = client.WatchStart(spec);
    if (!capacity) return CommandError(session, jsonCommand, capacity.error());
    shape::Watch result;
    if (then) result.then = RunPackCall(session, **then);
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
    std::vector<shape::WatchGap> gaps;
    result.rows = WatchSamples(spec, *rows, gaps);
    result.capacity = *capacity;
    result.complete = !rows->running;
    if (info) result.stopped = WatchEndText(info->stop);
    result.ranges = WatchRanges(spec);
    if (auto summary = shape::SummariseWatch(result.ranges, result.rows); !summary.empty()) result.summary = std::move(summary);
    Emit(session, jsonCommand, std::move(result));
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
    // Objects first: aligned, pointer-shaped values the pack identifies.
    DescribePeek(session, values);
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
    NamePeek(session, values, modules);

    shape::Peek result;
    result.address = start;
    result.where = Locate(start, modules);
    for (const auto& value : values) result.rows.push_back(MakePeekRow(value, modules));
    if (!command.file.empty()) {
        const auto* module = ModuleAt(start, modules);
        if (!module) return JsonError(session, jsonCommand, "label", "only addresses inside a loaded module can be labelled");
        if (!values[0].read) return JsonError(session, jsonCommand, "label", "the qword is unreadable");
        std::string name = command.target;
        if (name.empty() && !values[0].name.empty() && !values[0].object && result.rows[0].label == "value")
            name = "FName_" + values[0].name;
        if (name.empty()) return JsonError(session, jsonCommand, "label", "give a name with --name: the value has no name hint");
        const auto timestamp = ImageTimestamp(session, *module);
        if (!timestamp) return JsonError(session, jsonCommand, "label", "the module's PE header is unreadable");
        auto label = MakeLabel(*module, *timestamp, start, name, "global");
        std::ofstream file(command.file, std::ios::binary | std::ios::app);
        file << ToJson(label).dump() << '\n';
        if (!file) return JsonError(session, jsonCommand, "label", "cannot append to " + command.file);
        result.labelled = std::move(label);
    }
    Emit(session, jsonCommand, std::move(result));
}
}
