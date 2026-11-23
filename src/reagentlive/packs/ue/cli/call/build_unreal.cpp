// reagentlive CLI: building the Unreal pack verbs (find, call, get, each, ...).
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include "reagentlive/frontends/cli/call/build_internal.h"
#include "reagentlive/frontends/cli/call/layout.h"
#include "reagentlive/packs/ue/protocol_ue.h"

namespace reagentlive::cli::build {
using namespace text;
static_assert(MaxTypeName == ue::MaxClassName);
namespace {
// start: a cursor from a previous page's next_cursor; 0 when absent.
bool Cursor(const Args& args, uint64_t most, uint64_t& from) {
    from = args.Int("start");
    return from <= most;
}

// cls: one class into the request's data, as the agent takes it.
bool ClassFilter(const Args& args, Request& request) {
    if (!args.Has("cls")) return true;
    const auto value = args.Str("cls");
    if (!ClassName(value)) return false;
    memcpy(request.data, value.data(), value.size());
    request.data[value.size()] = 0;
    request.length = static_cast<uint32_t>(value.size() + 1);
    return true;
}

// A call's object: a ref or an address, or a default object by name
// (Default__Class), which the console finds before the call.
bool CallObject(std::string_view text, Command& command) {
    if (Object(text, command.request)) return true;
    if (!text.starts_with("Default__") || !ClassName(text)) return false;
    command.className = text;
    return true;
}
constexpr auto ObjectWhy = "object is a literal ref, \"0x...\" or a Default__ name";
constexpr auto ObjectOrClassWhy = "object is a literal ref, \"0x...\" or a class name";

// A function name for the request's text.
std::string FunctionName(const Args& args, Request& request) {
    const auto name = args.Str("function");
    if (name.empty() || name.size() >= MaxText) return "function is 1.." + std::to_string(MaxText - 1) + " characters";
    CopyText(request.text, name);
    return {};
}

// object, function and an optional hex block: call's raw form and release.
Parsed CallBlock(const Args& args, Command command) {
    Request& request = command.request;
    if (!CallObject(args.Str("object"), command)) return Fail(ObjectWhy);
    if (auto why = FunctionName(args, request); !why.empty()) return Fail(why);
    if (!Bytes(args.Str("block"), request.data, ue::MaxParams, request.length))
        return Fail("block must be hex pairs, at most " + std::to_string(ue::MaxParams));
    request.op = ue::op::Call;
    return command;
}

Parsed FunctionTarget(const Args& args, uint32_t op, Kind kind) {
    Command command;
    Request& request = command.request;
    if (!ObjectOrClass(args.Str("object"), command)) return Fail(ObjectOrClassWhy);
    if (auto why = FunctionName(args, request); !why.empty()) return Fail(why);
    request.op = ue::op::Function;
    Request probe = request;
    if (!probe.address) probe.address = 1; // A class name gets its ref later.
    if (ue::Check(probe) != Status::Ok) return Fail("function: invalid name");
    request.op = op;
    command.kind = kind;
    return command;
}

// outer: an object when it is a ref or "0x...", else an outer's name text.
std::string Outer(const Args& args, Command& command) {
    if (!args.Has("outer")) return {};
    command.outer = args.Str("outer");
    Request object{};
    if (Object(command.outer, object) && ue::GetRef(object).address) {
        command.outerRef = ue::GetRef(object); // Slot and serial too: the agent refuses a stale one.
        return {};
    }
    if (command.outer.empty() || command.outer.size() >= ue::MaxOuterText)
        return "outer is an object, or 1.." + std::to_string(ue::MaxOuterText - 1) + " characters of an outer's name";
    command.outerText = command.outer;
    return {};
}

// A list of paths into the command's fields; why not, or empty.
std::string Paths(const Args& args, std::string_view key, std::vector<std::string>& out, std::string_view what = "paths") {
    for (const auto& item : args.At(key)) {
        std::string path;
        if (out.size() == MaxGetPaths) return std::string(key) + ": at most " + std::to_string(MaxGetPaths) + " " + std::string(what);
        if (auto why = value::Path(item, path, MaxGetDepth); !why.empty()) return std::string(key) + ": " + why;
        out.push_back(std::move(path));
    }
    return {};
}

// fields, start and limit (call's returned arrays, rows); why not, or empty.
std::string RowOptions(const Args& args, Command& command) {
    if (auto why = Paths(args, "fields", command.rowPaths); !why.empty()) return why;
    command.nonzero = args.Has("nonzero");
    const uint64_t limit = args.Int("limit", command.limit);
    if (limit > UINT32_MAX) return "limit is a number (0: all)";
    command.limit = static_cast<uint32_t>(limit);
    uint64_t from = 0;
    if (!Cursor(args, UINT32_MAX, from)) return "start is a row number";
    command.from = static_cast<uint32_t>(from);
    return {};
}

// elements: shown per container in get and each; 0 when not given.
std::string Elements(const Args& args, Command& command) {
    if (!args.Has("elements")) return {};
    const uint64_t number = args.Int("elements");
    if (!number || number > 256) return "elements is 1..256";
    command.elements = static_cast<uint32_t>(number);
    return {};
}

// A JSON input as the typed call's text: a struct as "(a, b)" or "(Name=a)",
// an object as its ref, a name as its text, null as None.
std::string InputText(const Json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_null()) return "None";
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_number()) return value.dump();
    std::string text = "(";
    if (value.is_array()) {
        for (const auto& item : value) text += (text.size() > 1 ? ", " : "") + InputText(item);
    } else {
        for (const auto& [key, item] : value.items()) text += (text.size() > 1 ? ", " : "") + key + "=" + InputText(item);
    }
    return text + ")";
}

