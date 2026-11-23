// reagentlive CLI console. Checks a JSON call, calls the client API, prints the result.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include "reagentlive/frontends/cli/values/layout_json.h"
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/state/cache.h"
#include "reagentlive/frontends/cli/call/build.h"
#include "reagentlive/frontends/cli/call/plan.h"
#include "reagentlive/version.h"
#include "reagentlive/frontends/cli/values/format.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/frontends/cli/state/usage.h"
#include "reagentlive/frontends/cli/code/code_json.h"
#include "reagentlive/frontends/cli/code/native.h"
#include "reagentlive/frontends/cli/results/verbs.h"
#include "reagentlive/frontends/cli/results/budget.h"
#include "reagentlive/core/client.h"

#include "reagentlive/frontends/cli/console/internal.h"
#include "reagentlive/frontends/cli/console/pack.h"

namespace reagentlive::cli {
uint32_t FindProcess(std::wstring_view exeName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{sizeof(entry)};
    uint32_t pid = 0;
    const std::wstring wanted(exeName);
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
        if (!_wcsicmp(entry.szExeFile, wanted.c_str())) { pid = entry.th32ProcessID; break; }
    CloseHandle(snapshot);
    return pid;
}
}

namespace reagentlive::cli::detail {

std::span<const Module> LoadedModules(Session& session) {
    if (session.modules.empty() && !session.modulesListed) RefreshModules(session);
    return session.modules;
}

bool RefreshModules(Session& session) {
    Client& client = session.client;
    if (session.modulesListed) return false;
    session.modulesListed = true;
    if (auto modules = client.Modules(); modules) session.modules = std::move(*modules);
    return true;
}

namespace {
std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return file ? std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()) : std::string();
}

// stats from the log; `current` is this run's session, nullopt with no target.
std::expected<shape::Stats, std::string> BuildStats(const std::filesystem::path& path, std::span<const Verb> verbs,
                                                    const Command& command, std::optional<std::string> current) {
    const auto lines = path.empty() ? std::vector<UsageLine>{} : ParseUsage(ReadFile(path));
    return StatsFor(lines, verbs, {command.all, command.listSessions, command.target}, std::move(current));
}
}

void RunStats(Session& session, const Command& command, std::string_view jsonCommand) {
    auto stats = BuildStats(session.usage.path, session.verbs, command, session.usage.session);
    if (!stats) return JsonError(session, jsonCommand, "validation", stats.error());
    Emit(session, jsonCommand, std::move(*stats));
}

std::filesystem::path CachePath(uint32_t pid) {
    wchar_t folder[MAX_PATH + 1];
    const DWORD length = GetTempPathW(MAX_PATH + 1, folder);
    if (!length || length > MAX_PATH) return {};
    return std::filesystem::path(folder) / (L"reagentlive-" + std::to_wstring(pid) + L".cache");
}

// %TEMP%\reagentlive-<pid>-<owner>.results: the whole results of lines that
// must not run again (output.cpp), per agent instance as the cache file is.
// The first run against an instance removes the same pid's earlier folders.
std::filesystem::path ResultsFolder(uint32_t pid, uint32_t owner) {
    const auto cache = CachePath(pid);
    if (cache.empty() || !owner) return {};
    const auto folder = cache.parent_path();
    const auto ascii = budget::ResultsPrefix(pid);
    const std::wstring prefix(ascii.begin(), ascii.end()); // Temp may hold names a narrow string cannot.
    const auto mine = folder / budget::ResultsName(pid, owner);
    std::error_code error;
    if (std::filesystem::exists(mine, error)) return mine;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        const auto name = entry.path().filename().wstring();
        if (name.starts_with(prefix) && name.ends_with(L".results")) std::filesystem::remove_all(entry.path(), error);
    }
    return mine;
}

namespace {
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

void RunHelp(Session& session, const Command& command, std::string_view jsonCommand) {
    auto help = BuildHelp(session.verbs, session.origins, session.tableHash, command);
    if (!help) return JsonError(session, jsonCommand, "schema", help.error());
    Emit(session, jsonCommand, std::move(*help));
}

// The verb table: the core's rows, the pack's, then the host's own.
std::vector<Verb> AllVerbs(const ConsoleConfig& config) { return TableRows(PackVerbs(), config.verbs); }
std::vector<Origin> AllOrigins() { return TableOrigins(PackOrigins()); }

// A JSON call's own keys: the verb, its args, and the console's call flags.
std::string CallProblem(const Json& call) {
    if (!call.is_object() || !call.contains("verb") || !call["verb"].is_string())
        return "a call is {\"verb\": \"<verb>\", \"args\": {...}}";
    for (const auto& [key, value] : call.items()) {
        if (key == "verb" || key == "args") continue;
        if (key == "budget") {
            if (!value.is_number_unsigned()) return "budget is a number of characters (0: none)";
            continue;
        }
        if (key != "meta" && key != "timing" && key != "big")
            return "unknown key '" + key + "' (a call has verb, args, meta, timing, budget, big)";
        if (!value.is_boolean()) return key + " is true or false";
    }
    return {};
}
bool CallFlag(const Json& call, const char* key) { return call.contains(key) && call[key].is_boolean() && call[key].get<bool>(); }

// One call, read once: its JSON, its verb and the console's call flags.
struct InputLine {
    std::string original;       // As given: the usage log's size and hash.
    std::optional<Json> call;
    std::string name;           // The verb as given; empty for a blank line.
    bool meta = false, timing = false, big = false;
    std::optional<size_t> budget;
    std::string problem;        // Not a call: refused under the verb "call".

