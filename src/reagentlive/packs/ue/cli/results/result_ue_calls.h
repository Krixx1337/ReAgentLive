// reagentlive CLI, Unreal pack: result structs (results/schema.h) of the
// functions and calls: funcs, func, call and release, trace, calls-start, calls-read, game-thread.
// Built from client results only (no I/O); conventions in results/result_json.h,
// the verb table naming each verb's type in verbs_ue.h.
#pragma once
#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/packs/ue/cli/values/json_output.h"
#include "reagentlive/packs/ue/cli/state/native_names.h"
#include "reagentlive/packs/ue/client_ue.h"
#include "reagentlive/packs/ue/cli/results/result_ue_fields.h"

namespace reagentlive::cli {
namespace shape {
struct FunctionName {
    std::optional<std::string> owner;
    std::string name;
    // "interface": declared on a native interface the class implements, so
    // only on the interface class; func shows its implementation, call refuses it.
    std::optional<std::string> via;
    template<class V> static void Describe(V& v) {
        v("owner", &FunctionName::owner);
        v("name", &FunctionName::name);
        v("via", &FunctionName::via);
    }
};
struct Funcs {
    RefId ref;
    std::vector<FunctionName> rows;
    uint32_t from = 0;
    std::optional<uint32_t> nextCursor;
    template<class V> static void Describe(V& v) {
        v("ref", &Funcs::ref);
        v("rows", &Funcs::rows);
        v("from", &Funcs::from);
        v("next_cursor", &Funcs::nextCursor);
    }
};

struct Interface {
    HexNum offset;
    std::optional<int64_t> slot;
    std::optional<Addr> implementation;
    Location implementationWhere;
    template<class V> static void Describe(V& v) {
        v("offset", &Interface::offset);
        v("slot", &Interface::slot);
        v("implementation", &Interface::implementation);
        v.flat("implementation_", &Interface::implementationWhere);
    }
};
// func: reflected metadata and the exec entry (absent for none). `interface`
// for a function an interface declares: calls are refused, since the stub
// would run another function on this object.
struct ParamRow {
    FieldInfo field;
    std::string direction;
    uint32_t flags = 0;
    template<class V> static void Describe(V& v) {
        v.flat("", &ParamRow::field);
        v("direction", &ParamRow::direction, "in|out|in ref|in/out ref|return");
        v("flags", &ParamRow::flags);
    }
};
struct Func {
    RefId ref;
    std::string function;
    Addr address;
    uint32_t flags = 0;
    std::optional<Addr> exec;
    bool executable = false;
    Location where;
    std::optional<std::string> moduleError;
    std::optional<Interface> viaInterface;
    std::optional<uint32_t> blockSize;
    std::optional<std::vector<ParamRow>> params;
    std::optional<std::string> paramsError;
    template<class V> static void Describe(V& v) {
        v("ref", &Func::ref);
        v("function", &Func::function);
        v("address", &Func::address);
        v("flags", &Func::flags);
        v("exec", &Func::exec);
        v("executable", &Func::executable);
        v.flat("", &Func::where);
        v("module_error", &Func::moduleError);
        v("interface", &Func::viaInterface);
        v("block_size", &Func::blockSize);
        v("params", &Func::params);
        v("params_error", &Func::paramsError);
    }
};

// game-thread: the host's Draw tick (host_draw.h).
struct GameThread {
    std::string mode;                   // off, waiting, conflict, on, stopped.
    std::string text;
    std::optional<uint32_t> thread;     // The game thread's ID, once known.
    std::optional<Addr> viewport, table, draw;
    Location drawWhere;
    template<class V> static void Describe(V& v) {
        v("mode", &GameThread::mode, "off|waiting|conflict|on|stopped");
        v("text", &GameThread::text);
        v("thread", &GameThread::thread);
        v("viewport", &GameThread::viewport);
        v("table", &GameThread::table);
        v("draw", &GameThread::draw);
        v.flat("draw_", &GameThread::drawWhere);
    }
};

// call and release: the whole block after the call. A typed call adds its
// outputs; `kept`: the agent keeps the block because an output owns memory.
struct CallOutput {
    std::string name, direction;
    Json value;
    bool truncated = false;
    std::optional<FieldInfo> meta;      // `meta`: the parameter's layout.
    template<class V> static void Describe(V& v) {
        v("name", &CallOutput::name);
        v("direction", &CallOutput::direction, "out|return");
        v("value", &CallOutput::value);
        v("truncated", &CallOutput::truncated);
        v("meta", &CallOutput::meta);
    }
};
struct Call {
    RefId ref;
    std::string function, mode;
    uint64_t blockSize = 0;
    HexBytes hex;
    bool kept = false;
    std::optional<std::string> keptReason;
    std::optional<std::vector<CallOutput>> outputs;
    std::optional<bool> truncated;
    // `dry`: the function's native entry and whether it is const or pure.
    std::optional<Addr> exec;
    std::optional<bool> pure;
    std::optional<uint32_t> unnamed;    // Distinct pointers past the lookup cap, left as addresses.
    template<class V> static void Describe(V& v) {
        v("ref", &Call::ref);
        v("function", &Call::function);
        v("mode", &Call::mode, "raw|typed|release|dry");
        v("block_size", &Call::blockSize);
        v("hex", &Call::hex);
        v("kept", &Call::kept);
        v("kept_reason", &Call::keptReason);
        v("outputs", &Call::outputs);
        v("truncated", &Call::truncated);
        v("exec", &Call::exec);
        v("pure", &Call::pure);
        v("unnamed", &Call::unnamed);
    }
};

// trace: the first blocking hit on the line, decoded by the engine
// (GameplayStatics.BreakHitResult). Absent fields: nothing was hit, or the hit has none.
struct Trace {
    std::vector<double> start, end;
    std::string channel;
    bool complex = true;
    std::optional<RefId> ignored;
    bool hit = false;
    std::optional<bool> initialOverlap;
    std::optional<double> distance;
    std::optional<std::vector<double>> impact, normal;
    std::optional<Object> actor, component, material;
    std::optional<std::string> bone;
    std::optional<int32_t> face, item;
    template<class V> static void Describe(V& v) {
        v("start", &Trace::start);
        v("end", &Trace::end);
        v("channel", &Trace::channel);
        v("complex", &Trace::complex);
        v("ignored", &Trace::ignored);
        v("hit", &Trace::hit);
        v("initial_overlap", &Trace::initialOverlap);
        v("distance", &Trace::distance);
        v("impact", &Trace::impact);
        v("normal", &Trace::normal);
        v("actor", &Trace::actor);
        v("component", &Trace::component);
        v("material", &Trace::material);
        v("bone", &Trace::bone);
        v("face", &Trace::face);
        v("item", &Trace::item);
    }
};
// calls-start: the recording armed (or with `dry`, what would be swapped).
struct CallsStart {
    std::string function;
    uint32_t slot = 0;
    Addr exec, thunk;
    uint32_t blockSize = 0;
    std::optional<Addr> objectAddr;
    uint64_t durationMs = 0;
    bool dry = false;
    std::optional<std::string> note;    // What the recording will not see, when that is known up front.
    template<class V> static void Describe(V& v) {
        v("function", &CallsStart::function);
        v("slot", &CallsStart::slot);
        v("exec", &CallsStart::exec);
        v("thunk", &CallsStart::thunk);
        v("block_size", &CallsStart::blockSize);
        v("object_addr", &CallsStart::objectAddr);
        v("duration_ms", &CallsStart::durationMs);
        v("dry", &CallsStart::dry);
        v("note", &CallsStart::note);
    }
};
// One parameter of a recorded call, decoded by reflection.
struct CallsValue {
    std::string name, direction;
    Json value;
    bool truncated = false;
    template<class V> static void Describe(V& v) {
        v("name", &CallsValue::name);
        v("direction", &CallsValue::direction, "in|out|return");
        v("value", &CallsValue::value);
        v("truncated", &CallsValue::truncated);
    }
};
struct CallsRow {
    uint64_t sequence = 0;
    double atMs = 0;                    // Since the recording began.
    uint32_t thread = 0;
    RefId ref;
    std::optional<std::string> objectName, objectClass;
    std::string via;
    std::optional<std::vector<CallsValue>> values;
    template<class V> static void Describe(V& v) {
        v("sequence", &CallsRow::sequence);
        v("at_ms", &CallsRow::atMs);
        v("thread", &CallsRow::thread);
        v("ref", &CallsRow::ref);
        v("object_name", &CallsRow::objectName);
        v("object_class", &CallsRow::objectClass);
        v("via", &CallsRow::via, "event|vm");
        v("values", &CallsRow::values);
    }
};
// calls-read, with or without stop: the recording's state and its rows.
struct Calls {
    std::string function, state;
    uint32_t slot = 0;
    uint64_t calls = 0, events = 0, vm = 0, dropped = 0;
    std::vector<CallsRow> rows;
    std::optional<uint64_t> nextCursor;
    std::optional<uint32_t> unnamed;    // Distinct pointers past the lookup cap, left as addresses.
    template<class V> static void Describe(V& v) {
        v("function", &Calls::function);
        v("state", &Calls::state, "recording|expired|finished|replaced");
        v("slot", &Calls::slot);
        v("calls", &Calls::calls);
        v("events", &Calls::events);
        v("vm", &Calls::vm);
        v("dropped", &Calls::dropped);
        v("rows", &Calls::rows);
        v("next_cursor", &Calls::nextCursor);
        v("unnamed", &Calls::unnamed);
    }
};
}

// The agent lists functions as "Owner:Name".
inline shape::Funcs MakeFuncs(const Ref& ref, const ue::FunctionList& list, uint32_t from) {
    shape::Funcs out;
    out.ref = ReusableRef(ref);
    for (size_t i = 0; i < list.names.size(); ++i) {
        const auto& text = list.names[i];
        const auto colon = text.find(':');
        if (colon == std::string::npos) out.rows.push_back({std::nullopt, text, std::nullopt});
        else out.rows.push_back({text.substr(0, colon), text.substr(colon + 1), std::nullopt});
        if (i < list.interfaces.size() && list.interfaces[i]) out.rows.back().via = "interface";
    }
    out.from = from;
    if (list.next) out.nextCursor = list.next;
    return out;
}

// `modules`: the loaded modules, or empty with `modulesError` when they could not be listed.
inline shape::Func MakeFunc(const Ref& ref, const ue::FunctionInfo& function, std::span<const Module> modules,
                            std::string_view modulesError = {}) {
    shape::Func out;
    out.ref = ReusableRef(ref);
    out.function = function.owner + ":" + function.name;
    out.address = function.address;
    out.flags = function.flags;
    out.exec = NonZero(function.exec);
    out.executable = function.executable;
    if (function.interface) {
        shape::Interface via;
        via.offset = function.interfaceOffset;
        if (function.slot >= 0) via.slot = function.slot / 8;
        via.implementation = NonZero(function.implementation);
        via.implementationWhere = Locate(function.implementation, modules);
        out.viaInterface = std::move(via);
    }
    if (!function.exec) return out;
    if (!modulesError.empty()) out.moduleError = std::string(modulesError);
    out.where = Locate(function.exec, modules);
    return out;
}

inline const char* ParameterDirection(uint64_t flags) {
    return flags & ue::Return ? "return" :
        flags & ue::Reference ? (flags & ue::Output && !(flags & ue::Const) ? "in/out ref" : "in ref") :
        flags & ue::Output ? "out" : "in";
}

// func's signature: the call block's size and its parameters in block order.
inline void AddSignature(shape::Func& out, const ue::Signature& signature) {
    out.blockSize = signature.size;
    out.params.emplace();
    for (const auto& parameter : signature.parameters)
        out.params->push_back({MakeFieldInfo(parameter), ParameterDirection(parameter.parameterFlags), parameter.parameterFlags});
}

}