// call and call-pure: an object and a function; params (an object) makes a
// typed call, block a raw one. Neither: call sends a zeroed block, call-pure
// makes a typed call (decoded outputs; a function with inputs is refused, as
// a zeroed block is).
Parsed CallArgs(const Args& args, uint32_t flags) {
    Command command;
    command.request.flags = flags;
    command.dry = args.Has("dry");
    const bool typed = args.Has("params") || ((flags & ue::CallPure) && !args.Has("block"));
    // fields, start, limit and nonzero (call's view; call-pure has none) pick from a typed call's outputs.
    const bool rows = !(flags & ue::CallPure) &&
        (args.Has("fields") || args.Has("start") || args.Has("limit") || args.Has("nonzero"));
    command.limit = DefaultCallRows;
    if (args.Has("params") && args.Has("block")) return Fail("params: params makes a typed call, block a raw one; give one");
    if (rows && args.Has("block")) return Fail("block: fields, start, limit and nonzero need a typed call (params), not a raw block");
    if (rows)
        if (auto why = RowOptions(args, command); !why.empty()) return Fail(why);
    if (!typed && !rows) return CallBlock(args, std::move(command));
    if (!CallObject(args.Str("object"), command)) return Fail(ObjectWhy);
    if (auto why = FunctionName(args, command.request); !why.empty()) return Fail(why);
    command.kind = Kind::TypedCall;
    if (args.Has("params"))
        for (const auto& [name, value] : args.At("params").items()) command.fields.push_back(name + "=" + InputText(value));
    command.request.op = ue::op::Call;
    return command;
}

Parsed CensusOptions(const Args& args) {
    Command command;
    Request& request = command.request;
    command.limit = DefaultCensusLimit;
    command.changed = args.Has("diff");
    command.match = args.Str("match");
    if (!ClassFilter(args, request)) return Fail("cls is one class name");
    if (auto why = Outer(args, command); !why.empty()) return Fail(std::move(why));
    const uint64_t limit = args.Int("limit", DefaultCensusLimit);
    if (limit > UINT32_MAX) return Fail("limit is a number (0: all)");
    command.limit = static_cast<uint32_t>(limit);
    uint64_t from = 0;
    if (!Cursor(args, UINT32_MAX, from)) return Fail("start is a row number");
    command.from = static_cast<uint32_t>(from);
    request.op = ue::op::Census;
    return command;
}

