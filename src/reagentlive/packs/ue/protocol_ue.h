// reagentlive Unreal pack: ops and wire entries on top of the core protocol.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string_view>
#include "reagentlive/core/protocol.h"
#include "reagentlive/packs/ue/engine_layout.h"

// Find: text = name substring (may be empty when a class is given); data =
// optional class name, NUL-terminated, matching the object's class or any parent
// class (case-insensitive); args[1] = cursor, the object-table slot to start
// scanning from. detail packs the total matches and the next cursor (see
// FindDetail); only the first page (cursor 0) scans the whole table and
// counts the total, later pages report 0 and stop once full. flags =
// FindExact: the whole name equals text (case-sensitive) and the scan stops
// at the first match, counting nothing (total 0; next = the slot after it),
// so a lookup by name costs the slots up to the match, not the table.
// FindLive: live instances only; class default and archetype objects (the
// templates census also skips) are left out.
// Call: object ref (see SetRef), text =
// "Function" or "Class:Function", length and data = the parameter block.
// Fields: object ref (a class or struct lists its own fields, anything else
// its class's), length = index of the first field of this page.
namespace reagentlive::ue {
namespace op {
constexpr uint32_t Find = reagentlive::op::PackSlot(0), Fields = Find + 1, Call = Find + 2;  // Slot 0.
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
constexpr uint32_t CallsStart = Find + 18;
constexpr uint32_t Enum = Find + 21;
constexpr uint32_t GameThread = Find + 22;
constexpr uint32_t Owner = Find + 23, Route = Find + 24;
static_assert(Route < reagentlive::op::PackSlot(1), "the Unreal pack's ops stay in its slot");
}

// A UObject ref: core's Ref, with the object-table index and its serial.
using reagentlive::Ref;
using reagentlive::SetRef;
using reagentlive::GetRef;
using reagentlive::AddressRef;
constexpr size_t MaxParams = 1024;
// Guards for pins and watch ranges on an object: its object-table index
// (int32) and class pointer, adjacent in UE5's UObject. A freed or replaced
// object changes them (layout::EngineLayout objectIndex, objectClass).
using layout::ObjectIdentityLength;
constexpr size_t MaxFindResults = 24;
constexpr uint32_t MaxClassName = MaxText; // Class names in a request: as long as its text.
// The object filter find, each and census take: data = class name, NUL, then
// optionally the outer's name text and NUL, then, with an outer object, its
// slot and serial (two int32); address = the outer object (0: none). The
// agent refuses an outer object that is gone, as it refuses any stale ref.
// An object passes when its class matches and an outer up its chain (at most
// MaxOuterSteps) is that object or has a name containing the text, any case.
constexpr uint32_t MaxOuterText = 64, MaxOuterSteps = 16;
// Find's detail: total matches in the low 32 bits, the cursor for the next page
// in the high 32 bits (0 when this page holds the last matches).
constexpr uint32_t FindExact = 1, FindLive = 2;
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
    Ref outer;          // The object's outer (a function's class, a subobject's owner); zero for a package.
};
// Two names equal as far as a wire field of `capacity` bytes holds them: a
// longer name arrives cut to capacity - 1, so a full name is compared with a
// wire one by its first capacity - 1 characters. Pure; tested.
inline bool SameWireName(std::string_view a, std::string_view b, size_t capacity) {
    const size_t kept = capacity - 1;
    return a.substr(0, kept) == b.substr(0, kept);
}
constexpr size_t FindNameBytes = sizeof(FindEntry::name), FindClassBytes = sizeof(FindEntry::className);
// DescribeMany: data = 1..MaxDescribeMany refs (address-only, full, or a weak
// slot and serial, as for Describe). The response holds one FindEntry per ref
// in order; a ref that is gone or not an object gets a zeroed entry.
// detail = entries found.
constexpr uint32_t MaxDescribeMany = 28;
static_assert(MaxDescribeMany * sizeof(FindEntry) <= MaxRead);
struct FunctionEntry {
    uint64_t address, exec;
    uint32_t flags, executable;
    char owner[64], name[64];
    // A function declared on a native interface the object's class implements
    // (see the layout's interfaces): interface = 1, the interface part's offset in the
    // object, the vtable slot its stub calls (bytes; -1 unknown) and the
    // address the object's interface vtable holds there (0 unknown).
    uint32_t interface, interfaceOffset;
    int64_t slot;
    uint64_t implementation;
};

