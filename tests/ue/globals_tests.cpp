// reagentlive Unreal pack: locating and checking the object table and name
// pool (globals.h) over fake memory, including garbage and stale addresses.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "reagentlive/packs/ue/globals.h"
#include <ostream>
#include "doctest.h"
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace reagentlive::ue;

namespace {
// Memory the fake engine owns: copies outside it fail, as a guarded copy of
// an unmapped address would.
struct FakeMemory {
    std::vector<std::pair<uint64_t, uint64_t>> ranges;
    template<class T> uint64_t Add(std::vector<T>& bytes) {
        const auto begin = reinterpret_cast<uint64_t>(bytes.data());
        ranges.emplace_back(begin, begin + bytes.size() * sizeof(T));
        return begin;
    }
    static bool Copy(const void* user, uint64_t address, void* out, size_t bytes) {
        for (const auto& [begin, end] : static_cast<const FakeMemory*>(user)->ranges)
            if (address >= begin && address + bytes <= end) { memcpy(out, reinterpret_cast<const void*>(address), bytes); return true; }
        return false;
    }
    globals::Memory View() const { return {&Copy, this}; }
};

// A chunked table of 4 items per chunk; objects carry index, class and name
// where an engine object does.
constexpr globals::TableShape Shape{0x20, 0, 0x10, 0x14, 0x1c, 0x18, 4, 0x18, 8, 0xc, 0x10, 0x18};
constexpr size_t ObjectSize = 0x28;

struct FakeEngine {
    FakeMemory memory;
    std::vector<uint8_t> objects, table, items[3], pool, block;
    std::vector<uint64_t> chunkList;
    uint64_t tableAddress = 0, poolAddress = 0;
    int32_t names[6]{};

    uint64_t Object(int32_t slot) { return reinterpret_cast<uint64_t>(objects.data()) + slot * ObjectSize; }
    void Set(int32_t slot, int32_t index, uint64_t type, int32_t name) {
        auto* at = objects.data() + slot * ObjectSize;
        memcpy(at + Shape.objectIndexOffset, &index, 4);
        memcpy(at + Shape.objectClassOffset, &type, 8);
        memcpy(at + Shape.objectNameOffset, &name, 4);
    }

    // Slot 0 the class of classes ("Class"), slot 1 a class ("Object") and
    // the rest instances of it; names: None, Class, Object, Thing, Struct, Field.
    explicit FakeEngine(const names::Layout& layout, int32_t count = 10) {
        objects.resize(count * ObjectSize);
        memory.Add(objects);
        for (const auto text : {std::string_view("None"), std::string_view("Class"), std::string_view("Object"),
                                std::string_view("Thing"), std::string_view("Struct"), std::string_view("Field")}) {
            const size_t at = block.size();
            block.resize(at + layout.textOffset + text.size());
            const auto header = static_cast<uint16_t>(text.size() << 6);
            memcpy(block.data() + at + layout.headerOffset, &header, sizeof(header));
            memcpy(block.data() + at + layout.textOffset, text.data(), text.size());
            block.resize((block.size() + layout.stride - 1) / layout.stride * layout.stride);
        }
        block.resize(block.size() + 16);
        // Comparison indices of the names: offsets over the stride.
        uint32_t offset = 0;
        names::Entry entry{};
        for (int i = 0; i < 6 && names::EntryAt(block, offset, layout, entry); ++i) {
            names[i] = names::Index(0, offset, layout);
            offset = static_cast<uint32_t>(names::NextOffset(entry, layout));
        }
        memory.Add(block);
        pool.resize(names::BlocksOffset + 8 * sizeof(uint64_t));
        const uint64_t blockAddress = reinterpret_cast<uint64_t>(block.data());
        memcpy(pool.data() + names::BlocksOffset, &blockAddress, sizeof(blockAddress));
        const uint32_t cursor = static_cast<uint32_t>(block.size());
        memcpy(pool.data() + 12, &cursor, sizeof(cursor));
        poolAddress = memory.Add(pool);

        Set(0, 0, Object(0), names[1]);
        Set(1, 1, Object(0), names[2]);
        for (int32_t slot = 2; slot < count; ++slot) Set(slot, slot, Object(1), names[3]);
        const int32_t chunkCount = (count + 3) / 4;
        for (int32_t c = 0; c < chunkCount; ++c) {
            items[c].resize(4 * Shape.itemSize);
            for (int32_t i = 0; i < 4 && c * 4 + i < count; ++i) {
                const uint64_t object = Object(c * 4 + i);
                memcpy(items[c].data() + i * Shape.itemSize + Shape.itemObjectOffset, &object, 8);
            }
            chunkList.push_back(memory.Add(items[c]));
        }
        memory.Add(chunkList);
        table.resize(Shape.headerSize);
        const uint64_t list = reinterpret_cast<uint64_t>(chunkList.data());
        const int32_t max = 64, maxChunks = 16;
        memcpy(table.data() + Shape.objectsOffset, &list, 8);
        memcpy(table.data() + Shape.maxOffset, &max, 4);
        memcpy(table.data() + Shape.numOffset, &count, 4);
        memcpy(table.data() + Shape.numChunksOffset, &chunkCount, 4);
        memcpy(table.data() + Shape.maxChunksOffset, &maxChunks, 4);
        tableAddress = memory.Add(table);
    }
    FakeEngine(const FakeEngine&) = delete;
};
}

