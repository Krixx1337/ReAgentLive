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
#include "reagentlive/packs/ue/cli/values/json_output.h"
#include "reagentlive/packs/ue/cli/state/native_names.h"
#include "reagentlive/packs/ue/client_ue.h"

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

// A reflected field's layout, as get with `mode` `types`, get with `meta` and func's params list it.
struct FieldInfo {
    static constexpr std::string_view TypeName = "ue.FieldInfo";
    std::string name, owner, type;
    HexNum offset;
    uint32_t size = 0;
    std::optional<std::string> structName;
    std::optional<uint32_t> boolMask;
    std::optional<std::string> elementType;
    std::optional<uint32_t> elementSize;
    std::optional<std::string> valueType;
    std::optional<uint32_t> valueSize;
    std::optional<HexNum> valueOffset;
    std::optional<std::string> valueStruct;
    std::optional<uint32_t> stride;
    // The class an object-like property points to or an enum or byte
    // property's enum (a container's element or key); a map value's.
    std::optional<std::string> subtype, valueSubtype;
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
        v("subtype", &FieldInfo::subtype);
        v("value_subtype", &FieldInfo::valueSubtype);
    }
};
struct NativeRefused {
    uint64_t line = 0;
    std::string name, owner;
    HexNum offset;
    std::string reason;
    template<class V> static void Describe(V& v) {
        v("line", &NativeRefused::line);
        v("name", &NativeRefused::name);
        v("owner", &NativeRefused::owner);
        v("offset", &NativeRefused::offset);
        v("reason", &NativeRefused::reason);
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
// get's row, one type for its three modes:
// - every field (no paths): name (the readable one where recovered, `raw`
//   the reflected one beside it), offset, value; `before` with `mode` `changed`,
//   `object` for a pointer, `preview` for a container; the layout is `meta`
//   with `meta`.
// - paths: `path` as asked, value, `address` (with module and rva), `holder`
//   when a pointer led there; `type` and `hex` with `meta`; a failed path
//   keeps its place with `error`.
// - `mode` `types`: name, raw, owner, type, offset, size and the container's parts; no value.
// `native` marks a member from the native file (`stale`: labelled under
// another build); `truncated` only when true.
struct GetRow {
    std::optional<std::string> path, name, raw, owner, type;
    std::optional<HexNum> offset;
    std::optional<uint32_t> size;
    std::optional<std::string> structName;
    std::optional<uint32_t> boolMask;
    std::optional<std::string> elementType;
    std::optional<uint32_t> elementSize;
    std::optional<std::string> valueType;
    std::optional<uint32_t> valueSize;
    std::optional<HexNum> valueOffset;
    std::optional<std::string> valueStruct;
    std::optional<uint32_t> stride;
    std::optional<std::string> subtype, valueSubtype;   // As FieldInfo's.
    std::optional<bool> native, stale;
    std::optional<Json> value, before;
    std::optional<bool> truncated;
    std::optional<Addr> address;
    Location where;
    std::optional<RefId> holder;
    Location valueWhere;
    std::optional<Object> object;
    std::optional<Preview> preview;
    std::optional<std::string> previewError;
    std::optional<FieldInfo> meta;
    std::optional<HexBytes> hex;
    std::optional<std::string> error;
    template<class V> static void Describe(V& v) {
        v("path", &GetRow::path);
        v("name", &GetRow::name);
        v("raw", &GetRow::raw);
        v("owner", &GetRow::owner);
        v("type", &GetRow::type);
        v("offset", &GetRow::offset);
        v("size", &GetRow::size);
        v("struct", &GetRow::structName);
        v("bool_mask", &GetRow::boolMask);
        v("element_type", &GetRow::elementType);
        v("element_size", &GetRow::elementSize);
        v("value_type", &GetRow::valueType);
        v("value_size", &GetRow::valueSize);
        v("value_offset", &GetRow::valueOffset);
        v("value_struct", &GetRow::valueStruct);
        v("subtype", &GetRow::subtype);
        v("value_subtype", &GetRow::valueSubtype);
        v("stride", &GetRow::stride);
        v("native", &GetRow::native);
        v("stale", &GetRow::stale);
        v("value", &GetRow::value);
        v("before", &GetRow::before);
        v("truncated", &GetRow::truncated);
        v("address", &GetRow::address);
        v.flat("", &GetRow::where);
        v("holder", &GetRow::holder);
        v.flat("value_", &GetRow::valueWhere);
        v("object", &GetRow::object);
        v("preview", &GetRow::preview);
        v("preview_error", &GetRow::previewError);
        v("meta", &GetRow::meta);
        v("hex", &GetRow::hex);
        v("error", &GetRow::error);
    }
};
struct Filtered {
    uint64_t shown = 0, total = 0;
    template<class V> static void Describe(V& v) {
        v("shown", &Filtered::shown);
        v("total", &Filtered::total);
    }
};
struct SnapInfo {
    uint64_t size = 0;
    std::optional<std::string> path;
    template<class V> static void Describe(V& v) {
        v("size", &SnapInfo::size);
        v("path", &SnapInfo::path);
    }
};
// get: `same_tick` (paths) covers the value reads, not the lookups before
// them; `truncated` and `capped` (every field, `mode` `types`) say whether the rows
// are complete; `snap` answers `mode` `snap`.
struct Get {
    RefId ref;
    std::vector<GetRow> rows;
    std::optional<bool> sameTick, truncated, capped;
    std::optional<Filtered> filtered;
    std::optional<std::vector<NativeRefused>> nativeRefused;
    std::optional<SnapInfo> snap;
    std::optional<bool> tickAccurate;
    std::optional<uint32_t> unnamed;    // Distinct pointers past the lookup cap, left as addresses.
    template<class V> static void Describe(V& v) {
        v("ref", &Get::ref);
        v("rows", &Get::rows);
        v("same_tick", &Get::sameTick);
        v("truncated", &Get::truncated);
        v("capped", &Get::capped);
        v("filtered", &Get::filtered);
        v("native_refused", &Get::nativeRefused);
        v("snap", &Get::snap);
        v("tick_accurate", &Get::tickAccurate);
        v("unnamed", &Get::unnamed);
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
    std::optional<uint32_t> unnamed;    // Distinct pointers past the lookup cap, left as addresses.
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
        v("unnamed", &Entries::unnamed);
        v("tick_accurate", &Entries::tickAccurate);
    }
};

// rows: a data table's rows, decoded with its RowStruct.
// A data table's row (value: its RowStruct decoded), or a curve table's
// curve (keys; value only with `at`, the curve evaluated there).
struct TableRow {
    std::string name;
    Addr address;
    std::optional<Json> value;
    std::optional<std::string> interp;  // A simple curve's interp mode: linear, constant, cubic, none.
    std::optional<Json> keys;           // [t, v] (rich: [t, v, interp, arrive, leave]) per key.
    std::optional<bool> weighted;       // Rich keys with weighted tangents; value evaluates them unweighted.
    template<class V> static void Describe(V& v) {
        v("name", &TableRow::name);
        v("address", &TableRow::address);
        v("value", &TableRow::value);
        v("interp", &TableRow::interp, "linear|constant|cubic|none");
        v("keys", &TableRow::keys);
        v("weighted", &TableRow::weighted);
    }
};
struct Rows {
    RefId ref;
    std::optional<std::string> rowStruct;   // A data table.
    std::optional<RefId> rowStructRef;
    std::optional<std::string> curve;       // A curve table: simple or rich.
    std::optional<double> at;               // `at`: the time each value is for.
    uint64_t total = 0;
    std::vector<TableRow> rows;
    uint32_t from = 0;
    std::optional<uint32_t> nextCursor;
    bool truncated = false;
    std::optional<uint32_t> unnamed;    // Distinct pointers past the lookup cap, left as addresses.
    template<class V> static void Describe(V& v) {
        v("ref", &Rows::ref);
        v("row_struct", &Rows::rowStruct);
        v("row_struct_ref", &Rows::rowStructRef);
        v("curve", &Rows::curve, "simple|rich");
        v("at", &Rows::at);
        v("total", &Rows::total);
        v("rows", &Rows::rows);
        v("from", &Rows::from);
        v("next_cursor", &Rows::nextCursor);
        v("truncated", &Rows::truncated);
        v("unnamed", &Rows::unnamed);
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

// enum: the names and values in the enum's order.
struct EnumRow {
    std::string name;
    int64_t value = 0;
    template<class V> static void Describe(V& v) {
        v("name", &EnumRow::name);
        v("value", &EnumRow::value);
    }
};
struct Enum {
    RefId ref;
    std::string name;
    std::vector<EnumRow> rows;
    template<class V> static void Describe(V& v) {
        v("ref", &Enum::ref);
        v("name", &Enum::name);
        v("rows", &Enum::rows);
    }
};

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

// each: a table, one row per live instance with a value per column, in the
// columns' order. A call's value is its return value, or {return, outputs}.
// A cell that failed is null, with an `errors` entry naming its row and
// column. With `seconds` (one column) also the polls and the changes seen.
struct EachRow {
    RefId ref;
    std::optional<std::string> name; // The instance's object name (absent when it was gone).
    std::vector<Json> values;
    std::optional<HexBytes> hex;    // `meta`, one column read in the instance: its bytes.
    template<class V> static void Describe(V& v) {
        v("ref", &EachRow::ref);
        v("name", &EachRow::name);
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
    std::optional<uint64_t> matched, omitted; // `where`: instances that matched; past `view.limit`: rows not shown.
    std::optional<uint32_t> unnamed;    // Distinct pointers past the lookup cap, left as addresses.
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
        v("matched", &Each::matched);
        v("omitted", &Each::omitted);
        v("unnamed", &Each::unnamed);
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

// label: the member as the native file now holds it.
struct NativeLabel {
    std::string path;
    uint64_t line = 0;
    std::string owner;
    HexNum offset;
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
    std::optional<Addr> object;
    double seconds = 0;
    bool dry = false;
    std::optional<std::string> note;    // What the recording will not see, when that is known up front.
    template<class V> static void Describe(V& v) {
        v("function", &CallsStart::function);
        v("slot", &CallsStart::slot);
        v("exec", &CallsStart::exec);
        v("thunk", &CallsStart::thunk);
        v("block_size", &CallsStart::blockSize);
        v("object", &CallsStart::object);
        v("seconds", &CallsStart::seconds);
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
    double ms = 0;
    uint32_t thread = 0;
    RefId object;
    std::optional<std::string> objectName, objectClass;
    std::string via;
    std::optional<std::vector<CallsValue>> values;
    template<class V> static void Describe(V& v) {
        v("sequence", &CallsRow::sequence);
        v("ms", &CallsRow::ms);
        v("thread", &CallsRow::thread);
        v("object", &CallsRow::object);
        v("object_name", &CallsRow::objectName);
        v("object_class", &CallsRow::objectClass);
        v("via", &CallsRow::via, "event|vm");
        v("values", &CallsRow::values);
    }
};
// calls-read and calls-stop: the recording's state and its rows.
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
// layout-export: the reflected fields each named class or struct declares,
// one JSON line each in the file; names that are no class or struct.
struct LayoutExport {
    std::string path;
    uint64_t classes = 0, structs = 0, enums = 0, fields = 0;
    std::optional<std::vector<std::string>> missing;
    std::optional<bool> capped;     // `closure` stopped at the type cap.
    template<class V> static void Describe(V& v) {
        v("path", &LayoutExport::path);
        v("classes", &LayoutExport::classes);
        v("structs", &LayoutExport::structs);
        v("enums", &LayoutExport::enums);
        v("fields", &LayoutExport::fields);
        v("missing", &LayoutExport::missing);
        v("capped", &LayoutExport::capped);
    }
};
// layout-check: only the lines the live layout does not hold.
struct LayoutField {
    HexNum offset;
    uint32_t size = 0;
    std::string type;
    template<class V> static void Describe(V& v) {
        v("offset", &LayoutField::offset);
        v("size", &LayoutField::size);
        v("type", &LayoutField::type);
    }
};
struct LayoutMismatch {
    std::string owner, field, problem;
    LayoutField expected;
    std::optional<LayoutField> found;
    template<class V> static void Describe(V& v) {
        v("owner", &LayoutMismatch::owner);
        v("field", &LayoutMismatch::field);
        v("problem", &LayoutMismatch::problem, "no class|no field|offset|size|type");
        v("expected", &LayoutMismatch::expected);
        v("found", &LayoutMismatch::found);
    }
};
struct LayoutCheck {
    std::string path;
    uint64_t classes = 0, checked = 0, mismatched = 0;
    bool ok = false;
    std::vector<LayoutMismatch> rows;
    bool capped = false;
    template<class V> static void Describe(V& v) {
        v("path", &LayoutCheck::path);
        v("classes", &LayoutCheck::classes);
        v("checked", &LayoutCheck::checked);
        v("mismatched", &LayoutCheck::mismatched);
        v("ok", &LayoutCheck::ok);
        v("rows", &LayoutCheck::rows);
        v("capped", &LayoutCheck::capped);
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
// assets-export: one file per object; the index lists them all, rows the first.
struct ExportedAsset {
    static constexpr std::string_view TypeName = "ExportedAsset";
    std::string name, className, file;
    RefId ref;
    std::optional<uint64_t> tableRows;
    std::optional<std::string> error;
    template<class V> static void Describe(V& v) {
        v("name", &ExportedAsset::name);
        v("class", &ExportedAsset::className);
        v("ref", &ExportedAsset::ref);
        v("file", &ExportedAsset::file);
        v("table_rows", &ExportedAsset::tableRows);
        v("error", &ExportedAsset::error);
    }
};
struct ExportAssets {
    std::string dir, index;
    uint64_t objects = 0, tables = 0, failed = 0;
    bool capped = false;
    std::vector<ExportedAsset> rows;
    std::optional<uint64_t> omitted;
    template<class V> static void Describe(V& v) {
        v("dir", &ExportAssets::dir);
        v("index", &ExportAssets::index);
        v("exported", &ExportAssets::objects);
        v("tables", &ExportAssets::tables);
        v("failed", &ExportAssets::failed);
        v("capped", &ExportAssets::capped);
        v("rows", &ExportAssets::rows);
        v("omitted", &ExportAssets::omitted);
    }
};
}

inline shape::Object MakeObject(const ue::Object& object) {
    shape::Object row{FullRef(object.ref), object.name, object.className, {}, {}};
    if (object.size) row.classSize = object.size;
    return row;
}

// `outers`: the found objects' outers, described (nullopt where gone).
inline shape::Find MakeFind(const ue::FindResult& found, uint32_t from, std::span<const std::optional<ue::Object>> outers = {}) {
    shape::Find out;
    for (const auto& object : found.objects) {
        auto row = MakeObject(object);
        for (const auto& outer : outers)
            if (outer && outer->ref.address == object.outer.address) {
                row.outer = shape::ObjectOuter{outer->name, FullRef(outer->ref)};
                break;
            }
        out.rows.push_back(std::move(row));
    }
    if (!from) out.total = found.total;
    out.from = from;
    if (found.next) out.nextCursor = found.next;
    return out;
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
    if (!field.subtype.empty()) info.subtype = field.subtype;
    if (!field.valueSubtype.empty()) info.valueSubtype = field.valueSubtype;
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

// get with `mode` `types`: a row from a field's layout; `readable` is the recovered
// name, shown as name with the reflected one as raw.
inline shape::GetRow MakeTypeRow(const shape::FieldInfo& info, std::string_view readable) {
    shape::GetRow row;
    row.name = readable.empty() ? info.name : std::string(readable);
    if (!readable.empty() && readable != info.name) row.raw = info.name;
    row.owner = info.owner;
    row.type = info.type;
    row.offset = info.offset;
    row.size = info.size;
    row.structName = info.structName;
    row.boolMask = info.boolMask;
    row.elementType = info.elementType;
    row.elementSize = info.elementSize;
    row.valueType = info.valueType;
    row.valueSize = info.valueSize;
    row.valueOffset = info.valueOffset;
    row.valueStruct = info.valueStruct;
    row.stride = info.stride;
    row.subtype = info.subtype;
    row.valueSubtype = info.valueSubtype;
    return row;
}

inline shape::GetRow MakeNativeTypeRow(const NativeMember& member, bool stale) {
    auto row = MakeTypeRow(MakeFieldInfo(member), {});
    row.native = true;
    if (stale) row.stale = true;
    return row;
}

// get with `mode` `types`: `label(field)` is the readable name shown beside an obfuscated one.
template<class LabelOf>
shape::Get MakeTypes(const Ref& ref, std::span<const ue::Field> sorted, const LabelOf& label) {
    shape::Get out;
    out.ref = ReusableRef(ref);
    for (const auto& field : sorted) out.rows.push_back(MakeTypeRow(MakeFieldInfo(field), label(field)));
    out.capped = sorted.size() >= ue::MaxFieldStart;
    return out;
}

// Field rows (reflected and native) in offset order.
inline void SortRowsByOffset(std::vector<shape::GetRow>& rows) {
    const auto offset = [](const shape::GetRow& row) { return row.offset ? row.offset->value : 0; };
    std::stable_sort(rows.begin(), rows.end(), [&](const auto& a, const auto& b) { return offset(a) < offset(b); });
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