// Native interface functions. A class's implemented interfaces are a TArray
// at UClass + the layout's interfaces (UE5, read live) of {UClass*, int32 PointerOffset,
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
using layout::InterfaceEntrySize;
constexpr uint32_t MaxInterfaces = 64;
// The stub is scanned to its own end from its module's exception directory,
// never into the next function: a stub's call can sit past 160 bytes.
constexpr uint32_t MaxStubScan = 512;
// Length of the function that starts exactly at `rva`, from sorted
// RUNTIME_FUNCTION triples (begin, end, unwind); 0 when no entry starts
// there. Pure; tested.
inline uint32_t FunctionSpan(const uint32_t* entries, size_t count, uint32_t rva) {
    size_t low = 0, high = count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        const uint32_t begin = entries[middle * 3], end = entries[middle * 3 + 1];
        if (rva < begin) high = middle;
        else if (rva >= end) low = middle + 1;
        else return begin == rva && end > begin ? end - begin : 0;
    }
    return 0;
}
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
    // 1: declared on a native interface the class implements; owner is the
    // interface. Calling it through ProcessEvent on the object is refused (see
    // the layout's interfaces); func reports its implementation.
    uint32_t interface, reserved;
};
constexpr size_t MaxFunctionsPerPage = MaxRead / sizeof(FunctionNameEntry);
enum ParameterFlag : uint8_t {
    Param = 1, Output = 2, Return = 4, Reference = 8, Plain = 16, Scalar = 32,
    // A const parameter. A const reference is an input whose value travels in
    // the block although the engine also marks it OutParm.
    Const = 64
};
// A parameter the caller supplies: not an output, or a const reference that
// is not the return value. Pure; shared with tests.
inline bool InputParameter(uint8_t flags) {
    if (flags & Return) return false;
    return !(flags & Output) || ((flags & Const) && (flags & Reference));
}
static_assert(sizeof(FunctionEntry) <= MaxRead);
// Fields and Parameters pages. A page is a FieldsPageHeader, `count`
// FieldRecords, `refs` struct refs (each struct on the page once; a record
// names one by number, 0: none), then a string table of NUL-terminated
// strings, each stored once (a type or an owner repeats on every row), that a
// record names by byte offset (0: the empty string). Names travel whole:
// a field whose strings no longer fit ends the page, and the next page starts
// at it.
//
// One reflected property's numbers. size = element size x array dimension.
// Bool properties: offset is the byte holding the bit and boolMask selects
// it (0xff for a full-byte bool); 0 for every other type. Single struct
// fields and arrays of structs: structType and structName. Maps and sets: the
// key (a set's element) is elementType, elementSize and structType; a map's
// value is the value* members; stride is the element's whole slot (key,
// value and hash links), 0 when the layout is unknown or failed its check
// (ContainerLayoutValid). subtype: the class an object-like property points
// to, or the enum an enum or byte property holds; for an array or set its
// element's; for a map "Key|Value" (either side may be empty; '|' is never
// in an object name); empty when there is none or it could not be read.
struct FieldsPageHeader {
    uint16_t count, refs;
    uint32_t strings;                       // Bytes of the string table.
};
struct FieldRecord {
    uint32_t offset, size, elementSize, stride, valueOffset, valueSize;
    uint8_t boolMask, flags;                // flags: Parameters' ParameterFlag bits.
    uint8_t structType, valueStructType;    // 1-based into the page's refs; 0: none.
    uint16_t name, type, owner, elementType, structName, valueType, valueStructName; // Into the strings.
    uint16_t subtype;
};
static_assert(sizeof(FieldsPageHeader) == 8 && sizeof(FieldRecord) == 44);
// A record's strings: owner is the class or struct that declares it.
struct FieldStrings {
    std::string_view name, type, owner, elementType, structName, valueType, valueStructName, subtype;
};
constexpr size_t MaxFieldsPerPage = (MaxRead - sizeof(FieldsPageHeader)) / sizeof(FieldRecord);
constexpr size_t MaxPageRefs = 64;

