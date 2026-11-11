// reagentlive CLI: a thin x64 listing of live bytes (`disasm`) and the exact
// address fields of decoded instructions (`sig` feedback). Console only: the
// decoder (Zydis) never links into an agent. Pure: reads come in as a
// callback, so tests feed fixed bytes.
#pragma once
#include <Zydis/Zydis.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/core/client.h"
#include "reagentlive/frontends/cli/code/signature.h"

namespace reagentlive::cli::dis {
using ReadFn = sig::ReadFn;
constexpr size_t MaxInstructions = 64, MaxBytes = 512, DefaultCount = 24;
constexpr size_t MaxFollow = 3;     // Unconditional direct jumps stepped through per listing.
constexpr size_t MinString = 4, MaxString = 96;
constexpr size_t StringWindow = (MaxString + 1) * 2; // UTF-16 text and its terminator.

// Where an instruction's bytes encode a value: `relative` (a call, jump or
// branch target from the instruction's end), `rip` (a [rip + disp]
// displacement), `displacement` (any other memory displacement) or
// `immediate`.
struct Field {
    size_t offset = 0, size = 0;
    std::string_view kind;
    bool operator==(const Field&) const = default;
};
struct Instruction {
    uint64_t address = 0;
    std::vector<uint8_t> bytes;
    std::string text;
    std::optional<uint64_t> target;   // A relative branch's target or a RIP-relative operand's address.
    bool ripTarget = false;           // target comes from a [rip + disp] operand.
    std::string_view branch;          // Relative target: "call", "jump" or "branch"; empty otherwise.
    std::vector<Field> fields;
    std::optional<uint64_t> absolute; // A 64-bit immediate (mov r64, imm64).
    bool ends = false;                // Return or unconditional jump: straight-line code stops.
    bool directJump = false;          // Unconditional jump with a relative target (--follow steps through).
    std::optional<std::string> string; // Text at a RIP-relative target, cut at MaxString.
    bool wide = false;                // string was UTF-16.
};

namespace detail {
struct Decoder {
    ZydisDecoder decoder;
    ZydisFormatter formatter;
    Decoder() {
        ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
        ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL);
    }
};
}

// Decodes one instruction at the start of `bytes`. The error is
// "no more data" when the bytes end inside an instruction, else "undecodable".
enum class DecodeError { NoMoreData, Undecodable };
inline std::expected<Instruction, DecodeError> Decode(std::span<const uint8_t> bytes, uint64_t address) {
    static const detail::Decoder decoder; // Console only: thread-safe statics are fine here.
    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
    const ZyanStatus status = ZydisDecoderDecodeFull(&decoder.decoder, bytes.data(), bytes.size(), &instruction, operands);
    if (!ZYAN_SUCCESS(status))
        return std::unexpected(status == ZYDIS_STATUS_NO_MORE_DATA ? DecodeError::NoMoreData : DecodeError::Undecodable);
    Instruction out;
    out.address = address;
    out.bytes.assign(bytes.begin(), bytes.begin() + instruction.length);
    char text[256];
    if (ZYAN_SUCCESS(ZydisFormatterFormatInstruction(&decoder.formatter, &instruction, operands,
            instruction.operand_count_visible, text, sizeof(text), address, nullptr))) out.text = text;
    const auto category = instruction.meta.category;
    const bool relative = instruction.attributes & ZYDIS_ATTRIB_IS_RELATIVE;
    for (uint8_t i = 0; i < instruction.operand_count_visible; ++i) {
        const auto& operand = operands[i];
        const bool rip = operand.type == ZYDIS_OPERAND_TYPE_MEMORY && operand.mem.base == ZYDIS_REGISTER_RIP;
        const bool branch = operand.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && operand.imm.is_relative;
        if (!rip && !branch) continue;
        ZyanU64 target = 0;
        if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&instruction, &operand, address, &target))) {
            out.target = target;
            out.ripTarget = rip;
        }
        break;
    }
    if (instruction.raw.disp.size) {
        const bool rip = std::ranges::any_of(std::span(operands, instruction.operand_count_visible), [](const auto& o) {
            return o.type == ZYDIS_OPERAND_TYPE_MEMORY && o.mem.base == ZYDIS_REGISTER_RIP;
        });
        out.fields.push_back({instruction.raw.disp.offset, instruction.raw.disp.size / 8u, rip ? "rip" : "displacement"});
    }
    for (const auto& imm : instruction.raw.imm) {
        if (!imm.size) continue;
        out.fields.push_back({imm.offset, imm.size / 8u, imm.is_relative ? "relative" : "immediate"});
        if (!imm.is_relative && imm.size == 64) out.absolute = imm.value.u;
    }
    if (relative && out.target && !out.ripTarget) {
        out.branch = category == ZYDIS_CATEGORY_CALL ? "call" : category == ZYDIS_CATEGORY_UNCOND_BR ? "jump" : "branch";
    }
    out.ends = category == ZYDIS_CATEGORY_RET || category == ZYDIS_CATEGORY_UNCOND_BR;
    out.directJump = category == ZYDIS_CATEGORY_UNCOND_BR && relative && out.target && !out.ripTarget;
    return out;
}

