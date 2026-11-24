// reagentlive CLI console: declarations the console's core files share.
// Internal; frontends use console.h. The Unreal pack's are in packs/ue/cli/console/internal_ue.h,
// and what the core asks of a pack is in pack.h.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/call/build.h"
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/values/json.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/frontends/cli/code/signature.h"
#include "reagentlive/frontends/cli/code/image.h"
#include "reagentlive/frontends/cli/code/unwind.h"
#include "reagentlive/frontends/cli/state/cache.h"
#include "reagentlive/frontends/cli/state/usage.h"
#include "reagentlive/core/client.h"

namespace reagentlive::cli::detail {
using Clock = std::chrono::steady_clock;

// `timing`: client counters and clock at the start of the command.
struct Timing {
    const Client* client;
    ClientStats start;
    Clock::time_point began;
};

// A long verb's progress on stderr, at most one line a second (and the
// last one), as "  progress: <done>/<total> <unit>": a background run's
// status reads the newest. Nothing for work that ends within a second.
class ProgressLine {
public:
    ProgressLine(std::string unit, uint64_t total) : unit_(std::move(unit)), total_(total) {}
    void Done(uint64_t done);
private:
    std::string unit_;
    uint64_t total_;
    Clock::time_point began_ = Clock::now(), last_ = began_;
};

// Heartbeat and time at attach, then at the last status: the tick rate is
// measured over that interval, at least 100 ms.
struct Beat { uint64_t ticks; Clock::time_point at; };

// The usage log (state/usage.h): where it goes, this run's session and caller.
struct Usage {
    std::filesystem::path path;             // <console>.usage.jsonl; usage reads it.
    bool record = false;                    // False with --no-usage.
    std::string session, caller = "shell";
    // The session the cache file hands to the next one-shot run, and its
    // caller: a one-shot run's own, else the one loaded (PickSession).
    std::string handoff, handoffCaller;
    bool internal = false;                  // A verb's own calls (ExecuteLine): no line; their output is the verb's.
    // --context-limit (0: none) and what this session's results put into an
    // agent's context so far: the log's tool lines at start, then each printed
    // envelope of this run when the caller is tool.
    uint64_t contextLimit = 0, contextChars = 0;
    std::vector<UsageLine> pending;         // Batch items' lines, written with the batch's own.
};
// The line being run: its output so far and how it ended. A batch's items
// add their own output to `inner`, so the batch's line keeps its wrapper alone.
struct LineUsage {
    uint64_t out = 0, inner = 0;
    uint64_t digest = 0;                    // The results digests stood in for, at their full size.
    uint64_t outHash = UsageHashStart;      // What it printed, as the call made it (no timing, no context warning).
    bool ok = true;
    std::string refused, refusedKey;        // The first error's kind and the arg it names.
};

// The call being run, as its result cap (max_chars) needs it (output.cpp):
// what to run again, narrowed, when the result is past the cap.
struct LineInput {
    std::optional<Json> call;       // The call as given.
    size_t maxChars = 0;            // The per-result cap in characters (max_chars); 0: none.
    // Safe to run again: no write and no call that may change state. False
    // keeps a result past the cap in a file instead. A pack sets it true
    // for a call it knows is const or pure.
    bool reads = true;
};

// The engine pack's state for one console run (packs/ue/cli/console/internal_ue.h).
struct PackState;

// Everything one console run keeps between commands.
struct Session {
    Client& client;
    const ConsoleConfig& config;
    Names names;                            // Readable names for obfuscated fields; empty without a names file.
    bool failed = false;                    // Any error in one-shot mode makes the exit code 1.
    std::vector<Json>* capture = nullptr;   // batch: records collected instead of printed.
    std::optional<Timing> timing;
    bool meta = false;                      // `meta`: struct values with their member layout.
    std::optional<shape::Chain> chain;      // The pointer chain this command resolved; added to its result.
    std::vector<Verb> verbs;                // The verb table: core, pack and host rows.
    std::vector<Origin> origins;            // The core's, the pack's and the host's: help with `json` titles.
    std::string tableHash;                  // Of the whole table (status, help with `json`).
    ToolPackSet toolPacks = AllToolPacks;   // --packs: the verbs this console runs; others are refused (kind pack).
    const Verb* current = nullptr;          // The row of the line being run; its result type checks what is printed.
    Beat lastBeat{};
    std::vector<Module> modules;            // Exe first.
    bool modulesListed = false;             // Listed by this run, not only loaded from the cache.
    bool sliceSearch = false;               // sig, xrefs: the host refused background search; scan in tick slices.
    SearchText searchText;                  // search text: the last job's, kept in the cache file too.
    // Header layouts by module base (exception directory, sections), for the
    // session and the cache file; lookups keep their sampled entries for this run.
    std::map<uint64_t, image::Layout> layouts;
    std::map<uint64_t, std::unique_ptr<unwind::Table>> unwindTables;
    std::shared_ptr<PackState> pack;        // Made by the pack (pack.h).
    Usage usage;
    LineUsage* line = nullptr;              // The line being run, for the usage log.
    LineInput input;                        // The line being run, for its result cap.
    std::filesystem::path results;          // Whole results of lines that must not run again; empty: none.
    // <console>.labels.jsonl: the derived names labels-export writes when
    // given no file. `hostLabels`: the host's file for names people confirm
    // (ConsoleConfig::labelsFile), where peek with `label` writes; empty: `labels`.
    // disasm with `save` reads both. `executable`: this console's own path,
    // which a save records.
    std::filesystem::path labels, hostLabels, executable;
    std::string watchFile;                  // watch-start with `file`: where watch-read writes every sample.

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
void JsonError(Session& session, std::string_view command, ErrorKind kind, std::string_view message);
// A call refused before it ran: `error` as call/check.h RefusalJson builds it.
void JsonRefusal(Session& session, std::string_view command, Json error);
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
// One call's JSON text run as part of the running verb's own work (its
// records go to session.capture when set): not logged, and its output is
// not counted, since only what the verb prints reaches the caller.
void ExecuteLine(Session& session, std::string_view line);
// The console's source commit (memory.cpp holds the generated header).
std::string ConsoleCommit();
// search (a job: started, then polled up to wait) and search-job.
void RunSearch(Session& session, const Command& command, std::string_view jsonCommand);
void RunWatch(Session& session, const Command& command, std::string_view jsonCommand);
void RunSig(Session& session, const Command& command, std::string_view jsonCommand);
void RunDisasm(Session& session, const Command& command, std::string_view jsonCommand);
void RunXrefs(Session& session, const Command& command, std::string_view jsonCommand);
void RunTouches(Session& session, const Command& command, std::string_view jsonCommand);
void RunVtable(Session& session, const Command& command, std::string_view jsonCommand);
// %TEMP%\reagentlive-<pid>.cache: the cache file of the agent in that process.
std::filesystem::path CachePath(uint32_t pid);
// %TEMP%\reagentlive-<pid>-<owner>.results: whole results kept by the result cap.
std::filesystem::path ResultsFolder(uint32_t pid, uint32_t owner);
// A file to write whole and then rename over `path`: named per process, so two
// consoles replacing the same file never write into one temporary.
std::filesystem::path TemporaryBeside(const std::filesystem::path& path);
// mem-snap and mem-diff: a raw range kept in a file beside the cache, then compared.
void RunMemSnap(Session& session, const Command& command, std::string_view jsonCommand);
void RunMemDiff(Session& session, const Command& command, std::string_view jsonCommand);
void RunUsage(Session& session, const Command& command, std::string_view jsonCommand);
// One call, read once: its JSON, its verb and the console's call flags.
struct InputLine {
    std::string original;       // As given: the usage log's size and hash.
    std::optional<Json> call;
    std::string name;           // The verb as given; empty for a blank line.
    bool meta = false, timing = false, big = false;
    std::optional<size_t> maxChars;
    std::optional<Json> refused;  // A frontend's own refusal: logged and answered, never run.
    std::string problem;        // Not a call: refused under the verb "call".

