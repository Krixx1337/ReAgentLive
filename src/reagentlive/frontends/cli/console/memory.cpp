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
#include <cmath>
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
#include "reagentlive/frontends/cli/call/build.h"
#include "reagentlive/frontends/cli/values/format.h"
#include "reagentlive/frontends/cli/values/json.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/core/client.h"
#include "reagentlive/commit.h"
#include "reagentlive/version.h"

#include "reagentlive/frontends/cli/console/internal.h"
#include "reagentlive/frontends/cli/console/pack.h"

namespace reagentlive::cli::detail {

std::string ConsoleCommit() { return REAGENTLIVE_COMMIT; }

namespace {
// The process's executable file name (UTF-8), empty when it cannot be read.
std::string ExeName(uint32_t pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    wchar_t path[MAX_PATH * 2];
    DWORD length = static_cast<DWORD>(std::size(path));
    const bool read = QueryFullProcessImageNameW(process, 0, path, &length);
    CloseHandle(process);
    if (!read) return {};
    const std::wstring name = std::filesystem::path(std::wstring(path, length)).filename().wstring();
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(bytes > 0 ? bytes : 0), '\0');
    if (bytes > 0)
        WideCharToMultiByte(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), out.data(), bytes, nullptr, nullptr);
    return out;
}
}

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
    result.exe = ExeName(result.pid);
    result.build = client.AgentBuild();
    result.version = Version;
    result.commit = REAGENTLIVE_COMMIT;
    result.replacedAgents = client.AgentTakeovers();
    result.ticks = now.ticks;
    result.ticksPerSecond = static_cast<uint64_t>(rate + 0.5);
    result.intervalMs = static_cast<uint64_t>(seconds * 1000);
    result.ticking = ticking;
    result.hostThread = HostThreadText(thread);
    if (thread != HostThread::Engine)
        result.tickNote = "this host does not tick on the engine's thread: a tick is a host tick (a frame), "
            "not a moment of the game, so values read in one tick need not line up";
    // What watch-start's follow can follow.
    if (client.Supports(op::Watch))
        if (auto roots = client.WatchRoots(); roots) result.roots = std::move(*roots);
    result.packs = OfferedPacks(session);
    result.gameThread = PackGameThread(session);
    for (const auto& info : ToolPacks)
        if (session.toolPacks & ToolPackBit(info.pack)) result.toolPacks.emplace_back(info.name);
    result.tableHash = session.tableHash;
    Emit(session, jsonCommand, std::move(result));
}

// read with `type`: the bytes as a list of one element type.
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
    case PinStop::Unexpected: return "memory differed from expect";
    case PinStop::Cleared: return "cleared";
    case PinStop::Faulted: return "access faulted";
    }
    return "unknown";
}

shape::TimedWrite MakePin(const PinState& pin) {
    shape::TimedWrite row;
    row.id = "hold:" + std::to_string(pin.id);
    row.mode = "hold";
    row.address = pin.spec.address;
    row.bytes = HexBytes(std::span(pin.spec.bytes, pin.spec.length));
    row.state = PinStopText(pin.stop);
    row.remainingMs = pin.remaining;
    row.guarded = pin.spec.guard.address != 0;
    row.writes = pin.writes;
    return row;
}

// write's answer for a timed write: the write's place and size, and the write itself.
shape::Write TimedResult(Session& session, shape::TimedWrite row, uint32_t length) {
    shape::Write out{row.address, Locate(row.address.value, LoadedModules(session)), length, {}, {}};
    out.timed = std::move(row);
    return out;
}
}

// write with `hold`: a pin the agent writes every tick.
void RunPin(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(op::Pin)) return JsonError(session, jsonCommand, ErrorKind::Unsupported, "agent does not support hold");
    const Request& request = command.request;
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
    Emit(session, jsonCommand, TimedResult(session, MakePin(*pin), request.length));
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
shape::TimedWrite MakeTrial(const TrialState& trial) {
    const auto& spec = trial.spec;
    shape::TimedWrite row;
    row.id = "undo:" + std::to_string(trial.id);
    row.mode = "undo";
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
}

