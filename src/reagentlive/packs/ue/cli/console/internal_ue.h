// reagentlive CLI console, Unreal pack: declarations the packs/ue/cli/console/ files
// share, and the pack's state for one console run. Internal.
#pragma once
#include <deque>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "reagentlive/frontends/cli/console/internal.h"
#include "reagentlive/frontends/cli/console/pack.h"
#include "reagentlive/packs/ue/cli/results/census.h"
#include "reagentlive/packs/ue/cli/results/result_json_ue.h"
#include "reagentlive/packs/ue/cli/results/verbs_ue.h"
#include "reagentlive/packs/ue/cli/state/cache_ue.h"
#include "reagentlive/packs/ue/cli/state/native_names.h"
#include "reagentlive/packs/ue/cli/values/json_output.h"
#include "reagentlive/packs/ue/client_ue.h"

namespace reagentlive::cli::detail {
constexpr size_t MaxDumpBytes = ue::MaxObjectBytes;

// A field table, shared with the session's cache: holding one costs no copy.
using FieldList = std::shared_ptr<const std::vector<ue::Field>>;

std::vector<ue::Field> SortedByOffset(std::vector<ue::Field> fields);
// The object's field table (its class's, or its own for a class or struct);
// page 0 is always fetched, and a matching cached table is reused, not copied.
Result<FieldList> ObjectTable(Session& session, const Ref& object);
// The same, as a copy the caller may change.
Result<std::vector<ue::Field>> ObjectFields(Session& session, const Ref& object);

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
    // A deque: callers keep a returned list while decoding members, and a
    // lookup they make meanwhile adds an entry; a vector would move them all.
    std::deque<Entry> entries_;
};

// Class and struct objects by exact name, for the session (they live as long as the process).
// Entries loaded from the cache file are checked with one describe on first
// use; a ref the agent no longer accepts is dropped and the table scanned again.
struct ClassRef { std::string name; Ref ref; bool verified; };

// What the Unreal pack keeps for one console run (Session::pack).
struct PackState {
    StructCache structs;
    std::optional<Snapshot> baseline;       // The last snap.
    NativeNames native;                     // Unreflected members from the native file (label).
    std::filesystem::path nativeFile;       // <console>.native.txt beside the console.
    std::optional<uint32_t> exeTimestamp;   // The main image's PE timestamp, read once.
    // Field tables by type (object-table index), for the session and the cache
    // file. Every lookup still fetches page 0, which names the type and must
    // match the cached table; only the remaining pages are skipped.
    std::unordered_map<int32_t, FieldList> tables;
    std::vector<ClassRef> classRefs;
    std::optional<CensusBaseline> census;   // The last census: census --diff's baseline, census --start's rows.
    std::optional<CensusBaseline> censusBefore; // The census before it, for census --changed --start.
    std::vector<ClassRef> defaultObjects;  // Default__ objects by exact name, for the session (DefaultObject).
    std::unordered_map<std::string, int32_t> nameIndices; // Pooled names by lowercase text, for the session (NameIndex).

    explicit PackState(Session& session) : structs(session) {}
};

inline PackState& Ue(Session& session) { return *session.pack; }
inline const PackState& Ue(const Session& session) { return *session.pack; }
inline ValueStyle Style(const Session& session) { return {session.meta, &session.names}; }

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

size_t DumpEnd(std::span<const ue::Field> fields);
std::span<const uint8_t> FieldBytes(std::span<const uint8_t> bytes, const ue::Field& field);
// get --types: the layout; --match keeps names or readable labels containing it, --own the class's own.
void PrintTypes(Session& session, const Ref& object, const Command& command, std::string_view jsonCommand);
void RememberRef(std::vector<Ref>& refs, const Ref& object);
Ref PointerRef(std::string_view type, std::span<const uint8_t> bytes);
void PrintDump(Session& session, const Ref& object, std::vector<Ref>& refs, StructCache& structs, const Command& command,
               const std::optional<Snapshot>& baseline);
