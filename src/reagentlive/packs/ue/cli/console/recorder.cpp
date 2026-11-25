// reagentlive CLI console, Unreal pack: calls-start, and its recordings'
// rows for the core's calls-read (pack.h PackCalls; the agent's call
// recorder, protocol_ue.h, on the core's recorder slots; on one object, each
// row's function). Rows are decoded by the recorded function's reflected
// parameters through the shared reader, as a copy: names and objects are looked up, memory the block points into is
// not read (the caller may have freed it since).
#include <algorithm>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/packs/ue/cli/values/format_ue.h"
#include "reagentlive/packs/ue/cli/values/json_output.h"
#include "reagentlive/core/client.h"
#include "reagentlive/packs/ue/client_ue.h"

#include "reagentlive/packs/ue/cli/console/internal_ue.h"
#include "reagentlive/packs/ue/cli/console/rows.h"

namespace reagentlive::cli::detail {
namespace {
// The recorded function's "Owner:Name", ref and parameters, from its address.
struct Recorded { std::string name; std::optional<Ref> ref; ue::Signature signature; bool known = false; };
Recorded Lookup(Session& session, uint64_t function) {
    Recorded out;
    auto described = ue::Describe(session.client, Ref{static_cast<uintptr_t>(function), -1, 0});
    if (described) {
        out.name = described->name;
        out.ref = described->ref;
    }
    auto signature = ue::Parameters(session.client, function);
    if (signature) {
        out.signature = std::move(*signature);
        out.known = true;
    }
    return out;
}

const char* Direction(const ue::Field& parameter) {
    return parameter.parameterFlags & ue::Return ? "return" : ue::InputParameter(parameter.parameterFlags) ? "in" : "out";
}
std::vector<uint8_t> Pointer(uint64_t address) {
    uint8_t bytes[8];
    memcpy(bytes, &address, sizeof(bytes));
    return std::vector<uint8_t>(bytes, bytes + sizeof(bytes));
}

// An object's recording (ObjectCallsKind): each row's function, looked up
// once per function, and its block decoded by that function's parameters.
void ObjectCalls(Session& session, const RecorderRows& read, shape::Calls& out) {
    const RecorderStatus& state = read.status;
    const auto& calls = read.calls;
    out.recorder = "calls-start";
    out.target = Addr(state.target);
    out.truncated = state.counts[ue::ObjectTruncatedCount];
    out.faulted = state.counts[ue::ObjectFaultedCount];
    struct Row { uint64_t function = 0; std::vector<uint8_t> block; };
    std::vector<Row> rows(calls.size());
    std::unordered_map<uint64_t, Recorded> functions;
    for (size_t i = 0; i < calls.size(); ++i) {
        const auto& payload = calls[i].payload;
        if (payload.size() < sizeof(ue::ObjectCall)) continue;
        ue::ObjectCall call{};
        memcpy(&call, payload.data(), sizeof(call));
        rows[i] = {call.function, std::vector<uint8_t>(payload.begin() + sizeof(call), payload.end())};
        if (call.function && !functions.contains(call.function)) functions.emplace(call.function, Lookup(session, call.function));
    }
    RowReader reader(session, Ue(session).structs, RowReader::PreviewNested);
    reader.Snapshot();
    reader.Meta(session.meta);
    // The parameters a row's copied block holds whole.
    const auto each = [&](const Row& row, auto&& visit) {
        const auto found = functions.find(row.function);
        if (found == functions.end() || !found->second.known || row.block.empty()) return;
        for (const auto& parameter : found->second.signature.parameters)
            if (parameter.size && size_t{parameter.offset} + parameter.size <= row.block.size()) visit(parameter);
    };
    reader.Named("ObjectProperty", Pointer(state.target));
    for (const auto& row : rows)
        each(row, [&](const ue::Field& parameter) {
            bool ignored = false;
            reader.Value(parameter, FieldBytes(row.block, parameter), ignored);
        });
    reader.Resolve();
    const auto* self = reader.Named("ObjectProperty", Pointer(state.target));
    for (size_t i = 0; i < calls.size(); ++i) {
        const Row& recorded = rows[i];
        shape::CallsRow& row = out.rows[i];
        row.via = "event";
        if (self) {
            row.ref = FullRef(self->ref);
            row.objectName = self->name;
            row.objectClass = self->className;
        } else {
            row.ref = ReusableRef(Ref{static_cast<uintptr_t>(state.target), -1, 0});
        }
        if (calls[i].row.tag & ue::ObjectCallTruncated) row.truncated = true;
        if (calls[i].row.tag & ue::ObjectCallFaulted) row.copyFaulted = true;
        if (!recorded.function) continue;
        const Recorded& function = functions.find(recorded.function)->second;
        if (function.ref) {
            row.function = function.name;
            row.functionRef = ReusableRef(*function.ref);
        } else {
            row.function = HexNumber(recorded.function);
        }
        if (!function.known || recorded.block.empty()) continue;
        std::vector<shape::CallsValue> values;
        each(recorded, [&](const ue::Field& parameter) {
            shape::CallsValue value;
            value.name = parameter.name;
            value.direction = Direction(parameter);
            bool partial = false;
            value.value = reader.Value(parameter, FieldBytes(recorded.block, parameter), partial);
            value.truncated = partial;
            values.push_back(std::move(value));
        });
        row.values = std::move(values);
    }
    if (reader.Unnamed()) out.unnamed = reader.Unnamed();
}

// calls-start without function: every ProcessEvent on one live object.
void RunObjectCalls(Session& session, const Command& command, std::string_view jsonCommand) {
    const Request& request = command.request;
    const bool dry = (request.flags & ue::CallsDry) != 0;
    auto started = ue::ObjectCallsStart(session.client, ue::GetRef(request), request.args[1], dry);
    if (!started) return CommandError(session, jsonCommand, started.error());
    shape::CallsStart out;
    out.target = "object";
    out.slot = started->slot;
    out.exec = started->original;
    out.thunk = started->thunk;
    out.objectAddr = started->object;
    out.table = started->table;
    out.entry = started->entry;
    out.durationMs = request.args[1];
    out.dry = dry;
    out.note = "only ProcessEvent calls on this object are seen: Blueprint calls inside the VM and direct "
               "C++ calls skip it";
    return Emit(session, jsonCommand, std::move(out));
}
}

bool PackCalls(Session& session, const RecorderRows& read, shape::Calls& out) {
    if (read.status.kind == ue::ObjectCallsKind) {
        ObjectCalls(session, read, out);
        return true;
    }
    if (read.status.kind != ue::CallsKind) return false;
    const RecorderStatus& state = read.status;
    const auto& calls = read.calls;
    auto& structs = Ue(session).structs;
    const Recorded function = Lookup(session, state.target);
    out.recorder = "calls-start";
    out.function = function.name;
    out.events = state.counts[ue::CallsEventCount];
    out.vm = state.counts[ue::CallsVmCount];
    // The calling objects and every parameter, through one reader: its names,
    // objects and pointers resolved together, then printed.
    RowReader reader(session, structs, RowReader::PreviewNested);
    reader.Snapshot();
    reader.Meta(session.meta);
    const auto caller = [](const RecorderCall& call) { return Pointer(call.row.object); };
    const auto each = [&](const RecorderCall& call, auto&& visit) {
        if (!function.known || call.payload.empty()) return;
        for (const auto& parameter : function.signature.parameters)
            if (parameter.size && size_t{parameter.offset} + parameter.size <= call.payload.size()) visit(parameter);
    };
    for (const auto& call : calls) {
        reader.Named("ObjectProperty", caller(call));
        each(call, [&](const ue::Field& parameter) {
            bool ignored = false;
            reader.Value(parameter, FieldBytes(call.payload, parameter), ignored);
        });
    }
    reader.Resolve();
    for (size_t i = 0; i < calls.size(); ++i) {
        const auto& call = calls[i];
        shape::CallsRow& row = out.rows[i];
        row.via = call.row.tag == ue::CallsEvent ? "event" : "vm";
        if (const auto* named = reader.Named("ObjectProperty", caller(call))) {
            row.ref = FullRef(named->ref);
            row.objectName = named->name;
            row.objectClass = named->className;
        } else {
            row.ref = ReusableRef(Ref{static_cast<uintptr_t>(call.row.object), -1, 0});
        }
        if (function.known && !call.payload.empty()) {
            std::vector<shape::CallsValue> values;
            each(call, [&](const ue::Field& parameter) {
                shape::CallsValue value;
                value.name = parameter.name;
                value.direction = Direction(parameter);
                bool partial = false;
                value.value = reader.Value(parameter, FieldBytes(call.payload, parameter), partial);
                value.truncated = partial;
                values.push_back(std::move(value));
            });
            row.values = std::move(values);
        }
    }
    if (reader.Unnamed()) out.unnamed = reader.Unnamed();
    return true;
}

void RunCalls(Session& session, const Command& original, std::string_view jsonCommand) {
    Client& client = session.client;
    Command command = original;
    if (!client.Supports(command.request.op) || !client.Supports(op::RecorderRead) || !client.Supports(op::RecorderStop))
        return JsonError(session, jsonCommand, ErrorKind::Unsupported, "this host does not offer the call recorder");
    if (command.request.flags & ue::CallsObject) return RunObjectCalls(session, command, jsonCommand);
    uint64_t filter = 0;
    if (!command.className.empty()) {
        auto type = ResolveObjectOrClass(session, command.className);
        if (!type) return CommandError(session, jsonCommand, type.error());
        ue::SetRef(command.request, *type);
    } else {
        // An instance narrows the recording to it; a class or a class default object (by ref)
        // records every instance.
        const auto described = ue::Describe(client, ue::GetRef(command.request));
        if (!described) return CommandError(session, jsonCommand, described.error());
        if (!described->className.ends_with("Class") && !described->name.starts_with("Default__"))
            filter = described->ref.address;
    }
    const Request& request = command.request;
    const bool dry = (request.flags & ue::CallsDry) != 0;
    auto started = ue::CallsStart(client, ue::GetRef(request), request.text, request.args[1], filter, dry);
    if (!started) return CommandError(session, jsonCommand, started.error());
    shape::CallsStart out;
    out.target = "function";
    out.function = Lookup(session, started->function).name;
    out.slot = started->slot;
    out.exec = started->original;
    out.thunk = started->thunk;
    out.blockSize = started->blockSize;
    if (filter) out.objectAddr = filter;
    out.durationMs = request.args[1];
    out.dry = dry;
    // A Blueprint function runs through the shared script entry: a call
    // from another Blueprint never goes through its pointer.
    if (auto info = ue::Function(client, ue::GetRef(request), request.text);
        info && !(info->flags & ue::flags::FunctionNative))
        out.note = "a Blueprint function: calls from other Blueprints are not seen; events through ProcessEvent are. "
                   "To see it used, record a native function it calls.";
    return Emit(session, jsonCommand, std::move(out));
}
}
