// reagentlive CLI: sig results as typed structs and the lines of a patterns
// file (sig-file). No I/O; the conventions are in results/result_json.h.
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
namespace shape {
// A fixed field of a pattern that holds an address (a patch moves it).
struct SigField {
    uint64_t offset = 0, size = 0;
    std::string kind;
    Addr target;
    Location targetWhere;
    template<class V> static void Describe(V& v) {
        v("offset", &SigField::offset);
        v("size", &SigField::size);
        v("kind", &SigField::kind, "call|jump|branch|rip|absolute");
        v("target", &SigField::target);
        v.flat("target_", &SigField::targetWhere);
    }
};
struct SigHit {
    Addr address;
    Location where;
    std::optional<bool> reference;
    std::optional<uint64_t> divergesAt;
    std::optional<Addr> resolved;
    Location resolvedWhere;
    template<class V> static void Describe(V& v) {
        v("address", &SigHit::address);
        v.flat("", &SigHit::where);
        v("reference", &SigHit::reference);
        v("diverges_at", &SigHit::divergesAt);
        v("resolved", &SigHit::resolved);
        v.flat("resolved_", &SigHit::resolvedWhere);
    }
};
struct SigAnchor {
    uint64_t offset = 0, length = 0, hits = 0;
    template<class V> static void Describe(V& v) {
        v("offset", &SigAnchor::offset);
        v("length", &SigAnchor::length);
        v("hits", &SigAnchor::hits);
    }
};
struct SigResolve {
    uint64_t next = 0, field = 0, size = 0;
    template<class V> static void Describe(V& v) {
        v("next", &SigResolve::next);
        v("field", &SigResolve::field);
        v("size", &SigResolve::size);
    }
};
struct SigMismatch {
    uint64_t offset = 0;
    HexNum wanted, found;
    template<class V> static void Describe(V& v) {
        v("offset", &SigMismatch::offset);
        v("wanted", &SigMismatch::wanted);
        v("found", &SigMismatch::found);
    }
};
// sig-check: the pattern in both forms, the module it was checked in, its
// matches (`total`; `rows` shows the first MaxShownHits) and what to change.
// Optional keys only where they apply: expect and resolves with --expect,
// mismatches when the expected address holds no match (mismatch_error when
// its bytes are unreadable), unique_at_length for several matches that all diverge.
struct SigCheck {
    std::string pattern, list, module;
    bool unique = false;
    uint64_t total = 0;
    std::vector<SigHit> rows;
    uint64_t length = 0, fixedBytes = 0;
    SigAnchor anchor;
    std::vector<SigField> warnings;
    std::optional<uint64_t> decoded, uniqueAtLength;
    std::optional<SigResolve> resolve;
    std::optional<Addr> expect;
    Location expectWhere;
    std::optional<uint64_t> offset;
    std::optional<bool> resolves;
    std::optional<std::vector<SigMismatch>> mismatches;
    std::optional<std::string> mismatchError;
    template<class V> static void Describe(V& v) {
        v("pattern", &SigCheck::pattern);
        v("list", &SigCheck::list);
        v("module", &SigCheck::module);
        v("unique", &SigCheck::unique);
        v("total", &SigCheck::total);
        v("rows", &SigCheck::rows);
        v("length", &SigCheck::length);
        v("fixed_bytes", &SigCheck::fixedBytes);
        v("anchor", &SigCheck::anchor);
        v("warnings", &SigCheck::warnings);
        v("decoded", &SigCheck::decoded);
        v("unique_at_length", &SigCheck::uniqueAtLength);
        v("resolve", &SigCheck::resolve);
        v("expect", &SigCheck::expect);
        v.flat("expect_", &SigCheck::expectWhere);
        v("offset", &SigCheck::offset);
        v("resolves", &SigCheck::resolves);
        v("mismatches", &SigCheck::mismatches);
        v("mismatch_error", &SigCheck::mismatchError);
    }
};
struct SigFileRow {
    uint64_t line = 0;
    bool ok = false;
    std::optional<SigCheck> result;
    std::optional<std::string> error;
    template<class V> static void Describe(V& v) {
        v("line", &SigFileRow::line);
        v("ok", &SigFileRow::ok);
        v("result", &SigFileRow::result);
        v("error", &SigFileRow::error);
    }
};
// sig-file: one row per signature; `winner` is the line of the first that holds.
struct SigFile {
    std::string file;
    std::optional<uint64_t> winner;
    uint64_t searches = 0;
    std::vector<SigFileRow> rows;
    template<class V> static void Describe(V& v) {
        v("file", &SigFile::file);
        v("winner", &SigFile::winner);
        v("searches", &SigFile::searches);
        v("rows", &SigFile::rows);
    }
};
struct SigSuggested {
    std::string pattern, list;
    uint64_t fixedBytes = 0;
    std::vector<SigField> fields;
    std::optional<uint64_t> decoded;
    template<class V> static void Describe(V& v) {
        v("pattern", &SigSuggested::pattern);
        v("list", &SigSuggested::list);
        v("fixed_bytes", &SigSuggested::fixedBytes);
        v("fields", &SigSuggested::fields);
        v("decoded", &SigSuggested::decoded);
    }
};
// sig-bytes: the live bytes in both forms, and the same bytes with their
// address fields wildcarded.
struct SigBytes {
    Addr address;
    Location where;
    uint64_t length = 0;
    std::string pattern, list;
    SigSuggested suggested;
    template<class V> static void Describe(V& v) {
        v("address", &SigBytes::address);
        v.flat("", &SigBytes::where);
        v("length", &SigBytes::length);
        v("pattern", &SigBytes::pattern);
        v("list", &SigBytes::list);
        v("suggested", &SigBytes::suggested);
    }
};
}