TEST_CASE("a well-formed object table passes and names its class of classes") {
    FakeEngine engine(names::Layouts[0]);
    uint64_t root = 0;
    CHECK(globals::CheckTable(engine.memory.View(), engine.tableAddress, Shape, root) == nullptr);
    CHECK(root == engine.Object(0));
    CHECK(globals::ObjectAt(engine.memory.View(), globals::ParseHeader(engine.table.data(), Shape), Shape, 5) == engine.Object(5));
}

TEST_CASE("stale and garbage table addresses are refused, never followed blindly") {
    FakeEngine engine(names::Layouts[0]);
    const auto memory = engine.memory.View();
    uint64_t root = 0;
    CHECK(globals::CheckTable(memory, 0, Shape, root) != nullptr);
    CHECK(globals::CheckTable(memory, 0x12345678, Shape, root) != nullptr);          // Unmapped.
    CHECK(globals::CheckTable(memory, engine.poolAddress, Shape, root) != nullptr);  // Another global.
    CHECK(globals::CheckTable(memory, engine.Object(3), Shape, root) != nullptr);    // An object, not a table.

    engine.Set(5, 7, engine.Object(1), engine.names[3]); // An index that is not its slot.
    CHECK(std::string_view(globals::CheckTable(memory, engine.tableAddress, Shape, root)) == "table: index is not slot");
    engine.Set(5, 5, engine.Object(2), engine.names[3]); // A class chain ending elsewhere.
    engine.Set(2, 2, engine.Object(2), engine.names[3]);
    CHECK(globals::CheckTable(memory, engine.tableAddress, Shape, root) != nullptr);
}

TEST_CASE("implausible table headers fail the cheap check") {
    globals::TableHeader header{0x10000, 64, 10, 3, 16};
    CHECK(globals::HeaderProblem(header, Shape) == nullptr);
    auto bad = header; bad.num = 100;  // More than max.
    CHECK(globals::HeaderProblem(bad, Shape) != nullptr);
    bad = header; bad.numChunks = 2;   // Too few chunks for the items.
    CHECK(globals::HeaderProblem(bad, Shape) != nullptr);
    bad = header; bad.objects = 0x10003; // Misaligned.
    CHECK(globals::HeaderProblem(bad, Shape) != nullptr);
    bad = header; bad.objects = 0xffff800000000000ull; // Kernel half.
    CHECK(globals::HeaderProblem(bad, Shape) != nullptr);
}

