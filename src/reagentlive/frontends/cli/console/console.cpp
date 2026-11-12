// reagentlive CLI console. Parses a line, calls the client API, prints the result.
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
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/frontends/cli/values/format.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/frontends/cli/state/usage.h"
#include "reagentlive/frontends/cli/code/code_json.h"
#include "reagentlive/frontends/cli/code/native.h"
#include "reagentlive/frontends/cli/results/verbs.h"
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

// stats: from the log; `session` nullopt for --all, empty for none recorded yet.
shape::Stats BuildStats(const std::filesystem::path& path, std::span<const Verb> verbs, bool all,
                        std::optional<std::string> session) {
    const auto lines = path.empty() ? std::vector<UsageLine>{} : ParseUsage(ReadFile(path));
    if (all) session.reset();
    else if (!session) session = LatestSession(lines);
    return MakeStats(lines, session, verbs);
}
}

void RunStats(Session& session, const Command& command, std::string_view jsonCommand) {
    Emit(session, jsonCommand, BuildStats(session.usage.path, session.verbs, command.all, session.usage.session));
}

namespace {
// help: the verbs, one verb in full, or the whole table for programs. It
// reads the table only, so it also answers with no agent to attach to.
std::expected<shape::Help, std::string> BuildHelp(std::span<const Verb> verbs, std::span<const Origin> origins,
                                                  const std::string& hash, const Command& command) {
    shape::Help help;
    if (command.all) {
        help = MakeHelpTable(verbs, origins);
        help.tableHash = hash;
    } else if (!command.target.empty()) {
        const Verb* verb = FindVerb(verbs, command.target);
        if (!verb) return std::unexpected("unknown command '" + command.target + "' (try help)");
        help.rows.push_back(MakeHelpRow(*verb, true, false));
    } else {
        for (const auto& verb : verbs) help.rows.push_back(MakeHelpRow(verb, false, false));
        help.notes = GeneralNotes();
        for (const auto& note : PackNotes()) help.notes->push_back(note);
    }
    return help;
}

void RunHelp(Session& session, const Command& command, std::string_view jsonCommand) {
    auto help = BuildHelp(session.verbs, session.origins, session.tableHash, command);
    if (!help) return JsonError(session, jsonCommand, "syntax", help.error());
    Emit(session, jsonCommand, std::move(*help));
}

// The verb table: the core's rows, the pack's, then the host's own.
std::vector<Verb> AllVerbs(const ConsoleConfig& config) { return TableRows(PackVerbs(), PackOrigins(), config.verbs); }
std::vector<Origin> AllOrigins() { return TableOrigins(PackOrigins()); }

// A JSON call's own keys: the verb, its arguments, and the console's two line flags.
std::string CallProblem(const Json& call) {
    if (!call.is_object() || !call.contains("verb") || !call["verb"].is_string())
        return "a JSON call is {\"verb\": \"<verb>\", \"args\": {...}}";
    for (const auto& [key, value] : call.items()) {
        if (key == "verb" || key == "args") continue;
        if (key != "meta" && key != "timing") return "unknown key '" + key + "' (a call has verb, args, meta, timing)";
        if (!value.is_boolean()) return key + " is true or false";
    }
    return {};
}
bool CallFlag(const Json& call, const char* key) { return call.contains(key) && call[key].is_boolean() && call[key].get<bool>(); }

// One-shot lines that are all `help` or `stats`: answered from the table and
// the log, no agent needed.
bool OfflineOnly(std::span<const Verb> verbs, const std::vector<std::string>& lines) {
    const Verb* help = FindVerb(verbs, "help");
    const Verb* stats = FindVerb(verbs, "stats");
    return !lines.empty() && std::ranges::all_of(lines, [&](const std::string& line) {
        std::string_view rest = line;
        const Verb* verb = FindVerb(verbs, text::Next(rest));
        return verb && (verb == help || verb == stats);
    });
}

void Run(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    const Request& request = command.request;
    if (command.kind == Kind::Base) {
        const auto modules = LoadedModules(session);
        if (modules.empty()) return JsonError(session, jsonCommand, "unavailable", "base unknown: modules unavailable");
        return Emit(session, jsonCommand, MakeModule(modules.front()));
    }
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
        return Emit(session, jsonCommand, std::move(result));
    }
    case op::ReadBatch: {
        auto batch = client.ReadBatch(command.reads);
        if (!batch) return CommandError(session, jsonCommand, batch.error());
        const auto modules = LoadedModules(session);
        shape::ReadBatch result;
        result.tick = batch->tick;
        for (size_t i = 0; i < batch->items.size(); ++i) {
            const auto& item = batch->items[i];
            const auto& read = command.reads[i];
            shape::ReadBatchRow row;
            row.address = read.address;
            row.where = Locate(read.address, modules);
            row.size = read.length;
            row.ok = item.status == Status::Ok;
            if (row.ok) row.hex = HexBytes(item.bytes);
            else row.error = shape::ReadError{static_cast<uint32_t>(item.status), StatusText(item.status)};
            result.rows.push_back(std::move(row));
        }
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
            shape::Write{request.address, Locate(request.address, LoadedModules(session)), request.length});
    }
    case op::Search: {
        SearchQuery query;
        query.pattern.assign(request.data, request.data + request.length);
        query.from = request.address;
        query.to = request.args[0];
        query.alignment = SearchAlignment(request.flags);
        query.writableOnly = request.flags & SearchWritable;
        query.privateOnly = request.flags & SearchPrivate;
        query.paged = request.flags & SearchPaged;
        query.background = request.flags & SearchBackground;
        query.maxHits = static_cast<uint32_t>(request.args[1]);
        if (!command.target.empty()) {
            const auto* match = text::ModuleNamed(command.target, LoadedModules(session));
            if (!match && RefreshModules(session)) match = text::ModuleNamed(command.target, LoadedModules(session));
            if (!match) return JsonError(session, jsonCommand, "module", "no loaded module with that name");
            query.from = match->base;
            query.to = match->base + match->size;
        }
        auto found = client.Search(query, std::max(client.Timeout(), std::chrono::milliseconds(180000)));
        if (!found) return CommandError(session, jsonCommand, found.error());
        const auto modules = LoadedModules(session);
        shape::Search result;
        for (uint64_t hit : found->hits) result.rows.push_back({hit, Locate(hit, modules)});
        result.from = query.from;
        result.nextCursor = NonZero(found->resume);
        return Emit(session, jsonCommand, std::move(result));
    }
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
// (watch, record, each --seconds) do not: a hold would only add one wait.
bool Held(const Command& command) {
    if (command.kind == Kind::Peek) return true;
    return command.kind == Kind::Remote && command.request.op == reagentlive::op::Modules;
}

