// reagentlive CLI: bytes as text: hex, pointers, integers. Pure; tests cover it.
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
}