// each and call-each: the class, the columns and the options both take.
Parsed EachArgs(const Args& args, std::string_view classKey, std::vector<std::string> columns, bool call) {
    Command command;
    if (auto why = Outer(args, command); !why.empty()) return Fail(std::move(why));
    const auto className = args.Str(classKey);
    if (!ClassName(className)) return Fail(std::string(classKey) + " is a class name");
    if (args.Has("seconds")) {
        const uint64_t seconds = args.Int("seconds");
        if (!seconds || seconds > MaxEachSeconds) return Fail("seconds is 1.." + std::to_string(MaxEachSeconds));
        command.seconds = static_cast<uint32_t>(seconds);
    }
    command.fields = std::move(columns);
    // where and elements are each's alone: call-each's one column is the function's.
    if (!call && args.Has("where")) {
        const Json& where = args.At("where");
        if (auto why = value::Path(where["field"], command.whereField, MaxGetDepth); !why.empty()) return Fail("where: " + why);
        command.match = where["contains"].get<std::string>();
        if (command.match.empty()) return Fail("where: contains is the text to find");
        if (command.seconds) return Fail("where and seconds do not mix");
    }
    const uint64_t limit = args.Int("limit", DefaultEachRows);
    if (limit > UINT32_MAX) return Fail("limit is a number (0: all)");
    command.limit = static_cast<uint32_t>(limit);
    command.nonzero = args.Has("nonzero");
    if (!call)
        if (auto why = Elements(args, command); !why.empty()) return Fail(std::move(why));
    command.kind = Kind::Each;
    command.className = className;
    command.request.op = ue::op::Each;
    return command;
}
}

// find: objects by name text and/or class.
Parsed Find(const Args& args, const ParseContext&) {
    Command command;
    Request& request = command.request;
    const std::string text = args.Str("text");
    if (!ClassFilter(args, request)) return Fail("cls is one class name, under 64 characters");
    if (auto why = Outer(args, command); !why.empty()) return Fail(std::move(why));
    uint64_t from = 0;
    if (!Cursor(args, INT32_MAX, from)) return Fail("start is a slot number");
    request.args[1] = from;
    if (args.Has("limit")) {
        const uint64_t limit = args.Int("limit");
        if (!limit || limit > MaxFindRows) return Fail("limit must be 1.." + std::to_string(MaxFindRows));
        command.limit = static_cast<uint32_t>(limit);
    }
    if (text.empty() && !request.length && command.outer.empty()) return Fail("text: give text, cls or outer");
    if (text.size() >= MaxText) return Fail("text is too long");
    request.op = ue::op::Find;
    if (args.Has("live")) request.flags = ue::FindLive;
    CopyText(request.text, text);
    return command;
}

Parsed Call(const Args& args, const ParseContext&) { return CallArgs(args, 0); }

// The agent refuses, before calling, a function that is not const or pure
// or whose outputs own memory (ue::CallPure). On every instance: call-each (EachCall).
Parsed CallPure(const Args& args, const ParseContext&) { return CallArgs(args, ue::CallPure); }

// Drops a kept call result: one more call with zeroed inputs (or the given
// block), so the engine releases what the kept block held.
Parsed Release(const Args& args, const ParseContext&) {
    Command command;
    command.kind = Kind::Release;
    command.request.flags = ue::CallRelease;
    return CallBlock(args, std::move(command));
}

Parsed Funcs(const Args& args, const ParseContext&) {
    Command command;
    Request& request = command.request;
    if (!ObjectOrClass(args.Str("object"), command)) return Fail(ObjectOrClassWhy);
    uint64_t cursor = 0;
    if (!Cursor(args, ue::MaxFieldStart, cursor)) return Fail("start is a cursor");
    request.length = static_cast<uint32_t>(cursor);
    if (args.Str("match").size() >= MaxText) return Fail("match is too long");
    request.op = ue::op::Functions;
    CopyText(request.text, args.Str("match"));
    return command;
}

