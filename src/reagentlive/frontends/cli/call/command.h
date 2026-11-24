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
#include "reagentlive/frontends/cli/call/pattern.h"

namespace reagentlive::cli {
enum class Kind { Remote, TypedCall, Release, Dump, Snap, Each, Help, Status, Quit, Pin, Watch, Batch, Get,
    Trial, TimedWrites, Script, NameRefs, Peek, Entries, LabelsExport, Sig, Disasm, Xrefs, Vtable, Touches, DispScan, LabelsSign, LabelsPort, LabelsCheck, MemberName, Usage, Layout,
    MemSnap, MemDiff, LineTrace, Calls, Rows, Enum, GameThread, AssetsExport, Run };
struct Verb;
struct Command {
    Kind kind = Kind::Remote;
    const Verb* verb = nullptr; // The row this line parsed with (verb.h).
    Request request{};
    std::string file;
    std::vector<std::string> fields;
    std::vector<ReadSpan> reads;
    std::vector<uint32_t> watchTypes; // watch-start: per range in `reads`, its element type (WatchTypeCode), 0 = raw.
    // watch-start: ranges given as {"object", "field"}, resolved through the pack
    // before the start; `range` is the index into `reads` they fill.
    struct WatchField { size_t range; Ref object; std::string path; };
    std::vector<WatchField> watchFields;
    // A class or struct named instead of an object ref (get types, func,
    // funcs, params; each's class): the console resolves it before the request.
    std::string className;
    std::string target;         // A name the verb acts on (a function, a member, a watch action).
    uint32_t seconds = 0;       // each, call-each: repeat for this long, printing changes.
    bool eachCall = false;      // call-each: one function called on every instance (each's call form).
    uint32_t limit = 0;         // census, call: rows to print, 0 = all. find: rows at most, 0 = one agent page.
    uint32_t from = 0;          // census, call: the first row printed (census: a later page of the stored census).
    std::string view;           // read type: element type or layout for decoded values.
    // write hold and watch-start: per target (watch-start: per range in `reads`), the object an
    // address was given relative to ({"base": <ref>, "offset": 16}), 0 for raw addresses. The
    // console guards those on the object's identity.
    std::vector<uint64_t> guardObjects;
    uint32_t milliseconds = 0;  // write undo and hold, watch-start: duration.
    // watch-start, watch-read, watch-stop: `target` is "start", "read" or
    // "stop"; the sample interval (0: every tick) and a host root to follow;
    // watch-start file: `file`.
    uint32_t interval = 0;
    std::string root;
    // get filters (no paths): rows whose name contains `match` (case-insensitive), with
    // non-zero bytes, or changed against the snapshot (`file`, else the
    // session's baseline). census: `match` filters class names; census diff
    // sets `changed`, comparing with the census baseline. batch: `fields` holds the command
    // lines. get: `fields` holds the paths.
    std::string match;
    // each where Field~text: the field path whose value must contain `match`.
    std::string whereField;
    bool nonzero = false, changed = false, own = false; // get filters; own: only the fields the class itself declares.
    uint32_t elements = 0;      // elements: per container in get, each and dump; 0: the default.
    // outer on find, each and census: an object (its full ref) or an outer's name text.
    std::string outer, outerText;
    Ref outerRef{};
    // write undo: the owning object (guarded on its identity) and an
    // optional native object base inside it whose vtable is guarded too.
    Ref owner{};
    uint64_t object = 0;
    // sig-check, sig-bytes: the mode; sig-check's pattern, where it should
    // land (with resolve: its target) and how far past that the match lies;
    // `target` names the module (empty: the exe). sig-check file: `file`.
    // sig-bytes: request address and length.
    // help: `target` is the verb asked about (empty: all), `all` is json, `hashOnly` is hash.
    // usage: `all` is report all. watch-read, watch-stop: `all` is all (every sample).
    bool hashOnly = false;
    bool listSessions = false;  // usage report sessions; session <id> is `target`.
    bool advice = false;        // usage advice: verbs often digested or unfiltered, with a proposed change.
    std::vector<std::string> parts; // usage only: the report's parts kept (empty: all).
    std::string baseline;       // usage baseline: a saved report to compare with. usage save: `file`.
    enum class SigMode { Check, File, Bytes } sigMode = SigMode::Check;
    bool capped = false;        // sig-bytes: the length asked passed sig::MaxPattern and was cut to it.
    sig::Pattern pattern;
    std::optional<uint64_t> expect;
    uint64_t offset = 0;
    std::optional<sig::Resolve> resolve;
    // disasm: start at the containing function's start and stop at its end
    // (`resume`: start at the address, a cut listing's next; `file`: save);
    // step through unconditional direct jumps. xrefs: list unknown hits too.
    bool function = false, follow = false, all = false, resume = false;
    // disasm save: also save the functions it calls, breadth-first to this
    // depth (callees; 0: none), and name addresses from a labels file
    // (labels, labels-export' format).
    uint32_t calleeDepth = 0;
    std::string labels;
    // peek label: append the qword's label (to `file`, else the console's labels file).
    bool label = false;
    // search text: 1 for ASCII, 2 for UTF-16 (wide); rows then carry the text around each hit.
    uint8_t textSearch = 0;
    // read, peek, vtable, disasm: offsets of a pointer chain after the
    // address (<addr>->0x10->0x8); the console resolves it before running.
    std::vector<uint64_t> chain;
    // vtable: `offset` is where the table pointer sits in the object;
    // `table`: the address is the table itself; `bodies`: resolve jump stubs;
    // `rtti`: also the run-time type information's class.
    bool table = false, bodies = false, rtti = false;
    // touches: struct offsets to find and the bytes scanned each side of the
    // address; `readsOnly` (reads) ranks by offsets read and drops copies.
    std::vector<uint32_t> offsets;
    uint64_t range = 0;
    bool readsOnly = false;
    // touches and disp-scan: the base registers taken (empty: all but rsp and
    // rbp). disp-scan: the access, operand bytes (0: any) and immediate asked
    // for; `offsets` holds its one disp, request.address its start (0: the
    // first code byte).
    std::vector<std::string> bases;
    std::string access;
    uint32_t width = 0;
    std::optional<uint64_t> imm;
    // call dry: resolve the function and build the block, call nothing.
    bool dry = false;
    // call and rows: the member paths kept in each row (fields a,b.c);
    // `from` and `limit` page the rows. rows: `match` filters row names;
    // `curveAt` (at) evaluates a curve table's rows at that time.
    std::vector<std::string> rowPaths;
    std::optional<float> curveAt;
    // layout-export closure: how many references away from the named types to export.
    std::optional<uint32_t> closure;
    // line-trace: its two ends (a world point in cm, or an object's location or a
    // socket on it), the trace type query, simple collision, and an actor the
    // line passes through (also the world it runs in; else an end's object).
    struct TracePoint { bool literal = false; double point[3]{}; Ref object{}; std::string socket; };
    TracePoint traceEnds[2];
    uint8_t traceChannel = 0;
    bool traceSimple = false;
    Ref traceIgnore{};
    // calls-start, calls-read, calls-stop: `target` is "start", "read" or
    // "stop"; the recording's slot (ue::CallsLatest: the last started) and
    // calls-read's cursor; calls-start's request carries the object, the
    // function, the milliseconds (args[0]) and the object filter (args[1]).
    uint64_t callsSlot = 0, callsAfter = 0;
    // script decode: the bytecode as statements; entry: from this offset
    // to the event's end (an uber-graph entry); to: statements before this
    // offset only (with entry: from entry to it, past returns).
    bool decode = false;
    std::optional<uint32_t> entry, to;
};
constexpr uint32_t MaxEachSeconds = 60;
// A class or struct name as the console accepts it (the Unreal pack's wire limit).
constexpr size_t MaxTypeName = 128;
// watch follow: an offset from the root object, inside the object.
constexpr uint64_t MaxRootOffset = 0x10000;
constexpr size_t MaxBatchCommands = 64, MaxGetPaths = 16, MaxGetDepth = 8;
constexpr uint32_t DefaultCensusLimit = 100;
constexpr uint32_t MaxExportClasses = 16, MaxExportObjects = 2000; // assets-export: per call.
constexpr uint32_t DefaultCallRows = 100;   // call: rows of a returned array per page.
constexpr uint32_t DefaultEachRows = 100;   // each: rows printed.
// mem-snap: bytes kept; mem-diff: rows shown and the tests it takes.
namespace memsnap {
constexpr uint64_t MaxLength = uint64_t{1} << 20;
constexpr size_t MaxRows = 256, DefaultRows = 64;
inline bool TestName(std::string_view name) {
    return name == "changed" || name == "unchanged" || name == "increased" || name == "decreased";
}
}
// layout-export: classes per call; layout-check: lines per file and mismatch rows shown.
constexpr size_t MaxLayoutClasses = 256, MaxLayoutLines = 50000, MaxLayoutRows = 200, MaxClosureDepth = 8;
constexpr uint64_t MaxPeek = 64; // Qwords: one 512-byte read.
constexpr size_t MaxSearchText = 96; // search text: characters shown each side of a hit.
// disasm: up to MaxDisasm instructions (DisasmBytes per instruction asked, at
// least dis::MaxBytes), MaxFunctionDisasm with function.
constexpr uint64_t MaxDisasm = 1024, DefaultDisasm = 24, MaxFunctionDisasm = 2048, DisasmBytes = 8;
// disasm save callees: how many levels of calls a save follows.
constexpr uint32_t MaxCalleeDepth = 3;
constexpr uint64_t MaxFindRows = 1000; // find limit: rows gathered over the agent's pages.
namespace touch {
constexpr size_t MaxOffsets = 8;
constexpr uint64_t DefaultRange = 0x80000, MaxRange = 0x400000; // Bytes each side of the address.
}
// labels-export kinds: which rows to derive. Globals in the file are always
// kept (only peek label makes them), so "global" is accepted and changes nothing.
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

// read type cstr|wstr: text, not elements.
inline bool StringView(std::string_view type) { return type == "cstr" || type == "wstr"; }
constexpr uint64_t DefaultStringRead = 256;

// Element size of a read type type, 0 if unknown.
inline size_t ElementSize(std::string_view type) {
    if (type == "i8" || type == "u8") return 1;
    if (type == "i16" || type == "u16") return 2;
    if (type == "i32" || type == "u32" || type == "f32") return 4;
    if (type == "i64" || type == "u64" || type == "f64" || type == "ptr") return 8;
    return 0;
}

// A typed watch range's element type, as the number the console keeps in the
// agent's WatchRange::note so a later run decodes the capture: 0 = raw bytes.
inline constexpr std::string_view WatchElementTypes[] = {"", "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64",
    "f32", "f64", "ptr"};
inline uint32_t WatchTypeCode(std::string_view type) {
    for (uint32_t i = 1; i < std::size(WatchElementTypes); ++i)
        if (WatchElementTypes[i] == type) return i;
    return 0;
}
inline std::string_view WatchTypeName(uint32_t code) {
    return code < std::size(WatchElementTypes) ? WatchElementTypes[code] : std::string_view{};
}

}
