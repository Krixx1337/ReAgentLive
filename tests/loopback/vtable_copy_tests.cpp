// reagentlive loopback: the core's private vtable copies (core/vtable_copy.h)
// on a fake native object whose table lives in this test's image.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <memory>
#include <vector>
#include "reagentlive/core/vtable_copy.h"
#include "doctest.h"

using namespace reagentlive;

namespace {
using Fn = int (*)();
// Distinct bodies, so identical-code folding keeps them apart.
int First() { return 10; }
int Second() { return 20; }
int Third() { return 30; }
int Hook() { return 99; }

int g_marker = 1;  // Stands for the RTTI locator before the table.
int g_data = 2;    // Data in the image, not code: ends the copy.
// table[0] is the slot before the table; the object points at table + 1.
void* g_table[] = {&g_marker, reinterpret_cast<void*>(&First), reinterpret_cast<void*>(&Second),
                   reinterpret_cast<void*>(&Third), &g_data};
void* g_other[] = {&g_marker, reinterpret_cast<void*>(&Third), &g_data};
// Its second entry is another module's code (filled at run time).
void* g_mixed[] = {&g_marker, reinterpret_cast<void*>(&First), nullptr, reinterpret_cast<void*>(&Second), &g_data};

constexpr uint32_t Offset = 0x28;
struct Object {
    void* before[Offset / sizeof(void*)]{};
    void** vptr = g_table + 1;
};
static_assert(offsetof(Object, vptr) == Offset);

int Call(const Object& object, uint32_t index) { return reinterpret_cast<Fn>(object.vptr[index])(); }
}

TEST_CASE("a vtable copy keeps table[-1], stops at the first entry not in code, and swaps one entry") {
    Object object;
    vtable::Entry entry{};
    REQUIRE(vtable::Take(&object, Offset, 1, entry) == vtable::Taken::Ok);
    CHECK(entry.count == 3);
    CHECK(entry.entries[-1] == &g_marker);
    for (uint32_t i = 0; i < 3; ++i) CHECK(entry.entries[i] == g_table[i + 1]);
    CHECK(entry.table == g_table + 1);
    CHECK(entry.original == reinterpret_cast<void*>(&Second));
    CHECK(const_cast<void* const*>(entry.cell) == entry.entries + 1);
    CHECK(object.vptr == entry.entries);           // The vptr holds the copy...
    CHECK(g_table[2] == reinterpret_cast<void*>(&Second)); // ...and the table is untouched.
    CHECK(Call(object, 1) == 20);
    REQUIRE(vtable::Swap(entry, reinterpret_cast<void*>(&Hook)));
    CHECK(Call(object, 1) == 99);
    CHECK(Call(object, 0) == 10);
    CHECK(vtable::InCopy(reinterpret_cast<uintptr_t>(entry.cell)));
    CHECK(!vtable::InCopy(reinterpret_cast<uintptr_t>(&object)));
    REQUIRE(vtable::Unswap(entry, reinterpret_cast<void*>(&Hook)));
    CHECK(vtable::Give(entry) == vtable::Given::Restored);
    CHECK(object.vptr == g_table + 1);
}

TEST_CASE("entries of one object share its copy; the vptr goes back with the last") {
    Object object;
    vtable::Entry first{}, third{}, again{};
    REQUIRE(vtable::Take(&object, Offset, 0, first) == vtable::Taken::Ok);
    REQUIRE(vtable::Take(&object, Offset, 2, third) == vtable::Taken::Ok);
    CHECK(first.copy == third.copy);
    CHECK(first.entries == third.entries);
    CHECK(vtable::Take(&object, Offset, 0, again) == vtable::Taken::Busy);
    CHECK(vtable::Take(&object, Offset, 3, again) == vtable::Taken::NotCode); // Past the copy.
    REQUIRE(vtable::Swap(first, reinterpret_cast<void*>(&Hook)));
    REQUIRE(vtable::Swap(third, reinterpret_cast<void*>(&Hook)));
    CHECK(Call(object, 0) == 99);
    CHECK(Call(object, 1) == 20);
    CHECK(Call(object, 2) == 99);
    REQUIRE(vtable::Unswap(first, reinterpret_cast<void*>(&Hook)));
    CHECK(vtable::Give(first) == vtable::Given::Shared);
    CHECK(object.vptr == third.entries);
    CHECK(Call(object, 2) == 99);
    REQUIRE(vtable::Unswap(third, reinterpret_cast<void*>(&Hook)));
    CHECK(vtable::Give(third) == vtable::Given::Restored);
    CHECK(object.vptr == g_table + 1);
}

