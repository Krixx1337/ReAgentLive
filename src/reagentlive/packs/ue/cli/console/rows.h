// reagentlive CLI console, Unreal pack: rows of structs read from engine
// memory and decoded with their reflected struct (a call's returned arrays,
// a data table's rows), and the --fields tree that picks their members.
// For packs/ue/cli/console/ only.
#pragma once
#include <algorithm>
#include <cstring>
#include <deque>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "reagentlive/frontends/cli/syntax/text.h"
#include "reagentlive/packs/ue/cli/values/pack_input.h"
#include "reagentlive/packs/ue/cli/console/internal_ue.h"

namespace reagentlive::cli::detail {
// --fields a,b.c as a tree: the members kept at one level, each with the
// paths kept below it (empty: all of it).
struct RowPaths {
    std::vector<std::string> names;
    std::vector<RowPaths> below;
    bool Empty() const { return names.empty(); }
    const RowPaths* Find(const ue::Field& member, const std::string& key) const {
        for (size_t i = 0; i < names.size(); ++i)
            if (text::SameText(names[i], member.name) || text::SameText(names[i], key)) return &below[i];
        return nullptr;
    }
};
inline RowPaths ParsePaths(std::span<const std::string> paths) {
    RowPaths out;
    for (const auto& path : paths) {
        RowPaths* level = &out;
        std::string_view rest = path;
        while (!rest.empty()) {
            const auto dot = rest.find('.');
            const std::string name(rest.substr(0, dot));
            rest = dot == std::string_view::npos ? std::string_view{} : rest.substr(dot + 1);
            auto at = std::ranges::find_if(level->names, [&](const std::string& n) { return text::SameText(n, name); });
            if (at == level->names.end()) {
                level->names.push_back(name);
                level->below.emplace_back();
                at = std::prev(level->names.end());
            }
            level = &level->below[static_cast<size_t>(at - level->names.begin())];
        }
    }
    return out;
}

// An array's element as a field of its own.
inline ue::Field Element(const ue::Field& array) {
    ue::Field element{};
    element.name = array.name;
    element.size = array.elementSize;
    element.type = array.elementType;
    element.structType = array.structType;
    element.structName = array.structName;
    return element;
}

// Values read through engine memory and decoded with their reflected
// structs: a call's returned arrays and a data table's rows (paged), and the
// compact values of get, each and dump. Arrays nest (each read from the
// engine, at most `nested` elements, then `truncated`), structs expand
// member by member, a tag container is the list of its tags, and text is
// converted by the engine. Rendered twice: once collecting names and texts,
// once with them resolved (ResolveTexts between); element reads are cached
// between the two.
class RowReader {
public:
    static constexpr uint32_t MaxNested = 256;    // Elements per nested array in rows.
    static constexpr uint32_t PreviewNested = 8;  // In get, each and dump, as dump's previews.
    static constexpr unsigned MaxDepth = 3;       // Arrays inside arrays.
    static constexpr unsigned MaxStructs = 4;     // Structs inside structs.
    static constexpr size_t MaxBytes = 16u << 20; // All element reads of one command.

    RowReader(Session& session, StructCache& structs, uint32_t nested = MaxNested)
        : session_(session), structs_(structs), nested_(nested) {}

    // Why a path names no member, or empty.
    std::string Check(const ue::Field& element, const RowPaths& paths, unsigned depth = 0) {
        if (paths.Empty()) return {};
        const auto* members = element.type == "StructProperty" ? structs_(element.structType) : nullptr;
        if (!members) return element.name + " has no members to pick (" + element.type + ")";
        for (size_t i = 0; i < paths.names.size(); ++i) {
            // A member by its reflected name or the readable one rows print.
            const ValueStyle style{false, &session_.names};
            const auto found = std::ranges::find_if(*members, [&](const ue::Field& m) {
                return text::SameText(m.name, paths.names[i]) ||
                    text::SameText(MemberKey(m, style, Json::object()), paths.names[i]); });
            if (found == members->end()) {
                std::string known;
                for (const auto& m : *members) known += (known.empty() ? "" : ", ") + MemberKey(m, style, Json::object());
                return "no member " + paths.names[i] + " in " + element.structName + " (members: " + known + ")";
            }
            if (paths.below[i].Empty()) continue;
            if (depth >= MaxDepth) return "--fields goes " + std::to_string(MaxDepth) + " levels deep";
            const ue::Field next = found->type == "ArrayProperty" ? Element(*found) : *found;
            if (auto why = Check(next, paths.below[i], depth + 1); !why.empty()) return why;
        }
        return {};
    }

