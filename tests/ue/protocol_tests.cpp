// reagentlive request bounds (core and Unreal pack), detail packing, page sizes and
// the name-pool walk. No CLI headers: the pack is checked without the console.
#include "reagentlive/core/protocol.h"
#include "reagentlive/ue/agent_ue.h"
#include "reagentlive/ue/name_pool.h"
#include "reagentlive/ue/protocol_ue.h"
#include "doctest.h"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace reagentlive;
using reagentlive::ue::Ref;

TEST_CASE("refs round-trip through the request") {
    Request request{};
    const Ref ref{0x7ff012345678, 185664, -1};
    ue::SetRef(request, ref);
    CHECK(request.address == 0x7ff012345678);
    CHECK(ue::GetRef(request) == ref);
    ue::SetRef(request, {0x10, -1, 0});
    CHECK(ue::GetRef(request).index == -1);
}

TEST_CASE("op ranges keep core, packs and games apart") {
    CHECK(op::Read >= op::CoreFirst);
    CHECK(op::Modules < op::PackFirst);
    CHECK(ue::op::Find >= op::PackFirst);
    CHECK(ue::op::Call < op::GameFirst);
}

TEST_CASE("fields paging is bounded") {
    Request request{};
    request.op = ue::op::Fields;
    ue::SetRef(request, {0x1000, 1, 0});
    request.length = ue::MaxFieldStart + 1;
    CHECK(ue::Check(request) == Status::BadRequest);
}

TEST_CASE("agent bounds refuse malformed requests") {
    Request request{};
    CHECK(Check(request) == Status::BadRequest);
    request.op = op::Read;
    request.address = 0x10;
    request.length = MaxRead + 1;
    CHECK(Check(request) == Status::BadRequest);
    request.op = op::Write;
    request.length = MaxWrite + 1;
    CHECK(Check(request) == Status::BadRequest);
    request.op = ue::op::Find;
    for (char& c : request.text) c = 'a'; // Not terminated.
    CHECK(ue::Check(request) == Status::BadRequest);
    request = {};
    request.op = ue::op::Call;
    CopyText(request.text, "Fn");
    CHECK(ue::Check(request) == Status::BadRequest); // No object.
}

TEST_CASE("name matching is a case-insensitive substring") {
    CHECK(ue::NameMatches("BP_PlayerController_C", "playercontroller"));
    CHECK_FALSE(ue::NameMatches("Pawn", "PawnX"));
    CHECK_FALSE(ue::NameMatches("Pawn", ""));
}

TEST_CASE("function names split on the first colon") {
    auto split = ue::SplitFunction("Actor:K2_GetActorLocation");
    CHECK(split.owner == "Actor");
    CHECK(split.name == "K2_GetActorLocation");
    split = ue::SplitFunction("GetName");
    CHECK(split.owner.empty());
    CHECK(split.name == "GetName");
}

TEST_CASE("find and field pages fit one response") {
    CHECK(ue::MaxFindResults * sizeof(ue::FindEntry) <= sizeof(Response::data));
    CHECK(ue::MaxFieldsPerPage * sizeof(ue::FieldEntry) <= sizeof(Response::data));
    CHECK(LayoutCore != 0);
    CHECK(ue::LayoutPart != 0);
}

TEST_CASE("object reads require a full ref and a bounded range") {
    Request request{};
    request.op = ue::op::ReadObject;
    ue::SetRef(request, Ref{0x1000, 5, 9});
    request.length = static_cast<uint32_t>(MaxRead);
    request.args[1] = ue::MaxObjectBytes - request.length;
    CHECK(ue::Check(request) == Status::Ok);
    ++request.args[1];
    CHECK(ue::Check(request) == Status::BadRequest);
    request.args[1] = 0;
    request.address = UINT64_MAX;
    CHECK(ue::Check(request) == Status::BadRequest);
    ue::SetRef(request, Ref{0x1000, -1, 0});
    CHECK(ue::Check(request) == Status::BadRequest);
}

