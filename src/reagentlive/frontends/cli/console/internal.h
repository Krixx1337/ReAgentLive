// reagentlive CLI console: declarations the console's core files share.
// Internal; frontends use console.h. The Unreal pack's are in ue/internal_ue.h,
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

// The engine pack's state for one console run (ue/internal_ue.h).
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
void RunPeek(Session& session, const Command& command, std::string_view jsonCommand);
void RunWatch(Session& session, const Command& command, std::span<const Ref> refs, std::span<const Module> modules,
              std::string_view jsonCommand);
void RunSig(Session& session, const Command& command, std::string_view jsonCommand);
void RunDisasm(Session& session, const Command& command, std::string_view jsonCommand);
void RunXrefs(Session& session, const Command& command, std::string_view jsonCommand);
void RunTouches(Session& session, const Command& command, std::string_view jsonCommand);
void RunVtable(Session& session, const Command& command, std::string_view jsonCommand);
void RunRegion(Session& session, const Command& command, std::string_view jsonCommand);
// The region kind at an address (code, data, private, mapped...), nullopt
// when the agent has no region op or the query fails.
std::optional<std::string> RegionKind(Session& session, uint64_t address);
// Reads through the agent: read-batch items, nullopt where unreadable.
sig::ReadFn AgentRead(Session& session);
// The exception-directory entry holding an address (any loaded module),
// nullopt without one; an error when the table cannot be read.
std::expected<std::optional<unwind::Function>, std::string> FunctionAt(Session& session, uint64_t address);
// The module's headers, read once and cached.
std::expected<const image::Layout*, std::string> LayoutOf(Session& session, const Module& module);
// Inside an executable section of a loaded module (headers read as needed).
bool InCode(Session& session, uint64_t address);
// The module's PE timestamp (ties labels to one build); nullopt when its header is unreadable.
std::optional<uint32_t> ImageTimestamp(Session& session, const Module& module);
}