// write with `undo`: a trial the agent restores.
void RunTrial(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(op::Trial)) return JsonError(session, jsonCommand, ErrorKind::Unsupported, "agent does not support undo");
    const Request& request = command.request;
    auto ownerSize = TrialOwnerSize(session, command.owner);
    if (!ownerSize) return JsonError(session, jsonCommand, ErrorKind::Validation, "write with undo refused: " + ownerSize.error());
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
        return JsonError(session, jsonCommand, ErrorKind::Validation, spec.object ?
            "write with undo refused: the target must lie within 0x1000 bytes after object" :
            "write with undo refused: the target is outside the owner; give object for a native object it points to");
    auto trial = client.TrialAdd(spec);
    if (!trial) return CommandError(session, jsonCommand, trial.error());
    Emit(session, jsonCommand, TimedResult(session, MakeTrial(*trial), request.length));
}

// timed-writes: every timed write, `undo` ones first. write with `clear`
// ends them (all, or the one its id names: undo:N or hold:N) and answers
// the rows it ended as `cleared`.
void RunWrites(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    const bool trials = client.Supports(op::Trial), pins = client.Supports(op::Pin);
    if (!trials && !pins) return JsonError(session, jsonCommand, ErrorKind::Unsupported, "agent does not support timed writes");
    const bool clear = command.all;
    const auto id = static_cast<uint32_t>(command.request.args[0]);
    std::vector<shape::TimedWrite> rows;
    if (trials && command.target != "hold") {
        auto listed = clear ? client.TrialClear(id) : client.TrialList();
        if (!listed) return CommandError(session, jsonCommand, listed.error());
        for (const auto& trial : *listed) rows.push_back(MakeTrial(trial));
    }
    if (pins && command.target != "undo") {
        auto listed = client.PinList();
        if (!listed) return CommandError(session, jsonCommand, listed.error());
        std::vector<shape::TimedWrite> held;
        for (const auto& pin : *listed)
            if (!id || pin.id == id) held.push_back(MakePin(pin));
        if (clear) {
            auto cleared = client.PinClear(id);
            if (!cleared) return CommandError(session, jsonCommand, cleared.error());
            for (auto& row : held) if (row.state == "active") row.state = "cleared";
        }
        rows.insert(rows.end(), held.begin(), held.end());
    }
    if (!clear) return Emit(session, jsonCommand, shape::Writes{std::move(rows)});
    shape::Write out;
    out.cleared = std::move(rows);
    Emit(session, jsonCommand, std::move(out));
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
// Changes only unless `all` (shape::WatchChanges); the summary sees every sample.
template<class Shape>
void ReduceRows(Shape& result, bool all) {
    if (auto summary = shape::SummariseWatch(result.ranges, result.rows); !summary.empty()) result.summary = std::move(summary);
    if (all) return;
    uint64_t dropped = 0;
    result.samples = result.rows.size();
    result.rows = shape::WatchChanges(result.rows, shape::MaxWatchChangeRows, dropped);
    if (dropped) result.dropped = dropped;
}

// watch-start with `file`: the ranges as the first line, then one line per sample.
bool WriteCapture(const std::string& path, const std::vector<shape::WatchRange>& ranges,
                  std::span<const shape::WatchSample> rows) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    Json header = Json::array();
    for (const auto& range : ranges) header.push_back(ToJson(range));
    file << Json{{"ranges", std::move(header)}}.dump() << '\n';
    for (const auto& row : rows) file << ToJson(row).dump() << '\n';
    return static_cast<bool>(file);
}

void PrintCapture(Session& session, std::string_view jsonCommand, bool all) {
    Client& client = session.client;
    auto info = client.WatchStatus();
    if (!info) return CommandError(session, jsonCommand, info.error());
    auto rows = client.WatchRead(info->spec);
    if (!rows) return CommandError(session, jsonCommand, rows.error());
    std::vector<shape::WatchGap> gaps;
    shape::WatchCapture result;
    result.rows = WatchSamples(info->spec, *rows, gaps);
    if (!session.watchFile.empty()) {
        if (!WriteCapture(session.watchFile, WatchRanges(info->spec), result.rows))
            return JsonError(session, jsonCommand, ErrorKind::File, "cannot write " + session.watchFile);
        result.file = session.watchFile;
    }
    result.running = info->stop == WatchEnd::Running;
    result.stopped = WatchEndText(info->stop);
    result.capacity = info->maxRows;
    result.ms = info->spec.milliseconds;
    result.intervalMs = info->spec.interval;
    result.root = WatchRoot(info->spec);
    result.ranges = WatchRanges(info->spec);
    if (info->spec.root[0]) result.gaps = std::move(gaps);
    ReduceRows(result, all);
    Emit(session, jsonCommand, std::move(result));
}
}

