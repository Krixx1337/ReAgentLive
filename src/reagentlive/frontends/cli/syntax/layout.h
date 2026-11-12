// reagentlive CLI: typed layouts for raw bytes that reflection does not
// describe ("u8 phase, pad 3, f32 duration, f64[3] start"), shared by
// `read --type` and the native-member names file. Pure; tests cover it.
#pragma once
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/syntax/command.h"
#include "reagentlive/frontends/cli/syntax/text.h"

namespace reagentlive::cli::layout {
// One field in memory order: `count` elements of `type` (one of the `read
// --type` element types), or a gap of `count` bytes when `type` is empty.
struct Field {
    std::string type;
    uint32_t count = 1;
    bool array = false;         // Written with [n]: decodes to a list even for n = 1.
    std::string name;           // Empty: shown by its offset.
    uint32_t offset = 0;
    uint32_t Size() const { return type.empty() ? count : static_cast<uint32_t>(ElementSize(type)) * count; }
    bool operator==(const Field&) const = default;
};
struct Layout {
    std::vector<Field> fields;  // Gaps included.
    uint32_t size = 0;
};

// A name as the console prints it back: letters, digits, '_', an optional trailing '?'.
inline bool ValidName(std::string_view name) {
    if (name.empty() || name == "?") return false;
    if (name.ends_with('?')) name.remove_suffix(1);
    for (const char c : name)
        if (!(c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    return !(name.front() >= '0' && name.front() <= '9');
}

// "type[n] name" (count and name optional) or "pad n". `wantName`: the name is required.
inline std::expected<Field, std::string> ParseField(std::string_view text, bool wantName = false) {
    std::string_view rest = text;
    auto type = text::Next(rest);
    Field field;
    if (type == "pad") {
        uint64_t bytes = 0;
        if (!text::Number(text::Next(rest), bytes) || !bytes || bytes > MaxRead || !rest.empty())
            return std::unexpected("pad takes a byte count: pad <n>");
        field.count = static_cast<uint32_t>(bytes);
        return field;
    }
    if (const auto open = type.find('['); open != std::string_view::npos) {
        uint64_t count = 0;
        if (!type.ends_with(']') || !text::Number(type.substr(open + 1, type.size() - open - 2), count) || !count ||
            count > MaxRead) return std::unexpected("bad count in '" + std::string(type) + "': type[n], n 1.." +
                std::to_string(MaxRead));
        field.count = static_cast<uint32_t>(count);
        field.array = true;
        type = type.substr(0, open);
    }
    if (!ElementSize(type))
        return std::unexpected("unknown type '" + std::string(type) + "' (i8 u8 i16 u16 i32 u32 i64 u64 f32 f64 ptr)");
    field.type = type;
    field.name = text::Next(rest);
    if (!rest.empty()) return std::unexpected("one name per field: '" + std::string(text) + "'");
    if (field.name.empty() ? wantName : !ValidName(field.name))
        return std::unexpected(field.name.empty() ? "a name is required" : "bad name '" + field.name + "'");
    return field;
}

// Comma-separated fields in memory order; offsets follow from the sizes.
inline std::expected<Layout, std::string> Parse(std::string_view text) {
    Layout out;
    size_t index = 0;
    while (true) {
        const size_t comma = text.find(',');
        const auto part = text::Trim(text.substr(0, comma));
        ++index;
        if (part.empty()) return std::unexpected("field " + std::to_string(index) + " is empty");
        auto field = ParseField(part);
        if (!field) return std::unexpected("field " + std::to_string(index) + ": " + field.error());
        field->offset = out.size;
        out.size += field->Size();
        if (out.size > MaxRead) return std::unexpected("layout is larger than " + std::to_string(MaxRead) + " bytes");
        for (const auto& other : out.fields)
            if (!field->name.empty() && other.name == field->name) return std::unexpected("name '" + field->name + "' twice");
        out.fields.push_back(std::move(*field));
        if (comma == std::string_view::npos) return out;
        text = text.substr(comma + 1);
    }
}
}