// Builds one page into `out` (MaxRead bytes). Add returns false when the
// field does not fit; the page stays valid without it. Pure; tested.
class FieldsPageWriter {
public:
    explicit FieldsPageWriter(uint8_t* out) : out_(out) { strings_[0] = 0; }
    bool Add(const FieldRecord& numbers, const Ref& structType, const Ref& valueStructType, const FieldStrings& text) {
        if (count_ == MaxFieldsPerPage) return false;
        const uint32_t savedStrings = used_;
        const uint16_t savedRefs = refs_;
        FieldRecord record = numbers;
        bool fits = RefNumber(structType, record.structType) && RefNumber(valueStructType, record.valueStructType) &&
            String(text.name, record.name) && String(text.type, record.type) && String(text.owner, record.owner) &&
            String(text.elementType, record.elementType) && String(text.structName, record.structName) &&
            String(text.valueType, record.valueType) && String(text.valueStructName, record.valueStructName) &&
            String(text.subtype, record.subtype);
        fits = fits && Bytes(count_ + 1, refs_, used_) <= MaxRead;
        if (!fits) {
            used_ = savedStrings;
            refs_ = savedRefs;
            return false;
        }
        memcpy(out_ + sizeof(FieldsPageHeader) + size_t{count_} * sizeof(FieldRecord), &record, sizeof(record));
        ++count_;
        return true;
    }
    uint32_t Count() const { return count_; }
    // Lays out the header, refs and strings after the records; the page's length.
    uint32_t Finish() {
        const FieldsPageHeader header{static_cast<uint16_t>(count_), refs_, used_};
        memcpy(out_, &header, sizeof(header));
        uint8_t* at = out_ + sizeof(header) + size_t{count_} * sizeof(FieldRecord);
        memcpy(at, refTable_, size_t{refs_} * sizeof(Ref));
        memcpy(at + size_t{refs_} * sizeof(Ref), strings_, used_);
        return static_cast<uint32_t>(Bytes(count_, refs_, used_));
    }

private:
    static size_t Bytes(size_t records, size_t refs, size_t strings) {
        return sizeof(FieldsPageHeader) + records * sizeof(FieldRecord) + refs * sizeof(Ref) + strings;
    }
    bool RefNumber(const Ref& ref, uint8_t& number) {
        number = 0;
        if (!ref.address) return true;
        for (uint16_t i = 0; i < refs_; ++i)
            if (refTable_[i].address == ref.address && refTable_[i].index == ref.index && refTable_[i].serial == ref.serial) {
                number = static_cast<uint8_t>(i + 1);
                return true;
            }
        if (refs_ == MaxPageRefs) return false;
        refTable_[refs_++] = ref;
        number = static_cast<uint8_t>(refs_);
        return true;
    }
    bool String(std::string_view text, uint16_t& offset) {
        offset = 0;
        if (text.empty()) return true;
        for (uint32_t at = 1; at < used_; at += static_cast<uint32_t>(strlen(strings_ + at)) + 1)
            if (text == std::string_view(strings_ + at)) {
                offset = static_cast<uint16_t>(at);
                return true;
            }
        if (used_ + text.size() + 1 > MaxRead) return false;
        offset = static_cast<uint16_t>(used_);
        memcpy(strings_ + used_, text.data(), text.size());
        strings_[used_ + text.size()] = 0;
        used_ += static_cast<uint32_t>(text.size()) + 1;
        return true;
    }
    uint8_t* out_;
    uint32_t count_ = 0, used_ = 1;          // The table starts with the empty string.
    uint16_t refs_ = 0;
    Ref refTable_[MaxPageRefs]{};
    char strings_[MaxRead];
};