std::optional<Snapshot> LoadSnapshot(const std::string& path);
Result<std::vector<uint8_t>> ReadObjectBytes(Client& client, const Ref& object, size_t end);
void Snap(Session& session, const Ref& object, const std::string& path, std::optional<Snapshot>& baseline,
          std::string_view jsonCommand);
shape::Call MakeCall(const Request& request, std::span<const uint8_t> block, const char* mode, bool kept);
// call --dry: the block that would be sent, with the function's native entry and whether it is const or pure.
shape::Call MakeDryCall(const Request& request, std::span<const uint8_t> block, const ue::FunctionInfo& function);
void PrintFunction(Session& session, const Ref& ref, std::string_view name, std::string_view jsonCommand);
// A typed call (or with --dry its block only); the request's flags go to the agent (CallPure).
void RunTypedCall(Session& session, const Command& command, const ue::FunctionInfo& function,
                  const ue::Signature& signature, StructCache& structs, std::span<const Ref> refs,
                  std::string_view jsonCommand);
Result<Ref> ResolveClass(Session& session, const std::string& name);
Result<Ref> ResolveObjectOrClass(Session& session, const std::string& name);
void RunEach(Session& session, const Command& command, std::vector<Ref>& refs, StructCache& structs,
             std::string_view jsonCommand);
void RunEntries(Session& session, const Command& command, std::string_view jsonCommand);
void RunEnum(Session& session, const Command& command, std::string_view jsonCommand);
void RunGameThread(Session& session, const Command& command, std::string_view jsonCommand);
// rows: a data table's RowMap, each row decoded with its RowStruct.
void RunRows(Session& session, const Command& command, StructCache& structs, std::string_view jsonCommand);
void RunExportLabels(Session& session, const Command& command, std::string_view jsonCommand);
// layout-export and layout-check (command.target "export" or "check").
void RunLayout(Session& session, const Command& command, std::string_view jsonCommand);
void RunGet(Session& session, const Command& command, StructCache& structs, std::vector<Ref>& refs,
            std::string_view jsonCommand);
// The main image's PE timestamp (0 while unreadable), read once per run.
uint32_t ExeTimestamp(Session& session);
// Native-file members of the classes declaring `reflected`, checked against
// them and (for an instance) the class size: shown ones and refused ones.
struct NativeShown { std::vector<NativeCheck> shown, refused; };
NativeShown NativeMembers(Session& session, const Ref& object, std::span<const ue::Field> reflected, bool instance);
void RunLabel(Session& session, const Command& command, std::string_view jsonCommand);
// A pooled name's comparison index by its text (FNames compare without case), for the session.
Result<int32_t> NameIndex(Session& session, std::string_view text);
// A default object (Default__...) by its exact name, kept for the session.
Result<Ref> DefaultObject(Session& session, const std::string& name);

// FText display strings for get, each and dump: every TextProperty value
// collected, then each converted by KismetTextLibrary.Conv_TextToString on
// the game thread (an engine call, read-only by contract). When the host
// cannot call (no call op, not on the game thread) every value keeps its
// bytes and says why.
class TextBatch {
public:
    void Add(const ue::Field& field, std::span<const uint8_t> bytes);
    void Resolve(Session& session);
    // A TextProperty value: its string ({"text", "via"} with `meta`), or `raw`
    // with "text_error". Others: `raw`.
    Json Value(const ue::Field& field, std::span<const uint8_t> bytes, Json raw, bool meta) const;
private:
    std::vector<std::vector<uint8_t>> texts_;
    std::vector<std::optional<std::string>> strings_;
    std::vector<std::string> errors_;
    std::string why_; // Why none was converted.
};

void RunTrace(Session& session, const Command& command, std::string_view jsonCommand);
// calls-start, calls-read, calls-stop (the agent's call recorder).
void RunCalls(Session& session, const Command& command, std::string_view jsonCommand);
}
