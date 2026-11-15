// reagentlive CLI: mem-snap and mem-diff results.
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/frontends/cli/state/mem_snapshot.h"
#include "reagentlive/frontends/cli/values/json.h"

namespace reagentlive::cli {
namespace shape {
struct MemSnap {
    Addr address;
    Location where;
    uint64_t length = 0, tick = 0;
    template<class V> static void Describe(V& v) {
        v("address", &MemSnap::address);
        v.flat("", &MemSnap::where);
        v("length", &MemSnap::length);
        v("tick", &MemSnap::tick);
    }
};
struct MemDiffRow {
    Addr address;
    Json before, after;
    template<class V> static void Describe(V& v) {
        v("address", &MemDiffRow::address);
        v("before", &MemDiffRow::before);
        v("after", &MemDiffRow::after);
    }
};
struct MemDiff {
    Addr address;
    Location where;
    uint64_t length = 0;
    std::string type, test;
    uint64_t compared = 0, matched = 0, tick = 0;
    std::vector<MemDiffRow> rows;
    bool capped = false;
    std::optional<bool> kept;
    template<class V> static void Describe(V& v) {
        v("address", &MemDiff::address);
        v.flat("", &MemDiff::where);
        v("length", &MemDiff::length);
        v("type", &MemDiff::type);
        v("test", &MemDiff::test, "changed|unchanged|increased|decreased");
        v("compared", &MemDiff::compared);
        v("matched", &MemDiff::matched);
        v("tick", &MemDiff::tick);
        v("rows", &MemDiff::rows);
        v("capped", &MemDiff::capped);
        v("kept", &MemDiff::kept);
    }
};
}

// One element as a number of its type.
inline Json MemValue(std::string_view type, std::span<const uint8_t> bytes) {
    if (type == "i8") return JsonNumber<int8_t>(bytes);
    if (type == "u8") return JsonNumber<uint8_t>(bytes);
    if (type == "i16") return JsonNumber<int16_t>(bytes);
    if (type == "u16") return JsonNumber<uint16_t>(bytes);
    if (type == "i32") return JsonNumber<int32_t>(bytes);
    if (type == "u32") return JsonNumber<uint32_t>(bytes);
    if (type == "i64") return JsonNumber<int64_t>(bytes);
    if (type == "f32") return JsonNumber<float>(bytes);
    if (type == "f64") return JsonNumber<double>(bytes);
    return HexNumber(JsonNumber<uint64_t>(bytes).get<uint64_t>()); // u64 and ptr as hex text.
}

inline shape::MemDiff MakeMemDiff(const memsnap::Snapshot& before, std::span<const uint8_t> now, std::string_view type,
                                  size_t size, std::string_view test, const memsnap::Diff& diff, size_t limit,
                                  uint64_t tick, std::span<const Module> modules) {
    shape::MemDiff out;
    out.address = before.address;
    out.where = Locate(before.address, modules);
    out.length = before.bytes.size();
    out.type = type;
    out.test = test;
    out.compared = diff.compared;
    out.matched = diff.matched;
    out.tick = tick;
    for (const uint32_t offset : diff.offsets) {
        if (out.rows.size() == limit) { out.capped = true; break; }
        out.rows.push_back({before.address + offset, MemValue(type, std::span(before.bytes).subspan(offset, size)),
                            MemValue(type, now.subspan(offset, size))});
    }
    return out;
}
}