Parsed Func(const Args& args, const ParseContext&) { return FunctionTarget(args, ue::op::Function, Kind::Remote); }

// script: a Blueprint function's bytecode; decode reads it as statements, entry one uber-graph event.
Parsed Script(const Args& args, const ParseContext&) {
    auto command = FunctionTarget(args, ue::op::Function, Kind::Script);
    if (!command) return command;
    command->decode = args.Has("decode");
    if (args.Has("entry")) {
        const uint64_t entry = args.Int("entry");
        if (!command->decode) return Fail("entry needs decode");
        if (entry >= ue::MaxScriptBytes) return Fail("entry is a bytecode offset");
        command->entry = static_cast<uint32_t>(entry);
    }
    if (args.Has("to")) {
        const uint64_t to = args.Int("to");
        if (!command->decode) return Fail("to needs decode");
        if (!to || to > ue::MaxScriptBytes || (command->entry && to <= *command->entry))
            return Fail("to is a bytecode offset past entry");
        command->to = static_cast<uint32_t>(to);
    }
    command->match = args.Str("match");
    if (!command->match.empty() && !command->decode) return Fail("view.match needs decode: it keeps statements");
    return command;
}

// enum: an enum's names and values.
Parsed Enum(const Args& args, const ParseContext&) {
    Command command;
    if (!ObjectOrClass(args.Str("enum"), command)) return Fail("enum is a literal ref, \"0x...\" or an enum name");
    command.kind = Kind::Enum;
    command.request.op = ue::op::Enum;
    return command;
}

// game-thread: the host's Draw tick (status reads its state).
Parsed GameThread(const Args& args, const ParseContext&) {
    Command command;
    command.kind = Kind::GameThread;
    command.request.op = ue::op::GameThread;
    command.request.flags = static_cast<uint32_t>(args.Str("action") == "on" ? ue::GameThreadAction::On : ue::GameThreadAction::Off);
    return command;
}

Parsed Name(const Args& args, const ParseContext&) {
    Command command;
    Request& request = command.request;
    uint32_t count = 0;
    for (const auto& item : args.At("indices")) {
        const uint64_t index = Args::Number(item, UINT64_MAX);
        if (count == MaxRead / 4 || index > INT32_MAX)
            return Fail("indices: up to " + std::to_string(MaxRead / 4) + " FName indices; the console pages them");
        const auto number = static_cast<int32_t>(index);
        memcpy(request.data + count++ * 4, &number, 4);
    }
    request.op = ue::op::Names;
    request.length = count * 4;
    return command;
}

// describe: an object, or an object-table slot and serial (a weak pointer).
Parsed Describe(const Args& args, const ParseContext&) {
    Command command;
    Request& request = command.request;
    request.op = ue::op::Describe;
    const bool slot = args.Has("index") || args.Has("serial");
    if (slot == args.Has("object")) return Fail("object: give object, or index and serial");
    if (slot) {
        const uint64_t index = args.Int("index", UINT64_MAX), serial = args.Int("serial");
        if (index > INT32_MAX) return Fail("index is an object-table slot");
        if (!serial || serial > INT32_MAX) return Fail("serial is the slot's serial number, above 0");
        ue::SetRef(request, Ref{0, static_cast<int32_t>(index), static_cast<int32_t>(serial)});
        return command;
    }
    if (!Object(args.Str("object"), request)) return Fail("object is a literal ref or \"0x...\"");
    return command;
}

// label: names an unreflected member in the native file.
Parsed Label(const Args& args, const ParseContext&) {
    Command command;
    const uint64_t offset = args.Int("offset");
    if (!ObjectOrClass(args.Str("object"), command)) return Fail(ObjectOrClassWhy);
    if (offset >= ue::MaxObjectBytes) return Fail("offset is inside the object");
    command.target = args.Str("type");
    command.match = args.Str("name");
    if (auto field = layout::ParseField(command.target + " " + command.match, true); !field || field->type.empty())
        return Fail(field ? std::string("type is an element type, with [n] for an array") : "type: " + field.error());
    command.offset = offset;
    command.kind = Kind::Label;
    command.request.op = ue::op::Fields;
    return command;
}

