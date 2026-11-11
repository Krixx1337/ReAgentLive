// reagentlive CLI: what a loaded module's PE headers say, read once per module:
// the exception directory (function bounds) and the section table (which
// addresses are code). Pure: reads come in as a callback.
#pragma once
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <vector>
#include "reagentlive/core/client.h"
#include "reagentlive/frontends/cli/code/unwind.h"

namespace reagentlive::cli::image {
using ReadFn = sig::ReadFn;
constexpr uint32_t Executable = 0x20000000;  // IMAGE_SCN_MEM_EXECUTE.
constexpr uint32_t Writable = 0x80000000;    // IMAGE_SCN_MEM_WRITE.
constexpr uint32_t Code = 0x20;              // IMAGE_SCN_CNT_CODE.
constexpr size_t MaxSections = 96;

struct Section {
    std::string name;
    uint32_t rva = 0, size = 0, flags = 0;
    bool Exec() const { return flags & (Executable | Code); }
    bool operator==(const Section&) const = default;
};
struct Layout {
    unwind::Directory exception;
    std::vector<Section> sections;
    bool operator==(const Layout&) const = default;
    const Section* At(uint64_t rva) const {
        for (const auto& section : sections)
            if (rva >= section.rva && rva - section.rva < section.size) return &section;
        return nullptr;
    }
};

// Signature, file header, optional header (PE32+ only) and section table.
inline std::expected<Layout, std::string> ReadLayout(const ReadFn& read, const Module& module) {
    auto dos = unwind::detail::ReadOne(read, module.base + 0x3c, 4);
    if (!dos) return std::unexpected("the module's headers are unreadable");
    const uint32_t pe = unwind::detail::U32(*dos, 0);
    if (pe >= module.size || module.size - pe < 24) return std::unexpected("the module's PE header is malformed");
    auto file = unwind::detail::ReadOne(read, module.base + pe, 24);
    if (!file) return std::unexpected("the module's PE header is unreadable");
    uint16_t count = 0, optional = 0;
    memcpy(&count, file->data() + 6, 2);
    memcpy(&optional, file->data() + 20, 2);
    if (unwind::detail::U32(*file, 0) != 0x00004550) return std::unexpected("not a PE image");
    const uint64_t rest = uint64_t{optional} + uint64_t{count} * 40;
    if (optional < 112 || count > MaxSections || rest > MaxRead - 4 || module.size - pe - 24 < rest)
        return std::unexpected("the module's PE header is malformed");
    auto headers = unwind::detail::ReadOne(read, module.base + pe + 24, rest);
    if (!headers) return std::unexpected("the module's PE header is unreadable");
    const auto& h = *headers;
    if (h[0] != 0x0b || h[1] != 0x02) return std::unexpected("not a PE32+ image");
    Layout layout;
    // NumberOfRvaAndSizes at 108; the exception directory is entry 3 (from 112).
    if (unwind::detail::U32(h, 108) > 3 && optional >= 112 + 4 * 8) {
        unwind::Directory directory{unwind::detail::U32(h, 136), unwind::detail::U32(h, 140)};
        if (directory.rva && directory.size >= unwind::EntrySize && directory.rva < module.size &&
            module.size - directory.rva >= directory.size) layout.exception = directory;
    }
    for (uint16_t i = 0; i < count; ++i) {
        const size_t at = optional + i * 40u;
        Section section;
        const char* name = reinterpret_cast<const char*>(h.data() + at);
        section.name.assign(name, strnlen(name, 8));
        section.size = unwind::detail::U32(h, at + 8);
        section.rva = unwind::detail::U32(h, at + 12);
        section.flags = unwind::detail::U32(h, at + 36);
        layout.sections.push_back(std::move(section));
    }
    return layout;
}
}