inline std::vector<shape::SigField> MakeSigFields(std::span<const sig::AddressField> fields, std::span<const Module> modules) {
    std::vector<shape::SigField> rows;
    for (const auto& field : fields)
        rows.push_back({field.offset, field.size, field.kind, field.target, Locate(field.target, modules)});
    return rows;
}

inline shape::SigCheck MakeSigCheck(const sig::Check& check, const sig::CheckResult& result, const Module& module,
                                    std::span<const Module> modules) {
    shape::SigCheck out;
    for (size_t i = 0; i < result.matches.size() && i < sig::MaxShownHits; ++i) {
        const auto& match = result.matches[i];
        shape::SigHit row;
        row.address = match.address;
        row.where = Locate(match.address, modules);
        if (result.matches.size() > 1) {
            if (result.reference == i) row.reference = true;
            else if (match.divergesAt) row.divergesAt = *match.divergesAt;
        }
        if (check.resolve && match.resolved) {
            row.resolved = Addr(*match.resolved);
            row.resolvedWhere = Locate(*match.resolved, modules);
        }
        out.rows.push_back(std::move(row));
    }
    out.pattern = sig::Text(check.pattern);
    out.list = sig::List(check.pattern);
    out.module = module.name;
    out.unique = result.matches.size() == 1;
    out.total = result.matches.size();
    out.length = check.pattern.Size();
    out.fixedBytes = check.pattern.Fixed();
    out.anchor = {result.anchor.offset, result.anchor.length, result.anchorHits};
    out.warnings = MakeSigFields(result.warnings, modules);
    if (result.decoded) out.decoded = *result.decoded;
    if (result.matches.size() > 1 && result.uniqueAtLength) out.uniqueAtLength = *result.uniqueAtLength;
    if (check.resolve) out.resolve = shape::SigResolve{check.resolve->next, check.resolve->field, check.resolve->size};
    if (check.expect) {
        out.expect = Addr(*check.expect);
        out.expectWhere = Locate(*check.expect, modules);
        if (!check.resolve) out.offset = check.offset;
        out.resolves = *result.resolves;
    }
    if (result.compared) {
        if (!result.mismatches) out.mismatchError = "the bytes at the expected address are unreadable";
        else {
            out.mismatches.emplace();
            for (const auto& m : *result.mismatches) out.mismatches->push_back({m.offset, HexNum(m.wanted), HexNum(m.found)});
        }
    }
    return out;
}

// `exact`: the fields the console's decoder found (and how many bytes it
// read) in place of the byte heuristic.
struct ExactFields { std::vector<sig::AddressField> fields; size_t decoded = 0; };
inline shape::SigBytes MakeSigBytes(uint64_t address, std::span<const uint8_t> bytes, std::span<const Module> modules,
                                    const std::optional<ExactFields>& exact = std::nullopt) {
    const auto pattern = sig::FromBytes(bytes);
    const auto fields = exact ? exact->fields : sig::AddressFields(pattern, address, modules);
    const auto suggested = sig::Masked(pattern, fields);
    shape::SigBytes out;
    out.address = address;
    out.where = Locate(address, modules);
    out.length = bytes.size();
    out.pattern = sig::Text(pattern);
    out.list = sig::List(pattern);
    out.suggested = {sig::Text(suggested), sig::List(suggested), suggested.Fixed(), MakeSigFields(fields, modules), {}};
    if (exact) out.suggested.decoded = exact->decoded;
    return out;
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
