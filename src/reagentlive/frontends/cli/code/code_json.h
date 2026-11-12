// reagentlive CLI: fast-lane and native-object results as JSON (disasm, xrefs,
// function bounds, vtable).
// No I/O, so tests pin the keys against the table in docs/commands/results.md
// ("Result shapes").
#pragma once
#include <cstdint>
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
// {start, end, module, rva} of the function entry, plus `chunk` {start, end}
// when the address lies in a continuation; null without an entry.
inline Json FunctionJson(const std::optional<unwind::Function>& function, std::span<const Module> modules) {
    if (!function) return nullptr;
    Json json = {{"start", format_detail::Pointer(function->range.start)},
        {"end", format_detail::Pointer(function->range.end)}};
    AddLocation(json, function->range.start, modules);
    if (function->chunk)
        json["chunk"] = {{"start", format_detail::Pointer(function->chunk->start)},
            {"end", format_detail::Pointer(function->chunk->end)}};
    return json;
}

inline Json InstructionJson(const dis::Instruction& row, std::span<const Module> modules) {
    Json json = {{"address", format_detail::Pointer(row.address)}, {"bytes", format_detail::Hex(row.bytes, row.bytes.size())},
        {"text", row.text}};
    AddLocation(json, row.address, modules);
    if (row.target) {
        json["target"] = format_detail::Pointer(*row.target);
        AddLocation(json, *row.target, modules, "target_");
    }
    if (row.string) {
        json["string"] = *row.string;
        if (row.wide) json["wide"] = true;
    }
    if (!row.fields.empty()) {
        Json fields = Json::array();
        for (const auto& field : row.fields)
            fields.push_back({{"offset", field.offset}, {"size", field.size}, {"kind", std::string(field.kind)}});
        json["fields"] = std::move(fields);
    }
    if (row.ends) json["ends"] = true;
    return json;
}

// disasm: the rows, why the listing stopped, the hops --follow took, and the
// function entry holding the first address (null without one).
inline Json DisasmJson(uint64_t address, const dis::Listing& listing, const std::optional<unwind::Function>& function,
                       std::span<const Module> modules) {
    Json rows = Json::array();
    size_t bytes = 0;
    for (const auto& row : listing.rows) {
        rows.push_back(InstructionJson(row, modules));
        bytes += row.bytes.size();
    }
    Json json = {{"address", format_detail::Pointer(address)}, {"count", listing.rows.size()}, {"bytes", bytes},
        {"stop", listing.stop}, {"instructions", std::move(rows)}, {"function", FunctionJson(function, modules)}};
    AddLocation(json, address, modules);
    if (listing.end)
        json["end"] = {{"address", format_detail::Pointer(listing.end->address)}, {"offset", listing.end->offset},
            {"reason", listing.end->reason}};
    if (!listing.followed.empty()) {
        Json hops = Json::array();
        for (const auto& hop : listing.followed) {
            Json row = {{"from", format_detail::Pointer(hop.from)}, {"to", format_detail::Pointer(hop.to)}};
            AddLocation(row, hop.to, modules, "to_");
            hops.push_back(std::move(row));
        }
        json["followed"] = std::move(hops);
    }
    return json;
}

// xrefs: hits per kind over every hit, and the rows (unknown ones only with
// --all). Each row reports the instruction where the kind fixes it, else the
// field, with that address's function entry (null without one).
inline Json XrefsJson(uint64_t address, const Module& scanned, std::span<const xref::Hit> hits,
                      std::span<const std::optional<unwind::Function>> functions, bool all, bool truncated,
                      int64_t milliseconds, std::span<const Module> modules) {
    Json counts = Json::object();
    for (const auto kind : xref::Kinds) counts[std::string(kind)] = 0;
    Json rows = Json::array();
    for (size_t i = 0; i < hits.size(); ++i) {
        const auto& hit = hits[i];
        counts[std::string(hit.kind)] = counts[std::string(hit.kind)].get<size_t>() + 1;
        if (hit.kind == "unknown" && !all) continue;
        const uint64_t at = hit.instruction.value_or(hit.field);
        Json row = {{"kind", std::string(hit.kind)}, {"field", format_detail::Pointer(hit.field)},
            {"function", FunctionJson(i < functions.size() ? functions[i] : std::nullopt, modules)}};
        if (hit.instruction) row["instruction"] = format_detail::Pointer(*hit.instruction);
        AddLocation(row, at, modules);
        rows.push_back(std::move(row));
    }
    Json json = {{"address", format_detail::Pointer(address)}, {"scanned", scanned.name}, {"count", std::move(counts)},
        {"shown", rows.size()}, {"rows", std::move(rows)}, {"truncated", truncated}, {"scan_ms", milliseconds}};
    AddLocation(json, address, modules);
    return json;
}

