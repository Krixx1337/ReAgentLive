// reagentlive CLI: the verb table's core rows (syntax/verb.h): what each core
// verb takes and returns. Parsing, `help`, `help --json`, the generated
// docs/commands/results.md and the Python client's strict results all read
// these rows. The Unreal pack's rows are in results/ue/verbs_ue.h.
#pragma once
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/code/code_json.h"
#include "reagentlive/frontends/cli/code/signature_json.h"
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/frontends/cli/syntax/parse_internal.h"

namespace reagentlive::cli {
inline const std::vector<Verb>& CoreVerbs() {
    using schema::Of;
    using enum Arity;
    static const std::vector<Verb> verbs = {
        {"help", "core", "the verbs, one line each; help <verb>: its flags, result and notes",
            {{"verb", Optional, "a verb to show in full"}}, {{"--json", Takes::Nothing, "", "the whole table for programs"}},
            Of<shape::Help>, parse::Help, {}, {"?"}},
        {"status", "core", "agent pid, tick rate, the packs the host offers and the verb table's hash",
            {}, {}, Of<shape::Status>, parse::Status,
            "table_hash changes when any verb's arguments or result change: a cached script or skill is then stale."},
        {"quit", "core", "end the console", {}, {}, Of<shape::Quit>, parse::Quit, {}, {"exit", "q"}},
        {"batch", "core", "up to 64 commands in as few host ticks as possible; one envelope each",
            {{"{ <command>; <command>; ... }", Rest, "command lines between braces, split by ;"}}, {}, Of<shape::Batch>, parse::Batch,
            "A $N from an earlier find in the batch works. rows holds each command's envelope, with tick "
            "(and last_tick when its responses spanned ticks)."},
        {"read", "core", "hex dump of 1..4096 bytes (default 64); --as decodes values",
            {{"addr", One, "an address: 0x hex, $N[+off], base+off or <module>+off"}, {"len", Optional, "how many bytes"}},
            {{"--as", Takes::Rest, "type", "i8..u64|f32|f64|ptr: values; cstr|wstr: text; \"u8 phase, pad 3, f64[3] start\": layout"}},
            Of<shape::Read>, parse::Read,
            "--as <type> adds values (one per element); --as cstr|wstr adds text, length and terminated (default "
            "256 bytes); --as \"<type>[n] [name], pad <n>, ...\" adds layout, {name: value}. A 4- or 8-byte read "
            "adds views. <addr>->0x10->0x8 follows a pointer chain and adds chain."},
        {"read-batch", "core", "read up to 8 ranges in one host tick",
            {{"addr len", Some, "an address and a length, per range"}}, {}, Of<shape::ReadBatch>, parse::ReadBatch},
        {"peek", "core", "what 1..64 qwords hold (default 8): object, module, heap or value",
            {{"addr", One, "an address: 0x hex, $N[+off], base+off or <module>+off"}, {"count", Optional, "how many qwords"}},
            {{"--label", Takes::One, "file", "append this one qword as a confirmed global to a labels file"},
             {"--name", Takes::One, "name", "the label's name (default FName_<hint>)"}},
            Of<shape::Peek>, parse::Peek,
            "object needs a pack; value rows carry floats, a double and name_hint. Accepts a pointer chain."},
        {"write", "core", "write up to 4096 bytes",
            {{"addr", One, "an address: 0x hex, $N[+off], base+off or <module>+off"}, {"hex bytes", Some, "bytes as hex pairs, spaced or packed: 00 00 80 3f"}},
            {{"--expect", Takes::Some, "hex bytes", "write only while memory still holds these bytes"}},
            Of<shape::Write>, parse::Write,
            "e.g. write $1+0x120 00 00 80 3f. A refused --expect answers an error whose `found` holds the bytes there.", {}, true},
        {"trial", "core", "one write the agent undoes (default 10 s, up to 60 s)",
            {{"addr", One, "an address: 0x hex, $N[+off], base+off or <module>+off"}, {"hex bytes", Some, "bytes as hex pairs, spaced or packed: 00 00 80 3f"}},
            {{"--owner", Takes::One, "ref", "the live actor or component holding the address (a pack names it)", true},
             {"--expect", Takes::Some, "hex bytes", "the bytes there now, restored afterwards", true},
             {"--object", Takes::One, "addr", "a native object inside the owner whose vtable is guarded too"},
             {"--seconds", Takes::One, "N", ""}, {"--ms", Takes::One, "N", ""}},
            Of<shape::Trial>, parse::Trial,
            "Stops as conflict if the game changes the value, abandoned if the owner or --object's vtable changes.", {}, true},
        {"trial-list", "core", "trials with their state", {}, {}, Of<shape::TrialList>, parse::TrialList},
        {"trial-clear", "core", "end one trial or all, restoring active ones",
            {{"id", Optional, "its id (default: all)"}}, {}, Of<shape::TrialClear>, parse::TrialClear, {}, {}, true},
        {"base", "core", "exe base and size", {}, {}, Of<shape::ModuleRow>, parse::Base},
        {"modules", "core", "loaded modules, exe first (also refreshes the cached list)",
            {}, {}, Of<shape::Modules>, parse::Modules},
        {"search", "core", "addresses holding a value in resident, cacheable memory",
            {{"hex|u32|i32|u64|ptr|f32|f64|text", One, "the value's type"}, {"value", Some, "the value to find"}},
            {{"--wide", Takes::Nothing, "", "text as UTF-16"}, {"--from", Takes::One, "addr", "start, or a page's next_cursor"},
             {"--to", Takes::One, "addr", ""}, {"--module", Takes::One, "name", "in place of --from and --to"},
             {"--align", Takes::One, "N", "1, 2, 4, 8 or 16 (typed values default to their size)"},
             {"--writable", Takes::Nothing, "", ""}, {"--heap", Takes::Nothing, "", "private memory only"},
             {"--paged", Takes::Nothing, "", "also non-resident pages (slow)"},
             {"--background", Takes::Nothing, "", "agent worker thread, no host-tick cost (host opt-in)"},
             {"--max", Takes::One, "N", "rows per page (default 64)"}},
            Of<shape::Search>, parse::Search,
            "Quote text to keep spaces, ';' and '--'. next_cursor is present while more of the range is left."},
        {"pin", "core", "write the bytes every tick (default 10 s, up to 60 s)",
            {{"addr", One, "an address: 0x hex, $N[+off], base+off or <module>+off"}, {"hex bytes", Some, "bytes as hex pairs, spaced or packed: 00 00 80 3f"}},
            {{"--seconds", Takes::One, "N", ""}, {"--ms", Takes::One, "N", ""},
             {"--expect", Takes::Some, "hex bytes", "start only while memory holds these bytes"}},
            Of<shape::Pin>, parse::Pin, "$N+off targets stop when that object is freed or replaced.", {}, true},
        {"pin-list", "core", "pins with state and write counts", {}, {}, Of<shape::PinList>, parse::PinList},
        {"pin-clear", "core", "clear one pin or all", {{"id", Optional, "its id (default: all)"}}, {}, Of<shape::PinClear>, parse::PinClear},
        {"watch", "core", "sample up to 8 ranges every tick (up to 30 s) and return the rows",
            {{"seconds|Nms", One, "how long: seconds, or Nms, Ns"}, {"addr:len", Some, "a range to sample, per range"}},
            {{"--then", Takes::Rest, "call line", "a raw call (a pack runs it) right after sampling starts"}},
            Of<shape::Watch>, parse::Watch, {}, {}, true},
        {"watch-start", "core", "a capture the agent keeps (up to 60 min with --every); returns at once",
            {{"duration", One, "how long: seconds, or Nms, Ns, Nm"}, {"addr|offset:len", Some, "a range to sample; following a root, its offset from the root"}},
            {{"--every", Takes::One, "duration", "sample interval (default: every tick, then at most 30 s)"},
             {"--follow", Takes::One, "root", "continue on a host root's replacement; ranges are offsets from it"}},
            Of<shape::WatchStart>, parse::WatchStart, "status lists the roots. The capture outlives the console."},
        {"watch-read", "core", "the capture's rows so far", {}, {}, Of<shape::WatchCapture>, parse::WatchRead},
        {"watch-stop", "core", "end the capture and return its rows", {}, {}, Of<shape::WatchCapture>, parse::WatchStop},
        {"sig-check", "core", "is a byte signature unique in a module, and does it land where expected",
            {{"pattern", Some, "the signature: 48 8b ?? 89, or 0x48, 0x8B, -1, 0x89"}},
            {{"--module", Takes::One, "name", "default: the exe"}, {"--expect", Takes::One, "addr", "where it should land"},
             {"--offset", Takes::One, "N", "the match starts this far past --expect"},
             {"--resolve", Takes::One, "next,field[,size]", "a reference signature; --expect is then its target"}},
            Of<shape::SigCheck>, parse::SigCheck,
            "pattern: \"48 8b ?? 89\" or \"0x48, 0x8B, -1, 0x89\", up to 96 bytes. Reports mismatches, diverges_at "
            "and warnings for unmasked addresses."},
        {"sig-file", "core", "check a file of signatures (one JSON line each); names the first that holds",
            {{"patterns file", One, "a JSON-lines file, one signature per line"}}, {}, Of<shape::SigFile>, parse::SigFile},
        {"sig-bytes", "core", "live bytes (default 64, up to 96) in both pattern forms, address fields wildcarded",
            {{"addr", One, "an address: 0x hex, $N[+off], base+off or <module>+off"}, {"len", Optional, "how many bytes"}}, {}, Of<shape::SigBytes>, parse::SigBytes},
        {"disasm", "core", "1..64 instructions (default 24, up to 512 bytes) decoded from live bytes",
            {{"addr", One, "an address: 0x hex, $N[+off], base+off or <module>+off"}, {"count", Optional, "how many instructions"}},
            {{"--function", Takes::Nothing, "", "list the containing function (exception table)"},
             {"--follow", Takes::Nothing, "", "step through up to 3 unconditional direct jumps"}},
            Of<shape::Disasm>, parse::Disasm, "Rows carry targets, string hints and address fields. Accepts a pointer chain."},
        {"xrefs", "core", "what points at an address: rel32 fields and stored pointers, each with its function",
            {{"addr", One, "an address: 0x hex, $N[+off], base+off or <module>+off"}},
            {{"--module", Takes::One, "name", "scan another loaded module"},
             {"--all", Takes::Nothing, "", "also list unknown hits (likely chance bytes)"}},
            Of<shape::Xrefs>, parse::Xrefs},
        {"touches", "core", "functions near an address whose [reg + disp] operands use the offsets (up to 8)",
            {{"addr", One, "an address: 0x hex, $N[+off], base+off or <module>+off"}, {"offset", Some, "a struct offset to look for, per offset"}},
            {{"--range", Takes::One, "N", "bytes each side of addr (default 0x80000)"}},
            Of<shape::Touches>, parse::Touches, "Most offsets first, writes marked: the code behind an unreflected member."},
        {"vtable", "core", "an object's virtual table: slots until the first entry that is not code",
            {{"addr", One, "an address: 0x hex, $N[+off], base+off or <module>+off"}, {"count", Optional, "how many slots at most"}},
            {{"--offset", Takes::One, "N", "the table pointer at addr+N (an interface part)"},
             {"--table", Takes::Nothing, "", "addr is the table itself"},
             {"--resolve", Takes::Nothing, "", "where jump stubs lead"}},
            Of<shape::Vtable>, parse::Vtable, "Accepts a pointer chain."},
        {"region", "core", "what the memory at an address is: kind, base, size, protection",
            {{"addr", One, "an address: 0x hex, $N[+off], base+off or <module>+off"}}, {}, Of<shape::Region>, parse::Region, "Accepts a pointer chain."},
    };
    return verbs;
}

// What applies to every verb; `help` prints these under the rows.
inline const std::vector<std::string>& GeneralNotes() {
    static const std::vector<std::string> notes = {
        "help <verb> shows a verb's flags, its result's keys and notes; help --json the whole table.",
        "Numbers: 0x hex or decimal. <addr> accepts $N, $N+offset, base+offset and <module>+offset "
        "(game.exe+0x1a2b3c, inside the image). Addresses inside a module carry module and rva.",
        "<addr>->0x10->0x8 is a pointer chain in read, peek, vtable, disasm and region: each ->off but the last reads a pointer.",
        "A token in double quotes is taken exactly. A key that does not apply is omitted from a result.",
        "Any line ending in --timing adds trips, wait_ms and total_ms; --meta prints struct values with their layout.",
        "One console per agent: a second one is refused as busy.",
        "The module list, $N and a pack's lookups persist for the agent instance in %TEMP%\\reagentlive-<pid>.cache.",
        "One-shot: <console> [pid] -c \"status; modules\" emits JSON Lines.",
    };
    return notes;
}

inline std::string_view TakesName(Takes takes) {
    switch (takes) {
    case Takes::Nothing: return "nothing";
    case Takes::One: return "one";
    case Takes::Maybe: return "maybe";
    case Takes::Some: return "some";
    case Takes::Rest: return "rest";
    }
    return "nothing";
}
inline std::string_view ArityName(Arity arity) {
    switch (arity) {
    case Arity::One: return "one";
    case Arity::Optional: return "optional";
    case Arity::Some: return "some";
    case Arity::Any: return "any";
    case Arity::Rest: return "rest";
    }
    return "one";
}

// A verb's help row. Detail: the positionals, the flags, the result in
// notation and the notes. Machine: also the result's type for programs.
inline shape::HelpRow MakeHelpRow(const Verb& verb, bool detail, bool machine) {
    shape::HelpRow row{std::string(verb.name), std::string(verb.origin), Usage(verb), std::string(verb.summary),
        {}, {}, {}, {}, {}, {}, {}};
    if (verb.writes) row.writes = true;
    if (!detail && !machine) return row;
    const schema::Type type = verb.result ? verb.result() : schema::Of<schema::Nothing>();
    if (!verb.aliases.empty()) {
        row.aliases.emplace();
        for (const auto alias : verb.aliases) row.aliases->emplace_back(alias);
    }
    row.flags.emplace();
    for (const auto& flag : verb.flags) {
        shape::HelpFlag item{std::string(flag.name), std::string(TakesName(flag.takes)), {}, {}, {}};
        if (flag.required) item.required = true;
        if (!flag.value.empty()) item.value = std::string(flag.value);
        if (!flag.help.empty()) item.help = std::string(flag.help);
        row.flags->push_back(std::move(item));
    }
    row.result = schema::Notation(type);
    if (!verb.notes.empty()) row.notes = std::string(verb.notes);
    row.positionals.emplace();
    for (const auto& positional : verb.positionals) {
        shape::HelpPositional item{std::string(positional.name), std::string(ArityName(positional.arity)), {}};
        if (!positional.help.empty()) item.help = std::string(positional.help);
        row.positionals->push_back(std::move(item));
    }
    if (machine) row.type = schema::TypeJson(type);
    return row;
}

// help --json: every row in full. Its text is what `table_hash` hashes.
inline shape::Help MakeHelpTable(std::span<const Verb> verbs) {
    shape::Help help;
    for (const auto& verb : verbs) help.rows.push_back(MakeHelpRow(verb, true, true));
    return help;
}

// FNV-1a of the whole table as help --json prints it: changes when any
// verb's arguments or result change.
inline std::string TableHash(std::span<const Verb> verbs) {
    const std::string text = ToJson(MakeHelpTable(verbs)).dump();
    uint64_t hash = 0xcbf29ce484222325ull;
    for (const unsigned char c : text) hash = (hash ^ c) * 0x100000001b3ull;
    char out[20];
    snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(hash));
    return out;
}
}