// Where an instruction's bytes hold an address that moves with every build,
// in the terms of sig's byte heuristic: a 4-byte relative target (call,
// jump, branch), a [rip + disp32] displacement (rip) or a 64-bit immediate
// (absolute), each with what it points at. The caller keeps the ones inside
// a loaded module.
inline std::vector<sig::AddressField> AddressFields(const Instruction& instruction, size_t offset) {
    std::vector<sig::AddressField> fields;
    for (const auto& field : instruction.fields) {
        if (field.kind == "relative" && field.size == 4 && instruction.target)
            fields.push_back({offset + field.offset, 4, instruction.branch == "call" ? "call" :
                instruction.branch == "jump" ? "jump" : "branch", *instruction.target});
        else if (field.kind == "rip" && field.size == 4 && instruction.target)
            fields.push_back({offset + field.offset, 4, "rip", *instruction.target});
        else if (field.kind == "immediate" && field.size == 8 && instruction.absolute)
            fields.push_back({offset + field.offset, 8, "absolute", *instruction.absolute});
    }
    return fields;
}

// Decodes `bytes` (starting at an instruction boundary at `address`) and
// returns the address fields of every whole instruction whose field bytes
// are all fixed in `pattern`, pointing inside a loaded module. Where decoding
// stops (bytes that do not decode, or the end of the bytes inside an
// instruction), the rest of the pattern falls back to sig's byte heuristic.
// `decoded` reports how many pattern bytes the decoder covered.
inline std::vector<sig::AddressField> ExactFields(const sig::Pattern& pattern, std::span<const uint8_t> bytes,
                                                  uint64_t address, std::span<const Module> modules,
                                                  size_t* decoded = nullptr) {
    std::vector<sig::AddressField> fields;
    size_t at = 0;
    while (at < pattern.Size() && at < bytes.size()) {
        auto instruction = Decode(bytes.subspan(at), address + at);
        if (!instruction) break;
        for (const auto& field : AddressFields(*instruction, at)) {
            bool fixed = field.offset + field.size <= pattern.Size();
            for (size_t i = field.offset; fixed && i < field.offset + field.size; ++i) fixed = pattern.fixed[i];
            if (fixed && sig::detail::ModuleAt(field.target, modules)) fields.push_back(field);
        }
        at += instruction->bytes.size();
    }
    if (decoded) *decoded = std::min(at, pattern.Size());
    if (at < pattern.Size()) {
        sig::Pattern rest{std::vector<uint8_t>(pattern.bytes.begin() + at, pattern.bytes.end()),
            std::vector<bool>(pattern.fixed.begin() + at, pattern.fixed.end())};
        for (auto field : sig::AddressFields(rest, address + at, modules)) {
            field.offset += at;
            fields.push_back(field);
        }
    }
    return fields;
}

// Where a listing stopped early, and why.
struct End {
    uint64_t address = 0;
    size_t offset = 0;          // Bytes listed before it.
    std::string reason;
};
struct Hop { uint64_t from, to; };
// The end of straight-line code for an address (its function entry's end),
// or nullopt for no bound; only with --function.
using BoundFn = std::function<std::optional<uint64_t>(uint64_t address)>;
struct Options {
    size_t count = DefaultCount;  // 1..MaxInstructions.
    bool follow = false;
    BoundFn bound;                // Null: no bound.
};
struct Listing {
    std::vector<Instruction> rows;
    std::vector<Hop> followed;
    std::optional<End> end;       // Undecodable, unreadable or cut by a limit inside an instruction.
    std::string stop;             // "count", "bytes", "function end", or the end's reason.
};

