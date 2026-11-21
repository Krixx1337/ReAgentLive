// reagentlive CLI: the JSON type every result uses, numbers from raw bytes, and --nonzero.
#pragma once
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <nlohmann/json.hpp>
#include "reagentlive/frontends/cli/values/format.h"

namespace reagentlive::cli {
using Json = nlohmann::json;

template<class T> Json JsonNumber(std::span<const uint8_t> bytes) {
    T number{};
    format_detail::Load(bytes, number);
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(number)) return {{"non_finite", std::isnan(number) ? "nan" : number < 0 ? "-inf" : "inf"}};
    }
    return number;
}

// --nonzero on a decoded value: leaves out the zero members of its structs,
// at any depth (0, false, null, "", the name None, an empty list or array,
// a struct left with none). Elements stay, so a list keeps its positions.
// True when the whole value is zero.
inline bool DropZero(Json& value) {
    if (value.is_null()) return true;
    if (value.is_boolean()) return !value.get<bool>();
    if (value.is_number()) return value == 0;
    if (value.is_string()) return value.get_ref<const std::string&>().empty() || value == "None";
    if (value.is_array()) {
        bool zero = true;
        for (auto& element : value) zero &= DropZero(element);
        return value.empty() || zero;
    }
    // An array as decoded ({count, capacity, address, rows}): zero when empty.
    if (value.contains("count") && value.contains("rows") && value["count"] == 0) return true;
    if (value.contains("rows") && value["rows"].is_array()) {
        for (auto& row : value["rows"]) DropZero(row);
        return false;
    }
    for (auto member = value.begin(); member != value.end();) {
        if (DropZero(member.value())) member = value.erase(member);
        else ++member;
    }
    return value.empty();
}
}