TEST_CASE("a vptr outside a module image, or at another object's copy, is refused") {
    Object object, other;
    auto heap = std::make_unique<void*[]>(4);
    for (int i = 0; i < 4; ++i) heap[i] = g_table[i];
    object.vptr = heap.get() + 1;
    vtable::Entry entry{};
    CHECK(vtable::Take(&object, Offset, 0, entry) == vtable::Taken::Foreign);
    CHECK(object.vptr == heap.get() + 1);
    object.vptr = g_table + 1;
    REQUIRE(vtable::Take(&object, Offset, 0, entry) == vtable::Taken::Ok);
    other.vptr = object.vptr; // Our copy, but not this object's.
    vtable::Entry stray{};
    CHECK(vtable::Take(&other, Offset, 1, stray) == vtable::Taken::Foreign);
    CHECK(vtable::Give(entry) == vtable::Given::Restored);
    CHECK(vtable::Take(nullptr, Offset, 0, stray) == vtable::Taken::Unreadable);
}

TEST_CASE("an entry in another module's code is copied but not taken") {
    g_mixed[2] = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetTickCount"));
    REQUIRE(g_mixed[2]);
    Object object;
    object.vptr = g_mixed + 1;
    vtable::Entry entry{};
    CHECK(vtable::Take(&object, Offset, 1, entry) == vtable::Taken::NotCode);
    CHECK(object.vptr == g_mixed + 1);
    REQUIRE(vtable::Take(&object, Offset, 2, entry) == vtable::Taken::Ok);
    CHECK(entry.count == 3);
    CHECK(entry.entries[1] == g_mixed[2]);
    CHECK(vtable::Give(entry) == vtable::Given::Restored);
}

TEST_CASE("a vptr someone else changed is left alone") {
    Object object;
    vtable::Entry entry{}, more{};
    REQUIRE(vtable::Take(&object, Offset, 0, entry) == vtable::Taken::Ok);
    object.vptr = g_other + 1; // Another owner.
    CHECK(vtable::Take(&object, Offset, 1, more) == vtable::Taken::Changed);
    CHECK(vtable::Give(entry) == vtable::Given::Left);
    CHECK(object.vptr == g_other + 1);
    CHECK(vtable::Give(entry) == vtable::Given::Left); // Given already.
    object.vptr = g_table + 1;
    REQUIRE(vtable::Take(&object, Offset, 0, more) == vtable::Taken::Ok); // A fresh copy.
    CHECK(more.copy != entry.copy);
    CHECK(vtable::Give(more) == vtable::Given::Restored);
}

TEST_CASE("a cell someone else swapped keeps the copy until it holds the original again") {
    Object object;
    vtable::Entry entry{};
    REQUIRE(vtable::Take(&object, Offset, 1, entry) == vtable::Taken::Ok);
    *entry.cell = reinterpret_cast<void*>(&Hook); // Not through Swap.
    CHECK(!vtable::Unswap(entry, reinterpret_cast<void*>(&Third)));
    CHECK(vtable::Give(entry) == vtable::Given::Left);
    CHECK(object.vptr == entry.entries);
    CHECK(Call(object, 1) == 99);
    *entry.cell = entry.original;
    vtable::Entry again{};
    REQUIRE(vtable::Take(&object, Offset, 0, again) == vtable::Taken::Ok); // The same copy, still in place.
    CHECK(again.copy == entry.copy);
    CHECK(vtable::Give(again) == vtable::Given::Restored);
    CHECK(object.vptr == g_table + 1);
}

TEST_CASE("copies are bounded, and a restored copy of the same table is reused at once") {
    std::vector<Object> objects(vtable::MaxCopies + 1);
    std::vector<vtable::Entry> entries;
    vtable::Taken taken = vtable::Taken::Ok;
    for (auto& object : objects) {
        vtable::Entry entry{};
        if ((taken = vtable::Take(&object, Offset, 0, entry)) != vtable::Taken::Ok) break;
        entries.push_back(entry);
    }
    CHECK(taken == vtable::Taken::Full);
    CHECK(!entries.empty());
    CHECK(entries.size() <= vtable::MaxCopies);
    for (const auto& entry : entries) CHECK(vtable::Give(entry) == vtable::Given::Restored);
    vtable::Entry entry{};
    REQUIRE(vtable::Take(&objects.back(), Offset, 0, entry) == vtable::Taken::Ok);
    CHECK(vtable::Give(entry) == vtable::Given::Restored);
}