TEST_CASE("array preview bounds count, stride and address") {
    CHECK(ue::ArrayPreviewBytes({0x1000, 2, 8}, 8) == 16);
    CHECK(ue::ArrayPreviewBytes({0x1000, INT32_MAX, INT32_MAX}, 64) == 512);
    CHECK(ue::ArrayPreviewBytes({0x1000, 0, 8}, 8) == 0);
    CHECK(ue::ArrayPreviewBytes({0x1000, 8, 7}, 8) == 0);
    CHECK(ue::ArrayPreviewBytes({0, 2, 8}, 8) == 0);
    CHECK(ue::ArrayPreviewBytes({UINT64_MAX - 7, 1, 1}, 8) == 0);
    CHECK(ue::ArrayPreviewBytes({0x1000, 1, 1}, 513) == 0);
    CHECK(ue::ArrayPreviewBytes({0x1000, 3, 3}, 104) == 312);       // Short array of large elements.
    CHECK(ue::ArrayPreviewBytes({0x1000, 20, 20}, 512) == MaxRead);  // Eight would exceed one response.
    CHECK(ue::ArrayPreviewCount(20, 512) == 8);
    CHECK(ue::ArrayPreviewCount(20, 513) == 0);
    Request request{};
    request.op = ue::op::PreviewArray;
    ue::SetRef(request, Ref{0x1000, 5, 9});
    request.length = 8;
    request.args[1] = ue::MaxObjectBytes - sizeof(ue::ArrayHeader);
    CHECK(ue::Check(request) == Status::Ok);
    ++request.args[1];
    CHECK(ue::Check(request) == Status::BadRequest);
}

TEST_CASE("find bounds and detail packing") {
    Request request{};
    request.op = ue::op::Find;
    CHECK(ue::Check(request) == Status::BadRequest);          // Neither name nor class.
    request.length = 3;
    memcpy(request.data, "abc", 3);                          // Class without its terminator.
    CHECK(ue::Check(request) == Status::BadRequest);
    request.data[2] = 0;
    CHECK(ue::Check(request) == Status::Ok);
    request.args[1] = 0x80000000ull;
    CHECK(ue::Check(request) == Status::BadRequest);
    const auto detail = ue::FindDetail(2620, 331227);
    CHECK(ue::FindTotal(detail) == 2620);
    CHECK(ue::FindNext(detail) == 331227);
    CHECK(ue::FindNext(ue::FindDetail(5, 0)) == 0);
}

TEST_CASE("each bounds field reads and call blocks") {
    Request request{};
    request.op = ue::op::Each;
    request.args[0] = ue::EachField(0x308, 8);
    CHECK(ue::Check(request) == Status::BadRequest);          // No class.
    memcpy(request.data, "Pawn", 5);
    request.length = 5;
    CHECK(ue::Check(request) == Status::Ok);
    request.args[0] = ue::EachField(0x308, ue::MaxEachValue + 1);
    CHECK(ue::Check(request) == Status::BadRequest);
    request.args[0] = ue::EachField(ue::MaxObjectBytes - 4, 8);
    CHECK(ue::Check(request) == Status::BadRequest);
    CopyText(request.text, "Character:CanJump");
    request.args[0] = ue::EachField(0, 1);
    CHECK(ue::Check(request) == Status::Ok);
    request.args[0] = ue::EachField(8, 1);                    // Calls take no offset.
    CHECK(ue::Check(request) == Status::BadRequest);
    request.args[0] = ue::EachField(0, 1);
    request.flags = ue::EachReuse;
    CHECK(ue::Check(request) == Status::Ok);
    request.flags = 2;
    CHECK(ue::Check(request) == Status::BadRequest);
    request.flags = 0;
    request.args[1] = ue::MaxEachInstances + 1;
    CHECK(ue::Check(request) == Status::BadRequest);
    CHECK(ue::EachStride(1) == sizeof(Ref) + 8);
    CHECK(ue::EachStride(16) == sizeof(Ref) + 16);
}

