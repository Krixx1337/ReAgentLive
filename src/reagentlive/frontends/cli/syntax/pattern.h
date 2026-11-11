// reagentlive CLI: byte patterns for `sig`, their text and list forms, the
// anchor the search looks for, and resolve fields. Pure, and free of the
// matcher, so the parser can include it.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/core/protocol.h"

namespace reagentlive::cli::sig {
constexpr size_t MaxPattern = 96;
constexpr size_t MinAnchor = 4;           // Fixed bytes in a row the anchor search needs.
constexpr size_t MaxAnchorHits = 4096;    // More: the anchor is too common.
constexpr size_t MaxShownHits = 16;
constexpr size_t Extension = 64;          // Bytes read past each match, for diverges_at and resolve fields.
constexpr size_t DefaultBytes = 64;       // sig --bytes.

// Bytes with a mask: fixed[i] false is a wildcard (its byte is 0).
struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> fixed;
    size_t Size() const { return bytes.size(); }
    size_t Fixed() const { return static_cast<size_t>(std::ranges::count(fixed, true)); }
    bool operator==(const Pattern&) const = default;
};

namespace detail {
inline int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
// 0xNN, NN (hex digits only, in the text form) or a decimal byte (list form).
inline bool Byte(std::string_view token, bool hexOnly, uint8_t& out) {
    unsigned value = 0;
    const bool prefixed = token.size() > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X');
    if (prefixed) token.remove_prefix(2);
    if (token.empty() || token.size() > 3) return false;
    for (const char c : token) {
        const int digit = prefixed || hexOnly ? Hex(c) : (c >= '0' && c <= '9' ? c - '0' : -1);
        if (digit < 0) return false;
        value = value * (prefixed || hexOnly ? 16 : 10) + static_cast<unsigned>(digit);
    }
    if (value > 0xff) return false;
    out = static_cast<uint8_t>(value);
    return true;
}
}

// Either form, so a pattern pastes straight into or out of a host's table:
//   text:  "48 8b ?? ?? 89" (? or ?? per wildcard byte; "488b" also splits)
//   list:  "0x48, 0x8B, -1, -1, 0x89" (-1 per wildcard; braces ignored)
// A comma makes it the list form.
inline std::expected<Pattern, std::string> ParsePattern(std::string_view text) {
    Pattern pattern;
    const bool list = text.find(',') != std::string_view::npos;
    const auto add = [&](uint8_t byte, bool fixed) {
        pattern.bytes.push_back(byte);
        pattern.fixed.push_back(fixed);
    };
    const auto separator = [&](char c) {
        return c == ' ' || c == '\t' || c == ',' || c == '{' || c == '}' || c == '"' || c == '\'';
    };
    for (size_t at = 0; at < text.size();) {
        if (separator(text[at])) { ++at; continue; }
        size_t end = at;
        while (end < text.size() && !separator(text[end])) ++end;
        const auto token = text.substr(at, end - at);
        at = end;
        uint8_t byte = 0;
        if (list) {
            if (token == "-1") add(0, false);
            else if (detail::Byte(token, false, byte)) add(byte, true);
            else return std::unexpected("list items are 0xNN, a decimal byte or -1, not '" + std::string(token) + "'");
        } else if (token == "?" || token == "??") {
            add(0, false);
        } else {
            if (token.size() % 2) return std::unexpected("bytes are hex pairs or ?? , not '" + std::string(token) + "'");
            for (size_t i = 0; i < token.size(); i += 2) {
                if (!detail::Byte(token.substr(i, 2), true, byte))
                    return std::unexpected("bytes are hex pairs or ?? , not '" + std::string(token) + "'");
                add(byte, true);
            }
        }
        if (pattern.Size() > MaxPattern)
            return std::unexpected("pattern longer than " + std::to_string(MaxPattern) + " bytes");
    }
    if (pattern.bytes.empty()) return std::unexpected("empty pattern");
    return pattern;
}

