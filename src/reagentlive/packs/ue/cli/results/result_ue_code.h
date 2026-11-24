// reagentlive CLI, Unreal pack: result structs (results/schema.h) of the
// code: script (with decode), name-refs, labels-export.
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

namespace reagentlive::cli {
namespace shape {

// script: a Blueprint function's confirmed anchored entries by kind.
// `offset` is the first opcode's offset in the bytecode, `count` how often it occurs.
struct ScriptName {
    HexNum offset;
    uint32_t count = 0;
    int32_t index = 0;
    std::string name;
    template<class V> static void Describe(V& v) {
        v("offset", &ScriptName::offset);
        v("count", &ScriptName::count);
        v("index", &ScriptName::index);
        v("name", &ScriptName::name);
    }
};
struct ScriptFunction {
    HexNum offset;
    uint32_t count = 0;
    RefId ref;
    std::string function;
    template<class V> static void Describe(V& v) {
        v("offset", &ScriptFunction::offset);
        v("count", &ScriptFunction::count);
        v("ref", &ScriptFunction::ref);
        v("function", &ScriptFunction::function);
    }
};
struct ScriptProperty {
    HexNum offset;
    uint32_t count = 0;
    Addr address;
    std::string property;
    std::optional<std::string> raw; // `decode`: the reflected name where `property` shows a readable one.
    template<class V> static void Describe(V& v) {
        v("offset", &ScriptProperty::offset);
        v("count", &ScriptProperty::count);
        v("address", &ScriptProperty::address);
        v("property", &ScriptProperty::property);
        v("raw", &ScriptProperty::raw);
    }
};
// script with `decode`: one statement (a top-level expression) and its opcode's Script.h name.
struct ScriptStatement {
    HexNum offset;
    std::string op, text;
    template<class V> static void Describe(V& v) {
        v("offset", &ScriptStatement::offset);
        v("op", &ScriptStatement::op);
        v("text", &ScriptStatement::text);
    }
};
// script with `decode` of an uber graph: where an event enters it (script with `entry`).
struct ScriptEvent {
    HexNum offset;
    std::string event;
    template<class V> static void Describe(V& v) {
        v("offset", &ScriptEvent::offset);
        v("event", &ScriptEvent::event);
    }
};
struct Script {
    RefId ref;
    std::string function;
    uint32_t size = 0;
    std::vector<ScriptName> names;
    std::vector<ScriptFunction> functions;
    std::vector<ScriptProperty> properties;
    std::optional<std::vector<ScriptStatement>> statements;
    std::optional<std::vector<ScriptEvent>> events;
    std::optional<std::string> stopped; // decode stopped early: why ("unknown 0xNN at +0xOFF").
    std::optional<Filtered> filtered;   // view.match: statements kept of those decoded.
    template<class V> static void Describe(V& v) {
        v("ref", &Script::ref);
        v("function", &Script::function);
        v("size", &Script::size);
        v("names", &Script::names);
        v("functions", &Script::functions);
        v("properties", &Script::properties);
        v("statements", &Script::statements);
        v("events", &Script::events);
        v("stopped", &Script::stopped);
        v("filtered", &Script::filtered);
    }
};
struct RefsRow {
    RefId ref;
    std::string owner, name;
    HexNum offset;
    uint32_t count = 0;
    template<class V> static void Describe(V& v) {
        v("ref", &RefsRow::ref);
        v("owner", &RefsRow::owner);
        v("name", &RefsRow::name);
        v("offset", &RefsRow::offset);
        v("count", &RefsRow::count);
    }
};
struct Refs {
    int32_t index = 0;
    std::optional<std::string> name;
    std::vector<RefsRow> rows;
    bool capped = false;
    template<class V> static void Describe(V& v) {
        v("index", &Refs::index);
        v("name", &Refs::name);
        v("rows", &Refs::rows);
        v("capped", &Refs::capped);
    }
};

struct ExportLabels {
    std::string path, module;
    uint32_t timestamp = 0;
    uint64_t total = 0, globals = 0, user = 0, exec = 0, vtables = 0, interfaces = 0, interfaceLookups = 0;
    uint64_t otherBuilds = 0;   // Lines of other builds, kept as they were.
    bool capped = false;
    template<class V> static void Describe(V& v) {
        v("path", &ExportLabels::path);
        v("module", &ExportLabels::module);
        v("timestamp", &ExportLabels::timestamp);
        v("total", &ExportLabels::total);
        v("globals", &ExportLabels::globals);
        v("user", &ExportLabels::user);
        v("exec", &ExportLabels::exec);
        v("vtables", &ExportLabels::vtables);
        v("interfaces", &ExportLabels::interfaces);
        v("interface_lookups", &ExportLabels::interfaceLookups);
        v("other_builds", &ExportLabels::otherBuilds);
        v("capped", &ExportLabels::capped);
    }
};
}

inline shape::Script MakeScript(const Ref& ref, std::string_view function, const ue::ScriptResult& script) {
    shape::Script out;
    out.ref = ReusableRef(ref);
    out.function = function;
    out.size = script.size;
    for (const auto& entry : script.entries) {
        switch (entry.kind) {
        case ue::ScriptKind::Name: out.names.push_back({entry.offset, entry.count, entry.name, entry.text}); break;
        case ue::ScriptKind::Function:
            out.functions.push_back({entry.offset, entry.count, FullRef(entry.object), entry.text});
            break;
        case ue::ScriptKind::Property:
            out.properties.push_back({entry.offset, entry.count, entry.pointer, entry.text});
            break;
        }
    }
    return out;
}

inline shape::Refs MakeRefs(int32_t index, std::string_view name, const ue::RefsResult& refs) {
    shape::Refs out;
    out.index = index;
    if (!name.empty()) out.name = std::string(name);
    for (const auto& row : refs.rows) out.rows.push_back({FullRef(row.function), row.owner, row.name, row.offset, row.count});
    out.capped = refs.capped;
    return out;
}

}