// export-labels: names for a static-analysis tool.
Parsed ExportLabels(const Args& args, const ParseContext&) {
    Command command;
    command.file = args.Str("file");
    command.limit = label_kind::All;
    command.target = args.Str("module");
    if (args.Has("kinds")) {
        std::string list;
        for (const auto& kind : args.Strs("kinds")) list += (list.empty() ? "" : ",") + kind;
        command.limit = LabelKinds(list);
    }
    command.kind = Kind::ExportLabels;
    command.request.op = ue::op::ExecEntries;
    return command;
}

// export-assets: each object's fields and table rows, one file per object.
Parsed ExportAssets(const Args& args, const ParseContext&) {
    Command command;
    command.kind = Kind::ExportAssets;
    command.file = args.Str("dir");
    for (const auto& name : args.Strs("cls")) {
        if (!ClassName(name)) return Fail("cls: '" + name + "' is not a class name");
        command.fields.push_back(name);
    }
    if (command.fields.size() > MaxExportClasses) return Fail("cls: at most " + std::to_string(MaxExportClasses) + " classes per call");
    command.match = args.Str("match");
    return command;
}

// layout-export: the fields each class declares, as JSON lines.
Parsed LayoutExport(const Args& args, const ParseContext&) {
    Command command;
    command.kind = Kind::Layout;
    command.target = "export";
    command.file = args.Str("file");
    for (const auto& name : args.Strs("classes")) {
        if (!ClassName(name)) return Fail("classes: '" + name + "' is not a class, struct or enum name");
        command.fields.push_back(name);
    }
    if (command.fields.size() > MaxLayoutClasses) return Fail("classes: at most " + std::to_string(MaxLayoutClasses) + " per call");
    if (args.Has("closure")) {
        const uint64_t depth = args.Int("closure");
        if (!depth || depth > MaxClosureDepth) return Fail("closure is a depth, 1.." + std::to_string(MaxClosureDepth));
        command.closure = static_cast<uint32_t>(depth);
    }
    command.request.op = ue::op::Fields;
    return command;
}

// layout-check: the file's lines against the live layout.
Parsed LayoutCheck(const Args& args, const ParseContext&) {
    Command command;
    command.kind = Kind::Layout;
    command.target = "check";
    command.file = args.Str("file");
    command.request.op = ue::op::Fields;
    return command;
}

// entries: one page of an array, set or map.
Parsed Entries(const Args& args, const ParseContext&) {
    Command command;
    if (!CallObject(args.Str("object"), command)) return Fail(ObjectWhy);
    uint64_t from = 0;
    if (!Cursor(args, INT32_MAX, from)) return Fail("start is a cursor");
    command.kind = Kind::Entries;
    command.target = args.Str("field");
    if (command.target.empty() || command.target.size() >= MaxText) return Fail("field is one field name ([Owner:]Name)");
    command.limit = static_cast<uint32_t>(from);
    command.request.op = ue::op::Entries;
    return command;
}

// rows: a data table's rows.
Parsed Rows(const Args& args, const ParseContext&) {
    Command command;
    if (!CallObject(args.Str("object"), command)) return Fail(ObjectWhy);
    command.limit = DefaultCallRows;
    if (auto why = RowOptions(args, command); !why.empty()) return Fail(why);
    command.match = args.Str("match");
    if (args.Has("at")) {
        const double at = args.At("at").get<double>();
        if (!std::isfinite(static_cast<float>(at))) return Fail("at is a curve's time, a finite number");
        if (!command.rowPaths.empty() || command.nonzero) return Fail("at reads curve tables: no fields or nonzero");
        command.curveAt = static_cast<float>(at);
    }
    command.kind = Kind::Rows;
    command.request.op = ue::op::ReadObject;
    return command;
}