TEST_CASE("each runs only const or pure functions with plain outputs") {
    using namespace ue::flags;
    CHECK(ue::EachFunctionAllowed(FunctionBlueprintPure));
    CHECK(ue::EachFunctionAllowed(FunctionConst));
    CHECK_FALSE(ue::EachFunctionAllowed(0x04000000)); // BlueprintCallable alone.
    CHECK(ue::EachParameterAllowed(Parm | ReturnParm, true));
    CHECK(ue::EachParameterAllowed(Parm | ReturnParm | ConstParm, true));   // A const return value.
    CHECK(ue::EachParameterAllowed(Parm | OutParm, true));
    CHECK_FALSE(ue::EachParameterAllowed(Parm, true));                      // An input.
    CHECK_FALSE(ue::EachParameterAllowed(Parm | OutParm | ConstParm, true)); // A const ref input.
    CHECK_FALSE(ue::EachParameterAllowed(Parm | ReturnParm, false));        // Not plain data.
    CHECK(ue::OwningParameter(Parm | ReturnParm, false));
    CHECK_FALSE(ue::OwningParameter(Parm | ReturnParm, true));
    CHECK_FALSE(ue::OwningParameter(Parm | OutParm | ConstParm, false));     // An input: never carried.
}

namespace {
// A property as the plain rule sees it: flags, and members when it is a struct.
struct FakeProperty {
    uint64_t flags;
    std::vector<FakeProperty> members;
    bool isStruct = false;
};
bool IsPlain(const FakeProperty& property) {
    const auto flagsOf = [](const FakeProperty& p) { return p.flags; };
    const auto membersOf = [](const FakeProperty& p, const auto& visit) -> int {
        if (!p.isStruct) return -1;
        for (const auto& member : p.members) visit(member);
        return static_cast<int>(p.members.size());
    };
    return ue::PlainProperty(property, flagsOf, membersOf);
}
}

TEST_CASE("plain means nothing to destroy and zero is a valid start, by flags or by every member") {
    using namespace ue::flags;
    const FakeProperty name{ZeroConstructor | PlainOldData | NoDestructor, {}};
    const FakeProperty vector{ZeroConstructor | PlainOldData | NoDestructor, {}, true};
    CHECK(IsPlain(vector));
    // A gameplay tag return: NoDestructor only, one FName member with every flag.
    const FakeProperty tag{Parm | OutParm | ReturnParm | NoDestructor, {name}, true};
    CHECK(IsPlain(tag));
    // A tag container: arrays need destroying.
    const FakeProperty array{ZeroConstructor, {}};
    const FakeProperty container{Parm | ReturnParm, {array, array}, true};
    CHECK_FALSE(IsPlain(container));
    CHECK_FALSE(IsPlain(FakeProperty{NoDestructor, {}}));            // Not a struct: the flags decide.
    CHECK_FALSE(IsPlain(FakeProperty{NoDestructor, {}, true}));      // A struct with no members.
    CHECK(IsPlain(FakeProperty{0, {FakeProperty{0, {name}, true}}, true})); // Nested.
}

TEST_CASE("census and each pages fit one response") {
    CHECK(ue::MaxCensusPerPage * sizeof(ue::CensusEntry) <= sizeof(Response::data));
    CHECK(ue::MaxCensusPerPage >= 24);
    CHECK(sizeof(Response::data) / ue::EachStride(ue::MaxEachValue) >= 40);
}

