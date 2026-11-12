// reagentlive CLI: sig results as JSON and the lines of a patterns file
// (sig --check --file). No I/O, so tests pin the keys against the table in
// docs/commands/results.md ("Result shapes").
#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include "reagentlive/frontends/cli/values/json.h"
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/frontends/cli/code/signature.h"

namespace reagentlive::cli {
inline Json SigFieldJson(const sig::AddressField& field, std::span<const Module> modules) {
    Json row = {{"offset", field.offset}, {"size", field.size}, {"kind", field.kind},
        {"target", format_detail::Pointer(field.target)}};
    AddLocation(row, field.target, modules, "target_");
    return row;
}
inline Json SigFieldsJson(std::span<const sig::AddressField> fields, std::span<const Module> modules) {
    Json rows = Json::array();
    for (const auto& field : fields) rows.push_back(SigFieldJson(field, modules));
    return rows;
}

// sig --check: the pattern in both forms, the module it was checked in, its
// matches (the first MaxShownHits) and what to change. Optional keys only
// where they apply: expect/resolves with --expect, mismatches when the
// expected address holds no match, unique_at_length for several matches.
inline Json SigCheckJson(const sig::Check& check, const sig::CheckResult& result, const Module& module,
                         std::span<const Module> modules) {
    Json hits = Json::array();
    for (size_t i = 0; i < result.matches.size() && i < sig::MaxShownHits; ++i) {
        const auto& match = result.matches[i];
        Json row = {{"address", format_detail::Pointer(match.address)}};
        AddLocation(row, match.address, modules);
        if (result.matches.size() > 1) {
            if (result.reference == i) row["reference"] = true;
            else row["diverges_at"] = match.divergesAt ? Json(*match.divergesAt) : Json(nullptr);
        }
        if (check.resolve) {
            row["resolved"] = match.resolved ? Json(format_detail::Pointer(*match.resolved)) : Json(nullptr);
            if (match.resolved) AddLocation(row, *match.resolved, modules, "resolved_");
        }
        hits.push_back(std::move(row));
    }
    Json json = {{"pattern", sig::Text(check.pattern)}, {"list", sig::List(check.pattern)}, {"module", module.name},
        {"unique", result.matches.size() == 1}, {"count", result.matches.size()}, {"hits", std::move(hits)},
        {"length", check.pattern.Size()}, {"fixed_bytes", check.pattern.Fixed()},
        {"anchor", {{"offset", result.anchor.offset}, {"length", result.anchor.length}, {"hits", result.anchorHits}}},
        {"warnings", SigFieldsJson(result.warnings, modules)}};
    if (result.decoded) json["decoded"] = *result.decoded;
    if (result.matches.size() > 1)
        json["unique_at_length"] = result.uniqueAtLength ? Json(*result.uniqueAtLength) : Json(nullptr);
    if (check.resolve)
        json["resolve"] = {{"next", check.resolve->next}, {"field", check.resolve->field}, {"size", check.resolve->size}};
    if (check.expect) {
        json["expect"] = format_detail::Pointer(*check.expect);
        AddLocation(json, *check.expect, modules, "expect_");
        if (!check.resolve) json["offset"] = check.offset;
        json["resolves"] = *result.resolves;
    }
    if (result.compared) {
        if (!result.mismatches) json["mismatches"] = nullptr;
        else {
            Json rows = Json::array();
            for (const auto& m : *result.mismatches)
                rows.push_back({{"offset", m.offset}, {"wanted", HexNumber(m.wanted)}, {"found", HexNumber(m.found)}});
            json["mismatches"] = std::move(rows);
        }
    }
    return json;
}

// sig --bytes: the live bytes in both forms, and the same bytes with the
// fields the address heuristic recognises wildcarded.
// `exact`: the fields the console's decoder found (and how many bytes it
// read) in place of the byte heuristic.
struct ExactFields { std::vector<sig::AddressField> fields; size_t decoded = 0; };
inline Json SigBytesJson(uint64_t address, std::span<const uint8_t> bytes, std::span<const Module> modules,
                         const std::optional<ExactFields>& exact = std::nullopt) {
    const auto pattern = sig::FromBytes(bytes);
    const auto fields = exact ? exact->fields : sig::AddressFields(pattern, address, modules);
    const auto suggested = sig::Masked(pattern, fields);
    Json json = {{"address", format_detail::Pointer(address)}, {"length", bytes.size()},
        {"pattern", sig::Text(pattern)}, {"list", sig::List(pattern)},
        {"suggested", {{"pattern", sig::Text(suggested)}, {"list", sig::List(suggested)},
            {"fixed_bytes", suggested.Fixed()}, {"fields", SigFieldsJson(fields, modules)}}}};
    if (exact) json["suggested"]["decoded"] = exact->decoded;
    AddLocation(json, address, modules);
    return json;
}

// One line of a patterns file: {"pattern": "48 8b ?? 89" or [0x48, -1, ...],
// "module": "engine.dll", "rva": "0x1a2b3c" or a number, "offset": N,
// "resolve": "next,field[,size]" or [next, field, size]}; all but pattern optional.
struct SigEntry {
    sig::Pattern pattern;
    std::string module;             // Empty: the exe.
    std::optional<uint64_t> rva;    // Where it should land, in the module.
    uint64_t offset = 0;
    std::optional<sig::Resolve> resolve;
};
inline std::expected<SigEntry, std::string> ParseSigLine(std::string_view line) {
    const auto json = Json::parse(line, nullptr, false);
    if (json.is_discarded() || !json.is_object()) return std::unexpected("not a JSON object");
    SigEntry entry;
    const auto number = [](const Json& value, uint64_t& out) {
        if (value.is_number_unsigned()) { out = value.get<uint64_t>(); return true; }
        if (!value.is_string()) return false;
        uint64_t n = 0;
        if (!text::Number(value.get<std::string>(), n)) return false;
        out = n;
        return true;
    };
    for (const auto& [key, value] : json.items()) {
        if (key == "pattern") {
            std::string text;
            if (value.is_string()) text = value.get<std::string>();
            else if (value.is_array()) {
                for (const auto& item : value) {
                    if (!item.is_number_integer() || item.get<int64_t>() < -1 || item.get<int64_t>() > 0xff)
                        return std::unexpected("pattern array items are bytes or -1");
                    if (!text.empty()) text += ", ";
                    text += std::to_string(item.get<int64_t>());
                }
                if (value.size() == 1) text += ","; // Keep the list form.
            } else return std::unexpected("pattern is a string or an array");
            auto pattern = sig::ParsePattern(text);
            if (!pattern) return std::unexpected(pattern.error());
            entry.pattern = std::move(*pattern);
        } else if (key == "module") {
            if (!value.is_string()) return std::unexpected("module is a string");
            entry.module = value.get<std::string>();
        } else if (key == "rva") {
            uint64_t rva = 0;
            if (!number(value, rva)) return std::unexpected("rva is a number or a 0x string");
            entry.rva = rva;
        } else if (key == "offset") {
            if (!number(value, entry.offset) || entry.offset > UINT32_MAX) return std::unexpected("offset is a number");
        } else if (key == "resolve") {
            std::string text;
            if (value.is_string()) text = value.get<std::string>();
            else if (value.is_array()) {
                for (const auto& item : value) {
                    if (!item.is_number_unsigned()) return std::unexpected("resolve items are numbers");
                    if (!text.empty()) text += ',';
                    text += std::to_string(item.get<uint64_t>());
                }
            } else return std::unexpected("resolve is a string or an array");
            auto resolve = sig::ParseResolve(text);
            if (!resolve) return std::unexpected(resolve.error());
            entry.resolve = *resolve;
        } else {
            return std::unexpected("unknown key '" + key + "'");
        }
    }
    if (entry.pattern.bytes.empty()) return std::unexpected("no pattern");
    if (auto anchor = sig::ChooseAnchor(entry.pattern); !anchor) return std::unexpected(anchor.error());
    if (entry.offset && entry.resolve) return std::unexpected("offset and resolve do not combine");
    if (entry.offset && !entry.rva) return std::unexpected("offset needs an rva");
    return entry;
}
}