TEST_CASE("the name pool check picks the layout that decodes None and Class") {
    for (int layout = 0; layout < static_cast<int>(std::size(names::Layouts)); ++layout) {
        FakeEngine engine(names::Layouts[layout]);
        int picked = -1;
        CHECK(globals::CheckPool(engine.memory.View(), engine.poolAddress, engine.names[1], picked) == nullptr);
        CHECK(picked == layout);
        char text[16];
        size_t length = 0;
        REQUIRE(globals::DecodeName(engine.memory.View(), engine.poolAddress, names::Layouts[layout], engine.names[3], text, sizeof(text), length));
        CHECK(std::string_view(text, length) == "Thing");
    }
}

TEST_CASE("a wrong pool or a wrong class name index is refused") {
    FakeEngine engine(names::Layouts[0]);
    const auto memory = engine.memory.View();
    int layout = -1;
    CHECK(globals::CheckPool(memory, engine.poolAddress, engine.names[2], layout) != nullptr); // "Object", not "Class".
    CHECK(globals::CheckPool(memory, engine.tableAddress, engine.names[1], layout) != nullptr);
    CHECK(globals::CheckPool(memory, 0x12345678, engine.names[1], layout) != nullptr);
    char text[16];
    size_t length = 0;
    CHECK_FALSE(globals::DecodeName(memory, engine.poolAddress, names::Layouts[0], 5 << 16, text, sizeof(text), length)); // Past the last block.
    CHECK_FALSE(globals::DecodeName(memory, engine.poolAddress, names::Layouts[0], -1, text, sizeof(text), length));
}

TEST_CASE("the hashed layout's kind byte: fixed-length kind, unknown kinds are no name") {
    const auto& layout = names::Layouts[1];
    std::vector<uint8_t> entry(layout.textOffset + 24, 'a');
    const uint16_t header = 0; // The header's length is not used for the fixed kind.
    memcpy(entry.data() + layout.headerOffset, &header, sizeof(header));
    entry[layout.kindOffset] = static_cast<uint8_t>(layout.fixedKind);
    uint32_t length = 0;
    bool wide = true;
    CHECK(names::HeaderAt(entry.data(), layout, length, wide));
    CHECK(length == layout.fixedLength);
    CHECK_FALSE(wide);
    entry[layout.kindOffset] = static_cast<uint8_t>(layout.fixedKind + 1);
    CHECK_FALSE(names::HeaderAt(entry.data(), layout, length, wide));
}

TEST_CASE("the section scan finds a table placed anywhere in a section") {
    FakeEngine engine(names::Layouts[0]);
    std::vector<uint64_t> section(40000, 0x4141414141414141ull); // Larger than one window.
    const size_t slot = 33333;
    memcpy(&section[slot], engine.table.data(), Shape.headerSize);
    const uint64_t begin = engine.memory.Add(section);
    const auto memory = engine.memory.View();
    std::vector<uint8_t> buffer(64u << 10);
    uint64_t root = 0;
    const uint64_t hit = globals::ScanSection(memory, {begin, begin + section.size() * 8}, buffer, Shape.headerSize,
        [&](uint64_t address, const uint8_t* bytes, size_t available) {
            return available >= Shape.headerSize && !globals::HeaderProblem(globals::ParseHeader(bytes, Shape), Shape) &&
                !globals::CheckTable(memory, address, Shape, root);
        });
    CHECK(hit == begin + slot * 8);
}

TEST_CASE("writable sections come from a loaded image's headers") {
    const globals::Memory memory{[](const void*, uint64_t address, void* out, size_t bytes) {
        memcpy(out, reinterpret_cast<const void*>(address), bytes);
        return true;
    }, nullptr};
    globals::Section sections[16];
    const auto image = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
    const size_t count = globals::WritableSections(memory, image, sections, std::size(sections));
    REQUIRE(count >= 1);
    for (size_t i = 0; i < count; ++i) CHECK(sections[i].begin >= image);
}

TEST_CASE("a layout whose item differs from the engine's is refused") {
    FakeEngine engine(names::Layouts[0]);
    const auto memory = engine.memory.View();
    uint64_t root = 0;
    auto objectFirst = Shape;
    objectFirst.itemObjectOffset = 0; // The fake keeps the object after an 8-byte field.
    CHECK(globals::CheckTable(memory, engine.tableAddress, objectFirst, root) != nullptr);
    auto shortItems = Shape;
    shortItems.itemSize = 0x10;
    CHECK(globals::CheckTable(memory, engine.tableAddress, shortItems, root) != nullptr);
    CHECK(globals::CheckTable(memory, engine.tableAddress, Shape, root) == nullptr);
}