namespace {
// One block of name entries in a layout, as the engine lays them out.
std::vector<uint8_t> NameBlock(const ue::names::Layout& layout, std::initializer_list<std::string_view> names) {
    std::vector<uint8_t> bytes;
    for (const auto name : names) {
        const size_t at = bytes.size();
        bytes.resize(at + layout.textOffset + name.size());
        const auto header = static_cast<uint16_t>(name.size() << 6);
        memcpy(bytes.data() + at + layout.headerOffset, &header, sizeof(header));
        memcpy(bytes.data() + at + layout.textOffset, name.data(), name.size());
        bytes.resize((bytes.size() + layout.stride - 1) / layout.stride * layout.stride);
    }
    bytes.resize(bytes.size() + 16); // Zero header: the block's end.
    return bytes;
}
std::vector<std::pair<int32_t, std::string>> Walk(std::span<const uint8_t> block, const ue::names::Layout& layout) {
    std::vector<std::pair<int32_t, std::string>> out;
    uint32_t offset = 0;
    ue::names::Entry entry{};
    while (ue::names::EntryAt(block, offset, layout, entry)) {
        char text[64];
        ue::names::Text(block, entry, layout, text, sizeof(text));
        out.emplace_back(ue::names::Index(3, offset, layout), text);
        offset = static_cast<uint32_t>(ue::names::NextOffset(entry, layout));
    }
    return out;
}
}

TEST_CASE("name pool entries walk in both layouts") {
    for (const auto& layout : ue::names::Layouts) {
        const auto block = NameBlock(layout, {"None", "ByteProperty", "State.Location.Indoors", "Hostile"});
        const auto names = Walk(block, layout);
        REQUIRE(names.size() == 4);
        CHECK(names[0].second == "None");
        CHECK(names[2].second == "State.Location.Indoors");
        CHECK(names[0].first == 3 << 16);
        CHECK(ue::names::Block(names[3].first) == 3);
        CHECK(ue::names::Offset(names[1].first, layout) % layout.stride == 0);
    }
    // The hashed layout as seen live: None, then ByteProperty two strides later.
    const auto& hashed = ue::names::Layouts[1];
    CHECK(Walk(NameBlock(hashed, {"None", "ByteProperty"}), hashed)[1].first == (3 << 16 | 2));
}

TEST_CASE("name pool walking stops at truncated entries and narrows wide text") {
    const auto& layout = ue::names::Layouts[0];
    auto block = NameBlock(layout, {"Abc"});
    block.resize(4); // Header plus two of three characters.
    ue::names::Entry entry{};
    CHECK_FALSE(ue::names::EntryAt(block, 0, layout, entry));
    std::vector<uint8_t> wide(16, 0);
    const uint16_t header = static_cast<uint16_t>(2 << 6 | 1);
    const uint16_t chars[] = {'O', 0x263a};
    memcpy(wide.data(), &header, 2);
    memcpy(wide.data() + 2, chars, 4);
    REQUIRE(ue::names::EntryAt(wide, 0, layout, entry));
    CHECK(entry.wide);
    char text[8];
    CHECK(ue::names::Text(wide, entry, layout, text, sizeof(text)) == 2);
    CHECK(std::string_view(text) == "O?");
    CHECK(ue::names::NextOffset(entry, layout) == 6);
}

TEST_CASE("kept call results carry the return value and non-const outputs only") {
    using namespace ue::flags;
    const ue::ParameterSlot slots[] = {
        {0, 4, Parm},                          // Input.
        {4, 4, Parm | OutParm | ConstParm},    // Const reference input.
        {8, 4, Parm | OutParm},                // Out parameter.
        {12, 4, Parm | ReturnParm},            // Return value.
        {14, 8, Parm | ReturnParm},            // Overruns the block: skipped.
    };
    uint8_t kept[16], block[16];
    memset(kept, 0xAA, sizeof(kept));
    memset(block, 0x11, sizeof(block));
    ue::CarryOutputs(slots, std::size(slots), kept, block, sizeof(block));
    for (int i = 0; i < 8; ++i) CHECK(block[i] == 0x11);
    for (int i = 8; i < 16; ++i) CHECK(block[i] == 0xAA);
    CHECK(ue::CarriedParameter(Parm | OutParm));
    CHECK_FALSE(ue::CarriedParameter(Parm | OutParm | ConstParm));
    CHECK_FALSE(ue::OwningParameter(Parm | ReturnParm, true));
    CHECK(ue::OwningParameter(Parm | ReturnParm, false));                // A handle: has a destructor.
    CHECK_FALSE(ue::OwningParameter(Parm | OutParm | ConstParm, false)); // An input, never kept.
}