// Lists up to `count` instructions and MaxBytes bytes from `start`, an
// instruction boundary the caller gives. With `follow`, an unconditional
// direct jump continues the listing at its target, at most MaxFollow times,
// inside the same limits.
inline std::expected<Listing, std::string> List(const ReadFn& read, uint64_t start, const Options& options) {
    Listing listing;
    const size_t count = std::clamp<size_t>(options.count, 1, MaxInstructions);
    size_t used = 0;
    uint64_t cursor = start;
    std::optional<uint64_t> until = options.bound ? options.bound(start) : std::nullopt;
    std::vector<uint8_t> buffer;
    uint64_t bufferStart = 0;
    bool pageCut = false;   // The buffer ends at a page boundary because the rest was unreadable.
    // Reads from the cursor up to the budget and the bound; on failure, up to
    // the page end. False when nothing there is readable.
    const auto fill = [&]() -> std::expected<bool, std::string> {
        uint64_t want = MaxBytes - used;
        if (until) want = std::min<uint64_t>(want, *until > cursor ? *until - cursor : 0);
        if (!want) return true;
        const uint64_t pageRest = 0x1000 - (cursor & 0xfff);
        for (const uint64_t length : {want, std::min(want, pageRest)}) {
            const ReadSpan span{cursor, length};
            auto items = read(std::span(&span, 1));
            if (!items) return std::unexpected(items.error());
            if (!items->empty() && items->front()) {
                buffer = std::move(*items->front());
                bufferStart = cursor;
                pageCut = length < want;
                return true;
            }
            if (length == pageRest) break;
        }
        return false;
    };
    const auto stopAt = [&](std::string reason) {
        listing.end = End{cursor, used, reason};
        listing.stop = std::move(reason);
    };
    if (auto filled = fill(); !filled) return std::unexpected(filled.error());
    else if (!*filled) { stopAt("unreadable"); return listing; }
    while (true) {
        if (listing.rows.size() == count) { listing.stop = "count"; break; }
        if (until && cursor >= *until) { listing.stop = "function end"; break; }
        if (used >= MaxBytes) { listing.stop = "bytes"; break; }
        const auto available = std::span(buffer).subspan(static_cast<size_t>(cursor - bufferStart));
        auto instruction = Decode(available, cursor);
        if (!instruction && instruction.error() == DecodeError::NoMoreData && pageCut) {
            // The readable part ended mid-instruction: try past the page.
            const uint64_t had = bufferStart + buffer.size();
            if (auto filled = fill(); !filled) return std::unexpected(filled.error());
            else if (!*filled || bufferStart + buffer.size() <= had) { stopAt("instruction runs into unreadable memory"); break; }
            continue;
        }
        if (!instruction) {
            if (instruction.error() == DecodeError::Undecodable) stopAt("undecodable");
            else if (until && cursor + available.size() >= *until) stopAt("instruction runs past the function end");
            else stopAt("instruction runs past the byte limit");
            break;
        }
        const size_t length = instruction->bytes.size();
        used += length;
        const bool hop = options.follow && instruction->directJump && listing.followed.size() < MaxFollow;
        const uint64_t from = cursor;
        cursor += length;
        listing.rows.push_back(std::move(*instruction));
        if (hop) {
            const uint64_t to = *listing.rows.back().target;
            listing.followed.push_back({from, to});
            cursor = to;
            until = options.bound ? options.bound(to) : std::nullopt;
            if (listing.rows.size() == count || used >= MaxBytes) continue;
            if (auto filled = fill(); !filled) return std::unexpected(filled.error());
            else if (!*filled) { stopAt("unreadable"); break; }
            continue;
        }
        if (cursor - bufferStart >= buffer.size() && pageCut) {
            if (auto filled = fill(); !filled) return std::unexpected(filled.error());
            else if (!*filled) { stopAt("unreadable"); break; }
        }
    }
    return listing;
}

// Text at `bytes`: at least MinString printable characters (ASCII, or
// UTF-16 with the high bytes zero) and a terminator, cut at MaxString.
struct Text { std::string text; bool wide = false; };
inline std::optional<Text> StringAt(std::span<const uint8_t> bytes) {
    const auto printable = [](uint8_t c) { return (c >= 0x20 && c < 0x7f) || c == '\t' || c == '\n' || c == '\r'; };
    if (bytes.size() >= 2 && printable(bytes[0]) && bytes[1] == 0) {
        std::string text;
        for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
            if (!bytes[i] && !bytes[i + 1]) {
                if (text.size() < MinString) return std::nullopt;
                if (text.size() > MaxString) text.resize(MaxString);
                return Text{std::move(text), true};
            }
            if (bytes[i + 1] || !printable(bytes[i])) return std::nullopt;
            text += static_cast<char>(bytes[i]);
        }
        return std::nullopt;
    }
    std::string text;
    for (const uint8_t c : bytes) {
        if (!c) {
            if (text.size() < MinString) return std::nullopt;
            if (text.size() > MaxString) text.resize(MaxString);
            return Text{std::move(text), false};
        }
        if (!printable(c)) return std::nullopt;
        text += static_cast<char>(c);
    }
    return std::nullopt;
}

// The string hint of every RIP-relative target inside a loaded module, read
// MaxBatchReads per request.
inline std::expected<void, std::string> AddStrings(const ReadFn& read, std::vector<Instruction>& rows,
                                                   std::span<const Module> modules) {
    std::vector<ReadSpan> spans;
    std::vector<size_t> owners;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        if (!row.target || !row.ripTarget) continue;
        const auto* module = sig::detail::ModuleAt(*row.target, modules);
        if (!module) continue;
        const uint64_t room = module->base + module->size - *row.target;
        spans.push_back({*row.target, std::min<uint64_t>(StringWindow, room)});
        owners.push_back(i);
    }
    for (size_t first = 0; first < spans.size(); first += MaxBatchReads) {
        const size_t n = std::min(MaxBatchReads, spans.size() - first);
        auto items = read(std::span(spans).subspan(first, n));
        if (!items) return std::unexpected(items.error());
        for (size_t i = 0; i < n && i < items->size(); ++i) {
            if (!(*items)[i]) continue;
            if (auto text = StringAt(*(*items)[i])) {
                rows[owners[first + i]].string = std::move(text->text);
                rows[owners[first + i]].wide = text->wide;
            }
        }
    }
    return {};
}
}