TEST_CASE("the stock shapes share the UE5 object header and differ only in the item") {
    for (const auto& shape : globals::StockShapes) {
        CHECK(shape.objectIndexOffset == 0xc);
        CHECK(shape.objectClassOffset == 0x10);
        CHECK(shape.objectNameOffset == 0x18);
        CHECK(globals::ObjectHeaderBytes(shape) == 0x28);
        CHECK(shape.perChunk == 0x10000);
        CHECK(shape.itemSerialOffset == -1); // Unknown without an SDK: serials read 0.
    }
}

namespace {
// The engine's own class tree: Class -> Struct -> Field -> Object -> null at
// SuperStruct (+0x40), with UStruct::Size at +0x58, after a decoy pointer to
// Object at +0x28 (UField::Next) that the probe must pass over.
struct FakeClassTree {
    static constexpr uint32_t Super = 0x40, Size = 0x58;
    FakeEngine& engine;
    std::vector<uint8_t> classes[4]; // Class, Struct, Field, Object.
    uint64_t At(int i) { return reinterpret_cast<uint64_t>(classes[i].data()); }
    template<class T> void Put(int i, uint32_t offset, T value) { memcpy(classes[i].data() + offset, &value, sizeof(value)); }
    explicit FakeClassTree(FakeEngine& owner) : engine(owner) {
        const int32_t names[4] = {engine.names[1], engine.names[4], engine.names[5], engine.names[2]};
        const int32_t sizes[4] = {0x230, 0xb0, 0x30, 0x28};
        for (int i = 0; i < 4; ++i) {
            classes[i].resize(0x100);
            engine.memory.Add(classes[i]);
        }
        for (int i = 0; i < 4; ++i) {
            Put(i, Shape.objectNameOffset, names[i]);
            Put(i, Super, i < 3 ? At(i + 1) : uint64_t{0});
            Put(i, Size, sizes[i]);
            Put(i, 0x28, At(3)); // Decoy: a pointer to Object first.
        }
    }
};
}

TEST_CASE("SuperStruct and UStruct::Size are found from the engine's class tree") {
    FakeEngine engine(names::Layouts[0]);
    FakeClassTree tree(engine);
    const auto memory = engine.memory.View();
    uint64_t chain[3]{};
    const int32_t super = globals::ProbeSuperStruct(memory, engine.poolAddress, names::Layouts[0], Shape, tree.At(0), chain);
    CHECK(super == FakeClassTree::Super);
    CHECK(chain[0] == tree.At(1));
    CHECK(chain[1] == tree.At(2));
    CHECK(chain[2] == tree.At(3));
    CHECK(globals::ProbeStructSize(memory, Shape, super, chain[1], chain[2]) == FakeClassTree::Size);
}

TEST_CASE("a class tree that does not end in Object, then null, gives no SuperStruct") {
    FakeEngine engine(names::Layouts[0]);
    FakeClassTree tree(engine);
    const auto memory = engine.memory.View();
    uint64_t chain[3]{};
    tree.Put(3, FakeClassTree::Super, tree.At(0)); // Object's parent is not null.
    CHECK(globals::ProbeSuperStruct(memory, engine.poolAddress, names::Layouts[0], Shape, tree.At(0), chain) == -1);
    tree.Put(3, FakeClassTree::Super, uint64_t{0});
    tree.Put(2, FakeClassTree::Super, tree.At(1)); // Field's parent named Struct, not Object.
    CHECK(globals::ProbeSuperStruct(memory, engine.poolAddress, names::Layouts[0], Shape, tree.At(0), chain) == -1);
    CHECK(globals::ProbeStructSize(memory, Shape, -1, tree.At(2), tree.At(3)) == -1);
}
