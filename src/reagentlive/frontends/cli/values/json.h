// reagentlive CLI: the JSON type every result uses, numbers from raw bytes, and `view.nonzero`.
#pragma once
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <type_traits>
#include <nlohmann/json.hpp>
#include "reagentlive/frontends/cli/values/format.h"

namespace reagentlive::cli {
using Json = nlohmann::json;

// A float32 as the shortest decimal that reads back as the same float
// (2.35, not 2.3499999046325684): JSON prints the double it widens to.
inline double ShortFloat(float number) {
    char text[32];
    const auto written = std::to_chars(text, text + sizeof(text), number);
    double out = number;
    std::from_chars(text, written.ptr, out);
    return out;
}

template<class T> Json JsonNumber(std::span<const uint8_t> bytes) {
    T number{};
    format_detail::Load(bytes, number);
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(number)) return {{"non_finite", std::isnan(number) ? "nan" : number < 0 ? "-inf" : "inf"}};
    }
    if constexpr (std::is_same_v<T, float>) return ShortFloat(number);
    else return number;
}

// `view.nonzero` on a decoded value: leaves out the zero members of its structs,
// at any depth (0, false, null, "", the name None, an empty list or array,
// a float at its type's maximum, the usual "unset" default, a struct left with
// none). Elements stay, so a list keeps its positions; an
// element whose members were all zero says so, {"zeroed": N}, so it does not
// read as empty. True when the whole value is zero.
inline bool DropZero(Json& value) {
    // An element or top-level struct emptied by the filter, not empty to begin with.
    const auto zeroed = [](Json& element) {
        const size_t members = element.is_object() ? element.size() : 0;
        const bool zero = DropZero(element);
        if (zero && members && element.is_object() && element.empty()) element = Json{{"zeroed", members}};
        return zero;
    };
    if (value.is_null()) return true;
    if (value.is_boolean()) return !value.get<bool>();
    if (value.is_number()) {
        // A float prints as its shortest decimal (3.4028235e+38): anything that rounds to FLT_MAX.
        const double size = std::abs(value.get<double>());
        return size == 0 || (size >= 3.4028234e38 && size < 3.4028236e38);
    }
    if (value.is_string()) return value.get_ref<const std::string&>().empty() || value == "None";
    if (value.is_array()) {
        bool zero = true;
        for (auto& element : value) zero &= zeroed(element);
        return value.empty() || zero;
    }
    // An array as decoded ({count, capacity, address, rows}): zero when empty.
    if (value.contains("count") && value.contains("rows") && value["count"] == 0) return true;
    if (value.contains("rows") && value["rows"].is_array()) {
        for (auto& row : value["rows"]) zeroed(row);
        return false;
    }
    for (auto member = value.begin(); member != value.end();) {
        if (DropZero(member.value())) member = value.erase(member);
        else ++member;
    }
    return value.empty();
}
// `view.nonzero` on a whole value (a cell, a row's value, an output): as DropZero,
// and a struct the filter emptied is {"zeroed": N} rather than {}.
inline void KeepNonZero(Json& value) {
    const size_t members = value.is_object() ? value.size() : 0;
    if (DropZero(value) && members && value.is_object() && value.empty()) value = Json{{"zeroed", members}};
}
}
