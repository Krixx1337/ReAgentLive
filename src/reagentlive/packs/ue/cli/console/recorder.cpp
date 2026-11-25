// reagentlive CLI console, Unreal pack: calls-start and calls-read
// with its stop (the agent's call recorder, protocol_ue.h). Rows are decoded by
// the recorded function's reflected parameters through the shared reader, as
// a copy: names and objects are looked up, memory the block points into is
// not read (the caller may have freed it since).
#include <algorithm>
#include <cstring>
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
const char* StateText(uint32_t state) {
    switch (static_cast<ue::CallsSlotState>(state)) {
    case ue::CallsSlotState::Recording: return "recording";
    case ue::CallsSlotState::Expired: return "expired";
    case ue::CallsSlotState::Finished: return "finished";
    case ue::CallsSlotState::Replaced: return "replaced";
    case ue::CallsSlotState::Free: break;
    }
    return "free";
}

// The recorded function's "Owner:Name" and parameters, from its address.
struct Recorded { std::string name; ue::Signature signature; bool known = false; };
Recorded Lookup(Session& session, uint64_t function) {
    Recorded out;
    auto described = ue::Describe(session.client, Ref{static_cast<uintptr_t>(function), -1, 0});
    if (described) out.name = described->name;
    auto signature = ue::Parameters(session.client, function);
    if (signature) {
        out.signature = std::move(*signature);
        out.known = true;
    }
    return out;
}

shape::Calls MakeCalls(Session& session, const ue::CallsState& state, std::span<const ue::CallsCall> calls,
                       uint64_t after, uint64_t last) {
    auto& structs = Ue(session).structs;
    const Recorded function = Lookup(session, state.function);
    shape::Calls out;
    out.function = function.name;
    out.state = StateText(state.state);
    out.slot = state.slot;
    out.calls = state.written;
    out.events = state.events;
    out.vm = state.vm;
    // Rows the agent no longer held when read: before the first one returned.
    const uint64_t first = calls.empty() ? last + 1 : calls.front().row.sequence;
    out.dropped = first > after + 1 ? first - after - 1 : 0;
    // The calling objects and every parameter, through one reader: its names,
    // objects and pointers resolved together, then printed.
    RowReader reader(session, structs, RowReader::PreviewNested);
    reader.Snapshot();
    reader.Meta(session.meta);
    const auto direction = [](const ue::Field& parameter) {
        return parameter.parameterFlags & ue::Return ? "return" : ue::InputParameter(parameter.parameterFlags) ? "in" : "out";
    };
    const auto caller = [](const ue::CallsCall& call) {
        uint8_t bytes[8];
        memcpy(bytes, &call.row.object, sizeof(bytes));
        return std::vector<uint8_t>(bytes, bytes + sizeof(bytes));
    };
    const auto each = [&](const ue::CallsCall& call, auto&& visit) {
        if (!function.known || call.block.empty()) return;
        for (const auto& parameter : function.signature.parameters)
            if (parameter.size && size_t{parameter.offset} + parameter.size <= call.block.size()) visit(parameter);
    };
    for (const auto& call : calls) {
        reader.Named("ObjectProperty", caller(call));
        each(call, [&](const ue::Field& parameter) {
            bool ignored = false;
            reader.Value(parameter, FieldBytes(call.block, parameter), ignored);
        });
    }
    reader.Resolve();
    for (const auto& call : calls) {
        shape::CallsRow row;
        row.sequence = call.row.sequence;
        row.atMs = static_cast<double>(call.row.microseconds) / 1000.0;
        row.thread = call.row.thread;
        row.via = call.row.via == ue::CallsEvent ? "event" : "vm";
        if (const auto* named = reader.Named("ObjectProperty", caller(call))) {
            row.ref = FullRef(named->ref);
            row.objectName = named->name;
            row.objectClass = named->className;
        } else {
            row.ref = {ReusableRef(Ref{static_cast<uintptr_t>(call.row.object), -1, 0}).text};
        }
        if (function.known && !call.block.empty()) {
            std::vector<shape::CallsValue> values;
            each(call, [&](const ue::Field& parameter) {
                shape::CallsValue value;
                value.name = parameter.name;
                value.direction = direction(parameter);
                bool partial = false;
                value.value = reader.Value(parameter, FieldBytes(call.block, parameter), partial);
                value.truncated = partial;
                values.push_back(std::move(value));
            });
            row.values = std::move(values);
        }
        out.rows.push_back(std::move(row));
    }
    if (last < state.written) out.nextCursor = last;
    if (reader.Unnamed()) out.unnamed = reader.Unnamed();
    return out;
}
}

void RunCalls(Session& session, const Command& original, std::string_view jsonCommand) {
    Client& client = session.client;
    Command command = original;
    if (!client.Supports(command.request.op) || (command.target == "stop" && !client.Supports(ue::op::CallsStop)))
        return JsonError(session, jsonCommand, ErrorKind::Unsupported, "this host does not offer the call recorder");
    if (command.target == "start") {
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
    if (command.target == "stop") {
        auto stopped = ue::CallsStop(client, command.callsSlot);
        if (!stopped) return CommandError(session, jsonCommand, stopped.error());
        command.callsSlot = stopped->slot;
    }
    auto read = ue::CallsRead(client, command.callsSlot, command.callsAfter);
    if (!read) return CommandError(session, jsonCommand, read.error());
    return Emit(session, jsonCommand, MakeCalls(session, read->state, read->calls, command.callsAfter, read->last));
}
}
