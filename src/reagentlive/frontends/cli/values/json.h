// reagentlive CLI: the JSON type every result uses, and numbers from raw bytes.
#pragma once
#include <cmath>
#include <cstdint>
#include <span>
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
}
