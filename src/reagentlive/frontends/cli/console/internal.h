// reagentlive CLI console: declarations the console's core files share.
// Internal; frontends use console.h. The Unreal pack's are in packs/ue/cli/console/internal_ue.h,
// and what the core asks of a pack is in pack.h.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/values/json.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/frontends/cli/code/signature.h"
#include "reagentlive/frontends/cli/code/image.h"
#include "reagentlive/frontends/cli/code/unwind.h"
#include "reagentlive/core/client.h"

namespace reagentlive::cli::detail {
using Clock = std::chrono::steady_clock;

// --timing: client counters and clock at the start of the command.
struct Timing {
    const Client* client;
    ClientStats start;
    Clock::time_point began;
};

// Heartbeat and time at attach, then at the last status: the tick rate is
// measured over that interval, at least 100 ms.
struct Beat { uint64_t ticks; Clock::time_point at; };

// The usage log (state/usage.h): where it goes, this run's session and caller.
struct Usage {
    std::filesystem::path path;             // <console>.usage.jsonl; stats reads it.
    bool record = false;                    // False with --no-usage.
    std::string session, caller = "shell";
};
// The line being run: its output so far and how it ended. A batch's items
// add their own output to `inner`, so the batch's line keeps its wrapper alone.
struct LineUsage {
    uint64_t out = 0, inner = 0;
    uint64_t digest = 0;                    // The results digests stood in for, at their full size.
    bool ok = true;
    std::string refused;
};

// The line being run, as its output budget needs it (output.cpp): what to
// run again, narrowed, when the result is past the budget.
struct LineInput {
    std::string text;               // A typed line without its line flags.
    std::optional<Json> call;       // Or a JSON call.
    std::string flags;              // The line flags a rerun keeps (--meta, --budget N), typed.
    std::vector<Ref> refs;          // $1..$N when the line began: a rerun names them literally.
    size_t budget = 0;              // Characters; 0: none.
    // Safe to run again: no write and no call that may change state. False
    // keeps a result past the budget in a file instead. A pack sets it true
    // for a call it knows is const or pure.
    bool reads = true;
};

// The engine pack's state for one console run (packs/ue/cli/console/internal_ue.h).
struct PackState;

// Everything one console run keeps between commands.
struct Session {
    Client& client;
    const ConsoleConfig& config;
    std::vector<Ref> refs;                  // $1..$N.
    Names names;                            // Readable names for obfuscated fields; empty without a names file.
    bool failed = false;                    // Any error in one-shot mode makes the exit code 1.
    std::vector<Json>* capture = nullptr;   // batch: records collected instead of printed.
    std::optional<Timing> timing;
    bool meta = false;                      // --meta: struct values with their member layout.
    std::optional<shape::Chain> chain;      // The pointer chain this command resolved; added to its result.
    std::vector<Verb> verbs;                // The verb table: core, pack and host rows.
    std::vector<Origin> origins;            // The core's, the pack's and the host's: help --json titles.
    std::string tableHash;                  // Of the whole table (status, help --json).
    const Verb* current = nullptr;          // The row of the line being run; its result type checks what is printed.
    Beat lastBeat{};
    std::vector<Module> modules;            // Exe first.
    bool modulesListed = false;             // Listed by this run, not only loaded from the cache.
    bool sliceSearch = false;               // sig, xrefs: the host refused background search; scan in tick slices.
    // Header layouts by module base (exception directory, sections), for the
    // session and the cache file; lookups keep their sampled entries for this run.
    std::map<uint64_t, image::Layout> layouts;
    std::map<uint64_t, std::unique_ptr<unwind::Table>> unwindTables;
    std::shared_ptr<PackState> pack;        // Made by the pack (pack.h).
    Usage usage;
    LineUsage* line = nullptr;              // The line being run, for the usage log.
    LineInput input;                        // The line being run, for its budget.
    std::filesystem::path results;          // Whole results of lines that must not run again; empty: none.
    // <console>.labels.jsonl: the derived names export-labels writes when
    // given no file. `hostLabels`: the host's file for names people confirm
    // (ConsoleConfig::labelsFile), where peek --label writes; empty: `labels`.
    // disasm --save reads both.
    std::filesystem::path labels, hostLabels;

