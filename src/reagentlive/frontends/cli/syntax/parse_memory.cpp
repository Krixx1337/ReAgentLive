// reagentlive CLI: parsing the memory verbs (read, write, search, pin, watch, ...).
#include <algorithm>
#include <cstring>
#include <string>
#include "reagentlive/frontends/cli/syntax/layout.h"
#include "reagentlive/frontends/cli/syntax/parse_internal.h"

namespace reagentlive::cli::parse {
std::optional<Parsed> Memory(std::string_view verb, std::string_view rest, [[maybe_unused]] std::span<const Ref> refs,
    [[maybe_unused]] std::span<const Module> modules) {
    using namespace text;
    Command command;
    [[maybe_unused]] Request& request = command.request;
    const auto fail = [](std::string why) { return Parsed(std::unexpect, std::move(why)); };
    if (verb == "read") {
        constexpr auto usage = "usage: read <addr>[->off...] [len] [--as i8|u8|i16|u16|i32|u32|i64|u64|f32|f64|ptr|cstr|wstr"
            " | \"<type>[n] [name], pad <n>, ...\"]";
        const auto where = Next(rest);
        uint64_t length = 64;
        bool counted = false;
        if (!ChainAddress(where, refs, modules, request.address, command.chain)) return fail(usage);
        if (!rest.empty() && !rest.starts_with("--")) {
            if (!Number(Next(rest), length)) return fail("length must be a number");
            counted = true;
        }
        if (!rest.empty()) {
            if (Next(rest) != "--as") return fail(usage);
            // The rest of the line: one type, or a layout (quotes optional).
            std::string_view view = rest;
            if (view.size() >= 2 && view.front() == '"' && view.back() == '"') view = Trim(view.substr(1, view.size() - 2));
            if (view.empty()) return fail(usage);
            command.view = view;
            if (StringView(command.view)) {
                if (!counted) length = DefaultStringRead;
            } else if (const size_t size = ElementSize(command.view)) {
                if (length % size) return fail("length must be a multiple of the element size");
            } else {
                auto parsed = layout::Parse(view);
                if (!parsed) return fail("layout: " + parsed.error());
                if (counted && length != parsed->size)
                    return fail("length " + std::to_string(length) + " differs from the layout's " +
                        std::to_string(parsed->size) + " bytes");
                length = parsed->size;
            }
        }
        if (!length || length > MaxRead) return fail("length must be 1.." + std::to_string(MaxRead));
        request.op = op::Read;
        request.length = static_cast<uint32_t>(length);
        return command;
    }
    // peek <addr> [count] [--label <file> [name]]: what live qwords hold.
    // --label appends the one qword's label to a labels file for the static tool.
    if (verb == "peek") {
        constexpr auto usage = "usage: peek <addr>[->off...] [count 1..64] [--label <file> [name]]";
        uint64_t count = 8;
        bool counted = false;
        if (!ChainAddress(Next(rest), refs, modules, request.address, command.chain)) return fail(usage);
        if (!rest.empty() && !rest.starts_with("--")) {
            if (!Number(Next(rest), count) || !count || count > MaxPeek) return fail(usage);
            counted = true;
        }
        if (!rest.empty()) {
            if (Next(rest) != "--label") return fail(usage);
            command.file = Next(rest);
            command.target = Next(rest);
            if (command.file.empty() || !rest.empty()) return fail(usage);
            if (counted && count != 1) return fail("--label takes one qword");
            count = 1;
        }
        if (request.address > UINT64_MAX - count * 8) return fail(usage);
        command.kind = Kind::Peek;
        command.limit = static_cast<uint32_t>(count);
        request.op = op::Read;
        request.length = static_cast<uint32_t>(count * 8);
        return command;
    }
    if (verb == "read-batch") {
        constexpr auto usage = "usage: read-batch <addr> <len> [<addr> <len> ...]";
        while (!rest.empty()) {
            ReadSpan read{};
            if (!Address(Next(rest), refs, modules, read.address) || !Number(Next(rest), read.length))
                return fail(usage);
            command.reads.push_back(read);
            if (!ReadBatchSize(command.reads)) return fail("read batch exceeds limits or contains an invalid range");
        }
        if (command.reads.empty()) return fail(usage);
        request.op = op::ReadBatch;
        return command;
    }
    if (verb == "write" && rest.find("--trial") != std::string_view::npos) {
        constexpr auto usage = "usage: write <addr> <hex bytes> --trial --owner <ref> [--object <addr>] "
                               "--expect <hex bytes> [--seconds N | --ms N] | write --trial list | write --trial clear [id]";
        command.kind = Kind::Trial;
        const auto first = Next(rest);
        if (first == "--trial") {
            const auto action = Next(rest);
            uint64_t id = 0;
            if (action != "list" && action != "clear") return fail(usage);
            if (action == "clear" && !rest.empty() && (!Number(Next(rest), id) || !id || id > UINT32_MAX)) return fail(usage);
            if (!rest.empty()) return fail(usage);
            request.op = op::Trial;
            request.flags = static_cast<uint32_t>(action == "list" ? TrialAction::List : TrialAction::Clear);
            request.args[0] = id;
            return command;
        }
        if (IsChain(first)) return fail(ChainRefused);
        if (!Address(first, refs, modules, request.address)) return fail(usage);
        const size_t options = rest.find("--");
        if (!Bytes(Trim(rest.substr(0, options)), request.data, MaxTrialBytes, request.length) || !request.length)
            return fail("bytes must be hex pairs, at most " + std::to_string(MaxTrialBytes));
        rest = options == std::string_view::npos ? std::string_view{} : rest.substr(options);
        command.milliseconds = 10000;
        bool expected = false, owner = false;
        while (!rest.empty()) {
            const auto option = Next(rest);
            uint64_t n = 0;
            if (option == "--trial") continue;
            if (option == "--seconds" || option == "--ms") {
                if (!Number(Next(rest), n) || !n) return fail(usage);
                n *= option == "--seconds" ? 1000 : 1;
                if (n > MaxTrialMilliseconds) return fail("a trial lasts at most " + std::to_string(MaxTrialMilliseconds / 1000) + " s");
                command.milliseconds = static_cast<uint32_t>(n);
            } else if (option == "--owner") {
                if (!Reference(Next(rest), refs, command.owner) || command.owner.index < 0)
                    return fail("--owner needs $N or a literal ref (the object's identity is guarded)");
                owner = true;
            } else if (option == "--object") {
                if (!Address(Next(rest), refs, modules, command.object)) return fail(usage);
            } else if (option == "--expect") {
                const size_t next = rest.find("--");
                uint32_t count = 0;
                if (!Bytes(Trim(rest.substr(0, next)), request.data + MaxTrialBytes, MaxTrialBytes, count) ||
                    count != request.length) return fail("--expect needs as many hex bytes as the write");
                expected = true;
                rest = next == std::string_view::npos ? std::string_view{} : rest.substr(next);
            } else return fail(usage);
        }
        if (!owner || !expected) return fail("a trial needs --owner and --expect");
        request.op = op::Trial;
        request.flags = static_cast<uint32_t>(TrialAction::Add);
        return command;
    }
    if (verb == "write") {
        const auto where = Next(rest);
        constexpr auto usage = "usage: write <addr> <hex bytes> [--expect <hex bytes>]";
        if (IsChain(where)) return fail(ChainRefused);
        if (!Address(where, refs, modules, request.address) || rest.empty()) return fail(usage);
        const size_t option = rest.find("--expect");
        const auto bytes = Trim(rest.substr(0, option));
        if (!Bytes(bytes, request.data, MaxWrite, request.length) || !request.length)
            return fail("bytes must be hex pairs, at most " + std::to_string(MaxWrite));
        if (option != std::string_view::npos) {
            uint32_t expected = 0;
            const auto text = Trim(rest.substr(option + 8));
            if (!Bytes(text, request.data + request.length, MaxWrite, expected) || expected != request.length)
                return fail("--expect needs as many hex bytes as the write");
            request.flags = WriteExpect;
        }
        request.op = op::Write;
        return command;
    }
    if (verb == "base") {
        if (!rest.empty()) return fail("usage: base");
        command.kind = Kind::Base;
        return command;
    }
    if (verb == "search") {
        constexpr auto usage = "usage: search <hex|u32|i32|u64|ptr|f32|f64|text> <value> [--wide] [--from <addr>] "
                               "[--to <addr>] [--module <name>] [--align 1|2|4|8|16] [--writable] [--heap] [--paged] "
                               "[--background] [--max N]";
        const auto type = Next(rest);
        std::string value;
        bool unterminated = false;
        // Quoted text is taken exactly: spaces, ';' and "--" included.
        if (auto quoted = type == "text" ? Quoted(rest, unterminated) : std::nullopt) {
            value = std::move(*quoted);
        } else {
            if (unterminated) return fail("search text: the quote is not closed");
            while (!rest.empty() && !rest.starts_with("--")) {
                if (!value.empty()) value += ' ';
                value.append(Next(rest));
            }
        }
        if (type.empty() || value.empty()) return fail(usage);
        uint32_t align = 1;
        const auto put = [&](const void* bytes, uint32_t size) {
            memcpy(request.data, bytes, size);
            request.length = size;
            align = size;
        };
        uint64_t number = 0;
        const bool wide = rest.find("--wide") != std::string_view::npos;
        if (wide && type != "text") return fail("--wide is for search text");
        if (type == "text") {
            // The text's bytes, no terminator: an exact search for any string holding it.
            const size_t size = value.size() * (wide ? 2 : 1);
            if (size > MaxSearchPattern) return fail("text must be at most " + std::to_string(MaxSearchPattern / (wide ? 2 : 1)) +
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
                return fail("pattern must be hex pairs, at most " + std::to_string(MaxSearchPattern) + " bytes");
        } else if (type == "f32" || type == "f64") {
            char* end = nullptr;
            const std::string text(value);
            const double parsed = std::strtod(text.c_str(), &end);
            if (!end || *end) return fail("value must be a number");
            if (type == "f32") { const float f = static_cast<float>(parsed); put(&f, 4); }
            else put(&parsed, 8);
        } else if (type == "i32") {
            const bool negative = value.starts_with('-');
            if (!Number(negative ? std::string_view(value).substr(1) : std::string_view(value), number) || number > 0x80000000ull ||
                (!negative && number > INT32_MAX)) return fail("value must be a 32-bit integer");
            const int32_t v = negative ? static_cast<int32_t>(-static_cast<int64_t>(number)) : static_cast<int32_t>(number);
            put(&v, 4);
        } else if (type == "u32") {
            if (!Number(value, number) || number > UINT32_MAX) return fail("value must be a 32-bit unsigned integer");
            const uint32_t v = static_cast<uint32_t>(number);
            put(&v, 4);
        } else if (type == "u64" || type == "ptr") {
            if (!Address(value, refs, modules, number)) return fail("value must be a number, $N, base+offset or <module>+offset");
            put(&number, 8);
        } else return fail(usage);
        request.args[1] = 64;
        while (!rest.empty()) {
            const auto option = Next(rest);
            if (option == "--writable") { request.flags |= SearchWritable; continue; }
            if (option == "--heap") { request.flags |= SearchPrivate; continue; }
            if (option == "--paged") { request.flags |= SearchPaged; continue; }
            if (option == "--background") { request.flags |= SearchBackground; continue; }
            if (option == "--wide") continue;
            const auto argument = Next(rest);
            if (option == "--module") {
                if (argument.empty()) return fail(usage);
                command.target = argument; // The console turns it into --from/--to.
                continue;
            }
            uint64_t n = 0;
            if (option == "--from" || option == "--to") {
                if (!Address(argument, refs, modules, n)) return fail(usage);
                (option == "--from" ? request.address : request.args[0]) = n;
            } else if (option == "--align" && Number(argument, n) && n && n <= 16) align = static_cast<uint32_t>(n);
            else if (option == "--max" && Number(argument, n) && n && n <= MaxSearchHits) request.args[1] = n;
            else return fail(usage);
        }
        if (!command.target.empty() && (request.address || request.args[0]))
            return fail("--module replaces --from and --to");
        request.flags |= align;
        request.op = op::Search;
        if (Check(request) != Status::Ok) return fail("invalid search: alignment 1, 2, 4, 8 or 16; --max 1.." +
            std::to_string(MaxSearchHits) + "; --to above --from");
        return command;
    }
    if (verb == "pin") {
        constexpr auto usage = "usage: pin <addr> <hex bytes> [--seconds N | --ms N] [--expect <hex bytes>] | pin list | "
                               "pin clear [id]";
        const auto first = Next(rest);
        command.kind = Kind::Pin;
        if (first == "list" || first == "clear") {
            uint64_t id = 0;
            if (first == "clear" && !rest.empty() && (!Number(Next(rest), id) || !id || id > UINT32_MAX)) return fail(usage);
            if (!rest.empty()) return fail(usage);
            request.flags = static_cast<uint32_t>(first == "list" ? PinAction::List : PinAction::Clear);
            request.args[0] = id;
            return command;
        }
        if (IsChain(first)) return fail(ChainRefused);
        if (!Address(first, refs, modules, request.address)) return fail(usage);
        command.guardObjects.push_back(ObjectBase(first, refs));
        const size_t options = rest.find("--");
        if (!Bytes(Trim(rest.substr(0, options)), request.data, MaxPinBytes, request.length) || !request.length)
            return fail("bytes must be hex pairs, at most " + std::to_string(MaxPinBytes));
        rest = options == std::string_view::npos ? std::string_view{} : rest.substr(options);
        command.milliseconds = 10000;
        while (!rest.empty()) {
            const auto option = Next(rest);
            uint64_t n = 0;
            if (option == "--seconds" || option == "--ms") {
                if (!Number(Next(rest), n) || !n) return fail(usage);
                n *= option == "--seconds" ? 1000 : 1;
                if (n > MaxPinMilliseconds) return fail("a pin lasts at most " + std::to_string(MaxPinMilliseconds / 1000) + " s");
                command.milliseconds = static_cast<uint32_t>(n);
            } else if (option == "--expect") {
                const size_t next = rest.find("--");
                uint32_t expected = 0;
                if (!Bytes(Trim(rest.substr(0, next)), request.data + MaxPinBytes, MaxPinBytes, expected) ||
                    expected != request.length) return fail("--expect needs as many hex bytes as the pin");
                request.args[1] = 1;
                rest = next == std::string_view::npos ? std::string_view{} : rest.substr(next);
            } else return fail(usage);
        }
        request.flags = static_cast<uint32_t>(PinAction::Add);
        return command;
    }
    if (verb == "watch") {
        constexpr auto usage = "usage: watch <seconds|Nms> <addr>:<len> [<addr>:<len> ...] [--then call <object> "
                               "<[Class:]Function> [hex params]] | watch --start <duration> [--every <duration>] "
                               "[--follow <root>] <addr|offset>:<len> ... | watch --read | watch --stop";
        // 10, 10s, 500ms, 10m: milliseconds; 0 when malformed.
        const auto milliseconds = [](std::string_view text) -> uint64_t {
            uint64_t n = 0, scale = 1000;
            if (text.ends_with("ms")) { scale = 1; text.remove_suffix(2); }
            else if (text.ends_with('m')) { scale = 60000; text.remove_suffix(1); }
            else if (text.ends_with('s')) text.remove_suffix(1);
            return Number(text, n) && n <= UINT32_MAX / scale ? n * scale : 0;
        };
        command.kind = Kind::Watch;
        auto first = Next(rest);
        if (first == "--read" || first == "--stop") {
            if (!rest.empty()) return fail(usage);
            command.target = first.substr(2);
            return command;
        }
        const bool start = first == "--start";
        if (start) {
            command.target = "start";
            first = Next(rest);
        }
        const uint64_t n = milliseconds(first);
        if (!n) return fail(usage);
        while (start && rest.starts_with("--")) {
            const auto option = Next(rest);
            const auto value = Next(rest);
            if (option == "--every" && !command.interval) {
                const uint64_t every = milliseconds(value);
                if (!every || every > n) return fail("--every takes 1 ms up to the duration");
                command.interval = static_cast<uint32_t>(every);
            } else if (option == "--follow" && command.root.empty() && !value.empty() && value.size() < MaxRootName) {
                command.root = value;
            } else {
                return fail(usage);
            }
        }
        const uint64_t longest = command.interval ? MaxWatchCaptureMilliseconds : MaxWatchMilliseconds;
        if (n > longest)
            return fail(std::string("a watch ") + (command.interval ? "with --every" : "sampling every tick") +
                " lasts at most " + std::to_string(longest / 1000) + " s");
        command.milliseconds = static_cast<uint32_t>(n);
        const size_t then = rest.find("--then");
        if (then != std::string_view::npos && start) return fail("--then runs only with a blocking watch");
        if (then != std::string_view::npos) {
            command.then = std::string(Trim(rest.substr(then + 6)));
            if (!command.then.starts_with("call ")) return fail("--then takes a call line");
            rest = Trim(rest.substr(0, then));
        }
        while (!rest.empty()) {
            const auto item = Next(rest);
            const size_t colon = item.rfind(':');
            ReadSpan range{};
            // A literal ref has its own colon (0x..#index:serial); the length is after the last one.
            // Following a root, the part before the colon is an offset from it (+0x10 or 0x10).
            auto where = colon == std::string_view::npos ? std::string_view{} : item.substr(0, colon);
            if (!command.root.empty() && where.starts_with('+')) where.remove_prefix(1);
            const bool placed = command.root.empty() ? Address(where, refs, modules, range.address) :
                Number(where, range.address) && range.address < MaxRootOffset;
            if (colon == std::string_view::npos || !placed || !Number(item.substr(colon + 1), range.length) ||
                !range.length || range.length > MaxWatchRange)
                return fail(usage);
            if (command.reads.size() == MaxWatchRanges) return fail("at most " + std::to_string(MaxWatchRanges) + " ranges");
            command.reads.push_back(range);
            command.guardObjects.push_back(command.root.empty() ? ObjectBase(where, refs) : 0);
        }
        if (command.reads.empty()) return fail(usage);
        return command;
    }
    if (verb == "modules") {
        if (!rest.empty()) return fail("usage: modules");
        request.op = op::Modules;
        return command;
    }
    return std::nullopt;
}
}