void RunWatch(Session& session, const Command& original, std::string_view jsonCommand) {
    Client& client = session.client;
    Command command = original;
    // {"object", "field"} ranges: place, size and type from reflection; guarded on the field's object.
    for (const auto& field : command.watchFields) {
        auto range = PackWatchField(session, field.object, field.path);
        if (!range) return JsonError(session, jsonCommand, ErrorKind::Validation, range.error());
        command.reads[field.range].address = range->address;
        command.reads[field.range].length = range->length;
        command.watchTypes[field.range] = range->type;
        command.guardObjects[field.range] = range->object;
    }
    if (!client.Supports(op::Watch)) return JsonError(session, jsonCommand, ErrorKind::Unsupported, "agent does not support watch");
    if (command.target == "read") return PrintCapture(session, jsonCommand, command.all);
    if (command.target == "stop") {
        if (auto stopped = client.WatchStop(); !stopped) return CommandError(session, jsonCommand, stopped.error());
        return PrintCapture(session, jsonCommand, command.all);
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
        return JsonError(session, jsonCommand, ErrorKind::Schema, "ranges exceed the row limit of " + std::to_string(MaxWatchRowBytes) + " bytes");
    if (!WatchFits(spec))
        return JsonError(session, jsonCommand, ErrorKind::Validation, "the capture does not fit the agent's buffer: at most " +
            std::to_string(WatchLongestFit(spec) / 1000) + " s at this interval and row size");
    // A file is checked before the capture starts: the ranges go in as its first line.
    if (!command.file.empty() && !WriteCapture(command.file, WatchRanges(spec), {}))
        return JsonError(session, jsonCommand, ErrorKind::File, "cannot write " + command.file);
    auto capacity = client.WatchStart(spec);
    if (!capacity) return CommandError(session, jsonCommand, capacity.error());
    session.watchFile = command.file;
    shape::WatchStart result{*capacity, spec.milliseconds, spec.interval, WatchRoot(spec), WatchRanges(spec), {}};
    if (!command.file.empty()) result.file = command.file;
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
    result.region = RegionOf(session, start);
    for (const auto& value : values) result.rows.push_back(MakePeekRow(value, modules));
    if (command.label) {
        const std::string path = !command.file.empty() ? command.file
            : !session.hostLabels.empty() ? session.hostLabels.string() : session.labels.string();
        if (path.empty()) return JsonError(session, jsonCommand, ErrorKind::Label, "no labels file: give one with file");
        const auto* module = ModuleAt(start, modules);
        if (!module) return JsonError(session, jsonCommand, ErrorKind::Label, "only addresses inside a loaded module can be labelled");
        if (!values[0].read) return JsonError(session, jsonCommand, ErrorKind::Label, "the qword is unreadable");
        std::string name = command.target;
        if (name.empty() && !values[0].name.empty() && !values[0].object && result.rows[0].label == "value")
            name = "FName_" + values[0].name;
        if (name.empty()) return JsonError(session, jsonCommand, ErrorKind::Label, "give a name with name: the value has no name hint");
        const auto timestamp = ImageTimestamp(session, *module);
        if (!timestamp) return JsonError(session, jsonCommand, ErrorKind::Label, "the module's PE header is unreadable");
        auto label = MakeLabel(*module, *timestamp, start, name, "global");
        std::ofstream file(path, std::ios::binary | std::ios::app);
        file << ToJson(label).dump() << '\n';
        if (!file) return JsonError(session, jsonCommand, ErrorKind::Label, "cannot append to " + path);
        result.labelled = std::move(label);
    }
    Emit(session, jsonCommand, std::move(result));
}

namespace {
// Rows for a job's hits, with the text around each for search text.
shape::Search SearchShape(Session& session, const SearchJobResult& job) {
    const auto modules = LoadedModules(session);
    shape::Search result;
    for (uint64_t hit : job.hits) result.rows.push_back({hit, Locate(hit, modules), std::nullopt});
    const auto& text = session.searchText;
    if (text.job == job.id && text.step && session.client.Supports(op::ReadBatch)) {
        const size_t step = text.step, length = text.length, around = (MaxSearchText + 1) * step;
        std::vector<ReadSpan> spans;
        std::vector<size_t> before;
        for (uint64_t hit : job.hits) {
            const size_t back = static_cast<size_t>(std::min<uint64_t>(hit, around));
            spans.push_back({hit - back, back + length + around});
            before.push_back(back);
        }
        const auto read = AgentRead(session);
        for (size_t first = 0; first < spans.size(); first += MaxBatchReads) {
            const size_t n = std::min(MaxBatchReads, spans.size() - first);
            auto items = read(std::span(spans).subspan(first, n));
            if (!items) break; // The hits stand without text.
            for (size_t i = 0; i < n && i < items->size(); ++i)
                if ((*items)[i])
                    if (auto around_ = TextAround(*(*items)[i], before[first + i], length, step == 2, MaxSearchText);
                        !around_.empty()) result.rows[first + i].text = std::move(around_);
        }
    }
    result.job = job.id;
    result.state = job.state == SearchJobState::Running ? "running" : job.state == SearchJobState::Done ? "done" : "stopped";
    result.from = job.start;
    result.to = job.end;
    result.scanned = job.cursor >= job.end ? 1.0 :
        std::round(static_cast<double>(job.scanned) / SearchScannedWhole * 1000) / 1000;
    if (job.state != SearchJobState::Running && job.cursor < job.end) result.nextCursor = job.cursor;
    return result;
}
}

void RunSearch(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    const Request& request = command.request;
    if (request.flags & (SearchJobStatus | SearchJobStop)) {
        auto job = client.SearchPoll(static_cast<uint32_t>(request.args[0]), request.flags & SearchJobStop);
        if (!job) return CommandError(session, jsonCommand, job.error());
        return Emit(session, jsonCommand, SearchShape(session, *job));
    }
    SearchQuery query;
    query.pattern.assign(request.data, request.data + request.length);
    query.from = request.address;
    query.to = request.args[0];
    query.alignment = SearchAlignment(request.flags);
    query.writableOnly = request.flags & SearchWritable;
    query.privateOnly = request.flags & SearchPrivate;
    query.paged = request.flags & SearchPaged;
    query.maxHits = static_cast<uint32_t>(request.args[1]);
    // The range: a named module; else the main module when nothing else
    // narrows it; a start alone ends with the module it lies in.
    const auto inModule = [&](uint64_t address) { return ModuleAt(address, LoadedModules(session)); };
    if (!command.target.empty()) {
        const auto* match = text::ModuleNamed(command.target, LoadedModules(session));
        if (!match && RefreshModules(session)) match = text::ModuleNamed(command.target, LoadedModules(session));
        if (!match) return JsonError(session, jsonCommand, ErrorKind::Module, "no loaded module with that name");
        query.from = match->base;
        query.to = match->base + match->size;
    } else if (!query.to && !query.writableOnly && !query.privateOnly) {
        if (LoadedModules(session).empty()) RefreshModules(session);
        const Module* module = query.from ? inModule(query.from) : (LoadedModules(session).empty() ? nullptr : &LoadedModules(session)[0]);
        if (module) {
            if (!query.from) query.from = module->base;
            query.to = module->base + module->size;
        }
    }
    auto job = client.SearchStart(query);
    if (!job) return CommandError(session, jsonCommand, job.error());
    session.searchText = {job->id, request.length, command.textSearch};
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(command.seconds);
    while (job->state == SearchJobState::Running && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto next = client.SearchPoll(job->id, false);
        if (!next) return CommandError(session, jsonCommand, next.error());
        job = std::move(next);
    }
    Emit(session, jsonCommand, SearchShape(session, *job));
}
}
