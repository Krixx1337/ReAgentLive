// reagentlive CLI: which functions near an address use given struct offsets
// (`touches`). Finds the code behind an unreflected member: decode every
// function the exception directory lists around a known function (an exec
// thunk, a getter), and the executable code it leaves out, split into pieces
// at returns and int3 padding; keep memory operands [reg + disp] whose
// displacement is one of the offsets. disp-scan sweeps a whole image's code
// for one displacement the same way. Console only, pure: code bytes come in,
// so tests feed fixed bytes.
#pragma once
#include <Zydis/Zydis.h>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/call/command.h"
#include "reagentlive/frontends/cli/code/disasm.h"

namespace reagentlive::cli::touch {
constexpr size_t MaxRows = 24, MaxUsesPerRow = 12;

struct Use {
    uint64_t instruction = 0;
    uint32_t offset = 0;
    bool write = false;  // The operand is written (a store or read-modify-write).
    std::string text;
};

// Whether an instruction's raw displacement can be one of `offsets`. Its
// operands are decoded only then: most instructions are passed over on the
// raw bytes alone. EVEX and MVEX scale an 8-bit displacement (disp8*N), so
// theirs is decoded whenever there is one.
inline bool MayUse(const ZydisDecodedInstruction& instruction, std::span<const uint32_t> offsets) {
    const auto& disp = instruction.raw.disp;
    if (!disp.size) return false;
    if (instruction.encoding == ZYDIS_INSTRUCTION_ENCODING_EVEX || instruction.encoding == ZYDIS_INSTRUCTION_ENCODING_MVEX)
        return true;
    return disp.value > 0 && disp.value <= UINT32_MAX &&
        std::ranges::find(offsets, static_cast<uint32_t>(disp.value)) != offsets.end();
}

// Which base registers count: `only` names them (64-bit names: "rcx"); empty
// takes every base but the stack frame's (rsp, rbp), whose slots are locals,
// not struct members. RIP-relative operands are globals: never taken.
struct Bases {
    std::vector<std::string> only;
};
inline bool BaseTaken(ZydisRegister base, const Bases& bases) {
    if (base == ZYDIS_REGISTER_NONE || base == ZYDIS_REGISTER_RIP || base == ZYDIS_REGISTER_EIP) return false;
    const ZydisRegister wide = ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, base);
    if (bases.only.empty()) return wide != ZYDIS_REGISTER_RSP && wide != ZYDIS_REGISTER_RBP;
    const char* name = ZydisRegisterGetString(wide);
    return name && std::ranges::find(bases.only, std::string_view(name)) != bases.only.end();
}

// Uses in straight code [address, address + code.size()). Undecodable bytes
// (data between chunks) are stepped over one at a time. Only bases `bases`
// takes count (default: no stack slots).
inline std::vector<Use> InCode(std::span<const uint8_t> code, uint64_t address, std::span<const uint32_t> offsets,
                               const Bases& bases = {}) {
    static const dis::detail::Decoder decoder; // Console only: thread-safe statics are fine here.
    std::vector<Use> uses;
    ZydisDecoderContext context;
    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
    for (size_t at = 0; at < code.size();) {
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder.decoder, &context, code.data() + at, code.size() - at,
                &instruction))) {
            ++at;
            continue;
        }
        if (!MayUse(instruction, offsets) || !ZYAN_SUCCESS(ZydisDecoderDecodeOperands(&decoder.decoder, &context,
                &instruction, operands, instruction.operand_count_visible))) {
            at += instruction.length;
            continue;
        }
        for (uint8_t i = 0; i < instruction.operand_count_visible; ++i) {
            const auto& operand = operands[i];
            if (operand.type != ZYDIS_OPERAND_TYPE_MEMORY || !BaseTaken(operand.mem.base, bases) ||
                !operand.mem.disp.has_displacement || operand.mem.disp.value <= 0 || operand.mem.disp.value > UINT32_MAX) continue;
            const auto disp = static_cast<uint32_t>(operand.mem.disp.value);
            if (std::ranges::find(offsets, disp) == offsets.end()) continue;
            Use use{address + at, disp, (operand.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) != 0, {}};
            char text[256]; // The size Zydis examples use: long AVX text fits.
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
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder.lengths, nullptr, code.data() + at, code.size() - at,
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

// Whether the last end of straight-line code before `at` lies within
// `within` bytes, decoding `code` (at `address`) from the piece's `start`
// (itself an end): a return, an unconditional jump or padding (nop, int3).
// A function starts right after one, or behind a few junk bytes (a tail jump
// can share its piece with the next function); a call site lies far past one.
inline bool NearEnd(std::span<const uint8_t> code, uint64_t address, uint64_t start, uint64_t at, uint64_t within) {
    static const dis::detail::Decoder decoder; // Console only: thread-safe statics are fine here.
    if (start < address || at < start || at > address + code.size()) return false;
    ZydisDecodedInstruction instruction;
    size_t pos = static_cast<size_t>(start - address), end = pos;
    const size_t target = static_cast<size_t>(at - address);
    while (pos < target) {
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder.lengths, nullptr, code.data() + pos, code.size() - pos,
                &instruction))) {
            ++pos; // Junk: step over it a byte at a time.
            continue;
        }
        pos += instruction.length;
        const auto m = instruction.mnemonic;
        if (pos <= target && (m == ZYDIS_MNEMONIC_RET || m == ZYDIS_MNEMONIC_JMP || m == ZYDIS_MNEMONIC_NOP ||
                              m == ZYDIS_MNEMONIC_INT3)) end = pos;
    }
    return target - end <= within;
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

