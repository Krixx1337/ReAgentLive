// reagentlive CLI: names a loaded image already carries, read from live bytes.
// Run-time type information (an MSVC object's class and its bases, from the
// locator in the slot before its virtual table) and imports (which DLL
// function an import-table slot holds). Pure: reads come in as a callback.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/core/client.h"
#include "reagentlive/frontends/cli/code/signature.h"
#include "reagentlive/frontends/cli/code/unwind.h"

namespace reagentlive::cli::pe {
using ReadFn = sig::ReadFn;
constexpr size_t MaxBases = 32;
constexpr uint32_t MaxTypeName = 512, MaxImportDescriptors = 1600;

namespace detail {
using unwind::detail::ReadOne;
using unwind::detail::U32;
inline int32_t I32(std::span<const uint8_t> bytes, size_t at) { return static_cast<int32_t>(U32(bytes, at)); }
inline uint64_t U64(std::span<const uint8_t> bytes, size_t at) {
    uint64_t value = 0;
    memcpy(&value, bytes.data() + at, 8);
    return value;
}
// A NUL-terminated string at `address` inside `module`, at most `limit` bytes;
// read up to the page end first so a short name near the module's end works.
inline std::optional<std::string> CString(const ReadFn& read, const Module& module, uint64_t address, uint32_t limit) {
    if (address < module.base || address >= module.base + module.size) return std::nullopt;
    std::string out;
    while (out.size() < limit) {
        const uint64_t page = 0x1000 - (address & 0xfff);
        const uint64_t room = module.base + module.size - address;
        const uint64_t length = std::min<uint64_t>({page, room, limit - out.size(), 2048});
        if (!length) return std::nullopt;
        auto bytes = ReadOne(read, address, length);
        if (!bytes) return std::nullopt;
        const auto end = std::find(bytes->begin(), bytes->end(), uint8_t{0});
        out.append(bytes->begin(), end);
        if (end != bytes->end()) return out;
        address += length;
    }
    return std::nullopt;
}
}

// ".?AVWeapon@Game@@" to "Game::Weapon"; anything with a template or another
// mangled form is returned as it is (the static tool undecorates those).
inline std::string Undecorate(std::string_view decorated) {
    std::string_view name = decorated;
    if (!(name.starts_with(".?AV") || name.starts_with(".?AU")) || !name.ends_with("@@")) return std::string(decorated);
    name = name.substr(4, name.size() - 6);
    if (name.empty() || name.find_first_of("?$") != std::string_view::npos) return std::string(decorated);
    std::vector<std::string_view> parts;
    for (size_t at = 0; at <= name.size();) {
        const size_t next = name.find('@', at);
        parts.push_back(name.substr(at, next == std::string_view::npos ? std::string_view::npos : next - at));
        if (next == std::string_view::npos) break;
        at = next + 1;
    }
    std::string out;
    for (auto part = parts.rbegin(); part != parts.rend(); ++part) {
        if (part->empty()) return std::string(decorated);
        if (!out.empty()) out += "::";
        out += *part;
    }
    return out;
}

struct TypeName {
    std::string name, decorated;
};
struct Rtti {
    uint64_t locator = 0;
    uint32_t offset = 0;        // Where this table's part starts inside the complete object.
    TypeName type;
    std::vector<TypeName> bases; // Every base the hierarchy lists, nearest first, without the class itself.
    bool capped = false;         // More than MaxBases.
};

// The class behind a virtual table: the complete object locator in the slot
// before it (signature 1, its own RVA naming the module's base), its type
// descriptor's name, then the class hierarchy's base list.
inline std::expected<Rtti, std::string> ReadRtti(const ReadFn& read, std::span<const Module> modules, uint64_t table) {
    using namespace detail;
    const auto* tableModule = sig::detail::ModuleAt(table, modules);
    if (!tableModule || table - tableModule->base < 8) return std::unexpected("the table lies in no loaded module");
    auto slot = ReadOne(read, table - 8, 8);
    if (!slot) return std::unexpected("the slot before the table is unreadable");
    Rtti out;
    out.locator = U64(*slot, 0);
    const auto* module = sig::detail::ModuleAt(out.locator, modules);
    if (!module) return std::unexpected("no run-time type information: the slot before the table points into no module");
    auto locator = ReadOne(read, out.locator, 24);
    if (!locator) return std::unexpected("the type information locator is unreadable");
    const int32_t self = I32(*locator, 20);
    if (U32(*locator, 0) != 1 || self <= 0 || module->base + static_cast<uint32_t>(self) != out.locator)
        return std::unexpected("no run-time type information: the slot before the table holds no locator");
    out.offset = U32(*locator, 4);
    const auto at = [&](int32_t rva) { return rva > 0 && static_cast<uint32_t>(rva) < module->size ? module->base + static_cast<uint32_t>(rva) : 0; };
    const uint64_t descriptor = at(I32(*locator, 12)), hierarchy = at(I32(*locator, 16));
    if (!descriptor) return std::unexpected("the locator's type descriptor lies outside its module");
    auto decorated = CString(read, *module, descriptor + 16, MaxTypeName);
    if (!decorated || !decorated->starts_with(".?A")) return std::unexpected("the type descriptor holds no type name");
    out.type = {Undecorate(*decorated), *decorated};
    if (!hierarchy) return out;
    auto header = ReadOne(read, hierarchy, 16);
    if (!header) return out;
    const uint32_t count = U32(*header, 8);
    const uint64_t array = at(I32(*header, 12));
    if (!array || count < 2) return out; // The list starts with the class itself.
    const uint32_t shown = std::min<uint32_t>(count - 1, MaxBases);
    out.capped = count - 1 > MaxBases;
    auto entries = ReadOne(read, array + 4, uint64_t{shown} * 4);
    if (!entries) return out;
    for (uint32_t i = 0; i < shown; ++i) {
        const uint64_t base = at(I32(*entries, size_t{i} * 4));
        if (!base) continue;
        auto first = ReadOne(read, base, 4);
        if (!first) continue;
        const uint64_t baseDescriptor = at(I32(*first, 0));
        if (!baseDescriptor) continue;
        if (auto name = CString(read, *module, baseDescriptor + 16, MaxTypeName); name && name->starts_with(".?A"))
            out.bases.push_back({Undecorate(*name), *name});
    }
    return out;
}

// The import directory and import address table of a module (data directories 1 and 12).
struct ImportDirectories {
    uint32_t importRva = 0, importSize = 0, tableRva = 0, tableSize = 0;
    bool InTable(uint64_t rva) const { return tableSize && rva >= tableRva && rva - tableRva < tableSize; }
};
inline std::optional<ImportDirectories> ReadImportDirectories(const ReadFn& read, const Module& module) {
    using namespace detail;
    auto dos = ReadOne(read, module.base + 0x3c, 4);
    if (!dos) return std::nullopt;
    const uint32_t pe = U32(*dos, 0);
    if (pe >= module.size || module.size - pe < 24 + 216) return std::nullopt;
    // Optional header from pe + 24: magic, then data directories from 112, 8 bytes each.
    auto optional = ReadOne(read, module.base + pe + 24, 216);
    if (!optional || (*optional)[0] != 0x0b || (*optional)[1] != 0x02) return std::nullopt;
    const uint32_t directories = U32(*optional, 108);
    ImportDirectories out;
    if (directories > 1) out = {U32(*optional, 120), U32(*optional, 124), 0, 0};
    if (directories > 12) {
        out.tableRva = U32(*optional, 208);
        out.tableSize = U32(*optional, 212);
    }
    return out;
}

// "kernel32.dll!VirtualAlloc" (or "...!#12" by ordinal) for an import-table
// slot: the descriptor whose table holds it, then its name table's entry.
inline std::optional<std::string> ImportAt(const ReadFn& read, const Module& module, const ImportDirectories& dirs,
                                           uint64_t slotAddress) {
    using namespace detail;
    if (slotAddress < module.base || !dirs.importRva || !dirs.InTable(slotAddress - module.base)) return std::nullopt;
    const uint32_t slot = static_cast<uint32_t>(slotAddress - module.base);
    // Descriptors (20 bytes each, ended by a zero one): the one whose table starts
    // closest below the slot.
    const uint32_t count = std::min<uint32_t>(dirs.importSize / 20, MaxImportDescriptors);
    uint32_t bestTable = 0, bestNames = 0, bestDll = 0;
    for (uint32_t first = 0; first < count; first += 100) {
        const uint32_t n = std::min<uint32_t>(100, count - first);
        auto bytes = ReadOne(read, module.base + dirs.importRva + uint64_t{first} * 20, uint64_t{n} * 20);
        if (!bytes) return std::nullopt;
        bool end = false;
        for (uint32_t i = 0; i < n; ++i) {
            const size_t at = size_t{i} * 20;
            const uint32_t names = U32(*bytes, at), dll = U32(*bytes, at + 12), table = U32(*bytes, at + 16);
            if (!names && !dll && !table) { end = true; break; }
            // No name table: the import table itself holds resolved addresses in a live image, never names.
            if (table <= slot && table > bestTable) { bestTable = table; bestNames = names; bestDll = dll; }
        }
        if (end) break;
    }
    if (!bestTable || !bestNames || (slot - bestTable) % 8) return std::nullopt;
    auto entry = ReadOne(read, module.base + bestNames + (slot - bestTable), 8);
    if (!entry) return std::nullopt;
    auto dll = CString(read, module, module.base + bestDll, 256);
    if (!dll) return std::nullopt;
    const uint64_t thunk = U64(*entry, 0);
    if (!thunk) return std::nullopt; // Past the descriptor's last import.
    if (thunk >> 63) return *dll + "!#" + std::to_string(thunk & 0xffff);
    auto name = CString(read, module, module.base + static_cast<uint32_t>(thunk) + 2, 256);
    return name ? std::optional<std::string>(*dll + "!" + *name) : std::nullopt;
}
}
