// reagentlive CLI, Unreal pack: the pack's rows of the verb table
// (syntax/verb.h). The core's rows and what reads them are in results/verbs.h.
#pragma once
#include <vector>
#include "reagentlive/packs/ue/cli/results/census.h"
#include "reagentlive/packs/ue/cli/results/result_json_ue.h"
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
            {{"--typed", Takes::Nothing, "bool", "", "a typed call with no inputs"},
             {"--dry", Takes::Nothing, "bool", "", "resolve the function and build the block; call nothing"}},
            Of<shape::Call>, parse::Call,
            "A raw block must match the function's size (params shows it); none sends a zeroed block when every "
            "parameter is an output. hex is the block after the call; a typed call adds outputs. kept: an output "
            "owns memory (a handle, a string), so the agent reuses the block and the next call releases the old values. "
            "--dry answers mode dry: hex is the block that would be sent, with exec and pure; show it when asking to call. "
            "A call that faults in the engine is refused as faulted: the target may be left in a bad state.", {}, true},
        {"call-pure", "ue", "call a const or pure function that keeps nothing (read-only); as call takes",
            {{"object", "object", One, "ref", object}, {"function", "[Class:]Function", One, "str", function},
             {"params", "hex params | Name=value", Any, "str", "the parameter block as hex pairs, or one Name=value per input"}},
            {{"--typed", Takes::Nothing, "bool", "", "a typed call with no inputs"},
             {"--dry", Takes::Nothing, "bool", "", "resolve the function and build the block; call nothing"}},
            Of<shape::Call>, parse::CallPure,
            "The agent refuses, before calling, a function that is neither const nor pure, or one with an output "
            "that owns memory (use call). Pure is the engine's flag, not a proof: a function can still change state."},
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
        {"layout-export", "ue", "the reflected fields classes declare, into a file (JSON lines) for layout-check",
            {{"file", "file", One, "path", path}, {"classes", "Class", Some, "str", "a class or struct name, per class (up to 256)"}},
            {}, Of<shape::LayoutExport>, parse::LayoutExport,
            "One line per field the class itself declares: owner, field, offset, size, type. A line may be written "
            "by hand or by a tool from an SDK. missing lists names that are no class or struct."},
        {"layout-check", "ue", "a file of expected fields against the live layout; only the mismatches",
            {{"file", "file", One, "path", path}}, {}, Of<shape::LayoutCheck>, parse::LayoutCheck,
            "Lines as layout-export writes them; offset as a number or \"0x..\", size and type optional. problem: "
            "no class, no field, offset, size or type; ok when every line holds. Rows stop at 200 (capped)."},
        {"entries", "ue", "one page of an array, set or map: elements, or key and value",
            {{"object", "object", One, "ref", object},
             {"field", "[Owner:]Field", One, "str", "the field, with its declaring class where the name is ambiguous"}},
            {{"--start", Takes::One, "int", "N", "a page's next_cursor"}},
            Of<shape::Entries>, parse::Entries, "Names and object pointers are resolved."},
        {"get", "ue", "named fields, following object pointers and structs (up to 16 paths)",
            {{"object", "object", One, "ref", object}, {"paths", "Field[.Field...]", Some, "str", "a field path, per value"}},
            {}, Of<shape::Get>, parse::Get,
            "Owner:Field where a name is ambiguous. The values are read back to back; same_tick says whether in one tick."},
        {"trace", "ue", "the first thing a line hits: actor, component, physical material, distance (read-only)",
            {{"start", "x,y,z|object[@Socket]", One, "str",
              "the start: a world point in cm, an object's location (actor, component, camera manager), or a socket on a mesh component"},
             {"end", "x,y,z|object[@Socket]", One, "str", "the end, as start takes"}},
            {{"--channel", Takes::One, "str", "visibility|camera|N", "the trace type query (default visibility; N: TraceTypeQuery N+1)"},
             {"--simple", Takes::Nothing, "bool", "", "simple collision instead of complex"},
             {"--ignore", Takes::One, "ref", "actor", "an actor the line passes through; also the world it runs in"}},
            Of<shape::Trace>, parse::Trace,
            "KismetSystemLibrary.LineTraceSingle on the game thread, decoded by GameplayStatics.BreakHitResult; it "
            "changes no state. The world comes from --ignore, else from an end's object: two points need --ignore. "
            "Distance and points are in cm; distance counts from start. A socket needs a mesh component (get <actor> Mesh)."},
        {"calls-start", "ue", "record a reflected function's calls, with their arguments, for a while",
            {{"target", "object|Class", One, "str", target}, {"function", "[Owner:]Function", One, "str", function},
             {"duration", "duration", One, "dur", "how long: seconds, or Nms, Ns, Nm (at most 60 s)"}},
            {{"--object", Takes::One, "ref", "ref", "only calls on this object"},
             {"--dry", Takes::Nothing, "bool", "", "resolve and show the pointer that would be swapped; write nothing"}},
            Of<shape::CallsStart>, parse::CallsStart,
            "Swaps the function's native pointer (UFunction::Func, heap data; no code, vtable or import) for a "
            "recording thunk that forwards every call, and puts it back at the end or on calls-stop (only while it "
            "still holds the thunk). Calls through ProcessEvent (C++ callers, RPCs, Blueprint events, delegates) "
            "carry their parameters after the call; Blueprint graphs calling native code show only the object "
            "(via vm); direct C++ calls are not seen. 256 rows per recording; older ones are dropped.", {}, true},
        {"calls-read", "ue", "a recording's calls so far, decoded by the function's parameters",
            {},
            {{"--slot", Takes::One, "int", "N", "the recording (default: the last started)"},
             {"--start", Takes::One, "int", "N", "next_cursor: only calls after it"}},
            Of<shape::Calls>, parse::CallsRead,
            "values: in, out and return by name, read after the call (inputs as the function left them). Objects "
            "are named while they live. A recording past its time is restored first."},
        {"calls-stop", "ue", "end a recording now (restores the pointer) and return its calls",
            {}, {{"--slot", Takes::One, "int", "N", "the recording (default: the last started)"}},
            Of<shape::Calls>, parse::CallsStop,
            "state replaced: something else swapped the pointer since; it was left alone and the thunk keeps forwarding."},
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
        {"funcs", "ue", "functions on the class chain, then on the native interfaces it implements",
            {{"target", "object|Class", One, "str", target}, {"text", "text", Optional, "str", "keep function names containing it"}},
            {{"--start", Takes::One, "int", "N", "a page's next_cursor"}}, Of<shape::Funcs>, parse::Funcs,
            "A row with via \"interface\" is declared only on that native interface: call refuses it on the object; func shows the implementation to call instead."},
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
