// reagentlive CLI: Build, a call's entry point (build.h), and the verbs that
// reach no agent (help, status, quit, stats, batch).
#include <string>
#include "reagentlive/frontends/cli/call/build_internal.h"

namespace reagentlive::cli {
Parsed Build(const Verb& verb, const Args& args, std::span<const Module> modules) {
    if (!verb.build) return build::Fail("unknown verb '" + std::string(verb.name) + "'");
    auto built = verb.build(args, ParseContext{modules});
    if (built) built->verb = &verb;
    return built;
}

Parsed BuildCall(std::span<const Verb> verbs, const Json& call, std::span<const Module> modules) {
    if (!call.is_object() || !call.contains("verb") || !call["verb"].is_string())
        return build::Fail("a call is {\"verb\": \"<verb>\", \"args\": {...}}");
    const auto name = call["verb"].get<std::string>();
    const Verb* verb = FindVerb(verbs, name);
    if (!verb) return build::Fail("unknown verb '" + name + "' (help lists them)");
    auto args = CheckCall(*verb, call.contains("args") ? call["args"] : Json());
    if (!args) return build::Fail(args.error().message);
    return Build(*verb, *args, modules);
}

namespace build {
Parsed Help(const Args& args, const ParseContext&) {
    Command command;
    command.kind = Kind::Help;
    command.target = args.Str("verb");
    command.all = args.Has("json");
    command.hashOnly = args.Has("hash");
    if (command.all && !command.target.empty()) return Fail("json prints every verb: no verb");
    if (command.hashOnly && (command.all || !command.target.empty())) return Fail("hash prints the hash alone");
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

Parsed Stats(const Args& args, const ParseContext&) {
    Command command;
    command.kind = Kind::Stats;
    command.all = args.Has("all");
    command.listSessions = args.Has("sessions");
    command.target = args.Str("session");
    if (command.all + command.listSessions + !command.target.empty() > 1)
        return Fail("all, sessions and session are one at a time");
    return command;
}

namespace {
// Verbs with nothing for a batch to hold.
bool NotInBatch(std::string_view verb) {
    return verb == "batch" || verb == "run" || verb == "quit" || verb == "help" || verb == "status" || verb == "stats";
}
}

// batch: its calls, each kept as JSON text and run as its own call.
Parsed Batch(const Args& args, const ParseContext&) {
    Command command;
    for (const auto& call : args.At("calls")) {
        const auto verb = call["verb"].get<std::string>();
        if (NotInBatch(verb)) return Fail("calls: not allowed in a batch: " + verb);
        if (command.fields.size() == MaxBatchCommands)
            return Fail("calls: at most " + std::to_string(MaxBatchCommands) + " calls");
        command.fields.push_back(call.dump());
    }
    command.kind = Kind::Batch;
    return command;
}

// run: a plan file and where its lines go; the console reads and checks the plan.
Parsed Run(const Args& args, const ParseContext&) {
    Command command;
    command.kind = Kind::Run;
    command.file = args.Str("plan");
    command.target = args.Str("out");
    if (command.file.empty()) return Fail("plan is the plan file's path");
    if (args.Has("out") && command.target.empty()) return Fail("out is a file path");
    return command;
}
}
}
