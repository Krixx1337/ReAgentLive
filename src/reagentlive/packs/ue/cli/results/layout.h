// reagentlive CLI, Unreal pack: the line format layout-export writes and
// layout-check reads (docs/commands/unreal.md, layouts in bulk): `class`,
// `struct` and `enum` lines describe types, `field` lines (and lines with
// no kind) their fields; layout-check reads only the field lines. Pure;
// shared with tests.
#pragma once
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include "reagentlive/frontends/cli/call/values.h"
#include "reagentlive/frontends/cli/values/json.h"

namespace reagentlive::cli {
// One expected field: its declaring class, name and offset; size and type
// are checked only when the line gives them.
struct LayoutLine {
    std::string owner, field;
    uint32_t offset = 0;
    std::optional<uint32_t> size;
    std::optional<std::string> type;
};

// "0x2e8", "744" or 744.
inline std::optional<uint32_t> LayoutNumber(const Json& value) {
    if (value.is_number_unsigned() || (value.is_number_integer() && value.get<int64_t>() >= 0)) {
        const auto number = value.get<uint64_t>();
        return number <= UINT32_MAX ? std::optional<uint32_t>(static_cast<uint32_t>(number)) : std::nullopt;
    }
    uint64_t number = 0;
    if (value.is_string() && text::Number(value.get<std::string>(), number) && number <= UINT32_MAX)
        return static_cast<uint32_t>(number);
    return std::nullopt;
}

// A line layout-check skips: a class, struct or enum line.
inline bool LayoutTypeLine(const std::string& text) {
    const Json json = Json::parse(text, nullptr, false);
    return json.is_object() && json.contains("kind") && json["kind"] != "field";
}

// One field line, or why not.
inline std::expected<LayoutLine, std::string> ParseLayoutLine(const std::string& text) {
    const Json json = Json::parse(text, nullptr, false);
    if (!json.is_object()) return std::unexpected("not a JSON object");
    LayoutLine line;
    if (!json.contains("owner") || !json["owner"].is_string() || !json.contains("field") || !json["field"].is_string())
        return std::unexpected("owner and field are strings");
    line.owner = json["owner"].get<std::string>();
    line.field = json["field"].get<std::string>();
    const auto offset = json.contains("offset") ? LayoutNumber(json["offset"]) : std::nullopt;
    if (!offset) return std::unexpected("offset is a number or \"0x..\"");
    line.offset = *offset;
    if (json.contains("size")) {
        line.size = LayoutNumber(json["size"]);
        if (!line.size) return std::unexpected("size is a number");
    }
    if (json.contains("type")) {
        if (!json["type"].is_string()) return std::unexpected("type is a string");
        line.type = json["type"].get<std::string>();
    }
    return line;
}

inline std::string LayoutHex(uint64_t value) {
    char hex[24];
    snprintf(hex, sizeof(hex), "0x%llx", static_cast<unsigned long long>(value));
    return hex;
}

// The line layout-export writes for a field: what layout-check reads; the
// export adds the field's struct, class or enum and container parts.
inline Json LayoutJson(const std::string& owner, const std::string& field, uint32_t offset, uint32_t size,
                       const std::string& type) {
    return {{"kind", "field"}, {"owner", owner}, {"field", field}, {"offset", LayoutHex(offset)}, {"size", size},
            {"type", type}};
}

// Whether a property's subtype is an enum (enum and byte properties) or a class.
inline bool LayoutEnumType(const std::string& type) { return type == "EnumProperty" || type == "ByteProperty"; }

// A container's element, key or value: {type, size, struct?, class?, enum?}.
inline Json LayoutPart(const std::string& type, uint32_t size, const std::string& structName, const std::string& subtype) {
    Json part{{"type", type}, {"size", size}};
    if (!structName.empty()) part["struct"] = structName;
    if (!subtype.empty()) part[LayoutEnumType(type) ? "enum" : "class"] = subtype;
    return part;
}

// Why the live field differs from the line ("" when it holds): offset first, then size, then type.
inline std::string LayoutProblem(const LayoutLine& line, uint32_t offset, uint32_t size, const std::string& type) {
    if (offset != line.offset) return "offset";
    if (line.size && size != *line.size) return "size";
    if (line.type && type != *line.type) return "type";
    return {};
}
}
