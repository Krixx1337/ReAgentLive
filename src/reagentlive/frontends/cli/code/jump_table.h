// reagentlive CLI: switch jump tables in listed x64 code, for saves
// (`disasm` with `save`). The MSVC form, and the table-relative form some
// compilers emit, end in an indirect jump through a register:
//
//   cmp   idx, N ; ja default          (jae: N cases; or the inverted
//                                       jbe/jb to the dispatch)
//   [movzx idx, byte [rY + j + D2]]    (a byte index table over j)
//   lea   rX, [rip + ...]              (the image base, or the table)
//   mov|movsxd r, dword [rX + idx*4 + D]
//   add   r, rB                        (rB from a lea [rip + ...])
//   jmp   r
//
// Each entry is 32 bits; its target is rB's value plus the entry (zero- or
// sign-extended as the load does). Pure: rows in, sites out; the caller
// reads the tables and keeps the targets inside code.
#pragma once
#include <Zydis/Zydis.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>
#include "reagentlive/frontends/cli/code/disasm.h"

namespace reagentlive::cli::jumptab {
constexpr uint32_t MaxCases = 512;    // Entries read per table (one read: 2 KB).
constexpr size_t LookBack = 32;       // Instructions searched before the jump.

// One switch: the jump, its table of `count` 32-bit entries and the base
// each entry is added to. With an index table: `count` comes from its
// `indexCount` bytes (the largest plus one), once they are read.
struct Site {
    uint64_t jump = 0, table = 0, base = 0;
    uint32_t count = 0;
    bool signedEntries = false;
    std::optional<uint64_t> indexTable;
    uint32_t indexCount = 0;
    bool operator==(const Site&) const = default;
};

namespace detail {
struct Decoded {
    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
};
inline std::optional<Decoded> Decode(const dis::Instruction& row) {
    static const dis::detail::Decoder decoder; // Console only: thread-safe statics are fine here.
    Decoded out;
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder.decoder, row.bytes.data(), row.bytes.size(), &out.instruction,
                                             out.operands))) return std::nullopt;
    return out;
}
inline ZydisRegister Wide(ZydisRegister reg) { return ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, reg); }
inline bool IsReg(const ZydisDecodedOperand& operand) { return operand.type == ZYDIS_OPERAND_TYPE_REGISTER; }
inline bool WritesReg(const Decoded& d, ZydisRegister reg) {
    return d.instruction.operand_count_visible && IsReg(d.operands[0]) && Wide(d.operands[0].reg.value) == reg &&
        (d.operands[0].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE);
}
}

// The value a register holds at row `at`: the target of the nearest earlier
// `lea reg, [rip + disp]` in `decoded`, when nothing else wrote it between.
inline std::optional<uint64_t> RipValue(std::span<const dis::Instruction> rows,
                                        std::span<const std::optional<detail::Decoded>> decoded, size_t at,
                                        ZydisRegister reg, size_t floor) {
    for (size_t i = at; i-- > floor;) {
        if (!decoded[i] || !detail::WritesReg(*decoded[i], reg)) continue;
        if (decoded[i]->instruction.mnemonic == ZYDIS_MNEMONIC_LEA && rows[i].ripTarget && rows[i].target) return rows[i].target;
        return std::nullopt;
    }
    return std::nullopt;
}

// Whether the instruction's first operand is a register in `names` (widened).
inline bool IsRegOf(const detail::Decoded& d, std::span<const ZydisRegister> names) {
    return d.instruction.operand_count_visible && detail::IsReg(d.operands[0]) &&
        std::ranges::find(names, detail::Wide(d.operands[0].reg.value)) != names.end();
}

// The bound on an index register before row `at`: `cmp reg, imm` (through
// register copies and extensions of it) followed by ja (imm + 1 cases) or
// jae (imm cases) to the default, or by their inverse jbe (imm + 1) or jb
// (imm) to the dispatch. Zero when none is found.
inline uint32_t Bound(std::span<const std::optional<detail::Decoded>> decoded, size_t at, ZydisRegister reg, size_t floor) {
    std::vector<ZydisRegister> names{reg};
    for (size_t i = at; i-- > floor;) {
        if (!decoded[i]) continue;
        const auto& d = *decoded[i];
        const auto mnemonic = d.instruction.mnemonic;
        if (mnemonic == ZYDIS_MNEMONIC_CMP && IsRegOf(d, names) && d.instruction.operand_count_visible == 2 &&
            d.operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
            for (size_t j = i + 1; j < at; ++j) {
                if (!decoded[j]) continue;
                const auto branch = decoded[j]->instruction.mnemonic;
                const uint64_t limit = d.operands[1].imm.value.u;
                if (limit >= MaxCases) return 0;
                if (branch == ZYDIS_MNEMONIC_JNBE || branch == ZYDIS_MNEMONIC_JBE) return static_cast<uint32_t>(limit + 1);
                if (branch == ZYDIS_MNEMONIC_JNB || branch == ZYDIS_MNEMONIC_JB) return static_cast<uint32_t>(limit);
                if (decoded[j]->instruction.meta.category == ZYDIS_CATEGORY_COND_BR) return 0;
            }
            return 0;
        }
        // A copy or extension into a tracked register: track its source too.
        if ((mnemonic == ZYDIS_MNEMONIC_MOV || mnemonic == ZYDIS_MNEMONIC_MOVSXD || mnemonic == ZYDIS_MNEMONIC_MOVZX) &&
            IsRegOf(d, names) && d.instruction.operand_count_visible == 2 && detail::IsReg(d.operands[1])) {
            names.push_back(detail::Wide(d.operands[1].reg.value));
            continue;
        }
        if (mnemonic == ZYDIS_MNEMONIC_CDQE) continue;
        if (std::ranges::any_of(names, [&](ZydisRegister name) { return detail::WritesReg(d, name); })) return 0;
    }
    return 0;
}