TEST_CASE("a full kept-result table refuses a new function and keeps every entry") {
    static ue::KeptResults table{};
    for (uintptr_t f = 1; f <= ue::MaxKeptResults; ++f) {
        auto* entry = table.Add(f * 0x100, 16);
        REQUIRE(entry);
        entry->block[0] = static_cast<uint8_t>(f);
    }
    CHECK(table.Add(0x9900, 16) == nullptr);
    for (uintptr_t f = 1; f <= ue::MaxKeptResults; ++f) {
        auto* entry = table.Find(f * 0x100);
        REQUIRE(entry);
        CHECK(entry->block[0] == f);
    }
    ue::KeptResults::Drop(*table.Find(0x300));
    CHECK(table.Find(0x300) == nullptr);
    CHECK(table.Add(0x9900, 16) != nullptr);
    CHECK(table.Find(0) == nullptr);
}

TEST_CASE("call accepts only the release flag") {
    Request request{};
    request.op = ue::op::Call;
    request.address = 0x1000;
    strcpy_s(request.text, "GetName");
    CHECK(ue::Check(request) == Status::Ok);
    request.flags = ue::CallRelease;
    CHECK(ue::Check(request) == Status::Ok);
    request.flags = 2;
    CHECK(ue::Check(request) == Status::BadRequest);
}
TEST_CASE("the script scan finds anchored names, calls and locals in real bytecode") {
    // From BP_Action_Reload_C:GetReloadSpeedMultiplier, read live: the tag
    // lookup, a local handle, then the GetValues math call.
    const uint8_t bytes[] = {0x21, 0x0f, 0x3a, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x16,
        0x14, 0x00, 0x80, 0xd9, 0x68, 0xc5, 0xaa, 0x01, 0x00, 0x00,
        0x68, 0x78, 0x03, 0x7a, 0xd8, 0xf4, 0x7f, 0x00, 0x00, 0x0b};
    std::vector<ue::ScriptCandidate> found;
    ue::ScanScript(bytes, sizeof(bytes), [&](const ue::ScriptCandidate& c) { found.push_back(c); });
    const auto has = [&](ue::ScriptKind kind, uint64_t value, uint32_t offset) {
        return std::ranges::any_of(found, [&](const ue::ScriptCandidate& c) {
            return c.kind == kind && c.value == value && c.offset == offset;
        });
    };
    CHECK(has(ue::ScriptKind::Name, 145935, 0));
    CHECK(has(ue::ScriptKind::Property, 0x1aac568d980, 16));
    CHECK(has(ue::ScriptKind::Function, 0x7ff4d87a0378, 25));
    // Every candidate sits right after its opcode; nothing is read past the end.
    for (const auto& c : found) {
        CHECK(bytes[c.offset] == c.opcode);
        CHECK(c.offset + 9 <= sizeof(bytes));
    }
    // A name constant cut short by the end is not a candidate.
    const uint8_t cut[] = {0x21, 0x0f, 0x3a, 0x02, 0x00};
    found.clear();
    ue::ScanScript(cut, sizeof(cut), [&](const ue::ScriptCandidate& c) { found.push_back(c); });
    CHECK(found.empty());
}

TEST_CASE("describe many takes 1..32 whole refs") {
    Request request{};
    request.op = ue::op::DescribeMany;
    CHECK(ue::Check(request) == Status::BadRequest); // None.
    request.length = sizeof(ue::Ref);
    CHECK(ue::Check(request) == Status::Ok);
    request.length = sizeof(ue::Ref) * ue::MaxDescribeMany;
    CHECK(ue::Check(request) == Status::Ok);
    request.length = sizeof(ue::Ref) * (ue::MaxDescribeMany + 1);
    CHECK(ue::Check(request) == Status::BadRequest);
    request.length = sizeof(ue::Ref) + 4; // Not whole refs.
    CHECK(ue::Check(request) == Status::BadRequest);
}

