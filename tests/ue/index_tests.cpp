// reagentlive Unreal pack: the class index (class_index.h) over a fake table:
// candidates per class name, passes that read only changed slots, verdicts
// kept per name, rebuilds by age and work resumed across ticks.
#include "reagentlive/packs/ue/class_index.h"
#include <ostream>
#include "doctest.h"
#include <deque>
#include <string>
#include <string_view>
#include <vector>

using namespace reagentlive::ue;

namespace {
// Classes are ints whose address stands for the class; a class's parent is
// in `parents`; objects are ints naming their class.
struct FakeTable {
    int classes[4]{}; // 0 Object, 1 Actor : Object, 2 Pawn : Actor, 3 Table : Object
    const char* names[4]{"Object", "Actor", "Pawn", "Table"};
    int parents[4]{-1, 0, 1, 0};
    std::deque<int> objects;           // Per object: its class number (stable addresses).
    std::vector<const void*> slots;    // Per slot: an object or null.
    int headerReads = 0, namedCalls = 0;

    void Add(int type) {
        objects.push_back(type);
        slots.push_back(&objects.back());
    }
    int32_t Count() { return static_cast<int32_t>(slots.size()); }
    const void* Object(int32_t slot) { return slots[static_cast<size_t>(slot)]; }
    const void* ClassOf(const void* object, int32_t) {
        ++headerReads;
        return &classes[*static_cast<const int*>(object)];
    }
    bool Named(const void* type, std::string_view name) {
        ++namedCalls;
        for (int at = static_cast<int>(static_cast<const int*>(type) - classes); at >= 0; at = parents[at])
            if (name == names[at]) return true;
        return false;
    }
};

std::vector<int32_t> Slots(const index::ClassIndex& classIndex, int32_t end) {
    std::vector<int32_t> out;
    for (int32_t slot = classIndex.Next(0, end); slot < end; slot = classIndex.Next(slot + 1, end)) out.push_back(slot);
    return out;
}
bool Never() { return false; }
}

TEST_CASE("class index: candidates by class or parent, kept up to date by passes") {
    FakeTable table;
    for (int type : {1, 2, 3, 2, 0}) table.Add(type);
    index::ClassIndex classIndex;
    classIndex.Start("Actor", 1000);
    REQUIRE(classIndex.Step(table, Never));
    CHECK(Slots(classIndex, table.Count()) == std::vector<int32_t>{0, 1, 3});
    CHECK(table.headerReads == 5);

    // Nothing changed: the pass reads no header and the verdicts are kept.
    const int named = table.namedCalls;
    classIndex.Start("Actor", 2000);
    REQUIRE(classIndex.Step(table, Never));
    CHECK(table.headerReads == 5);
    CHECK(table.namedCalls == named);

    // A slot emptied, one replaced and one added: only those are read.
    table.slots[1] = nullptr;
    table.Add(3);
    table.slots[2] = table.slots.back();
    table.Add(2);
    classIndex.Start("Pawn", 3000);
    REQUIRE(classIndex.Step(table, Never));
    CHECK(Slots(classIndex, table.Count()) == std::vector<int32_t>{3, 6});
    CHECK(table.headerReads == 5 + 3);
    CHECK(classIndex.Next(4, 6) == 6); // The end bounds the walk.
}

TEST_CASE("class index: a full rebuild once the last is RebuildMs old") {
    FakeTable table;
    for (int type : {1, 3}) table.Add(type);
    index::ClassIndex classIndex;
    classIndex.Start("Table", 10);
    REQUIRE(classIndex.Step(table, Never));
    CHECK(table.headerReads == 2);
    classIndex.Start("Table", 10 + index::RebuildMs - 1);
    REQUIRE(classIndex.Step(table, Never));
    CHECK(table.headerReads == 2);
    classIndex.Start("Table", 10 + index::RebuildMs);
    REQUIRE(classIndex.Step(table, Never));
    CHECK(table.headerReads == 4);
    CHECK(Slots(classIndex, table.Count()) == std::vector<int32_t>{1});
}

TEST_CASE("class index: work resumes across ticks") {
    FakeTable table;
    for (int i = 0; i < 1000; ++i) table.Add(i % 4);
    index::ClassIndex classIndex;
    classIndex.Start("Object", 1);
    int ticks = 0;
    while (!classIndex.Step(table, [] { return true; })) ++ticks; // Every budget check says spent.
    CHECK(ticks == 3);
    CHECK(table.headerReads == 1000);
    CHECK(Slots(classIndex, table.Count()).size() == 1000);
}

TEST_CASE("class index: an unknown name has no candidates") {
    FakeTable table;
    table.Add(1);
    index::ClassIndex classIndex;
    classIndex.Start("Nothing", 1);
    REQUIRE(classIndex.Step(table, Never));
    CHECK(classIndex.Next(0, table.Count()) == table.Count());
}