// Every switch the rows end in a jump through, in row order.
inline std::vector<Site> Find(std::span<const dis::Instruction> rows) {
    std::vector<std::optional<detail::Decoded>> decoded(rows.size());
    std::vector<Site> out;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].bytes.empty() || rows[i].target) continue; // A jmp through a register has no target.
        auto jump = detail::Decode(rows[i]);
        if (!jump || jump->instruction.mnemonic != ZYDIS_MNEMONIC_JMP || !detail::IsReg(jump->operands[0])) continue;
        const ZydisRegister r = detail::Wide(jump->operands[0].reg.value);
        const size_t floor = i > LookBack ? i - LookBack : 0;
        for (size_t k = floor; k < i; ++k) if (!decoded[k]) decoded[k] = detail::Decode(rows[k]);
        // add r, rB right before, then the load into r.
        size_t at = i;
        std::optional<ZydisRegister> base;
        while (at-- > floor) {
            if (!decoded[at]) break;
            const auto& d = *decoded[at];
            if (d.instruction.mnemonic == ZYDIS_MNEMONIC_ADD && detail::WritesReg(d, r) && detail::IsReg(d.operands[1])) {
                base = detail::Wide(d.operands[1].reg.value);
                break;
            }
            if (detail::WritesReg(d, r)) break;
        }
        if (!base || at == floor) continue;
        size_t load = at;
        std::optional<Site> site;
        while (load-- > floor) {
            if (!decoded[load]) break;
            const auto& d = *decoded[load];
            if (!detail::WritesReg(d, r)) continue;
            const auto mnemonic = d.instruction.mnemonic;
            const auto& mem = d.operands[1];
            if ((mnemonic != ZYDIS_MNEMONIC_MOV && mnemonic != ZYDIS_MNEMONIC_MOVSXD) || d.instruction.operand_count_visible != 2 ||
                mem.type != ZYDIS_OPERAND_TYPE_MEMORY || mem.size != 32 || mem.mem.scale != 4 ||
                mem.mem.index == ZYDIS_REGISTER_NONE || mem.mem.base == ZYDIS_REGISTER_NONE)
                break;
            const auto tableBase = RipValue(rows, decoded, load, detail::Wide(mem.mem.base), floor);
            const auto addBase = RipValue(rows, decoded, at, *base, floor);
            if (!tableBase || !addBase) break;
            Site found;
            found.jump = rows[i].address;
            found.table = *tableBase + static_cast<uint64_t>(mem.mem.disp.value);
            found.base = *addBase;
            found.signedEntries = mnemonic == ZYDIS_MNEMONIC_MOVSXD;
            const ZydisRegister index = detail::Wide(mem.mem.index);
            // A byte index table loaded into the index: its bound counts its bytes.
            for (size_t b = load; b-- > floor;) {
                if (!decoded[b] || !detail::WritesReg(*decoded[b], index)) continue;
                const auto& byteLoad = *decoded[b];
                const auto& bytes = byteLoad.operands[1];
                if (byteLoad.instruction.mnemonic == ZYDIS_MNEMONIC_MOVZX && bytes.type == ZYDIS_OPERAND_TYPE_MEMORY &&
                    bytes.size == 8 && bytes.mem.index != ZYDIS_REGISTER_NONE && bytes.mem.scale <= 1) {
                    const auto indexBase = RipValue(rows, decoded, b, detail::Wide(bytes.mem.base), floor);
                    const uint32_t cases = Bound(decoded, b, detail::Wide(bytes.mem.index), floor);
                    if (indexBase && cases) {
                        found.indexTable = *indexBase + static_cast<uint64_t>(bytes.mem.disp.value);
                        found.indexCount = cases;
                        site = found;
                    }
                }
                break;
            }
            if (!site) {
                found.count = Bound(decoded, load, index, floor);
                if (found.count) site = found;
            }
            break;
        }
        if (site) out.push_back(*site);
    }
    return out;
}

// The dword count an index table gives: its largest byte plus one.
inline uint32_t CountFromIndex(std::span<const uint8_t> indexBytes) {
    return indexBytes.empty() ? 0 : static_cast<uint32_t>(*std::ranges::max_element(indexBytes)) + 1;
}

// The targets a table's entries give, in entry order, each once.
inline std::vector<uint64_t> Targets(const Site& site, std::span<const uint8_t> entries) {
    std::vector<uint64_t> out;
    for (size_t at = 0; at + 4 <= entries.size(); at += 4) {
        uint32_t raw = 0;
        memcpy(&raw, entries.data() + at, 4);
        const uint64_t offset = site.signedEntries ? static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(raw))) : raw;
        const uint64_t target = site.base + offset;
        if (std::ranges::find(out, target) == out.end()) out.push_back(target);
    }
    return out;
}
}
