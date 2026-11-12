// reagentlive CLI: values of a typed layout (layout.h) as JSON. Pure; tests cover it.
#pragma once
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include "reagentlive/frontends/cli/values/json.h"
#include "reagentlive/frontends/cli/syntax/layout.h"

namespace reagentlive::cli::layout {
// One element of a `read --as` type; NaN and null pointers as null.
inline Json Element(std::string_view type, std::span<const uint8_t> bytes) {
    const auto load = [&]<class T>(T) { T value{}; memcpy(&value, bytes.data(), sizeof(T)); return value; };
    if (bytes.size() < ElementSize(type)) return nullptr;
    if (type == "i8") return load(int8_t{});
    if (type == "u8") return load(uint8_t{});
    if (type == "i16") return load(int16_t{});
    if (type == "u16") return load(uint16_t{});
    if (type == "i32") return load(int32_t{});
    if (type == "u32") return load(uint32_t{});
    if (type == "i64") return load(int64_t{});
    if (type == "u64") return load(uint64_t{});
    if (type == "f32") { const float value = load(float{}); return value == value ? Json(value) : Json(nullptr); }
    if (type == "f64") { const double value = load(double{}); return value == value ? Json(value) : Json(nullptr); }
    if (type == "ptr") { const uint64_t value = load(uint64_t{}); return value ? Json(format_detail::Pointer(value)) : Json(nullptr); }
    return nullptr;
}

// A field's value from bytes starting at the field: one element, or a list.
inline Json Value(const Field& field, std::span<const uint8_t> bytes) {
    const size_t size = ElementSize(field.type);
    if (!field.array) return Element(field.type, bytes.first(std::min(bytes.size(), size)));
    Json list = Json::array();
    for (size_t i = 0; i < field.count; ++i)
        list.push_back((i + 1) * size <= bytes.size() ? Element(field.type, bytes.subspan(i * size, size)) : Json(nullptr));
    return list;
}

// The key of an unnamed field: its offset, "+0x8".
inline std::string OffsetKey(uint32_t offset) {
    char key[16];
    snprintf(key, sizeof(key), "+0x%x", offset);
    return key;
}

// {name: value} for every field but gaps; `bytes` is the layout's size.
inline Json Decode(const Layout& layout, std::span<const uint8_t> bytes) {
    Json values = Json::object();
    for (const auto& field : layout.fields) {
        if (field.type.empty()) continue;
        const auto data = field.offset < bytes.size() ? bytes.subspan(field.offset) : std::span<const uint8_t>{};
        values[field.name.empty() ? OffsetKey(field.offset) : field.name] = Value(field, data);
    }
    return values;
}
}