    const Json& Args() const {
        static const Json none;
        return call && call->contains("args") ? (*call)["args"] : none;
    }
};

InputLine ReadLine(std::string_view line) {
    InputLine input;
    input.original = std::string(line);
    if (text::Trim(line).empty()) return input;
    Json call = Json::parse(line, nullptr, false);
    input.problem = call.is_discarded() ? std::string("not JSON: a call is {\"verb\": \"<verb>\", \"args\": {...}}") :
        CallProblem(call);
    if (!input.problem.empty()) return input;
    input.meta = CallFlag(call, "meta");
    input.timing = CallFlag(call, "timing");
    input.big = CallFlag(call, "big");
    if (call.contains("budget")) input.budget = call["budget"].get<size_t>();
    input.name = call["verb"].get<std::string>();
    input.call = std::move(call);
    return input;
}

// The calls in a text: a JSON call, a JSON list of calls, or one call per
// line (a line may hold a list too). A line that is no JSON stays as it is,
// for ReadLine to refuse.
std::vector<std::string> SplitCalls(std::string_view text) {
    std::vector<std::string> calls;
    const auto add = [&](const Json& value) {
        if (value.is_array())
            for (const auto& item : value) calls.push_back(item.dump(-1, ' ', true, Json::error_handler_t::replace));
        else calls.push_back(value.dump(-1, ' ', true, Json::error_handler_t::replace));
    };
    if (Json whole = Json::parse(text, nullptr, false); !whole.is_discarded()) {
        add(whole);
        return calls;
    }
    for (size_t start = 0; start < text.size();) {
        const size_t end = std::min(text.find('\n', start), text.size());
        const auto line = text::Trim(text.substr(start, end - start));
        start = end + 1;
        if (line.empty()) continue;
        if (Json value = Json::parse(line, nullptr, false); !value.is_discarded()) add(value);
        else calls.emplace_back(line);
    }
    return calls;
}

// A call checked against its verb's row and built into a command; a refusal
// is the error object (kind schema, the key's schema, a corrected example).
std::expected<Command, Json> BuildLine(std::span<const Verb> verbs, const InputLine& input,
                                       std::span<const Module> modules) {
    const Verb* verb = FindVerb(verbs, input.name);
    if (!verb) {
        std::string message = "unknown verb '" + input.name + "'";
        const auto closest = check::NearestVerbs(verbs, input.name);
        for (size_t i = 0; i < closest.size(); ++i) message += (i ? ", " : " (did you mean ") + closest[i];
        message += closest.empty() ? " (help lists them)" : "?)";
        return std::unexpected(Json{{"kind", "schema"}, {"message", message}});
    }
    auto args = CheckCall(*verb, input.Args());
    if (!args) return std::unexpected(RefusalJson(*verb, input.Args(), args.error()));
    auto built = Build(*verb, *args, modules);
    if (!built) return std::unexpected(RefusalJson(*verb, input.Args(), {built.error(), KeyOf(*verb, built.error())}));
    return std::move(*built);
}

// One-shot lines that are all `help` or `stats`: answered from the table and
// the log, no agent needed.
// How long a console waits for another client to leave the agent.
constexpr std::chrono::milliseconds BusyWait{2000};

bool OfflineOnly(const std::vector<std::string>& lines) {
    return !lines.empty() && std::ranges::all_of(lines, [&](const std::string& line) {
        const InputLine input = ReadLine(line);
        return input.name == "help" || input.name == "stats";
    });
}

void Run(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    const Request& request = command.request;
    if (!client.Supports(request.op))
        return JsonError(session, jsonCommand, "unsupported", "agent does not support this operation");
    switch (request.op) {
    case op::Read: {
        auto bytes = client.Read(request.address, request.length);
        // A string may end before an unreadable page: read up to the page end.
        if (!bytes && StringView(command.view) && 0x1000 - (request.address & 0xfff) < request.length)
            bytes = client.Read(request.address, static_cast<uint32_t>(0x1000 - (request.address & 0xfff)));
        if (!bytes) return CommandError(session, jsonCommand, bytes.error());
        const auto modules = LoadedModules(session);
        shape::Read result;
        result.address = request.address;
        result.where = Locate(request.address, modules);
        result.size = bytes->size();
        result.hex = HexBytes(*bytes);
        if (bytes->size() == 4) {
            shape::ReadViews views;
            int32_t i32{};
            uint32_t u32{};
            format_detail::Load(*bytes, i32);
            format_detail::Load(*bytes, u32);
            views.i32 = i32;
            views.u32 = u32;
            views.f32 = JsonNumber<float>(*bytes);
            result.views = std::move(views);
        } else if (bytes->size() == 8) {
            shape::ReadViews views;
            int64_t i64{};
            uint64_t u64{};
            format_detail::Load(*bytes, i64);
            format_detail::Load(*bytes, u64);
            views.i64 = i64;
            views.u64 = u64;
            views.ptr = NonZero(u64);
            views.ptrWhere = Locate(u64, modules);
            views.f64 = JsonNumber<double>(*bytes);
            result.views = std::move(views);
        }
        if (StringView(command.view)) {
            const auto decoded = command.view == "wstr" ? native::WideString(*bytes) : native::CString(*bytes);
            result.text = decoded.text;
            result.length = decoded.length;
            result.terminated = decoded.terminated;
        } else if (ElementSize(command.view)) {
            result.values = DecodeValues(*bytes, command.view);
        } else if (!command.view.empty()) {
            if (auto parsed = layout::Parse(command.view)) result.layout = layout::Decode(*parsed, *bytes);
        }
        result.region = RegionOf(session, request.address);
        return Emit(session, jsonCommand, std::move(result));
    }
    case op::Write: {
        std::vector<uint8_t> found;
        auto written = request.flags & WriteExpect ?
            client.WriteIf(request.address, std::span(request.data, request.length),
                           std::span(request.data + request.length, request.length), &found) :
            client.Write(request.address, std::span(request.data, request.length));
        if (!written) {
            if (found.empty()) return CommandError(session, jsonCommand, written.error());
            session.failed = true;
            return PrintJson(session, {{"command", jsonCommand}, {"ok", false}, {"error", {{"kind", "agent"},
                {"message", written.error().message}, {"reason", written.error().reason},
                {"found", format_detail::Hex(found, found.size())}}}});
        }
        return Emit(session, jsonCommand,
            shape::Write{request.address, Locate(request.address, LoadedModules(session)), request.length, {}});
    }
    case op::Search: return RunSearch(session, command, jsonCommand);
    case op::Modules: {
        auto modules = client.Modules();
        if (!modules) return CommandError(session, jsonCommand, modules.error());
        shape::Modules result;
        for (const auto& module : *modules) result.rows.push_back(MakeModule(module));
        session.modules = std::move(*modules);
        session.modulesListed = true;
        return Emit(session, jsonCommand, std::move(result));
    }
    }
    JsonError(session, jsonCommand, "unsupported", "unsupported operation");
}

// Commands that make several round trips run held, so they finish in as few
// host ticks as the agent's budget allows. Single requests and timed loops
// (each --seconds) do not: a hold would only add one wait.
bool Held(const Command& command) {
    if (command.kind == Kind::Peek) return true;
    // read: the bytes, then the region they lie in.
    return command.kind == Kind::Remote &&
        (command.request.op == reagentlive::op::Modules || command.request.op == reagentlive::op::Read);
}

bool Execute(Session& session, const InputLine& input, bool inBatch);

std::string UtcNow() {
    return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
}

// One call's line in the usage log, appended when the call ends; the line's
// output is counted by PrintJson while it runs (output.cpp).
struct UsageScope {
    Session& session;
    LineUsage line;
    LineUsage* outer;
    Clock::time_point began = Clock::now();
    UsageLine record;
    bool skip = false;

