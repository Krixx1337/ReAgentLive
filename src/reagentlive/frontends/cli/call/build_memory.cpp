// reagentlive CLI: building the memory verbs (read, write, writes, search, watch, ...).
#include <algorithm>
#include <cstring>
#include <string>
#include "reagentlive/frontends/cli/call/build_internal.h"
#include "reagentlive/frontends/cli/call/layout.h"

namespace reagentlive::cli::build {
using namespace text;
namespace {
// A watch range's element type and count ({"type": "f64", "count": 2}): its
// length and type code; false when the type is no read element type.
bool RangeType(std::string_view type, uint64_t count, uint64_t& length, uint32_t& code) {
    const size_t size = ElementSize(type);
    code = WatchTypeCode(type);
    if (!size || !code || !count || count > MaxWatchRange) return false;
    length = size * count;
    return true;
}

// watch-start's ranges: {addr, len}, {addr, type, count?}, {object, field},
// or following a root {offset, len} and {offset, type, count?}.
Parsed WatchRanges(const Args& args, const ParseContext& context, Command command) {
    const std::string form = "ranges: each is {addr, len 1.." + std::to_string(MaxWatchRange) +
        "}, {addr, type, count?} (i8..u64, f32, f64, ptr), {object, field}, or with follow {offset, ...}";
    for (const auto& item : args.At("ranges")) {
        if (command.reads.size() == MaxWatchRanges) return Fail("ranges: at most " + std::to_string(MaxWatchRanges));
        // A reflected field: its place and type from the pack.
        if (item.contains("object")) {
            Ref object{};
            std::string path;
            if (!command.root.empty()) return Fail("ranges: a field range does not follow a root");
            if (!check::Keys(item, {"object", "field"}) || !item["object"].is_string() ||
                !LiteralRef(item["object"].get<std::string>(), object) || !check::IsPath(item["field"]))
                return Fail(form);
            if (auto why = value::Path(item["field"], path, MaxGetDepth); !why.empty()) return Fail("ranges: " + why);
            command.watchFields.push_back({command.reads.size(), object, path});
            command.reads.push_back(ReadSpan{});
            command.watchTypes.push_back(0);
            command.guardObjects.push_back(0);
            continue;
        }
        const bool follows = !command.root.empty();
        const char* where = follows ? "offset" : "addr";
        if (!check::Keys(item, {where, item.contains("type") ? "type" : "len", "count"}, 2) ||
            (item.contains("count") && !item.contains("type")))
            return Fail(form);
        ReadSpan range{};
        uint32_t type = 0;
        uint64_t object = 0;
        if (follows) {
            if (!check::IsInt(item["offset"]) || (range.address = value::Int(item["offset"])) >= MaxRootOffset)
                return Fail("ranges: an offset from the root is below " + std::to_string(MaxRootOffset));
        } else {
            if (!check::Fits("addr", item["addr"])) return Fail(form);
            auto place = value::Address(item["addr"], context.modules);
            if (!place) return Fail("ranges: " + place.error());
            range.address = place->address;
            object = place->object;
        }
        if (item.contains("type")) {
            const uint64_t count = item.contains("count") ? value::Int(item["count"]) : 1;
            if (!item["type"].is_string() || (item.contains("count") && !check::IsInt(item["count"])) ||
                !RangeType(item["type"].get<std::string>(), count, range.length, type) || range.length > MaxWatchRange)
                return Fail(form);
        } else {
            if (!check::IsInt(item["len"]) || !(range.length = value::Int(item["len"])) || range.length > MaxWatchRange)
                return Fail(form);
        }
        command.reads.push_back(range);
        command.watchTypes.push_back(type);
        command.guardObjects.push_back(object);
    }
    return command;
}
}

// mem-snap: up to 1 MiB kept for mem-diff.
Parsed MemSnap(const Args& args, const ParseContext& context) {
    Command command;
    if (auto why = Place(args, "addr", context, command, true); !why.empty()) return Fail(why);
    command.range = args.Int("len");
    if (!command.range || command.range > memsnap::MaxLength) return Fail("len must be 1.." + std::to_string(memsnap::MaxLength));
    command.kind = Kind::MemSnap;
    command.request.op = op::Read;
    return command;
}

Parsed MemDiff(const Args& args, const ParseContext&) {
    Command command;
    command.target = args.Str("compare");
    command.view = args.Str("type");
    if (!ElementSize(command.view)) return Fail("type is one of i8..u64, f32, f64 or ptr");
    command.all = args.Has("keep");
    const uint64_t limit = args.Int("limit", memsnap::DefaultRows);
    if (!limit || limit > memsnap::MaxRows) return Fail("limit is 1.." + std::to_string(memsnap::MaxRows));
    command.limit = static_cast<uint32_t>(limit);
    command.kind = Kind::MemDiff;
    command.request.op = op::Read;
    return command;
}

Parsed Read(const Args& args, const ParseContext& context) {
    Command command;
    Request& request = command.request;
    const bool counted = args.Has("len");
    uint64_t length = args.Int("len", 64);
    if (auto why = Place(args, "addr", context, command, true); !why.empty()) return Fail(why);
    if (args.Has("type")) {
        const std::string view(Trim(args.Str("type")));
        if (view.empty()) return Fail("type is an element type or a layout");
        command.view = view;
        if (StringView(command.view)) {
            if (!counted) length = DefaultStringRead;
        } else if (const size_t size = ElementSize(command.view)) {
            if (length % size) return Fail("len must be a multiple of the element size");
        } else {
            auto parsed = layout::Parse(view);
            if (!parsed) return Fail("type: layout: " + parsed.error());
            if (counted && length != parsed->size)
                return Fail("len " + std::to_string(length) + " differs from the layout's " + std::to_string(parsed->size) +
                    " bytes");
            length = parsed->size;
        }
    }
    if (!length || length > MaxRead) return Fail("len must be 1.." + std::to_string(MaxRead));
    request.op = op::Read;
    request.length = static_cast<uint32_t>(length);
    return command;
}

// peek: what live qwords hold. label appends the one qword's label to a
// labels file for the static tool (no file: the console's own).
Parsed Peek(const Args& args, const ParseContext& context) {
    Command command;
    Request& request = command.request;
    const bool counted = args.Has("count");
    uint64_t count = args.Int("count", 8);
    if (auto why = Place(args, "addr", context, command, true); !why.empty()) return Fail(why);
    if (!count || count > MaxPeek) return Fail("count must be 1.." + std::to_string(MaxPeek));
    if ((args.Has("name") || args.Has("file")) && !args.Has("label")) return Fail("label: name and file go with label");
    if (args.Has("label")) {
        command.label = true;
        command.file = args.Str("file");
        command.target = args.Str("name");
        if (counted && count != 1) return Fail("count: label takes one qword");
        count = 1;
    }
    if (request.address > UINT64_MAX - count * 8) return Fail("addr: the qwords pass the end of memory");
    command.kind = Kind::Peek;
    command.limit = static_cast<uint32_t>(count);
    request.op = op::Read;
    request.length = static_cast<uint32_t>(count * 8);
    return command;
}

// write: a plain write, a trial (undo: the agent restores it) or a pin (hold: rewritten every tick).
namespace {
// write with clear: end the timed writes, all or the one an id names (undo:N or hold:N).
Parsed ClearTimed(const Args& args) {
    Command command;
    command.kind = Kind::TimedWrites;
    command.all = true;                         // clear
    for (const auto* key : {"addr", "data", "expect", "undo", "hold"})
        if (args.Has(key)) return Fail(std::string(key) + ": clear ends timed writes alone; " + key + " goes with a write");
    const std::string id = args.Str("clear");
    if (id == "all") return command;
    const size_t colon = id.find(':');
    uint64_t n = 0;
    const auto mode = std::string_view(id).substr(0, colon);
    if (colon == std::string::npos || (mode != "undo" && mode != "hold") || !Number(std::string_view(id).substr(colon + 1), n) ||
        !n || n > UINT32_MAX) return Fail("clear is \"all\" or one id as timed-writes lists it: undo:N or hold:N");
    command.target = mode;
    command.request.args[0] = n;
    return command;
}
}

Parsed Write(const Args& args, const ParseContext& context) {
    if (args.Has("clear")) return ClearTimed(args);
    if (!args.Has("addr")) return Fail("addr: required (or clear alone, to end timed writes)");
    if (!args.Has("data")) return Fail("data: required (or clear alone, to end timed writes)");
    Command command;
    Request& request = command.request;
    auto place = value::Address(args.At("addr"), context.modules);
    if (!place) return Fail("addr: " + place.error());
    request.address = place->address;
    const bool hold = args.Has("hold"), undo = args.Has("undo"), timed = hold || undo;
    if (hold && undo) return Fail("hold: does not combine with undo (hold ends on its own)");
    const size_t most = !timed ? MaxWrite : hold ? MaxPinBytes : MaxTrialBytes;
    if (!Bytes(args.Str("data"), request.data, most, request.length) || !request.length)
        return Fail("data must be hex pairs, at most " + std::to_string(most));
    const bool expect = args.Has("expect");
    uint32_t expected = 0;
    // Expected bytes sit after the write's own: right after them for a write, at the spec's slot for a timed one.
    uint8_t* expectAt = request.data + (!timed ? request.length : hold ? MaxPinBytes : MaxTrialBytes);
    if (expect && (!Bytes(args.Str("expect"), expectAt, most, expected) || expected != request.length))
        return Fail("expect needs as many hex bytes as data");
    if (!timed) {
        if (expect) request.flags = WriteExpect;
        request.op = op::Write;
        return command;
    }
    const uint64_t n = args.Milliseconds(hold ? "hold" : "after");
    const uint32_t longest = hold ? MaxPinMilliseconds : MaxTrialMilliseconds;
    if (n > longest) return Fail(std::string(hold ? "hold" : "after") + ": a timed write lasts at most " +
        std::to_string(longest / 1000) + " s");
    command.milliseconds = static_cast<uint32_t>(n);
    if (hold) {
        command.kind = Kind::Pin;
        command.guardObjects.push_back(place->object);
        if (expect) request.args[1] = 1;
        request.flags = static_cast<uint32_t>(PinAction::Add);
        return command;
    }
    command.kind = Kind::Trial;
    if (!expect) return Fail("expect: undo needs expect (the bytes it restores over)");
    if (!LiteralRef(args.Str("owner"), command.owner) || command.owner.index < 0)
        return Fail("owner is a literal ref (the object's identity is guarded)");
    if (args.Has("object")) {
        auto object = value::Address(args.At("object"), context.modules);
        if (!object) return Fail("object: " + object.error());
        command.object = object->address;
    }
    request.op = op::Trial;
    request.flags = static_cast<uint32_t>(TrialAction::Add);
    return command;
}

// writes: the timed writes; clear ends one (id) or all.
Parsed TimedWrites(const Args&, const ParseContext&) {
    Command command;
    command.kind = Kind::TimedWrites;
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
    const bool wide = args.Str("type") == "wtext";
    const std::string type = wide ? std::string("text") : args.Str("type");
    const Json& given = args.At("value");
    const std::string value = given.is_string() ? given.get<std::string>() : given.dump();
    if (value.empty()) return Fail("value is empty");
    uint32_t align = 1;
    const auto put = [&](const void* bytes, uint32_t size) {
        memcpy(request.data, bytes, size);
        request.length = size;
        align = size;
    };
    const bool wide64 = type == "u64" || type == "ptr";
    if (!wide64 && (type == "text" || type == "hex") != given.is_string())
        return Fail("value: type " + args.Str("type") + " takes " + (given.is_string() ? "a number" : "a string"));
    if (type == "text") {
        // The text's bytes, no terminator: an exact search for any string holding it.
        const size_t size = value.size() * (wide ? 2 : 1);
        if (size > MaxSearchPattern) return Fail("value: text is at most " + std::to_string(MaxSearchPattern / (wide ? 2 : 1)) +
            (wide ? " UTF-16 characters" : " bytes"));
        for (size_t i = 0; i < value.size(); ++i) {
            if (wide) {
                request.data[i * 2] = static_cast<uint8_t>(value[i]);
                request.data[i * 2 + 1] = 0;
            } else request.data[i] = static_cast<uint8_t>(value[i]);
        }
        request.length = static_cast<uint32_t>(size);
        command.textSearch = wide ? 2 : 1;
    } else if (type == "hex") {
        if (!Bytes(value, request.data, MaxSearchPattern, request.length) || !request.length)
            return Fail("value: hex pairs, at most " + std::to_string(MaxSearchPattern) + " bytes");
    } else if (type == "f32" || type == "f64") {
        const double parsed = given.get<double>();
        if (type == "f32") { const float f = static_cast<float>(parsed); put(&f, 4); }
        else put(&parsed, 8);
    } else if (type == "i32") {
        if (!given.is_number_integer() || given.get<int64_t>() < INT32_MIN || given.get<int64_t>() > INT32_MAX)
            return Fail("value must be a 32-bit integer");
        const int32_t v = static_cast<int32_t>(given.get<int64_t>());
        put(&v, 4);
    } else if (type == "u32") {
        if (!given.is_number_unsigned() || given.get<uint64_t>() > UINT32_MAX) return Fail("value must be a 32-bit unsigned integer");
        const uint32_t v = static_cast<uint32_t>(given.get<uint64_t>());
        put(&v, 4);
    } else {    // u64, ptr: a number, or "0x..." text past a double's precision.
        uint64_t v = 0;
        if (given.is_number_unsigned()) v = given.get<uint64_t>();
        else if (!given.is_string() || !Number(value, v)) return Fail("value must be a 64-bit unsigned integer or \"0x...\"");
        put(&v, 8);
    }
    request.args[1] = 64;
    if (args.Has("writable")) request.flags |= SearchWritable;
    if (args.Has("heap")) request.flags |= SearchPrivate;
    if (args.Has("paged")) request.flags |= SearchPaged;
    const double wait = args.Has("wait") ? static_cast<double>(args.Milliseconds("wait")) / 1000 : 10;
    if (wait > 60) return Fail("wait is up to 60 seconds");
    command.seconds = wait;
    command.target = args.Str("module"); // The console turns it into the module's range.
    for (const auto* key : {"from", "to"}) {
        if (!args.Has(key)) continue;
        auto place = value::Address(args.At(key), context.modules);
        if (!place) return Fail("scope: " + std::string(key) + ": " + place.error());
        (key[0] == 'f' ? request.address : request.args[0]) = place->address;
    }
    // view.cursor goes on where a result's next_cursor stopped; inside a named module it only moves the start.
    const bool ranged = request.address || request.args[0];
    if (!command.target.empty() && ranged) return Fail("scope: module replaces from and to");
    if (args.Has("cursor")) {
        command.cursor = args.Int("cursor");
        if (command.target.empty()) request.address = command.cursor;
    }
    if (args.Has("align")) {
        const uint64_t n = args.Int("align");
        if (!n || n > 16) return Fail("align is 1, 2, 4, 8 or 16");
        align = static_cast<uint32_t>(n);
    }
    if (args.Has("max")) {
        const uint64_t n = args.Int("max");
        if (!n || n > MaxSearchJobHits) return Fail("max is 1.." + std::to_string(MaxSearchJobHits));
        request.args[1] = n;
    }
    request.flags |= align | SearchJobStart;
    request.op = op::Search;
    if (Check(request) != Status::Ok) return Fail("invalid search: align 1, 2, 4, 8 or 16; max 1.." +
        std::to_string(MaxSearchJobHits) + "; to above from");
    return command;
}

// search-job: the search job's hits so far, or stop it.
Parsed SearchJob(const Args& args, const ParseContext&) {
    Command command;
    Request& request = command.request;
    const uint64_t id = args.Int("job");
    if (!id || id > UINT32_MAX) return Fail("job is the id a search returned");
    request.op = op::Search;
    request.args[0] = id;
    request.flags = args.Has("stop") ? SearchJobStop : SearchJobStatus;
    return command;
}

// watch-start: a capture the agent keeps.
Parsed WatchStart(const Args& args, const ParseContext& context) {
    Command command;
    command.kind = Kind::Watch;
    command.target = "start";
    const uint64_t n = args.Milliseconds("duration");
    if (args.Has("every")) {
        const uint64_t every = args.Milliseconds("every");
        if (every > n) return Fail("every takes 1 ms up to the duration");
        command.interval = static_cast<uint32_t>(every);
    }
    if (args.Has("follow")) {
        if (args.Str("follow").empty() || args.Str("follow").size() >= MaxRootName) return Fail("follow: the root's name is 1.." +
            std::to_string(MaxRootName - 1) + " characters");
        command.root = args.Str("follow");
    }
    command.file = args.Str("file");
    const uint64_t longest = command.interval ? MaxWatchCaptureMilliseconds : MaxWatchMilliseconds;
    if (n > longest)
        return Fail(std::string("duration: a watch ") + (command.interval ? "with every" : "sampling every tick") +
            " lasts at most " + std::to_string(longest / 1000) + " s");
    command.milliseconds = static_cast<uint32_t>(n);
    return WatchRanges(args, context, std::move(command));
}

Parsed WatchRead(const Args& args, const ParseContext&) {
    Command command;
    command.kind = Kind::Watch;
    command.target = args.Has("stop") ? "stop" : "read";
    command.all = args.Has("all");
    return command;
}
}
