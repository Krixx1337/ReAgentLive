// reagentlive CLI: Parse, the console syntax entry point (parse.h).
#include <string>
#include "reagentlive/frontends/cli/syntax/parse_internal.h"

namespace reagentlive::cli {
Parsed Parse(std::string_view line, std::span<const Ref> refs, std::span<const Module> modules) {
    using namespace text;
    std::string_view rest = line;
    const auto verb = Next(rest);
    Command command;
    [[maybe_unused]] Request& request = command.request;
    const auto fail = [](std::string why) { return Parsed(std::unexpect, std::move(why)); };
    if (verb == "help" || verb == "?") { command.kind = Kind::Help; return command; }
    if (verb == "status") { command.kind = Kind::Status; return command; }
    if (verb == "quit" || verb == "exit" || verb == "q") { command.kind = Kind::Quit; return command; }
    for (const auto area : {parse::Memory, parse::Unreal, parse::Code})
        if (auto parsed = area(verb, rest, refs, modules)) return std::move(*parsed);
    if (verb == "batch") {
        constexpr auto usage = "usage: batch { <command>; <command>; ... }";
        if (!rest.starts_with('{') || !rest.ends_with('}')) return fail(usage);
        const auto body = rest.substr(1, rest.size() - 2);
        if (FindUnquoted(body, "{}") != std::string_view::npos) return fail("batches do not nest");
        for (size_t start = 0; start <= body.size();) {
            const size_t end = FindUnquoted(body, ";", start);
            const auto item = Trim(body.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
            if (!item.empty()) {
                std::string_view probe = item;
                const auto inner = Next(probe);
                if (inner == "batch" || inner == "watch" || inner == "record" || inner == "quit" || inner == "exit" ||
                    inner == "q" || inner == "help" || inner == "?" || inner == "status")
                    return fail("not allowed in a batch: " + std::string(inner));
                if (command.fields.size() == MaxBatchCommands)
                    return fail("at most " + std::to_string(MaxBatchCommands) + " commands");
                command.fields.emplace_back(item);
            }
            if (end == std::string_view::npos) break;
            start = end + 1;
        }
        if (command.fields.empty()) return fail(usage);
        command.kind = Kind::Batch;
        return command;
    }
    if (verb.empty()) return fail("");
    return fail("unknown command '" + std::string(verb) + "' (try help)");
}
}