    UsageScope(Session& session, std::string_view input, bool inBatch) : session(session), outer(session.line) {
        session.line = &line;
        record.in = input.size();
        record.hash = UsageHash(input);
        record.batch = inBatch;
    }
    // The verb as its row names it, and the keys given that are the row's.
    void Describe(std::string_view name, const Json& callArgs) {
        const Verb* verb = FindVerb(session.verbs, name);
        record.verb = verb ? std::string(verb->name) : std::string(name.substr(0, MaxUsageVerb));
        record.origin = verb ? std::string(verb->origin) : std::string();
        if (!verb) return;
        // An object's keys by path (view.limit).
        const auto given = [](const Json& in, const std::string& key) {
            return in.is_object() && in.contains(key) && !in[key].is_null() && !(in[key].is_boolean() && !in[key].get<bool>());
        };
        for (const auto& arg : verb->args) {
            const std::string key(arg.key);
            if (!given(callArgs, key)) continue;
            if (arg.fields.empty()) {
                record.keys.push_back(key);
                continue;
            }
            for (const auto& field : arg.fields)
                if (given(callArgs[key], std::string(field.key))) record.keys.push_back(key + "." + std::string(field.key));
            if (!callArgs[key].is_object() || callArgs[key].empty()) record.keys.push_back(key);
        }
        // A program's own reads of the table and its quit are not the agent's calls.
        const auto has = [&](const char* key) { return std::ranges::find(record.keys, key) != record.keys.end(); };
        skip = record.verb == "quit" || (record.verb == "help" && (has("json") || has("hash")));
    }
    ~UsageScope() {
        session.line = outer;
        if (outer) outer->inner += line.out;
        if (skip || record.verb.empty() || !session.usage.record || session.usage.path.empty()) return;
        record.time = UtcNow();
        record.session = session.usage.session;
        record.caller = session.usage.caller;
        record.out = line.out > line.inner ? line.out - line.inner : 0;
        record.digest = line.digest;
        record.ok = line.ok;
        record.refused = line.refused;
        record.ms = std::chrono::duration<double, std::milli>(Clock::now() - began).count();
        std::ofstream file(session.usage.path, std::ios::binary | std::ios::app);
        file << UsageText(record) << '\n';
    }
};

void RunBatch(Session& session, const Command& command, std::string_view jsonCommand) {
    std::vector<Json> results;
    std::vector<BatchItemTicks> ticks;
    uint64_t first = 0, last = 0;
    {
        Client::Hold hold(session.client);
        session.capture = &results;
        for (const auto& item : command.fields) {
            const size_t printed = results.size();
            BatchItemTicks at{hold.LastTick()};
            hold.BeginSpan();
            Execute(session, ReadLine(item), true);
            if (hold.SpanFirstTick()) {
                at.first = hold.SpanFirstTick();
                at.last = hold.LastTick();
            }
            if (results.size() > printed) {
                ticks.resize(results.size());
                ticks.back() = at;
            }
        }
        session.capture = nullptr;
        first = hold.FirstTick();
        last = hold.LastTick();
    }
    auto result = MakeBatch(std::move(results), ticks, first, last);
    if (!TickAccurate(session)) result.tickAccurate = false;
    Emit(session, jsonCommand, std::move(result));
}

// run: a plan's steps, each run as its own call (its pack checked, its hold
// its own) with its envelope captured into a line; then the report.
void RunPlan(Session& session, const Command& command, std::string_view jsonCommand) {
    std::ifstream in(command.file, std::ios::binary);
    if (!in) return JsonError(session, jsonCommand, "file", "plan: cannot read " + command.file);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto steps = plan::Parse(text);
    if (!steps) return JsonError(session, jsonCommand, "schema", steps.error());
    std::ofstream out;
    if (!command.target.empty()) {
        out.open(command.target, std::ios::binary | std::ios::trunc);
        if (!out) return JsonError(session, jsonCommand, "file", "out: cannot write " + command.target);
    }
    const auto began = Clock::now();
    const auto since = [](Clock::time_point from) {
        return std::chrono::duration<double, std::milli>(Clock::now() - from).count();
    };
    const auto tenths = [](double ms) { return static_cast<double>(static_cast<int64_t>(ms * 10 + 0.5)) / 10; };
    plan::Tally tally;
    shape::Run result;
    std::vector<Json> rows;
    const auto write = [&](const Json& line) {
        ++result.lines;
        if (out.is_open()) out << line.dump(-1, ' ', true, Json::error_handler_t::replace) << '\n';
        else rows.push_back(line);
    };
    std::vector<Json>* outer = session.capture;
    for (size_t index = 0; index < steps->size(); ++index) {
        const plan::Step& step = (*steps)[index];
        plan::Tally::StepTime time{index + 1, step.verb};
        const size_t runs = step.Runs();
        const auto stepBegan = Clock::now();
        for (size_t run = 0; run < runs; ++run) {
            // A repeat keeps its pace from the step's start, not from the last call's end.
            if (run) {
                const auto due = stepBegan + std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<double>(step.every * static_cast<double>(run)));
                if (const auto now = Clock::now(); due > now) std::this_thread::sleep_until(due);
            }
            std::vector<Json> records;
            session.capture = &records;
            const auto callBegan = Clock::now();
            Execute(session, ReadLine(step.call.dump()), false);
            const double ms = since(callBegan);
            session.capture = outer;
            Json envelope = records.empty() ? Json{{"command", step.verb}, {"ok", false},
                {"error", {{"kind", "internal"}, {"message", "no result"}}}} : std::move(records.back());
            Json line = {{"step", index + 1}};
            if (runs > 1) line["run"] = run + 1;
            line["at_ms"] = tenths(since(began));
            line["ms"] = tenths(ms);
            for (auto& [key, value] : envelope.items()) line[key] = std::move(value);
            if (step.expect) {
                const auto why = plan::Mismatch(*step.expect, line);
                line["expect"] = why.empty() ? "met" : "failed";
                if (!why.empty()) line["expect_failed"] = why;
                ++(why.empty() ? tally.expectMet : tally.expectFailed);
            }
            tally.Count(line);
            ++time.calls;
            time.ms += ms;
            write(line);
        }
        tally.steps.push_back(std::move(time));
    }
    shape::RunReport& report = result.report;
    report.steps = steps->size();
    report.calls = tally.calls;
    report.ok = tally.ok;
    report.failed = tally.failed;
    if (!tally.refused.empty()) report.refused = tally.refused;
    report.expectMet = tally.expectMet;
    report.expectFailed = tally.expectFailed;
    report.ms = tenths(since(began));
    for (const auto& time : tally.steps) report.timings.push_back({time.step, time.verb, time.calls, tenths(time.ms)});
    report.console = Version;
    report.commit = ConsoleCommit();
    report.agent = session.client.AgentBuild();
    report.tableHash = session.tableHash;
    if (out.is_open()) {
        out << Json{{"report", ToJson(report)}}.dump(-1, ' ', true, Json::error_handler_t::replace) << '\n';
        out.flush();
        if (!out) return JsonError(session, jsonCommand, "file", "out: cannot write " + command.target);
        result.file = command.target;
    } else {
        result.rows = std::move(rows);
    }
    Emit(session, jsonCommand, std::move(result));
}

