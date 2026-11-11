// reagentlive CLI: which functions near an address use given struct offsets
// (`touches`). Finds the code behind an unreflected member: decode every
// function the exception directory lists around a known function (an exec
// thunk, a getter) and keep memory operands [reg + disp] whose displacement
// is one of the offsets. Console only, pure: code bytes come in, so tests feed
// fixed bytes.
#pragma once
#include <Zydis/Zydis.h>
#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/syntax/command.h"
#include "reagentlive/frontends/cli/code/disasm.h"

namespace reagentlive::cli::touch {
constexpr size_t MaxRows = 24, MaxUsesPerRow = 12;

struct Use {
    uint64_t instruction = 0;
    uint32_t offset = 0;
    bool write = false;  // The operand is written (a store or read-modify-write).
    std::string text;
};

// Uses in straight code [address, address + code.size()). Undecodable bytes
// (data between chunks) are stepped over one at a time. Stack slots
// ([rsp + disp]) and RIP-relative globals are never struct members.
inline std::vector<Use> InCode(std::span<const uint8_t> code, uint64_t address, std::span<const uint32_t> offsets) {
    static const dis::detail::Decoder decoder; // Console only: thread-safe statics are fine here.
    std::vector<Use> uses;
    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
    for (size_t at = 0; at < code.size();) {
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder.decoder, code.data() + at, code.size() - at, &instruction, operands))) {
            ++at;
            continue;
        }
        for (uint8_t i = 0; i < instruction.operand_count_visible; ++i) {
            const auto& operand = operands[i];
            if (operand.type != ZYDIS_OPERAND_TYPE_MEMORY || operand.mem.base == ZYDIS_REGISTER_NONE ||
                operand.mem.base == ZYDIS_REGISTER_RIP || operand.mem.base == ZYDIS_REGISTER_RSP ||
                !operand.mem.disp.has_displacement || operand.mem.disp.value <= 0 || operand.mem.disp.value > UINT32_MAX) continue;
            const auto disp = static_cast<uint32_t>(operand.mem.disp.value);
            if (std::ranges::find(offsets, disp) == offsets.end()) continue;
            Use use{address + at, disp, (operand.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) != 0, {}};
            char text[96];
            if (ZYAN_SUCCESS(ZydisFormatterFormatInstruction(&decoder.formatter, &instruction, operands,
                    instruction.operand_count_visible, text, sizeof(text), address + at, nullptr))) use.text = text;
            uses.push_back(std::move(use));
        }
        at += instruction.length;
    }
    return uses;
}

// One function's uses, ranked by how many of the asked offsets it touches.
struct Row {
    uint64_t start = 0, end = 0;
    std::vector<Use> uses;
    size_t distinct = 0;
    bool writes = false;
};
inline void Rank(std::vector<Row>& rows) {
    for (auto& row : rows) {
        std::vector<uint32_t> seen;
        for (const auto& use : row.uses) {
            if (std::ranges::find(seen, use.offset) == seen.end()) seen.push_back(use.offset);
            row.writes = row.writes || use.write;
        }
        row.distinct = seen.size();
    }
    std::ranges::stable_sort(rows, [](const Row& a, const Row& b) {
        if (a.distinct != b.distinct) return a.distinct > b.distinct;
        if (a.writes != b.writes) return a.writes;
        return a.uses.size() > b.uses.size();
    });
}
}
