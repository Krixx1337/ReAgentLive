// reagentlive CLI console: declarations the console_*.cpp files share. Internal;
// frontends use console.h.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "reagentlive/frontends/cli/results/census.h"
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/values/json_output.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/frontends/cli/state/native_names.h"
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/frontends/cli/code/signature.h"
#include "reagentlive/frontends/cli/code/image.h"
#include "reagentlive/frontends/cli/code/unwind.h"
#include "reagentlive/core/client.h"
#include "reagentlive/ue/client_ue.h"

namespace reagentlive::cli::detail {
constexpr size_t MaxDumpBytes = ue::MaxObjectBytes;
using Clock = std::chrono::steady_clock;

struct Session;
std::vector<ue::Field> SortedByOffset(std::vector<ue::Field> fields);
Result<std::vector<ue::Field>> ObjectFields(Session& session, const Ref& object);

// --timing: client counters and clock at the start of the command.
struct Timing {
    const Client* client;
    ClientStats start;
    Clock::time_point began;
};

// Heartbeat and time at attach, then at the last status: the tick rate is
// measured over that interval, at least 100 ms.
struct Beat { uint64_t ticks; Clock::time_point at; };

// Struct members by struct ref for the session; a struct's layout is fixed in
// a running process. Failed lookups are remembered so they are not retried.
class StructCache {
public:
    explicit StructCache(Session& session) : session_(session) {}
    const std::vector<ue::Field>* operator()(const Ref& type);
private:
    static constexpr size_t MaxTypes = 256;
    struct Entry {
        Ref type;
        std::optional<std::vector<ue::Field>> fields;
    };
    Session& session_;
    std::vector<Entry> entries_;
};

struct Snapshot {
    Ref object;
    std::vector<ue::Field> fields;
    std::vector<uint8_t> bytes;
};

// Class and struct objects by exact name, for the session (they live as long as the process).
// Entries loaded from the cache file are checked with one describe on first
// use; a ref the agent no longer accepts is dropped and the table scanned again.
struct ClassRef { std::string name; Ref ref; bool verified; };

// Everything one console run keeps between commands.
struct Session {
    Client& client;
    const ConsoleConfig& config;
    std::vector<Ref> refs;                  // $1..$N.
    StructCache structs;
    std::optional<Snapshot> baseline;       // The last snap.
    Names names;                            // Readable names for obfuscated fields; empty without a names file.
    NativeNames native;                     // Unreflected members from the native file (label).
    std::filesystem::path nativeFile;       // <console>.native.txt beside the console.
    std::optional<uint32_t> exeTimestamp;   // The main image's PE timestamp, read once.
    bool failed = false;                    // Any error in one-shot mode makes the exit code 1.
    std::vector<Json>* capture = nullptr;   // batch: records collected instead of printed.
    std::optional<Timing> timing;
    bool meta = false;                      // --meta: struct values with their member layout.
    std::optional<Json> chain;              // The pointer chain this command resolved; added to its result.
    Beat lastBeat{};
    // Field tables by type (object-table index), for the session and the cache
    // file. Every lookup still fetches page 0, which names the type and must
    // match the cached table; only the remaining pages are skipped.
    std::unordered_map<int32_t, std::vector<ue::Field>> tables;
    std::vector<ClassRef> classRefs;
    std::vector<Module> modules;            // Exe first.
    bool modulesListed = false;             // Listed by this run, not only loaded from the cache.
    std::optional<CensusBaseline> census;   // The last census: census --changed's baseline, census --from's rows.
    std::optional<CensusBaseline> censusBefore; // The census before it, for census --changed --from.
    bool sliceSearch = false;               // sig, xrefs: the host refused background search; scan in tick slices.
    // Header layouts by module base (exception directory, sections), for the
    // session and the cache file; lookups keep their sampled entries for this run.
    std::map<uint64_t, image::Layout> layouts;
    std::map<uint64_t, std::unique_ptr<unwind::Table>> unwindTables;