    Session(Client& client, const ConsoleConfig& config) : client(client), config(config) {}
};

// Loaded modules, exe first: from the cache file, else listed on first use.
// RefreshModules lists them again, at most once per console run (a module
// loaded since the cache was written); false when it already did.
std::span<const Module> LoadedModules(Session& session);
bool RefreshModules(Session& session);

void PrintJson(Session& session, Json record);
// A result object, checked against the running verb's row: one that does not
// match its declared type is an internal error, never printed as a result.
void JsonResult(Session& session, std::string_view command, Json result);
// A typed result (results/schema.h); a resolved pointer chain goes into its `chain`.
template<class T> void Emit(Session& session, std::string_view command, T result) {
    if constexpr (requires { result.chain; }) {
        if (session.chain) result.chain = std::move(*session.chain);
    }
    session.chain.reset();
    JsonResult(session, command, ToJson(result));
}
void JsonError(Session& session, std::string_view command, std::string_view kind, std::string_view message);
void CommandError(Session& session, std::string_view jsonCommand, const Error& error);
void PrintStatus(Session& session, std::string_view jsonCommand);
// False when the host does not tick on the engine's thread: same_tick then
// means one host tick, not one moment of the game.
inline bool TickAccurate(const Session& session) { return session.client.AgentHostThread() == HostThread::Engine; }
std::vector<Json> DecodeValues(const std::vector<uint8_t>& bytes, std::string_view type);
void RunPin(Session& session, const Command& command, std::string_view jsonCommand);
void RunTrial(Session& session, const Command& command, std::string_view jsonCommand);
void RunWrites(Session& session, const Command& command, std::string_view jsonCommand);
void RunPeek(Session& session, const Command& command, std::string_view jsonCommand);
void RunWatch(Session& session, const Command& command, std::span<const Ref> refs, std::span<const Module> modules,
              std::string_view jsonCommand);
void RunSig(Session& session, const Command& command, std::string_view jsonCommand);
void RunDisasm(Session& session, const Command& command, std::string_view jsonCommand);
void RunXrefs(Session& session, const Command& command, std::string_view jsonCommand);
void RunTouches(Session& session, const Command& command, std::string_view jsonCommand);
void RunVtable(Session& session, const Command& command, std::string_view jsonCommand);
// %TEMP%\reagentlive-<pid>.cache: the cache file of the agent in that process.
std::filesystem::path CachePath(uint32_t pid);
// %TEMP%\reagentlive-<pid>-<owner>.results: whole results kept by the output budget.
std::filesystem::path ResultsFolder(uint32_t pid, uint32_t owner);
// mem-snap and mem-diff: a raw range kept in a file beside the cache, then compared.
void RunMemSnap(Session& session, const Command& command, std::string_view jsonCommand);
void RunMemDiff(Session& session, const Command& command, std::string_view jsonCommand);
void RunStats(Session& session, const Command& command, std::string_view jsonCommand);
// The region kind at an address (code, data, private, mapped...), nullopt
// when the agent has no region op or the query fails.
std::optional<std::string> RegionKind(Session& session, uint64_t address);
// The memory an address lies in (read and peek); nullopt as RegionKind.
std::optional<shape::Region> RegionOf(Session& session, uint64_t address);
// Reads through the agent: read-batch items, nullopt where unreadable.
sig::ReadFn AgentRead(Session& session);
// Anchor searches for sig's matcher over the whole image (worker thread when the host allows it).
sig::SearchFn AgentSearch(Session& session);
// labels-sign and labels-port: signatures on a labels file's names.
void RunLabelsSign(Session& session, const Command& command, std::string_view jsonCommand);
void RunLabelsPort(Session& session, const Command& command, std::string_view jsonCommand);
// The exception-directory entry holding an address (any loaded module),
// nullopt without one; an error when the table cannot be read.
std::expected<std::optional<unwind::Function>, std::string> FunctionAt(Session& session, uint64_t address);
// The module's headers, read once and cached.
std::expected<const image::Layout*, std::string> LayoutOf(Session& session, const Module& module);
// Inside an executable section of a loaded module (headers read as needed).
bool InCode(Session& session, uint64_t address);
// The module's PE timestamp (ties labels to one build); nullopt when its header is unreadable.
std::optional<uint32_t> ImageTimestamp(Session& session, const Module& module);

// One page of a scan on the agent's worker thread when the host allows it,
// else in host-tick slices; a refusal for background is remembered.
template<class Query, class Scan>
Result<SearchResult> ScanPage(Session& session, Query query, Scan scan) {
    const auto timeout = std::max(session.client.Timeout(), std::chrono::milliseconds(180000));
    if (!session.sliceSearch) {
        query.background = true;
        auto found = scan(query, timeout);
        if (found || found.error().kind != Error::Kind::Agent || found.error().status != Status::Refused ||
            found.error().reason.find("background") == std::string::npos) return found;
        session.sliceSearch = true;
    }
    query.background = false;
    return scan(query, timeout);
}

// Every hit in [from, to) up to `cap`; true in the pair when the cap cut it.
template<class Query, class Scan>
Result<std::pair<std::vector<uint64_t>, bool>> ScanAll(Session& session, Query query, size_t cap, Scan scan) {
    std::vector<uint64_t> hits;
    while (true) {
        query.maxHits = static_cast<uint32_t>(std::min<size_t>(MaxSearchHits, cap + 1 - hits.size()));
        auto page = ScanPage(session, query, scan);
        if (!page) return std::unexpected(page.error());
        hits.insert(hits.end(), page->hits.begin(), page->hits.end());
        if (hits.size() > cap) {
            hits.resize(cap);
            return std::pair(std::move(hits), true);
        }
        if (!page->resume || page->resume <= query.from) return std::pair(std::move(hits), false);
        query.from = page->resume;
    }
}
}