// Every byte fixed (sig --bytes).
inline Pattern FromBytes(std::span<const uint8_t> bytes) {
    return {std::vector<uint8_t>(bytes.begin(), bytes.end()), std::vector<bool>(bytes.size(), true)};
}

// "48 8b ?? 89".
inline std::string Text(const Pattern& pattern) {
    std::string text;
    char cell[4];
    for (size_t i = 0; i < pattern.Size(); ++i) {
        if (i) text += ' ';
        if (!pattern.fixed[i]) { text += "??"; continue; }
        snprintf(cell, sizeof(cell), "%02x", pattern.bytes[i]);
        text += cell;
    }
    return text;
}
// "0x48, 0x8B, -1, 0x89".
inline std::string List(const Pattern& pattern) {
    std::string text;
    char cell[8];
    for (size_t i = 0; i < pattern.Size(); ++i) {
        if (i) text += ", ";
        if (!pattern.fixed[i]) { text += "-1"; continue; }
        snprintf(cell, sizeof(cell), "0x%02X", pattern.bytes[i]);
        text += cell;
    }
    return text;
}

// The run of fixed bytes the search looks for: the longest (the first of
// equals), at most MaxSearchPattern bytes of it.
struct Anchor { size_t offset = 0, length = 0; };
inline std::expected<Anchor, std::string> ChooseAnchor(const Pattern& pattern) {
    Anchor best;
    for (size_t at = 0; at < pattern.Size();) {
        if (!pattern.fixed[at]) { ++at; continue; }
        size_t end = at;
        while (end < pattern.Size() && pattern.fixed[end]) ++end;
        if (end - at > best.length) best = {at, end - at};
        at = end;
    }
    if (best.length < MinAnchor)
        return std::unexpected("the pattern needs at least " + std::to_string(MinAnchor) + " fixed bytes in a row");
    best.length = std::min(best.length, MaxSearchPattern);
    return best;
}

inline bool Matches(const Pattern& pattern, std::span<const uint8_t> bytes) {
    if (bytes.size() < pattern.Size()) return false;
    for (size_t i = 0; i < pattern.Size(); ++i)
        if (pattern.fixed[i] && bytes[i] != pattern.bytes[i]) return false;
    return true;
}

// A reference signature: target = match + next + signed value at match + field.
struct Resolve {
    uint32_t next = 0, field = 0, size = 4;   // size 1, 2 or 4.
    bool operator==(const Resolve&) const = default;
};
// "next,field[,size]".
inline std::expected<Resolve, std::string> ParseResolve(std::string_view text) {
    Resolve resolve;
    uint32_t* parts[] = {&resolve.next, &resolve.field, &resolve.size};
    size_t count = 0;
    for (size_t at = 0; at <= text.size() && count < 4;) {
        const size_t comma = std::min(text.find(',', at), text.size());
        const auto part = text.substr(at, comma - at);
        uint64_t value = 0;
        const bool hex = part.size() > 2 && part[0] == '0' && (part[1] == 'x' || part[1] == 'X');
        const auto digits = hex ? part.substr(2) : part;
        if (digits.empty() || digits.size() > 8 || count == 3) return std::unexpected("--resolve takes next,field[,size]");
        for (const char c : digits) {
            const int d = hex ? detail::Hex(c) : (c >= '0' && c <= '9' ? c - '0' : -1);
            if (d < 0) return std::unexpected("--resolve takes next,field[,size]");
            value = value * (hex ? 16 : 10) + static_cast<uint64_t>(d);
        }
        if (value > UINT32_MAX) return std::unexpected("--resolve takes next,field[,size]");
        *parts[count++] = static_cast<uint32_t>(value);
        at = comma + 1;
    }
    if (count < 2) return std::unexpected("--resolve takes next,field[,size]");
    if (resolve.size != 1 && resolve.size != 2 && resolve.size != 4) return std::unexpected("resolve size is 1, 2 or 4");
    if (resolve.field + resolve.size > MaxPattern + Extension)
        return std::unexpected("the resolve field lies past the bytes read");
    return resolve;
}
}
