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
    constexpr std::string_view hex = "bytes as hex pairs, spaced or packed: \"00 00 80 3f\"";
    constexpr std::string_view module = "the module (default: the exe)";
    constexpr std::string_view labels = "the labels file (default: the host's names file, else the console's)";
    static const std::vector<Verb> verbs = {
        {"help", "core", ToolPack::Explore, "the verbs, one line each; with verb: its args, result, notes and an example",
            {{"verb", "str", "a verb to show in full"},
             {"json", "bool", "the whole table for programs"},
             {"hash", "bool", "only the table's hash: has it changed since a program read it?"}},
            Of<shape::Help>, build::Help},
        {"status", "core", ToolPack::Explore, "agent pid, tick rate, the packs the host offers and the verb table's hash",
            {}, Of<shape::Status>, build::Status,
            "table_hash changes when any verb's args or result change: a cached script or skill is then stale."},
        {"quit", "core", ToolPack::Explore, "end the console", {}, Of<shape::Quit>, build::Quit},
        {"usage", "core", ToolPack::Explore, "what this session cost: calls, output and refusals per verb, largest output first",
            {{"all", "bool", "every session in the log, and the verbs none called"},
             {"sessions", "bool", "the log's sessions, newest first: one row each"},
             {"session", "str", "one session from the log, by its id"},
             {"advice", "bool", "verbs often digested or unfiltered, with a proposed default view.limit or a hint"},
             {"baseline", "file", "a report save wrote: what got worse (and better) against it"},
             {"save", "file", "write this report to a file, a baseline for a later session"}},
            Of<shape::UsageReport>, build::Usage,
            "From the usage log beside the console; works with no target (then: the log's latest session). calls "
            "counts top-level calls (a batch is one, its calls are batched), and so do calls_by_pack and ms; a "
            "verb's row counts its calls wherever they ran. out is characters; out_by_caller.tool is what reached an agent's context. digested counts results past the "
            "result cap (max_chars), and saved the characters their digests kept out (full size less the digest); of "
            "the verb's next call after a digest, followed counts one that changed its narrowing args (the digest's "
            "narrow) and raised one that only raised max_chars or set big. Marks per "
            "verb: unfiltered (it takes a view, own or cls, the call gave none of view's match, nonzero or limit, own, cls or "
            "an optional paths (get's), and printed whole, past 2000 characters; never a refused call), repeated (the same input and the same result again in its session; the same input with another result is "
            "same_args: the state changed, the call was needed), refused (by "
            "error kind; one refused for its pack is not in calls_by_pack). A batch's "
            "calls count under their own verbs (batched); the batch row holds its wrapper. sessions lists up to 30 "
            "sessions as session_rows, with totals over the whole log. One session's report adds context: chars "
            "(what its tool-caller results put into an agent's context), limit (the console's --context-limit, "
            "default 400000; 0: none) and over; past it every envelope a tool caller gets carries context_warning, "
            "one line. Nothing stops: max_chars stays the per-result cap. advice lists verbs with at least two digested "
            "or unfiltered calls (or half their calls), most first: limit proposes a smaller default view.limit "
            "(the default scaled to what the cap held of the largest digested result), hint one line on what else "
            "to change in the row or its help. A report: nothing changes by itself. save writes the report (JSON) and "
            "answers saved; baseline compares this session with such a file: compare.worse and compare.better list the "
            "figures that moved (totals calls, refused, digested, context and ms; refused_by.<kind>; per verb calls, "
            "refused, digested and ms_per_call), totals first, largest move first. A count is worse when it grew, "
            "time when it grew by a quarter and past noise (100 ms in all, 50 ms a call). baseline and save take one "
            "session (session, or none for this one), never all or sessions."},
        {"batch", "core", ToolPack::Explore, "up to 64 calls in as few host ticks as possible; one envelope each",
            {{"calls", "calls", "the calls, each {\"verb\": ..., \"args\": {...}} (up to 64)", {}, true}},
            Of<shape::Batch>, build::Batch,
            "rows holds each call's envelope, with tick (and last_tick when its responses spanned ticks). help, "
            "status, usage, quit, batch and run are not allowed inside."},
        {"run", "core", ToolPack::Explore, "a plan file's steps, each its own call: one NDJSON line per result, then a report",
            {{"plan", "file", "the plan: a JSON list of {\"verb\", \"args\"?, \"expect\"?, \"repeat\"?: {\"every\", \"for\"}}", {}, true},
             {"out", "file", "write the lines here (rewritten), not in the result"}},
            Of<shape::Run>, build::Run,
            "Each step runs as its own call under this console's --packs: a step of a pack outside them is a "
            "refused line, not a stop, so a plan with modify steps needs modify in --packs (and its approval). "
            "Steps run one after another, so values from two steps are never from the same moment: for one "
            "moment, make one batch step (its line holds every item's envelope). repeat runs the step at 0, "
            "every, 2*every, ... seconds while before `for` (ceil(for / every) runs: every 1 for 5 is 5; up to "
            "600 s, 1000 runs). A line is "
            "{step, run?, at_ms, ms, command, ok, result|error|digest}, plus expect (met or failed) and "
            "expect_failed (where the envelope departs) when the step has expect: a subset of the envelope, "
            "{\"ok\": true, \"result\": {...}}. The report (the result's, and out's last line as {report}) counts "
            "calls, ok, failed, refused by kind and expects, with each step's time and the console's and agent's "
            "builds. Up to 256 steps; run and quit are not allowed in a plan. A plan file is the replay key: run it "
            "again to repeat an investigation.", false, true},
        {"read", "core", ToolPack::Explore, "hex dump of 1..4096 bytes (default 64); type decodes values",
            {{"addr", "chain", atChain, {}, true}, {"len", "int", "how many bytes (1..4096)", "64"},
             {"type", "layout", "i8..u64|f32|f64|ptr: values; cstr|wstr: text; \"u8 phase, pad 3, f64[3] start\": a layout"}},
            Of<shape::Read>, build::Read,
            "type adds values (one per element); cstr|wstr adds text, length and terminated (default 256 bytes); "
            "a layout \"<type>[n] [name], pad <n>, ...\" adds layout, {name: value}. A 4- or 8-byte read adds views. "
            "A chain adds chain. region is the memory the address lies in (kind: code, rodata, data, mapped, "
            "private, reserved or free)."},
        {"peek", "core", ToolPack::Explore, "what 1..64 qwords hold (default 8): object, module, heap or value",
            {{"addr", "chain", atChain, {}, true}, {"count", "int", "how many qwords (1..64)", "8"},
             {"label", "bool", "append this one qword as a confirmed global to a labels file"},
             {"file", "file", "with label: the labels file (default: the host's names file, else the console's)"},
             {"name", "str", "with label: the label's name (default FName_<hint>)"}},
            Of<shape::Peek>, build::Peek,
            "object needs a pack; value rows carry floats, a double and name_hint. region is the memory the address "
            "lies in, as read gives it."},
        {"write", "core", ToolPack::Modify, "write up to 256 bytes; undo restores it after a while, hold rewrites it every tick for a while",
            {{"addr", "addr", at, {}, true},
             {"data", "bytes", "bytes as hex pairs, spaced or packed: \"00 00 80 3f\" (up to 256; 64 with undo or hold)", {}, true},
             {"expect", "bytes", "write only while memory still holds these bytes (with undo: required, restored afterwards)"},
             {"undo", "obj", "restore the bytes after a while, guarded on their owner (needs expect)", {}, false, false, {},
              {{"after", "dur", "restore after this long (up to 60 s)", {}, true},
               {"owner", "ref", "the live actor or component holding the address (a pack names it)", {}, true},
               {"object", "addr", "a native object inside the owner whose vtable is guarded too"}}},
             {"hold", "dur", "rewrite the bytes every tick for this long (up to 60 s)"}},
            Of<shape::Write>, build::Write,
            "A refused expect answers an error whose `found` holds the bytes there. undo and hold answer `timed`, "
            "the timed write as timed-writes lists it; up to 64 bytes. undo stops as conflict if the game changes the "
            "value, abandoned if the owner or object's vtable changes; the owner's check is the pack's, so without "
            "one it is refused. hold on an addr based on a literal ref ({\"base\": <ref>, \"offset\": N}) stops "
            "when that object is freed or replaced. Options: expect guards every write; undo and hold are the two "
            "timed forms (undo's owner and object only guard an undo, so they live inside it).", true},
        {"timed-writes", "core", ToolPack::Modify, "the timed writes (write undo, hold) with their state; clear ends one or all, restoring undo ones",
            {{"clear", "bool", "end them"},
             {"id", "str", "with clear: one write's id as timed-writes lists it (undo:N or hold:N); default all"}},
            Of<shape::Writes>, build::TimedWrites,
            "Rows are undo writes (restored, conflict or abandoned ones stay listed until cleared) then hold ones. "
            "clear answers cleared and the rows it ended.", true},
        {"modules", "core", ToolPack::Explore, "loaded modules, exe first (also refreshes the cached list)",
            {}, Of<shape::Modules>, build::Modules},
        {"search", "core", ToolPack::Native, "addresses holding a value in resident, cacheable memory, as a job the agent runs",
            {{"type", "hex|u32|i32|u64|ptr|f32|f64|text|wtext", "the value's type (wtext: text as UTF-16)", {}, true},
             {"value", "scalar", "the value: text or hex pairs as a string, a number otherwise (u64 and ptr also \"0x...\")", {}, true, false,
              "\"00 00 80 3f\""},
             {"scope", "obj", "where to look (default: the main module's resident pages)", {}, false, false, {},
              {{"module", "str", "one module, in place of start and to"},
               {"start", "addr", "start, or a job's next_cursor"},
               {"to", "addr", "end"},
               {"writable", "bool", "writable memory only, every module and heap"},
               {"heap", "bool", "private memory only (heaps, stacks)"},
               {"paged", "bool", "also non-resident pages (slow)"}}},
             {"align", "int", "1, 2, 4, 8 or 16 (typed values default to their size)"},
             {"wait", "int", "seconds to wait for the job before answering with it running (0..60)", "10"},
             {"max", "int", "hits at most (1..507); the job stops when full", "64"}},
            Of<shape::Search>, build::Search,
            "The range is the main module unless scope says otherwise; scope.start alone ends with the module it "
            "lies in. A job still running after wait comes back with state running and its hits so far: search-job "
            "with its id for more, with stop to end it. A new search stops the last job. next_cursor (a stopped or "
            "full job) resumes as scope.start. Options: scope picks the memory, align the step, wait how long the "
            "call blocks, max when the job stops (what the agent does, so not a view)."},
        {"search-job", "core", ToolPack::Native, "a search job's state and hits so far; stop ends it first",
            {{"job", "int", "the job a search returned", {}, true},
             {"stop", "bool", "end the job first; its next_cursor resumes it"}},
            Of<shape::Search>, build::SearchJob,
            "Only the last job answers. scanned is the share of the range's searchable bytes (the committed, "
            "readable memory scope takes) behind the cursor, 0 while the job first measures them."},
        {"mem-snap", "core", ToolPack::Observe, "keep up to 1 MiB of memory for mem-diff (a file beside the cache, across runs)",
            {{"addr", "chain", atChain, {}, true}, {"len", "int", "how many bytes, up to 1048576", {}, true}},
            Of<shape::MemSnap>, build::MemSnap,
            "A new snap replaces the last one and its candidates. Refused when part of the range is unreadable."},
        {"mem-diff", "core", ToolPack::Observe, "elements of the last mem-snap that changed, stayed, rose or fell since; keep narrows",
            {{"compare", "changed|unchanged|increased|decreased", "what an element must have done", {}, true},
             {"type", "str", "the element type: i8..u64, f32, f64 or ptr; elements every size bytes from the start", {}, true, false,
              "\"f32\""},
             {"keep", "bool", "keep only the matches as candidates and today's bytes as the new baseline"},
             View("limit", "64")},
            Of<shape::MemDiff>, build::MemDiff,
            "Reads the range again. changed and unchanged compare bytes; increased and decreased compare numbers "
            "(NaN never passes). Narrowing: mem-snap, act, mem-diff changed f32 with keep, act, mem-diff ... with "
            "keep until few remain; matched counts all, rows show the first (view.limit: 1..256). A snap from another agent "
            "instance is refused."},
        {"watch-start", "core", ToolPack::Observe, "a capture the agent keeps (up to 60 min with every); returns at once",
            {{"duration", "dur", "how long, in seconds", {}, true},
             {"ranges", "range", "up to 8 ranges to sample, each {\"addr\": <addr>, \"len\": N}, {\"addr\": <addr>, \"type\": \"f64\", "
              "\"count\"?: N} to decode values, {\"object\": <ref>, \"field\": <path>} for a reflected field (a pack "
              "resolves it); following a root, {\"offset\": N, ...} in place of addr", {}, true, true},
             {"every", "dur", "sample interval (default: every tick, then at most 30 s)"},
             {"follow", "str", "continue on a host root's replacement; ranges are offsets from it"},
             {"file", "file", "keep every sample in this file (JSON lines), rewritten by each watch-read and watch-stop"}},
            Of<shape::WatchStart>, build::WatchStart,
            "status lists the roots. The capture outlives the console, its types too. A typed range's samples are "
            "numbers (a list for count), and watch-read and watch-stop add its summary: first, last, least, greatest "
            "and the change per second, per value. A field range takes the field's place and type at the start and "
            "is guarded on its object; an addr based on a literal ref is guarded on that object. file writes the "
            "ranges as the first line now; the file persists for the agent instance in the cache, and a later "
            "watch-start without file drops it. For a blocking capture, start, wait and stop (the Python session's "
            "watch helper)."},
        {"watch-read", "core", ToolPack::Observe, "the capture so far: its first sample, each change and its last",
            {{"all", "bool", "every sample, not only the changes"}}, Of<shape::WatchCapture>, build::WatchRead,
            "Rows are the first sample, every sample that differs from the one before (each with its time) and the last; "
            "samples counts them all. Past 200 rows the middle is dropped (dropped). The summary covers every sample, "
            "with changes per value. all returns every sample. With watch-start's file, every sample also goes to "
            "the file (file names it)."},
        {"watch-stop", "core", ToolPack::Observe, "end the capture and return it: its first sample, each change and its last",
            {{"all", "bool", "every sample, not only the changes"}}, Of<shape::WatchCapture>, build::WatchStop,
            "As watch-read."},
        {"sig-check", "core", ToolPack::Native, "is a byte signature unique in a module, and does it land where expected; file checks a file of them",
            {{"pattern", "pattern", "the signature: \"48 8b ?? 89\" or \"0x48, 0x8B, -1, 0x89\", up to 96 bytes", {}, false, false,
              "\"48 8b 05 ?? ?? ?? ?? 48 85 c0\""},
             {"file", "file", "a JSON-lines file, one signature per line, in place of pattern"},
             {"module", "str", module},
             {"expect", "addr", "where it should land"},
             {"offset", "int", "the match starts this far past expect"},
             {"resolve", "str", "a reference signature, \"next,field[,size]\" as labels files keep it; expect is then its target"}},
            Of<shape::SigCheckResult>, build::SigCheck,
            "Reports mismatches, diverges_at and warnings for unmasked addresses. file takes no other key and "
            "answers file, winner (the first line that holds) and a row per line. Options: pattern or file is the "
            "signature; module, expect, offset and resolve say where it must land, each a field labels files keep."},
        {"sig-bytes", "core", ToolPack::Native, "live bytes (default 64, up to 96; longer is cut: capped) in both pattern forms, address fields wildcarded",
            {{"addr", "addr", at, {}, true}, {"len", "int", "how many bytes", "64"}},
            Of<shape::SigBytes>, build::SigBytes},
        {"labels-sign", "core", ToolPack::Native, "a byte signature on each of this build's user and global names, so they survive a patch",
            {{"file", "file", labels}, {"module", "str", module},
             {"retry", "bool", "sign rows that have a signature or failed before too"}},
            Of<shape::LabelsSign>, build::LabelsSign,
            "A function's signature starts at it, a global's at the first code that loads it (a reference signature); "
            "each is cut on whole instructions and grows from 16 bytes until exactly one match lands on the row (up to 96). It is stored as the row's "
            "sig (pattern, offset or resolve, as sig-check takes them); a row it cannot sign is listed with why and "
            "marked sig_failed (timestamp, reason): later calls on that build skip it (failed_before) unless retry. "
            "The file is rewritten in place; other builds' lines stay as they are."},
        {"labels-port", "core", ToolPack::Native, "find other builds' signed names in the running build and add them to the file",
            {{"file", "file", labels}, {"module", "str", module}},
            Of<shape::LabelsPort>, build::LabelsPort,
            "Per name this build lacks, the newest signed row of another build is matched in the image (one shared "
            "search per anchor). One match appends a row for this build with the same sig and ported_from (the old "
            "timestamp); none (missing: the code changed) or several (ambiguous) are listed, never written. Old "
            "rows stay."},
        {"disasm", "core", ToolPack::Native, "instructions decoded from live bytes: 1..1024 (default 24; 8 bytes each, at least 512), or a whole function",
            {{"addr", "chain", atChain, {}, true}, {"count", "int", "how many instructions (1..1024; with function: up to 2048)", "24"},
             {"function", "bool", "list the whole containing function (exception table; else walk from addr)"},
             {"resume", "bool", "with function: start at addr (a cut listing's next), not the function's start"},
             {"save", "obj", "with function: write the function and its read-only data for a static tool, not the rows",
              {}, false, false, {},
              {{"file", "file", "the file to write", {}, true},
               {"callees", "int", "also save the functions it calls or tail-jumps to, this many levels deep (1..3)"},
               {"labels", "file", "name from this labels file (default: the console's and the host's names file)"}}},
             {"follow", "bool", "step through up to 3 unconditional direct jumps"}},
            Of<shape::Disasm>, build::Disasm, "An answer about one address: the default count stays small, and a whole "
            "function or more belongs in a save file for a static tool (save), not in a reply. Rows carry rva, bytes, text, targets, string hints, import names and the "
            "value a read-only [rip + disp] load reads; the listing names the module once. meta adds each row's module "
            "and address fields. function lists to the function's end, up to 2048 "
            "instructions or 16 KB (a cut gives next: continue with resume), with compact rows (no bytes, rva on the "
            "first row; meta for full rows) and strings: each string referenced, with the rows using it. Where no "
            "exception-table entry holds addr it walks from addr to the first return or jump no earlier forward branch "
            "passes: stop \"walk end\", no function key. Strings built at run time, reached through a pointer, or held as "
            "names are not seen. save leaves rows and strings out of the result (the file holds them) and adds saved "
            "and saved_rows, and saves each chunk the function jumps into: chained entries and switch targets "
            "(chunks, up to 256, then capped), each switch's table as data (tables), and the function's targets "
            "still unsaved in jumpouts_left; callees follows direct calls and tail jumps breadth-first and adds functions (each saved one, "
            "with its depth) and skipped (each target left out, why: depth, count past 16 callees, bytes past 256 KB of code in the whole "
            "save, unreadable). With an engine pack, a saved function whose label names a reflected class (Class::Method, or "
            "Class_Method by the longest class prefix) brings that class's layout (layout-export with closure 1) into the file "
            "as layout rows, counted in layouts (classes, types). A walk from more than 32 bytes past the last return, jump or padding in its unlisted piece adds "
            "piece (the piece holding addr, as xrefs gives it), and save then refuses unless resume. Options: count and function decide what is read (not a view: "
            "they bound the decoding), resume continues a cut listing, save writes files, follow steps through jumps."},
        {"xrefs", "core", ToolPack::Native, "what points at an address: rel32 fields and stored pointers, each with its function",
            {{"addr", "addr", at, {}, true},
             {"module", "str", "scan another loaded module"},
             {"all", "bool", "also list unknown hits (likely chance bytes)"}},
            Of<shape::Xrefs>, build::Xrefs, "A code row outside every exception-table entry carries unlisted, its "
            "function the piece holding it, cut as touches cuts unlisted code (between returns and int3 padding, within "
            "16 KB each side): a function with several returns spans several pieces, and junk bytes may lead one. "
            "Entries under 4 bytes (a packed image's decoys) bound nothing."},
        {"touches", "core", ToolPack::Native, "functions near an address whose [reg + disp] operands use the offsets (up to 8)",
            {{"addr", "addr", at, {}, true}, {"offsets", "int", "the struct offsets to look for (up to 8, each above 0)", {}, true, true},
             {"range", "int", "bytes each side of addr (1..0x400000)", "\"0x80000\""},
             {"reads", "bool", "rank by offsets read; drop rows writing every offset they read"}},
            Of<shape::Touches>, build::Touches, "Most offsets first, writes marked: the code behind an unreflected member. "
            "Code outside every exception-table entry is scanned too, in pieces cut at returns and int3 padding: "
            "those rows carry unlisted, their bounds the piece's. Entries under 4 bytes (a packed image's decoys) count as "
            "unlisted code. reads leaves out struct copies and constructors."},
        {"vtable", "core", ToolPack::Native, "an object's virtual table: slots until the first entry that is not code",
            {{"addr", "chain", atChain, {}, true}, {"count", "int", "how many slots at most (1..512)"},
             {"offset", "int", "the table pointer at addr+offset (an interface part; 0..0x100000)"},
             {"table", "bool", "addr is the table itself"},
             {"resolve", "bool", "where jump stubs lead"},
             {"rtti", "bool", "also the class and bases its run-time type information names (MSVC images)"}},
            Of<shape::Vtable>, build::Vtable, "rtti adds rtti, read from the locator in the "
            "slot before the table; complete is the whole object when addr is one of its parts. Where that slot holds "
            "no locator (no type information, or not an MSVC image; engines often build without it) it adds "
            "rtti_error instead. Options: count bounds what is read (not a view); offset and table say where the "
            "table pointer is; resolve and rtti add reads."},
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
        "A call is one JSON object per line: {\"verb\": \"read\", \"args\": {\"addr\": \"0x1000\", \"len\": 16}}, "
        "args named as help shows them; \"meta\", \"timing\" and \"big\" as true, \"max_chars\" as a number. A line may "
        "also hold a JSON list of calls.",
        "help with verb shows its args (type, default, help), its result's keys, notes and an example call; help "
        "with json the whole table.",
        "Unknown keys, missing required ones and values of the wrong type are refused (kind schema) with the key's "
        "part of the schema and a corrected example.",
        "An int is a number or \"0x...\" text. An addr is \"0x...\", a literal ref, {\"module\": \"x.dll\", \"rva\": N} "
        "or {\"base\": <addr>, \"offset\": N}; addresses inside a module carry module and rva.",
        "A chain is {\"base\": <addr>, \"chain\": [N, N]} in read, peek, mem-snap, vtable and disasm: each offset "
        "but the last reads a pointer, so one alone is refused ([0, N] reads the pointer at base and adds N).",
        "A path is a list of field names: [\"Info\", \"Owner\"]. A key that does not apply is omitted from a result.",
        "meta prints struct values with their layout; timing adds trips, wait_ms and total_ms.",
        "A --caller tool console prints at most 25000 characters a result; past that a digest stands in for it, with "
        "its largest parts and next, the call narrowed (or file, for a call that must not run again). max_chars sets "
        "the limit, big lifts it. Past the session's --context-limit (default 400000 characters to a tool caller) "
        "each envelope also carries context_warning; usage shows the total.",
        "One console per agent: a second one is refused as busy.",
        "The module list and a pack's lookups persist for the agent instance in %TEMP%\\reagentlive-<pid>.cache.",
        "One-shot: <console> [pid] -c '<call or list of calls>' (or -c @<file>, one call per line) emits JSON Lines.",
        "Each call is logged beside the console as facts (verb, keys, sizes, refusal), never values; usage reads it, "
        "also with no target. --caller tool|script|shell says who reads the output; --no-usage turns it off.",
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