// One command line; true on quit. Inside a batch the batch's hold covers it.
bool Execute(Session& session, const InputLine& input, bool inBatch) {
    Client& client = session.client;
    // --meta covers this line (a batch's covers its items); the previous state
    // returns after it, and so does the row a batch runs under.
    // A batch item that may have changed state keeps the batch's result in a
    // file too.
    struct LineScope {
        Session& session;
        bool meta;
        const Verb* row;
        LineInput input;
        ~LineScope() {
            const bool reads = session.input.reads;
            session.meta = meta;
            session.current = row;
            session.input = std::move(input);
            session.input.reads &= reads;
        }
    } lineScope{session, session.meta, session.current, session.input};
    session.current = nullptr;
    {
        // The budget: the call's own, a batch's for its items, else the caller's default.
        LineInput line;
        line.call = input.call;
        line.budget = input.big ? 0 : input.budget ? *input.budget : inBatch ? session.input.budget :
            session.usage.caller == "tool" ? budget::ToolBudget : 0;
        session.input = std::move(line);
    }
    UsageScope usage(session, input.original, inBatch);
    if (!input.problem.empty()) {
        usage.Describe("call", Json());
        JsonError(session, "call", "schema", input.problem);
        return false;
    }
    if (input.meta) session.meta = true;
    const std::string_view jsonCommand = input.name;
    if (jsonCommand.empty()) return false;
    const Json& callArgs = input.Args();
    usage.Describe(input.name, callArgs);
    // The ceiling (--packs): a verb of another pack is refused before it is parsed, inside a batch too.
    if (const Verb* row = FindVerb(session.verbs, jsonCommand); row && !(session.toolPacks & ToolPackBit(row->pack))) {
        const auto pack = std::string(ToolPackName(row->pack));
        JsonError(session, jsonCommand, "pack", std::string(row->name) + " is in the " + pack + " pack, outside this console's --packs " +
            ToolPackList(session.toolPacks) + ": start it with " + pack + " in --packs");
        return false;
    }
    // A host's verb: its row checks the arguments and the result, the host does the rest.
    if (const Verb* host = FindVerb(session.config.verbs, jsonCommand); host && session.config.hostCommand) {
        session.current = host;
        session.input.reads = !host->writes;
        auto args = CheckCall(*host, callArgs);
        if (!args) {
            JsonRefusal(session, jsonCommand, RefusalJson(*host, callArgs, args.error()));
            return false;
        }
        Json envelope;
        if (!session.config.hostCommand(client, *host, *args, envelope) || !envelope.is_object() || !envelope.contains("ok") ||
            !envelope["ok"].is_boolean()) {
            JsonError(session, jsonCommand, "host", "host command returned no result");
        } else if (envelope["ok"].get<bool>()) {
            JsonResult(session, jsonCommand, envelope.value("result", Json::object()));
        } else {
            envelope["command"] = jsonCommand;
            session.failed = true;
            PrintJson(session, std::move(envelope));
        }
        return false;
    }
    // Module addresses ({"module", "rva"}) need the list; a name it lacks
    // may be a module loaded since it was read.
    const bool moduleAddress = input.original.find("\"rva\"") != std::string::npos;
    const auto parse = [&](std::span<const Module> modules) { return BuildLine(session.verbs, input, modules); };
    auto parsed = parse(moduleAddress ? LoadedModules(session) : std::span<const Module>{});
    if (!parsed && moduleAddress && RefreshModules(session)) parsed = parse(LoadedModules(session));
    if (!parsed) {
        JsonRefusal(session, jsonCommand, std::move(parsed.error()));
        return false;
    }
    session.current = parsed->verb;
    session.input.reads = !parsed->verb || !parsed->verb->writes;
    // The table and the verbs' docs are what a client reads to call at all: never digested.
    if (parsed->kind == Kind::Help) session.input.budget = 0;
    if (inBatch && parsed->kind == Kind::Each && parsed->seconds) {
        JsonError(session, jsonCommand, "schema", "seconds: each with seconds is not allowed in a batch");
        return false;
    }
    // A pointer chain: resolved here, then the command runs at its end.
    session.chain.reset();
    if (!parsed->chain.empty()) {
        auto chain = client.ResolveChain(parsed->request.address, parsed->chain);
        if (!chain) {
            CommandError(session, jsonCommand, chain.error());
            return false;
        }
        auto resolved = MakeChain(parsed->request.address, *chain, LoadedModules(session));
        if (chain->broken) {
            session.failed = true;
            Json error = {{"kind", "chain"}, {"message", "hop " + std::to_string(*chain->broken) + ": " + chain->reason},
                {"chain", ToJson(resolved)}};
            PrintJson(session, {{"command", jsonCommand}, {"ok", false}, {"error", std::move(error)}});
            return false;
        }
        parsed->request.address = chain->address;
        session.chain = std::move(resolved);
    }
    switch (parsed->kind) {
    case Kind::Quit:
        Emit(session, jsonCommand, shape::Quit{});
        return true;
    case Kind::Help: RunHelp(session, *parsed, jsonCommand); return false;
    case Kind::Status: PrintStatus(session, jsonCommand); return false;
    case Kind::Stats: RunStats(session, *parsed, jsonCommand); return false;
    case Kind::Pin: RunPin(session, *parsed, jsonCommand); return false;
    case Kind::Trial: RunTrial(session, *parsed, jsonCommand); return false;
    case Kind::Writes: RunWrites(session, *parsed, jsonCommand); return false;
    case Kind::Watch: RunWatch(session, *parsed, jsonCommand); return false;
    case Kind::Batch: RunBatch(session, *parsed, jsonCommand); return false;
    case Kind::Run: RunPlan(session, *parsed, jsonCommand); return false;
    case Kind::Sig: RunSig(session, *parsed, jsonCommand); return false;
    case Kind::Disasm: RunDisasm(session, *parsed, jsonCommand); return false;
    case Kind::Xrefs: RunXrefs(session, *parsed, jsonCommand); return false;
    case Kind::Touches: RunTouches(session, *parsed, jsonCommand); return false;
    case Kind::LabelsSign: RunLabelsSign(session, *parsed, jsonCommand); return false;
    case Kind::LabelsPort: RunLabelsPort(session, *parsed, jsonCommand); return false;
    case Kind::Vtable: RunVtable(session, *parsed, jsonCommand); return false;
    case Kind::MemSnap: RunMemSnap(session, *parsed, jsonCommand); return false;
    case Kind::MemDiff: RunMemDiff(session, *parsed, jsonCommand); return false;
    default: break;
    }
    std::optional<Client::Hold> hold;
    if (!inBatch && (Held(*parsed) || (PackCommand(*parsed) && PackHeld(*parsed)))) hold.emplace(client);
    if (parsed->kind == Kind::Peek) {
        RunPeek(session, *parsed, jsonCommand);
        return false;
    }
    if (PackCommand(*parsed)) {
        RunPackCommand(session, *parsed, jsonCommand);
        return false;
    }
    Run(session, *parsed, jsonCommand);
    return false;
}


