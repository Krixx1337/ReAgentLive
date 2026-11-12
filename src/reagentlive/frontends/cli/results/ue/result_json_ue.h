// reagentlive CLI, Unreal pack: results of the object, field and function
// commands as typed structs (results/schema.h), built from client results
// only (no I/O). The conventions are in results/result_json.h; the verb
// table naming each verb's type is results/ue/verbs_ue.h.
#pragma once
#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/frontends/cli/values/ue/json_output.h"
#include "reagentlive/frontends/cli/state/ue/native_names.h"
#include "reagentlive/ue/client_ue.h"

namespace reagentlive::cli {
namespace shape {
struct Find {
    std::vector<Object> rows;
    std::optional<uint64_t> total;      // Only the first page counts.
    uint32_t from = 0;
    std::optional<uint32_t> nextCursor;
    template<class V> static void Describe(V& v) {
        v("rows", &Find::rows);
        v("total", &Find::total);
        v("from", &Find::from);
        v("next_cursor", &Find::nextCursor);
    }
};
struct FunctionName {
    std::optional<std::string> owner;
    std::string name;
    template<class V> static void Describe(V& v) {
        v("owner", &FunctionName::owner);
        v("name", &FunctionName::name);
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

// A reflected field's layout, as fields, dump and params list it.
struct FieldInfo {
    static constexpr std::string_view TypeName = "ue.FieldInfo";
    std::string name, owner, type;
    uint32_t offset = 0, size = 0;
    std::optional<std::string> structName;
    std::optional<uint32_t> boolMask;
    std::optional<std::string> elementType;
    std::optional<uint32_t> elementSize;
    std::optional<std::string> valueType;
    std::optional<uint32_t> valueSize, valueOffset;
    std::optional<std::string> valueStruct;
    std::optional<uint32_t> stride;
    template<class V> static void Describe(V& v) {
        v("name", &FieldInfo::name);
        v("owner", &FieldInfo::owner);
        v("type", &FieldInfo::type);
        v("offset", &FieldInfo::offset);
        v("size", &FieldInfo::size);
        v("struct", &FieldInfo::structName);
        v("bool_mask", &FieldInfo::boolMask);
        v("element_type", &FieldInfo::elementType);
        v("element_size", &FieldInfo::elementSize);
        v("value_type", &FieldInfo::valueType);
        v("value_size", &FieldInfo::valueSize);
        v("value_offset", &FieldInfo::valueOffset);
        v("value_struct", &FieldInfo::valueStruct);
        v("stride", &FieldInfo::stride);
    }
};
// A fields row: the layout, the readable name beside an obfuscated one, and
// for a member from the native file `native` (`stale` when it was labelled
// under another build of the main image).
struct FieldRow {
    FieldInfo field;
    std::string label;
    std::optional<bool> native, stale;
    template<class V> static void Describe(V& v) {
        v.flat("", &FieldRow::field);
        v("label", &FieldRow::label);
        v("native", &FieldRow::native);
        v("stale", &FieldRow::stale);
    }
};
struct NativeRefused {
    uint64_t line = 0;
    std::string name, owner;
    uint32_t offset = 0;
    std::string reason;
    template<class V> static void Describe(V& v) {
        v("line", &NativeRefused::line);
        v("name", &NativeRefused::name);
        v("owner", &NativeRefused::owner);
        v("offset", &NativeRefused::offset);
        v("reason", &NativeRefused::reason);
    }
};
struct Fields {
    RefId ref;
    std::vector<FieldRow> rows;
    bool capped = false;
    std::optional<std::vector<NativeRefused>> nativeRefused;
    template<class V> static void Describe(V& v) {
        v("ref", &Fields::ref);
        v("rows", &Fields::rows);
        v("capped", &Fields::capped);
        v("native_refused", &Fields::nativeRefused);
    }
};

// One previewed element: an array's or set's `value`, a map's `key` and
// `value`, each with the object a pointer names.
struct PreviewElement {
    std::optional<Json> key;
    Json value;
    std::optional<Object> object, keyObject, valueObject;
    template<class V> static void Describe(V& v) {
        v("key", &PreviewElement::key);
        v("value", &PreviewElement::value);
        v("object", &PreviewElement::object);
        v("key_object", &PreviewElement::keyObject);
        v("value_object", &PreviewElement::valueObject);
    }
};
struct Preview {
    uint64_t total = 0;
    std::vector<PreviewElement> elements;
    template<class V> static void Describe(V& v) {
        v("total", &Preview::total);
        v("elements", &Preview::elements);
    }
};
// A dump row, compact: the name (the readable one where it was recovered,
// with the reflected one as `raw` beside it), the offset and the value;
// `before` with --changed, the object a pointer names and the first elements
// of a container. The layout (owner, type, size, bool mask) is `meta`, with
// --meta; `fields` is the layout command.
struct DumpRow {
    std::string name;
    std::optional<std::string> raw;
    uint32_t offset = 0;
    std::optional<bool> native, stale;
    Json value;
    std::optional<Json> before;
    std::optional<bool> truncated;
    Location valueWhere;
    std::optional<Object> object;
    std::optional<Preview> preview;
    std::optional<std::string> previewError;
    std::optional<FieldInfo> meta;
    template<class V> static void Describe(V& v) {
        v("name", &DumpRow::name);
        v("raw", &DumpRow::raw);
        v("offset", &DumpRow::offset);
        v("native", &DumpRow::native);
        v("stale", &DumpRow::stale);
        v("value", &DumpRow::value);
        v("before", &DumpRow::before);
        v("truncated", &DumpRow::truncated);
        v.flat("value_", &DumpRow::valueWhere);
        v("object", &DumpRow::object);
        v("preview", &DumpRow::preview);
        v("preview_error", &DumpRow::previewError);
        v("meta", &DumpRow::meta);
    }
};
struct Filtered {
    uint64_t shown = 0, total = 0;
    template<class V> static void Describe(V& v) {
        v("shown", &Filtered::shown);
        v("total", &Filtered::total);
    }
};
struct Dump {
    RefId ref;
    std::vector<DumpRow> rows;
    bool truncated = false, capped = false, objectLookupLimited = false;
    std::optional<Filtered> filtered;
    std::optional<std::vector<NativeRefused>> nativeRefused;
    std::optional<bool> tickAccurate;
    template<class V> static void Describe(V& v) {
        v("ref", &Dump::ref);
        v("rows", &Dump::rows);
        v("truncated", &Dump::truncated);
        v("capped", &Dump::capped);
        v("object_lookup_limited", &Dump::objectLookupLimited);
        v("filtered", &Dump::filtered);
        v("native_refused", &Dump::nativeRefused);
        v("tick_accurate", &Dump::tickAccurate);
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
    }
};
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
struct Params {
    RefId ref;
    std::string function;
    Addr address;
    uint32_t blockSize = 0;
    std::vector<ParamRow> rows;
    template<class V> static void Describe(V& v) {
        v("ref", &Params::ref);
        v("function", &Params::function);
        v("address", &Params::address);
        v("block_size", &Params::blockSize);
        v("rows", &Params::rows);
    }
};

struct NameRow {
    static constexpr std::string_view TypeName = "ue.Name";
    int32_t index = 0;
    std::optional<std::string> name;    // Absent: no pooled name at that index.
    template<class V> static void Describe(V& v) {
        v("index", &NameRow::index);
        v("name", &NameRow::name);
    }
};
struct Names {
    std::vector<NameRow> rows;
    template<class V> static void Describe(V& v) { v("rows", &Names::rows); }
};
struct NameFind {
    std::vector<NameRow> rows;
    uint32_t from = 0;
    std::optional<uint32_t> nextCursor;
    template<class V> static void Describe(V& v) {
        v("rows", &NameFind::rows);
        v("from", &NameFind::from);
        v("next_cursor", &NameFind::nextCursor);
    }
};

// entries: one page of an array, set or map.
struct EntryRow {
    std::optional<Json> key;
    Json value;
    std::optional<Object> keyObject, valueObject;
    template<class V> static void Describe(V& v) {
        v("key", &EntryRow::key);
        v("value", &EntryRow::value);
        v("key_object", &EntryRow::keyObject);
        v("value_object", &EntryRow::valueObject);
    }
};
struct Entries {
    RefId ref;
    std::string field, kind;
    uint64_t total = 0;
    std::vector<EntryRow> rows;
    uint32_t from = 0;
    std::optional<uint32_t> nextCursor;
    bool truncated = false;
    std::optional<bool> tickAccurate;
    template<class V> static void Describe(V& v) {
        v("ref", &Entries::ref);
        v("field", &Entries::field);
        v("kind", &Entries::kind, "array|set|map");
        v("total", &Entries::total);
        v("rows", &Entries::rows);
        v("from", &Entries::from);
        v("next_cursor", &Entries::nextCursor);
        v("truncated", &Entries::truncated);
        v("tick_accurate", &Entries::tickAccurate);
    }
};

// script: a Blueprint function's confirmed anchored entries by kind.
// `offset` is the first opcode's offset in the bytecode, `count` how often it occurs.
struct ScriptName {
    uint32_t offset = 0, count = 0;
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
    uint32_t offset = 0, count = 0;
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
    uint32_t offset = 0, count = 0;
    Addr address;
    std::string property;
    template<class V> static void Describe(V& v) {
        v("offset", &ScriptProperty::offset);
        v("count", &ScriptProperty::count);
        v("address", &ScriptProperty::address);
        v("property", &ScriptProperty::property);
    }
};
struct Script {
    RefId ref;
    std::string function;
    uint32_t size = 0;
    std::vector<ScriptName> names;
    std::vector<ScriptFunction> functions;
    std::vector<ScriptProperty> properties;
    template<class V> static void Describe(V& v) {
        v("ref", &Script::ref);
        v("function", &Script::function);
        v("size", &Script::size);
        v("names", &Script::names);
        v("functions", &Script::functions);
        v("properties", &Script::properties);
    }
};
struct RefsRow {
    RefId ref;
    std::string owner, name;
    uint32_t offset = 0, count = 0;
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

// each: a table, one row per live instance with a value per column, in the
// columns' order. A call's value is its return value, or {return, outputs}.
// A cell that failed is null, with an `errors` entry naming its row and
// column. With --seconds (one column) also the polls and the changes seen.
struct EachRow {
    RefId ref;
    std::vector<Json> values;
    std::optional<HexBytes> hex;    // --meta, one column read in the instance: its bytes.
    template<class V> static void Describe(V& v) {
        v("ref", &EachRow::ref);
        v("values", &EachRow::values);
        v("hex", &EachRow::hex);
    }
};
struct EachError {
    RefId ref;
    uint32_t column = 0;
    std::string message;
    template<class V> static void Describe(V& v) {
        v("ref", &EachError::ref);
        v("column", &EachError::column);
        v("message", &EachError::message);
    }
};
struct EachChange {
    int64_t ms = 0;
    uint64_t tick = 0;
    RefId ref;
    Json before, after;
    template<class V> static void Describe(V& v) {
        v("ms", &EachChange::ms);
        v("tick", &EachChange::tick);
        v("ref", &EachChange::ref);
        v("before", &EachChange::before);
        v("after", &EachChange::after);
    }
};
// each's passes: the [begin, end) spans its columns need inside an instance,
// grouped so each pass reads at most `most` bytes. Sorted by begin.
inline std::vector<std::pair<uint32_t, uint32_t>> EachPasses(std::vector<std::pair<uint32_t, uint32_t>> spans, uint32_t most) {
    std::ranges::sort(spans);
    std::vector<std::pair<uint32_t, uint32_t>> passes;
    for (const auto& [begin, end] : spans) {
        if (!passes.empty() && end <= passes.back().first + most) {
            passes.back().second = std::max(passes.back().second, end);
            continue;
        }
        passes.emplace_back(begin, end);
    }
    return passes;
}

// `same_tick`: every value came from one agent pass (one column window, no
// path through a pointer).
struct Each {
    std::string className;
    RefId classRef;
    std::vector<std::string> columns;
    std::string mode;
    std::vector<EachRow> rows;
    bool sameTick = false, capped = false, truncated = false;
    std::optional<std::vector<EachError>> errors;
    std::optional<uint32_t> seconds;
    std::optional<uint64_t> polls;
    std::optional<std::vector<EachChange>> changes;
    std::optional<std::string> pollError;
    template<class V> static void Describe(V& v) {
        v("class", &Each::className);
        v("class_ref", &Each::classRef);
        v("columns", &Each::columns);
        v("mode", &Each::mode, "field|call");
        v("rows", &Each::rows);
        v("same_tick", &Each::sameTick);
        v("capped", &Each::capped);
        v("truncated", &Each::truncated);
        v("errors", &Each::errors);
        v("seconds", &Each::seconds);
        v("polls", &Each::polls);
        v("changes", &Each::changes);
        v("poll_error", &Each::pollError);
    }
};

// call and release: the whole block after the call. A typed call adds its
// outputs; `kept`: the agent keeps the block because an output owns memory.
struct CallOutput {
    std::string name, direction;
    Json value;
    bool truncated = false;
    std::optional<FieldInfo> meta;      // --meta: the parameter's layout.
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
    template<class V> static void Describe(V& v) {
        v("ref", &Call::ref);
        v("function", &Call::function);
        v("mode", &Call::mode, "raw|typed|release");
        v("block_size", &Call::blockSize);
        v("hex", &Call::hex);
        v("kept", &Call::kept);
        v("kept_reason", &Call::keptReason);
        v("outputs", &Call::outputs);
        v("truncated", &Call::truncated);
    }
};

// label: the member as the native file now holds it.
struct NativeLabel {
    std::string path;
    uint64_t line = 0;
    std::string owner;
    uint32_t offset = 0;
    std::string type, name;
    uint32_t size = 0, timestamp = 0;
    template<class V> static void Describe(V& v) {
        v("path", &NativeLabel::path);
        v("line", &NativeLabel::line);
        v("owner", &NativeLabel::owner);
        v("offset", &NativeLabel::offset);
        v("type", &NativeLabel::type);
        v("name", &NativeLabel::name);
        v("size", &NativeLabel::size);
        v("timestamp", &NativeLabel::timestamp);
    }
};

// get: one row per path, in the order asked; a failed path keeps its place
// with `error` in place of the value. `object` is the holder of the last
// field, given only when the path crossed a pointer to reach it. `truncated`
// only when true; `type` and `hex` with --meta.
struct GetRow {
    std::optional<Json> value;
    std::optional<Addr> address;
    Location where;
    std::optional<RefId> object;
    std::optional<bool> truncated, native, stale;
    Location valueWhere;
    std::optional<std::string> type;
    std::optional<HexBytes> hex;
    std::optional<std::string> error;
    template<class V> static void Describe(V& v) {
        v("value", &GetRow::value);
        v("address", &GetRow::address);
        v.flat("", &GetRow::where);
        v("object", &GetRow::object);
        v("truncated", &GetRow::truncated);
        v("native", &GetRow::native);
        v("stale", &GetRow::stale);
        v.flat("value_", &GetRow::valueWhere);
        v("type", &GetRow::type);
        v("hex", &GetRow::hex);
        v("error", &GetRow::error);
    }
};
// `same_tick` covers the value reads, not the lookups before them.
struct Get {
    RefId ref;
    std::vector<GetRow> rows;
    bool sameTick = false;
    std::optional<bool> tickAccurate;
    template<class V> static void Describe(V& v) {
        v("ref", &Get::ref);
        v("rows", &Get::rows);
        v("same_tick", &Get::sameTick);
        v("tick_accurate", &Get::tickAccurate);
    }
};
struct Snap {
    RefId ref;
    uint64_t size = 0;
    std::optional<std::string> path;
    template<class V> static void Describe(V& v) {
        v("ref", &Snap::ref);
        v("size", &Snap::size);
        v("path", &Snap::path);
    }
};
struct Record {
    RefId ref;
    std::string path;
    uint64_t total = 0;                 // Samples written.
    template<class V> static void Describe(V& v) {
        v("ref", &Record::ref);
        v("path", &Record::path);
        v("total", &Record::total);
    }
};
struct ExportLabels {
    std::string path, module;
    uint32_t timestamp = 0;
    uint64_t total = 0, globals = 0, exec = 0, vtables = 0, interfaces = 0, interfaceLookups = 0;
    bool capped = false;
    template<class V> static void Describe(V& v) {
        v("path", &ExportLabels::path);
        v("module", &ExportLabels::module);
        v("timestamp", &ExportLabels::timestamp);
        v("total", &ExportLabels::total);
        v("globals", &ExportLabels::globals);
        v("exec", &ExportLabels::exec);
        v("vtables", &ExportLabels::vtables);
        v("interfaces", &ExportLabels::interfaces);
        v("interface_lookups", &ExportLabels::interfaceLookups);
        v("capped", &ExportLabels::capped);
    }
};
}

inline shape::Object MakeObject(const ue::Object& object) {
    shape::Object row{FullRef(object.ref), object.name, object.className, {}};
    if (object.size) row.classSize = object.size;
    return row;
}

inline shape::Find MakeFind(const ue::FindResult& found, uint32_t from) {
    shape::Find out;
    for (const auto& object : found.objects) out.rows.push_back(MakeObject(object));
    if (!from) out.total = found.total;
    out.from = from;
    if (found.next) out.nextCursor = found.next;
    return out;
}

// The agent lists functions as "Owner:Name".
inline shape::Funcs MakeFuncs(const Ref& ref, const ue::FunctionList& list, uint32_t from) {
    shape::Funcs out;
    out.ref = ReusableRef(ref);
    for (const auto& text : list.names) {
        const auto colon = text.find(':');
        if (colon == std::string::npos) out.rows.push_back({std::nullopt, text});
        else out.rows.push_back({text.substr(0, colon), text.substr(colon + 1)});
    }
    out.from = from;
    if (list.next) out.nextCursor = list.next;
    return out;
}

inline shape::FieldInfo MakeFieldInfo(const ue::Field& field) {
    shape::FieldInfo info;
    info.name = field.name;
    info.owner = field.owner;
    info.type = field.type;
    info.offset = field.offset;
    info.size = field.size;
    if (!field.structName.empty()) info.structName = field.structName;
    if (field.boolMask) info.boolMask = field.boolMask;
    if (field.elementSize) {
        info.elementType = field.elementType;
        info.elementSize = field.elementSize;
    }
    if (field.valueSize) {
        info.valueType = field.valueType;
        info.valueSize = field.valueSize;
        info.valueOffset = field.valueOffset;
        if (!field.valueStructName.empty()) info.valueStruct = field.valueStructName;
    }
    if (field.stride) info.stride = field.stride;
    return info;
}

// A member from the native file as a field's layout.
inline shape::FieldInfo MakeFieldInfo(const NativeMember& member) {
    shape::FieldInfo info;
    info.name = member.field.name;
    info.owner = member.owner;
    info.type = NativeType(member.field);
    info.offset = member.field.offset;
    info.size = static_cast<uint32_t>(member.field.Size());
    return info;
}

inline shape::FieldRow MakeNativeFieldRow(const NativeMember& member, bool stale) {
    shape::FieldRow row{MakeFieldInfo(member), member.field.name, true, {}};
    if (stale) row.stale = true;
    return row;
}

// `label(field)` is the readable name shown beside an obfuscated one.
template<class LabelOf>
shape::Fields MakeFields(const Ref& ref, std::span<const ue::Field> sorted, const LabelOf& label) {
    shape::Fields out;
    out.ref = ReusableRef(ref);
    for (const auto& field : sorted) out.rows.push_back({MakeFieldInfo(field), label(field), {}, {}});
    out.capped = sorted.size() >= ue::MaxFieldStart;
    return out;
}

// Field rows (reflected and native) in offset order.
template<class Row> void SortRowsByOffset(std::vector<Row>& rows) {
    const auto offset = [](const Row& row) {
        if constexpr (requires { row.field.offset; }) return row.field.offset;
        else return row.offset;
    };
    std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) { return offset(a) < offset(b); });
}

inline std::vector<shape::NativeRefused> MakeNativeRefused(std::span<const NativeCheck> refused) {
    std::vector<shape::NativeRefused> rows;
    for (const auto& check : refused)
        rows.push_back({check.member->line, check.member->field.name, check.member->owner, check.member->field.offset,
            check.problem});
    return rows;
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
        flags & ue::Reference ? (flags & ue::Output ? "in/out ref" : "in ref") :
        flags & ue::Output ? "out" : "in";
}

inline shape::Params MakeParams(const Ref& ref, const ue::FunctionInfo& function, const ue::Signature& signature) {
    shape::Params out;
    out.ref = ReusableRef(ref);
    out.function = function.owner + ":" + function.name;
    out.address = function.address;
    out.blockSize = signature.size;
    for (const auto& parameter : signature.parameters)
        out.rows.push_back({MakeFieldInfo(parameter), ParameterDirection(parameter.parameterFlags), parameter.parameterFlags});
    return out;
}

inline shape::Names MakeNames(std::span<const int32_t> indices, std::span<const std::string> names) {
    shape::Names out;
    for (size_t i = 0; i < indices.size(); ++i) {
        shape::NameRow row{indices[i], {}};
        if (i < names.size() && !names[i].empty()) row.name = names[i];
        out.rows.push_back(std::move(row));
    }
    return out;
}

inline shape::NameFind MakeNameFind(const ue::NameFindResult& found, uint32_t from) {
    shape::NameFind out;
    for (const auto& name : found.names) out.rows.push_back({name.index, name.name});
    out.from = from;
    if (found.next) out.nextCursor = found.next;
    return out;
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
