// Shared by the fast-lane tests: a fake module image served from a byte vector
// (PE headers, an exception directory, holes), and byte helpers.
#pragma once
#include <cstring>
#include <expected>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/code/signature.h"
#include "reagentlive/frontends/cli/code/unwind.h"
#include "doctest.h"

namespace code_support {
using namespace reagentlive;
using namespace reagentlive::cli;
inline constexpr uint64_t Base = 0x140000000;
inline constexpr uint64_t PdataRva = 0x5000;

struct Image {
    Module module;
    std::vector<uint8_t> bytes;
    std::vector<std::pair<uint64_t, uint64_t>> holes; // Unreadable ranges.
    size_t requests = 0;

    explicit Image(size_t size = 0x20000) : module{"Game.exe", Base, size}, bytes(size, 0xcc) {}
    void Put(uint64_t address, std::vector<uint8_t> data) {
        std::memcpy(bytes.data() + (address - module.base), data.data(), data.size());
    }
    void Put32(uint64_t address, uint32_t value) {
        std::memcpy(bytes.data() + (address - module.base), &value, 4);
    }
    // PE32+ headers with an exception directory of `count` entries at
    // PdataRva and two sections: .text (code, 0x1000..0x5000) and .rdata.
    void Headers(size_t count) {
        constexpr uint64_t Pe = Base + 0x80;
        Put32(Base + 0x3c, 0x80);
        Put32(Pe, 0x00004550);
        Put(Pe + 6, {2, 0});            // NumberOfSections.
        Put(Pe + 20, {0xf0, 0});        // SizeOfOptionalHeader.
        Put(Pe + 24, {0x0b, 0x02});
        Put32(Pe + 24 + 108, 16);
        Put32(Pe + 160, PdataRva);
        Put32(Pe + 164, static_cast<uint32_t>(count * unwind::EntrySize));
        const uint64_t sections = Pe + 24 + 0xf0;
        Put(sections, {'.', 't', 'e', 'x', 't', 0, 0, 0});
        Put32(sections + 8, 0x4000);
        Put32(sections + 12, 0x1000);
        Put32(sections + 36, 0x60000020);
        Put(sections + 40, {'.', 'r', 'd', 'a', 't', 'a', 0, 0});
        Put32(sections + 48, 0x3000);
        Put32(sections + 52, 0x5000);
        Put32(sections + 76, 0x40000040);
    }
    void Entry(size_t index, uint32_t begin, uint32_t end, uint32_t unwind) {
        const uint64_t at = Base + PdataRva + index * unwind::EntrySize;
        Put32(at, begin);
        Put32(at + 4, end);
        Put32(at + 8, unwind);
    }
    sig::ReadFn Read() {
        return [this](std::span<const ReadSpan> spans)
                   -> std::expected<std::vector<std::optional<std::vector<uint8_t>>>, std::string> {
            ++requests;
            REQUIRE(spans.size() <= MaxBatchReads);
            REQUIRE(ReadBatchSize(spans));
            std::vector<std::optional<std::vector<uint8_t>>> items;
            for (const auto& span : spans) {
                bool hole = span.address < module.base || span.address + span.length > module.base + module.size;
                for (const auto& [from, to] : holes) hole |= span.address < to && span.address + span.length > from;
                if (hole) { items.emplace_back(); continue; }
                const auto* at = bytes.data() + (span.address - module.base);
                items.emplace_back(std::vector<uint8_t>(at, at + span.length));
            }
            return items;
        };
    }
};

inline std::set<std::string> Keys(const Json& object) {
    std::set<std::string> keys;
    for (const auto& item : object.items()) keys.insert(item.key());
    return keys;
}
using KeySet = std::set<std::string>;
inline std::vector<uint8_t> Rel32(int32_t value) {
    std::vector<uint8_t> out(4);
    std::memcpy(out.data(), &value, 4);
    return out;
}
inline std::vector<uint8_t> Join(std::initializer_list<std::vector<uint8_t>> parts) {
    std::vector<uint8_t> out;
    for (const auto& part : parts) out.insert(out.end(), part.begin(), part.end());
    return out;
}
}