// One-shot runs against the same agent instance join the cache's usage
// session when they have its caller: a shell's checks stay out of a tool's figures.
void LoadSessionCache(Session& session, const std::filesystem::path& path, bool oneShot) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return;
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto data = LoadCache(text);
    if (!data || data->owner != session.client.AgentOwner()) return; // Another agent instance: start fresh.
    if (oneShot && !data->session.empty() && data->sessionCaller == session.usage.caller)
        session.usage.session = data->session;
    session.modules = std::move(data->modules);
    session.watchFile = std::move(data->watchFile);
    for (auto& [base, layout] : data->layouts) session.layouts[base] = std::move(layout);
    LoadPackCache(session, data->packs);
}

void SaveSessionCache(const Session& session, const std::filesystem::path& path) {
    CacheData data;
    data.owner = session.client.AgentOwner();
    if (!data.owner) return;
    data.session = session.usage.session;
    data.sessionCaller = session.usage.caller;
    data.watchFile = session.watchFile;
    data.modules.assign(session.modules.begin(), session.modules.end());
    for (const auto& [base, layout] : session.layouts)
        if (std::ranges::any_of(session.modules, [base](const Module& m) { return m.base == base; }))
            data.layouts.emplace_back(base, layout);
    data.packs = SavePackCache(session);
    const auto temporary = std::filesystem::path(path).concat(L".tmp");
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << SaveCache(data);
        if (!file) return;
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
}

