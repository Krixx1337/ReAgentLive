// reagentlive CLI: Parse, the console syntax entry point (parse.h), and the
// verbs that reach no agent (help, status, quit, batch).
#include <string>
#include "reagentlive/frontends/cli/syntax/parse_internal.h"

namespace reagentlive::cli {
Parsed Parse(std::span<const Verb> verbs, std::string_view line, std::span<const Ref> refs,
             std::span<const Module> modules) {
    std::string_view rest = line;
    const auto name = text::Next(rest);
    if (name.empty()) return parse::Fail();
    const Verb* verb = FindVerb(verbs, name);
    if (!verb || !verb->parse) return parse::Fail("unknown command '" + std::string(name) + "' (try help)");
    auto args = Tokenise(*verb, rest);
    if (!args) return parse::Fail(args.error());
    auto parsed = verb->parse(*args, ParseContext{refs, modules});
    if (!parsed) {
        const std::string usage = "usage: " + Usage(*verb);
        return parse::Fail(parsed.error().empty() ? usage : parsed.error() + "; " + usage);
    }
    parsed->verb = verb;
    return parsed;
}

namespace parse {
Parsed Help(const Args& args, const ParseContext&) {
    Command command;
    command.kind = Kind::Help;
    command.target = args.At(0);
    command.all = args.Has("--json");
    if (command.all && !command.target.empty()) return Fail("--json prints every verb");
    return command;
}

Parsed Status(const Args&, const ParseContext&) {
    Command command;
    command.kind = Kind::Status;
    return command;
}

Parsed Quit(const Args&, const ParseContext&) {
    Command command;
    command.kind = Kind::Quit;
    return command;
}

// batch { <command>; <command>; ... }: the lines, split on ';' outside quotes.
Parsed Batch(const Args& args, const ParseContext&) {
    using namespace text;
    Command command;
    const std::string_view rest = Trim(args.rest);
    if (!rest.starts_with('{') || !rest.ends_with('}') || rest.size() < 2) return Fail();
    const auto body = rest.substr(1, rest.size() - 2);
    if (FindUnquoted(body, "{}") != std::string_view::npos) return Fail("batches do not nest");
    for (size_t start = 0; start <= body.size();) {
        const size_t end = FindUnquoted(body, ";", start);
        const auto item = Trim(body.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (!item.empty()) {
            std::string_view probe = item;
            const auto inner = Next(probe);
            // Timed commands hold the console, not the tick; the rest have nothing to hold.
            if (inner == "batch" || inner == "watch" || inner == "record" || inner == "quit" || inner == "exit" ||
                inner == "q" || inner == "help" || inner == "?" || inner == "status")
                return Fail("not allowed in a batch: " + std::string(inner));
            if (command.fields.size() == MaxBatchCommands)
                return Fail("at most " + std::to_string(MaxBatchCommands) + " commands");
            command.fields.emplace_back(item);
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    if (command.fields.empty()) return Fail();
    command.kind = Kind::Batch;
    return command;
}
}
}
