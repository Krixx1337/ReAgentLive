// reagentlive CLI, Unreal pack: the pack's rows of the verb table
// (syntax/verb.h). The core's rows and what reads them are in results/verbs.h.
#pragma once
#include <vector>
#include "reagentlive/frontends/cli/results/ue/census.h"
#include "reagentlive/frontends/cli/results/ue/result_json_ue.h"
#include "reagentlive/frontends/cli/syntax/parse_internal.h"

namespace reagentlive::cli {
// The pack's origin: its id on every row and the title headings show.
inline constexpr Origin UnrealOrigin{"ue", "Unreal pack"};

inline const std::vector<Verb>& UnrealVerbs() {
    using schema::Of;
    using enum Arity;
    constexpr std::string_view object = "an object: $N, a literal ref or an address";
    constexpr std::string_view target = "an object ($N, a literal ref, an address), or a class or struct by name";
    constexpr std::string_view function = "the function, with its declaring class where the name is ambiguous";
    constexpr std::string_view path = "a file path (quote one with spaces)";
    static const std::vector<Verb> verbs = {
        {"find", "ue", "objects whose name contains text and/or instances of a class; 24 per page",
            {{"text", "text", Any, "text", "part of the object's name"}},
            {{"--cls", Takes::One, "str", "Class", "instances of the class or a subclass"},
             {"--start", Takes::One, "int", "N", "a page's next_cursor"}},
            Of<shape::Find>, parse::Find, "The rows become $1..$N. Only the first page counts total."},
        {"census", "ue", "live instances per class, most first; pages of 100 rows",
            {},
            {{"--cls", Takes::One, "str", "Class", "only this class and its subclasses"},
             {"--match", Takes::One, "str", "text", "keep class names containing text"},
             {"--limit", Takes::One, "int", "N", "rows per page (0: all)", "100"},
             {"--start", Takes::One, "int", "N", "next_cursor: pages the stored census without a new scan"}},
            Of<shape::Census>, parse::Census, "Class refs become $1..$N. Every census becomes census-diff's baseline."},
        {"census-diff", "ue", "the classes whose count differs from the last census of that --cls",
            {},
            {{"--cls", Takes::One, "str", "Class", "only this class and its subclasses"},
             {"--match", Takes::One, "str", "text", "keep class names containing text"},
             {"--limit", Takes::One, "int", "N", "rows per page (0: all)", "100"},
             {"--start", Takes::One, "int", "N", "next_cursor"}},
            Of<shape::CensusDiff>, parse::CensusDiff,
            "Takes a new census, which becomes the next baseline. Class refs become $1..$N."},
        {"name-find", "ue", "FName indices whose text contains text (gameplay tags, row names)",
            {{"text", "text", One, "text", "part of the pooled name"}},
            {{"--start", Takes::One, "int", "N", "a page's next_cursor"}}, Of<shape::NameFind>, parse::NameFind},
        {"name", "ue", "FName comparison indices as text (FName #N in dumps)",
            {{"indices", "index", Some, "int", "an FName comparison index"}}, {}, Of<shape::Names>, parse::Name},
        {"each", "ue", "a table over the live instances of a class: a value per column (up to 16 field paths, or one function)",
            {{"cls", "Class", One, "str", "a class or struct name"},
             {"paths", "Field[.Field...]|[Owner:]Function", Some, "str",
              "a field path per column (structs and object pointers, as get takes), or one function to call"}},
            {{"--seconds", Takes::One, "int", "N", "one column: repeat for 1..60 s and report changes"}},
            Of<shape::Each>, parse::Each,
            "rows: {ref, values} in the columns' order; a failed cell is null with an errors entry. Spans inside the "
            "instance are read in passes of up to 64 bytes, the first in one tick; a path through a pointer is "
            "followed per instance (same_tick false). Functions must be const or pure with outputs only; a call's "
            "value is its return value, or {return, outputs}. The instances become $1..$N.", {}, true},
        {"call", "ue", "call a reflected function with a raw block or Name=value inputs",
            {{"object", "object", One, "ref", object}, {"function", "[Class:]Function", One, "str", function},
             {"params", "hex params | Name=value", Any, "str", "the parameter block as hex pairs, or one Name=value per input"}},
            {{"--typed", Takes::Nothing, "bool", "", "a typed call with no inputs"}},
            Of<shape::Call>, parse::Call,
            "A raw block must match the function's size (params shows it); none sends a zeroed block when every "
            "parameter is an output. hex is the block after the call; a typed call adds outputs. kept: an output "
            "owns memory (a handle, a string), so the agent reuses the block and the next call releases the old values.", {}, true},
        {"release", "ue", "one more call (zeroed inputs by default) that drops the kept block",
            {{"object", "object", One, "ref", object}, {"function", "[Class:]Function", One, "str", function},
             {"params", "hex params", Any, "bytes", "the parameter block as hex pairs (default: zeroed)"}},
            {}, Of<shape::Call>, parse::Release, {}, {}, true},
        {"fields", "ue", "reflected properties by offset; a class or struct lists its own",
            {{"target", "object|Class", One, "str", target}}, {}, Of<shape::Fields>, parse::Fields},
        {"dump", "ue", "fields with their current values",
            {{"target", "object|Class", One, "str", target}},
            {{"--match", Takes::One, "str", "text", "names containing text"},
             {"--nonzero", Takes::Nothing, "bool", "", "non-zero values"},
             {"--own", Takes::Nothing, "bool", "", "only the fields the object's own class declares, none it inherits"},
             {"--changed", Takes::Maybe, "path", "file", "changed since snap (or a snapshot file); adds before"}},
            Of<shape::Dump>, parse::Dump,
            "Use on instances, Default__ objects included. Check truncated on the result and its rows before "
            "treating values as complete. A dump can span several ticks."},
        {"label", "ue", "name an unreflected member in <console>.native.txt",
            {{"target", "object|Class", One, "str", target}, {"offset", "offset", One, "int", "the member's offset in the object"},
             {"type", "type[n]", One, "str", "its type, with a count for an array: f64[3]"},
             {"name", "Name", One, "str", "the member's name (a trailing ? marks it unconfirmed)"}},
            {}, Of<shape::NativeLabel>, parse::Label, "fields, dump and get then show it (native; stale after a patch)."},
        {"export-labels", "ue", "names for a static-analysis tool (JSON lines, one build)",
            {{"file", "file", One, "path", path}},
            {{"--module", Takes::One, "str", "name", "default: the exe"},
             {"--kinds", Takes::One, "str", "exec,vtable,interface,global", "which rows to derive (default: all)"}},
            Of<shape::ExportLabels>, parse::ExportLabels,
            "Native exec thunks, vtables, interface implementations, and the globals peek --label put in the "
            "file (always kept). After another tool's mappings: --kinds interface,global."},
        {"entries", "ue", "one page of an array, set or map: elements, or key and value",
            {{"object", "object", One, "ref", object},
             {"field", "[Owner:]Field", One, "str", "the field, with its declaring class where the name is ambiguous"}},
            {{"--start", Takes::One, "int", "N", "a page's next_cursor"}},
            Of<shape::Entries>, parse::Entries, "Names and object pointers are resolved."},
        {"get", "ue", "named fields, following object pointers and structs (up to 16 paths)",
            {{"object", "object", One, "ref", object}, {"paths", "Field[.Field...]", Some, "str", "a field path, per value"}},
            {}, Of<shape::Get>, parse::Get,
            "Owner:Field where a name is ambiguous. The values are read back to back; same_tick says whether in one tick."},
        {"describe", "ue", "an object's name, class and reusable ref",
            {{"object", "address|$N|#index:serial", One, "ref", "the object: an address, $N, a literal ref, or a table slot and serial"}},
            {}, Of<shape::Object>, parse::Describe},
        {"func", "ue", "a function's reflected metadata and exec entry address",
            {{"target", "object|Class", One, "str", target}, {"function", "[Owner:]Function", One, "str", function}},
            {}, Of<shape::Func>, parse::Func},
        {"script", "ue", "a Blueprint function's bytecode: the names, functions and properties it uses",
            {{"target", "object|Class", One, "str", target}, {"function", "[Owner:]Function", One, "str", function}},
            {}, Of<shape::Script>, parse::Script, "Each entry is read right after its opcode and confirmed live."},
        {"refs", "ue", "Blueprint functions whose bytecode names a pooled name (EX_NameConst)",
            {{"name", "name|#index", One, "str", "the exact pooled name, or its comparison index"}}, {}, Of<shape::Refs>, parse::Refs},
        {"funcs", "ue", "functions on the class chain",
            {{"target", "object|Class", One, "str", target}, {"text", "text", Optional, "str", "keep function names containing it"}},
            {{"--start", Takes::One, "int", "N", "a page's next_cursor"}}, Of<shape::Funcs>, parse::Funcs},
        {"params", "ue", "a function's reflected parameter signature",
            {{"target", "object|Class", One, "str", target}, {"function", "[Owner:]Function", One, "str", function}},
            {}, Of<shape::Params>, parse::Params},
        {"snap", "ue", "save a baseline for dump --changed (file: across runs)",
            {{"object", "ref", One, "ref", "an object ref: $N or 0x<address>#<index>:<serial>"}, {"file", "file", Optional, "path", path}},
            {}, Of<shape::Snap>, parse::Snap},
        {"record", "ue", "sample 1..8 named fields for 1..10 seconds into a new file, after a countdown",
            {{"object", "ref", One, "ref", "an object ref: $N or 0x<address>#<index>:<serial>"},
             {"seconds", "seconds", One, "int", "how long, 1..10"}, {"file", "file", One, "path", path},
             {"fields", "field", Some, "str", "an exact field name, per column"}},
            {}, Of<shape::Record>, parse::Record, {}, {}, false, true},
    };
    return verbs;
}

// What applies to every verb of the pack; `help` prints these under the rows.
inline const std::vector<std::string>& UnrealNotes() {
    static const std::vector<std::string> notes = {
        "Unreal pack verbs are refused when the host does not offer the pack (status lists the packs).",
        "Refs: $N from the last find, or the printed literal 0x<address>#<index>:<serial> (works across runs).",
        "A bare class or struct name works where object|Class is shown; the first lookup scans the table.",
        "Struct values print as {Name: value}; --meta at the end of a line adds their member layout.",
    };
    return notes;
}
}