TEST_CASE("map and set layouts are checked before use") {
    // The tag-to-float attribute map decoded by hand: 8-byte key, 4-byte value, 20-byte stride.
    CHECK(ue::ContainerLayoutValid({8, 12, 16, 20}, 8, 4));
    CHECK(ue::ContainerLayoutValid({0, 8, 12, 16}, 8, 0));    // A set of 8-byte elements.
    CHECK_FALSE(ue::ContainerLayoutValid({4, 12, 16, 20}, 8, 4));  // Value overlaps the key.
    CHECK_FALSE(ue::ContainerLayoutValid({8, 10, 14, 20}, 8, 4));  // Hash link inside the value.
    CHECK_FALSE(ue::ContainerLayoutValid({8, 12, 20, 24}, 8, 4));  // Links not adjacent.
    CHECK_FALSE(ue::ContainerLayoutValid({8, 12, 16, 18}, 8, 4));  // Stride too short.
    CHECK_FALSE(ue::ContainerLayoutValid({8, 12, 16, 0}, 8, 4));
    CHECK_FALSE(ue::ContainerLayoutValid({8, 12, 16, 1024}, 8, 4)); // Past MaxArrayElementBytes.
    CHECK_FALSE(ue::ContainerLayoutValid({8, 12, 16, 20}, 0, 4));
}

TEST_CASE("entries requests name a known container kind and a bounded stride") {
    Request request{};
    request.op = ue::op::Entries;
    request.address = 0x1000;
    request.length = 20;
    request.flags = static_cast<uint32_t>(ue::ContainerKind::Map);
    request.args[1] = ue::EntriesArgs(0x100, 7);
    CHECK(ue::Check(request) == Status::Ok);
    CHECK(ue::EntriesOffset(request.args[1]) == 0x100);
    CHECK(ue::EntriesFirst(request.args[1]) == 7);
    request.flags = 4;
    CHECK(ue::Check(request) == Status::BadRequest);  // Unknown kind.
    request.flags = static_cast<uint32_t>(ue::ContainerKind::Array);
    request.length = ue::MaxArrayElementBytes + 1;
    CHECK(ue::Check(request) == Status::BadRequest);
    request.length = 0;
    CHECK(ue::Check(request) == Status::BadRequest);
    request.length = 8;
    request.args[1] = ue::EntriesArgs(ue::MaxObjectBytes, 0); // Header past the object.
    CHECK(ue::Check(request) == Status::BadRequest);
    request.args[1] = 0;
    request.address = 0;
    CHECK(ue::Check(request) == Status::BadRequest);
}

TEST_CASE("a sparse set skips free slots and pages from the next allocated slot") {
    // Slots 0..9; 2, 5 and 6 are free.
    const uint32_t bits[1] = {0b1110011011};
    std::vector<uint32_t> seen;
    uint32_t next = ue::WalkSlots(bits, 10, 0, 4, [&](uint32_t slot) { seen.push_back(slot); });
    CHECK(seen == std::vector<uint32_t>{0, 1, 3, 4});
    CHECK(next == 7);
    seen.clear();
    next = ue::WalkSlots(bits, 10, next, 4, [&](uint32_t slot) { seen.push_back(slot); });
    CHECK(seen == std::vector<uint32_t>{7, 8, 9});
    CHECK(next == 0);
    seen.clear();
    CHECK(ue::WalkSlots(bits, 10, 10, 4, [&](uint32_t slot) { seen.push_back(slot); }) == 0);
    CHECK(seen.empty());
}

TEST_CASE("set headers are checked before any element read") {
    ue::SparseHeader header{};
    header.data = 0x5000;
    header.num = header.numBits = 10;
    header.max = 16;
    header.maxBits = 128;
    header.numFree = 3;
    CHECK(ue::SparseHeaderValid(header));
    CHECK(ue::SparseHeaderValid(ue::SparseHeader{})); // Empty.
    auto bad = header;
    bad.numFree = 11;                                   // More free than slots.
    CHECK_FALSE(ue::SparseHeaderValid(bad));
    bad = header;
    bad.max = 5;                                        // Count past capacity.
    CHECK_FALSE(ue::SparseHeaderValid(bad));
    bad = header;
    bad.numBits = 9;                                    // One bit per slot.
    CHECK_FALSE(ue::SparseHeaderValid(bad));
    bad = header;
    bad.data = 0;
    CHECK_FALSE(ue::SparseHeaderValid(bad));
    bad = header;
    bad.num = bad.numBits = 200;                        // Past the inline words without a secondary pointer.
    bad.max = bad.maxBits = 256;
    CHECK_FALSE(ue::SparseHeaderValid(bad));
    bad.secondaryBits = 0x6000;
    CHECK(ue::SparseHeaderValid(bad));
}