// One page's fields, checked: the header inside `length`, every ref number
// and string offset inside its table, every string terminated in it.
// visit(record, structType, valueStructType, strings) per field; false when
// the page is malformed. Pure; tested.
template<class Visit>
bool ReadFieldsPage(const uint8_t* data, size_t length, Visit&& visit) {
    FieldsPageHeader header{};
    if (length < sizeof(header)) return false;
    memcpy(&header, data, sizeof(header));
    const size_t records = sizeof(header) + size_t{header.count} * sizeof(FieldRecord);
    const size_t refs = records + size_t{header.refs} * sizeof(Ref);
    if (header.count > MaxFieldsPerPage || header.refs > MaxPageRefs || !header.strings ||
        refs + header.strings != length || data[refs] != 0 || data[length - 1] != 0)
        return false;
    const char* table = reinterpret_cast<const char*>(data + refs);
    const auto text = [&](uint16_t offset, std::string_view& out) {
        if (offset >= header.strings || (offset && table[offset - 1] != 0)) return false;
        out = std::string_view(table + offset);
        return true;
    };
    const auto ref = [&](uint8_t number, Ref& out) {
        out = {};
        if (!number) return true;
        if (number > header.refs) return false;
        memcpy(&out, data + records + (size_t{number} - 1) * sizeof(Ref), sizeof(Ref));
        return true;
    };
    for (uint16_t i = 0; i < header.count; ++i) {
        FieldRecord record{};
        memcpy(&record, data + sizeof(header) + size_t{i} * sizeof(FieldRecord), sizeof(record));
        Ref structType{}, valueStructType{};
        FieldStrings strings;
        if (!ref(record.structType, structType) || !ref(record.valueStructType, valueStructType) ||
            !text(record.name, strings.name) || !text(record.type, strings.type) || !text(record.owner, strings.owner) ||
            !text(record.elementType, strings.elementType) || !text(record.structName, strings.structName) ||
            !text(record.valueType, strings.valueType) || !text(record.valueStructName, strings.valueStructName) ||
            !text(record.subtype, strings.subtype))
            return false;
        visit(record, structType, valueStructType, strings);
    }
    return true;
}

// UE5 keeps a map's or set's element layout in the property, after its inner
// property pointers: FScriptMapLayout (ValueOffset, then FScriptSetLayout) at
// the layout's mapLayout, FScriptSetLayout (HashNextIdOffset, HashIndexOffset, Size)
// at its setLayout. The SDK does not describe it, so every layout read
// through them is checked before use.
struct ContainerLayout { int32_t valueOffset, hashNextIdOffset, hashIndexOffset, stride; };
// The key at 0, a map's value after it, then two int32 hash links, all inside
// a stride of at most MaxObjectBytes (a slot of a big struct is a valid layout;
// only `entries` and previews keep to MaxArrayElementBytes). valueSize 0 = a
// set. Pure; tested.
inline bool ContainerLayoutValid(const ContainerLayout& layout, uint32_t keySize, uint32_t valueSize) {
    if (!keySize || layout.stride <= 0 || static_cast<uint32_t>(layout.stride) > MaxObjectBytes) return false;
    if (valueSize && (layout.valueOffset < 0 || static_cast<uint32_t>(layout.valueOffset) < keySize)) return false;
    const int64_t used = valueSize ? int64_t{layout.valueOffset} + valueSize : int64_t{keySize};
    return layout.hashNextIdOffset >= used && layout.hashIndexOffset == int64_t{layout.hashNextIdOffset} + 4 &&
        layout.stride >= int64_t{layout.hashIndexOffset} + 4;
}
// detail: Find = total matches, Fields = total fields (high half: the listed
// type's object-table index; Parameters: the block size); a parameter-size
// refusal carries the expected size.
static_assert(MaxFindResults * sizeof(FindEntry) <= MaxRead);
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
// detail CallKept marks a kept block. CallPure refuses, before calling, a
// function that is not const or pure (EachFunctionAllowed) or whose outputs
// own memory: such a call reads, so a client may run it without approval.
// A call that faults inside the engine is answered Faulted (detail: the
// object); the engine may be left in a bad state.
constexpr uint32_t CallRelease = 1, CallPure = 2;
constexpr uint64_t CallKept = 1;
constexpr uint32_t MaxKeptResults = 8;
// Slots carried from a kept block: the return value and outputs that are not
// const references (UE marks const& inputs OutParm too). A non-const
// reference is an input as well, but its kept value (memory the engine owns,
// such as an array's storage) replaces the request's, so nothing leaks.
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

// Enum: an enum object's ref; args[1] = the first entry of this page. The
// reply is one EnumEntry per name in the enum's order (UEnum::Names: each
// value's FName and int64), as many as MaxRead holds; detail =
// FindDetail(entries, next entry or 0). The client names them with Names.
struct EnumEntry {
    int64_t value;
    int32_t name;       // FName comparison index.
    uint32_t number;    // FName instance number.
};
constexpr uint32_t MaxEnumEntries = 65536;
constexpr size_t MaxEnumPerPage = MaxRead / sizeof(EnumEntry);

