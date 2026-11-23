// reagentlive CLI, Unreal pack: typed call inputs as parameter-block bytes.
// Pure; shared with tests. Name lookups need the client: the caller passes them in.
#pragma once
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>
#include "reagentlive/frontends/cli/call/values.h"
#include "reagentlive/packs/ue/client_ue.h"

namespace reagentlive::cli::pack_input {
constexpr unsigned MaxStructDepth = 4;

inline std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

// "(a, b=(1,2), c)" into its top-level items; false unless the whole text is
// one balanced parenthesised list. "()" has no items.
inline bool SplitTuple(std::string_view text, std::vector<std::string_view>& items) {
    items.clear();
    text = Trim(text);
    if (text.size() < 2 || text.front() != '(' || text.back() != ')') return false;
    text = text.substr(1, text.size() - 2);
    if (Trim(text).empty()) return true;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        const char c = i < text.size() ? text[i] : ',';
        if (c == '(') ++depth;
        else if (c == ')' && --depth < 0) return false;
        else if (c == ',' && depth == 0) {
            const auto item = Trim(text.substr(start, i - start));
            if (item.empty()) return false;
            items.push_back(item);
            start = i + 1;
        }
    }
    return depth == 0;
}

inline bool Signed(std::string_view s, int64_t& out) {
    out = 0;
    const bool negative = !s.empty() && s.front() == '-';
    if (negative) s.remove_prefix(1);
    uint64_t magnitude = 0;
    if (!text::Number(s, magnitude)) return false;
    if (negative ? magnitude > uint64_t{1} << 63 : magnitude > static_cast<uint64_t>(INT64_MAX)) return false;
    out = negative ? static_cast<int64_t>(0 - magnitude) : static_cast<int64_t>(magnitude);
    return true;
}

inline bool Real(std::string_view s, double& out) {
    out = 0;
    if (s.empty()) return false;
    const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), out);
    return error == std::errc{} && end == s.data() + s.size() && std::isfinite(out);
}

// Bool, integer, float and byte/enum properties. False for anything else or a
// value outside the property's range.
inline bool PackScalar(const ue::Field& field, std::string_view value, std::span<uint8_t> bytes) {
    const std::string_view type = field.type;
    value = Trim(value);
    if (type == "BoolProperty") {
        if (bytes.size() != 1) return false;
        bool on;
        if (value == "true" || value == "1") on = true;
        else if (value == "false" || value == "0") on = false;
        else return false;
        const uint8_t mask = field.boolMask ? field.boolMask : 0xff;
        bytes[0] = static_cast<uint8_t>((bytes[0] & ~mask) | (on ? mask : 0));
        return true;
    }
    if (type == "FloatProperty" || type == "DoubleProperty") {
        double number;
        if (!Real(value, number)) return false;
        if (type == "FloatProperty") {
            if (bytes.size() != 4 || std::fabs(number) > 3.4028234663852886e38) return false;
            const float narrow = static_cast<float>(number);
            memcpy(bytes.data(), &narrow, 4);
        } else {
            if (bytes.size() != 8) return false;
            memcpy(bytes.data(), &number, 8);
        }
        return true;
    }
    const bool isSigned = type == "Int8Property" || type == "Int16Property" || type == "IntProperty" ||
        type == "Int64Property";
    const bool isUnsigned = type == "ByteProperty" || type == "EnumProperty" || type == "UInt16Property" ||
        type == "UInt32Property" || type == "UInt64Property";
    if ((!isSigned && !isUnsigned) || (bytes.size() != 1 && bytes.size() != 2 && bytes.size() != 4 && bytes.size() != 8))
        return false;
    const unsigned bits = static_cast<unsigned>(bytes.size()) * 8;
    uint64_t raw = 0;
    if (isSigned) {
        int64_t number;
        if (!Signed(value, number)) return false;
        if (bits < 64) {
            const int64_t limit = int64_t{1} << (bits - 1);
            if (number < -limit || number >= limit) return false;
        }
        raw = static_cast<uint64_t>(number);
    } else {
        if (!text::Number(value, raw) || (bits < 64 && raw >> bits)) return false;
    }
    memcpy(bytes.data(), &raw, bytes.size()); // Little-endian: the low bytes.
    return true;
}

// A struct that holds one name and nothing else (a gameplay tag) takes the
// name alone: `Tag=Some.Tag.Name`.
inline const ue::Field* LoneName(const std::vector<ue::Field>& members) {
    return members.size() == 1 && members.front().type == "NameProperty" ? &members.front() : nullptr;
}

// A scalar, a name, or a plain struct as "(Member=value, ...)" or "(value, ...)"
// in member order; members left out keep their bytes, nested structs nest.
// `fieldsOf(ref)` gives a struct's member fields (offsets inside the struct) or
// null; `nameOf(text, index, number)` resolves a name's pooled index and
// instance number, false when the pool has no such name.
template<class FieldsOf, class NameOf>
bool PackValue(const ue::Field& field, std::string_view value, std::span<uint8_t> bytes, const FieldsOf& fieldsOf,
               const NameOf& nameOf, unsigned depth = 0) {
    value = Trim(value);
    if (field.type == "NameProperty") {
        int32_t index = 0;
        uint32_t number = 0;
        if (bytes.size() != 8 || value.empty() || !nameOf(value, index, number)) return false;
        memcpy(bytes.data(), &index, sizeof(index));
        memcpy(bytes.data() + sizeof(index), &number, sizeof(number));
        return true;
    }
    if (field.type != "StructProperty") return PackScalar(field, value, bytes);
    if (depth >= MaxStructDepth || !field.structType.address) return false;
    const std::vector<ue::Field>* members = fieldsOf(field.structType);
    if (!members) return false;
    const auto into = [&](const ue::Field& member, std::string_view text) {
        if (!member.size || size_t{member.offset} + member.size > bytes.size()) return false;
        return PackValue(member, text, bytes.subspan(member.offset, member.size), fieldsOf, nameOf, depth + 1);
    };
    if (!value.starts_with('('))
        if (const auto* name = LoneName(*members)) return into(*name, value);
    std::vector<std::string_view> items;
    if (!SplitTuple(value, items) || items.size() > members->size()) return false;
    const bool named = !items.empty() && items.front().find('=') != std::string_view::npos &&
        items.front().find('(') > items.front().find('=');
    std::vector<bool> used(members->size());
    for (size_t i = 0; i < items.size(); ++i) {
        size_t index = i;
        std::string_view text = items[i];
        if (named) {
            const auto equals = text.find('=');
            if (equals == std::string_view::npos) return false;
            const auto name = Trim(text.substr(0, equals));
            index = members->size();
            for (size_t m = 0; m < members->size(); ++m) if ((*members)[m].name == name) { index = m; break; }
            if (index == members->size()) return false;
            text = text.substr(equals + 1);
        }
        if (used[index]) return false;
        used[index] = true;
        if (!into((*members)[index], text)) return false;
    }
    return true;
}
}
