// reagentlive CLI: typed text for reflected property values. Pure; tests cover it.
#pragma once
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace reagentlive::cli {
namespace format_detail {
struct UnresolvedName {
    std::string_view operator()(int32_t) const { return {}; }
};

template<class T> bool Load(std::span<const uint8_t> bytes, T& out, size_t at = 0) {
    if (bytes.size() < at + sizeof(T)) return false;
    memcpy(&out, bytes.data() + at, sizeof(T));
    return true;
}
inline std::string Hex(std::span<const uint8_t> bytes, size_t shown = 16) {
    std::string text;
    char cell[4];
    for (size_t i = 0; i < bytes.size() && i < shown; ++i) {
        snprintf(cell, sizeof(cell), i ? " %02x" : "%02x", bytes[i]);
        text += cell;
    }
    if (bytes.size() > shown) text += " ...";
    return text;
}
inline std::string Pointer(uint64_t value) {
    if (!value) return "null";
    char text[24];
    snprintf(text, sizeof(text), "0x%" PRIx64, value);
    return text;
}
// TArray/FString header: data pointer, count, capacity.
inline std::string ArrayHeader(std::span<const uint8_t> bytes, const char* kind) {
    uint64_t data = 0;
    int32_t num = 0, max = 0;
    if (!Load(bytes, data) || !Load(bytes, num, 8) || !Load(bytes, max, 12)) return Hex(bytes);
    char text[96];
    snprintf(text, sizeof(text), "%s num=%d max=%d @%s", kind, num, max, Pointer(data).c_str());
    return text;
}
template<class T> std::string Integer(std::span<const uint8_t> bytes, const char* format) {
    T value{};
    if (!Load(bytes, value)) return Hex(bytes);
    char text[32];
    snprintf(text, sizeof(text), format, value);
    return text;
}
}

// One property's value from its bytes (offset already applied). type is the
// engine's property class name; boolMask as in ue::FieldEntry.
template<class NameLookup = format_detail::UnresolvedName>
std::string FormatValue(std::string_view type, std::span<const uint8_t> bytes,
                        uint8_t boolMask = 0, const NameLookup& nameLookup = {}) {
    using namespace format_detail;
    if (bytes.empty()) return {};
    if (type == "BoolProperty") {
        const uint8_t mask = boolMask ? boolMask : 0xff;
        return (bytes[0] & mask) ? "true" : "false";
    }
    if (type == "FloatProperty" && bytes.size() == 4) {
        float value; Load(bytes, value);
        char text[32]; snprintf(text, sizeof(text), "%g", value); return text;
    }
    if (type == "DoubleProperty" && bytes.size() == 8) {
        double value; Load(bytes, value);
        char text[32]; snprintf(text, sizeof(text), "%g", value); return text;
    }
    if (type == "Int8Property" && bytes.size() == 1) return Integer<int8_t>(bytes, "%d");
    if (type == "Int16Property" && bytes.size() == 2) return Integer<int16_t>(bytes, "%d");
    if (type == "UInt16Property" && bytes.size() == 2) return Integer<uint16_t>(bytes, "%u");
    if (type == "IntProperty" && bytes.size() == 4) return Integer<int32_t>(bytes, "%d");
    if (type == "UInt32Property" && bytes.size() == 4) return Integer<uint32_t>(bytes, "%u");
    if (type == "Int64Property" && bytes.size() == 8) return Integer<int64_t>(bytes, "%" PRId64);
    if (type == "UInt64Property" && bytes.size() == 8) return Integer<uint64_t>(bytes, "%" PRIu64);
    if (type == "ByteProperty" || type == "EnumProperty") {
        switch (bytes.size()) {
        case 1: return Integer<uint8_t>(bytes, "%u");
        case 2: return Integer<uint16_t>(bytes, "%u");
        case 4: return Integer<uint32_t>(bytes, "%u");
        case 8: return Integer<uint64_t>(bytes, "%" PRIu64);
        }
    }
    if ((type == "ObjectProperty" || type == "ClassProperty" || type == "InterfaceProperty") && bytes.size() >= 8) {
        uint64_t value = 0; Load(bytes, value); return Pointer(value);
    }
    if (type == "WeakObjectProperty" && bytes.size() == 8) {
        int32_t index = 0, serial = 0; Load(bytes, index); Load(bytes, serial, 4);
        char text[48]; snprintf(text, sizeof(text), "weak #%d serial %d", index, serial); return text;
    }
    if (type == "NameProperty" && bytes.size() >= 8) {
        int32_t index = 0; uint32_t number = 0; Load(bytes, index); Load(bytes, number, 4);
        if (const std::string_view name = nameLookup(index); !name.empty()) {
            std::string text(name);
            if (number) text.append("_").append(std::to_string(number - 1));
            return text;
        }
        char text[48]; snprintf(text, sizeof(text), "FName #%d number %u", index, number); return text;
    }
    if (type == "StrProperty" && bytes.size() >= 16) return ArrayHeader(bytes, "FString");
    if (type == "ArrayProperty" && bytes.size() >= 16) return ArrayHeader(bytes, "TArray");
    return Hex(bytes);
}

// Struct expansion bounds: nesting levels and members shown per struct.
constexpr unsigned MaxStructDepth = 3;
constexpr size_t MaxStructMembers = 16;

template<class Field, class Lookup, class NameLookup = format_detail::UnresolvedName>
std::string FormatField(const Field& field, std::span<const uint8_t> bytes, Lookup&& lookup,
                        unsigned depth = 0, const NameLookup& nameLookup = {});

// "(Name value, ...)" from the struct's members sorted by offset. Empty when a
// member falls outside the bytes, so the caller keeps the plain value.
template<class Field, class Lookup, class NameLookup>
std::string FormatStruct(std::span<const Field> members, std::span<const uint8_t> bytes, Lookup&& lookup,
                         unsigned depth, const NameLookup& nameLookup) {
    std::string text = "(";
    size_t shown = 0;
    for (const auto& member : members) {
        if (shown == MaxStructMembers) { text += ", ..."; break; }
        if (!member.size || size_t{member.offset} + member.size > bytes.size()) return {};
        if (shown++) text += ", ";
        text.append(member.name).append(1, ' ');
        text += FormatField(member, bytes.subspan(member.offset, member.size), lookup, depth, nameLookup);
    }
    return shown ? text + ")" : std::string{};
}

// A property's value; StructProperty fields expand through lookup(structType),
// which returns the struct's members sorted by offset, or nullptr if unknown.
template<class Field, class Lookup, class NameLookup>
std::string FormatField(const Field& field, std::span<const uint8_t> bytes, Lookup&& lookup,
                        unsigned depth, const NameLookup& nameLookup) {
    if (field.type == "StructProperty" && field.structType.address && depth < MaxStructDepth && !bytes.empty()) {
        if (const auto* members = lookup(field.structType)) {
            auto text = FormatStruct(std::span<const Field>(*members), bytes, lookup, depth + 1, nameLookup);
            if (!text.empty()) return text;
        }
    }
    return FormatValue(field.type, bytes, field.boolMask, nameLookup);
}
}
