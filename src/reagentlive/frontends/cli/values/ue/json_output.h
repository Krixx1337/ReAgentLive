// reagentlive CLI, Unreal pack: reflected field values as JSON.
#pragma once
#include <algorithm>
#include "reagentlive/frontends/cli/values/ue/format_ue.h"
#include "reagentlive/frontends/cli/values/json.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/ue/client_ue.h"

namespace reagentlive::cli {
inline Json FieldMetadata(const ue::Field& field) {
    Json row = {{"name", field.name}, {"owner", field.owner}, {"type", field.type},
        {"offset", field.offset}, {"size", field.size}};
    if (!field.structName.empty()) row["struct"] = field.structName;
    if (field.boolMask) row["bool_mask"] = field.boolMask;
    if (field.elementSize) {
        row["element_type"] = field.elementType;
        row["element_size"] = field.elementSize;
    }
    if (field.valueSize) {
        row["value_type"] = field.valueType;
        row["value_size"] = field.valueSize;
        row["value_offset"] = field.valueOffset;
        if (!field.valueStructName.empty()) row["value_struct"] = field.valueStructName;
    }
    if (field.stride) row["stride"] = field.stride;
    return row;
}

// How decoded values print. Compact (the default): a struct is {Name: value}
// with readable names where the names file has them. `meta` (`--meta`): a
// struct is {members: [{name, owner, type, offset, size, value}], truncated},
// for when the layout itself is the question.
struct ValueStyle {
    bool meta = false;
    const Names* names = nullptr;
};

// A struct member's key in the compact form: its readable name, else its
// reflected one; a name already taken gets "@<offset>".
inline std::string MemberKey(const ue::Field& member, const ValueStyle& style, const Json& taken) {
    std::string key = member.name;
    if (style.names)
        if (const auto name = style.names->Find(member.owner, member.offset, member.size, member.boolMask); !name.empty())
            key = name;
    if (taken.contains(key)) key += "@" + std::to_string(member.offset);
    return key;
}

template<class StructLookup, class NameLookup>
Json JsonFieldValue(const ue::Field& field, std::span<const uint8_t> bytes, StructLookup& structs,
                    const NameLookup& names, bool& truncated, const ValueStyle& style = {}, unsigned depth = 0) {
    using format_detail::Load;
    const auto& type = field.type;
    if (bytes.empty()) {
        truncated |= field.size != 0;
        return nullptr;
    }
    if (type == "BoolProperty" && bytes.size() == 1)
        return (bytes[0] & (field.boolMask ? field.boolMask : 0xff)) != 0;
    if (type == "FloatProperty" && bytes.size() == 4) return JsonNumber<float>(bytes);
    if (type == "DoubleProperty" && bytes.size() == 8) return JsonNumber<double>(bytes);
    if (type == "Int8Property" && bytes.size() == 1) return JsonNumber<int8_t>(bytes);
    if (type == "Int16Property" && bytes.size() == 2) return JsonNumber<int16_t>(bytes);
    if (type == "UInt16Property" && bytes.size() == 2) return JsonNumber<uint16_t>(bytes);
    if (type == "IntProperty" && bytes.size() == 4) return JsonNumber<int32_t>(bytes);
    if (type == "UInt32Property" && bytes.size() == 4) return JsonNumber<uint32_t>(bytes);
    if (type == "Int64Property" && bytes.size() == 8) return JsonNumber<int64_t>(bytes);
    if (type == "UInt64Property" && bytes.size() == 8) return JsonNumber<uint64_t>(bytes);
    if (type == "ByteProperty" || type == "EnumProperty") {
        switch (bytes.size()) {
        case 1: return JsonNumber<uint8_t>(bytes);
        case 2: return JsonNumber<uint16_t>(bytes);
        case 4: return JsonNumber<uint32_t>(bytes);
        case 8: return JsonNumber<uint64_t>(bytes);
        }
    }
    if ((type == "ObjectProperty" || type == "ClassProperty" || type == "InterfaceProperty") && bytes.size() == 8) {
        uint64_t address{};
        Load(bytes, address);
        return address ? Json(format_detail::Pointer(address)) : Json(nullptr);
    }
    if (type == "WeakObjectProperty" && bytes.size() == 8) {
        int32_t index{}, serial{};
        Load(bytes, index);
        Load(bytes, serial, 4);
        return {{"index", index}, {"serial", serial}};
    }
    if (type == "NameProperty" && bytes.size() == 8) {
        int32_t index{};
        uint32_t number{};
        Load(bytes, index);
        Load(bytes, number, 4);
        const std::string_view name = names(index);
        return {{"index", index}, {"number", number}, {"name", name.empty() ? Json(nullptr) : Json(name)}};
    }
    if (type == "ArrayProperty" || type == "StrProperty") {
        ue::ArrayHeader header{};
        if (bytes.size() == sizeof(header) && Load(bytes, header)) {
            const bool partial = header.count != 0 && (depth != 0 || type == "StrProperty");
            truncated |= partial;
            Json value = {{"count", header.count}, {"capacity", header.capacity},
                {"address", header.data ? Json(format_detail::Pointer(header.data)) : Json(nullptr)}};
            if (depth || type == "StrProperty") value["truncated"] = partial;
            return value;
        }
    }
    if (type == "StructProperty" && field.structType.address && depth < MaxStructDepth) {
        if (const auto* found = structs(field.structType); found && !found->empty()) {
            const auto members = std::span(*found);
            const size_t shown = (std::min)(members.size(), MaxStructMembers);
            bool partial = members.size() > shown;
            Json rows = Json::array(), compact = Json::object();
            for (const auto& member : members.first(shown)) {
                const bool fits = size_t{member.offset} + member.size <= bytes.size();
                const auto data = fits ? bytes.subspan(member.offset, member.size) : std::span<const uint8_t>{};
                auto value = JsonFieldValue(member, data, structs, names, partial, style, depth + 1);
                if (style.meta) {
                    Json row = FieldMetadata(member);
                    row["value"] = std::move(value);
                    rows.push_back(std::move(row));
                } else {
                    compact[MemberKey(member, style, compact)] = std::move(value);
                }
            }
            truncated |= partial;
            if (style.meta) return {{"members", std::move(rows)}, {"truncated", partial}};
            // Members past the shown ones: their count under "...".
            if (members.size() > shown) compact["..."] = members.size() - shown;
            return compact;
        }
    }
    constexpr size_t MaxHexBytes = 16;
    const bool partial = bytes.size() > MaxHexBytes;
    truncated |= partial;
    return {{"hex", format_detail::Hex(bytes.first((std::min)(bytes.size(), MaxHexBytes)))},
        {"byte_count", bytes.size()}, {"truncated", partial}};
}
}
