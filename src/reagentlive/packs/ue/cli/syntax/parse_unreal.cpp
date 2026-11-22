// reagentlive CLI: parsing the Unreal pack verbs (find, call, dump, get, each, ...).
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <string>
#include "reagentlive/frontends/cli/syntax/layout.h"
#include "reagentlive/frontends/cli/syntax/parse_internal.h"
#include "reagentlive/packs/ue/protocol_ue.h"

namespace reagentlive::cli::parse {
using namespace text;
static_assert(MaxTypeName == ue::MaxClassName);
namespace {
// --start N: a cursor from a previous page's next_cursor; 0 when absent.
bool Cursor(const Args& args, uint64_t most, uint64_t& from) {
    from = 0;
    return !args.Has("--start") || (Number(args.Value("--start"), from) && from <= most);
}

// --cls Class into the request's data, as the agent takes it.
bool ClassFilter(const Args& args, Request& request) {
    if (!args.Has("--cls")) return true;
    const auto value = args.Value("--cls");
    if (value.empty() || value.size() >= ue::MaxClassName) return false;
    memcpy(request.data, value.data(), value.size());
    request.data[value.size()] = 0;
    request.length = static_cast<uint32_t>(value.size() + 1);
    return true;
}

// object, function and an optional hex block: call's raw form and release.
Parsed CallBlock(const Args& args, const ParseContext& context, size_t firstByte, Command command) {
    Request& request = command.request;
    if (!Object(args.At(0), context.refs, request)) return Fail("object must be $N or an address");
    if (args.At(1).size() >= MaxText) return Fail("function name too long");
    if (!Bytes(args.From(firstByte), request.data, ue::MaxParams, request.length))
        return Fail("params must be hex pairs, at most " + std::to_string(ue::MaxParams));
    request.op = ue::op::Call;
    CopyText(request.text, args.At(1));
    return command;
}

Parsed FunctionTarget(const Args& args, const ParseContext& context, uint32_t op, Kind kind) {
    Command command;
    Request& request = command.request;
    if (!ObjectOrClass(args.At(0), context.refs, command)) return Fail("object must be $N, an address or a class name");
    if (args.At(1).size() >= MaxText) return Fail("function name too long");
    request.op = ue::op::Function;
    CopyText(request.text, args.At(1));
    Request probe = request;
    if (!probe.address) probe.address = 1; // A class name gets its ref later.
    if (ue::Check(probe) != Status::Ok) return Fail("invalid function name");
    request.op = op;
    command.kind = kind;
    return command;
}

// --outer: an object when it parses as one (a literal ref, an address, $N), else an outer's name text.
std::string Outer(const Args& args, std::span<const Ref> refs, Command& command) {
    if (!args.Has("--outer")) return {};
    command.outer = args.Value("--outer");
    Request object{};
    if (Object(command.outer, refs, object) && ue::GetRef(object).address) {
        command.outerRef = ue::GetRef(object); // Slot and serial too: the agent refuses a stale one.
        return {};
    }
    if (command.outer.empty() || command.outer.size() >= ue::MaxOuterText)
        return "--outer is an object, or 1.." + std::to_string(ue::MaxOuterText - 1) + " characters of an outer's name";
    command.outerText = command.outer;
    return {};
}

Parsed CensusOptions(const Args& args, std::span<const Ref> refs, bool changed) {
    Command command;
    Request& request = command.request;
    command.limit = DefaultCensusLimit;
    command.changed = changed;
    command.match = args.Value("--match");
    if (args.Has("--cls") && (!ClassName(args.Value("--cls")) || !ClassFilter(args, request)))
        return Fail("--cls is one class name");
    if (auto why = Outer(args, refs, command); !why.empty()) return Fail(std::move(why));
    uint64_t number = 0;
    if (args.Has("--limit")) {
        if (!Number(args.Value("--limit"), number) || number > UINT32_MAX) return Fail("--limit is a number (0: all)");
        command.limit = static_cast<uint32_t>(number);
    }
    if (!Cursor(args, UINT32_MAX, number)) return Fail("--start is a row number");
    command.from = static_cast<uint32_t>(number);
    request.op = ue::op::Census;
    return command;
}
}

// find [text] [--cls Class] [--live] [--start cursor]; text keeps its inner spaces.
Parsed Find(const Args& args, const ParseContext& context) {
    Command command;
    Request& request = command.request;
    const std::string text = args.From(0);
    if (!ClassFilter(args, request)) return Fail("one class name, under 64 characters");
    if (auto why = Outer(args, context.refs, command); !why.empty()) return Fail(std::move(why));
    uint64_t from = 0;
    if (!Cursor(args, INT32_MAX, from)) return Fail("cursor must be a slot number");
    request.args[1] = from;
    if (text.empty() && !request.length && command.outer.empty()) return Fail();
    if (text.size() >= MaxText) return Fail("search text too long");
    request.op = ue::op::Find;
    if (args.Has("--live")) request.flags = ue::FindLive;
    CopyText(request.text, text);
    return command;
}

namespace {
// --fields a,b.c, --start N and --limit N (call's returned arrays, rows); why not, or empty.
std::string RowOptions(const Args& args, Command& command) {
    if (args.Has("--fields"))
        for (std::string_view list = args.Value("--fields");;) {
            const auto comma = list.find(',');
            const auto path = list.substr(0, comma);
            if (path.empty() || path.front() == '.' || path.back() == '.' || path.find("..") != std::string_view::npos)
                return "--fields is a comma list of member paths: a,b.c";
            command.rowPaths.emplace_back(path);
            if (comma == std::string_view::npos) break;
            list = list.substr(comma + 1);
        }
    command.nonzero = args.Has("--nonzero");
    uint64_t number = 0;
    if (args.Has("--limit")) {
        if (!Number(args.Value("--limit"), number) || number > UINT32_MAX) return "--limit is a number (0: all)";
        command.limit = static_cast<uint32_t>(number);
    }
    if (!Cursor(args, UINT32_MAX, number)) return "--start is a row number";
    command.from = static_cast<uint32_t>(number);
    return {};
}

// --elements N: elements shown per container in get, each and dump; 0 when not given.
std::string Elements(const Args& args, Command& command) {
    if (!args.Has("--elements")) return {};
    uint64_t number = 0;
    if (!Number(args.Value("--elements"), number) || !number || number > 256) return "--elements is 1..256";
    command.elements = static_cast<uint32_t>(number);
    return {};
}

// call and call-pure: <object> <[Class:]Function> [hex params] | Name=value ... | --typed
Parsed CallLine(const Args& args, const ParseContext& context, uint32_t flags) {
    Command command;
    command.request.flags = flags;
    command.dry = args.Has("--dry");
    const bool named = std::ranges::any_of(args.positionals.begin() + 2, args.positionals.end(),
        [](const std::string& token) { return token.find('=') != std::string::npos; });
    // --fields, --start, --limit and --nonzero pick from a typed call's outputs.
    const bool rows = args.Has("--fields") || args.Has("--start") || args.Has("--limit") || args.Has("--nonzero");
    command.limit = DefaultCallRows;
    if (rows) {
        if (!named && args.positionals.size() > 2)
            return Fail("--fields, --start, --limit and --nonzero need a typed call, not a raw block");
        if (auto why = RowOptions(args, command); !why.empty()) return Fail(std::move(why));
    }
    if (!named && !args.Has("--typed") && !rows) return CallBlock(args, context, 2, std::move(command));
    if (args.Has("--typed") && args.positionals.size() > 2) return Fail("--typed is a typed call with no inputs");
    if (!Object(args.At(0), context.refs, command.request)) return Fail("object must be $N or an address");
    if (args.At(1).size() >= MaxText) return Fail("function name too long");
    command.kind = Kind::TypedCall;
    for (size_t i = 2; i < args.positionals.size(); ++i) command.fields.push_back(args.positionals[i]);
    command.request.op = ue::op::Call;
    CopyText(command.request.text, args.At(1));
    return command;
}
}

Parsed Call(const Args& args, const ParseContext& context) { return CallLine(args, context, 0); }

// The agent refuses, before calling, a function that is not const or pure
// or whose outputs own memory (ue::CallPure).
Parsed CallPure(const Args& args, const ParseContext& context) { return CallLine(args, context, ue::CallPure); }

// Drops a kept call result: one more call with zeroed inputs (or the given
// block), so the engine releases what the kept block held.
Parsed Release(const Args& args, const ParseContext& context) {
    Command command;
    command.kind = Kind::Release;
    command.request.flags = ue::CallRelease;
    return CallBlock(args, context, 2, std::move(command));
}

Parsed Funcs(const Args& args, const ParseContext& context) {
    Command command;
    Request& request = command.request;
    if (!ObjectOrClass(args.At(0), context.refs, command)) return Fail("object must be $N, an address or a class name");
    uint64_t cursor = 0;
    if (!Cursor(args, ue::MaxFieldStart, cursor)) return Fail("--start is a cursor");
    request.length = static_cast<uint32_t>(cursor);
    if (args.At(1).size() >= MaxText) return Fail("function filter too long");
    request.op = ue::op::Functions;
    CopyText(request.text, args.At(1));
    return command;
}

Parsed Func(const Args& args, const ParseContext& context) {
    return FunctionTarget(args, context, ue::op::Function, Kind::Remote);
}

// script <object|class> <[Owner:]Function>: a Blueprint function's bytecode.
Parsed Script(const Args& args, const ParseContext& context) {
    return FunctionTarget(args, context, ue::op::Function, Kind::Script);
}

Parsed Params(const Args& args, const ParseContext& context) {
    return FunctionTarget(args, context, ue::op::Parameters, Kind::Remote);
}

Parsed Name(const Args& args, const ParseContext&) {
    Command command;
    Request& request = command.request;
    uint32_t count = 0;
    for (const auto& token : args.positionals) {
        uint64_t index = 0;
        if (count == MaxRead / 4 || !Number(token, index) || index > INT32_MAX)
            return Fail("up to " + std::to_string(MaxRead / 4) + " FName indices; the console pages them");
        const auto value = static_cast<int32_t>(index);
        memcpy(request.data + count++ * 4, &value, 4);
    }
    request.op = ue::op::Names;
    request.length = count * 4;
    return command;
}

Parsed Describe(const Args& args, const ParseContext& context) {
    Command command;
    Request& request = command.request;
    const std::string_view target = args.At(0);
    uint64_t index = 0, serial = 0;
    const size_t colon = target.find(':');
    request.op = ue::op::Describe;
    if (target.starts_with('#') && colon != std::string_view::npos && Number(target.substr(1, colon - 1), index) &&
        Number(target.substr(colon + 1), serial) && index <= INT32_MAX && serial && serial <= INT32_MAX) {
        // Object-table slot and serial (a weak pointer, or an aim capture's target).
        ue::SetRef(request, Ref{0, static_cast<int32_t>(index), static_cast<int32_t>(serial)});
        return command;
    }
    if (!Object(target, context.refs, request)) return Fail();
    return command;
}

Parsed Snap(const Args& args, const ParseContext& context) {
    Command command;
    Ref ref{};
    if (!Reference(args.At(0), context.refs, ref)) return Fail("use an object ref ($N or 0x<address>#<index>:<serial>)");
    ue::SetRef(command.request, ref);
    command.file = args.At(1); // Optional snapshot file.
    command.kind = Kind::Snap;
    return command;
}

Parsed Record(const Args& args, const ParseContext& context) {
    Command command;
    Ref ref{};
    if (!Reference(args.At(0), context.refs, ref)) return Fail("use an object ref ($N or 0x<address>#<index>:<serial>)");
    ue::SetRef(command.request, ref);
    uint64_t seconds = 0;
    if (!Number(args.At(1), seconds) || seconds < 1 || seconds > 10) return Fail("seconds is 1..10");
    command.file = args.At(2);
    for (size_t i = 3; i < args.positionals.size(); ++i) command.fields.push_back(args.positionals[i]);
    if (command.fields.size() > 8) return Fail("record needs a file and 1..8 exact field names");
    command.request.length = static_cast<uint32_t>(seconds);
    command.kind = Kind::Record;
    return command;
}

Parsed Fields(const Args& args, const ParseContext& context) {
    Command command;
    if (!ObjectOrClass(args.At(0), context.refs, command)) return Fail("object must be $N, an address or a class name");
    command.match = args.Value("--match");
    command.request.op = ue::op::Fields;
    return command;
}

// dump <object|class> [--match text] [--nonzero] [--changed [file]]: fields, then reads of the object's bytes.
Parsed Dump(const Args& args, const ParseContext& context) {
    Command command;
    if (!ObjectOrClass(args.At(0), context.refs, command)) return Fail("object must be $N, an address or a class name");
    command.nonzero = args.Has("--nonzero");
    command.changed = args.Has("--changed");
    if (auto why = Elements(args, command); !why.empty()) return Fail(std::move(why));
    command.own = args.Has("--own");
    command.file = args.Value("--changed");
    command.match = args.Value("--match");
    command.request.op = ue::op::Fields;
    command.kind = Kind::Dump;
    return command;
}

// label <object|Class> <offset> <type>[n] <Name>: names an unreflected member in the native file.
Parsed Label(const Args& args, const ParseContext& context) {
    Command command;
    uint64_t offset = 0;
    if (!ObjectOrClass(args.At(0), context.refs, command) || !Number(args.At(1), offset) || offset >= ue::MaxObjectBytes)
        return Fail();
    command.target = args.At(2);
    command.match = args.At(3);
    if (auto field = layout::ParseField(command.target + " " + command.match, true); !field || field->type.empty())
        return Fail(field ? std::string() : field.error());
    command.offset = offset;
    command.kind = Kind::Label;
    command.request.op = ue::op::Fields;
    return command;
}

// export-labels <file> [--module M] [--kinds K,...]: names for a static-analysis tool.
Parsed ExportLabels(const Args& args, const ParseContext&) {
    Command command;
    command.file = args.At(0);
    command.limit = label_kind::All;
    command.target = args.Value("--module");
    if (args.Has("--kinds") && !(command.limit = LabelKinds(args.Value("--kinds"))))
        return Fail("kinds are exec, vtable, interface and global");
    command.kind = Kind::ExportLabels;
    command.request.op = ue::op::ExecEntries;
    return command;
}

// layout-export <file> <Class>...: the fields each class declares, as JSON lines.
Parsed LayoutExport(const Args& args, const ParseContext&) {
    Command command;
    command.kind = Kind::Layout;
    command.target = "export";
    command.file = args.At(0);
    for (size_t i = 1; i < args.positionals.size(); ++i) {
        if (!ClassName(args.positionals[i])) return Fail("'" + args.positionals[i] + "' is not a class or struct name");
        command.fields.push_back(args.positionals[i]);
    }
    if (command.fields.empty()) return Fail();
    if (command.fields.size() > MaxLayoutClasses)
        return Fail("at most " + std::to_string(MaxLayoutClasses) + " classes per call");
    command.request.op = ue::op::Fields;
    return command;
}

// layout-check <file>: the file's lines against the live layout.
Parsed LayoutCheck(const Args& args, const ParseContext&) {
    Command command;
    command.kind = Kind::Layout;
    command.target = "check";
    command.file = args.At(0);
    command.request.op = ue::op::Fields;
    return command;
}

// entries <object> <[Owner:]Field> [--start N]: one page of an array, set or map.
Parsed Entries(const Args& args, const ParseContext& context) {
    Command command;
    if (!Object(args.At(0), context.refs, command.request)) return Fail("object must be $N or an address");
    uint64_t from = 0;
    if (!Cursor(args, INT32_MAX, from)) return Fail("--start is a cursor");
    command.kind = Kind::Entries;
    command.target = args.At(1);
    command.limit = static_cast<uint32_t>(from);
    command.request.op = ue::op::Entries;
    return command;
}

// rows <object> [--match text] [--fields a,b.c] [--start N] [--limit N]: a data table's rows.
Parsed Rows(const Args& args, const ParseContext& context) {
    Command command;
    if (!Object(args.At(0), context.refs, command.request)) return Fail("object must be $N or an address");
    command.limit = DefaultCallRows;
    if (auto why = RowOptions(args, command); !why.empty()) return Fail(std::move(why));
    command.match = args.Value("--match");
    command.kind = Kind::Rows;
    command.request.op = ue::op::ReadObject;
    return command;
}

// get <object> <Field[.Field...]>...: named fields, following object pointers.
Parsed Get(const Args& args, const ParseContext& context) {
    Command command;
    if (!Object(args.At(0), context.refs, command.request)) return Fail("object must be $N or an address");
    for (size_t i = 1; i < args.positionals.size(); ++i) {
        const std::string_view path = args.positionals[i];
        size_t depth = 1;
        for (char c : path) depth += c == '.';
        if (command.fields.size() == MaxGetPaths) return Fail("at most " + std::to_string(MaxGetPaths) + " paths");
        if (path.empty() || path.front() == '.' || path.back() == '.' || path.find("..") != std::string_view::npos ||
            depth > MaxGetDepth) return Fail("a path is Field[.Field...], up to " + std::to_string(MaxGetDepth) + " deep");
        command.fields.emplace_back(path);
    }
    command.nonzero = args.Has("--nonzero");
    if (auto why = Elements(args, command); !why.empty()) return Fail(std::move(why));
    command.kind = Kind::Get;
    command.request.op = ue::op::Fields;
    return command;
}

namespace {
// One end of a trace: "x,y,z" (cm, optionally in parentheses), an object, or object@Socket.
bool TraceEnd(std::string_view token, std::span<const Ref> refs, Command::TracePoint& end) {
    end = {};
    if (const auto at = token.find('@'); at != std::string_view::npos) {
        end.socket = std::string(token.substr(at + 1));
        token = token.substr(0, at);
        if (end.socket.empty() || end.socket.size() >= MaxText) return false;
        return Reference(token, refs, end.object);
    }
    if (Reference(token, refs, end.object)) return true;
    if (token.starts_with('(') && token.ends_with(')')) token = token.substr(1, token.size() - 2);
    for (int axis = 0; axis < 3; ++axis) {
        const size_t comma = token.find(',');
        if ((axis < 2) == (comma == std::string_view::npos)) return false;
        const auto part = token.substr(0, comma);
        const auto [last, error] = std::from_chars(part.data(), part.data() + part.size(), end.point[axis]);
        if (error != std::errc{} || last != part.data() + part.size() || !std::isfinite(end.point[axis])) return false;
        token = axis < 2 ? token.substr(comma + 1) : std::string_view{};
    }
    end.literal = true;
    return true;
}
}

// trace <start> <end> [--channel visibility|camera|N] [--simple] [--ignore <actor>]
Parsed Trace(const Args& args, const ParseContext& context) {
    Command command;
    for (int i = 0; i < 2; ++i)
        if (!TraceEnd(args.At(i), context.refs, command.traceEnds[i]))
            return Fail("an end is x,y,z (cm), an object ($N or a literal ref) or object@Socket: " + std::string(args.At(i)));
    if (args.Has("--channel")) {
        const auto channel = args.Value("--channel");
        uint64_t number = 0;
        if (channel == "visibility") command.traceChannel = 0;
        else if (channel == "camera") command.traceChannel = 1;
        else if (Number(channel, number) && number < 32) command.traceChannel = static_cast<uint8_t>(number);
        else return Fail("--channel is visibility, camera or a trace type query 0..31");
    }
    command.traceSimple = args.Has("--simple");
    if (args.Has("--ignore") && !Reference(args.Value("--ignore"), context.refs, command.traceIgnore))
        return Fail("--ignore is an actor: $N or a literal ref");
    command.kind = Kind::Trace;
    return command;
}

namespace {
// --slot N (default: the last started recording).
bool CallsSlot(const Args& args, Command& command) {
    command.callsSlot = ue::CallsLatest;
    return !args.Has("--slot") || (Number(args.Value("--slot"), command.callsSlot) && command.callsSlot < ue::MaxCallsSlots);
}
}

// calls-start <object|Class> <[Owner:]Function> <duration> [--object <ref>] [--dry]
Parsed CallsStart(const Args& args, const ParseContext& context) {
    Command command;
    Request& request = command.request;
    if (!ObjectOrClass(args.At(0), context.refs, command)) return Fail("object must be $N, an address or a class name");
    if (args.At(1).empty() || args.At(1).size() >= MaxText) return Fail("function name too long");
    CopyText(request.text, args.At(1));
    const uint64_t milliseconds = DurationMilliseconds(args.At(2));
    if (!milliseconds || milliseconds > ue::MaxCallsMilliseconds)
        return Fail("duration is 1 ms .. " + std::to_string(ue::MaxCallsMilliseconds / 1000) + " s");
    request.args[1] = milliseconds;
    if (args.Has("--object")) {
        Ref filter{};
        if (!Reference(args.Value("--object"), context.refs, filter)) return Fail("--object is $N or a literal ref");
        const uint64_t address = filter.address;
        memcpy(request.data, &address, sizeof(address));
        request.length = sizeof(address);
    }
    request.flags = args.Has("--dry") ? ue::CallsDry : 0;
    request.op = ue::op::CallsStart;
    command.kind = Kind::Calls;
    command.target = "start";
    return command;
}

// calls-read [--slot N] [--start N]
Parsed CallsRead(const Args& args, const ParseContext&) {
    Command command;
    if (!CallsSlot(args, command)) return Fail("--slot is 0.." + std::to_string(ue::MaxCallsSlots - 1));
    if (args.Has("--start") && !Number(args.Value("--start"), command.callsAfter)) return Fail("--start is a next_cursor");
    command.request.op = ue::op::CallsRead;
    command.kind = Kind::Calls;
    command.target = "read";
    return command;
}

// calls-stop [--slot N]
Parsed CallsStop(const Args& args, const ParseContext&) {
    Command command;
    if (!CallsSlot(args, command)) return Fail("--slot is 0.." + std::to_string(ue::MaxCallsSlots - 1));
    command.request.op = ue::op::CallsStop;
    command.kind = Kind::Calls;
    command.target = "stop";
    return command;
}

Parsed Census(const Args& args, const ParseContext& context) { return CensusOptions(args, context.refs, false); }

// census-diff: only the classes whose count differs from the last census of that --cls.
Parsed CensusDiff(const Args& args, const ParseContext& context) { return CensusOptions(args, context.refs, true); }

// refs <name|#index>: functions whose bytecode names it. Text must match a
// pooled name exactly; the console resolves it.
Parsed Refs(const Args& args, const ParseContext&) {
    Command command;
    Request& request = command.request;
    const std::string_view query = args.At(0);
    command.kind = Kind::Refs;
    request.op = ue::op::Refs;
    if (query.starts_with('#')) {
        uint64_t index = 0;
        if (!Number(query.substr(1), index) || !index || index > INT32_MAX) return Fail();
        request.args[0] = index;
    } else {
        if (query.size() >= MaxText) return Fail("name too long");
        CopyText(request.text, query);
    }
    return command;
}

Parsed NameFind(const Args& args, const ParseContext&) {
    Command command;
    Request& request = command.request;
    uint64_t from = 0;
    if (!Cursor(args, INT32_MAX, from)) return Fail("--start is a cursor");
    request.args[1] = from;
    if (args.At(0).size() >= MaxText) return Fail("search text too long");
    request.op = ue::op::NameFind;
    CopyText(request.text, args.At(0));
    return command;
}

// each <Class> <Field|[Owner:]Function> [--seconds N]
Parsed Each(const Args& args, const ParseContext& context) {
    Command command;
    if (auto why = Outer(args, context.refs, command); !why.empty()) return Fail(std::move(why));
    if (!ClassName(args.At(0))) return Fail("the first argument is a class name");
    if (args.Has("--seconds")) {
        uint64_t seconds = 0;
        if (!Number(args.Value("--seconds"), seconds) || !seconds || seconds > MaxEachSeconds)
            return Fail("--seconds is 1.." + std::to_string(MaxEachSeconds));
        command.seconds = static_cast<uint32_t>(seconds);
    }
    for (size_t i = 1; i < args.positionals.size(); ++i) {
        const std::string_view path = args.positionals[i];
        size_t depth = 1;
        for (char c : path) depth += c == '.';
        if (command.fields.size() == MaxGetPaths) return Fail("at most " + std::to_string(MaxGetPaths) + " columns");
        if (path.size() >= MaxText) return Fail("a path or function name is under " + std::to_string(MaxText) + " characters");
        if (path.empty() || path.front() == '.' || path.back() == '.' || path.find("..") != std::string_view::npos ||
            depth > MaxGetDepth) return Fail("a path is Field[.Field...], up to " + std::to_string(MaxGetDepth) + " deep");
        command.fields.emplace_back(path);
    }
    if (args.Has("--where")) {
        const std::string_view where = args.Value("--where");
        const auto tilde = where.find('~');
        const auto field = where.substr(0, tilde);
        if (tilde == std::string_view::npos || field.empty() || tilde + 1 == where.size() || field.front() == '.' ||
            field.back() == '.' || field.find("..") != std::string_view::npos || field.size() >= MaxText)
            return Fail("--where is Field[.Field]~text");
        command.whereField = field;
        command.match = where.substr(tilde + 1);
        if (command.seconds) return Fail("--where and --seconds do not mix");
    }
    command.limit = DefaultEachRows;
    if (args.Has("--limit")) {
        uint64_t number = 0;
        if (!Number(args.Value("--limit"), number) || number > UINT32_MAX) return Fail("--limit is a number (0: all)");
        command.limit = static_cast<uint32_t>(number);
    }
    command.nonzero = args.Has("--nonzero");
    if (auto why = Elements(args, command); !why.empty()) return Fail(std::move(why));
    command.kind = Kind::Each;
    command.className = args.At(0);
    command.request.op = ue::op::Each;
    return command;
}
}
