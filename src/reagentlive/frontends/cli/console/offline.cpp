// reagentlive CLI console: answers from the verb table and the usage log
// (help, usage), and the runs with no target (no agent, or --check).
#include <chrono>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/values/format.h"
#include "reagentlive/frontends/cli/results/verbs.h"
#include "reagentlive/frontends/cli/console/internal.h"
#include "reagentlive/frontends/cli/console/pack.h"

namespace reagentlive::cli::detail {
namespace {
// usage from the log; `current` is this run's session, nullopt with no target.
std::expected<shape::UsageReport, std::string> BuildUsage(const std::filesystem::path& path, std::span<const Verb> verbs,
                                                    const Command& command, std::optional<std::string> current,
                                                    uint64_t contextLimit) {
    const auto lines = path.empty() ? std::vector<UsageLine>{} : ParseUsage(ReadFile(path));
    auto report = UsageFor(lines, verbs, {command.all, command.listSessions, command.advice, command.target, contextLimit},
                           std::move(current));
    if (!report) return report;
    Json baseline;
    if (!command.baseline.empty()) {
        std::ifstream in{std::filesystem::path(command.baseline), std::ios::binary};
        if (!in) return std::unexpected("baseline: cannot read " + command.baseline);
        baseline = Json::parse(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>(), nullptr, false);
        if (auto problem = BaselineProblem(baseline, command.baseline); !problem.empty())
            return std::unexpected(std::move(problem));
    }
    if (!command.file.empty()) {
        std::ofstream out{std::filesystem::path(command.file), std::ios::binary | std::ios::trunc};
        out << ToJson(*report).dump(1, ' ', true, Json::error_handler_t::replace) << '\n';
        if (!out) return std::unexpected("save: cannot write " + command.file);
        report->saved = command.file;
    }
    if (!command.baseline.empty()) report->compare = CompareUsage(baseline, ToJson(*report), command.baseline);
    KeepParts(*report, command.parts);
    return report;
}

// help: the verbs, one verb in full, or the whole table for programs. It
// reads the table only, so it also answers with no agent to attach to.
std::expected<shape::Help, std::string> BuildHelp(std::span<const Verb> verbs, std::span<const Origin> origins,
                                                  const std::string& hash, const Command& command) {
    shape::Help help;
    if (command.hashOnly) {
        help.tableHash = hash;
    } else if (command.all) {
        help = MakeHelpTable(verbs, origins);
        help.tableHash = hash;
    } else if (!command.target.empty()) {
        const Verb* verb = FindVerb(verbs, command.target);
        if (!verb) return std::unexpected("verb: unknown verb '" + command.target + "' (help lists them)");
        help.rows.push_back(MakeHelpRow(*verb, true, false));
    } else {
        help.rows = HelpByPack(verbs);
        help.notes = GeneralNotes();
        for (const auto& note : PackNotes()) help.notes->push_back(note);
    }
    return help;
}

// The usage line of a frontend's refusal, appended as a call's would be.
void LogRefused(const std::filesystem::path& path, std::span<const Verb> verbs, const InputLine& input,
                const std::string& session, const std::string& caller) {
    if (path.empty() || session.empty()) return;
    UsageLine record;
    const Verb* verb = FindVerb(verbs, input.name);
    record.time = std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
    record.session = session;
    record.verb = verb ? std::string(verb->name) : input.name.substr(0, MaxUsageVerb);
    record.origin = verb ? std::string(verb->origin) : std::string();
    record.caller = caller;
    if (const Json& args = input.Args(); args.is_object())
        for (const auto& [key, value] : args.items()) record.keys.push_back(key);
    record.hash = UsageHash(input.original);
    record.in = input.original.size();
    record.ok = false;
    record.refused = (*input.refused)["kind"].get<std::string>();
    if (input.refused->contains("key")) record.refusedKey = (*input.refused)["key"].get<std::string>().substr(0, MaxUsageVerb);
    AppendUsage(path, UsageText(record) + '\n');
}
}

void RunUsage(Session& session, const Command& command, std::string_view jsonCommand) {
    auto stats = BuildUsage(session.usage.path, session.verbs, command, session.usage.session, session.usage.contextLimit);
    if (!stats) return JsonError(session, jsonCommand, ErrorKind::Validation, stats.error());
    Emit(session, jsonCommand, std::move(*stats));
}

void RunHelp(Session& session, const Command& command, std::string_view jsonCommand) {
    auto help = BuildHelp(session.verbs, session.origins, session.tableHash, command);
    if (!help) return JsonError(session, jsonCommand, ErrorKind::Schema, help.error());
    Emit(session, jsonCommand, std::move(*help));
}

// With no target: help and usage from the table and the log (usage: the
// log's latest session). Any other verb is refused with why there is no
// target (kind startup, or busy while another client holds the agent) and
// ends the console, so a client starts it again. Nothing is recorded but a
// frontend's own refusal (a call with `refused`), logged under `session`
// (empty: not logged).
int RunOffline(std::span<const Verb> verbs, std::span<const Origin> origins, const std::string& hash,
               const std::filesystem::path& usagePath, uint64_t contextLimit, const std::vector<std::string>* lines,
               const std::function<bool(std::string&)>& readStdin, ErrorKind kind, std::string_view why,
               const std::string& session, const std::string& caller) {
    const auto print = [](const Json& envelope) {
        std::puts(envelope.dump(-1, ' ', true, Json::error_handler_t::replace).c_str());
        std::fflush(stdout);
    };
    int failed = 0;
    size_t next = 0;
    std::string line;
    const auto nextLine = [&] {
        if (lines) {
            if (next == lines->size()) return false;
            line = (*lines)[next++];
            return true;
        }
        return readStdin(line);
    };
    while (nextLine()) {
        const InputLine input = ReadLine(line);
        if (!input.problem.empty()) {
            print(ErrorEnvelope("call", ErrorKind::Schema, input.problem));
            failed = 1;
            continue;
        }
        const std::string& name = input.name;
        if (name.empty()) continue;
        if (input.refused) {
            LogRefused(usagePath, verbs, input, session, caller);
            print(RefusedEnvelope(input));
            failed = 1;
            continue;
        }
        const auto parsed = BuildLine(verbs, input, {});
        if (!parsed) {
            print(Json{{"command", name}, {"ok", false}, {"error", parsed.error()}});
            failed = 1;
            continue;
        }
        switch (parsed->kind) {
        case Kind::Help:
            if (auto help = BuildHelp(verbs, origins, hash, *parsed)) print(ResultEnvelope(name, ToJson(*help)));
            else {
                print(ErrorEnvelope(name, ErrorKind::Schema, help.error()));
                failed = 1;
            }
            break;
        case Kind::Usage:
            if (auto stats = BuildUsage(usagePath, verbs, *parsed, std::nullopt, contextLimit)) print(ResultEnvelope(name, ToJson(*stats)));
            else {
                print(ErrorEnvelope(name, ErrorKind::Validation, stats.error()));
                failed = 1;
            }
            break;
        case Kind::Quit: print(ResultEnvelope(name, ToJson(shape::Quit{}))); return failed;
        default: print(ErrorEnvelope(name, kind, why)); return 1;
        }
    }
    return failed;
}

// --check: each call checked as it would be before it runs (build.h
// CheckWithoutTarget), with no target attached. One line per call: ok with
// {"checked": true}, or the refusal; exit 1 if any was refused.
int RunCheck(std::span<const Verb> verbs, const std::vector<std::string>* lines,
             const std::function<bool(std::string&)>& readStdin) {
    int failed = 0;
    size_t next = 0;
    std::string line;
    while (lines ? next < lines->size() : readStdin(line)) {
        if (lines) line = (*lines)[next++];
        if (text::Trim(line).empty()) continue;
        const Json call = Json::parse(line, nullptr, false);
        const std::string name = call.is_object() && call.contains("verb") && call["verb"].is_string() ?
            call["verb"].get<std::string>() : "call";
        Json envelope = ResultEnvelope(name, {{"checked", true}});
        if (call.is_discarded()) {
            envelope = ErrorEnvelope("call", ErrorKind::Schema, "not JSON: a call is {\"verb\": \"<verb>\", \"args\": {...}}");
        } else if (auto refused = CheckWithoutTarget(verbs, call)) {
            envelope = Json{{"command", name}, {"ok", false}, {"error", std::move(*refused)}};
        }
        failed |= envelope["ok"].get<bool>() ? 0 : 1;
        std::puts(envelope.dump(-1, ' ', true, Json::error_handler_t::replace).c_str());
    }
    std::fflush(stdout);
    return failed;
}
}