// get: one reader of reflected fields. Its modes run as the internal kinds:
// paths (Get), every field (Dump), the layout (op::Fields) and the baseline (Snap).
Parsed Get(const Args& args, const ParseContext&) {
    Command command;
    if (!ObjectOrClass(args.Str("object"), command))
        return Fail("object is a literal ref, \"0x...\", a Default__ name, or with types a class name");
    const std::string mode = args.Has("mode") ? args.Str("mode") : std::string("values");
    const bool types = mode == "types", snap = mode == "snap", changed = mode == "changed";
    const bool named = !command.className.empty() && !command.className.starts_with("Default__");
    if (named && !types)
        return Fail("object: a class or struct by name has no values: mode types gives its layout, or give an object "
                    "(Default__" + command.className + " for its defaults)");
    if (auto why = Paths(args, "paths", command.fields); !why.empty()) return Fail(why);
    const bool paths = !command.fields.empty();
    if (paths && (args.Has("match") || args.Has("own") || snap || changed || types))
        return Fail("paths: match, own and the modes types, snap and changed take no paths");
    if (types && (args.Has("nonzero") || args.Has("elements")))
        return Fail("mode: types reads no values: no nonzero or elements");
    if (args.Has("file") && !snap && !changed) return Fail("file goes with mode snap or changed");
    if ((snap || changed) && command.className.empty() && ue::GetRef(command.request).index < 0)
        return Fail("object: snap and changed need a literal ref (0x<address>#<index>:<serial>)");
    if (snap && (args.Has("match") || args.Has("nonzero") || args.Has("own") || args.Has("elements")))
        return Fail("mode: snap keeps the whole object: no filters");
    command.nonzero = args.Has("nonzero");
    if (auto why = Elements(args, command); !why.empty()) return Fail(std::move(why));
    command.own = args.Has("own");
    command.match = args.Str("match");
    command.request.op = ue::op::Fields;
    if (paths) {
        command.kind = Kind::Get;
    } else if (types) {
        command.kind = Kind::Remote;
    } else if (snap) {
        command.kind = Kind::Snap;
        command.file = args.Str("file");
    } else {
        command.kind = Kind::Dump;
        command.changed = changed;
        command.file = args.Str("file");
    }
    return command;
}

namespace {
// One end of a trace: [x, y, z] (cm), an object, or {object, socket}.
bool TraceEnd(const Json& end, Command::TracePoint& out) {
    out = {};
    if (end.is_array()) {
        for (int axis = 0; axis < 3; ++axis) {
            out.point[axis] = end[axis].get<double>();
            if (!std::isfinite(out.point[axis])) return false;
        }
        out.literal = true;
        return true;
    }
    if (end.is_object()) {
        out.socket = end["socket"].get<std::string>();
        if (out.socket.empty() || out.socket.size() >= MaxText) return false;
        return LiteralRef(end["object"].get<std::string>(), out.object);
    }
    return LiteralRef(end.get<std::string>(), out.object);
}
}

// trace: the first thing a line hits.
Parsed Trace(const Args& args, const ParseContext&) {
    Command command;
    for (int i = 0; i < 2; ++i) {
        const char* key = i ? "end" : "start";
        if (!TraceEnd(args.At(key), command.traceEnds[i]))
            return Fail(std::string(key) + " is [x, y, z] in cm, an object's literal ref, or {\"object\": <ref>, \"socket\": \"Name\"}");
    }
    const uint64_t channel = args.Int("channel");
    if (channel >= 32) return Fail("channel is a trace type query, 0..31 (0 visibility, 1 camera)");
    command.traceChannel = static_cast<uint8_t>(channel);
    command.traceSimple = args.Has("simple");
    if (args.Has("ignore") && !LiteralRef(args.Str("ignore"), command.traceIgnore)) return Fail("ignore is an actor's literal ref");
    command.kind = Kind::Trace;
    return command;
}

namespace {
// slot (default: the last started recording).
bool CallsSlot(const Args& args, Command& command) {
    command.callsSlot = args.Int("slot", ue::CallsLatest);
    return !args.Has("slot") || command.callsSlot < ue::MaxCallsSlots;
}
}

