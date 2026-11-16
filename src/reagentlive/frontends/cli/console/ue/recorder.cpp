// reagentlive CLI console, Unreal pack: calls-start, calls-read and
// calls-stop (the agent's call recorder, protocol_ue.h). Rows are decoded by
// the recorded function's reflected parameters, as typed call outputs are.
#include <algorithm>
#include <cstring>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/values/ue/format_ue.h"
#include "reagentlive/frontends/cli/values/ue/json_output.h"
#include "reagentlive/core/client.h"
#include "reagentlive/ue/client_ue.h"

#include "reagentlive/frontends/cli/console/ue/internal_ue.h"

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
    // Name the calling objects in as few round trips as describe-many allows.
    std::vector<Ref> objects;
    for (const auto& call : calls) {
        const Ref ref{static_cast<uintptr_t>(call.row.object), -1, 0};
        if (ref.address && std::ranges::find(objects, ref) == objects.end()) objects.push_back(ref);
    }
    std::unordered_map<uint64_t, ue::Object> named;
    for (size_t at = 0; at < objects.size(); at += ue::MaxDescribeMany) {
        const auto page = std::span(objects).subspan(at, std::min<size_t>(ue::MaxDescribeMany, objects.size() - at));
        auto described = ue::DescribeMany(session.client, page);
        if (!described) break;
        for (size_t i = 0; i < page.size(); ++i)
            if ((*described)[i]) named.emplace(page[i].address, *(*described)[i]);
    }
    for (const auto& call : calls) {
        shape::CallsRow row;
        row.sequence = call.row.sequence;
        row.ms = static_cast<double>(call.row.microseconds) / 1000.0;
        row.thread = call.row.thread;
        row.via = call.row.via == ue::CallsEvent ? "event" : "vm";
        if (const auto found = named.find(call.row.object); found != named.end()) {
            row.object = FullRef(found->second.ref);
            row.objectName = found->second.name;
            row.objectClass = found->second.className;
        } else {
            row.object = {ReusableRef(Ref{static_cast<uintptr_t>(call.row.object), -1, 0}).text};
        }
        if (function.known && !call.block.empty()) {
            std::vector<shape::CallsValue> values;
            for (const auto& parameter : function.signature.parameters) {
                if (!parameter.size || size_t{parameter.offset} + parameter.size > call.block.size()) continue;
                shape::CallsValue value;
                value.name = parameter.name;
                value.direction = parameter.parameterFlags & ue::Return ? "return" :
                    ue::InputParameter(parameter.parameterFlags) ? "in" : "out";
                bool partial = false;
                value.value = JsonFieldValue(parameter, FieldBytes(call.block, parameter), structs,
                    format_detail::UnresolvedName{}, partial, Style(session));
                value.truncated = partial;
                values.push_back(std::move(value));
            }
            row.values = std::move(values);
        }
        out.rows.push_back(std::move(row));
    }
    if (last < state.written) out.nextCursor = last;
    return out;
}
}

void RunCalls(Session& session, const Command& original, std::string_view jsonCommand) {
    Client& client = session.client;
    Command command = original;
    if (!client.Supports(command.request.op))
        return JsonError(session, jsonCommand, "unsupported", "this host does not offer the call recorder");
    if (command.target == "start") {
        if (!command.className.empty()) {
            auto type = ResolveClass(session, command.className);
            if (!type) return CommandError(session, jsonCommand, type.error());
            ue::SetRef(command.request, *type);
        }
        const Request& request = command.request;
        const bool dry = (request.flags & ue::CallsDry) != 0;
        uint64_t filter = 0;
        if (request.length == sizeof(filter)) memcpy(&filter, request.data, sizeof(filter));
        auto started = ue::CallsStart(client, ue::GetRef(request), request.text, request.args[1], filter, dry);
        if (!started) return CommandError(session, jsonCommand, started.error());
        shape::CallsStart out;
        out.function = Lookup(session, started->function).name;
        out.slot = started->slot;
        out.exec = started->original;
        out.thunk = started->thunk;
        out.blockSize = started->blockSize;
        if (filter) out.object = filter;
        out.seconds = static_cast<double>(request.args[1]) / 1000.0;
        out.dry = dry;
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
