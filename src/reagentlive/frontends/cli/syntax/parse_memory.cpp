// reagentlive CLI: parsing the memory verbs (read, write, search, pin, watch, ...).
#include <algorithm>
#include <cstring>
#include <string>
#include "reagentlive/frontends/cli/syntax/layout.h"
#include "reagentlive/frontends/cli/syntax/parse_internal.h"

namespace reagentlive::cli::parse {
using namespace text;
namespace {
// An optional pin, trial or capture id: 0 when absent.
bool OptionalId(const Args& args, uint64_t& id) {
    id = 0;
    return args.At(0).empty() || (Number(args.At(0), id) && id && id <= UINT32_MAX);
}

// --seconds N or --ms N into milliseconds, up to `longest`; false when malformed.
bool Duration(const Args& args, uint32_t longest, uint32_t& milliseconds, std::string& why, std::string_view what) {
    for (const std::string_view flag : {"--seconds", "--ms"}) {
        if (!args.Has(flag)) continue;
        uint64_t n = 0;
        if (!Number(args.Value(flag), n) || !n) return false;
        n *= flag == "--seconds" ? 1000 : 1;
        if (n > longest) {
            why = std::string(what) + " lasts at most " + std::to_string(longest / 1000) + " s";
            return false;
        }
        milliseconds = static_cast<uint32_t>(n);
    }
    return true;
}

// 10, 10s, 500ms, 10m: milliseconds; 0 when malformed.
uint64_t Milliseconds(std::string_view text) {
    uint64_t n = 0, scale = 1000;
    if (text.ends_with("ms")) { scale = 1; text.remove_suffix(2); }
    else if (text.ends_with('m')) { scale = 60000; text.remove_suffix(1); }
    else if (text.ends_with('s')) text.remove_suffix(1);
    return Number(text, n) && n <= UINT32_MAX / scale ? n * scale : 0;
}

// watch's ranges: <addr>:<len>, or following a root <offset>:<len>.
Parsed WatchRanges(const Args& args, const ParseContext& context, Command command) {
    for (size_t i = 1; i < args.positionals.size(); ++i) {
        const std::string_view item = args.positionals[i];
        const size_t colon = item.rfind(':');
        ReadSpan range{};
        // A literal ref has its own colon (0x..#index:serial); the length is after the last one.
        // Following a root, the part before the colon is an offset from it (+0x10 or 0x10).
        auto where = colon == std::string_view::npos ? std::string_view{} : item.substr(0, colon);
        if (!command.root.empty() && where.starts_with('+')) where.remove_prefix(1);
        const bool placed = command.root.empty() ? Address(where, context.refs, context.modules, range.address) :
            Number(where, range.address) && range.address < MaxRootOffset;
        if (colon == std::string_view::npos || !placed || !Number(item.substr(colon + 1), range.length) ||
            !range.length || range.length > MaxWatchRange)
            return Fail("a range is <addr>:<len>, len 1.." + std::to_string(MaxWatchRange));
        if (command.reads.size() == MaxWatchRanges) return Fail("at most " + std::to_string(MaxWatchRanges) + " ranges");
        command.reads.push_back(range);
        command.guardObjects.push_back(command.root.empty() ? ObjectBase(where, context.refs) : 0);
    }
    return command;
}
}

Parsed Read(const Args& args, const ParseContext& context) {
    Command command;
    Request& request = command.request;
    uint64_t length = 64;
    const bool counted = !args.At(1).empty();
    if (!ChainAddress(args.At(0), context.refs, context.modules, request.address, command.chain)) return Fail();
    if (counted && !Number(args.At(1), length)) return Fail("length must be a number");
    if (args.Has("--as")) {
        // The rest of the line: one type, or a layout (quotes optional).
        std::string_view view = args.Value("--as");
        if (view.size() >= 2 && view.front() == '"' && view.back() == '"') view = Trim(view.substr(1, view.size() - 2));
        if (view.empty()) return Fail();
        command.view = view;
        if (StringView(command.view)) {
            if (!counted) length = DefaultStringRead;
        } else if (const size_t size = ElementSize(command.view)) {
            if (length % size) return Fail("length must be a multiple of the element size");
        } else {
            auto parsed = layout::Parse(view);
            if (!parsed) return Fail("layout: " + parsed.error());
            if (counted && length != parsed->size)
                return Fail("length " + std::to_string(length) + " differs from the layout's " +
                    std::to_string(parsed->size) + " bytes");
            length = parsed->size;
        }
    }
    if (!length || length > MaxRead) return Fail("length must be 1.." + std::to_string(MaxRead));
    request.op = op::Read;
    request.length = static_cast<uint32_t>(length);
    return command;
}

// peek <addr> [count] [--label <file>] [--name <name>]: what live qwords hold.
// --label appends the one qword's label to a labels file for the static tool.
Parsed Peek(const Args& args, const ParseContext& context) {
    Command command;
    Request& request = command.request;
    uint64_t count = 8;
    const bool counted = !args.At(1).empty();
    if (!ChainAddress(args.At(0), context.refs, context.modules, request.address, command.chain)) return Fail();
    if (counted && (!Number(args.At(1), count) || !count || count > MaxPeek))
        return Fail("count must be 1.." + std::to_string(MaxPeek));
    if (args.Has("--name") && !args.Has("--label")) return Fail("--name goes with --label");
    if (args.Has("--label")) {
        command.file = args.Value("--label");
        command.target = args.Value("--name");
        if (counted && count != 1) return Fail("--label takes one qword");
        count = 1;
    }
    if (request.address > UINT64_MAX - count * 8) return Fail();
    command.kind = Kind::Peek;
    command.limit = static_cast<uint32_t>(count);
    request.op = op::Read;
    request.length = static_cast<uint32_t>(count * 8);
    return command;
}

Parsed ReadBatch(const Args& args, const ParseContext& context) {
    Command command;
    if (args.positionals.size() % 2) return Fail();
    for (size_t i = 0; i < args.positionals.size(); i += 2) {
        ReadSpan read{};
        if (!Address(args.At(i), context.refs, context.modules, read.address) || !Number(args.At(i + 1), read.length))
            return Fail();
        command.reads.push_back(read);
        if (!ReadBatchSize(command.reads)) return Fail("read batch exceeds limits or contains an invalid range");
    }
    command.request.op = op::ReadBatch;
    return command;
}

Parsed Write(const Args& args, const ParseContext& context) {
    Command command;
    Request& request = command.request;
    if (IsChain(args.At(0))) return Fail(ChainRefused);
    if (!Address(args.At(0), context.refs, context.modules, request.address)) return Fail();
    if (!Bytes(args.From(1), request.data, MaxWrite, request.length) || !request.length)
        return Fail("bytes must be hex pairs, at most " + std::to_string(MaxWrite));
    if (const auto* expect = args.Find("--expect")) {
        uint32_t expected = 0;
        if (!Bytes(Args::Joined(*expect), request.data + request.length, MaxWrite, expected) || expected != request.length)
            return Fail("--expect needs as many hex bytes as the write");
        request.flags = WriteExpect;
    }
    request.op = op::Write;
    return command;
}

// trial <addr> <hex bytes> --owner <ref> --expect <hex bytes> [--object <addr>] [--seconds N | --ms N]:
// one write the agent undoes.
Parsed Trial(const Args& args, const ParseContext& context) {
    Command command;
    Request& request = command.request;
    command.kind = Kind::Trial;
    if (IsChain(args.At(0))) return Fail(ChainRefused);
    if (!Address(args.At(0), context.refs, context.modules, request.address)) return Fail();
    if (!Bytes(args.From(1), request.data, MaxTrialBytes, request.length) || !request.length)
        return Fail("bytes must be hex pairs, at most " + std::to_string(MaxTrialBytes));
    command.milliseconds = 10000;
    std::string why;
    if (!Duration(args, MaxTrialMilliseconds, command.milliseconds, why, "a trial")) return Fail(why);
    const auto* expect = args.Find("--expect");
    if (!args.Has("--owner") || !expect) return Fail("a trial needs --owner and --expect");
    if (!Reference(args.Value("--owner"), context.refs, command.owner) || command.owner.index < 0)
        return Fail("--owner needs $N or a literal ref (the object's identity is guarded)");
    if (args.Has("--object") && !Address(args.Value("--object"), context.refs, context.modules, command.object))
        return Fail("--object is an address");
    uint32_t count = 0;
    if (!Bytes(Args::Joined(*expect), request.data + MaxTrialBytes, MaxTrialBytes, count) || count != request.length)
        return Fail("--expect needs as many hex bytes as the write");
    request.op = op::Trial;
    request.flags = static_cast<uint32_t>(TrialAction::Add);
    return command;
}

Parsed TrialList(const Args&, const ParseContext&) {
    Command command;
    command.kind = Kind::Trial;
    command.request.op = op::Trial;
    command.request.flags = static_cast<uint32_t>(TrialAction::List);
    return command;
}

Parsed TrialClear(const Args& args, const ParseContext&) {
    Command command;
    uint64_t id = 0;
    if (!OptionalId(args, id)) return Fail("id is a trial's number");
    command.kind = Kind::Trial;
    command.request.op = op::Trial;
    command.request.flags = static_cast<uint32_t>(TrialAction::Clear);
    command.request.args[0] = id;
    return command;
}

Parsed Base(const Args&, const ParseContext&) {
    Command command;
    command.kind = Kind::Base;
    return command;
}

Parsed Modules(const Args&, const ParseContext&) {
    Command command;
    command.request.op = op::Modules;
    return command;
}

Parsed Search(const Args& args, const ParseContext& context) {
    Command command;
    Request& request = command.request;
    const std::string_view type = args.At(0);
    const std::string value = args.From(1);
    if (value.empty()) return Fail();
    uint32_t align = 1;
    const auto put = [&](const void* bytes, uint32_t size) {
        memcpy(request.data, bytes, size);
        request.length = size;
        align = size;
    };
    uint64_t number = 0;
    const bool wide = args.Has("--wide");
    if (wide && type != "text") return Fail("--wide is for search text");
    if (type == "text") {
        // The text's bytes, no terminator: an exact search for any string holding it.
        const size_t size = value.size() * (wide ? 2 : 1);
        if (size > MaxSearchPattern) return Fail("text must be at most " + std::to_string(MaxSearchPattern / (wide ? 2 : 1)) +
            (wide ? " UTF-16 characters" : " bytes"));
        for (size_t i = 0; i < value.size(); ++i) {
            if (wide) {
                request.data[i * 2] = static_cast<uint8_t>(value[i]);
                request.data[i * 2 + 1] = 0;
            } else request.data[i] = static_cast<uint8_t>(value[i]);
        }
        request.length = static_cast<uint32_t>(size);
    } else if (type == "hex") {
        if (!Bytes(value, request.data, MaxSearchPattern, request.length) || !request.length)
            return Fail("pattern must be hex pairs, at most " + std::to_string(MaxSearchPattern) + " bytes");
    } else if (type == "f32" || type == "f64") {
        char* end = nullptr;
        const double parsed = std::strtod(value.c_str(), &end);
        if (!end || *end) return Fail("value must be a number");
        if (type == "f32") { const float f = static_cast<float>(parsed); put(&f, 4); }
        else put(&parsed, 8);
    } else if (type == "i32") {
        const bool negative = value.starts_with('-');
        if (!Number(negative ? std::string_view(value).substr(1) : std::string_view(value), number) || number > 0x80000000ull ||
            (!negative && number > INT32_MAX)) return Fail("value must be a 32-bit integer");
        const int32_t v = negative ? static_cast<int32_t>(-static_cast<int64_t>(number)) : static_cast<int32_t>(number);
        put(&v, 4);
    } else if (type == "u32") {
        if (!Number(value, number) || number > UINT32_MAX) return Fail("value must be a 32-bit unsigned integer");
        const uint32_t v = static_cast<uint32_t>(number);
        put(&v, 4);
    } else if (type == "u64" || type == "ptr") {
        if (!Address(value, context.refs, context.modules, number))
            return Fail("value must be a number, $N, base+offset or <module>+offset");
        put(&number, 8);
    } else return Fail("type is hex, u32, i32, u64, ptr, f32, f64 or text");
    request.args[1] = 64;
    if (args.Has("--writable")) request.flags |= SearchWritable;
    if (args.Has("--heap")) request.flags |= SearchPrivate;
    if (args.Has("--paged")) request.flags |= SearchPaged;
    if (args.Has("--background")) request.flags |= SearchBackground;
    command.target = args.Value("--module"); // The console turns it into --from/--to.
    uint64_t n = 0;
    if (args.Has("--from") && !Address(args.Value("--from"), context.refs, context.modules, request.address))
        return Fail("--from is an address");
    if (args.Has("--to") && !Address(args.Value("--to"), context.refs, context.modules, request.args[0]))
        return Fail("--to is an address");
    if (args.Has("--align")) {
        if (!Number(args.Value("--align"), n) || !n || n > 16) return Fail("--align is 1, 2, 4, 8 or 16");
        align = static_cast<uint32_t>(n);
    }
    if (args.Has("--max")) {
        if (!Number(args.Value("--max"), n) || !n || n > MaxSearchHits) return Fail("--max is 1.." + std::to_string(MaxSearchHits));
        request.args[1] = n;
    }
    if (!command.target.empty() && (request.address || request.args[0])) return Fail("--module replaces --from and --to");
    request.flags |= align;
    request.op = op::Search;
    if (Check(request) != Status::Ok) return Fail("invalid search: alignment 1, 2, 4, 8 or 16; --max 1.." +
        std::to_string(MaxSearchHits) + "; --to above --from");
    return command;
}

Parsed Pin(const Args& args, const ParseContext& context) {
    Command command;
    Request& request = command.request;
    command.kind = Kind::Pin;
    if (IsChain(args.At(0))) return Fail(ChainRefused);
    if (!Address(args.At(0), context.refs, context.modules, request.address)) return Fail();
    command.guardObjects.push_back(ObjectBase(args.At(0), context.refs));
    if (!Bytes(args.From(1), request.data, MaxPinBytes, request.length) || !request.length)
        return Fail("bytes must be hex pairs, at most " + std::to_string(MaxPinBytes));
    command.milliseconds = 10000;
    std::string why;
    if (!Duration(args, MaxPinMilliseconds, command.milliseconds, why, "a pin")) return Fail(why);
    if (const auto* expect = args.Find("--expect")) {
        uint32_t expected = 0;
        if (!Bytes(Args::Joined(*expect), request.data + MaxPinBytes, MaxPinBytes, expected) || expected != request.length)
            return Fail("--expect needs as many hex bytes as the pin");
        request.args[1] = 1;
    }
    request.flags = static_cast<uint32_t>(PinAction::Add);
    return command;
}

Parsed PinList(const Args&, const ParseContext&) {
    Command command;
    command.kind = Kind::Pin;
    command.request.flags = static_cast<uint32_t>(PinAction::List);
    return command;
}

Parsed PinClear(const Args& args, const ParseContext&) {
    Command command;
    uint64_t id = 0;
    if (!OptionalId(args, id)) return Fail("id is a pin's number");
    command.kind = Kind::Pin;
    command.request.flags = static_cast<uint32_t>(PinAction::Clear);
    command.request.args[0] = id;
    return command;
}

// watch <duration> <addr>:<len>... [--then <call line>]: blocks for the duration.
Parsed Watch(const Args& args, const ParseContext& context) {
    Command command;
    command.kind = Kind::Watch;
    const uint64_t n = Milliseconds(args.At(0));
    if (!n) return Fail("duration is seconds, or Nms, Ns, Nm");
    if (n > MaxWatchMilliseconds)
        return Fail("a watch sampling every tick lasts at most " + std::to_string(MaxWatchMilliseconds / 1000) + " s");
    command.milliseconds = static_cast<uint32_t>(n);
    if (args.Has("--then")) {
        command.then = args.Value("--then");
        if (!command.then.starts_with("call ")) return Fail("--then takes a call line");
    }
    return WatchRanges(args, context, std::move(command));
}

// watch-start <duration> <addr|offset>:<len>... [--every <duration>] [--follow <root>]:
// a capture the agent keeps.
Parsed WatchStart(const Args& args, const ParseContext& context) {
    Command command;
    command.kind = Kind::Watch;
    command.target = "start";
    const uint64_t n = Milliseconds(args.At(0));
    if (!n) return Fail("duration is seconds, or Nms, Ns, Nm");
    if (args.Has("--every")) {
        const uint64_t every = Milliseconds(args.Value("--every"));
        if (!every || every > n) return Fail("--every takes 1 ms up to the duration");
        command.interval = static_cast<uint32_t>(every);
    }
    if (args.Has("--follow")) {
        if (args.Value("--follow").size() >= MaxRootName) return Fail("the root's name is too long");
        command.root = args.Value("--follow");
    }
    const uint64_t longest = command.interval ? MaxWatchCaptureMilliseconds : MaxWatchMilliseconds;
    if (n > longest)
        return Fail(std::string("a watch ") + (command.interval ? "with --every" : "sampling every tick") +
            " lasts at most " + std::to_string(longest / 1000) + " s");
    command.milliseconds = static_cast<uint32_t>(n);
    return WatchRanges(args, context, std::move(command));
}

Parsed WatchRead(const Args&, const ParseContext&) {
    Command command;
    command.kind = Kind::Watch;
    command.target = "read";
    return command;
}

Parsed WatchStop(const Args&, const ParseContext&) {
    Command command;
    command.kind = Kind::Watch;
    command.target = "stop";
    return command;
}
}
