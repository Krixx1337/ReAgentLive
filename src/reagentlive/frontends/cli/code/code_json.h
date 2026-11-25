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
#include "reagentlive/frontends/cli/code/pe_names.h"
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
    std::optional<HexBytes> bytes;       // Absent in a whole-function listing without `meta`.
    std::string text;
    std::optional<Addr> target;
    Location targetWhere;
    std::optional<std::string> string;
    std::optional<bool> wide;
    std::optional<std::string> import;   // An import-table slot target: "kernel32.dll!VirtualAlloc".
    std::optional<std::vector<InstructionField>> fields;
    std::optional<bool> ends;
    // A read-only [rip + disp] load's value: a scalar as `value` with
    // `value_type` (f32, f64, i32, i64), anything else as `value_hex`.
    std::optional<double> value;
    std::optional<std::string> valueType;
    std::optional<HexBytes> valueHex;
    template<class V> static void Describe(V& v) {
        v("address", &Instruction::address);
        v.flat("", &Instruction::where);
        v("bytes", &Instruction::bytes);
        v("text", &Instruction::text);
        v("target", &Instruction::target);
        v.flat("target_", &Instruction::targetWhere);
        v("string", &Instruction::string);
        v("wide", &Instruction::wide);
        v("import", &Instruction::import);
        v("fields", &Instruction::fields);
        v("ends", &Instruction::ends);
        v("value", &Instruction::value);
        v("value_type", &Instruction::valueType);
        v("value_hex", &Instruction::valueHex);
    }
};
// A string a whole-function listing references, once, with the rows using it.
struct DisasmString {
    std::string text;
    std::optional<bool> wide;
    std::vector<Addr> at;
    template<class V> static void Describe(V& v) {
        v("text", &DisasmString::text);
        v("wide", &DisasmString::wide);
        v("at", &DisasmString::at);
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
        v("source", &DisasmHop::from);
        v("target", &DisasmHop::to);
        v.flat("target_", &DisasmHop::toWhere);
    }
};
// disasm save: one saved function or chunk, its bounds and instruction
// count; a function saved with callees also its call depth (0: the first).
struct SavedFunction {
    Addr start, end;
    uint64_t rows = 0;
    std::optional<uint32_t> depth;
    template<class V> static void Describe(V& v) {
        v("start", &SavedFunction::start);
        v("end", &SavedFunction::end);
        v("rows", &SavedFunction::rows);
        v("depth", &SavedFunction::depth);
    }
};
// disasm save with callees: a call or tail-jump target left unsaved, and
// why: "depth" (past the depth asked), "count" or "bytes" (a cap of the
// whole save), "unreadable" (nothing listed there).
struct SkippedCall {
    Addr address;
    std::string why;
    template<class V> static void Describe(V& v) {
        v("address", &SkippedCall::address);
        v("why", &SkippedCall::why);
    }
};
// disasm with `save`: a switch found in the saved code: its indirect jump, its
// table (saved as data) and the distinct targets the table gives.
struct JumpTable {
    Addr jump, table;
    uint32_t targets = 0;
    template<class V> static void Describe(V& v) {
        v("jump", &JumpTable::jump);
        v("table", &JumpTable::table);
        v("targets", &JumpTable::targets);
    }
};
// disasm save: the reflected classes owning the saved functions (by their
// labels) and the types their layout rows describe.
struct SavedLayouts {
    std::vector<std::string> classes;
    uint64_t types = 0;
    template<class V> static void Describe(V& v) {
        v("classes", &SavedLayouts::classes);
        v("types", &SavedLayouts::types);
    }
};
// disasm: the rows, how many bytes they cover, why the listing stopped
// (`next_cursor` where a count or byte limit cut it), the hops `jumps` took, the
// function entry holding the first address, and with `whole` the strings
// the rows reference.
struct Disasm {
    Addr address;
    Location where;
    uint64_t size = 0;
    std::string stop;
    std::optional<Addr> nextCursor;
    std::optional<std::vector<Instruction>> rows; // Absent with `save`.
    std::optional<std::vector<DisasmString>> strings;
    std::optional<std::string> saved;    // `save`: the file written; rows and strings are left out.
    std::optional<uint64_t> savedRows;   // `save`: the instructions the file holds.
    std::optional<std::vector<SavedFunction>> functions; // callees: each saved function, the first included.
    std::optional<std::vector<SkippedCall>> skipped;     // callees: call targets the depth or a cap left out.
    std::optional<std::vector<SavedFunction>> chunks;    // `save`: each chunk saved with its function.
    std::optional<std::vector<JumpTable>> tables;        // `save`: the switches whose targets were followed.
    std::optional<std::vector<Addr>> jumpoutsLeft;       // `save`: the function's jump targets still unsaved.
    std::optional<bool> capped;                          // `save`: chunks stopped at the cap.
    std::optional<SavedLayouts> layouts;                 // save: layout rows for the saved methods' classes.
    std::optional<AddressRange> piece;   // A walk from inside unlisted code: the piece holding the address.
    std::optional<FunctionBounds> function;
    std::optional<DisasmEnd> end;
    std::optional<std::vector<DisasmHop>> followed;
    std::optional<Chain> chain;
    template<class V> static void Describe(V& v) {
        v("address", &Disasm::address);
        v.flat("", &Disasm::where);
        v("size", &Disasm::size);
        v("stop", &Disasm::stop);
        v("next_cursor", &Disasm::nextCursor);
        v("rows", &Disasm::rows);
        v("strings", &Disasm::strings);
        v("saved", &Disasm::saved);
        v("saved_rows", &Disasm::savedRows);
        v("functions", &Disasm::functions);
        v("skipped", &Disasm::skipped);
        v("chunks", &Disasm::chunks);
        v("tables", &Disasm::tables);
        v("jumpouts_left", &Disasm::jumpoutsLeft);
        v("capped", &Disasm::capped);
        v("layouts", &Disasm::layouts);
        v("piece", &Disasm::piece);
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
    std::optional<bool> unlisted;   // True: code outside every exception-table entry, bounds the piece's.
    template<class V> static void Describe(V& v) {
        v("kind", &XrefRow::kind, "call|jmp|jcc|lea|load|store|rip|pointer|unknown");
        v("field", &XrefRow::field);
        v("instruction", &XrefRow::instruction);
        v.flat("", &XrefRow::where);
        v("function", &XrefRow::function);
        v("unlisted", &XrefRow::unlisted);
    }
};
// xrefs: hits per kind over every hit, and the rows (unknown ones only with
// `all`). Each row locates the instruction where the kind fixes it, else the field.
struct Xrefs {
    Addr address;
    Location where;
    std::string scanned;
    std::map<std::string, uint64_t> kinds;
    std::vector<XrefRow> rows;
    bool capped = false;
    int64_t scanMs = 0;
    std::optional<Chain> chain;
    template<class V> static void Describe(V& v) {
        v("address", &Xrefs::address);
        v.flat("", &Xrefs::where);
        v("scanned", &Xrefs::scanned);
        v("kinds", &Xrefs::kinds);
        v("rows", &Xrefs::rows);
        v("capped", &Xrefs::capped);
        v("scan_ms", &Xrefs::scanMs);
        v("chain", &Xrefs::chain);
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
// A class in run-time type information: its readable and decorated names.
struct RttiType {
    std::string name, decorated;
    template<class V> static void Describe(V& v) {
        v("name", &RttiType::name);
        v("decorated", &RttiType::decorated);
    }
};
// vtable with `rtti`: the class an MSVC object's run-time type information names.
struct VtableRtti {
    Addr locator;
    std::string name, decorated;
    Addr complete;               // The complete object: this part minus its offset inside it.
    HexNum completeOffset;
    std::vector<RttiType> bases;
    bool capped = false;
    template<class V> static void Describe(V& v) {
        v("locator", &VtableRtti::locator);
        v("name", &VtableRtti::name);
        v("decorated", &VtableRtti::decorated);
        v("complete", &VtableRtti::complete);
        v("complete_offset", &VtableRtti::completeOffset);
        v("bases", &VtableRtti::bases);
        v("capped", &VtableRtti::capped);
    }
};

// vtable with `parts`: one distinct table inside the object, every offset
// that holds it (embedded structs repeat a table), and the slots asked
// (with `slots`).
struct VtablePart {
    std::vector<HexNum> offsets;
    Addr table;
    Location tableWhere;
    std::vector<VtableSlot> rows;
    template<class V> static void Describe(V& v) {
        v("offsets", &VtablePart::offsets);
        v("table", &VtablePart::table);
        v.flat("table_", &VtablePart::tableWhere);
        v("rows", &VtablePart::rows);
    }
};

// vtable: the object (or the table itself), the table pointer (absent when
// the qword there points into no module: `found` holds it), and the slots up
// to the first entry that is not code; with `parts`, the object's tables.
struct Vtable {
    Addr address;
    Location where;
    HexNum offset;
    std::optional<Addr> table;
    Location tableWhere;
    std::optional<std::string> stop;
    std::optional<HexNum> stopValue, found;
    std::vector<VtableSlot> rows;
    std::optional<std::vector<VtablePart>> parts;
    std::optional<VtableRtti> rtti;
    std::optional<std::string> rttiError;
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
        v("parts", &Vtable::parts);
        v("rtti", &Vtable::rtti);
        v("rtti_error", &Vtable::rttiError);
        v("chain", &Vtable::chain);
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
    std::optional<bool> unlisted;   // True: code outside every exception-table entry, bounds the piece's.
    template<class V> static void Describe(V& v) {
        v("function", &TouchRow::function);
        v("unlisted", &TouchRow::unlisted);
        v("distinct", &TouchRow::distinct);
        v("writes", &TouchRow::writes);
        v("total", &TouchRow::total);
        v("uses", &TouchRow::uses);
    }
};
struct TouchScanned {
    Addr from, to;
    uint64_t functions = 0, unlisted = 0, bytes = 0;  // unlisted: pieces outside every entry.
    template<class V> static void Describe(V& v) {
        v("scan_from", &TouchScanned::from);
        v("scan_to", &TouchScanned::to);
        v("functions", &TouchScanned::functions);
        v("unlisted", &TouchScanned::unlisted);
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
    std::optional<Chain> chain;
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
        v("chain", &Touches::chain);
    }
};
}

namespace shape {
struct DispHit {
    Addr address;
    HexNum rva;                         // In the scan's module (the result's module).
    HexNum disp;                        // Which of the asked displacements.
    std::string base;                   // The base register ("rdi").
    std::string access;
    uint64_t width = 0;
    std::string text;
    std::optional<HexNum> function;     // The holding function's start rva (exception table); absent outside every entry.
    template<class V> static void Describe(V& v) {
        v("address", &DispHit::address);
        v("rva", &DispHit::rva);
        v("disp", &DispHit::disp);
        v("base", &DispHit::base);
        v("access", &DispHit::access, "write|read|both|address");
        v("width", &DispHit::width);
        v("text", &DispHit::text);
        v("function", &DispHit::function);
    }
};
// same_base: a function using every asked displacement on one base register.
struct DispFunction {
    HexNum function;                    // Its start rva.
    std::string base;
    uint64_t hits = 0;                  // Its rows.
    template<class V> static void Describe(V& v) {
        v("function", &DispFunction::function);
        v("base", &DispFunction::base);
        v("hits", &DispFunction::hits);
    }
};
// disp-scan: one page of the sweep. scan_from and scan_to bound what this call read;
// scanned is the share of the module's code behind next_cursor (1 when done).
struct DispScan {
    std::vector<HexNum> disps;
    std::string access;
    std::string module;
    Addr from, to;
    double scanned = 0;
    uint64_t total = 0;
    std::vector<DispHit> rows;
    std::optional<std::vector<DispFunction>> functions;
    std::optional<Addr> nextCursor;
    int64_t scanMs = 0, readMs = 0;
    template<class V> static void Describe(V& v) {
        v("disps", &DispScan::disps);
        v("access", &DispScan::access);
        v("module", &DispScan::module);
        v("scan_from", &DispScan::from);
        v("scan_to", &DispScan::to);
        v("scanned", &DispScan::scanned);
        v("total", &DispScan::total);
        v("rows", &DispScan::rows);
        v("functions", &DispScan::functions);
        v("next_cursor", &DispScan::nextCursor);
        v("scan_ms", &DispScan::scanMs);
        v("read_ms", &DispScan::readMs);
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

// Compact unless `full` (`meta`): no `fields`, and no module name the listing
// already gives (the row's own, or a target's in the same module). `whole`
// (a `function` listing without `meta`) also drops bytes.
inline shape::Instruction MakeInstruction(const dis::Instruction& row, std::span<const Module> modules, bool full = true,
                                          bool whole = false) {
    shape::Instruction out;
    out.address = row.address;
    out.where = Locate(row.address, modules);
    if (full || !whole) out.bytes = HexBytes(row.bytes);
    out.text = row.text;
    if (row.target) {
        out.target = Addr(*row.target);
        out.targetWhere = Locate(*row.target, modules);
        if (!full && out.targetWhere.module == out.where.module) out.targetWhere.module.reset();
    }
    if (row.string) {
        out.string = *row.string;
        if (row.wide) out.wide = true;
    }
    out.import = row.import;
    if (full && !row.fields.empty()) {
        out.fields.emplace();
        for (const auto& field : row.fields) out.fields->push_back({field.offset, field.size, std::string(field.kind)});
    }
    if (row.ends) out.ends = true;
    if (row.value) {
        out.value = *row.value;
        out.valueType = std::string(row.valueType);
    } else if (!row.valueBytes.empty()) out.valueHex = HexBytes(row.valueBytes);
    if (!full) out.where.module.reset();
    return out;
}

// `whole`: a `function` listing. Without `full` its rows lose bytes and,
// past the first, their rva; it adds the strings summary either way.
inline shape::Disasm MakeDisasm(uint64_t address, const dis::Listing& listing,
                                const std::optional<unwind::Function>& function, std::span<const Module> modules,
                                bool full = true, bool whole = false) {
    shape::Disasm out;
    out.address = address;
    out.where = Locate(address, modules);
    auto& rows = out.rows.emplace();
    for (const auto& row : listing.rows) {
        rows.push_back(MakeInstruction(row, modules, full, whole));
        if (whole && !full && rows.size() > 1) rows.back().where.rva.reset();
        out.size += row.bytes.size();
    }
    if (whole) {
        out.strings.emplace();
        for (const auto& use : dis::Strings(listing.rows)) {
            shape::DisasmString item{use.text, std::nullopt, {}};
            if (use.wide) item.wide = true;
            for (const uint64_t at : use.at) item.at.push_back(Addr(at));
            out.strings->push_back(std::move(item));
        }
    }
    out.stop = listing.stop;
    if (listing.next) out.nextCursor = Addr(*listing.next);
    out.function = MakeFunction(function, modules);
    if (listing.end) out.end = shape::DisasmEnd{listing.end->address, listing.end->offset, listing.end->reason};
    if (!listing.followed.empty()) {
        out.followed.emplace();
        for (const auto& hop : listing.followed) out.followed->push_back({hop.from, hop.to, Locate(hop.to, modules)});
    }
    return out;
}

// disasm with `save`: the file holds the listing, so the result keeps what it says
// about it (bounds, size, why it stopped, the instruction count) and no rows.
inline void KeepSaveSummary(shape::Disasm& out, std::string file) {
    out.savedRows = out.rows ? out.rows->size() : 0;
    out.rows.reset();
    out.strings.reset();
    out.saved = std::move(file);
}

inline shape::Xrefs MakeXrefs(uint64_t address, const Module& scanned, std::span<const xref::Hit> hits,
                              std::span<const std::optional<unwind::Function>> functions, bool all, bool capped,
                              int64_t milliseconds, std::span<const Module> modules,
                              std::span<const std::optional<touch::Piece>> pieces = {}) {
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
        if (!row.function && i < pieces.size() && pieces[i]) {
            row.function = shape::FunctionBounds{pieces[i]->start, pieces[i]->end, Locate(pieces[i]->start, modules), {}};
            row.unlisted = true;
        }
        out.rows.push_back(std::move(row));
    }
    out.capped = capped;
    out.scanMs = milliseconds;
    return out;
}

// `address` and `offset` as vtable reads them: the part and where its table pointer sits.
inline shape::VtableRtti MakeVtableRtti(uint64_t address, uint64_t offset, const pe::Rtti& rtti) {
    shape::VtableRtti out;
    out.locator = rtti.locator;
    out.name = rtti.type.name;
    out.decorated = rtti.type.decorated;
    out.complete = address + offset - rtti.offset;
    out.completeOffset = rtti.offset;
    for (const auto& base : rtti.bases) out.bases.push_back({base.name, base.decorated});
    out.capped = rtti.capped;
    return out;
}

inline std::vector<shape::VtableSlot> MakeVtableRows(std::span<const native::Slot> slots, std::span<const Module> modules) {
    std::vector<shape::VtableSlot> rows;
    for (const auto& slot : slots) {
        shape::VtableSlot row{slot.slot, HexNum(slot.slot * 8), slot.target, Locate(slot.target, modules), {}, {}};
        if (slot.body) {
            row.body = Addr(*slot.body);
            row.bodyWhere = Locate(*slot.body, modules);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

// vtable with `parts`: each table found in the object, with its picked slots.
inline shape::VtablePart MakeVtablePart(std::span<const uint64_t> offsets, uint64_t table,
                                        std::span<const native::Slot> slots, std::span<const Module> modules) {
    shape::VtablePart part{{}, Addr(table), Locate(table, modules), MakeVtableRows(slots, modules)};
    for (const uint64_t offset : offsets) part.offsets.push_back(HexNum(offset));
    return part;
}

inline shape::Vtable MakeVtable(uint64_t address, uint64_t offset, std::optional<uint64_t> table,
                                std::optional<uint64_t> found, const native::Slots& slots,
                                std::span<const Module> modules) {
    shape::Vtable out;
    out.address = address;
    out.where = Locate(address, modules);
    out.offset = offset;
    out.rows = MakeVtableRows(slots.slots, modules);
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

// The memory an address lies in, for read and peek; `kind` is native::region::Kind's.
inline shape::Region MakeRegion(const RegionInfo& info, std::string_view kind) {
    return {std::string(kind), info.base, info.size, native::region::Protection(info.protect)};
}

inline shape::Touches MakeTouches(uint64_t address, std::span<const uint32_t> offsets, uint64_t range, uint64_t from,
                                  uint64_t to, size_t entries, size_t unlisted, size_t bytes,
                                  const std::vector<touch::Row>& rows, int64_t ms, std::span<const Module> modules) {
    shape::Touches out;
    out.address = address;
    out.where = Locate(address, modules);
    for (const uint32_t offset : offsets) out.offsets.push_back(HexNum(offset));
    out.range = range;
    out.scanned = {from, to, entries, unlisted, bytes};
    out.total = rows.size();
    for (size_t i = 0; i < rows.size() && i < touch::MaxRows; ++i) {
        const auto& row = rows[i];
        shape::TouchRow item;
        item.function = {row.start, row.end, Locate(row.start, modules), {}};
        if (!row.listed) item.unlisted = true;
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
