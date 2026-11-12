// reagentlive CLI: the verb table's core rows (syntax/verb.h): what each core
// verb takes and returns. Parsing, `help`, `help --json`, the generated
// docs/commands/results.md and the Python client's strict results all read
// these rows, and a JSON call's arguments are read by them (CallArgs). The
// Unreal pack's rows are in results/ue/verbs_ue.h.
#pragma once
#include <algorithm>
#include <expected>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/code/code_json.h"
#include "reagentlive/frontends/cli/code/signature_json.h"
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/frontends/cli/results/stats.h"
#include "reagentlive/frontends/cli/syntax/parse_internal.h"

namespace reagentlive::cli {
inline const std::vector<Verb>& CoreVerbs() {
    using schema::Of;
    using enum Arity;
    constexpr std::string_view at = "an address: 0x hex, $N[+off], base+off or <module>+off";
    constexpr std::string_view hex = "bytes as hex pairs, spaced or packed: 00 00 80 3f";
    static const std::vector<Verb> verbs = {
        {"help", "core", "the verbs, one line each; help <verb>: its flags, result and notes",
            {{"verb", "verb", Optional, "str", "a verb to show in full"}},
            {{"--json", Takes::Nothing, "bool", "", "the whole table for programs"}},
            Of<shape::Help>, parse::Help, {}, {"?"}},
        {"status", "core", "agent pid, tick rate, the packs the host offers and the verb table's hash",
            {}, {}, Of<shape::Status>, parse::Status,
            "table_hash changes when any verb's arguments or result change: a cached script or skill is then stale."},
        {"quit", "core", "end the console", {}, {}, Of<shape::Quit>, parse::Quit, {}, {"exit", "q"}},
        {"stats", "core", "what this session cost: calls, output and refusals per verb, largest output first",
            {}, {{"--all", Takes::Nothing, "bool", "", "every session in the log, and the verbs none called"}},
            Of<shape::Stats>, parse::Stats,
            "From the usage log beside the console; works with no target (then: the log's latest session). out is "
            "characters; out_by_caller.tool is what reached an agent's context. Marks per verb: unfiltered (it has "
            "--match, --nonzero, --own, --changed, --cls or --limit and the call gave none), repeated (the same input "
            "again in its session), refused (by error kind). A batch's items count under their own verbs (batched); "
            "the batch row holds its wrapper."},
        {"batch", "core", "up to 64 commands in as few host ticks as possible; one envelope each",
            {{"calls", "{ <command>; <command>; ... }", Rest, "calls",
              "command lines between braces, split by ;  (a JSON call: a list of {verb, args})"}},
            {}, Of<shape::Batch>, parse::Batch,
            "A $N from an earlier find in the batch works in a typed line. rows holds each command's envelope, with "
            "tick (and last_tick when its responses spanned ticks)."},
        {"read", "core", "hex dump of 1..4096 bytes (default 64); --type decodes values",
            {{"addr", "addr", One, "addr", at}, {"len", "len", Optional, "int", "how many bytes", "64"}},
            {{"--type", Takes::Rest, "layout", "type", "i8..u64|f32|f64|ptr: values; cstr|wstr: text; \"u8 phase, pad 3, f64[3] start\": layout"}},
            Of<shape::Read>, parse::Read,
            "--type <type> adds values (one per element); --type cstr|wstr adds text, length and terminated (default "
            "256 bytes); --type \"<type>[n] [name], pad <n>, ...\" adds layout, {name: value}. A 4- or 8-byte read "
            "adds views. <addr>->0x10->0x8 follows a pointer chain and adds chain."},
        {"read-batch", "core", "read up to 8 ranges in one host tick",
            {{"spans", "addr len", Some, "span", "an address and a length, per range"}}, {}, Of<shape::ReadBatch>, parse::ReadBatch},
        {"peek", "core", "what 1..64 qwords hold (default 8): object, module, heap or value",
            {{"addr", "addr", One, "addr", at}, {"count", "count", Optional, "int", "how many qwords", "8"}},
            {{"--label", Takes::One, "path", "file", "append this one qword as a confirmed global to a labels file"},
             {"--name", Takes::One, "str", "name", "the label's name (default FName_<hint>)"}},
            Of<shape::Peek>, parse::Peek,
            "object needs a pack; value rows carry floats, a double and name_hint. Accepts a pointer chain."},
        {"write", "core", "write up to 4096 bytes",
            {{"addr", "addr", One, "addr", at}, {"data", "hex bytes", Some, "bytes", hex}},
            {{"--expect", Takes::Some, "bytes", "hex bytes", "write only while memory still holds these bytes"}},
            Of<shape::Write>, parse::Write,
            "e.g. write $1+0x120 00 00 80 3f. A refused --expect answers an error whose `found` holds the bytes there.", {}, true},
        {"trial", "core", "one write the agent undoes (default 10 s, up to 60 s)",
            {{"addr", "addr", One, "addr", at}, {"data", "hex bytes", Some, "bytes", hex}},
            {{"--owner", Takes::One, "ref", "ref", "the live actor or component holding the address (a pack names it)", {}, true},
             {"--expect", Takes::Some, "bytes", "hex bytes", "the bytes there now, restored afterwards", {}, true},
             {"--object", Takes::One, "addr", "addr", "a native object inside the owner whose vtable is guarded too"},
             {"--seconds", Takes::One, "int", "N", "how long", "10"}, {"--ms", Takes::One, "int", "N", "how long, in milliseconds"}},
            Of<shape::Trial>, parse::Trial,
            "Stops as conflict if the game changes the value, abandoned if the owner or --object's vtable changes. "
            "The trial is the core's; the owner's check is the pack's, so without one it is refused.", {}, true, false, true},
        {"trial-list", "core", "trials with their state", {}, {}, Of<shape::TrialList>, parse::TrialList},
        {"trial-clear", "core", "end one trial or all, restoring active ones",
            {{"id", "id", Optional, "int", "its id (default: all)"}}, {}, Of<shape::TrialClear>, parse::TrialClear, {}, {}, true},
        {"base", "core", "exe base and size", {}, {}, Of<shape::ModuleRow>, parse::Base},
        {"modules", "core", "loaded modules, exe first (also refreshes the cached list)",
            {}, {}, Of<shape::Modules>, parse::Modules},
        {"search", "core", "addresses holding a value in resident, cacheable memory",
            {{"type", "hex|u32|i32|u64|ptr|f32|f64|text", One, "hex|u32|i32|u64|ptr|f32|f64|text", "the value's type"},
             {"value", "value", Some, "text", "the value to find"}},
            {{"--wide", Takes::Nothing, "bool", "", "text as UTF-16"},
             {"--start", Takes::One, "addr", "addr", "start, or a page's next_cursor"},
             {"--to", Takes::One, "addr", "addr", "end"}, {"--module", Takes::One, "str", "name", "in place of --start and --to"},
             {"--align", Takes::One, "int", "N", "1, 2, 4, 8 or 16 (typed values default to their size)"},
             {"--writable", Takes::Nothing, "bool", "", "writable memory only"},
             {"--heap", Takes::Nothing, "bool", "", "private memory only"},
             {"--paged", Takes::Nothing, "bool", "", "also non-resident pages (slow)"},
             {"--background", Takes::Nothing, "bool", "", "agent worker thread, no host-tick cost (host opt-in)"},
             {"--max", Takes::One, "int", "N", "rows per page", "64"}},
            Of<shape::Search>, parse::Search,
            "Quote text to keep spaces, ';' and '--'. next_cursor is present while more of the range is left."},
        {"pin", "core", "write the bytes every tick (default 10 s, up to 60 s)",
            {{"addr", "addr", One, "addr", at}, {"data", "hex bytes", Some, "bytes", hex}},
            {{"--seconds", Takes::One, "int", "N", "how long", "10"}, {"--ms", Takes::One, "int", "N", "how long, in milliseconds"},
             {"--expect", Takes::Some, "bytes", "hex bytes", "start only while memory holds these bytes"}},
            Of<shape::Pin>, parse::Pin, "$N+off targets stop when that object is freed or replaced.", {}, true},
        {"pin-list", "core", "pins with state and write counts", {}, {}, Of<shape::PinList>, parse::PinList},
        {"pin-clear", "core", "clear one pin or all", {{"id", "id", Optional, "int", "its id (default: all)"}}, {},
            Of<shape::PinClear>, parse::PinClear},
        {"watch", "core", "sample up to 8 ranges every tick (up to 30 s) and return the rows",
            {{"duration", "seconds|Nms", One, "dur", "how long: seconds, or Nms, Ns"},
             {"ranges", "addr:len|addr:type[n]", Some, "range", "a range to sample, per range: a length, or a read --type element (f64, i32[2]) to decode"}},
            {{"--then", Takes::Rest, "line", "call line", "a raw call (a pack runs it) right after sampling starts"}},
            Of<shape::Watch>, parse::Watch, {}, {}, true, true},
        {"watch-start", "core", "a capture the agent keeps (up to 60 min with --every); returns at once",
            {{"duration", "duration", One, "dur", "how long: seconds, or Nms, Ns, Nm"},
             {"ranges", "addr|offset:len|type[n]", Some, "range",
              "a range to sample: a length, or a read --type element (f64, i32[2]) to decode; following a root, its offset from the root"}},
            {{"--every", Takes::One, "dur", "duration", "sample interval (default: every tick, then at most 30 s)"},
             {"--follow", Takes::One, "str", "root", "continue on a host root's replacement; ranges are offsets from it"}},
            Of<shape::WatchStart>, parse::WatchStart,
            "status lists the roots. The capture outlives the console, its types too. A typed range's samples are "
            "numbers (a list for [n]), and watch-read and watch-stop add its summary: first, last, least, greatest "
            "and the change per second, per value."},
        {"watch-read", "core", "the capture's rows so far", {}, {}, Of<shape::WatchCapture>, parse::WatchRead},
        {"watch-stop", "core", "end the capture and return its rows", {}, {}, Of<shape::WatchCapture>, parse::WatchStop},
        {"sig-check", "core", "is a byte signature unique in a module, and does it land where expected",
            {{"pattern", "pattern", Some, "pattern", "the signature: 48 8b ?? 89, or 0x48, 0x8B, -1, 0x89"}},
            {{"--module", Takes::One, "str", "name", "default: the exe"},
             {"--expect", Takes::One, "addr", "addr", "where it should land"},
             {"--offset", Takes::One, "int", "N", "the match starts this far past --expect"},
             {"--resolve", Takes::One, "str", "next,field[,size]", "a reference signature; --expect is then its target"}},
            Of<shape::SigCheck>, parse::SigCheck,
            "pattern: \"48 8b ?? 89\" or \"0x48, 0x8B, -1, 0x89\", up to 96 bytes. Reports mismatches, diverges_at "
            "and warnings for unmasked addresses."},
        {"sig-file", "core", "check a file of signatures (one JSON line each); names the first that holds",
            {{"file", "patterns file", One, "path", "a JSON-lines file, one signature per line"}}, {}, Of<shape::SigFile>, parse::SigFile},
        {"sig-bytes", "core", "live bytes (default 64, up to 96) in both pattern forms, address fields wildcarded",
            {{"addr", "addr", One, "addr", at}, {"len", "len", Optional, "int", "how many bytes", "64"}}, {},
            Of<shape::SigBytes>, parse::SigBytes},
        {"disasm", "core", "1..64 instructions (default 24, up to 512 bytes) decoded from live bytes",
            {{"addr", "addr", One, "addr", at}, {"count", "count", Optional, "int", "how many instructions", "24"}},
            {{"--function", Takes::Nothing, "bool", "", "list the containing function (exception table)"},
             {"--follow", Takes::Nothing, "bool", "", "step through up to 3 unconditional direct jumps"}},
            Of<shape::Disasm>, parse::Disasm, "Rows carry targets, string hints and address fields. Accepts a pointer chain."},
        {"xrefs", "core", "what points at an address: rel32 fields and stored pointers, each with its function",
            {{"addr", "addr", One, "addr", at}},
            {{"--module", Takes::One, "str", "name", "scan another loaded module"},
             {"--all", Takes::Nothing, "bool", "", "also list unknown hits (likely chance bytes)"}},
            Of<shape::Xrefs>, parse::Xrefs},
        {"touches", "core", "functions near an address whose [reg + disp] operands use the offsets (up to 8)",
            {{"addr", "addr", One, "addr", at}, {"offsets", "offset", Some, "int", "a struct offset to look for, per offset"}},
            {{"--range", Takes::One, "int", "N", "bytes each side of addr", "0x80000"}},
            Of<shape::Touches>, parse::Touches, "Most offsets first, writes marked: the code behind an unreflected member."},
        {"vtable", "core", "an object's virtual table: slots until the first entry that is not code",
            {{"addr", "addr", One, "addr", at}, {"count", "count", Optional, "int", "how many slots at most"}},
            {{"--offset", Takes::One, "int", "N", "the table pointer at addr+N (an interface part)"},
             {"--table", Takes::Nothing, "bool", "", "addr is the table itself"},
             {"--resolve", Takes::Nothing, "bool", "", "where jump stubs lead"}},
            Of<shape::Vtable>, parse::Vtable, "Accepts a pointer chain."},
        {"region", "core", "what the memory at an address is: kind, base, size, protection",
            {{"addr", "addr", One, "addr", at}}, {}, Of<shape::Region>, parse::Region, "Accepts a pointer chain."},
    };
    return verbs;
}

// The core's origin; a pack gives its own beside its rows, the host's is HostOrigin.
inline constexpr Origin CoreOrigin{"core", "Core"};
inline constexpr Origin HostOrigin{"host", "Host"};

// A console's verb table: the core's rows, the pack's, then the host's. With
// no pack (no pack origin) the core rows that need one are left out.
inline std::vector<Verb> TableRows(std::span<const Verb> packVerbs, std::span<const Origin> packOrigins,
                                   std::span<const Verb> hostVerbs) {
    std::vector<Verb> verbs;
    for (const auto& verb : CoreVerbs())
        if (!verb.needsPack || !packOrigins.empty()) verbs.push_back(verb);
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
        "help <verb> shows a verb's flags, its result's keys and notes; help --json the whole table.",
        "Numbers: 0x hex or decimal. <addr> accepts $N, $N+offset, base+offset and <module>+offset "
        "(game.exe+0x1a2b3c, inside the image). Addresses inside a module carry module and rva.",
        "<addr>->0x10->0x8 is a pointer chain in read, peek, vtable, disasm and region: each ->off but the last reads a "
        "pointer, so one ->off alone is refused (<addr>->0->0x30 reads the pointer at <addr>).",
        "A token in double quotes is taken exactly. A key that does not apply is omitted from a result.",
        "Any line ending in --timing adds trips, wait_ms and total_ms; --meta prints struct values with their layout.",
        "A line starting with { is a JSON call: {\"verb\": \"read\", \"args\": {\"addr\": \"0x1000\", \"len\": 16}}, "
        "keys as help --json names them; \"meta\" and \"timing\" as true. It takes literal refs, never $N.",
        "One console per agent: a second one is refused as busy.",
        "The module list, $N and a pack's lookups persist for the agent instance in %TEMP%\\reagentlive-<pid>.cache.",
        "One-shot: <console> [pid] -c \"status; modules\" emits JSON Lines.",
        "Each call is logged beside the console as facts (verb, flags, sizes, refusal), never values; stats reads it, "
        "also with no target. --caller tool|script|shell says who reads the output; --no-usage turns it off.",
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
// notation (named types by name, defined under `types`) and the notes.
// Machine: also the result's type for programs; its named types go to `named`.
inline shape::HelpRow MakeHelpRow(const Verb& verb, bool detail, bool machine, schema::NamedTypes* named = nullptr) {
    shape::HelpRow row{std::string(verb.name), std::string(verb.origin), Usage(verb), std::string(verb.summary)};
    if (verb.writes) row.writes = true;
    if (verb.blocks) row.blocks = true;
    if (!detail && !machine) return row;
    const schema::Type type = verb.result ? verb.result() : schema::Of<schema::Nothing>();
    if (!verb.aliases.empty()) {
        row.aliases.emplace();
        for (const auto alias : verb.aliases) row.aliases->emplace_back(alias);
    }
    row.flags.emplace();
    for (const auto& flag : verb.flags) {
        shape::HelpFlag item{std::string(flag.name), std::string(FlagKey(flag)), std::string(TakesName(flag.takes)),
            std::string(flag.type)};
        if (flag.required) item.required = true;
        if (!flag.value.empty()) item.value = std::string(flag.value);
        if (!flag.help.empty()) item.help = std::string(flag.help);
        if (!flag.fallback.empty()) item.fallback = std::string(flag.fallback);
        row.flags->push_back(std::move(item));
    }
    row.result = schema::Notation(type, true);
    if (!verb.notes.empty()) row.notes = std::string(verb.notes);
    row.positionals.emplace();
    for (const auto& positional : verb.positionals) {
        shape::HelpPositional item{std::string(positional.key), std::string(positional.name),
            std::string(ArityName(positional.arity)), std::string(positional.type)};
        if (!positional.help.empty()) item.help = std::string(positional.help);
        if (!positional.fallback.empty()) item.fallback = std::string(positional.fallback);
        row.positionals->push_back(std::move(item));
    }
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

// help --json: every row in full, the named types once, and each origin's
// title. Its text is what `table_hash` hashes.
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
    return help;
}

// FNV-1a of the whole table as help --json prints it: changes when any
// verb's arguments or result change.
inline std::string TableHash(std::span<const Verb> verbs, std::span<const Origin> origins) {
    const std::string text = ToJson(MakeHelpTable(verbs, origins)).dump();
    uint64_t hash = 0xcbf29ce484222325ull;
    for (const unsigned char c : text) hash = (hash ^ c) * 0x100000001b3ull;
    char out[20];
    snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(hash));
    return out;
}

// A JSON call: {"verb": "read", "args": {"addr": "0x1000", "len": 16}}. Its
// arguments are named by the row's keys and land in the same Args a typed
// line gives, so one check and one parser per verb cover both, and nothing is
// quoted or escaped. A list (or one value) for a positional that takes
// several; true for a switch; a number for an int, an addr (as hex) or a dur
// (seconds); batch's calls as a list of calls. A string starting with $ is
// refused: a call carries the literal ref a result printed, never "the last find".
inline std::expected<Args, std::string> CallArgs(const Verb& verb, const Json& given) {
    using Out = std::expected<Args, std::string>;
    const auto fail = [&](std::string why) { return Out(std::unexpect, why + "; usage: " + Usage(verb)); };
    if (!given.is_null() && !given.is_object()) return fail("args is an object of named arguments");
    const auto text = [](const Json& value, std::string_view type, std::string& out) -> std::string {
        if (value.is_string()) {
            out = value.get<std::string>();
            if (type != "line" && out.starts_with('$'))
                return "'" + out + "': a JSON call takes the literal ref a result carries, not $N";
            return {};
        }
        if (value.is_number_unsigned() || (value.is_number_integer() && value.get<int64_t>() >= 0)) {
            const uint64_t number = value.get<uint64_t>();
            out = type == "addr" ? HexNumber(number) : type == "dur" ? std::to_string(number) + "s" : std::to_string(number);
            return {};
        }
        if (value.is_number() && type == "dur") {
            const double seconds = value.get<double>();
            if (seconds <= 0) return "a duration above 0";
            out = std::to_string(static_cast<uint64_t>(seconds * 1000 + 0.5)) + "ms";
            return {};
        }
        if (value.is_number()) {
            out = value.dump();
            return {};
        }
        return "a string or a number";
    };
    // A value, or a list of them (a span's elements are pairs), into tokens.
    const auto tokens = [&](const Json& value, std::string_view type, std::vector<std::string>& out) -> std::string {
        const auto one = [&](const Json& item) -> std::string {
            if (item.is_array()) {
                if (type != "span" || item.size() != 2) return "a list of values";
                for (const auto& part : item) {
                    std::string token;
                    if (auto problem = text(part, out.size() % 2 ? "int" : "addr", token); !problem.empty()) return problem;
                    out.push_back(std::move(token));
                }
                return {};
            }
            std::string token;
            if (auto problem = text(item, type, token); !problem.empty()) return problem;
            out.push_back(std::move(token));
            return {};
        };
        if (!value.is_array()) return one(value);
        for (const auto& item : value)
            if (auto problem = one(item); !problem.empty()) return problem;
        return {};
    };
    // An unknown key first: a misspelt name would otherwise read as a left-out argument.
    if (given.is_object()) {
        for (const auto& [key, value] : given.items()) {
            const bool known = std::ranges::any_of(verb.positionals, [&](const Positional& p) { return p.key == key; }) ||
                std::ranges::any_of(verb.flags, [&](const Flag& f) { return FlagKey(f) == key; });
            if (known) continue;
            std::string keys;
            for (const auto& p : verb.positionals) keys += (keys.empty() ? "" : ", ") + std::string(p.key);
            for (const auto& f : verb.flags) keys += (keys.empty() ? "" : ", ") + std::string(FlagKey(f));
            return fail("unknown argument '" + key + "'" + (keys.empty() ? " (it takes none)" : " (it takes " + keys + ")"));
        }
    }
    Args args;
    // Positionals fill in order: one left out ends the run (only optional ones may be).
    std::vector<bool> taken(verb.positionals.size()), flagTaken(verb.flags.size());
    bool gap = false;
    for (size_t i = 0; i < verb.positionals.size(); ++i) {
        const auto& positional = verb.positionals[i];
        const std::string key(positional.key);
        if (!given.is_object() || !given.contains(key)) {
            gap = true;
            continue;
        }
        taken[i] = true;
        if (gap) return fail(key + " given after a left-out argument");
        const Json& value = given[key];
        if (positional.arity == Arity::Rest) {
            if (positional.type == "calls") {
                if (!value.is_array()) return fail(key + ": a list of {verb, args}");
                for (const auto& call : value) {
                    if (!call.is_object() || !call.contains("verb") || !call["verb"].is_string())
                        return fail(key + ": each call is {verb, args}");
                    args.calls.push_back({call["verb"].get<std::string>(), call.dump()});
                }
                continue;
            }
            if (auto problem = text(value, positional.type, args.rest); !problem.empty()) return fail(key + ": " + problem);
            continue;
        }
        if (value.is_array() && (positional.arity == Arity::One || positional.arity == Arity::Optional))
            return fail(key + ": one value, not a list");
        std::vector<std::string> values;
        if (auto problem = tokens(value, positional.type, values); !problem.empty()) return fail(key + ": " + problem);
        for (auto& part : values) args.positionals.push_back(std::move(part));
    }
    for (size_t i = 0; i < verb.flags.size(); ++i) {
        const auto& flag = verb.flags[i];
        const std::string key(FlagKey(flag));
        if (!given.is_object() || !given.contains(key)) continue;
        flagTaken[i] = true;
        const Json& value = given[key];
        Args::Given item{flag.name, {}, {}};
        switch (flag.takes) {
        case Takes::Nothing:
            if (!value.is_boolean()) return fail(key + ": true or false");
            if (!value.get<bool>()) continue;
            break;
        case Takes::Maybe:
            if (value.is_boolean()) {
                if (!value.get<bool>()) continue;
                break;
            }
            [[fallthrough]];
        case Takes::One: {
            std::string token;
            if (value.is_array()) return fail(key + ": one value, not a list");
            if (auto problem = text(value, flag.type, token); !problem.empty()) return fail(key + ": " + problem);
            item.values.push_back(std::move(token));
            break;
        }
        case Takes::Some:
            if (auto problem = tokens(value, flag.type, item.values); !problem.empty()) return fail(key + ": " + problem);
            break;
        case Takes::Rest:
            if (auto problem = text(value, flag.type, item.rest); !problem.empty()) return fail(key + ": " + problem);
            break;
        }
        args.flags.push_back(std::move(item));
    }
    // batch's calls stand in for its body.
    if (!args.calls.empty()) args.rest = "{}";
    if (auto problem = CheckArgs(verb, args); !problem.empty()) return fail(problem);
    return args;
}
}