// With no target: help and stats from the table and the log (stats: the
// log's latest session). Any other verb is refused with why there is no
// target (kind startup, or busy while another client holds the agent) and
// ends the console, so a client starts it again. Nothing is recorded.
int RunOffline(std::span<const Verb> verbs, std::span<const Origin> origins, const std::string& hash,
               const std::filesystem::path& usagePath, const std::vector<std::string>* lines,
               const std::function<bool(std::string&)>& readStdin, std::string_view kind, std::string_view why) {
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
            print(ErrorEnvelope("call", "schema", input.problem));
            failed = 1;
            continue;
        }
        const std::string& name = input.name;
        if (name.empty()) continue;
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
                print(ErrorEnvelope(name, "schema", help.error()));
                failed = 1;
            }
            break;
        case Kind::Stats:
            if (auto stats = BuildStats(usagePath, verbs, *parsed, std::nullopt)) print(ResultEnvelope(name, ToJson(*stats)));
            else {
                print(ErrorEnvelope(name, "validation", stats.error()));
                failed = 1;
            }
            break;
        case Kind::Quit: print(ResultEnvelope(name, ToJson(shape::Quit{}))); return failed;
        default: print(ErrorEnvelope(name, kind, why)); return 1;
        }
    }
    return failed;
}

// The log trimmed to its cap (state/usage.h), written whole then renamed.
void TrimUsageFile(const std::filesystem::path& path) {
    const auto trimmed = TrimUsage(ReadFile(path));
    if (!trimmed) return;
    const auto temporary = std::filesystem::path(path).concat(L".tmp");
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << *trimmed;
        if (!file) return;
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
}
}
}

