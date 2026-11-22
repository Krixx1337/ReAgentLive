// reagentlive CLI console, Unreal pack: rows of structs read from engine
// memory and decoded with their reflected struct (a call's returned arrays,
// a data table's rows), and the --fields tree that picks their members.
// For packs/ue/cli/console/ only.
#pragma once
#include <algorithm>
#include <cstring>
#include <deque>
#include <iterator>
#include <optional>
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
    const RowPaths* Find(std::string_view name) const {
        for (size_t i = 0; i < names.size(); ++i)
            if (text::SameText(names[i], name)) return &below[i];
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

// A map's key (a set's element) and a map's value as fields of their own.
inline ue::Field MapKey(const ue::Field& map) {
    ue::Field key{};
    key.name = "key";
    key.size = map.elementSize;
    key.type = map.elementType;
    key.structType = map.structType;
    key.structName = map.structName;
    return key;
}
inline ue::Field MapValue(const ue::Field& map) {
    ue::Field value{};
    value.name = "value";
    value.offset = map.valueOffset;
    value.size = map.valueSize;
    value.type = map.valueType;
    value.structType = map.valueStructType;
    value.structName = map.valueStructName;
    return value;
}
inline bool IsContainer(std::string_view type) { return type == "SetProperty" || type == "MapProperty"; }
inline bool IsPointer(std::string_view type) {
    return type == "ObjectProperty" || type == "ClassProperty" || type == "WeakObjectProperty";
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
// compact values of get, each, dump and entries. Arrays, sets and maps nest
// (each read from the engine, at most `nested` elements, then `truncated`),
// structs expand member by member, a tag container is the list of its tags,
// text is converted by the engine and object pointers are named. Rendered
// twice: once collecting names, texts and pointers, once with them resolved
// (Resolve between); element reads are cached between the two.
class RowReader {
public:
    static constexpr uint32_t MaxNested = 256;    // Elements per nested array in rows.
    static constexpr uint32_t PreviewNested = 8;  // In get, each and dump, as dump's previews.
    static constexpr unsigned MaxDepth = 3;       // Arrays inside arrays.
    static constexpr unsigned MaxStructs = 4;     // Structs inside structs.
    static constexpr size_t MaxBytes = 16u << 20; // All element reads of one command.
    static constexpr size_t MaxPointers = 4 * ue::MaxDescribeMany; // Distinct pointers named per command.

    RowReader(Session& session, StructCache& structs, uint32_t nested = MaxNested)
        : session_(session), structs_(structs), nested_(nested) {}

    // Top-level pointer fields stay addresses: dump and get name them on their rows.
    void KeepTopPointers() { topPointers_ = false; }
    // Distinct pointers past MaxPointers, printed as addresses: the result's `unnamed`.
    uint32_t Unnamed() const { return unnamed_; }

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
            if (found->type == "MapProperty") { // Below a map: its key and its value.
                for (size_t j = 0; j < paths.below[i].names.size(); ++j) {
                    const auto& part = paths.below[i].names[j];
                    if (!text::SameText(part, "key") && !text::SameText(part, "value"))
                        return found->name + " is a map: below it are key and value, not " + part;
                    const ue::Field next = text::SameText(part, "key") ? MapKey(*found) : MapValue(*found);
                    if (auto why = Check(next, paths.below[i].below[j], depth + 1); !why.empty()) return why;
                }
                continue;
            }
            const ue::Field next = found->type == "ArrayProperty" ? Element(*found) :
                found->type == "SetProperty" ? MapKey(*found) : *found;
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

    // Between the two passes: the texts met, converted by the engine
    // (TextBatch), and the pointers met, named in describe-many requests.
    void Resolve() {
        texts_.Resolve(session_);
        resolved_ = true;
        asked_ = std::min(targets_.size(), MaxPointers);
        unnamed_ = static_cast<uint32_t>(targets_.size() - asked_);
        if (!asked_ || !session_.client.Supports(ue::op::DescribeMany)) { asked_ = 0; unnamed_ = 0; return; }
        for (size_t from = 0; from < asked_; from += ue::MaxDescribeMany) {
            const size_t count = std::min<size_t>(ue::MaxDescribeMany, asked_ - from);
            auto found = ue::DescribeMany(session_.client, std::span(targets_).subspan(from, count));
            if (!found) { asked_ = from; break; } // Left as addresses, not counted gone.
            for (auto& object : *found) described_.push_back(std::move(object));
        }
        unnamed_ = static_cast<uint32_t>(targets_.size() - asked_);
    }

    // One value: a field of an object (get, each, dump) or a struct row
    // (rows). `depth` counts arrays above it, `nest` structs.
    template<class Names>
    Json Value(const ue::Field& field, std::span<const uint8_t> bytes, const RowPaths& paths, const Names& names,
               bool& truncated, unsigned depth = 0, unsigned nest = 0) {
        const ValueStyle style{false, &session_.names};
        if (field.type == "TextProperty") {
            texts_.Add(field, bytes);
            return texts_.Value(field, bytes, JsonFieldValue(field, bytes, structs_, names, truncated, style, 1), false);
        }
        if (field.type == "ArrayProperty" && depth < MaxDepth && bytes.size() == sizeof(ue::ArrayHeader))
            return Array(field, bytes, 0, nested_, paths, names, truncated, depth + 1);
        if (IsContainer(field.type) && depth < MaxDepth) return Container(field, bytes, paths, names, truncated, depth + 1);
        if (IsPointer(field.type) && bytes.size() == 8 && (depth || nest || topPointers_))
            return Pointer(field.type, bytes, JsonFieldValue(field, bytes, structs_, names, truncated, style, 1));
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
    // A set or map, as arrays print: {count, rows, truncated?}; a map's rows
    // are {key, value}. The stock UE5 sparse layout, checked as `entries`
    // checks it; a header that fails falls back to hex with the reason.
    template<class Names>
    Json Container(const ue::Field& field, std::span<const uint8_t> bytes, const RowPaths& paths, const Names& names,
                   bool& truncated, unsigned depth) {
        const bool map = field.type == "MapProperty";
        const auto fallback = [&](std::string why) {
            Json out = JsonFieldValue(field, bytes, structs_, names, truncated, ValueStyle{false, &session_.names}, 1);
            if (!out.is_object()) out = Json{{"value", std::move(out)}};
            out["container_error"] = std::move(why);
            truncated = true;
            return out;
        };
        ue::SparseHeader header{};
        if (!field.stride) return fallback("the element layout is unknown (failed its check)");
        if (bytes.size() < sizeof(header)) return fallback("too small for a set header");
        memcpy(&header, bytes.data(), sizeof(header));
        if (!ue::SparseHeaderValid(header)) return fallback("not a valid set header");
        const auto num = static_cast<uint32_t>(header.num);
        const uint32_t words = (num + 31) / 32;
        std::vector<uint32_t> bits(std::begin(header.inlineBits), std::end(header.inlineBits));
        if (header.secondaryBits && words) {
            const auto* live = Elements(header.secondaryBits, size_t{words} * sizeof(uint32_t));
            if (!live) return fallback("allocation bits unreadable: " + error_);
            bits.assign(words, 0);
            memcpy(bits.data(), live->data(), size_t{words} * sizeof(uint32_t));
        }
        uint32_t occupied = 0;
        for (uint32_t slot = 0; slot < num; ++slot) occupied += ue::SlotAllocated(bits.data(), slot);
        if (occupied != num - static_cast<uint32_t>(header.numFree))
            return fallback("allocation bits disagree with the free count");
        std::vector<uint32_t> slots;
        ue::WalkSlots(bits.data(), num, 0, nested_, [&](uint32_t slot) { slots.push_back(slot); });
        Json out = {{"count", occupied}};
        Json rows = Json::array();
        if (!slots.empty()) {
            const auto* data = Elements(header.data, size_t{slots.back() + 1} * field.stride);
            if (!data) {
                out["rows_error"] = error_.empty() ? "elements unreadable" : error_;
                truncated = true;
                return out;
            }
            const ue::Field key = MapKey(field), value = MapValue(field);
            const RowPaths* keyPaths = map && !paths.Empty() ? paths.Find("key") : &paths;
            const RowPaths* valuePaths = map && !paths.Empty() ? paths.Find("value") : &paths;
            for (const uint32_t slot : slots) {
                const auto element = std::span(*data).subspan(size_t{slot} * field.stride, field.stride);
                if (!map) {
                    rows.push_back(Value(key, element.first(key.size), paths, names, truncated, depth, 0));
                    continue;
                }
                Json row = Json::object();
                if (keyPaths) row["key"] = Value(key, element.first(key.size), *keyPaths, names, truncated, depth, 0);
                if (valuePaths && size_t{value.offset} + value.size <= element.size())
                    row["value"] = Value(value, element.subspan(value.offset, value.size), *valuePaths, names, truncated,
                        depth, 0);
                rows.push_back(std::move(row));
            }
        }
        out["rows"] = std::move(rows);
        if (slots.size() < occupied) {
            out["truncated"] = true;
            truncated = true;
        }
        return out;
    }

    // An object, class or weak pointer: named once Resolve has looked it up
    // ({name, class, ref}); null stays as `plain` has it. A weak pointer the
    // lookup did not find is `gone` when its answer came from the engine's
    // thread, else `unverified`. One past MaxPointers stays an address.
    Json Pointer(std::string_view type, std::span<const uint8_t> bytes, Json plain) {
        const Ref target = PointerRef(type, bytes);
        if (!target.address && !target.serial) return plain;
        const auto at = std::ranges::find(targets_, target);
        if (!resolved_) {
            if (at == targets_.end()) targets_.push_back(target);
            return plain;
        }
        const auto slot = static_cast<size_t>(at - targets_.begin());
        if (at == targets_.end() || slot >= asked_) return plain;
        if (slot < described_.size() && described_[slot]) return ToJson(MakeObject(*described_[slot]));
        if (type != "WeakObjectProperty") return plain;
        Json weak = {{"index", target.index}, {"serial", target.serial}};
        weak[TickAccurate(session_) ? "gone" : "unverified"] = true;
        return weak;
    }

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
    bool topPointers_ = true, resolved_ = false;
    std::vector<Ref> targets_;                          // Pointers met, in order.
    std::vector<std::optional<ue::Object>> described_;  // Their objects, for the first asked_.
    size_t asked_ = 0;
    uint32_t unnamed_ = 0;
    TextBatch texts_;
    std::deque<Read> reads_;
    size_t spent_ = 0;
    std::string error_;
};
}
