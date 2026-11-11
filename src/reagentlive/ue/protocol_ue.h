// reagentlive Unreal pack: ops and wire entries on top of the core protocol.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string_view>
#include "reagentlive/core/protocol.h"

// Find: text = name substring (may be empty when a class is given); data =
// optional class name, NUL-terminated, matching the object's class or any parent
// class (case-insensitive); args[1] = cursor, the object-table slot to start
// scanning from. detail packs the total matches and the next cursor (see
// FindDetail); only the first page (cursor 0) scans the whole table and
// counts the total, later pages report 0 and stop once full. Call: object ref (see SetRef), text =
// "Function" or "Class:Function", length and data = the parameter block.
// Fields: object ref (a class or struct lists its own fields, anything else
// its class's), length = index of the first field of this page.
namespace reagentlive::ue {
namespace op {
constexpr uint32_t Find = reagentlive::op::PackFirst, Fields = Find + 1, Call = Find + 2;
constexpr uint32_t ReadObject = Find + 3;
constexpr uint32_t Describe = Find + 4;
constexpr uint32_t PreviewArray = Find + 5;
constexpr uint32_t Function = Find + 6;
constexpr uint32_t Names = Find + 7;
constexpr uint32_t Functions = Find + 8;
constexpr uint32_t Parameters = Find + 9;
constexpr uint32_t Census = Find + 10;
constexpr uint32_t NameFind = Find + 11;
constexpr uint32_t Each = Find + 12;
constexpr uint32_t Script = Find + 13;
constexpr uint32_t Refs = Find + 14;
constexpr uint32_t DescribeMany = Find + 15;
constexpr uint32_t ExecEntries = Find + 16;
constexpr uint32_t Entries = Find + 17;
}

// UObject identity: address, object-table index and serial. index < 0 means
// address only; the agent identifies it. Serial 0 = not assigned yet.
struct Ref {
    uintptr_t address;
    int32_t index, serial;
    bool operator==(const Ref&) const = default;
};
// On the wire a ref is Request::address plus args[0] (index low, serial high).
inline void SetRef(Request& request, const Ref& ref) {
    request.address = ref.address;
    request.args[0] = static_cast<uint32_t>(ref.index) | static_cast<uint64_t>(static_cast<uint32_t>(ref.serial)) << 32;
}
inline Ref GetRef(const Request& request) {
    return {static_cast<uintptr_t>(request.address), static_cast<int32_t>(static_cast<uint32_t>(request.args[0])),
            static_cast<int32_t>(static_cast<uint32_t>(request.args[0] >> 32))};
}
constexpr size_t MaxParams = 1024;
// Guards for pins and watch ranges on an object: its object-table index
// (int32) and class pointer, adjacent in UE5's UObject. A freed or replaced
// object changes them. The agent checks the layout against its SDK.
constexpr uint32_t ObjectIdentityOffset = 0xC, ObjectIdentityLength = 12;
constexpr size_t MaxFindResults = 24;
constexpr uint32_t MaxClassName = 64;
// Find's detail: total matches in the low 32 bits, the cursor for the next page
// in the high 32 bits (0 when this page holds the last matches).
inline uint64_t FindDetail(uint32_t total, uint32_t next) { return total | static_cast<uint64_t>(next) << 32; }
inline uint32_t FindTotal(uint64_t detail) { return static_cast<uint32_t>(detail); }
inline uint32_t FindNext(uint64_t detail) { return static_cast<uint32_t>(detail >> 32); }
// Reflected fields per page; the client pages until it has them all.
constexpr uint32_t MaxFieldStart = 16384;
constexpr uint32_t MaxObjectBytes = 0x10000;
// Names: data holds int32 FName comparison indices (length = 4 x count); the
// response packs one NUL-terminated name per index, empty when undecodable,
// and detail counts the names returned (fewer when MaxRead fills).
constexpr uint32_t MaxNames = 128;
constexpr uint32_t MaxArrayPreviewElements = 8;
// Elements up to 512 bytes; larger strides preview fewer elements so the
// prefix fits one response.
constexpr uint32_t MaxArrayElementBytes = 512;

struct ArrayHeader {
    uint64_t data;
    int32_t count, capacity;
};

// Elements a preview returns for an array of `count` elements of `stride` bytes.
inline uint32_t ArrayPreviewCount(uint64_t count, uint32_t stride) {
    if (!stride || stride > MaxArrayElementBytes) return 0;
    uint64_t shown = count < MaxArrayPreviewElements ? count : MaxArrayPreviewElements;
    if (shown * stride > MaxRead) shown = MaxRead / stride;
    return static_cast<uint32_t>(shown);
}

// Zero means no readable preview. An empty array can still have allocated storage.
inline uint32_t ArrayPreviewBytes(const ArrayHeader& header, uint32_t stride) {
    if (header.count <= 0 || header.capacity < header.count || !header.data) return 0;
    const auto bytes = ArrayPreviewCount(static_cast<uint32_t>(header.count), stride) * stride;
    return header.data <= UINT64_MAX - bytes ? bytes : 0;
}

struct FindEntry {
    Ref object;
    char name[64];
    char className[48];
};
// DescribeMany: data = 1..MaxDescribeMany refs (address-only, full, or a weak
// slot and serial, as for Describe). The response holds one FindEntry per ref
// in order; a ref that is gone or not an object gets a zeroed entry.
// detail = entries found.
constexpr uint32_t MaxDescribeMany = 32;
static_assert(MaxDescribeMany * sizeof(FindEntry) <= MaxRead);
struct FunctionEntry {
    uint64_t address, exec;
    uint32_t flags, executable;
    char owner[64], name[64];
    // A function declared on a native interface the object's class implements
    // (see InterfacesOffset): interface = 1, the interface part's offset in the
    // object, the vtable slot its stub calls (bytes; -1 unknown) and the
    // address the object's interface vtable holds there (0 unknown).
    uint32_t interface, interfaceOffset;
    int64_t slot;
    uint64_t implementation;
};

// Native interface functions. A class's implemented interfaces are a TArray
// at UClass+InterfacesOffset (UE5, read live) of {UClass*, int32 PointerOffset,
// bool} entries. A function declared on an interface is only on the interface
// class. Its exec stub calls a slot of the vtable at the start of the object it
// is given, without moving `this` to the interface part, so through
// ProcessEvent on the implementing object it would run another function: Call
// refuses it and Function reports where the implementation is instead.
// The offset is not in the SDK, so every class's array is checked before use:
// at most MaxInterfaces entries, each an interface class (a UInterface
// subclass) whose part lies inside the implementing class's size. One bad
// entry makes the array implausible and Function refuses (another engine
// layout) instead of trusting any of it.
constexpr uint32_t InterfacesOffset = 0x1E0, InterfaceEntrySize = 16, MaxInterfaces = 64;
constexpr uint32_t MaxStubScan = 160;
// Byte offset of the vtable slot a stub calls: the first `mov rax, [reg]`
// directly followed by `call [rax + disp8|disp32]`; -1 when none. Pure; tested.
inline int64_t StubVtableSlot(const uint8_t* code, size_t size) {
    for (size_t i = 0; i + 6 <= size; ++i) {
        const uint8_t rex = code[i], opcode = code[i + 1], modrm = code[i + 2];
        const bool load = (rex == 0x48 || rex == 0x49) && opcode == 0x8B && modrm < 0x08 && modrm != 0x04 &&
            modrm != 0x05; // mov rax, [reg] without SIB or RIP addressing.
        if (!load || code[i + 3] != 0xFF) continue;
        if (code[i + 4] == 0x50) return code[i + 5] < 0x80 ? code[i + 5] : -1; // disp8 is signed.
        if (code[i + 4] == 0x90 && i + 9 <= size) {
            int32_t displacement = 0;
            memcpy(&displacement, code + i + 5, sizeof(displacement));
            return displacement >= 0 ? displacement : -1;
        }
    }
    return -1;
}
struct FunctionNameEntry {
    char owner[64], name[64];
};
constexpr size_t MaxFunctionsPerPage = MaxRead / sizeof(FunctionNameEntry);
enum ParameterFlag : uint8_t {
    Param = 1, Output = 2, Return = 4, Reference = 8, Plain = 16, Scalar = 32
};
static_assert(sizeof(FunctionEntry) <= MaxRead);
// One reflected property. size = element size x array dimension. Bool
// properties: offset is the byte holding the bit and boolMask selects it
// (0xff for a full-byte bool); 0 for every other type.
struct FieldEntry {
    uint32_t offset, size;
    uint8_t boolMask, reserved[3]; // Parameters uses reserved[0] for ParameterFlag bits.
    char name[56];
    char type[32];
    char owner[40]; // Class or struct that declares it.
    uint32_t elementSize;
    char elementType[32];
    // Single struct fields and arrays of structs: the struct object (a ref
    // for Fields) and its name. Zero address otherwise.
    Ref structType;
    char structName[48];
    // Maps and sets: the key (a set's element) is elementType, elementSize and
    // structType above; a map's value is below. stride is the element's whole
    // slot (key, value and hash links), 0 when the layout is unknown or failed
    // its check (ContainerLayoutValid).
    uint32_t stride, valueOffset, valueSize, reserved2;
    char valueType[32];
    Ref valueStructType;
    char valueStructName[48];
};
constexpr size_t MaxFieldsPerPage = MaxRead / sizeof(FieldEntry);

// UE5 keeps a map's or set's element layout in the property, after its inner
// property pointers: FScriptMapLayout (ValueOffset, then FScriptSetLayout) at
// MapLayoutOffset, FScriptSetLayout (HashNextIdOffset, HashIndexOffset, Size)
// at SetLayoutOffset. The SDK does not describe it, so these are pack
// constants and every layout read through them is checked before use.
constexpr uint32_t MapLayoutOffset = 0x80, SetLayoutOffset = 0x78;
struct ContainerLayout { int32_t valueOffset, hashNextIdOffset, hashIndexOffset, stride; };
// The key at 0, a map's value after it, then two int32 hash links, all inside
// a stride of at most MaxArrayElementBytes. valueSize 0 = a set. Pure; tested.
inline bool ContainerLayoutValid(const ContainerLayout& layout, uint32_t keySize, uint32_t valueSize) {
    if (!keySize || layout.stride <= 0 || static_cast<uint32_t>(layout.stride) > MaxArrayElementBytes) return false;
    if (valueSize && (layout.valueOffset < 0 || static_cast<uint32_t>(layout.valueOffset) < keySize)) return false;
    const int64_t used = valueSize ? int64_t{layout.valueOffset} + valueSize : int64_t{keySize};
    return layout.hashNextIdOffset >= used && layout.hashIndexOffset == int64_t{layout.hashNextIdOffset} + 4 &&
        layout.stride >= int64_t{layout.hashIndexOffset} + 4;
}
// detail: Find = total matches, Fields = total fields (high half: the listed
// type's object-table index; Parameters: the block size); a parameter-size
// refusal carries the expected size.
static_assert(MaxFindResults * sizeof(FindEntry) <= MaxRead);
static_assert(MaxFieldsPerPage * sizeof(FieldEntry) <= MaxRead);
static_assert(MaxParams <= MaxRead);

// Census: data = optional class name as for Find; text empty; args[1] = index
// of the first class of this page. One scan counts live instances (class
// default and archetype objects excluded) per exact class, most instances
// first; later pages of the same filter come from the agent's cached result.
// detail = FindDetail(class count, next page index or 0).
constexpr uint32_t MaxCensusSamples = 3;
struct CensusEntry {
    Ref type;                       // The class object.
    uint32_t count, samples;        // Instances; refs filled in sample[].
    Ref sample[MaxCensusSamples];
    char name[64];
};
constexpr size_t MaxCensusPerPage = MaxRead / sizeof(CensusEntry);

// NameFind: text = case-insensitive substring of a name in the engine's name
// pool; args[1] = cursor (the comparison index to resume at, 0 = start). The
// response packs int32 index + NUL-terminated text per match; detail = next
// cursor, 0 once the pool is done. Covers names no object carries, such as
// gameplay tags and row names.
constexpr uint32_t MaxNameFindResults = 64;
constexpr uint32_t MaxNameFindText = 200;  // Longer names are cut.

// Each: data = class name as for Find (required); one value per live instance
// of the class or a subclass, all read in one tick. Field mode: text empty,
// args[0] = offset | size << 32. Call mode: text = "[Owner:]Function", args[0] =
// the function's parameter block size; only const or pure functions whose
// parameters are all plain outputs or the return value run. args[1] = index of
// the first row of this page; later pages come from the cached rows. flags
// EachReuse re-reads the instance list cached by the previous Each of the same
// class instead of scanning the object table. Rows: Ref then the value padded
// to 8 bytes. detail = FindDetail(rows, next row index or 0); reason "capped"
// when more than MaxEachInstances matched.
constexpr uint32_t EachReuse = 1;
constexpr uint32_t MaxEachValue = 64;
constexpr uint32_t MaxEachInstances = 1024;
constexpr uint32_t EachStride(uint32_t valueSize) { return static_cast<uint32_t>(sizeof(Ref)) + ((valueSize + 7) & ~7u); }
inline uint32_t EachOffset(uint64_t args0) { return static_cast<uint32_t>(args0); }
inline uint32_t EachSize(uint64_t args0) { return static_cast<uint32_t>(args0 >> 32); }
inline uint64_t EachField(uint32_t offset, uint32_t size) { return offset | static_cast<uint64_t>(size) << 32; }
// Unreal flag bits the Each guard needs (EFunctionFlags, EPropertyFlags).
namespace flags {
constexpr uint32_t FunctionBlueprintPure = 0x10000000, FunctionConst = 0x40000000, FunctionNative = 0x400;
constexpr uint64_t Parm = 0x80, OutParm = 0x100, ReturnParm = 0x400, ConstParm = 0x2, ZeroConstructor = 0x200,
    PlainOldData = 0x40000000, NoDestructor = 0x1000000000;
}
// Read-only by contract: a const or pure function. Pure; shared with tests.
inline bool EachFunctionAllowed(uint32_t functionFlags) {
    return (functionFlags & (flags::FunctionBlueprintPure | flags::FunctionConst)) != 0;
}

// Plain data: nothing to destroy (NoDestructor) and zeroed memory is a valid
// value (ZeroConstructor), so a zeroed block is a valid call and a result
// needs no keeping. A flag the property lacks still holds for a struct whose
// members all are plain (a gameplay tag: its property has NoDestructor only,
// its one FName member has both). `flagsOf(p)` gives the property flags;
// `membersOf(p, visit)` calls visit(member) for each member of p's struct
// and returns the count, or -1 when p is no struct or its members are
// unreadable. No list of struct names. Pure; shared with tests.
constexpr unsigned MaxPlainDepth = 4;
template<class Property, class FlagsOf, class MembersOf>
bool PlainProperty(const Property& property, const FlagsOf& flagsOf, const MembersOf& membersOf, unsigned depth = 0) {
    const uint64_t propertyFlags = flagsOf(property);
    if ((propertyFlags & flags::NoDestructor) && (propertyFlags & flags::ZeroConstructor)) return true;
    if (depth >= MaxPlainDepth) return false;
    bool plain = true;
    const auto count = membersOf(property, [&](const Property& member) {
        plain = plain && PlainProperty(member, flagsOf, membersOf, depth + 1);
    });
    return count > 0 && plain;
}

// Owned call results. A call whose return value or out parameter owns
// something (not plain data, or with a destructor: weak handles, strings,
// arrays) keeps its block per function. The next call of that function starts
// from the kept outputs, so the engine releases the old values when it
// assigns the new ones; inputs always come from the request. Call flags
// CallRelease run once more with the request's inputs (usually empty) and drop
// the entry. A call that would need a new entry while the table is full is
// refused: an entry still owns references, so it is never evicted. Response
// detail CallKept marks a kept block.
constexpr uint32_t CallRelease = 1;
constexpr uint64_t CallKept = 1;
constexpr uint32_t MaxKeptResults = 8;
// Slots carried from a kept block: the return value and outputs that are not
// const references (UE marks const& inputs OutParm too).
inline bool CarriedParameter(uint64_t propertyFlags) {
    return (propertyFlags & flags::ReturnParm) ||
        ((propertyFlags & flags::OutParm) && !(propertyFlags & flags::ConstParm));
}
// A carried slot that is not plain data (PlainProperty), which makes the block worth keeping.
inline bool OwningParameter(uint64_t propertyFlags, bool plain) { return CarriedParameter(propertyFlags) && !plain; }
// Each calls only functions whose parameters are all carried outputs (the
// return value, const or not, and out parameters that are not const refs)
// and plain data, so a zeroed block is a valid call and nothing leaks.
inline bool EachParameterAllowed(uint64_t propertyFlags, bool plain) { return CarriedParameter(propertyFlags) && plain; }
struct ParameterSlot { uint32_t offset, size; uint64_t flags; };
// Copies the carried slots of `kept` into `block` (both `size` bytes); slots
// outside the block are skipped. Pure; shared with tests.
inline void CarryOutputs(const ParameterSlot* slots, size_t count, const uint8_t* kept, uint8_t* block, uint32_t size) {
    for (size_t i = 0; i < count; ++i) {
        const auto& slot = slots[i];
        if (CarriedParameter(slot.flags) && slot.offset <= size && slot.size <= size - slot.offset)
            memcpy(block + slot.offset, kept + slot.offset, slot.size);
    }
}
// Fixed table of kept blocks keyed by function address. Constant-initialised,
// so it works as an agent global in a mapped image.
struct KeptResult {
    uintptr_t function;
    uint32_t size;
    uint8_t block[MaxParams]; // Copied with memcpy only; the call itself runs on an aligned copy.
};
struct KeptResults {
    KeptResult entries[MaxKeptResults];
    KeptResult* Find(uintptr_t function) {
        for (auto& entry : entries) if (function && entry.function == function) return &entry;
        return nullptr;
    }
    // A free entry for `function`, or nullptr when the table is full.
    KeptResult* Add(uintptr_t function, uint32_t size) {
        for (auto& entry : entries) if (!entry.function) {
            entry.function = function;
            entry.size = size;
            return &entry;
        }
        return nullptr;
    }
    static void Drop(KeptResult& entry) { entry.function = 0; entry.size = 0; }
};

// Blueprint bytecode (UStruct::Script, a TArray<uint8>). An anchored scan,
// not a disassembler: a value counts only right after its opcode byte. The
// SDK has no EExprToken; these are UE5's public Script.h values, each seen
// live before a value of its kind.
namespace expr {
constexpr uint8_t LocalVariable = 0x00, InstanceVariable = 0x01, FinalFunction = 0x1C, NameConst = 0x21,
    LocalFinalFunction = 0x46, LocalOutVariable = 0x48, CallMath = 0x68;
}
constexpr uint32_t ScriptOffset = 0x60;        // UStruct::Script in UE5 (read live).
constexpr uint32_t ScriptNameOperand = 12;     // FScriptName: comparison index, display index, number.
constexpr uint32_t MaxScriptBytes = 1u << 20;  // Larger scripts are refused as implausible.
enum class ScriptKind : uint8_t { Name = 1, Function = 2, Property = 3 };
// A candidate the agent still has to confirm: a decodable name, a live
// function, or a pointer whose field class name ends in "Property".
struct ScriptCandidate {
    uint32_t offset;   // Of the opcode byte.
    ScriptKind kind;
    uint8_t opcode;
    uint64_t value;    // Name: comparison index. Others: the pointer.
};
// Calls visit(candidate) for every anchored operand. Pointers must look like
// user-mode addresses, 8-aligned. Pure; shared with tests.
template<class Visit> void ScanScript(const uint8_t* bytes, size_t size, Visit&& visit) {
    for (size_t i = 0; i < size; ++i) {
        const uint8_t opcode = bytes[i];
        if (opcode == expr::NameConst && size - i > ScriptNameOperand) {
            int32_t index = 0;
            memcpy(&index, bytes + i + 1, sizeof(index));
            if (index > 0) visit(ScriptCandidate{static_cast<uint32_t>(i), ScriptKind::Name, opcode, static_cast<uint64_t>(index)});
            continue;
        }
        const bool function = opcode == expr::FinalFunction || opcode == expr::LocalFinalFunction || opcode == expr::CallMath;
        const bool property = opcode == expr::LocalVariable || opcode == expr::InstanceVariable ||
            opcode == expr::LocalOutVariable;
        if ((!function && !property) || size - i <= sizeof(uint64_t)) continue;
        uint64_t pointer = 0;
        memcpy(&pointer, bytes + i + 1, sizeof(pointer));
        if (pointer < 0x10000 || pointer >= 0x800000000000 || (pointer & 7)) continue;
        visit(ScriptCandidate{static_cast<uint32_t>(i), function ? ScriptKind::Function : ScriptKind::Property, opcode, pointer});
    }
}
// Script: object ref = a UFunction (or any UStruct with bytecode); args[1] =
// first entry of this page. Entries are confirmed and deduplicated by value
// (first offset, occurrence count). detail = script size | next entry << 32
// (0 when this page holds the last).
struct ScriptEntry {
    uint32_t offset, count;
    ScriptKind kind;
    uint8_t opcode, reserved[2];
    int32_t name;      // Name: comparison index.
    Ref object;        // Function: its ref.
    uint64_t pointer;  // Function or property address.
    char text[96];     // Name text; Function "Owner:Name"; Property "Type Name".
};
constexpr size_t MaxScriptEntriesPerPage = MaxRead / sizeof(ScriptEntry);
constexpr uint32_t MaxScriptEntries = 1024;
inline uint64_t ScriptDetail(uint32_t size, uint32_t next) { return size | static_cast<uint64_t>(next) << 32; }
// Refs: args[0] = an FName comparison index; args[1] = first row of this page.
// Scans every function's bytecode for EX_NameConst with that index (budgeted
// per tick, like Census; later pages come from the cached result).
// detail = FindDetail(rows, next row or 0).
struct RefsEntry {
    Ref function;
    uint32_t offset, count; // First anchored hit and hits in that function.
    char owner[64], name[64];
};
constexpr size_t MaxRefsPerPage = MaxRead / sizeof(RefsEntry);
constexpr uint32_t MaxRefs = 512;

// Entries: one page of a reflected container's elements, header and elements
// read in the same tick. Object ref = a live instance; args[1] = the field's
// offset | first index (arrays) or slot (sets, maps) << 32; length = the
// element stride; flags = ContainerKind. Arrays return elements from the
// index; sets and maps return whole allocated slots from the slot, skipping
// free ones. As many as fit MaxRead. detail = FindDetail(elements or occupied
// slots, next index or slot, 0 when done).
enum class ContainerKind : uint32_t { Array = 1, Set = 2, Map = 3 };
// TSet and TMap storage (UE5): a TSparseArray (element data, slot count and
// capacity, allocation bits in a TBitArray with 4 inline words, free list),
// then the hash.
struct SparseHeader {
    uint64_t data;
    int32_t num, max;
    uint32_t inlineBits[4];
    uint64_t secondaryBits;
    int32_t numBits, maxBits;
    int32_t firstFree, numFree;
};
static_assert(sizeof(SparseHeader) == 0x38);
constexpr uint32_t MaxSparseSlots = 1u << 20;
inline uint64_t EntriesArgs(uint32_t offset, uint32_t first) { return offset | static_cast<uint64_t>(first) << 32; }
inline uint32_t EntriesOffset(uint64_t args1) { return static_cast<uint32_t>(args1); }
inline uint32_t EntriesFirst(uint64_t args1) { return static_cast<uint32_t>(args1 >> 32); }
// One bit per slot; inline bits unless the array outgrew them. Pure; tested.
inline bool SparseHeaderValid(const SparseHeader& header) {
    if (header.num < 0 || header.max < header.num || static_cast<uint32_t>(header.num) > MaxSparseSlots) return false;
    if (header.numFree < 0 || header.numFree > header.num || header.numBits != header.num) return false;
    if (header.maxBits < header.numBits || (header.num && !header.data)) return false;
    return header.secondaryBits || header.numBits <= 128;
}
inline bool SlotAllocated(const uint32_t* bits, uint32_t slot) { return (bits[slot / 32] >> (slot % 32)) & 1; }
// Allocated slots from `first`, at most `limit`, through visit(slot); returns
// the slot to resume at, 0 when none is left. Pure; tested.
template<class Visit> uint32_t WalkSlots(const uint32_t* bits, uint32_t num, uint32_t first, uint32_t limit, Visit&& visit) {
    uint32_t taken = 0;
    for (uint32_t slot = first; slot < num; ++slot) {
        if (!SlotAllocated(bits, slot)) continue;
        if (taken == limit) return slot;
        visit(slot);
        ++taken;
    }
    return 0;
}

// ExecEntries: args[1] = first row of this page. One object-table scan
// (budgeted per tick, like Census) lists every native function (a native
// exec thunk of its own; Blueprint functions share the script interpreter's)
// with its exec entry, for labelling code in a static-analysis tool. Later
// pages come from the cached scan. detail = FindDetail(rows, next row or 0).
struct ExecEntry {
    uint64_t exec;
    char owner[64], name[64];
};
constexpr size_t MaxExecPerPage = MaxRead / sizeof(ExecEntry);
constexpr uint32_t MaxExecEntries = 1u << 20;

// The pack's part of the layout check (added to reagentlive::LayoutCore by the host).
// New ops need no change here: the section header lists the ops an agent handles.
// Every struct the pack sends is in the sum (see reagentlive::LayoutCore).
constexpr uint32_t LayoutPart = static_cast<uint32_t>(sizeof(FindEntry) * 5 + sizeof(FieldEntry) * 11 +
    sizeof(FunctionEntry) * 3 + sizeof(Ref) * 53 + sizeof(ArrayHeader) * 59 + sizeof(FunctionNameEntry) * 61 +
    sizeof(CensusEntry) * 67 + sizeof(ScriptEntry) * 71 + sizeof(RefsEntry) * 73 + sizeof(SparseHeader) * 79 +
    sizeof(ExecEntry) * 83 +
    op::Fields * 101 + MaxArrayElementBytes * 7 + MaxClassName * 13);

namespace detail {
// A class name in data: NUL-terminated, non-empty, under MaxClassName.
inline bool HasClass(const Request& request) {
    return request.length > 1 && request.length <= MaxClassName && request.data[0] &&
        memchr(request.data, 0, request.length) != nullptr;
}
inline bool FunctionText(std::string_view name) {
    const auto colon = name.find(':');
    return !name.empty() && colon != 0 && (colon == std::string_view::npos || colon + 1 < name.size());
}
}

// Bounds of the pack's ops; other ops are bad requests. Pure; shared with tests.
inline Status Check(const Request& request) {
    switch (request.op) {
    case op::Find: {
        if (!Terminated(request.text) || request.args[1] > INT32_MAX || request.length > MaxClassName) return Status::BadRequest;
        const bool hasClass = detail::HasClass(request);
        if (request.length && !hasClass) return Status::BadRequest;
        return request.text[0] || hasClass ? Status::Ok : Status::BadRequest;
    }
    case op::Census:
        if (!Terminated(request.text) || request.text[0] || request.args[1] > INT32_MAX ||
            (request.length && !detail::HasClass(request))) return Status::BadRequest;
        return Status::Ok;
    case op::NameFind:
        return Terminated(request.text) && request.text[0] && !request.length && request.args[1] <= INT32_MAX
            ? Status::Ok : Status::BadRequest;
    case op::Each: {
        if (!Terminated(request.text) || !detail::HasClass(request) || request.args[1] > MaxEachInstances ||
            (request.flags & ~EachReuse)) return Status::BadRequest;
        const uint32_t offset = EachOffset(request.args[0]), size = EachSize(request.args[0]);
        if (request.text[0])
            return detail::FunctionText(request.text) && !offset && size && size <= MaxEachValue ? Status::Ok : Status::BadRequest;
        return size && size <= MaxEachValue && offset <= MaxObjectBytes - size ? Status::Ok : Status::BadRequest;
    }
    case op::Call:
        return request.address && Terminated(request.text) && request.text[0] &&
            request.length <= MaxParams && !(request.flags & ~CallRelease) ? Status::Ok : Status::BadRequest;
    case op::Script:
        return request.address && !request.length && request.args[1] <= MaxScriptEntries ? Status::Ok : Status::BadRequest;
    case op::Entries: {
        const uint32_t offset = EntriesOffset(request.args[1]);
        const auto kind = static_cast<ContainerKind>(request.flags);
        const bool known = kind == ContainerKind::Array || kind == ContainerKind::Set || kind == ContainerKind::Map;
        const uint32_t header = kind == ContainerKind::Array ? sizeof(ArrayHeader) : sizeof(SparseHeader);
        return request.address && known && request.length && request.length <= MaxArrayElementBytes &&
            offset <= MaxObjectBytes - header && request.address <= UINT64_MAX - offset - header ? Status::Ok : Status::BadRequest;
    }
    case op::ExecEntries:
        return !request.address && !request.length && request.args[1] <= MaxExecEntries ? Status::Ok : Status::BadRequest;
    case op::Refs:
        return request.args[0] && request.args[0] <= INT32_MAX && !request.length && request.args[1] <= MaxRefs
            ? Status::Ok : Status::BadRequest;
    case op::Function: {
        if (!request.address || request.length || !Terminated(request.text)) return Status::BadRequest;
        return detail::FunctionText(request.text) ? Status::Ok : Status::BadRequest;
    }
    case op::Names:
        return request.length && request.length % 4 == 0 && request.length <= MaxNames * 4 ? Status::Ok : Status::BadRequest;
    case op::Fields:
    case op::Parameters:
        return request.address && request.length <= MaxFieldStart ? Status::Ok : Status::BadRequest;
    case op::Functions:
        return request.address && request.length <= MaxFieldStart && Terminated(request.text)
            ? Status::Ok : Status::BadRequest;
    case op::ReadObject: {
        const uint64_t offset = request.args[1];
        return request.address && GetRef(request).index >= 0 && request.length &&
            request.length <= MaxRead && offset <= MaxObjectBytes - request.length &&
            request.address <= UINT64_MAX - offset - request.length ? Status::Ok : Status::BadRequest;
    }
    case op::DescribeMany:
        return request.length && request.length % sizeof(Ref) == 0 && request.length / sizeof(Ref) <= MaxDescribeMany
            ? Status::Ok : Status::BadRequest;
    case op::Describe: {
        const Ref ref = GetRef(request);
        // Address-only lookup, full ref, or a weak slot with a positive serial.
        return ref.address || (ref.index >= 0 && ref.serial > 0) ? Status::Ok : Status::BadRequest;
    }
    case op::PreviewArray: {
        const auto offset = request.args[1];
        return request.address && request.length && request.length <= MaxArrayElementBytes &&
            offset <= MaxObjectBytes - sizeof(ArrayHeader) &&
            request.address <= UINT64_MAX - offset - sizeof(ArrayHeader) ? Status::Ok : Status::BadRequest;
    }
    }
    return Status::BadRequest;
}

// Case-insensitive ASCII substring.
inline bool NameMatches(std::string_view name, std::string_view query) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    if (query.empty() || query.size() > name.size()) return false;
    for (size_t start = 0; start + query.size() <= name.size(); ++start) {
        size_t i = 0;
        while (i < query.size() && lower(name[start + i]) == lower(query[i])) ++i;
        if (i == query.size()) return true;
    }
    return false;
}

// "Class:Function" → owner "Class"; "Function" → empty owner (first match on the chain).
struct FunctionName { std::string_view owner, name; };
inline FunctionName SplitFunction(std::string_view text) {
    const size_t colon = text.find(':');
    if (colon == std::string_view::npos) return {{}, text};
    return {text.substr(0, colon), text.substr(colon + 1)};
}
}
