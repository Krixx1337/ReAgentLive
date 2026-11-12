// reagentlive CLI: parsing the Unreal pack verbs (find, call, dump, get, each, ...).
#include <algorithm>
#include <cstring>
#include <string>
#include "reagentlive/frontends/cli/syntax/layout.h"
#include "reagentlive/frontends/cli/syntax/parse_internal.h"
#include "reagentlive/ue/protocol_ue.h"

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

Parsed CensusOptions(const Args& args, bool changed) {
    Command command;
    Request& request = command.request;
    command.limit = DefaultCensusLimit;
    command.changed = changed;
    command.match = args.Value("--match");
    if (args.Has("--cls") && (!ClassName(args.Value("--cls")) || !ClassFilter(args, request)))
        return Fail("--cls is one class name");
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

// find [text] [--cls Class] [--start cursor]; text keeps its inner spaces.
Parsed Find(const Args& args, const ParseContext&) {
    Command command;
    Request& request = command.request;
    const std::string text = args.From(0);
    if (!ClassFilter(args, request)) return Fail("one class name, under 64 characters");
    uint64_t from = 0;
    if (!Cursor(args, INT32_MAX, from)) return Fail("cursor must be a slot number");
    request.args[1] = from;
    if (text.empty() && !request.length) return Fail();
    if (text.size() >= MaxText) return Fail("search text too long");
    request.op = ue::op::Find;
    CopyText(request.text, text);
    return command;
}

// call <object> <[Class:]Function> [hex params] | Name=value ... | --typed
Parsed Call(const Args& args, const ParseContext& context) {
    Command command;
    const bool named = std::ranges::any_of(args.positionals.begin() + 2, args.positionals.end(),
        [](const std::string& token) { return token.find('=') != std::string::npos; });
    if (!named && !args.Has("--typed")) return CallBlock(args, context, 2, std::move(command));
    if (args.Has("--typed") && args.positionals.size() > 2) return Fail("--typed is a typed call with no inputs");
    if (!Object(args.At(0), context.refs, command.request)) return Fail("object must be $N or an address");
    if (args.At(1).size() >= MaxText) return Fail("function name too long");
    command.kind = Kind::TypedCall;
    for (size_t i = 2; i < args.positionals.size(); ++i) command.fields.push_back(args.positionals[i]);
    command.request.op = ue::op::Call;
    CopyText(command.request.text, args.At(1));
    return command;
}

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
    command.request.op = ue::op::Fields;
    return command;
}

// dump <object|class> [--match text] [--nonzero] [--changed [file]]: fields, then reads of the object's bytes.
Parsed Dump(const Args& args, const ParseContext& context) {
    Command command;
    if (!ObjectOrClass(args.At(0), context.refs, command)) return Fail("object must be $N, an address or a class name");
    command.nonzero = args.Has("--nonzero");
    command.changed = args.Has("--changed");
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
    command.kind = Kind::Get;
    command.request.op = ue::op::Fields;
    return command;
}

Parsed Census(const Args& args, const ParseContext&) { return CensusOptions(args, false); }

// census-diff: only the classes whose count differs from the last census of that --cls.
Parsed CensusDiff(const Args& args, const ParseContext&) { return CensusOptions(args, true); }

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
Parsed Each(const Args& args, const ParseContext&) {
    Command command;
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
    command.kind = Kind::Each;
    command.className = args.At(0);
    command.request.op = ue::op::Each;
    return command;
}
}
