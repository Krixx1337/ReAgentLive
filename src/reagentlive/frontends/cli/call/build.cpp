// reagentlive CLI: Build, a call's entry point (build.h), the checks a call
// meets with no target, and the verbs that reach no agent (help, status,
// quit, usage, batch).
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/call/build_internal.h"
#include "reagentlive/frontends/cli/call/plan.h"

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

std::string CallProblem(const Json& call) {
    if (!call.is_object() || !call.contains("verb") || !call["verb"].is_string())
        return "a call is {\"verb\": \"<verb>\", \"args\": {...}}";
    for (const auto& [key, value] : call.items()) {
        if (key == "verb" || key == "args") continue;
        if (key == "max_chars") {
            if (!value.is_number_unsigned()) return "max_chars is a number of characters (0: none)";
            continue;
        }
        if (key != "meta" && key != "timing" && key != "big")
            return "unknown key '" + key + "' (a call has verb, args, meta, timing, max_chars, big)";
        if (!value.is_boolean()) return key + " is true or false";
    }
    return {};
}

std::expected<Command, Json> BuildChecked(std::span<const Verb> verbs, std::string_view name, const Json& args,
                                          std::span<const Module> modules) {
    const Verb* verb = FindVerb(verbs, name);
    if (!verb) {
        std::string message = "unknown verb '" + std::string(name) + "'";
        const auto closest = check::NearestVerbs(verbs, name);
        for (size_t i = 0; i < closest.size(); ++i) message += (i ? ", " : " (did you mean ") + closest[i];
        message += closest.empty() ? " (help lists them)" : "?)";
        return std::unexpected(Json{{"kind", KindName(ErrorKind::Schema)}, {"message", message}});
    }
    auto checked = CheckCall(*verb, args);
    if (!checked) return std::unexpected(RefusalJson(*verb, args, checked.error()));
    auto built = Build(*verb, *checked, modules);
    if (!built) return std::unexpected(RefusalJson(*verb, args, {built.error(), KeyOf(*verb, built.error())}));
    return std::move(*built);
}

namespace {
// Every {"module": "<name>"} in a call, as a loaded module of its own.
void NamedModules(const Json& value, std::vector<Module>& out) {
    if (value.is_object() && value.contains("module") && value["module"].is_string()) {
        const auto name = value["module"].get<std::string>();
        if (name != "base" && std::ranges::none_of(out, [&](const Module& m) { return text::SameText(m.name, name); }))
            out.push_back({name, 0x7ff000000000 + out.size() * 0x100000000, 0x100000000});
    }
    if (value.is_structured())
        for (const auto& item : value) NamedModules(item, out);
}

Json SchemaError(std::string message) { return {{"kind", KindName(ErrorKind::Schema)}, {"message", std::move(message)}}; }
}

std::optional<Json> CheckWithoutTarget(std::span<const Verb> verbs, const Json& call) {
    Json plain = call;
    if (call.is_object() && (call.contains("expect") || call.contains("repeat"))) {
        auto step = plan::Parse(Json::array({call}).dump());
        if (!step) return SchemaError(step.error());
        plain = step->front().call;
    }
    if (const auto problem = CallProblem(plain); !problem.empty()) return SchemaError(problem);
    const auto name = plain["verb"].get<std::string>();
    const Json args = plain.contains("args") ? plain["args"] : Json();
    if (const Verb* verb = FindVerb(verbs, name); verb && !verb->build) {
        if (auto checked = CheckCall(*verb, args); !checked) return RefusalJson(*verb, args, checked.error());
        return std::nullopt;
    }
    std::vector<Module> modules{{"target.exe", 0x7ff600000000, 0x10000000}};
    NamedModules(args, modules);
    auto built = BuildChecked(verbs, name, args, modules);
    if (!built) return std::move(built.error());
    if (built->kind == Kind::Batch)
        for (const auto& item : args["calls"])
            if (auto refused = CheckWithoutTarget(verbs, item)) return refused;
    return std::nullopt;
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

Parsed Usage(const Args& args, const ParseContext&) {
    Command command;
    command.kind = Kind::Usage;
    command.all = args.Has("all");
    command.listSessions = args.Has("sessions");
    command.target = args.Str("session");
    command.advice = args.Has("advice");
    if (command.all + command.listSessions + !command.target.empty() > 1)
        return Fail("all, sessions and session are one at a time");
    return command;
}

namespace {
// Verbs with nothing for a batch to hold.
bool NotInBatch(std::string_view verb) {
    return verb == "batch" || verb == "run" || verb == "quit" || verb == "help" || verb == "status" || verb == "usage";
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