// vtable: the object (or the table itself with --table), the table pointer
// (null when the qword there points into no module: `found` holds it), and
// the slots up to the first entry that is not code.
inline Json VtableJson(uint64_t address, uint64_t offset, std::optional<uint64_t> table, std::optional<uint64_t> found,
                       const native::Slots& slots, std::span<const Module> modules) {
    Json rows = Json::array();
    for (const auto& slot : slots.slots) {
        Json row = {{"slot", slot.slot}, {"offset", HexNumber(slot.slot * 8)}, {"target", format_detail::Pointer(slot.target)}};
        AddLocation(row, slot.target, modules);
        if (slot.body) {
            row["body"] = format_detail::Pointer(*slot.body);
            AddLocation(row, *slot.body, modules, "body_");
        }
        rows.push_back(std::move(row));
    }
    Json json = {{"address", format_detail::Pointer(address)}, {"offset", HexNumber(offset)},
        {"table", table ? Json(format_detail::Pointer(*table)) : Json(nullptr)}, {"count", slots.slots.size()},
        {"slots", std::move(rows)}};
    AddLocation(json, address, modules);
    if (table) {
        AddLocation(json, *table, modules, "table_");
        json["stop"] = slots.stop;
        if (slots.stopValue) json["stop_value"] = HexNumber(*slots.stopValue);
    } else if (found) {
        json["found"] = HexNumber(*found);
    }
    return json;
}

// A resolved pointer chain: the base, each hop (where a pointer was read and
// what it held), the final address, and the broken hop's index and reason.
inline Json ChainJson(uint64_t base, const PointerChain& chain, std::span<const Module> modules) {
    Json hops = Json::array();
    for (size_t i = 0; i < chain.hops.size(); ++i) {
        const auto& hop = chain.hops[i];
        Json row = {{"hop", i}, {"address", format_detail::Pointer(hop.address)}, {"value", HexNumber(hop.value)}};
        AddLocation(row, hop.address, modules);
        hops.push_back(std::move(row));
    }
    Json json = {{"base", format_detail::Pointer(base)}, {"hops", std::move(hops)}};
    if (chain.broken) {
        json["broken"] = *chain.broken;
        json["reason"] = chain.reason;
    } else {
        json["address"] = format_detail::Pointer(chain.address);
        AddLocation(json, chain.address, modules);
    }
    return json;
}

// region: the VirtualQuery facts and a one-word kind; `module`, `rva` and
// `section` inside an image.
inline Json RegionJson(uint64_t address, const RegionInfo& info, std::string_view kind, std::string_view section,
                       std::span<const Module> modules) {
    Json json = {{"address", format_detail::Pointer(address)}, {"kind", std::string(kind)},
        {"base", format_detail::Pointer(info.base)}, {"size", HexNumber(info.size)},
        {"allocation_base", format_detail::Pointer(info.allocationBase)},
        {"state", std::string(native::region::State(info.state))}, {"type", std::string(native::region::Type(info.type))},
        {"protect", native::region::Protection(info.protect)},
        {"allocation_protect", native::region::Protection(info.allocationProtect)}};
    AddLocation(json, address, modules);
    if (!section.empty()) json["section"] = std::string(section);
    return json;
}
// touches: the asked offsets, what was scanned, and the ranked functions with
// their uses (each capped at MaxUsesPerRow).
inline Json TouchesJson(uint64_t address, std::span<const uint32_t> offsets, uint64_t range, uint64_t from, uint64_t to,
                        size_t entries, size_t bytes, const std::vector<touch::Row>& rows, long long ms,
                        std::span<const Module> modules) {
    Json asked = Json::array();
    for (const uint32_t offset : offsets) asked.push_back(format_detail::Pointer(offset));
    Json list = Json::array();
    for (size_t i = 0; i < rows.size() && i < touch::MaxRows; ++i) {
        const auto& row = rows[i];
        Json function = {{"start", format_detail::Pointer(row.start)}, {"end", format_detail::Pointer(row.end)}};
        AddLocation(function, row.start, modules);
        Json uses = Json::array();
        for (size_t j = 0; j < row.uses.size() && j < touch::MaxUsesPerRow; ++j) {
            const auto& use = row.uses[j];
            Json item = {{"offset", format_detail::Pointer(use.offset)}, {"instruction", format_detail::Pointer(use.instruction)},
                {"write", use.write}, {"text", use.text}};
            AddLocation(item, use.instruction, modules);
            uses.push_back(std::move(item));
        }
        list.push_back({{"function", std::move(function)}, {"distinct", row.distinct}, {"writes", row.writes},
            {"count", row.uses.size()}, {"uses", std::move(uses)}});
    }
    Json json = {{"address", format_detail::Pointer(address)}, {"offsets", std::move(asked)}, {"range", range},
        {"scanned", {{"from", format_detail::Pointer(from)}, {"to", format_detail::Pointer(to)}, {"functions", entries},
            {"bytes", bytes}}},
        {"count", rows.size()}, {"rows", std::move(list)}, {"truncated", rows.size() > touch::MaxRows}, {"scan_ms", ms}};
    AddLocation(json, address, modules);
    return json;
}
}