// calls-start: record a function's calls for a while.
Parsed CallsStart(const Args& args, const ParseContext&) {
    Command command;
    Request& request = command.request;
    if (!ObjectOrClass(args.Str("owner"), command)) return Fail("owner is a literal ref, \"0x...\" or a class name");
    if (auto why = FunctionName(args, request); !why.empty()) return Fail(why);
    const uint64_t milliseconds = args.Milliseconds("duration");
    if (milliseconds > ue::MaxCallsMilliseconds)
        return Fail("duration is at most " + std::to_string(ue::MaxCallsMilliseconds / 1000) + " s");
    request.args[1] = milliseconds;
    if (args.Has("object")) {
        Ref filter{};
        if (!LiteralRef(args.Str("object"), filter)) return Fail("object is a literal ref");
        const uint64_t address = filter.address;
        memcpy(request.data, &address, sizeof(address));
        request.length = sizeof(address);
    }
    request.flags = args.Has("dry") ? ue::CallsDry : 0;
    request.op = ue::op::CallsStart;
    command.kind = Kind::Calls;
    command.target = "start";
    return command;
}

Parsed CallsRead(const Args& args, const ParseContext&) {
    Command command;
    if (!CallsSlot(args, command)) return Fail("slot is 0.." + std::to_string(ue::MaxCallsSlots - 1));
    command.callsAfter = args.Int("start");
    command.request.op = ue::op::CallsRead;
    command.kind = Kind::Calls;
    command.target = "read";
    return command;
}

Parsed CallsStop(const Args& args, const ParseContext&) {
    Command command;
    if (!CallsSlot(args, command)) return Fail("slot is 0.." + std::to_string(ue::MaxCallsSlots - 1));
    command.request.op = ue::op::CallsStop;
    command.kind = Kind::Calls;
    command.target = "stop";
    return command;
}

// census: diff answers only the classes whose count differs from the last census of that cls.
Parsed Census(const Args& args, const ParseContext&) { return CensusOptions(args); }

// refs: functions whose bytecode names a pooled name (by text or index); the console resolves it.
Parsed Refs(const Args& args, const ParseContext&) {
    Command command;
    Request& request = command.request;
    command.kind = Kind::Refs;
    request.op = ue::op::Refs;
    if (args.Has("name") == args.Has("index")) return Fail("name: give name or index");
    if (args.Has("index")) {
        const uint64_t index = args.Int("index");
        if (!index || index > INT32_MAX) return Fail("index is an FName comparison index");
        request.args[0] = index;
        return command;
    }
    const auto name = args.Str("name");
    if (name.empty() || name.size() >= MaxText) return Fail("name is 1.." + std::to_string(MaxText - 1) + " characters");
    CopyText(request.text, name);
    return command;
}

Parsed NameFind(const Args& args, const ParseContext&) {
    Command command;
    Request& request = command.request;
    uint64_t from = 0;
    if (!Cursor(args, INT32_MAX, from)) return Fail("start is a cursor");
    request.args[1] = from;
    if (args.Str("text").size() >= MaxText) return Fail("text is too long");
    request.op = ue::op::NameFind;
    CopyText(request.text, args.Str("text"));
    return command;
}

// each: reads only; a function is call-each's, and Owner:Function is refused here already.
Parsed Each(const Args& args, const ParseContext&) {
    std::vector<std::string> columns;
    if (auto why = Paths(args, "paths", columns, "columns"); !why.empty()) return Fail(why);
    for (const auto& path : columns)
        if (path.find(':') != std::string::npos)
            return Fail("paths: " + path + " is a function: call-each calls it on every instance");
    return EachArgs(args, "cls", std::move(columns), false);
}

// call-each: the one column a function with no inputs.
Parsed EachCall(const Args& args, const ParseContext&) {
    std::string function = args.Str("function");
    if (function.empty() || function.size() >= MaxText) return Fail("function is 1.." + std::to_string(MaxText - 1) + " characters");
    auto command = EachArgs(args, "cls", {function}, true);
    if (command) command->eachCall = true;
    return command;
}
}