    Session(Client& client, const ConsoleConfig& config) : client(client), config(config), structs(*this) {}
    ValueStyle Style() const { return {meta, &names}; }
};

inline const std::vector<ue::Field>* StructCache::operator()(const Ref& type) {
    auto found = std::ranges::find(entries_, type, &Entry::type);
    if (found == entries_.end()) {
        if (entries_.size() >= MaxTypes || !session_.client.Supports(ue::op::Fields)) return nullptr;
        auto fields = ObjectFields(session_, type);
        entries_.push_back({type, fields ? std::optional(SortedByOffset(std::move(*fields))) : std::nullopt});
        found = std::prev(entries_.end());
    }
    return found->fields ? &*found->fields : nullptr;
}

// Loaded modules, exe first: from the cache file, else listed on first use.
// RefreshModules lists them again, at most once per console run (a module
// loaded since the cache was written); false when it already did.
std::span<const Module> LoadedModules(Session& session);
bool RefreshModules(Session& session);

void PrintJson(Session& session, Json record);
void JsonResult(Session& session, std::string_view command, Json result);
void JsonError(Session& session, std::string_view command, std::string_view kind, std::string_view message);
void CommandError(Session& session, std::string_view jsonCommand, const Error& error);
void PrintStatus(Session& session, std::string_view jsonCommand);
// False when the host does not tick on the engine's thread: same_tick then
// means one host tick, not one moment of the game.
inline bool TickAccurate(const Session& session) { return session.client.AgentHostThread() == HostThread::Engine; }
Json DecodeValues(const std::vector<uint8_t>& bytes, std::string_view type);
void RunPin(Session& session, const Command& command, std::string_view jsonCommand);
void RunTrial(Session& session, const Command& command, std::string_view jsonCommand);
void RunPeek(Session& session, const Command& command, std::string_view jsonCommand);
void RunWatch(Session& session, const Command& command, std::span<const Ref> refs, std::span<const Module> modules,
              std::string_view jsonCommand);
size_t DumpEnd(std::span<const ue::Field> fields);
std::span<const uint8_t> FieldBytes(std::span<const uint8_t> bytes, const ue::Field& field);
std::string FieldValue(const ue::Field& field, std::span<const uint8_t> bytes, StructCache& structs);
void PrintFields(Session& session, const Ref& object, bool instance, std::string_view jsonCommand);
void RememberRef(std::vector<Ref>& refs, const Ref& object);
Ref PointerRef(std::string_view type, std::span<const uint8_t> bytes);
void PrintDump(Session& session, const Ref& object, std::vector<Ref>& refs, StructCache& structs, const Command& command,
               const std::optional<Snapshot>& baseline);
std::optional<Snapshot> LoadSnapshot(const std::string& path);
void Snap(Session& session, const Ref& object, const std::string& path, std::optional<Snapshot>& baseline,
          std::string_view jsonCommand);
void Record(Session& session, const Ref& object, const Command& command, StructCache& structs, std::string_view jsonCommand);
int Replay(const std::filesystem::path& path);
Json CallJson(const Request& request, std::span<const uint8_t> block, const char* mode, bool kept);
Json ModuleJson(const Module& module);
void PrintFunction(Session& session, const Ref& ref, std::string_view name, std::string_view jsonCommand);
void PrintParameters(Session& session, const ue::FunctionInfo& function, const ue::Signature& signature, const Ref& object);
void RunTypedCall(Session& session, const Command& command, uint64_t functionAddress,
                  const ue::Signature& signature, StructCache& structs, std::string_view jsonCommand);
Result<Ref> ResolveClass(Session& session, const std::string& name);
void RunEach(Session& session, const Command& command, std::vector<Ref>& refs, StructCache& structs,
             std::string_view jsonCommand);
void RunEntries(Session& session, const Command& command, std::string_view jsonCommand);
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
void RunExportLabels(Session& session, const Command& command, std::string_view jsonCommand);
// The module's PE timestamp (ties labels to one build); nullopt when its header is unreadable.
std::optional<uint32_t> ImageTimestamp(Session& session, const Module& module);
void RunGet(Session& session, const Command& command, StructCache& structs, std::vector<Ref>& refs,
            std::string_view jsonCommand);
// The main image's PE timestamp (0 while unreadable), read once per run.
uint32_t ExeTimestamp(Session& session);
// Native-file members of the classes declaring `reflected`, checked against
// them and (for an instance) the class size: shown ones and refused ones.
struct NativeShown { std::vector<NativeCheck> shown, refused; };
NativeShown NativeMembers(Session& session, const Ref& object, std::span<const ue::Field> reflected, bool instance);
Json NativeRefusedJson(std::span<const NativeCheck> refused);
void RunLabel(Session& session, const Command& command, std::string_view jsonCommand);
}
