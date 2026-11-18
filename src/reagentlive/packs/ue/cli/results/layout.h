// reagentlive CLI, Unreal pack: the line format layout-export writes and
// layout-check reads. Pure; shared with tests.
#pragma once
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include "reagentlive/frontends/cli/syntax/text.h"
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

// One file line, or why not.
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

// The line layout-export writes for a field.
inline Json LayoutJson(const std::string& owner, const std::string& field, uint32_t offset, uint32_t size,
                       const std::string& type) {
    char hex[16];
    snprintf(hex, sizeof(hex), "0x%x", offset);
    return {{"owner", owner}, {"field", field}, {"offset", hex}, {"size", size}, {"type", type}};
}

// Why the live field differs from the line ("" when it holds): offset first, then size, then type.
inline std::string LayoutProblem(const LayoutLine& line, uint32_t offset, uint32_t size, const std::string& type) {
    if (offset != line.offset) return "offset";
    if (line.size && size != *line.size) return "size";
    if (line.type && type != *line.type) return "type";
    return {};
}
}