// GameThread: the host's game-thread tick through the game viewport's Draw
// (host_draw.h; a host that links reagentlive::ue-draw answers it). flags =
// GameThreadAction: Status reads it, On arms it (the host installs it on its
// next tick), Off puts the viewport's table back. The reply is a
// GameThreadState.
enum class GameThreadAction : uint32_t { Status, On, Off };
enum class GameThreadMode : uint32_t { Off, Waiting, Conflict, On, Stopped };
struct GameThreadState {
    uint32_t mode;              // GameThreadMode.
    uint32_t thread;            // The game thread's ID; 0 until known.
    uint64_t viewport;          // The viewport client hooked (or found); 0 until known.
    uint64_t table;             // Its Draw interface's own vtable; 0 until known.
    uint64_t draw;              // The game's Draw (that table's entry 4); 0 until known.
    char text[96];              // The state as one line.
};

// The call recorder, on the core's recorder slots (the recorder section of
// core/protocol.h). A host opts in by listing CallsStart with the core's
// op::RecorderRead and op::RecorderStop, which read and stop it. CallsStart swaps
// one UFunction's native pointer (UFunction::Func, a heap field: no code,
// vtable or import is written) for its slot's thunk.
// CallsStart: object ref (an instance, a default object or a class), text =
// "[Owner:]Function" (the ref holds args[0]), args[1] = milliseconds
// (1..MaxRecorderMilliseconds), data = an optional object address (length 8):
// only calls on that object; flags CallsDry
// resolves and reports without writing. Reply: CallsStarted.
// A CallsKind row's object is the call's context object, its tag CallsEvent
// or CallsVm, its payload the function's parameters after the call: on the
// ProcessEvent path (the frame's Node is the function) the frame's locals
// plus the return value; on the Blueprint VM path the arguments are
// bytecode, so the row has no payload (CallsVm). Status: target = the
// UFunction, payload = its parameter block's size, counts = {events, vm}.
constexpr uint32_t CallsKind = op::CallsStart;
constexpr uint32_t CallsDry = 1;
constexpr uint32_t CallsEventCount = 0, CallsVmCount = 1;
// UE5 frame and function layout the thunk reads (UObject::ProcessEvent,
// FFrame's constructor and UFunction::Invoke, CL-516586): FFrame::Node and
// ::Locals, UFunction::ReturnValueOffset (0xFFFF: none): the layout's
// frameNode, frameLocals and returnValueOffset. Not in the SDK.
struct CallsStarted {
    uint64_t function, original, thunk;
    uint32_t slot, blockSize, returnOffset, returnSize;
};
constexpr uint8_t CallsEvent = 1, CallsVm = 2;

// Owner: data = 1..MaxOwnerAddresses addresses (uint64 each). One object-table
// scan (budgeted per tick, like Census) finds, per address, the object whose
// memory holds it: the nearest object at or below the address, accepted when
// the offset is inside its class's size. The reply is one OwnerEntry per
// address, in the request's order; detail = addresses with an owner.
// Addresses in heap buffers (an array's elements) have no owner here: the
// client checks candidate objects' containers itself.
constexpr uint32_t MaxOwnerAddresses = 64;
constexpr uint32_t OwnerOuter = 1;  // The address is the owner's Outer pointer.
struct OwnerEntry {
    Ref object;          // Zero: no object holds the address.
    uint32_t offset;     // The address minus the object.
    uint32_t size;       // The object's class size.
    uint32_t flags, reserved;
};
static_assert(MaxOwnerAddresses * sizeof(OwnerEntry) <= MaxRead);
// For addresses sorted ascending: visit(i) for each address an object at
// `object` could hold (at or above it, within MaxObjectBytes). Pure; tested.
template<class Visit> void Covered(const uint64_t* sorted, size_t count, uint64_t object, Visit&& visit) {
    size_t low = 0, high = count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        if (sorted[middle] < object) low = middle + 1;
        else high = middle;
    }
    for (size_t i = low; i < count && sorted[i] - object < MaxObjectBytes; ++i) visit(i);
}

