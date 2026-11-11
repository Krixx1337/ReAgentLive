// reagentlive CLI: a parsed console command and the limits the parser applies.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/core/client.h"
#include "reagentlive/core/protocol.h"
#include "reagentlive/frontends/cli/syntax/pattern.h"
#include "reagentlive/ue/protocol_ue.h"

namespace reagentlive::cli {
enum class Kind { Remote, TypedCall, Release, Dump, Snap, Record, Each, Base, Help, Status, Quit, Pin, Watch, Batch, Get,
    Trial, Script, Refs, Peek, Entries, ExportLabels, Sig, Disasm, Xrefs, Vtable, Region, Touches, Label };
using ue::Ref;
struct Command {
    Kind kind = Kind::Remote;
    Request request{};
    std::string file;
    std::vector<std::string> fields;
    std::vector<ReadSpan> reads;
    // A class or struct named instead of an object ref (fields, dump, func,
    // funcs, params; each's class): the console resolves it before the request.
    std::string className;
    std::string target;         // each: field or [Owner:]Function.
    uint32_t seconds = 0;       // each: repeat for this long, printing changes.
    uint32_t limit = 0;         // census: rows to print, 0 = all.
    uint32_t from = 0;          // census: the first row printed (a later page of the stored census).
    std::string view;           // read --as: element type for decoded values.
    // pin and watch: per target (watch: per range in `reads`), the object an
    // address was given relative to ($N+0x10), 0 for raw addresses. The
    // console guards those on the object's identity.
    std::vector<uint64_t> guardObjects;
    uint32_t milliseconds = 0;  // pin, watch: duration.
    // watch: `target` is "start", "read", "stop" or empty (a blocking
    // watch); the sample interval (0: every tick) and a host root to follow.
    uint32_t interval = 0;
    std::string root;
    std::string then;           // watch --then: a call line run right after the start.
    // dump filters: rows whose name contains `match` (case-insensitive), with
    // non-zero bytes, or changed against the snapshot (`file`, else the
    // session's baseline). census: `match` filters class names, `changed`
    // compares with the census baseline. batch: `fields` holds the command
    // lines. get: `fields` holds the paths.
    std::string match;
    bool nonzero = false, changed = false;
    // write --trial: the owning object (guarded on its identity) and an
    // optional native object base inside it whose vtable is guarded too.
    Ref owner{};
    uint64_t object = 0;
    // sig: the mode; --check's pattern, where it should land (with
    // --resolve: its target) and how far past that the match lies; `target`
    // names the module (empty: the exe). --file: `file`. --bytes: request
    // address and length.
    enum class SigMode { Check, File, Bytes } sigMode = SigMode::Check;
    sig::Pattern pattern;
    std::optional<uint64_t> expect;
    uint64_t offset = 0;
    std::optional<sig::Resolve> resolve;
    // disasm: start at the containing function's start and stop at its end;
    // step through unconditional direct jumps. xrefs: list unknown hits too.
    bool function = false, follow = false, all = false;
    // read, peek, vtable, disasm: offsets of a pointer chain after the
    // address (<addr>->0x10->0x8); the console resolves it before running.
    std::vector<uint64_t> chain;
    // vtable: `offset` is where the table pointer sits in the object;
    // `table`: the address is the table itself; `bodies`: resolve jump stubs.
    bool table = false, bodies = false;
    // touches: struct offsets to find and the bytes scanned each side of the address.
    std::vector<uint32_t> offsets;
    uint64_t range = 0;
};
constexpr uint32_t MaxEachSeconds = 60;
constexpr size_t MaxBatchCommands = 64, MaxGetPaths = 16, MaxGetDepth = 8;
constexpr uint32_t DefaultCensusLimit = 100;
constexpr uint64_t MaxPeek = 64; // Qwords: one 512-byte read.
constexpr uint64_t MaxDisasm = 64, DefaultDisasm = 24;
namespace touch {
constexpr size_t MaxOffsets = 8;
constexpr uint64_t DefaultRange = 0x80000, MaxRange = 0x400000; // Bytes each side of the address.
}
// export-labels --kinds: which rows to derive. Globals in the file are always
// kept (only peek --label makes them), so "global" is accepted and changes nothing.
namespace label_kind {
constexpr uint32_t Exec = 1, Vtable = 2, Interface = 4, Global = 8, All = 15;
}
// "exec,vtable,interface,global" (any subset) to bits; 0 for an unknown or empty kind.
inline uint32_t LabelKinds(std::string_view list) {
    uint32_t kinds = 0;
    while (!list.empty()) {
        const size_t comma = list.find(',');
        const auto kind = list.substr(0, comma);
        if (comma != std::string_view::npos && comma + 1 == list.size()) return 0; // Trailing comma.
        list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
        if (kind == "exec") kinds |= label_kind::Exec;
        else if (kind == "vtable") kinds |= label_kind::Vtable;
        else if (kind == "interface") kinds |= label_kind::Interface;
        else if (kind == "global") kinds |= label_kind::Global;
        else return 0;
    }
    return kinds;
}
using Parsed = std::expected<Command, std::string>;

// A full ref as the parser accepts it back: 0x<address>#<index>:<serial>.
inline std::string RefText(const Ref& ref) {
    char text[48];
    snprintf(text, sizeof(text), "0x%llx#%d:%d", static_cast<unsigned long long>(ref.address), ref.index, ref.serial);
    return text;
}

// read --as cstr|wstr: text, not elements.
inline bool StringView(std::string_view type) { return type == "cstr" || type == "wstr"; }
constexpr uint64_t DefaultStringRead = 256;

// Element size of a read --as type, 0 if unknown.
inline size_t ElementSize(std::string_view type) {
    if (type == "i8" || type == "u8") return 1;
    if (type == "i16" || type == "u16") return 2;
    if (type == "i32" || type == "u32" || type == "f32") return 4;
    if (type == "i64" || type == "u64" || type == "f64" || type == "ptr") return 8;
    return 0;
}

}
