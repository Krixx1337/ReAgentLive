// reagentlive CLI: what a reference to an address is, from the bytes in front of
// its field (`xrefs`). The agent finds 4-byte fields that land on the address
// (relative) and 8-byte values that equal it (pointer); this names each from
// the opcode and ModRM bytes before the field. A byte reading, not a decoder:
// a linear decoder cannot walk backwards to an instruction start.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace reagentlive::cli::xref {
constexpr size_t Before = 4;               // Bytes read in front of each field.
constexpr size_t Window = Before + 4;      // And the field itself.
constexpr size_t MaxRelative = 1024;       // Relative hits scanned before `truncated`.
constexpr size_t MaxPointers = 512;        // Pointer hits likewise.

// call (E8), jmp (E9), jcc (0F 8x), lea, load, store and rip (another opcode
// with a [rip + disp32] operand) for relative fields; pointer for absolute
// ones; unknown for a relative hit with none of those in front (most likely
// chance bytes).
struct Hit {
    uint64_t field = 0;
    std::string_view kind;
    std::optional<uint64_t> instruction;   // Where the kind fixes the length before the field.
    bool operator==(const Hit&) const = default;
};

// `window`: the Before bytes in front of the field and its 4 bytes (bytes
// before the module are zero). The field lands on target + slack, where the
// slack is the immediate after it (0, 1, 2 or 4).
inline Hit Classify(uint64_t field, std::span<const uint8_t, Window> window, uint64_t target) {
    int32_t value;
    memcpy(&value, window.data() + Before, 4);
    const uint64_t slack = target - (field + 4) - static_cast<uint64_t>(static_cast<int64_t>(value));
    const uint8_t b1 = window[3], b2 = window[2], b3 = window[1], b4 = window[0];
    const auto rex = [](uint8_t b) { return (b & 0xF0) == 0x40; };
    if (slack == 0 && b1 == 0xE8) return {field, "call", field - 1};
    if (slack == 0 && b1 == 0xE9) return {field, "jmp", field - 1};
    if (slack == 0 && b2 == 0x0F && (b1 & 0xF0) == 0x80) return {field, "jcc", field - 2};
    if ((b1 & 0xC7) != 0x05) return {field, "unknown", std::nullopt}; // ModRM mod 00, r/m 101: [rip + disp32].
    const uint8_t modrm = b1, op = b2;
    const int reg = (modrm >> 3) & 7;
    // Start of an instruction whose opcode sits at `opcode`: an optional REX
    // in front, and before it (only where `operand16` applies) a 66 prefix.
    const auto start = [&](uint64_t opcode, size_t index, bool operand16) -> std::pair<uint64_t, bool> {
        uint64_t at = opcode;
        bool wide16 = false;
        if (index >= 1 && rex(window[index - 1])) { --at; --index; }
        if (operand16 && index >= 1 && window[index - 1] == 0x66) { --at; wide16 = true; }
        return {at, wide16};
    };
    if (b3 == 0x0F && (op == 0xB6 || op == 0xB7 || op == 0xBE || op == 0xBF)) { // movzx, movsx.
        if (slack) return {field, "unknown", std::nullopt};
        const uint64_t at = field - 3 - (rex(b4) ? 1 : 0);
        return {field, "load", at};
    }
    if (b3 == 0x0F) return {field, "rip", std::nullopt};   // Another two-byte opcode.
    switch (op) {
    case 0x8D:
        if (slack) return {field, "unknown", std::nullopt};
        return {field, "lea", start(field - 2, 2, false).first};
    case 0x8A: case 0x8B:
        if (slack) return {field, "unknown", std::nullopt};
        return {field, "load", start(field - 2, 2, op == 0x8B).first};
    case 0x63:
        if (slack || !rex(b3)) return {field, "rip", std::nullopt};
        return {field, "load", field - 3};                  // movsxd r64, [rip].
    case 0x88: case 0x89:
        if (slack) return {field, "unknown", std::nullopt};
        return {field, "store", start(field - 2, 2, op == 0x89).first};
    case 0xC6:
        if (reg || slack != 1) return {field, reg ? "rip" : "unknown", std::nullopt};
        return {field, "store", start(field - 2, 2, false).first};
    case 0xC7: {
        if (reg) return {field, "rip", std::nullopt};
        const auto [at, wide16] = start(field - 2, 2, true);
        if (slack != (wide16 ? 2u : 4u)) return {field, "unknown", std::nullopt};
        return {field, "store", at};
    }
    default:
        return {field, "rip", std::nullopt};
    }
}

constexpr std::array<std::string_view, 9> Kinds = {
    "call", "jmp", "jcc", "lea", "load", "store", "rip", "pointer", "unknown"};
}