    // A returned array as {count, capacity, address, rows, next_cursor}.
    template<class Names>
    Json Array(const ue::Field& array, std::span<const uint8_t> header, uint32_t from, uint32_t limit,
               const RowPaths& paths, const Names& names, bool& truncated, unsigned depth = 0) {
        ue::ArrayHeader h{};
        if (header.size() != sizeof(h)) return nullptr;
        memcpy(&h, header.data(), sizeof(h));
        Json out = {{"count", h.count}, {"capacity", h.capacity},
            {"address", h.data ? Json(format_detail::Pointer(h.data)) : Json(nullptr)}};
        const ue::Field element = Element(array);
        if (h.count < 0 || h.capacity < h.count || !element.size || (h.count && !h.data)) {
            out["rows_error"] = "not a valid array header";
            truncated = true;
            return out;
        }
        const auto count = static_cast<uint32_t>(h.count);
        const uint32_t first = std::min(from, count);
        const uint32_t shown = limit ? std::min(limit, count - first) : count - first;
        Json rows = Json::array();
        if (shown) {
            const auto* bytes = Elements(h.data + uint64_t{first} * element.size, size_t{shown} * element.size);
            if (!bytes) {
                out["rows_error"] = error_.empty() ? "elements unreadable" : error_;
                truncated = true;
                return out;
            }
            for (uint32_t i = 0; i < shown; ++i)
                rows.push_back(Value(element, std::span(*bytes).subspan(size_t{i} * element.size, element.size), paths,
                    names, truncated, depth, 0));
        }
        out["rows"] = std::move(rows);
        if (first) out["start"] = first;
        if (first + shown < count) {
            if (depth) { out["truncated"] = true; truncated = true; }
            else out["next_cursor"] = first + shown;
        }
        return out;
    }

    // Text values met so far, converted by the engine (TextBatch).
    void ResolveTexts() { texts_.Resolve(session_); }

    // One value: a field of an object (get, each, dump) or a struct row
    // (rows). `depth` counts arrays above it, `nest` structs.
    template<class Names>
    Json Value(const ue::Field& field, std::span<const uint8_t> bytes, const RowPaths& paths, const Names& names,
               bool& truncated, unsigned depth = 0, unsigned nest = 0) {
        const ValueStyle style{false, &session_.names};
        if (field.type == "TextProperty") {
            texts_.Add(field, bytes);
            return texts_.Value(field, bytes, JsonFieldValue(field, bytes, structs_, names, truncated, style, 1));
        }
        if (field.type == "ArrayProperty" && depth < MaxDepth && bytes.size() == sizeof(ue::ArrayHeader))
            return Array(field, bytes, 0, nested_, paths, names, truncated, depth + 1);
        const auto* members = field.type == "StructProperty" && nest < MaxStructs ? structs_(field.structType) : nullptr;
        if (!members || members->empty() || pack_input::LoneName(*members))
            return JsonFieldValue(field, bytes, structs_, names, truncated, style, 1);
        if (paths.Empty())
            if (const auto* tags = TagList(*members)) return Tags(*tags, bytes, names, truncated, depth);
        Json row = Json::object();
        for (const auto& member : *members) {
            const auto key = MemberKey(member, style, row);
            const RowPaths* below = paths.Empty() ? &paths : paths.Find(member, key);
            if (!below) continue;
            if (!member.size || size_t{member.offset} + member.size > bytes.size()) { row[key] = nullptr; continue; }
            row[key] = Value(member, bytes.subspan(member.offset, member.size), *below, names, truncated, depth, nest + 1);
        }
        return row;
    }

private:
    // A tag container: every member an array of single-name structs (its
    // tags, then the implied parents). Its first array, or null.
    const ue::Field* TagList(const std::vector<ue::Field>& members) {
        for (const auto& member : members) {
            if (member.type != "ArrayProperty" || member.elementType != "StructProperty") return nullptr;
            const auto* element = structs_(member.structType);
            if (!element || !pack_input::LoneName(*element)) return nullptr;
        }
        return &members.front();
    }
    // The container's own tags as a list of names.
    template<class Names>
    Json Tags(const ue::Field& array, std::span<const uint8_t> bytes, const Names& names, bool& truncated, unsigned depth) {
        if (size_t{array.offset} + sizeof(ue::ArrayHeader) > bytes.size()) return nullptr;
        bool partial = false;
        auto rows = Array(array, bytes.subspan(array.offset, sizeof(ue::ArrayHeader)), 0, nested_, {}, names, partial,
            depth + 1);
        truncated |= partial;
        if (!rows.is_object() || !rows.contains("rows")) return rows;
        return rows["rows"];
    }

    const std::vector<uint8_t>* Elements(uint64_t address, size_t size) {
        const auto key = std::pair{address, size};
        if (const auto found = std::ranges::find(reads_, key, &Read::key); found != reads_.end()) return &found->bytes;
        if (spent_ + size > MaxBytes) {
            error_ = "the arrays pass " + std::to_string(MaxBytes >> 20) + " MiB of reads";
            return nullptr;
        }
        auto bytes = session_.client.ReadRange(address, size);
        if (!bytes) { error_ = bytes.error().message; return nullptr; }
        spent_ += size;
        reads_.push_back({key, std::move(*bytes)});
        return &reads_.back().bytes;
    }

    struct Read { std::pair<uint64_t, size_t> key; std::vector<uint8_t> bytes; };
    Session& session_;
    StructCache& structs_;
    uint32_t nested_;
    TextBatch texts_;
    std::deque<Read> reads_;
    size_t spent_ = 0;
    std::string error_;
};
}