TEST_CASE("exec entries take only a row cursor") {
    Request request{};
    request.op = ue::op::ExecEntries;
    CHECK(ue::Check(request) == Status::Ok);
    request.args[1] = ue::MaxExecEntries;
    CHECK(ue::Check(request) == Status::Ok);
    request.args[1] = ue::MaxExecEntries + 1;
    CHECK(ue::Check(request) == Status::BadRequest);
    request.args[1] = 0;
    request.address = 0x1000; // No object.
    CHECK(ue::Check(request) == Status::BadRequest);
    request.address = 0;
    request.length = 4;
    CHECK(ue::Check(request) == Status::BadRequest);
    CHECK(ue::MaxExecPerPage == 30);
}

TEST_CASE("script and refs requests are bounded") {
    Request request{};
    request.op = ue::op::Script;
    CHECK(ue::Check(request) == Status::BadRequest); // No object.
    request.address = 0x1000;
    CHECK(ue::Check(request) == Status::Ok);
    request.op = ue::op::Refs;
    request.address = 0;
    CHECK(ue::Check(request) == Status::BadRequest); // No name index.
    request.args[0] = 145935;
    CHECK(ue::Check(request) == Status::Ok);
    request.args[1] = ue::MaxRefs + 1;
    CHECK(ue::Check(request) == Status::BadRequest);
}
TEST_CASE("the interface stub's vtable slot comes from mov rax, [reg] then call [rax + disp]") {
    // GetSpreadAngle's stub, read live: mov rcx, rsi; ...; mov rax, [rsi]; call [rax + 0xA8].
    const uint8_t live[] = {0x48, 0x8B, 0xCE, 0x48, 0x8B, 0x54, 0x24, 0x30, 0x48, 0x8B, 0x06, 0xFF, 0x90, 0xA8, 0x00, 0x00,
        0x00, 0x48, 0x8B, 0x5C};
    CHECK(ue::StubVtableSlot(live, sizeof(live)) == 0xA8);
    const uint8_t short8[] = {0x49, 0x8B, 0x00, 0xFF, 0x50, 0x18};
    CHECK(ue::StubVtableSlot(short8, sizeof(short8)) == 0x18);
    const uint8_t negative[] = {0x48, 0x8B, 0x06, 0xFF, 0x50, 0xF8};
    CHECK(ue::StubVtableSlot(negative, sizeof(negative)) == -1);
    // RIP-relative and SIB loads are not object vtable loads.
    const uint8_t rip[] = {0x48, 0x8B, 0x05, 0xFF, 0x90, 0x10, 0x00, 0x00, 0x00};
    CHECK(ue::StubVtableSlot(rip, sizeof(rip)) == -1);
    const uint8_t cut[] = {0x48, 0x8B, 0x06, 0xFF, 0x90, 0xA8};
    CHECK(ue::StubVtableSlot(cut, sizeof(cut)) == -1);
}

TEST_CASE("pack handlers need the host's engine-thread check") {
    ue::Context missing{nullptr, nullptr};
    CHECK(std::string(ue::EngineThreadProblem(missing)) == "host gave no engine-thread check");
    ue::Context elsewhere{nullptr, [] { return false; }};
    CHECK(std::string(ue::EngineThreadProblem(elsewhere)) == "not on the game thread");
    ue::Context engine{nullptr, [] { return true; }};
    CHECK(ue::EngineThreadProblem(engine) == nullptr);
}
