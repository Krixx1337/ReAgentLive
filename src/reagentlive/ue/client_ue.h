// reagentlive Unreal pack: typed client calls for find, fields and call.
#pragma once
#include <chrono>
#include <functional>
#include <optional>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/core/client.h"
#include "reagentlive/ue/protocol_ue.h"

namespace reagentlive::ue {
// size: the object's class size from a single `Describe` (0 from other
// lookups, or when the agent could not read it).
struct Object { Ref ref; std::string name, className; uint32_t size = 0; };
struct FindResult {
    std::vector<Object> objects; // Up to MaxFindResults matches from the cursor on.
    uint64_t total;              // All matches in the table.
    uint32_t next;               // Cursor for the next page; 0 when none remain.
};
struct FindOptions {
    std::string_view className;  // The object's class or a parent class; empty: any.
    uint32_t from = 0;           // Cursor from a previous page's next.
};
struct Field {
    uint32_t offset, size;
    uint8_t boolMask;            // Bool properties only; see FieldEntry.
    std::string type, name, owner;
    uint32_t elementSize;
    std::string elementType;
    Ref structType;              // Struct fields and arrays of structs; see FieldEntry.
    std::string structName;
    uint8_t parameterFlags = 0;
    // Maps and sets (see FieldEntry): stride 0 when the layout is unknown.
    uint32_t stride = 0, valueOffset = 0, valueSize = 0;
    std::string valueType;
    Ref valueStructType{};
    std::string valueStructName;
    bool operator==(const Field&) const = default;
};

// A full object table can take many seconds; the default timeout allows it.
// Name substring and/or class; at least one. The first page scans the whole
// table and counts the total; later pages start at their cursor (total 0).
Result<FindResult> Find(Client& client, std::string_view text, FindOptions options = {},
                        std::chrono::milliseconds timeout = std::chrono::minutes(2));
// All pages, in the agent's order (most derived class first).
Result<std::vector<Field>> Fields(Client& client, const Ref& object);
// With a cache: page 0 names the listed type (its object-table index; an
// instance lists its class). When `known` returns a table for that type of
// the same length whose first page matches, the remaining pages are skipped.
struct FieldTable {
    int32_t type;                // Object-table index of the listed class or struct.
    std::vector<Field> fields;
    bool cached;                 // Reused from `known`.
};
using KnownTable = std::function<const std::vector<Field>*(int32_t type)>;
Result<FieldTable> Fields(Client& client, const Ref& object, const KnownTable& known);
Result<std::vector<uint8_t>> ReadObject(Client& client, const Ref& object, uint32_t offset, uint32_t length);
// An address-only ref or a weak ref (address zero, positive serial) is resolved by the agent.
Result<Object> Describe(Client& client, const Ref& ref);
// One entry per ref, in order (nullopt for one that is gone); MaxDescribeMany per request.
Result<std::vector<std::optional<Object>>> DescribeMany(Client& client, std::span<const Ref> refs);
struct FunctionInfo {
    uint64_t address, exec;
    uint32_t flags;
    bool executable;
    std::string owner, name;
    // Declared on a native interface the object implements (protocol_ue.h):
    // where the interface part starts, the stub's vtable slot (bytes, -1
    // unknown) and the implementation that slot holds (0 unknown).
    bool interface = false;
    uint32_t interfaceOffset = 0;
    int64_t slot = -1;
    uint64_t implementation = 0;
};
Result<FunctionInfo> Function(Client& client, const Ref& ref, std::string_view name);
struct FunctionList {
    std::vector<std::string> names;
    uint32_t next;
};
Result<FunctionList> Functions(Client& client, const Ref& ref, std::string_view filter, uint32_t from);
struct Signature {
    uint32_t size = 0;
    std::vector<Field> parameters;
};
Result<Signature> Parameters(Client& client, uint64_t functionAddress);
struct ArrayPreview {
    uint32_t total;
    std::vector<uint8_t> bytes;
};
Result<ArrayPreview> PreviewArray(Client& client, const Ref& ref, const Field& field);
// One page of a container field's elements (whole slots for sets and maps).
struct EntriesPage {
    uint32_t total, next;        // Elements or occupied slots; where the next page starts (0: done).
    std::vector<uint8_t> bytes;  // Whole elements of `stride` bytes.
};
Result<EntriesPage> Entries(Client& client, const Ref& ref, uint32_t offset, ContainerKind kind, uint32_t stride,
                            uint32_t first);
// FName comparison indices to base names (no _N suffix); empty when undecodable.
Result<std::vector<std::string>> Names(Client& client, std::span<const int32_t> indices);
// Returns the parameter block after the call (outputs and return value).
// `flags`: CallRelease drops the function's kept result. `kept`: set when the
// agent kept this block (outputs that own memory; see protocol_ue.h).
Result<std::vector<uint8_t>> Call(Client& client, const Ref& object, std::string_view function,
                                  std::span<const uint8_t> params, uint64_t expectedFunction = 0,
                                  uint32_t flags = 0, bool* kept = nullptr);
struct CensusRow {
    Object type;                 // The class object (ref, its name, its class's name unset).
    uint32_t count;
    std::vector<Ref> samples;    // Up to MaxCensusSamples instances.
};
struct CensusResult {
    std::vector<CensusRow> rows; // Most instances first.
    uint64_t instances;          // Sum over rows.
};
// All pages: live instances per exact class, optionally under class `className`.
Result<CensusResult> Census(Client& client, std::string_view className,
                            std::chrono::milliseconds timeout = std::chrono::minutes(2));
// A function's bytecode: confirmed anchored entries (protocol_ue.h), all pages.
struct ScriptResult {
    uint32_t size;                    // Bytecode bytes.
    std::vector<ScriptEntry> entries;
};
Result<ScriptResult> Script(Client& client, const Ref& function);
// Functions whose bytecode names `nameIndex` (EX_NameConst), all pages.
struct RefsResult {
    std::vector<RefsEntry> rows;
    bool capped;
};
Result<RefsResult> Refs(Client& client, int32_t nameIndex, std::chrono::milliseconds timeout = std::chrono::minutes(2));
// Every native function's exec entry, all pages (one table scan in the agent).
// A row whose function is gone has exec 0.
struct ExecFunction { uint64_t exec; std::string owner, name; };
Result<std::vector<ExecFunction>> ExecEntries(Client& client, std::chrono::milliseconds timeout = std::chrono::minutes(2));
struct NameMatch { int32_t index; std::string name; };
struct NameFindResult {
    std::vector<NameMatch> names;
    uint32_t next;               // Cursor for the next page; 0 when the pool is done.
};
// One page of names in the engine's name pool containing `text`.
Result<NameFindResult> NameFind(Client& client, std::string_view text, uint32_t from = 0,
                                std::chrono::milliseconds timeout = std::chrono::minutes(1));
struct EachQuery {
    std::string_view className;
    std::string_view function;   // Call mode: "[Owner:]Function"; empty = field mode.
    uint32_t offset = 0, size = 0; // Field: its offset and size. Call: the parameter block size.
    bool reuse = false;          // Re-read the instance list of the previous query of this class.
};
struct EachRow { Ref object; std::vector<uint8_t> value; };
struct EachResult {
    std::vector<EachRow> rows;
    bool capped;                 // More instances than MaxEachInstances.
};
// All pages; the values were all read in one agent tick.
Result<EachResult> Each(Client& client, const EachQuery& query,
                        std::chrono::milliseconds timeout = std::chrono::minutes(2));
// A raw address as a ref the agent identifies.
inline Ref AddressRef(uint64_t address) { return {static_cast<uintptr_t>(address), -1, 0}; }
}
