// reagentlive CLI: bytes as text: hex, pointers, integers, the text around a
// search hit. Pure; tests cover it.
#pragma once
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace reagentlive::cli {
namespace format_detail {
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
template<class T> std::string Integer(std::span<const uint8_t> bytes, const char* format) {
    T value{};
    if (!Load(bytes, value)) return Hex(bytes);
    char text[32];
    snprintf(text, sizeof(text), format, value);
    return text;
}
}

// The printable run holding a search hit: `bytes` start `before` bytes ahead
// of the hit, which is `length` bytes long; ASCII, or UTF-16 with zero high
// bytes when `wide`. Stops at a terminator or a byte that is not text, and
// at `most` characters each side (cut ends get "..."). Empty when the hit is
// not inside text.
inline std::string TextAround(std::span<const uint8_t> bytes, size_t before, size_t length, bool wide, size_t most = 96) {
    const size_t step = wide ? 2 : 1;
    const auto text = [&](size_t at) {
        if (at + step > bytes.size()) return false;
        const uint8_t c = bytes[at];
        return ((c >= 0x20 && c < 0x7f) || c == '\t') && (!wide || bytes[at + 1] == 0);
    };
    if (before + length > bytes.size() || length % step) return {};
    for (size_t at = before; at < before + length; at += step)
        if (!text(at)) return {};
    size_t start = before, end = before + length, left = 0, right = 0;
    while (start >= step && text(start - step) && left < most) { start -= step; ++left; }
    while (text(end) && right < most) { end += step; ++right; }
    std::string out = left == most && start >= step && text(start - step) ? "..." : "";
    for (size_t at = start; at < end; at += step) out += static_cast<char>(bytes[at]);
    if (right == most && text(end)) out += "...";
    return out;
}
}
