// reagentlive CLI: the verb table's core rows (call/verb.h): what each core
// verb takes and returns. The checks of a JSON call (call/check.h), `help`,
// `help` with `json`, the generated docs/commands/results.md and the Python
// client's strict results all read these rows. The Unreal pack's rows are in
// packs/ue/cli/results/verbs_ue.h.
#pragma once
#include <algorithm>
#include <expected>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/code/code_json.h"
#include "reagentlive/frontends/cli/code/label_sign.h"
#include "reagentlive/frontends/cli/code/signature_json.h"
#include "reagentlive/frontends/cli/results/mem_json.h"
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/frontends/cli/results/usage_report.h"
#include "reagentlive/frontends/cli/call/build_internal.h"

namespace reagentlive::cli {
// The bounds the builders enforce, as the arg help below states them.
static_assert(MaxSearchJobHits == 507 && MaxRead == 4096 && MaxPeek == 64 && MaxWrite == 256 && MaxTrialBytes == 64 &&
    MaxPinBytes == 64 && MaxDisasm == 1024 && MaxFunctionDisasm == 2048 && MaxBatchCommands == 64 &&
    MaxWatchRanges == 8 && touch::MaxOffsets == 8 && touch::MaxRange == 0x400000);
inline const std::vector<Verb>& CoreVerbs() {
    using schema::Of;
    constexpr std::string_view at = "the address";
    constexpr std::string_view atChain = "the address, or a pointer chain to follow first";
    constexpr std::string_view module = "the module (default: the exe)";
    constexpr std::string_view labels = "the labels file (default: names file)";
    constexpr std::string_view registers = "rax|rbx|rcx|rdx|rsi|rdi|rbp|rsp|r8|r9|r10|r11|r12|r13|r14|r15";
    constexpr std::string_view bases = "only operands on these base registers (default: any but rsp and rbp)";
    constexpr std::string_view scanBases = "only operands on these base registers (default: any but rsp)";
    static const std::vector<Verb> verbs = {
        {"help", "core", ToolPack::Explore, "the verbs; with verb: its args, result, notes and example",
            {{"verb", "str", "the verb"},
             {"json", "bool", "the whole table for programs"},
             {"hash", "bool", "only the table's hash"}},
            Of<shape::Help>, build::Help},
        {"status", "core", ToolPack::Explore, "agent pid, exe, tick rate, host packs, table_hash",
            {}, Of<shape::Status>, build::Status,
            "table_hash changes when a verb's args or result change: a cached script or skill is stale."},
        {"quit", "core", ToolPack::Explore, "end the console", {}, Of<shape::Quit>, build::Quit},
        {"usage", "core", ToolPack::Explore, "this session's cost: calls, output and refusals per verb, largest first",
            {{"report", "sessions|all", "sessions: list the log's sessions; all: every session together"},
             {"session", "str", "a session id"},
             {"advice", "bool", "verbs often digested or unfiltered, with a proposed view.limit or hint"},
             {"baseline", "file", "a saved report: what got worse or better"},
             {"save", "file", "write this report to a file as a baseline"},
             {"only", "totals|rows|largest|never_called|session_rows|context|refusals|advice|compare",
              "keep only these parts (advice turns it on)", {}, false, true}},
            Of<shape::UsageReport>, build::Usage,
            "Reads the usage log beside the console; works with no target. out is characters; out_by_caller.tool is "
            "what reached an agent's context. Marks and compare rules: docs/commands/console.md#usage-log-and-usage. "
            "Past --context-limit (default 400000) every envelope carries context_warning; nothing stops."},
        {"batch", "core", ToolPack::Explore, "up to 64 calls in as few host ticks as possible; one envelope each",
            {{"calls", "calls", "calls, each {\"verb\", \"args\"} (up to 64)", {}, true}},
            Of<shape::Batch>, build::Batch,
            "rows: each call's envelope (tick, last_tick). Not allowed inside: help, status, usage, quit, batch, run."},
        {"run", "core", ToolPack::Explore, "a plan file's steps, each its own call: NDJSON lines, then a report",
            {{"plan", "file", "JSON list of {\"verb\", \"args\"?, \"expect\"?, \"repeat\"?: {\"every\", \"for\"}}", {}, true},
             {"out", "file", "write the lines here, not in the result"},
             {"background", "bool", "frontends only: run in its own console, answer with a run id (a console refuses)"}},
            Of<shape::Run>, build::Run,
            "A step of a pack outside --packs is a refused line, not a stop. Up to 256 steps; run and quit not "
            "allowed. Steps are not from one moment: use a batch step for that. repeat: up to 600 s, 1000 runs. "
            "Line and report shape: docs/commands/console.md#plans. Background runs: run_status, run_read, run_stop.",
            false, true},
        {"read", "core", ToolPack::Explore, "hex dump of bytes; type decodes values",
            {{"addr", "chain", atChain, {}, true}, {"len", "int", "how many bytes (1..4096)", "64"},
             {"type", "layout", "i8..u64|f32|f64|ptr|cstr|wstr, or a layout \"u8 phase, pad 3, f64[3] start\""}},
            Of<shape::Read>, build::Read,
            "cstr|wstr: text (default 256 bytes). region is the memory kind at the address."},
        {"peek", "core", ToolPack::Explore, "what 1..64 qwords hold: object, module, heap or value",
            {{"addr", "chain", atChain, {}, true}, {"count", "int", "how many qwords (1..64)", "8"},
             {"label", "bool", "append this qword as a confirmed global to a labels file"},
             {"file", "file", "with label: the labels file (default: names file)"},
             {"name", "str", "with label: the name (default FName_<hint>)"}},
            Of<shape::Peek>, build::Peek,
            "object needs a pack. region as read."},
        {"write", "core", ToolPack::Modify, "write up to 256 bytes, optionally timed (undo, hold); clear ends timed writes",
            {{"addr", "addr", "the address (required unless clear)", {}, false, false, "\"0x140001000\""},
             {"data", "bytes", "hex pairs (up to 256; 64 with undo or hold; required unless clear)",
              {}, false, false, "\"00 00 80 3f\""},
             {"expect", "bytes", "write only while memory holds these bytes (required with undo, restored after)"},
             {"undo", "obj", "restore after a while, guarded on the owner (needs expect)", {}, false, false, {},
              {{"after", "dur", "restore after this long (up to 60 s)", {}, true},
               {"owner", "ref", "the live actor or component holding the address (a pack names it)", {}, true},
               {"object", "addr", "a native object inside the owner whose vtable is guarded too"}}},
             {"hold", "dur", "rewrite the bytes every tick for this long (up to 60 s)"},
             {"clear", "str", "end timed writes: \"all\" or an id from timed-writes"}},
            Of<shape::Write>, build::Write,
            "A refused expect answers found = the bytes there. undo and hold answer timed. undo ends conflict (value "
            "changed) or abandoned (owner or vtable changed); without a pack's owner check it is refused. clear takes "
            "no other key.", true},
        {"timed-writes", "core", ToolPack::Observe, "timed writes (undo, hold) and their state",
            {}, Of<shape::Writes>, build::TimedWrites,
            "Rows: undo writes (restored, conflict or abandoned stay until cleared), then hold ones."},
        {"modules", "core", ToolPack::Explore, "loaded modules, exe first (also refreshes the cached list)",
            {}, Of<shape::Modules>, build::Modules},
        {"search", "core", ToolPack::Native, "addresses holding a value in resident memory, as a job the agent runs",
            {{"type", "hex|u32|i32|u64|ptr|f32|f64|text|wtext", "the value's type (wtext: text as UTF-16)", {}, true},
             {"value", "scalar", "a string for text or hex pairs, else a number (u64, ptr also \"0x...\")", {}, true, false,
              "\"00 00 80 3f\""},
             {"scope", "obj", "where to look (default: main module)", {}, false, false, {},
              {{"module", "str", "one module, in place of start and to"},
               {"start", "addr", "start, or a job's next_cursor"},
               {"to", "addr", "end"},
               {"writable", "bool", "writable memory only, every module and heap"},
               {"heap", "bool", "private memory only (heaps, stacks)"},
               {"paged", "bool", "also non-resident pages (slow)"}}},
             {"align", "int", "1, 2, 4, 8 or 16 (default: the type's size)"},
             {"wait", "int", "seconds to wait for the job (0..60)", "10"},
             {"max", "int", "hits at most (1..507); the job stops when full", "64"}},
            Of<shape::Search>, build::Search,
            "Still running after wait: state running and hits so far; search-job with its id for more or stop. A new "
            "search stops the last job. next_cursor resumes as scope.start."},
        {"search-job", "core", ToolPack::Native, "a search job's state and hits so far; stop ends it first",
            {{"job", "int", "the job a search returned", {}, true},
             {"stop", "bool", "end the job first; its next_cursor resumes it"}},
            Of<shape::Search>, build::SearchJob,
            "Only the last job answers. scanned: share of searchable bytes behind the cursor."},
        {"mem-snap", "core", ToolPack::Observe, "keep up to 1 MiB for mem-diff",
            {{"addr", "chain", atChain, {}, true}, {"len", "int", "bytes (up to 1048576)", {}, true}},
            Of<shape::MemSnap>, build::MemSnap,
            "A new snap replaces the last one. Refused if part of the range is unreadable."},
        {"mem-diff", "core", ToolPack::Observe, "elements of the last mem-snap that changed, stayed, rose or fell; keep narrows",
            {{"compare", "changed|unchanged|increased|decreased", "what an element must have done", {}, true},
             {"type", "str", "element type i8..u64, f32, f64 or ptr; one per size bytes", {}, true, false,
              "\"f32\""},
             {"keep", "bool", "keep only the matches as candidates and today's bytes as the new baseline"},
             View("limit", "64")},
            Of<shape::MemDiff>, build::MemDiff,
            "changed and unchanged compare bytes, increased and decreased numbers (NaN never passes). matched counts "
            "all; rows show view.limit (1..256). A snap from another agent instance is refused."},
        {"watch-start", "core", ToolPack::Observe, "a capture the agent keeps (up to 60 min); returns at once",
            {{"duration", "dur", "how long, in seconds", {}, true},
             {"ranges", "range", "up to 8: {addr, len}, {addr, type, count?} to decode, {object, field} for a reflected "
              "field; {offset, ...} instead of addr with follow", {}, true, true},
             {"every", "dur", "interval (default: each tick, then at most 30 s)"},
             {"follow", "str", "a host root: ranges are offsets from it"},
             {"file", "file", "keep every sample here (JSON lines)"}},
            Of<shape::WatchStart>, build::WatchStart,
            "status lists the roots. The capture outlives the console. Typed ranges add a summary (first, last, least, "
            "greatest, change/s). file persists in the cache; a later watch-start without it drops it. For a blocking "
            "capture: start, wait, stop."},
        {"watch-read", "core", ToolPack::Observe, "the capture so far: first sample, changes, last",
            {{"all", "bool", "every sample, not only the changes"}}, Of<shape::WatchCapture>, build::WatchRead,
            "Past 200 rows the middle is dropped (dropped). samples counts all; the summary covers every sample."},
        {"watch-stop", "core", ToolPack::Observe, "end the capture and return it: first sample, changes, last",
            {{"all", "bool", "every sample, not only the changes"}}, Of<shape::WatchCapture>, build::WatchStop,
            "As watch-read."},
        {"sig-check", "core", ToolPack::Native, "is a byte signature unique in a module and where it lands; file checks a file of them",
            {{"pattern", "pattern", "the signature: \"48 8b ?? 89\" or \"0x48, 0x8B, -1, 0x89\", up to 96 bytes", {}, false, false,
              "\"48 8b 05 ?? ?? ?? ?? 48 85 c0\""},
             {"file", "file", "a JSON-lines file, one signature per line, in place of pattern"},
             {"module", "str", module},
             {"expect", "addr", "where it should land"},
             {"offset", "int", "the match starts this far past expect"},
             {"resolve", "str", "a reference signature \"next,field[,size]\"; expect is then its target"}},
            Of<shape::SigCheckResult>, build::SigCheck,
            "Reports mismatches, diverges_at, warnings. file takes no other key: answers file, winner, a row per line."},
        {"sig-bytes", "core", ToolPack::Native, "live bytes in both pattern forms, address fields wildcarded (up to 96; longer is capped)",
            {{"addr", "addr", at, {}, true}, {"len", "int", "bytes", "64"}},
            Of<shape::SigBytes>, build::SigBytes},
        {"labels-sign", "core", ToolPack::Native, "sign each user and global name of this build so it survives a patch",
            {{"file", "file", labels}, {"module", "str", module},
             {"retry", "bool", "also rows with a signature or that failed before"}},
            Of<shape::LabelsSign>, build::LabelsSign,
            "Signs each name by a byte signature (a global by a reference signature) stored as the row's sig; "
            "unsignable rows get sig_failed and are skipped later (failed_before) unless retry. The file is rewritten "
            "in place; other builds' lines stay."},
        {"labels-port", "core", ToolPack::Native, "find other builds' signed names in the running build and add them to the file",
            {{"file", "file", labels}, {"module", "str", module}},
            Of<shape::LabelsPort>, build::LabelsPort,
            "Per name this build lacks, matches the newest signed row of another build; one match appends a row (sig, "
            "ported_from); none (missing) or several (ambiguous) are listed, never written. Old rows stay."},
        {"labels-check", "core", ToolPack::Native, "re-verify this build's signed names: does each sig still land",
            {{"file", "file", labels}, {"module", "str", module},
             {"status", "ok|moved|missing|ambiguous|invalid", "only rows with these statuses (counts still cover every row)",
              {}, false, true},
             View("limit")},
            Of<shape::LabelsCheck>, build::LabelsCheck,
            "Per signed row: ok, moved (found), missing, ambiguous (matches) or invalid. unsigned counts rows with no "
            "sig. status keeps only those statuses; view.limit the first rows (0: all). Read-only."},
        {"disasm", "core", ToolPack::Native, "instructions from live bytes (default 24), or a whole function",
            {{"addr", "chain", atChain, {}, true}, {"count", "int", "instructions (1..1024; with function: up to 2048)", "24"},
             {"function", "bool", "the whole containing function (exception table, else a walk from addr)"},
             {"resume", "bool", "with function: start at addr (a cut listing's next)"},
             {"save", "obj", "with function: write it and its read-only data to files for a static tool",
              {}, false, false, {},
              {{"file", "file", "the file to write", {}, true},
               {"callees", "int", "also the functions it calls, this many levels (1..3)"},
               {"labels", "file", "name from this labels file"}}},
             {"follow", "bool", "step through up to 3 unconditional direct jumps"}},
            Of<shape::Disasm>, build::Disasm,
            "Keep count small: a whole function belongs in save, not a reply. function stops at 2048 instructions or "
            "16 KB (next: continue with resume). save leaves rows out of the result. Without an exception-table "
            "entry, save refuses a walk from past padding unless resume."},
        {"xrefs", "core", ToolPack::Native, "what points at an address: rel32 fields and stored pointers, each with its function",
            {{"addr", "addr", at, {}, true},
             {"module", "str", "scan another loaded module"},
             {"all", "bool", "also list unknown hits (likely chance bytes)"}},
            Of<shape::Xrefs>, build::Xrefs,
            "A code row outside every exception-table entry carries unlisted, its function the piece holding it."},
        {"touches", "core", ToolPack::Native, "functions near an address whose [reg + disp] operands use the offsets (up to 8)",
            {{"addr", "addr", at, {}, true}, {"offsets", "int", "the struct offsets to look for (up to 8, each above 0)", {}, true, true},
             {"range", "int", "bytes each side of addr (1..0x400000)", "524288", false, false, "\"0x80000\""},
             {"reads", "bool", "rank by offsets read; drop rows writing every offset they read"},
             {"bases", registers, bases, {}, false, true}},
            Of<shape::Touches>, build::Touches,
            "Most offsets first, writes marked. Unlisted code is scanned in pieces (rows carry unlisted). Stack slots "
            "([rsp + d], [rbp + d]) are left out unless bases names their register. reads drops struct copies and "
            "constructors. The whole image for a few offsets: disp-scan."},
        {"disp-scan", "core", ToolPack::Native, "instructions in the main module with a [reg + disp] operand at these offsets, paged",
            {{"offsets", "int", "the displacements (the fields' offsets; up to 8, each above 0)", {}, true, true},
             {"access", "write|read|any", "write: stores and read-modify-writes; read: loads and read-modify-writes; any: also lea",
              "\"write\""},
             {"width", "int", "only operands of this many bytes (1, 2, 4, 8, 16, 32 or 64)"},
             {"imm", "int", "only instructions with this immediate (compared in its width)"},
             {"bases", registers, scanBases, {}, false, true},
             {"same_base", "bool", "only functions using every disp on one base register (2+ disps): functions lists them"},
             View("start limit", "128")},
            Of<shape::DispScan>, build::DispScan,
            "Each call scans up to 8 s and stops at view.limit hits (default 128, at most 4096). view.start resumes at "
            "next_cursor (absent at the end). A full sweep takes seconds through the agent's image window, else about "
            "1 MB/s. Sweeps over many pages belong in a run plan. Jump tables can decode as code: check a surprising "
            "hit with disasm. Near one function: touches."},
        {"vtable", "core", ToolPack::Native, "an object's virtual table: slots until the first entry that is not code",
            {{"addr", "chain", atChain, {}, true},
             {"offset", "int", "the table pointer at addr+offset (an interface part; 0..0x100000)"},
             {"slots", "int", "only these slots (each 0..511)", {}, false, true},
             {"parts", "int", "list the table pointers in the object's first bytes (8..0x10000)"},
             {"resolve", "bool", "where jump stubs lead"},
             {"rtti", "bool", "also the class and bases its run-time type information names (MSVC images)"},
             View("limit")},
            Of<shape::Vtable>, build::Vtable,
            "addr is the object, or the table itself when its first qword points into code. view.limit: 1..512 "
            "slots. rtti_error when the image has no locator."},
    };
    return verbs;
}

// The core's origin; a pack gives its own beside its rows, the host's is HostOrigin.
inline constexpr Origin CoreOrigin{"core", "Core"};
inline constexpr Origin HostOrigin{"host", "Host"};

// A console's verb table: the core's rows, the pack's, then the host's.
inline std::vector<Verb> TableRows(std::span<const Verb> packVerbs, std::span<const Verb> hostVerbs) {
    std::vector<Verb> verbs(CoreVerbs().begin(), CoreVerbs().end());
    verbs.insert(verbs.end(), packVerbs.begin(), packVerbs.end());
    verbs.insert(verbs.end(), hostVerbs.begin(), hostVerbs.end());
    return verbs;
}
inline std::vector<Origin> TableOrigins(std::span<const Origin> packOrigins) {
    std::vector<Origin> origins{CoreOrigin};
    origins.insert(origins.end(), packOrigins.begin(), packOrigins.end());
    origins.push_back(HostOrigin);
    return origins;
}

// What applies to every verb; `help` prints these under the rows.
inline const std::vector<std::string>& GeneralNotes() {
    static const std::vector<std::string> notes = {
        "Load the reagentlive skill before investigating: method, approvals, pitfalls.",
        "A call is one JSON object per line: {\"verb\": \"read\", \"args\": {\"addr\": \"0x1000\", \"len\": 16}}; "
        "\"meta\", \"timing\", \"big\" as true, \"max_chars\" a number. A line may be a list of calls.",
        "help with verb: args, result, notes, example; with json: the whole table. Unknown keys, missing required "
        "ones and wrong types are refused (kind schema) with a corrected example.",
        "An int is a number or \"0x...\" text. An addr is \"0x...\", a literal ref, {\"module\": \"x.dll\", \"rva\": N} "
        "or {\"base\": <addr>, \"offset\": N}.",
        "A chain is {\"base\": <addr>, \"chain\": [N, N]} (read, peek, mem-snap, vtable, disasm): each offset but "
        "the last reads a pointer; one alone is refused.",
        "A path is a list of field names: [\"Info\", \"Owner\"]. Keys that do not apply are omitted from a result.",
        "A --caller tool console caps a result at 25000 characters (a digest instead, with next); max_chars sets the "
        "cap, big lifts it. Past --context-limit (default 400000) envelopes carry context_warning.",
        "One console per agent: a second one is refused as busy.",
        "One-shot: <console> [pid] -c '<call or list of calls>' (or -c @<file>, one call per line). Calls are logged "
        "as facts, never values (usage; --no-usage).",
    };
    return notes;
}

// A verb's help row. Detail: the args, the result in notation (named types
// by name, defined under `types`), the notes and an example call. Machine:
// also the result's type for programs; its named types go to `named`.
inline shape::HelpRow MakeHelpRow(const Verb& verb, bool detail, bool machine, schema::NamedTypes* named = nullptr) {
    shape::HelpRow row{std::string(verb.name), std::string(verb.origin), std::string(ToolPackName(verb.pack)),
        std::string(verb.summary)};
    if (verb.writes) row.writes = true;
    if (verb.blocks) row.blocks = true;
    if (!detail && !machine) return row;
    const schema::Type type = verb.result ? verb.result() : schema::Of<schema::Nothing>();
    row.args.emplace();
    for (const auto& arg : verb.args) row.args->push_back(ArgJson(arg));
    row.result = schema::Notation(type, true);
    if (!verb.notes.empty()) row.notes = std::string(verb.notes);
    row.example = ExampleCall(verb);
    schema::NamedTypes used;
    schema::CollectNamed(type, used, true);
    if (detail && !machine && !used.empty()) {
        row.types.emplace();
        for (const auto& [name, definition] : used) (*row.types)[name] = schema::Notation(definition, true);
    }
    if (machine) row.type = schema::TypeJson(type, true);
    if (named) for (auto& [name, definition] : used) named->emplace(name, std::move(definition));
    return row;
}

// help with `json`: every row in full, the named types once, each origin's
// title and the tool packs. Its text is what `table_hash` hashes.
inline shape::Help MakeHelpTable(std::span<const Verb> verbs, std::span<const Origin> origins) {
    shape::Help help;
    schema::NamedTypes named;
    for (const auto& verb : verbs) help.rows.push_back(MakeHelpRow(verb, true, true, &named));
    help.types.emplace();
    for (const auto& [name, definition] : named)
        (*help.types)[name] = shape::HelpType{schema::Notation(definition, true), schema::TypeJson(definition, true)};
    help.origins.emplace();
    for (const auto& origin : origins)
        if (std::ranges::any_of(verbs, [&](const Verb& verb) { return verb.origin == origin.id; }))
            help.origins->push_back({std::string(origin.id), std::string(origin.title)});
    help.packs.emplace();
    for (const auto& pack : ToolPacks) help.packs->push_back({std::string(pack.name), std::string(pack.job)});
    return help;
}

// help with no verb: one line per verb, grouped by tool pack in pack order.
inline std::vector<shape::HelpRow> HelpByPack(std::span<const Verb> verbs) {
    std::vector<shape::HelpRow> rows;
    for (const auto& pack : ToolPacks)
        for (const auto& verb : verbs)
            if (verb.pack == pack.pack) rows.push_back(MakeHelpRow(verb, false, false));
    return rows;
}

// FNV-1a of the whole table as help with `json` prints it: changes when any
// verb's args or result change.
inline std::string TableHash(std::span<const Verb> verbs, std::span<const Origin> origins) {
    const std::string text = ToJson(MakeHelpTable(verbs, origins)).dump();
    uint64_t hash = 0xcbf29ce484222325ull;
    for (const unsigned char c : text) hash = (hash ^ c) * 0x100000001b3ull;
    char out[20];
    snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(hash));
    return out;
}
}
