// reagentlive CLI: parsing the Unreal pack verbs (find, call, dump, get, each, ...).
#include <algorithm>
#include <cstring>
#include <string>
#include "reagentlive/frontends/cli/syntax/layout.h"
#include "reagentlive/frontends/cli/syntax/parse_internal.h"

namespace reagentlive::cli::parse {
std::optional<Parsed> Unreal(std::string_view verb, std::string_view rest, [[maybe_unused]] std::span<const Ref> refs,
    [[maybe_unused]] std::span<const Module> modules) {
    using namespace text;
    Command command;
    [[maybe_unused]] Request& request = command.request;
    const auto fail = [](std::string why) { return Parsed(std::unexpect, std::move(why)); };
    if (verb == "find") {
        // find [text] [--class Class] [--from cursor]; text keeps its inner spaces.
        constexpr auto usage = "usage: find [text] [--class <Class>] [--from <cursor>]";
        std::string text;
        while (!rest.empty()) {
            const auto token = Next(rest);
            if (token == "--class" || token == "--from") {
                const auto value = Next(rest);
                if (value.empty()) return fail(usage);
                if (token == "--class") {
                    if (request.length || value.size() >= ue::MaxClassName) return fail("one class name, under 64 characters");
                    memcpy(request.data, value.data(), value.size());
                    request.data[value.size()] = 0;
                    request.length = static_cast<uint32_t>(value.size() + 1);
                } else {
                    uint64_t from = 0;
                    if (!Number(value, from) || from > INT32_MAX) return fail("cursor must be a slot number");
                    request.args[1] = from;
                }
                continue;
            }
            if (!text.empty()) text += ' ';
            text.append(token);
        }
        if (text.empty() && !request.length) return fail(usage);
        if (text.size() >= MaxText) return fail("search text too long");
        request.op = ue::op::Find;
        CopyText(request.text, text);
        return command;
    }
    if (verb == "call") {
        const auto target = Next(rest);
        const auto function = Next(rest);
        if (target.empty() || function.empty()) return fail("usage: call <object> <[Class:]Function> [hex params]");
        if (!Object(target, refs, request)) return fail("object must be $N or an address");
        if (function.size() >= MaxText) return fail("function name too long");
        if (rest.find('=') != std::string_view::npos || rest == "--typed") {
            command.kind = Kind::TypedCall;
            if (rest != "--typed")
                while (!rest.empty()) command.fields.emplace_back(Next(rest));
        } else if (!Bytes(rest, request.data, ue::MaxParams, request.length))
            return fail("params must be hex pairs, at most " + std::to_string(ue::MaxParams));
        request.op = ue::op::Call;
        CopyText(request.text, function);
        return command;
    }
    // Drops a kept call result: one more call with zeroed inputs (or the given
    // block), so the engine releases what the kept block held.
    if (verb == "release") {
        const auto target = Next(rest);
        const auto function = Next(rest);
        if (target.empty() || function.empty()) return fail("usage: release <object> <[Class:]Function> [hex params]");
        if (!Object(target, refs, request)) return fail("object must be $N or an address");
        if (function.size() >= MaxText) return fail("function name too long");
        if (!Bytes(rest, request.data, ue::MaxParams, request.length))
            return fail("params must be hex pairs, at most " + std::to_string(ue::MaxParams));
        command.kind = Kind::Release;
        request.op = ue::op::Call;
        request.flags = ue::CallRelease;
        CopyText(request.text, function);
        return command;
    }
    if (verb == "funcs") {
        const auto target = Next(rest);
        if (!ObjectOrClass(target, refs, command)) return fail("usage: funcs <object|class> [text] [--from N]");
        auto filter = Next(rest);
        if (filter == "--from" || !rest.empty()) {
            const auto option = filter == "--from" ? filter : Next(rest);
            if (filter == "--from") filter = {};
            uint64_t cursor = 0;
            if (option != "--from" || !Number(Next(rest), cursor) || cursor > ue::MaxFieldStart || !rest.empty())
                return fail("usage: funcs <object|class> [text] [--from N]");
            request.length = static_cast<uint32_t>(cursor);
        }
        if (filter.size() >= MaxText) return fail("function filter too long");
        request.op = ue::op::Functions;
        CopyText(request.text, filter);
        return command;
    }
    if (verb == "func" || verb == "params") {
        const auto target = Next(rest);
        const auto function = Next(rest);
        if (verb == "func" && rest == "--script") {
            command.kind = Kind::Script;
            rest = {};
        }
        if (function.empty() || !rest.empty() || !ObjectOrClass(target, refs, command))
            return fail("usage: " + std::string(verb) + " <object|class> <[Owner:]Function>" + (verb == "func" ? " [--script]" : ""));
        if (function.size() >= MaxText) return fail("function name too long");
        request.op = ue::op::Function;
        CopyText(request.text, function);
        Request probe = request;
        if (!probe.address) probe.address = 1; // A class name gets its ref later.
        if (ue::Check(probe) != Status::Ok) return fail("invalid function name");
        if (verb == "params") request.op = ue::op::Parameters;
        return command;
    }
    if (verb == "name") {
        uint32_t count = 0;
        while (!rest.empty()) {
            uint64_t index = 0;
            if (count == MaxRead / 4 || !Number(Next(rest), index) || index > INT32_MAX)
                return fail("usage: name <index> [index ...] (up to " + std::to_string(MaxRead / 4) +
                            " FName indices; the console pages them)");
            const auto value = static_cast<int32_t>(index);
            memcpy(request.data + count++ * 4, &value, 4);
        }
        if (!count) return fail("usage: name <index> [index ...]");
        request.op = ue::op::Names;
        request.length = count * 4;
        return command;
    }
    if (verb == "describe") {
        const auto target = Next(rest);
        uint64_t index = 0, serial = 0;
        const size_t colon = target.find(':');
        if (target.starts_with('#') && colon != std::string_view::npos && rest.empty() &&
            Number(target.substr(1, colon - 1), index) && Number(target.substr(colon + 1), serial) &&
            index <= INT32_MAX && serial && serial <= INT32_MAX) {
            // Object-table slot and serial (a weak pointer, or an aim capture's target).
            ue::SetRef(request, Ref{0, static_cast<int32_t>(index), static_cast<int32_t>(serial)});
            request.op = ue::op::Describe;
            return command;
        }
        if (!rest.empty() || !Object(target, refs, request)) return fail("usage: describe <address|$N|#index:serial>");
        request.op = ue::op::Describe;
        return command;
    }
    if (verb == "snap" || verb == "record") {
        Ref ref{};
        if (!Reference(Next(rest), refs, ref)) return fail("use an object ref ($N or 0x<address>#<index>:<serial>)");
        ue::SetRef(request, ref);
        if (verb == "record") {
            uint64_t seconds = 0;
            if (!Number(Next(rest), seconds) || seconds < 1 || seconds > 10)
                return fail("usage: record $N <seconds (1..10)> <file> <field> [field ...]");
            command.file = Next(rest);
            while (!rest.empty() && command.fields.size() < 8)
                command.fields.emplace_back(Next(rest));
            if (command.file.empty() || command.fields.empty() || !rest.empty())
                return fail("record needs a file and 1..8 exact field names");
            request.length = static_cast<uint32_t>(seconds);
            command.kind = Kind::Record;
        } else {
            command.file = Next(rest); // Optional snapshot file.
            if (!rest.empty()) return fail("usage: snap <ref> [file]");
            command.kind = Kind::Snap;
        }
        return command;
    }
    if (verb == "fields" || verb == "dump") {
        constexpr auto dumpUsage = "usage: dump <object|class> [--match <text>] [--nonzero] [--changed [file]]";
        const auto target = Next(rest);
        if (target.empty() || (verb == "fields" && !rest.empty()))
            return fail(verb == "dump" ? std::string(dumpUsage) : "usage: fields <object|class>");
        if (!ObjectOrClass(target, refs, command)) return fail("object must be $N, an address or a class name");
        while (!rest.empty()) {
            const auto option = Next(rest);
            if (option == "--nonzero") command.nonzero = true;
            else if (option == "--changed" && !command.changed) {
                command.changed = true;
                if (!rest.empty() && !rest.starts_with("--")) command.file = Next(rest);
            } else if (option == "--match" && command.match.empty() && !rest.empty() && !rest.starts_with("--"))
                command.match = Next(rest);
            else return fail(dumpUsage);
        }
        request.op = ue::op::Fields;
        if (verb == "dump") command.kind = Kind::Dump; // Fields, then reads of the object's bytes.
        return command;
    }
    // label <object|Class> <offset> <type>[n] <Name>: names an unreflected member in the native file.
    if (verb == "label") {
        constexpr auto usage = "usage: label <object|Class> <offset> <type>[n] <Name>";
        const auto target = Next(rest);
        uint64_t offset = 0;
        if (!ObjectOrClass(target, refs, command) || !Number(Next(rest), offset) || offset >= ue::MaxObjectBytes)
            return fail(usage);
        command.target = Next(rest);
        command.match = Next(rest);
        if (command.match.empty() || !rest.empty()) return fail(usage);
        if (auto field = layout::ParseField(command.target + " " + command.match, true); !field || field->type.empty())
            return fail(field ? usage : field.error());
        command.offset = offset;
        command.kind = Kind::Label;
        request.op = ue::op::Fields;
        return command;
    }
    // export-labels <file> [--module M] [--kinds K,...]: names for a static-analysis tool.
    if (verb == "export-labels") {
        constexpr auto usage = "usage: export-labels <file> [--module <name>] [--kinds exec,vtable,interface,global]";
        command.file = Next(rest);
        if (command.file.empty() || command.file.starts_with("--")) return fail(usage);
        command.limit = label_kind::All;
        while (!rest.empty()) {
            const auto option = Next(rest), value = Next(rest);
            if (value.empty()) return fail(usage);
            if (option == "--module") command.target = value;
            else if (option == "--kinds") {
                if (!(command.limit = LabelKinds(value))) return fail("kinds are exec, vtable, interface and global");
            } else return fail(usage);
        }
        command.kind = Kind::ExportLabels;
        request.op = ue::op::ExecEntries;
        return command;
    }
    // entries <object> <[Owner:]Field> [--from N]: one page of an array, set or map.
    if (verb == "entries") {
        constexpr auto usage = "usage: entries <object> <[Owner:]Field> [--from N]";
        const auto target = Next(rest), field = Next(rest);
        if (target.empty() || field.empty() || field.starts_with("--") || !Object(target, refs, request)) return fail(usage);
        uint64_t from = 0;
        if (!rest.empty() && (Next(rest) != "--from" || !Number(Next(rest), from) || from > INT32_MAX || !rest.empty()))
            return fail(usage);
        command.kind = Kind::Entries;
        command.target = field;
        command.limit = static_cast<uint32_t>(from);
        request.op = ue::op::Entries;
        return command;
    }
    if (verb == "get") {
        constexpr auto usage = "usage: get <object> <Field[.Field...]> [...] (Owner:Field where ambiguous)";
        const auto target = Next(rest);
        if (target.empty() || rest.empty() || !Object(target, refs, request)) return fail(usage);
        while (!rest.empty()) {
            const auto path = Next(rest);
            size_t depth = 1;
            for (char c : path) depth += c == '.';
            if (command.fields.size() == MaxGetPaths) return fail("at most " + std::to_string(MaxGetPaths) + " paths");
            if (path.front() == '.' || path.back() == '.' || path.find("..") != std::string_view::npos ||
                depth > MaxGetDepth) return fail(usage);
            command.fields.emplace_back(path);
        }
        command.kind = Kind::Get;
        request.op = ue::op::Fields;
        return command;
    }
    if (verb == "census") {
        constexpr auto usage = "usage: census [--class <Class>] [--match <text>] [--changed] [--limit N] [--from N]";
        command.limit = DefaultCensusLimit;
        while (!rest.empty()) {
            const auto option = Next(rest);
            if (option == "--changed" && !command.changed) {
                command.changed = true;
                continue;
            }
            const auto value = Next(rest);
            uint64_t number = 0;
            if (option == "--match" && command.match.empty() && !value.empty() && !value.starts_with("--")) {
                command.match = value;
            } else if (option == "--class" && ClassName(value) && !request.length) {
                memcpy(request.data, value.data(), value.size());
                request.data[value.size()] = 0;
                request.length = static_cast<uint32_t>(value.size() + 1);
            } else if (option == "--limit" && Number(value, number) && number <= UINT32_MAX) {
                command.limit = static_cast<uint32_t>(number);
            } else if (option == "--from" && Number(value, number) && number <= UINT32_MAX && !command.from) {
                command.from = static_cast<uint32_t>(number);
            } else {
                return fail(usage);
            }
        }
        request.op = ue::op::Census;
        return command;
    }
    // refs <name|#index>: functions whose bytecode names it. Text must match a
    // pooled name exactly; the console resolves it.
    if (verb == "refs") {
        const auto query = Next(rest);
        if (query.empty() || !rest.empty()) return fail("usage: refs <name|#index>");
        command.kind = Kind::Refs;
        request.op = ue::op::Refs;
        if (query.starts_with('#')) {
            uint64_t index = 0;
            if (!Number(query.substr(1), index) || !index || index > INT32_MAX) return fail("usage: refs <name|#index>");
            request.args[0] = index;
        } else {
            if (query.size() >= MaxText) return fail("name too long");
            CopyText(request.text, query);
        }
        return command;
    }
    if (verb == "name-find") {
        constexpr auto usage = "usage: name-find <text> [--from N]";
        const auto query = Next(rest);
        if (query.empty() || query.starts_with("--")) return fail(usage);
        if (!rest.empty()) {
            uint64_t from = 0;
            if (Next(rest) != "--from" || !Number(Next(rest), from) || from > INT32_MAX || !rest.empty()) return fail(usage);
            request.args[1] = from;
        }
        if (query.size() >= MaxText) return fail("search text too long");
        request.op = ue::op::NameFind;
        CopyText(request.text, query);
        return command;
    }
    if (verb == "each") {
        constexpr auto usage = "usage: each <Class> <Field|[Owner:]Function> [--seconds N]";
        const auto type = Next(rest);
        const auto target = Next(rest);
        if (!ClassName(type) || target.empty() || target.starts_with("--")) return fail(usage);
        if (!rest.empty()) {
            uint64_t seconds = 0;
            if (Next(rest) != "--seconds" || !Number(Next(rest), seconds) || !seconds || seconds > MaxEachSeconds ||
                !rest.empty()) return fail("usage: each <Class> <target> [--seconds 1.." + std::to_string(MaxEachSeconds) + "]");
            command.seconds = static_cast<uint32_t>(seconds);
        }
        if (target.size() >= MaxText) return fail("target name too long");
        command.kind = Kind::Each;
        command.className = type;
        command.target = target;
        request.op = ue::op::Each;
        return command;
    }
    // disasm <addr> [count] [--function] [--follow]: one read, decoded in the console.
    return std::nullopt;
}
}
