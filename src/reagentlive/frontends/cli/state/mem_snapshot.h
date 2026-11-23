// reagentlive CLI: a raw memory range kept between runs (mem-snap) and the
// typed comparison against it (mem-diff): changed, unchanged, increased or
// decreased per aligned element, and with `keep` a narrowing set of candidates
// that later diffs compare alone. Pure: streams in and out, no files opened.
#pragma once
#include <algorithm>
#include <cmath>
#include <type_traits>
#include <cstdint>
#include <cstring>
#include <istream>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/call/command.h"

namespace reagentlive::cli::memsnap {
constexpr uint32_t Magic = 0x534d4c52;                  // "RLMS".
constexpr uint32_t Format = 1;

struct Snapshot {
    uint64_t address = 0;
    uint32_t owner = 0;         // The agent instance it was read from; another one's is stale.
    uint64_t tick = 0;
    std::vector<uint8_t> bytes;
    // Offsets still in the running after a `keep` diff, with the element type
    // they were narrowed under; empty with no type: every element.
    std::string type;
    std::vector<uint32_t> candidates;
    bool narrowed = false;
};

inline void Save(std::ostream& out, const Snapshot& s) {
    const auto put = [&out](const void* data, size_t size) { out.write(static_cast<const char*>(data), size); };
    const uint64_t length = s.bytes.size(), candidates = s.candidates.size();
    const uint32_t typeLength = static_cast<uint32_t>(s.type.size()), narrowed = s.narrowed;
    put(&Magic, 4); put(&Format, 4); put(&s.address, 8); put(&s.owner, 4); put(&s.tick, 8); put(&length, 8);
    put(s.bytes.data(), s.bytes.size());
    put(&typeLength, 4); put(s.type.data(), s.type.size()); put(&narrowed, 4);
    put(&candidates, 8); put(s.candidates.data(), s.candidates.size() * 4);
}

inline std::optional<Snapshot> Load(std::istream& in) {
    const auto get = [&in](void* data, size_t size) { return static_cast<bool>(in.read(static_cast<char*>(data), size)); };
    Snapshot s;
    uint32_t magic = 0, format = 0, typeLength = 0, narrowed = 0;
    uint64_t length = 0, candidates = 0;
    if (!get(&magic, 4) || magic != Magic || !get(&format, 4) || format != Format || !get(&s.address, 8) ||
        !get(&s.owner, 4) || !get(&s.tick, 8) || !get(&length, 8) || length > MaxLength) return std::nullopt;
    s.bytes.resize(length);
    if (!get(s.bytes.data(), length) || !get(&typeLength, 4) || typeLength > 8) return std::nullopt;
    s.type.resize(typeLength);
    if (!get(s.type.data(), typeLength) || !get(&narrowed, 4) || !get(&candidates, 8) || candidates > length)
        return std::nullopt;
    s.narrowed = narrowed != 0;
    s.candidates.resize(candidates);
    if (!get(s.candidates.data(), candidates * 4)) return std::nullopt;
    return s;
}

enum class Test { Changed, Unchanged, Increased, Decreased };
inline std::optional<Test> TestNamed(std::string_view name) {
    if (name == "changed") return Test::Changed;
    if (name == "unchanged") return Test::Unchanged;
    if (name == "increased") return Test::Increased;
    if (name == "decreased") return Test::Decreased;
    return std::nullopt;
}

namespace detail {
template<class T> T Load(const uint8_t* at) {
    T value;
    memcpy(&value, at, sizeof(T));
    return value;
}
// -1, 0 or 1 for after against before; nullopt when they do not compare (NaN).
template<class T> std::optional<int> Order(const uint8_t* before, const uint8_t* after) {
    const T a = Load<T>(before), b = Load<T>(after);
    if constexpr (std::is_floating_point_v<T>)
        if (std::isnan(a) || std::isnan(b)) return std::nullopt;
    return b < a ? -1 : b > a ? 1 : 0;
}
inline std::optional<int> OrderOf(std::string_view type, const uint8_t* before, const uint8_t* after) {
    if (type == "i8") return Order<int8_t>(before, after);
    if (type == "u8") return Order<uint8_t>(before, after);
    if (type == "i16") return Order<int16_t>(before, after);
    if (type == "u16") return Order<uint16_t>(before, after);
    if (type == "i32") return Order<int32_t>(before, after);
    if (type == "u32") return Order<uint32_t>(before, after);
    if (type == "i64") return Order<int64_t>(before, after);
    if (type == "u64" || type == "ptr") return Order<uint64_t>(before, after);
    if (type == "f32") return Order<float>(before, after);
    if (type == "f64") return Order<double>(before, after);
    return std::nullopt;
}
}

struct Diff {
    uint64_t compared = 0, matched = 0;
    std::vector<uint32_t> offsets;   // Every match, for `keep`.
};

// Elements of `size` bytes every `size` bytes from the start (or the snapshot's candidates)
// that pass `compare` from `before` to `now`. Bytes compare for changed and
// unchanged; numbers for increased and decreased.
inline Diff Compare(const Snapshot& before, std::span<const uint8_t> now, std::string_view type, size_t size, Test test) {
    Diff out;
    const size_t length = std::min(before.bytes.size(), now.size());
    const auto check = [&](uint32_t offset) {
        if (size_t{offset} + size > length) return;
        ++out.compared;
        const uint8_t* a = before.bytes.data() + offset;
        const uint8_t* b = now.data() + offset;
        bool pass = false;
        if (test == Test::Changed || test == Test::Unchanged) {
            const bool same = memcmp(a, b, size) == 0;
            pass = test == Test::Changed ? !same : same;
        } else if (const auto order = detail::OrderOf(type, a, b)) {
            pass = test == Test::Increased ? *order > 0 : *order < 0;
        }
        if (!pass) return;
        ++out.matched;
        out.offsets.push_back(offset);
    };
    if (before.narrowed) {
        for (const uint32_t offset : before.candidates) check(offset);
    } else {
        for (size_t offset = 0; offset + size <= length; offset += size) check(static_cast<uint32_t>(offset));
    }
    return out;
}
}
