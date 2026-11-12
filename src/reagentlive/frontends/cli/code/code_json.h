// reagentlive CLI: fast-lane and native-object results as typed structs
// (disasm, xrefs, touches, function bounds, vtable, region). No I/O; the
// conventions are in results/result_json.h.
#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include "reagentlive/frontends/cli/code/disasm.h"
#include "reagentlive/frontends/cli/values/json.h"
#include "reagentlive/frontends/cli/code/native.h"
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/frontends/cli/code/touches.h"
#include "reagentlive/frontends/cli/code/unwind.h"
#include "reagentlive/frontends/cli/code/xrefs.h"

namespace reagentlive::cli {
namespace shape {
struct AddressRange {
    Addr start, end;
    template<class V> static void Describe(V& v) {
        v("start", &AddressRange::start);
        v("end", &AddressRange::end);
    }
};
// A function entry from the exception table, plus `chunk` when the address
// lies in a continuation.
struct FunctionBounds {
    Addr start, end;
    Location where;
    std::optional<AddressRange> chunk;
    template<class V> static void Describe(V& v) {
        v("start", &FunctionBounds::start);
        v("end", &FunctionBounds::end);
        v.flat("", &FunctionBounds::where);
        v("chunk", &FunctionBounds::chunk);
    }
};
struct InstructionField {
    uint64_t offset = 0, size = 0;
    std::string kind;
    template<class V> static void Describe(V& v) {
        v("offset", &InstructionField::offset);
        v("size", &InstructionField::size);
        v("kind", &InstructionField::kind);
    }
};
struct Instruction {
    Addr address;
    Location where;
    HexBytes bytes;
    std::string text;
    std::optional<Addr> target;
    Location targetWhere;
    std::optional<std::string> string;
    std::optional<bool> wide;
    std::optional<std::vector<InstructionField>> fields;
    std::optional<bool> ends;
    template<class V> static void Describe(V& v) {
        v("address", &Instruction::address);
        v.flat("", &Instruction::where);
        v("bytes", &Instruction::bytes);
        v("text", &Instruction::text);
        v("target", &Instruction::target);
        v.flat("target_", &Instruction::targetWhere);
        v("string", &Instruction::string);
        v("wide", &Instruction::wide);
        v("fields", &Instruction::fields);
        v("ends", &Instruction::ends);
    }
};
struct DisasmEnd {
    Addr address;
    uint64_t offset = 0;
    std::string reason;
    template<class V> static void Describe(V& v) {
        v("address", &DisasmEnd::address);
        v("offset", &DisasmEnd::offset);
        v("reason", &DisasmEnd::reason);
    }
};
struct DisasmHop {
    Addr from, to;
    Location toWhere;
    template<class V> static void Describe(V& v) {
        v("from", &DisasmHop::from);
        v("to", &DisasmHop::to);
        v.flat("to_", &DisasmHop::toWhere);
    }
};
// disasm: the rows, how many bytes they cover, why the listing stopped, the
// hops --follow took, and the function entry holding the first address.
struct Disasm {
    Addr address;
    Location where;
    uint64_t size = 0;
    std::string stop;
    std::vector<Instruction> rows;
    std::optional<FunctionBounds> function;
    std::optional<DisasmEnd> end;
    std::optional<std::vector<DisasmHop>> followed;
    std::optional<Chain> chain;
    template<class V> static void Describe(V& v) {
        v("address", &Disasm::address);
        v.flat("", &Disasm::where);
        v("size", &Disasm::size);
        v("stop", &Disasm::stop);
        v("rows", &Disasm::rows);
        v("function", &Disasm::function);
        v("end", &Disasm::end);
        v("followed", &Disasm::followed);
        v("chain", &Disasm::chain);
    }
};

struct XrefRow {
    std::string kind;
    Addr field;
    std::optional<Addr> instruction;
    Location where;
    std::optional<FunctionBounds> function;
    template<class V> static void Describe(V& v) {
        v("kind", &XrefRow::kind, "call|jmp|jcc|lea|load|store|rip|pointer|unknown");
        v("field", &XrefRow::field);
        v("instruction", &XrefRow::instruction);
        v.flat("", &XrefRow::where);
        v("function", &XrefRow::function);
    }
};
// xrefs: hits per kind over every hit, and the rows (unknown ones only with
// --all). Each row locates the instruction where the kind fixes it, else the field.
struct Xrefs {
    Addr address;
    Location where;
    std::string scanned;
    std::map<std::string, uint64_t> kinds;
    std::vector<XrefRow> rows;
    bool capped = false;
    int64_t scanMs = 0;
    template<class V> static void Describe(V& v) {
        v("address", &Xrefs::address);
        v.flat("", &Xrefs::where);
        v("scanned", &Xrefs::scanned);
        v("kinds", &Xrefs::kinds);
        v("rows", &Xrefs::rows);
        v("capped", &Xrefs::capped);
        v("scan_ms", &Xrefs::scanMs);
    }
};

struct VtableSlot {
    uint64_t slot = 0;
    HexNum offset;
    Addr target;
    Location where;
    std::optional<Addr> body;
    Location bodyWhere;
    template<class V> static void Describe(V& v) {
        v("slot", &VtableSlot::slot);
        v("offset", &VtableSlot::offset);
        v("target", &VtableSlot::target);
        v.flat("", &VtableSlot::where);
        v("body", &VtableSlot::body);
        v.flat("body_", &VtableSlot::bodyWhere);
    }
};
// vtable: the object (or the table itself with --table), the table pointer
// (absent when the qword there points into no module: `found` holds it), and
// the slots up to the first entry that is not code.
struct Vtable {
    Addr address;
    Location where;
    HexNum offset;
    std::optional<Addr> table;
    Location tableWhere;
    std::optional<std::string> stop;
    std::optional<HexNum> stopValue, found;
    std::vector<VtableSlot> rows;
    std::optional<Chain> chain;
    template<class V> static void Describe(V& v) {
        v("address", &Vtable::address);
        v.flat("", &Vtable::where);
        v("offset", &Vtable::offset);
        v("table", &Vtable::table);
        v.flat("table_", &Vtable::tableWhere);
        v("stop", &Vtable::stop, "count|not code|end of read");
        v("stop_value", &Vtable::stopValue);
        v("found", &Vtable::found);
        v("rows", &Vtable::rows);
        v("chain", &Vtable::chain);
    }
};

// region: the VirtualQuery facts and a one-word kind; `section` inside an image.
struct Region {
    Addr address;
    Location where;
    std::optional<std::string> section;
    std::string kind;
    Addr base;
    HexNum size;
    Addr allocationBase;
    std::string state, type, protect, allocationProtect;
    std::optional<Chain> chain;
    template<class V> static void Describe(V& v) {
        v("address", &Region::address);
        v.flat("", &Region::where);
        v("section", &Region::section);
        v("kind", &Region::kind, "code|rodata|data|mapped|private|reserved|free");
        v("base", &Region::base);
        v("size", &Region::size);
        v("allocation_base", &Region::allocationBase);
        v("state", &Region::state);
        v("type", &Region::type);
        v("protect", &Region::protect);
        v("allocation_protect", &Region::allocationProtect);
        v("chain", &Region::chain);
    }
};

struct TouchUse {
    HexNum offset;
    Addr instruction;
    Location where;
    bool write = false;
    std::string text;
    template<class V> static void Describe(V& v) {
        v("offset", &TouchUse::offset);
        v("instruction", &TouchUse::instruction);
        v.flat("", &TouchUse::where);
        v("write", &TouchUse::write);
        v("text", &TouchUse::text);
    }
};
struct TouchRow {
    FunctionBounds function;
    uint64_t distinct = 0;
    bool writes = false;
    uint64_t total = 0;             // Uses in the function; `uses` shows the first ones.
    std::vector<TouchUse> uses;
    template<class V> static void Describe(V& v) {
        v("function", &TouchRow::function);
        v("distinct", &TouchRow::distinct);
        v("writes", &TouchRow::writes);
        v("total", &TouchRow::total);
        v("uses", &TouchRow::uses);
    }
};
struct TouchScanned {
    Addr from, to;
    uint64_t functions = 0, bytes = 0;
    template<class V> static void Describe(V& v) {
        v("from", &TouchScanned::from);
        v("to", &TouchScanned::to);
        v("functions", &TouchScanned::functions);
        v("bytes", &TouchScanned::bytes);
    }
};
// touches: the asked offsets, what was scanned, and the ranked functions with
// their uses.
struct Touches {
    Addr address;
    Location where;
    std::vector<HexNum> offsets;
    uint64_t range = 0;
    TouchScanned scanned;
    uint64_t total = 0;
    std::vector<TouchRow> rows;
    bool capped = false;
    int64_t scanMs = 0;
    template<class V> static void Describe(V& v) {
        v("address", &Touches::address);
        v.flat("", &Touches::where);
        v("offsets", &Touches::offsets);
        v("range", &Touches::range);
        v("scanned", &Touches::scanned);
        v("total", &Touches::total);
        v("rows", &Touches::rows);
        v("capped", &Touches::capped);
        v("scan_ms", &Touches::scanMs);
    }
};
}

inline std::optional<shape::FunctionBounds> MakeFunction(const std::optional<unwind::Function>& function,
                                                         std::span<const Module> modules) {
    if (!function) return std::nullopt;
    shape::FunctionBounds bounds{function->range.start, function->range.end, Locate(function->range.start, modules), {}};
    if (function->chunk) bounds.chunk = shape::AddressRange{function->chunk->start, function->chunk->end};
    return bounds;
}

inline shape::Instruction MakeInstruction(const dis::Instruction& row, std::span<const Module> modules) {
    shape::Instruction out;
    out.address = row.address;
    out.where = Locate(row.address, modules);
    out.bytes = HexBytes(row.bytes);
    out.text = row.text;
    if (row.target) {
        out.target = Addr(*row.target);
        out.targetWhere = Locate(*row.target, modules);
    }
    if (row.string) {
        out.string = *row.string;
        if (row.wide) out.wide = true;
    }
    if (!row.fields.empty()) {
        out.fields.emplace();
        for (const auto& field : row.fields) out.fields->push_back({field.offset, field.size, std::string(field.kind)});
    }
    if (row.ends) out.ends = true;
    return out;
}

inline shape::Disasm MakeDisasm(uint64_t address, const dis::Listing& listing,
                                const std::optional<unwind::Function>& function, std::span<const Module> modules) {
    shape::Disasm out;
    out.address = address;
    out.where = Locate(address, modules);
    for (const auto& row : listing.rows) {
        out.rows.push_back(MakeInstruction(row, modules));
        out.size += row.bytes.size();
    }
    out.stop = listing.stop;
    out.function = MakeFunction(function, modules);
    if (listing.end) out.end = shape::DisasmEnd{listing.end->address, listing.end->offset, listing.end->reason};
    if (!listing.followed.empty()) {
        out.followed.emplace();
        for (const auto& hop : listing.followed) out.followed->push_back({hop.from, hop.to, Locate(hop.to, modules)});
    }
    return out;
}

inline shape::Xrefs MakeXrefs(uint64_t address, const Module& scanned, std::span<const xref::Hit> hits,
                              std::span<const std::optional<unwind::Function>> functions, bool all, bool capped,
                              int64_t milliseconds, std::span<const Module> modules) {
    shape::Xrefs out;
    out.address = address;
    out.where = Locate(address, modules);
    out.scanned = scanned.name;
    for (const auto kind : xref::Kinds) out.kinds[std::string(kind)] = 0;
    for (size_t i = 0; i < hits.size(); ++i) {
        const auto& hit = hits[i];
        ++out.kinds[std::string(hit.kind)];
        if (hit.kind == "unknown" && !all) continue;
        shape::XrefRow row;
        row.kind = hit.kind;
        row.field = hit.field;
        if (hit.instruction) row.instruction = Addr(*hit.instruction);
        row.where = Locate(hit.instruction.value_or(hit.field), modules);
        row.function = MakeFunction(i < functions.size() ? functions[i] : std::nullopt, modules);
        out.rows.push_back(std::move(row));
    }
    out.capped = capped;
    out.scanMs = milliseconds;
    return out;
}

inline shape::Vtable MakeVtable(uint64_t address, uint64_t offset, std::optional<uint64_t> table,
                                std::optional<uint64_t> found, const native::Slots& slots,
                                std::span<const Module> modules) {
    shape::Vtable out;
    out.address = address;
    out.where = Locate(address, modules);
    out.offset = offset;
    for (const auto& slot : slots.slots) {
        shape::VtableSlot row{slot.slot, HexNum(slot.slot * 8), slot.target, Locate(slot.target, modules), {}, {}};
        if (slot.body) {
            row.body = Addr(*slot.body);
            row.bodyWhere = Locate(*slot.body, modules);
        }
        out.rows.push_back(std::move(row));
    }
    if (table) {
        out.table = Addr(*table);
        out.tableWhere = Locate(*table, modules);
        out.stop = slots.stop;
        if (slots.stopValue) out.stopValue = HexNum(*slots.stopValue);
    } else if (found) {
        out.found = HexNum(*found);
    }
    return out;
}

inline shape::Region MakeRegion(uint64_t address, const RegionInfo& info, std::string_view kind, std::string_view section,
                                std::span<const Module> modules) {
    shape::Region out;
    out.address = address;
    out.where = Locate(address, modules);
    if (!section.empty()) out.section = std::string(section);
    out.kind = kind;
    out.base = info.base;
    out.size = info.size;
    out.allocationBase = info.allocationBase;
    out.state = native::region::State(info.state);
    out.type = native::region::Type(info.type);
    out.protect = native::region::Protection(info.protect);
    out.allocationProtect = native::region::Protection(info.allocationProtect);
    return out;
}

inline shape::Touches MakeTouches(uint64_t address, std::span<const uint32_t> offsets, uint64_t range, uint64_t from,
                                  uint64_t to, size_t entries, size_t bytes, const std::vector<touch::Row>& rows,
                                  int64_t ms, std::span<const Module> modules) {
    shape::Touches out;
    out.address = address;
    out.where = Locate(address, modules);
    for (const uint32_t offset : offsets) out.offsets.push_back(HexNum(offset));
    out.range = range;
    out.scanned = {from, to, entries, bytes};
    out.total = rows.size();
    for (size_t i = 0; i < rows.size() && i < touch::MaxRows; ++i) {
        const auto& row = rows[i];
        shape::TouchRow item;
        item.function = {row.start, row.end, Locate(row.start, modules), {}};
        item.distinct = row.distinct;
        item.writes = row.writes;
        item.total = row.uses.size();
        for (size_t j = 0; j < row.uses.size() && j < touch::MaxUsesPerRow; ++j) {
            const auto& use = row.uses[j];
            item.uses.push_back({HexNum(use.offset), use.instruction, Locate(use.instruction, modules), use.write, use.text});
        }
        out.rows.push_back(std::move(item));
    }
    out.capped = rows.size() > touch::MaxRows;
    out.scanMs = ms;
    return out;
}
}