// disp-scan: what one instruction form must be. `access` is write (stores
// and read-modify-writes), read (loads and read-modify-writes) or any (also
// lea, the field's address taken); `width` the memory operand's bytes (0:
// any); `imm` an immediate operand's value (compared in the operand's width).
struct Form {
    uint32_t disp = 0;
    std::string access = "write";
    uint32_t width = 0;
    std::optional<uint64_t> imm;
    Bases bases;
};
struct Hit {
    uint64_t address = 0;
    std::string access;     // write, read, both (read-modify-write) or address (lea).
    uint32_t width = 0;     // The memory operand's bytes; 0 for lea.
    std::string text;
};
constexpr size_t MaxInstruction = 15;

// One linear sweep over `code` (at `address`, an instruction boundary): the
// instructions of `form`, up to `most` hits in all. Returns where the sweep
// stopped, an instruction boundary: the end of the code when `last`, else
// the first instruction that might run past it (the next chunk starts there),
// or right after the hit that filled `hits`. Code read straight through can
// meet data (jump tables): a hit there is possible, so check one with disasm.
inline size_t Sweep(std::span<const uint8_t> code, uint64_t address, const Form& form, bool last, std::vector<Hit>& hits,
                    size_t most) {
    static const dis::detail::Decoder decoder; // Console only: thread-safe statics are fine here.
    ZydisDecoderContext context;
    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
    const uint32_t offsets[] = {form.disp};
    size_t at = 0;
    while (at < code.size()) {
        if (!last && code.size() - at < MaxInstruction) break;
        if (code[at] == 0xcc) { ++at; continue; } // Padding.
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder.decoder, &context, code.data() + at, code.size() - at,
                &instruction))) {
            ++at;
            continue;
        }
        const size_t length = instruction.length;
        if (!MayUse(instruction, offsets) || !ZYAN_SUCCESS(ZydisDecoderDecodeOperands(&decoder.decoder, &context,
                &instruction, operands, instruction.operand_count_visible))) {
            at += length;
            continue;
        }
        std::optional<Hit> hit;
        bool immediate = !form.imm;
        for (uint8_t i = 0; i < instruction.operand_count_visible; ++i) {
            const auto& operand = operands[i];
            if (operand.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && form.imm) {
                const uint64_t mask = operand.size >= 64 || !operand.size ? ~uint64_t{0} : (uint64_t{1} << operand.size) - 1;
                if ((operand.imm.value.u & mask) == (*form.imm & mask)) immediate = true;
                continue;
            }
            if (operand.type != ZYDIS_OPERAND_TYPE_MEMORY || !BaseTaken(operand.mem.base, form.bases) ||
                !operand.mem.disp.has_displacement || operand.mem.disp.value != static_cast<int64_t>(form.disp)) continue;
            const bool agen = operand.mem.type == ZYDIS_MEMOP_TYPE_AGEN;
            const bool writes = (operand.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) != 0;
            const bool reads = (operand.actions & ZYDIS_OPERAND_ACTION_MASK_READ) != 0;
            const std::string access = agen ? "address" : writes && reads ? "both" : writes ? "write" : "read";
            if (form.access == "write" && !writes) continue;
            if (form.access == "read" && (!reads || agen)) continue;
            const uint32_t width = agen ? 0 : operand.size / 8;
            if (form.width && width != form.width) continue;
            hit = Hit{address + at, access, width, {}};
        }
        if (hit && immediate) {
            char text[256]; // The size Zydis examples use: long AVX text fits.
            if (ZYAN_SUCCESS(ZydisFormatterFormatInstruction(&decoder.formatter, &instruction, operands,
                    instruction.operand_count_visible, text, sizeof(text), address + at, nullptr))) hit->text = text;
            hits.push_back(std::move(*hit));
            if (hits.size() >= most) return at + length;
        }
        at += length;
    }
    return std::min(at, code.size());
}
}
