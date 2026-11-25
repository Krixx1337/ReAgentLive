// reagentlive CLI console: the pointer-swap recorders. native-calls-start
// (the core's native call recorder) and calls-read for every kind: the
// core's rows here, a pack's through pack.h PackCalls. A native row's return
// address is located in its module and named from the labels files: the
// function holding it (the module's exception directory) when a label names
// that function's start.
#include <map>
#include <string>
#include <unordered_map>
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/results/calls.h"
#include "reagentlive/core/client.h"

#include "reagentlive/frontends/cli/console/internal.h"
#include "reagentlive/frontends/cli/console/pack.h"

namespace reagentlive::cli::detail {
namespace {
constexpr auto NoRecorder = "this host does not offer the native call recorder";

// "Name+0x1c" for a return address inside a labelled function; empty when
// no label names the function holding it.
class ReturnNames {
public:
    ReturnNames(Session& session, std::span<const Module> modules) : session_(session), modules_(modules) {}
    std::string operator()(uint64_t address) {
        if (const auto found = cache_.find(address); found != cache_.end()) return found->second;
        std::string name;
        const Module* module = ModuleAt(address, modules_);
        if (!module) return cache_[address] = name;
        if (auto function = FunctionAt(session_, address); function && *function) {
            const auto& labels = Labels(*module);
            const uint64_t start = (*function)->range.start;
            if (const auto label = labels.find(start); label != labels.end())
                name = label->second + (address > start ? "+" + HexNumber(address - start) : "");
        }
        return cache_[address] = name;
    }

private:
    const std::map<uint64_t, std::string>& Labels(const Module& module) {
        auto [entry, added] = labels_.try_emplace(module.base);
        if (added)
            if (const auto timestamp = ImageTimestamp(session_, module)) entry->second = LabelNames(session_, module, *timestamp);
        return entry->second;
    }
    Session& session_;
    std::span<const Module> modules_;
    std::unordered_map<uint64_t, std::string> cache_;
    std::unordered_map<uint64_t, std::map<uint64_t, std::string>> labels_;
};
}

void RunCallsRead(Session& session, const Command& original, std::string_view jsonCommand) {
    Client& client = session.client;
    Command command = original;
    if (!client.Supports(op::RecorderRead) || (command.target == "stop" && !client.Supports(op::RecorderStop)))
        return JsonError(session, jsonCommand, ErrorKind::Unsupported, "this host does not offer the call recorders");
    const Client::Hold hold(client);
    if (command.target == "stop") {
        auto stopped = client.RecorderStop(command.callsSlot);
        if (!stopped) return CommandError(session, jsonCommand, stopped.error());
        command.callsSlot = stopped->slot;
    }
    auto read = client.RecorderRead(command.callsSlot, command.callsAfter);
    if (!read) return CommandError(session, jsonCommand, read.error());
    if (read->status.kind == NativeCallsKind) {
        const auto modules = LoadedModules(session);
        return Emit(session, jsonCommand, MakeNativeCalls(read->status, read->calls, command.callsAfter, read->last,
            modules, ReturnNames(session, modules)));
    }
    shape::Calls out = MakeCallsBase(read->status, read->calls, command.callsAfter, read->last);
    if (!PackCalls(session, *read, out))
        return JsonError(session, jsonCommand, ErrorKind::Validation, "slot " + std::to_string(read->status.slot) +
            " holds a recording this console cannot decode (kind " + std::to_string(read->status.kind) + ")");
    return Emit(session, jsonCommand, std::move(out));
}

void RunNativeCalls(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(op::NativeCallsStart) || !client.Supports(op::RecorderRead) || !client.Supports(op::RecorderStop))
        return JsonError(session, jsonCommand, ErrorKind::Unsupported, NoRecorder);
    NativeCallsSpec spec = command.nativeCalls;
    spec.address = command.request.address; // After a chain was followed.
    auto started = client.NativeCallsStart(spec);
    if (!started) return CommandError(session, jsonCommand, started.error());
    const bool vtable = spec.target == static_cast<uint32_t>(NativeTarget::Vtable);
    shape::NativeCallsStart out;
    out.slot = started->slot;
    out.target = vtable ? "vtable" : "cell";
    out.cell = started->cell;
    out.original = started->original;
    out.originalWhere = Locate(started->original, LoadedModules(session));
    out.thunk = started->thunk;
    if (vtable) {
        out.table = Addr(started->table);
        out.entry = spec.index;
    }
    out.durationMs = spec.milliseconds;
    out.dry = (spec.flags & NativeCallsDry) != 0;
    out.note = "only calls through this pointer are seen (virtual or pointer dispatch); direct and devirtualised "
               "calls of the same function are not";
    return Emit(session, jsonCommand, std::move(out));
}
}