// Route: object ref = the target; data = RouteSpec. A breadth-first walk
// over reflected object references (budgeted per tick, like Census) from the
// sources: object properties (strong and interface pointers; no class, weak,
// soft or lazy ones), arrays, sets and map values of them or of structs
// holding them, and structs holding any of these, to MaxRouteStructDepth.
// Containers inside a container's element are not followed. RouteOuter also
// follows each object's Outer. A source is an object or a host root by name
// (Host::root). Every object is identified when found and resolved again
// before its fields are read, so one freed between ticks is skipped.
// The reply is a RouteHeader, then per route a RoutePath and its hops, the
// shortest routes only (all the same length), at most MaxRoutes.
constexpr uint32_t MaxRouteSources = 4, MaxRoutes = 4, MaxRouteDepth = 16, MaxRouteRoot = 32;
constexpr uint32_t MaxRouteBudget = 1u << 20;      // Objects visited.
constexpr uint32_t MaxRouteStructDepth = 4;
constexpr uint32_t MaxRouteElements = 1u << 16;    // Elements or slots read per container.
constexpr uint32_t RouteOuter = 1;
struct RouteSpec {
    uint32_t depth, budget, flags, sources;
    Ref from[MaxRouteSources];                 // A source object (index -1: identified by the agent), or zero.
    char roots[MaxRouteSources][MaxRouteRoot]; // Else a host root by name.
};
// Found: routes follow. Depth: none within depth hops (objects were left
// past it). Budget: none before budget objects were visited. None: every
// object reachable from the sources was visited.
enum class RouteEnd : uint32_t { Found, Depth, Budget, None };
struct RouteHeader {
    uint32_t routes;
    uint32_t visited;    // Objects reached, the sources included.
    uint32_t depth;      // Found: each route's hops; else the deepest level walked.
    uint32_t end;        // RouteEnd.
};
struct RoutePath { uint32_t source, hops; };      // The source's index in the spec.
// One hop: the pointer to the next object, read from `holder`.
enum class HopKind : uint32_t { Field, Array, Set, Map, Outer };
struct RouteHop {
    Ref holder;
    uint32_t kind;       // HopKind.
    uint32_t offset;     // Field: the pointer's offset in the holder; a container: the container's.
    uint32_t index;      // A container: the element (an array) or slot (a set or map).
    uint32_t inner;      // A container: the pointer's offset in the element or slot.
};
static_assert(sizeof(RouteHeader) + MaxRoutes * (sizeof(RoutePath) + MaxRouteDepth * sizeof(RouteHop)) <= MaxRead);
// Every source is an object or a root name (terminated, not both); bounds. Pure; tested.
inline bool RouteSpecValid(const RouteSpec& spec) {
    if (!spec.depth || spec.depth > MaxRouteDepth || !spec.budget || spec.budget > MaxRouteBudget ||
        (spec.flags & ~RouteOuter) || !spec.sources || spec.sources > MaxRouteSources) return false;
    for (uint32_t i = 0; i < MaxRouteSources; ++i) {
        const bool root = spec.roots[i][0] != 0, object = spec.from[i].address != 0;
        if (!memchr(spec.roots[i], 0, MaxRouteRoot)) return false;
        if (i < spec.sources ? root == object : root || object) return false;
    }
    return true;
}

// The pack's part of the layout check (added to reagentlive::LayoutCore by the host).
// New ops need no change here: the section header lists the ops an agent handles.
// Every struct the pack sends is in the sum (see reagentlive::LayoutCore).
constexpr uint32_t LayoutPart = static_cast<uint32_t>(sizeof(FindEntry) * 5 + sizeof(FieldRecord) * 11 +
    sizeof(FieldsPageHeader) * 107 + MaxPageRefs * 109 +
    sizeof(FunctionEntry) * 3 + sizeof(Ref) * 53 + sizeof(ArrayHeader) * 59 + sizeof(FunctionNameEntry) * 61 +
    sizeof(CensusEntry) * 67 + sizeof(ScriptEntry) * 71 + sizeof(RefsEntry) * 73 + sizeof(SparseHeader) * 79 +
    sizeof(ExecEntry) * 83 + sizeof(CallsStarted) * 89 + CallsKind * 97 +
    sizeof(EnumEntry) * 113 + sizeof(GameThreadState) * 127 + sizeof(OwnerEntry) * 131 + sizeof(RouteSpec) * 137 +
    sizeof(RouteHeader) * 139 + sizeof(RoutePath) * 149 + sizeof(RouteHop) * 151 +
    op::Fields * 101 + MaxArrayElementBytes * 7 + MaxClassName * 13);

}