bool Execute(Session& session, const std::string& line, bool inBatch);
bool TakeFlag(std::string& line, std::string_view flag);

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
    // The verb as its row names it, and the names of the flags given: a typed
    // line's tokens that are the row's flags, a JSON call's keys that are.
    void Describe(std::string_view name, const Json* callArgs, std::string_view rest) {
        const Verb* verb = FindVerb(session.verbs, name);
        record.verb = verb ? std::string(verb->name) : std::string(name.substr(0, MaxUsageVerb));
        record.origin = verb ? std::string(verb->origin) : std::string();
        if (!verb) return;
        for (const auto& flag : verb->flags) {
            bool given = false;
            if (callArgs) {
                const std::string key(FlagKey(flag));
                given = callArgs->is_object() && callArgs->contains(key) && !(*callArgs)[key].is_null() &&
                    !((*callArgs)[key].is_boolean() && !(*callArgs)[key].get<bool>());
            } else {
                for (std::string_view tokens = rest; !given;) {
                    const auto token = text::Next(tokens);
                    if (token.empty()) break;
                    given = token == flag.name;
                }
            }
            if (given) record.flags.emplace_back(flag.name);
        }
        // A program's own reads of the table and its quit are not the agent's calls.
        skip = record.verb == "quit" || (record.verb == "help" &&
            std::ranges::find(record.flags, std::string("--json")) != record.flags.end());
    }
    ~UsageScope() {
        session.line = outer;
        if (outer) outer->inner += line.out;
        if (skip || record.verb.empty() || !session.usage.record || session.usage.path.empty()) return;
        record.time = UtcNow();
        record.session = session.usage.session;
        record.caller = session.usage.caller;
        record.out = line.out > line.inner ? line.out - line.inner : 0;
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
            Execute(session, item, true);
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

// One command line; true on quit. Inside a batch the batch's hold covers it.
bool Execute(Session& session, const std::string& original, bool inBatch) {
    Client& client = session.client;
    // --meta covers this line (a batch's covers its items); the previous state
    // returns after it, and so does the row a batch runs under.
    struct LineScope {
        Session& session;
        bool meta;
        const Verb* row;
        ~LineScope() { session.meta = meta; session.current = row; }
    } lineScope{session, session.meta, session.current};
    session.current = nullptr;
    UsageScope usage(session, original, inBatch);
    std::string line = original;
    // A JSON call: the verb's row reads its named arguments; nothing is tokenised or quoted.
    std::optional<Json> call;
    std::string_view rest = line;
    std::string name;
    if (text::Trim(line).starts_with('{')) {
        call = Json::parse(line, nullptr, false);
        if (const auto problem = call->is_discarded() ? std::string("not JSON") : CallProblem(*call); !problem.empty()) {
            usage.Describe("call", nullptr, {});
            JsonError(session, "call", "syntax", problem);
            return false;
        }
        if (CallFlag(*call, "meta")) session.meta = true;
        name = (*call)["verb"].get<std::string>();
    } else {
        if (TakeFlag(line, "--meta")) session.meta = true;
        rest = line;
        name = text::Next(rest);
    }
    const std::string_view jsonCommand = name;
    if (jsonCommand.empty()) return false;
    const Json noArgs;
    const Json& callArgs = call && call->contains("args") ? (*call)["args"] : noArgs;
    usage.Describe(name, call ? &callArgs : nullptr, rest);
    // A host's verb: its row checks the arguments and the result, the host does the rest.
    if (const Verb* host = FindVerb(session.config.verbs, jsonCommand); host && session.config.hostCommand) {
        session.current = host;
        auto args = call ? CallArgs(*host, callArgs) : Tokenise(*host, rest);
        if (!args) {
            JsonError(session, jsonCommand, "syntax", args.error());
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
    // Module addresses (base+0x10, game.exe+0x10) need the list; a name it
    // lacks may be a module loaded since it was read.
    const bool moduleAddress = line.find('+') != std::string::npos || line.find("base") != std::string::npos;
    // A JSON call carries literal refs only (CallArgs refuses $N), so it parses without them.
    const auto parse = [&](std::span<const Module> modules) -> Parsed {
        if (!call) return Parse(session.verbs, line, session.refs, modules);
        const Verb* verb = FindVerb(session.verbs, jsonCommand);
        if (!verb) return Parsed(std::unexpect, "unknown command '" + name + "' (try help)");
        auto args = CallArgs(*verb, callArgs);
        if (!args) return Parsed(std::unexpect, args.error());
        return Build(*verb, *args, {}, modules);
    };
    auto parsed = parse(moduleAddress ? LoadedModules(session) : std::span<const Module>{});
    if (!parsed && moduleAddress && RefreshModules(session)) parsed = parse(LoadedModules(session));
    if (!parsed) {
        JsonError(session, jsonCommand, "syntax", parsed.error());
        return false;
    }
    session.current = parsed->verb;
    if (inBatch && parsed->kind == Kind::Each && parsed->seconds) {
        JsonError(session, jsonCommand, "syntax", "each --seconds is not allowed in a batch");
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
    case Kind::Watch: RunWatch(session, *parsed, session.refs, LoadedModules(session), jsonCommand); return false;
    case Kind::Batch: RunBatch(session, *parsed, jsonCommand); return false;
    case Kind::Sig: RunSig(session, *parsed, jsonCommand); return false;
    case Kind::Disasm: RunDisasm(session, *parsed, jsonCommand); return false;
    case Kind::Xrefs: RunXrefs(session, *parsed, jsonCommand); return false;
    case Kind::Touches: RunTouches(session, *parsed, jsonCommand); return false;
    case Kind::Vtable: RunVtable(session, *parsed, jsonCommand); return false;
    case Kind::Region: RunRegion(session, *parsed, jsonCommand); return false;
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

std::filesystem::path CachePath(uint32_t pid) {
    wchar_t folder[MAX_PATH + 1];
    const DWORD length = GetTempPathW(MAX_PATH + 1, folder);
    if (!length || length > MAX_PATH) return {};
    return std::filesystem::path(folder) / (L"reagentlive-" + std::to_wstring(pid) + L".cache");
}

// One-shot runs against the same agent instance join the cache's usage session.
void LoadSessionCache(Session& session, const std::filesystem::path& path, bool oneShot) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return;
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto data = LoadCache(text);
    if (!data || data->owner != session.client.AgentOwner()) return; // Another agent instance: start fresh.
    if (oneShot && !data->session.empty()) session.usage.session = data->session;
    session.refs = std::move(data->refs);
    session.modules = std::move(data->modules);
    for (auto& [base, layout] : data->layouts) session.layouts[base] = std::move(layout);
    LoadPackCache(session, data->packs);
}

void SaveSessionCache(const Session& session, const std::filesystem::path& path) {
    CacheData data;
    data.owner = session.client.AgentOwner();
    if (!data.owner) return;
    data.session = session.usage.session;
    for (const auto& ref : session.refs) {
        if (!ref.address) break; // Not expressible as a literal ref; $N past it would shift.
        data.refs.push_back(ref);
    }
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

// A console flag (--timing, --meta) at the end of a line: strips it and says so.
bool TakeFlag(std::string& line, std::string_view flag) {
    const auto trimmed = text::Trim(line);
    if (!trimmed.ends_with(flag) || (trimmed.size() > flag.size() && trimmed[trimmed.size() - flag.size() - 1] != ' '))
        return false;
    line = std::string(text::Trim(trimmed.substr(0, trimmed.size() - flag.size())));
    return true;
}

// With no target: help and stats from the table and the log (stats: the
// log's latest session). Any other verb is refused with why there is no
// target (kind startup) and ends the console, so a client starts it again
// once the target runs. Nothing is recorded.
int RunOffline(std::span<const Verb> verbs, std::span<const Origin> origins, const std::string& hash,
               const std::filesystem::path& usagePath, const std::vector<std::string>* lines, std::string_view why) {
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
        return static_cast<bool>(std::getline(std::cin, line));
    };
    while (nextLine()) {
        std::string name;
        Parsed parsed;
        if (text::Trim(line).starts_with('{')) {
            const Json call = Json::parse(line, nullptr, false);
            if (const auto problem = call.is_discarded() ? std::string("not JSON") : CallProblem(call); !problem.empty()) {
                print(ErrorEnvelope("call", "syntax", problem));
                failed = 1;
                continue;
            }
            name = call["verb"].get<std::string>();
            const Verb* verb = FindVerb(verbs, name);
            auto args = verb ? CallArgs(*verb, call.contains("args") ? call["args"] : Json()) :
                std::expected<Args, std::string>(std::unexpect, "unknown command '" + name + "' (try help)");
            parsed = args ? Build(*verb, *args, {}, {}) : Parsed(std::unexpect, args.error());
        } else {
            while (TakeFlag(line, "--timing") || TakeFlag(line, "--meta")) {}
            std::string_view rest = line;
            name = text::Next(rest);
            if (name.empty()) continue;
            parsed = Parse(verbs, line, {});
        }
        if (!parsed) {
            print(ErrorEnvelope(name, "syntax", parsed.error()));
            failed = 1;
            continue;
        }
        switch (parsed->kind) {
        case Kind::Help:
            if (auto help = BuildHelp(verbs, origins, hash, *parsed)) print(ResultEnvelope(name, ToJson(*help)));
            else {
                print(ErrorEnvelope(name, "syntax", help.error()));
                failed = 1;
            }
            break;
        case Kind::Stats:
            print(ResultEnvelope(name, ToJson(BuildStats(usagePath, verbs, parsed->all, std::nullopt))));
            break;
        case Kind::Quit: print(ResultEnvelope(name, ToJson(shape::Quit{}))); return failed;
        default: print(ErrorEnvelope(name, "startup", why)); return 1;
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
    if (argc > 1 && std::wstring_view(argv[1]) == L"--replay") {
        if (argc == 3) return Replay(argv[2]);
        std::puts("Usage: <console> --replay <file>");
        return 1;
    }
    constexpr const char* usage = "Usage: <console> [pid] [-c \"command; command ...\"] [--names <file>] "
        "[--caller tool|script|shell] [--no-usage]";
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
    std::filesystem::path namesFile, usagePath;
    bool namesRequired = false, record = true;
    std::string caller = "shell";
    wchar_t module[MAX_PATH];
    if (const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH); length && length < MAX_PATH) {
        if (!config.namesFile.empty()) namesFile = std::filesystem::path(module).parent_path() / config.namesFile;
        usagePath = std::filesystem::path(module).replace_extension(L".usage.jsonl");
    }
    std::optional<std::vector<std::string>> oneShot;
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg(argv[i]);
        if (arg == L"-c" && i + 1 < argc && !oneShot) {
            const std::wstring_view wide(argv[++i]);
            std::string commands;
            for (const wchar_t c : wide) {
                if (c > 0x7f) return startupError("-c commands must be ASCII");
                commands += static_cast<char>(c);
            }
            oneShot = SplitCommands(commands);
        } else if (arg == L"--names" && i + 1 < argc) {
            namesFile = argv[++i];
            namesRequired = true;
        } else if (arg == L"--caller" && i + 1 < argc) {
            const std::wstring_view value(argv[++i]);
            if (value != L"tool" && value != L"script" && value != L"shell") return startupError("--caller is tool, script or shell");
            caller.clear();
            for (const wchar_t c : value) caller += static_cast<char>(c); // ASCII: one of the three words.
        } else if (arg == L"--no-usage") {
            record = false;
        } else if (!pid && !arg.empty() && std::ranges::all_of(arg, [](wchar_t c) { return c >= L'0' && c <= L'9'; })) {
            pid = static_cast<uint32_t>(_wtoi(argv[i]));
        } else {
            return startupError(usage);
        }
    }
    if (oneShot && oneShot->empty()) return startupError("provide commands via -c");
    const auto verbs = AllVerbs(config);
    const auto origins = AllOrigins();
    const auto tableHash = TableHash(verbs, origins);
    const std::vector<std::string>* lines = oneShot ? &*oneShot : nullptr;
    const auto offline = [&](std::string_view why) {
        if (!oneShot) std::fprintf(stderr, "  no target: help and stats only (%.*s)\n", static_cast<int>(why.size()), why.data());
        return RunOffline(verbs, origins, tableHash, usagePath, lines, why);
    };
    if (oneShot && OfflineOnly(verbs, *oneShot)) return offline("");
    if (!pid) pid = FindProcess(config.exeName);
    if (!pid) return offline(std::string("Target not running. ") + usage);
    if (!namesFile.empty()) {
        std::ifstream file(namesFile);
        if (!file && namesRequired) return startupError("cannot open names file");
        if (file) {
            if (const size_t bad = names.Load(file))
                return startupError("malformed names file at line " + std::to_string(bad));
        }
    }
    auto client = Client::Attach(std::wstring(config.sectionPrefix) + std::to_wstring(pid), config.layout);
    if (!client) return offline(client.error().message);
    client->SetWaitNotice([](std::chrono::milliseconds waited, bool ticking) {
        std::fprintf(stderr, "  waiting %lld s (%s)\n", static_cast<long long>(waited.count() / 1000),
            ticking ? "agent ticking, request running" : "agent not ticking");
        std::fflush(stderr);
    });
    Session session(*client, config);
    session.verbs = verbs;
    session.origins = origins;
    session.tableHash = tableHash;
    session.pack = MakePackState(session);
    if (const auto problem = LoadPackFiles(session); !problem.empty()) return startupError(problem);
    session.names = std::move(names);
    session.lastBeat = {client->Heartbeat(), Clock::now()};
    const auto cachePath = CachePath(pid);
    session.usage = {usagePath, record && !usagePath.empty(),
        std::format("{:%Y%m%dT%H%M%S}-{}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()),
            GetCurrentProcessId()), caller};
    if (session.usage.record) TrimUsageFile(usagePath);
    if (!cachePath.empty()) LoadSessionCache(session, cachePath, oneShot.has_value());
    size_t next = 0;
    std::string line;
    const auto nextLine = [&] {
        if (oneShot) {
            if (next == oneShot->size()) return false;
            line = (*oneShot)[next++];
            return true;
        }
        return static_cast<bool>(std::getline(std::cin, line));
    };
    while (nextLine()) {
        // --timing and --meta in either order; Execute takes --meta.
        bool timing = false, meta = false;
        if (text::Trim(line).starts_with('{')) {
            const Json call = Json::parse(line, nullptr, false);
            timing = !call.is_discarded() && CallFlag(call, "timing");
        } else {
            for (bool more = true; more;) {
                more = false;
                if (TakeFlag(line, "--timing")) timing = more = true;
                if (TakeFlag(line, "--meta")) meta = more = true;
            }
        }
        if (meta) line += " --meta";
        if (timing) session.timing = Timing{&*client, client->Stats(), Clock::now()};
        const bool quit = Execute(session, line, false);
        session.timing.reset();
        if (quit) break;
    }
    if (!cachePath.empty()) SaveSessionCache(session, cachePath);
    return session.failed ? 1 : 0;
}
}
