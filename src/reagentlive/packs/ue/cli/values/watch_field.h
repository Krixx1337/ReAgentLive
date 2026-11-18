// reagentlive CLI, Unreal pack: a reflected field as a watch range's element
// type, so `watch-start 30s $1.Health` decodes the value as the engine stores
// it. Pure; shared with tests.
#pragma once
#include <cstdint>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/syntax/command.h"
#include "reagentlive/packs/ue/client_ue.h"

namespace reagentlive::cli::watch_field {
// The read --type element of a scalar property of `size` bytes; empty when
// it has none (strings, names, containers: watched as raw bytes).
inline std::string_view ScalarType(std::string_view type, uint32_t size) {
    if (type == "FloatProperty" && size == 4) return "f32";
    if (type == "DoubleProperty" && size == 8) return "f64";
    if (type == "Int8Property" && size == 1) return "i8";
    if (type == "Int16Property" && size == 2) return "i16";
    if (type == "IntProperty" && size == 4) return "i32";
    if (type == "Int64Property" && size == 8) return "i64";
    if (type == "UInt16Property" && size == 2) return "u16";
    if (type == "UInt32Property" && size == 4) return "u32";
    if (type == "UInt64Property" && size == 8) return "u64";
    if (type == "BoolProperty" && size == 1) return "u8"; // The byte holding the bit.
    if ((type == "ObjectProperty" || type == "ClassProperty") && size == 8) return "ptr";
    if (type == "ByteProperty" || type == "EnumProperty") {
        switch (size) {
        case 1: return "u8";
        case 2: return "u16";
        case 4: return "u32";
        case 8: return "u64";
        default: return {};
        }
    }
    return {};
}

// The watch element code (WatchTypeCode, 0 = raw) for a field. A struct whose
// members are one scalar type laid end to end (FVector: f64[3]) is that type;
// any other struct is raw bytes. `members` is the struct's field table or null.
inline uint32_t FieldType(const ue::Field& field, const std::vector<ue::Field>* members) {
    if (field.type != "StructProperty") return WatchTypeCode(ScalarType(field.type, field.size));
    if (!members || members->empty()) return 0;
    std::string_view element;
    uint32_t next = 0;
    for (const auto& member : *members) {
        const auto type = ScalarType(member.type, member.size);
        if (type.empty() || (!element.empty() && type != element) || member.offset != next) return 0;
        element = type;
        next += member.size;
    }
    return next == field.size ? WatchTypeCode(element) : 0;
}
}