    const Json& Args() const {
        static const Json none;
        return call && call->contains("args") ? (*call)["args"] : none;
    }
};
InputLine ReadLine(std::string_view line);
// A call checked against its verb's row and built into a command (build.h).
std::expected<Command, Json> BuildLine(std::span<const Verb> verbs, const InputLine& input, std::span<const Module> modules);
// The envelope answering a frontend's own refusal (a call with `refused`).
Json RefusedEnvelope(const InputLine& input);
// One command line; true on quit. Inside a batch the batch's hold covers it.
bool Execute(Session& session, const InputLine& input, bool inBatch);
// batch and run (batch.cpp).
void RunBatch(Session& session, const Command& command, std::string_view jsonCommand);
void RunPlan(Session& session, const Command& command, std::string_view jsonCommand);
// help from the table (offline.cpp).
void RunHelp(Session& session, const Command& command, std::string_view jsonCommand);
// The runs with no target (offline.cpp): help and usage only, or --check.
int RunOffline(std::span<const Verb> verbs, std::span<const Origin> origins, const std::string& hash,
               const std::filesystem::path& usagePath, uint64_t contextLimit, const std::vector<std::string>* lines,
               const std::function<bool(std::string&)>& readStdin, ErrorKind kind, std::string_view why,
               const std::string& session, const std::string& caller);
int RunCheck(std::span<const Verb> verbs, const std::vector<std::string>* lines,
             const std::function<bool(std::string&)>& readStdin);
// Files a run keeps (session_files.cpp): a whole file, empty when unreadable;
// usage lines appended in one write; the log trimmed to its cap; the cache file.
std::string ReadFile(const std::filesystem::path& path);
void AppendUsage(const std::filesystem::path& path, const std::string& text);
void TrimUsageFile(const std::filesystem::path& path);
void LoadSessionCache(Session& session, const std::filesystem::path& path);
void SaveSessionCache(const Session& session, const std::filesystem::path& path);
// The region kind at an address (code, data, private, mapped...), nullopt
// when the agent has no region op or the query fails.
std::optional<std::string> RegionKind(Session& session, uint64_t address);
// The memory an address lies in (read and peek); nullopt as RegionKind.
std::optional<shape::Region> RegionOf(Session& session, uint64_t address);
// Reads through the agent: read-batch items, nullopt where unreadable.
sig::ReadFn AgentRead(Session& session);
// The bytes of [from, to) in blocks of up to `block` bytes, `fill` where
// unreadable: through the agent's image window (ImageCopy, its worker when
// the host allows) when it has one, else MaxRead reads through host ticks.
// `each(address, bytes)` returning false stops early.
std::expected<void, std::string> ImageBlocks(Session& session, uint64_t from, uint64_t to, uint8_t fill, uint64_t block,
                                             const std::function<bool(uint64_t, std::span<const uint8_t>)>& each);
// Anchor searches for sig's matcher over the whole image (worker thread when the host allows it).
sig::SearchFn AgentSearch(Session& session);
// labels-sign and labels-port: signatures on a labels file's names.
void RunLabelsSign(Session& session, const Command& command, std::string_view jsonCommand);
void RunLabelsPort(Session& session, const Command& command, std::string_view jsonCommand);
void RunLabelsCheck(Session& session, const Command& command, std::string_view jsonCommand);
void RunDispScan(Session& session, const Command& command, std::string_view jsonCommand);
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
