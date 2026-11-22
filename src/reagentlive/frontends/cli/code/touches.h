// reagentlive CLI: which functions near an address use given struct offsets
// (`touches`). Finds the code behind an unreflected member: decode every
// function the exception directory lists around a known function (an exec
// thunk, a getter), and the executable code it leaves out, split into pieces
// at returns and int3 padding; keep memory operands [reg + disp] whose
// displacement is one of the offsets. Console only, pure: code bytes come in,
// so tests feed fixed bytes.
#pragma once
#include <Zydis/Zydis.h>
#include <algorithm>
#include <cstdint>
#include <optional>
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

// Code no exception-table entry lists, cut into pieces that each end after a
// return or before a run of int3 padding; the padding itself is left out.
struct Piece { uint64_t start = 0, end = 0; };
inline std::vector<Piece> Pieces(std::span<const uint8_t> code, uint64_t address) {
    static const dis::detail::Decoder decoder; // Console only: thread-safe statics are fine here.
    std::vector<Piece> pieces;
    ZydisDecodedInstruction instruction;
    size_t begin = 0;
    const auto close = [&](size_t end) {
        if (end > begin) pieces.push_back({address + begin, address + end});
    };
    for (size_t at = 0; at < code.size();) {
        if (code[at] == 0xcc) {
            close(at);
            while (at < code.size() && code[at] == 0xcc) ++at;
            begin = at;
            continue;
        }
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder.decoder, nullptr, code.data() + at, code.size() - at,
                &instruction))) {
            ++at;
            continue;
        }
        at += instruction.length;
        if (instruction.mnemonic == ZYDIS_MNEMONIC_RET) {
            close(at);
            begin = at;
        }
    }
    close(code.size());
    return pieces;
}

// Entries shorter than this bound no unlisted code: a function with unwind
// data holds at least a stack adjustment and a return. Some packed images fill
// the directory with one-byte decoys, which would cut that code into slivers.
constexpr uint64_t MinEntryBytes = 4;
inline bool Bounds(uint64_t start, uint64_t end) { return end - start >= MinEntryBytes; }

// Where to cut the pieces around one address no entry lists: up to `reach`
// bytes each side, inside its section [low, high), clipped at the nearest
// entries before and after. Empty when an entry holds the address.
constexpr uint64_t PieceReach = 0x4000;
inline std::optional<Piece> Window(uint64_t address, uint64_t low, uint64_t high, std::span<const Piece> entries,
                                   uint64_t reach = PieceReach) {
    low = std::max(low, address - std::min(address, reach));
    high = std::min(high, address + reach);
    for (const auto& entry : entries) {
        if (!Bounds(entry.start, entry.end)) continue;
        if (entry.end <= address) low = std::max(low, entry.end);
        else if (entry.start > address) high = std::min(high, entry.start);
        else return std::nullopt;
    }
    if (address < low || address >= high) return std::nullopt;
    return Piece{low, high};
}

// The piece of `code` (at `address`, a boundary such as an entry's end) that
// holds `at`, cut as Pieces cuts it.
inline std::optional<Piece> PieceAt(std::span<const uint8_t> code, uint64_t address, uint64_t at) {
    for (const auto& piece : Pieces(code, address))
        if (piece.start <= at && at < piece.end) return piece;
    return std::nullopt;
}

// One function's uses, ranked by how many of the asked offsets it touches.
// `listed` is false for a piece of code outside every exception-table entry.
struct Row {
    uint64_t start = 0, end = 0;
    std::vector<Use> uses;
    size_t distinct = 0;
    bool writes = false;
    bool listed = true;
};
// With `reads`: count only the offsets read, and drop rows that read none or
// write every offset they read (struct copies, constructors).
inline void Rank(std::vector<Row>& rows, bool reads = false) {
    for (auto& row : rows) {
        std::vector<uint32_t> seen, read, written;
        for (const auto& use : row.uses) {
            if (std::ranges::find(seen, use.offset) == seen.end()) seen.push_back(use.offset);
            auto& side = use.write ? written : read;
            if (std::ranges::find(side, use.offset) == side.end()) side.push_back(use.offset);
            row.writes = row.writes || use.write;
        }
        row.distinct = reads ? read.size() : seen.size();
        if (reads && std::ranges::all_of(read, [&](uint32_t o) { return std::ranges::find(written, o) != written.end(); }))
            row.distinct = 0;
    }
    if (reads) std::erase_if(rows, [](const Row& row) { return row.distinct == 0; });
    std::ranges::stable_sort(rows, [](const Row& a, const Row& b) {
        if (a.distinct != b.distinct) return a.distinct > b.distinct;
        if (a.writes != b.writes) return a.writes;
        return a.uses.size() > b.uses.size();
    });
}
}