namespace reagentlive::cli {
using namespace detail;

int RunConsole(const ConsoleConfig& config, int argc, wchar_t** argv) {
    constexpr const char* usage = "Usage: <console> [pid] [-c '<call or list of calls>' | -c @<file>] [--names <file>] "
        "[--caller tool|script|shell] [--session <id>] [--no-usage] [--packs explore,native,observe,modify]";
    // Before a session exists: print the envelope directly.
    const auto printStartup = [](const Json& envelope) {
        std::puts(envelope.dump(-1, ' ', true, Json::error_handler_t::replace).c_str());
        std::fflush(stdout);
    };
    const auto startupError = [&](std::string_view message) {
        printStartup(ErrorEnvelope("startup", "startup", message));
        return 1;
    };
    Names names;
    uint32_t pid = 0;
    std::filesystem::path namesFile, usagePath, labelsPath;
    bool namesRequired = false, record = true;
    ToolPackSet toolPacks = AllToolPacks;
    std::string caller = "shell", sessionId;
    wchar_t module[MAX_PATH];
    if (const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH); length && length < MAX_PATH) {
        if (!config.namesFile.empty()) namesFile = std::filesystem::path(module).parent_path() / config.namesFile;
        usagePath = std::filesystem::path(module).replace_extension(L".usage.jsonl");
        labelsPath = std::filesystem::path(module).replace_extension(L".labels.jsonl");
    }
    std::optional<std::vector<std::string>> oneShot;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        if (arg == L"-c" && i + 1 < argc && !oneShot) {
            const std::wstring_view wide(argv[++i]);
            std::string calls;
            if (wide.starts_with(L'@')) {
                // A file of calls: no shell quoting, and any text JSON holds.
                std::ifstream file(std::filesystem::path(wide.substr(1)), std::ios::binary);
                if (!file) return startupError("-c @<file>: cannot open the file");
                calls.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            } else {
                for (const wchar_t c : wide) {
                    if (c > 0x7f) return startupError("-c calls must be ASCII (escape others as \\u in JSON, or use -c @<file>)");
                    calls += static_cast<char>(c);
                }
            }
            oneShot = SplitCalls(calls);
        } else if (arg == L"--names" && i + 1 < argc) {
            namesFile = argv[++i];
            namesRequired = true;
        } else if (arg == L"--caller" && i + 1 < argc) {
            const std::wstring_view value(argv[++i]);
            if (value != L"tool" && value != L"script" && value != L"shell") return startupError("--caller is tool, script or shell");
            caller.clear();
            for (const wchar_t c : value) caller += static_cast<char>(c); // ASCII: one of the three words.
        } else if (arg == L"--session" && i + 1 < argc) {
            // The usage log's session for this run: a client that starts a console per call keeps one.
            const std::wstring_view value(argv[++i]);
            const auto plain = [](wchar_t c) {
                return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'-' ||
                    c == L'_' || c == L'.';
            };
            if (value.empty() || value.size() > 64 || !std::ranges::all_of(value, plain))
                return startupError("--session is 1 to 64 letters, digits, '-', '_' or '.'");
            for (const wchar_t c : value) sessionId += static_cast<char>(c);
        } else if (arg == L"--no-usage") {
            record = false;
        } else if (arg == L"--packs" && i + 1 < argc) {
            std::string list;
            for (const wchar_t c : std::wstring_view(argv[++i])) list += c < 0x80 ? static_cast<char>(c) : '?';
            auto packs = ParseToolPacks(list);
            if (!packs) return startupError("--packs: " + packs.error());
            toolPacks = *packs;
        } else if (!pid && !arg.empty() && std::ranges::all_of(arg, [](wchar_t c) { return c >= L'0' && c <= L'9'; })) {
            pid = static_cast<uint32_t>(_wtoi(argv[i]));
        } else {
            return startupError(usage);
        }
    }
    if (oneShot && oneShot->empty()) return startupError("-c holds no calls");
    const auto verbs = AllVerbs(config);
    const auto origins = AllOrigins();
    const auto tableHash = TableHash(verbs, origins);
    const std::vector<std::string>* lines = oneShot ? &*oneShot : nullptr;
    // stdin: each line's calls, in order.
    std::vector<std::string> pending;
    size_t pendingAt = 0;
    const auto readStdin = [&](std::string& line) {
        while (pendingAt == pending.size()) {
            std::string text;
            if (!std::getline(std::cin, text)) return false;
            pending = SplitCalls(text);
            pendingAt = 0;
        }
        line = pending[pendingAt++];
        return true;
    };
    const auto offline = [&](std::string_view why, std::string_view kind = "startup") {
        if (!oneShot) std::fprintf(stderr, "  no target: help and stats only (%.*s)\n", static_cast<int>(why.size()), why.data());
        return RunOffline(verbs, origins, tableHash, usagePath, lines, readStdin, kind, why);
    };
    if (oneShot && OfflineOnly(*oneShot)) return offline("");
    if (!pid) pid = FindProcess(config.exeName);
    // A console with no exe name to look for (the own-thread example's) needs the pid.
    if (!pid)
        return offline(std::string(config.exeName.empty() ? "No target: pass its pid. " : "Target not running. ") + usage);
    if (!namesFile.empty()) {
        std::ifstream file(namesFile);
        if (!file && namesRequired) return startupError("cannot open names file");
        if (file) {
            if (const size_t bad = names.Load(file))
                return startupError("malformed names file at line " + std::to_string(bad));
        }
    }
    // One client per agent. Another console (a script's, a second tool server's) usually ends within a
    // call, so a busy agent is asked again for a short while before the call is refused as busy.
    const auto section = std::wstring(config.sectionPrefix) + std::to_wstring(pid);
    auto client = Client::Attach(section, config.layout);
    for (const auto until = Clock::now() + BusyWait;
         !client && client.error().kind == Error::Kind::Busy && Clock::now() < until;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        client = Client::Attach(section, config.layout);
    }
    if (!client) return offline(client.error().message, client.error().kind == Error::Kind::Busy ? "busy" : "startup");
    client->SetWaitNotice([](std::chrono::milliseconds waited, bool ticking) {
        std::fprintf(stderr, "  waiting %lld s (%s)\n", static_cast<long long>(waited.count() / 1000),
            ticking ? "agent ticking, request running" : "agent not ticking");
        std::fflush(stderr);
    });
    Session session(*client, config);
    session.verbs = verbs;
    session.origins = origins;
    session.tableHash = tableHash;
    session.toolPacks = toolPacks;
    session.pack = MakePackState(session);
    if (const auto problem = LoadPackFiles(session); !problem.empty()) return startupError(problem);
    session.names = std::move(names);
    session.lastBeat = {client->Heartbeat(), Clock::now()};
    const auto cachePath = CachePath(pid);
    session.results = ResultsFolder(pid, client->AgentOwner());
    session.labels = labelsPath;
    if (!config.labelsFile.empty()) {
        const std::filesystem::path host(config.labelsFile);
        session.hostLabels = host.is_absolute() || labelsPath.empty() ? host : labelsPath.parent_path() / host;
    }
    session.usage = {usagePath, record && !usagePath.empty(),
        std::format("{:%Y%m%dT%H%M%S}-{}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()),
            GetCurrentProcessId()), caller};
    if (session.usage.record) TrimUsageFile(usagePath);
    if (!cachePath.empty()) LoadSessionCache(session, cachePath, oneShot.has_value());
    if (!sessionId.empty()) session.usage.session = sessionId;
    size_t next = 0;
    std::string line;
    const auto nextLine = [&] {
        if (oneShot) {
            if (next == oneShot->size()) return false;
            line = (*oneShot)[next++];
            return true;
        }
        return readStdin(line);
    };
    while (nextLine()) {
        const InputLine input = ReadLine(line);
        if (input.timing) session.timing = Timing{&*client, client->Stats(), Clock::now()};
        const bool quit = Execute(session, input, false);
        session.timing.reset();
        if (quit) break;
    }
    if (!cachePath.empty()) SaveSessionCache(session, cachePath);
    return session.failed ? 1 : 0;
}
}

namespace reagentlive::cli::detail {
void ExecuteLine(Session& session, std::string_view line) { Execute(session, ReadLine(line), true); }
}
