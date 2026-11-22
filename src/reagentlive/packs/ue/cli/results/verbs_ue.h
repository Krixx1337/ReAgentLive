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
    constexpr std::string_view objectOrClass = "an object ($N, a literal ref, an address), or a class or struct by name";
    constexpr std::string_view function = "the function, with its declaring class where the name is ambiguous";
    constexpr std::string_view path = "a file path (quote one with spaces)";
    // Flags several verbs take, written once so their help cannot drift apart.
    static const Flag cls{"--cls", Takes::One, "str", "Class", "instances of the class or a subclass"};
    static const Flag outer{"--outer", Takes::One, "str", "object|text",
        "objects with this object up their outer chain (a ref, address or $N), or an outer whose name contains text"};
    static const Flag nonzero{"--nonzero", Takes::Nothing, "bool", "", "leave out zero members inside values (0, false, empty, None)"};
    static const Flag elements{"--elements", Takes::One, "int", "N", "elements shown per array, set or map (1..256)", "8"};
    static const Flag cursor{"--start", Takes::One, "int", "N", "a page's next_cursor"};
    static const Flag pageLimit{"--limit", Takes::One, "int", "N", "rows per page (0: all)", "100"};
    static const Flag classMatch{"--match", Takes::One, "str", "text", "keep class names containing text, any case"};
    static const Flag fieldMatch{"--match", Takes::One, "str", "text", "field names or readable labels containing text, any case"};
    static const std::vector<Verb> verbs = {
        {"find", "ue", "objects whose name contains text and/or instances of a class; 24 per page",
            {{"text", "text", Any, "text", "part of the object's name, any case"}},
            {cls,
             {"--live", Takes::Nothing, "bool", "", "live instances only: no Default__ or archetype objects"},
             outer,
             cursor},
            Of<shape::Find>, parse::Find, "The rows become $1..$N. Only the first page counts total."},
        {"census", "ue", "live instances per class, most first; pages of 100 rows",
            {},
            {cls,
             outer,
             classMatch,
             pageLimit,
             {"--start", Takes::One, "int", "N", "next_cursor: pages the stored census without a new scan"}},
            Of<shape::Census>, parse::Census, "Class refs become $1..$N. Every census becomes census-diff's baseline."},
        {"census-diff", "ue", "the classes whose count differs from the last census of that --cls",
            {},
            {cls,
             outer,
             classMatch,
             pageLimit,
             cursor},
            Of<shape::CensusDiff>, parse::CensusDiff,
            "Takes a new census, which becomes the next baseline. Class refs become $1..$N."},
        {"name-find", "ue", "FName indices whose text contains text (gameplay tags, row names)",
            {{"text", "text", One, "text", "part of the pooled name"}},
            {cursor}, Of<shape::NameFind>, parse::NameFind},
        {"name", "ue", "FName comparison indices as text (FName #N in dumps)",
            {{"indices", "index", Some, "int", "an FName comparison index"}}, {}, Of<shape::Names>, parse::Name},
        {"each", "ue", "a table over the live instances of a class: a value per column (up to 16 field paths, or one function)",
            {{"cls", "Class", One, "str", "a class or struct name"},
             {"paths", "Field[.Field...]|[Owner:]Function", Some, "str",
              "a field path per column (structs and object pointers, as get takes), or one function to call"}},
            {{"--seconds", Takes::One, "int", "N", "one column: repeat for 1..60 s and report changes"},
             {"--where", Takes::One, "str", "Field~text", "only instances whose field's value contains text, any case "
              "(a string, a name or text; else its JSON); the field need not be a column; Outer, Name and Class "
              "match the object's own (Class~ is the class's name: --cls takes subclasses)"},
             outer,
             {"--limit", Takes::One, "int", "N", "rows shown (0: all); omitted counts the rest", "100"},
             nonzero,
             elements},
            Of<shape::Each>, parse::Each,
            "rows: {ref, values} in the columns' order; a failed cell is null with an errors entry. Spans inside the "
            "instance are read in passes of up to 64 bytes, the first in one tick; a path through a pointer is "
            "followed per instance (same_tick false). Functions must be const or pure with outputs only; a call's "
            "value is its return value, or {return, outputs}. The instances become $1..$N.", {}, true},
        {"call", "ue", "call a reflected function with a raw block or Name=value inputs",
            {{"object", "object", One, "ref", object}, {"function", "[Class:]Function", One, "str", function},
             {"params", "hex params | Name=value", Any, "str", "the parameter block as hex pairs, or one Name=value per input"}},
            {{"--typed", Takes::Nothing, "bool", "", "a typed call with no inputs"},
             {"--dry", Takes::Nothing, "bool", "", "resolve the function and build the block; call nothing"},
             {"--fields", Takes::One, "str", "a,b.c", "a returned array's row members to keep (implies a typed call)"},
             {"--start", Takes::One, "int", "N", "a returned array's first row: its next_cursor"},
             {"--limit", Takes::One, "int", "N", "a returned array's rows per page (0: all)", "100"},
             {"--nonzero", Takes::Nothing, "bool", "", "as get takes it, inside the outputs; implies a typed call"}},
            Of<shape::Call>, parse::Call,
            "A raw block must match the function's size (params shows it); none sends a zeroed block when every "
            "parameter is an output. hex is the block after the call; a typed call adds outputs, a returned array "
            "as {count, rows, next_cursor} with each element decoded from its struct (nested arrays up to 256 "
            "elements, 3 deep; names resolved). kept: an output "
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
            {{"object", "object|Class", One, "str", objectOrClass}},
            {fieldMatch},
            Of<shape::Fields>, parse::Fields},
        {"dump", "ue", "fields with their current values",
            {{"object", "object|Class", One, "str", objectOrClass}},
            {fieldMatch,
             {"--nonzero", Takes::Nothing, "bool", "", "non-zero fields, without the zero members inside their values"},
             {"--own", Takes::Nothing, "bool", "", "only the fields the object's own class declares, none it inherits"},
             {"--changed", Takes::Maybe, "path", "file", "changed since snap (or a snapshot file); adds before"},
             elements},
            Of<shape::Dump>, parse::Dump,
            "Use on instances, Default__ objects included. Check truncated on the result and its rows before "
            "treating values as complete. A dump can span several ticks."},
        {"label", "ue", "name an unreflected member in <console>.native.txt",
            {{"object", "object|Class", One, "str", objectOrClass}, {"offset", "offset", One, "int", "the member's offset in the object"},
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
            {cursor},
            Of<shape::Entries>, parse::Entries, "Names and object pointers are resolved."},
        {"rows", "ue", "a data table's rows: each row's name and its RowStruct members decoded",
            {{"object", "object", One, "ref", object}},
            {{"--match", Takes::One, "str", "text", "row names containing text, any case"},
             {"--fields", Takes::One, "str", "a,b.c", "the row members to keep (paths into structs and arrays)"},
             cursor,
             pageLimit,
             nonzero},
            Of<shape::Rows>, parse::Rows,
            "RowMap is unreflected: read as the stock UE5 TMap<FName, uint8*> right after RowStruct, its slot "
            "count checked against its allocation bits (refused when they disagree). Nested arrays up to 256 "
            "elements, 3 deep; names resolved. total counts the rows --match kept."},
        {"get", "ue", "named fields, following object pointers and structs (up to 16 paths)",
            {{"object", "object", One, "ref", object}, {"paths", "Field[.Field...]", Some, "str", "a field path, per value"}},
            {nonzero,
             elements}, Of<shape::Get>, parse::Get,
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
            {{"owner", "object|Class", One, "str", "whose function: an object or a class by name (--object narrows to one object)"}, {"function", "[Owner:]Function", One, "str", function},
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
            "are named while they live; strings, arrays, sets and maps print their headers (the caller may have "
            "freed them). A recording past its time is restored first."},
        {"calls-stop", "ue", "end a recording now (restores the pointer) and return its calls",
            {}, {{"--slot", Takes::One, "int", "N", "the recording (default: the last started)"}},
            Of<shape::Calls>, parse::CallsStop,
            "state replaced: something else swapped the pointer since; it was left alone and the thunk keeps forwarding."},
        {"describe", "ue", "an object's name, class and reusable ref",
            {{"object", "address|$N|#index:serial", One, "ref", "the object: an address, $N, a literal ref, or a table slot and serial"}},
            {}, Of<shape::Object>, parse::Describe},
        {"func", "ue", "a function's reflected metadata and exec entry address",
            {{"object", "object|Class", One, "str", objectOrClass}, {"function", "[Owner:]Function", One, "str", function}},
            {}, Of<shape::Func>, parse::Func},
        {"script", "ue", "a Blueprint function's bytecode: the names, functions and properties it uses",
            {{"object", "object|Class", One, "str", objectOrClass}, {"function", "[Owner:]Function", One, "str", function}},
            {}, Of<shape::Script>, parse::Script, "Each entry is read right after its opcode and confirmed live."},
        {"enum", "ue", "an enum's names and values, in order",
            {{"enum", "object|Enum", One, "str", "an enum object ($N, a literal ref, an address), or an enum by name"}},
            {}, Of<shape::Enum>, parse::Enum,
            "Byte and enum properties print numbers; this names them. Names keep the enum's prefix (EFoo::Bar)."},
        {"game-thread", "ue", "the host's game-thread tick through the game viewport's Draw: status, on, off",
            {{"action", "on|off|status", Optional, "str", "on arms it, off puts the viewport's table back", "status"}},
            {}, Of<shape::GameThread>, parse::GameThread,
            "Calls, each with functions, script, refs and the recorder need the game thread. on swaps one pointer in "
            "the viewport object for a copy of its Draw table: confirm Draw first (the skill's new-game page). "
            "Agents whose host does not link reagentlive::ue-draw answer unsupported.",
            {}, true},
        {"refs", "ue", "Blueprint functions whose bytecode names a pooled name (EX_NameConst)",
            {{"name", "name|#index", One, "str", "the exact pooled name, or its comparison index"}}, {}, Of<shape::Refs>, parse::Refs},
        {"funcs", "ue", "functions on the class chain, then on the native interfaces it implements",
            {{"object", "object|Class", One, "str", objectOrClass}, {"text", "text", Optional, "str", "keep function names containing it"}},
            {cursor}, Of<shape::Funcs>, parse::Funcs,
            "A row with via \"interface\" is declared only on that native interface: call refuses it on the object; func shows the implementation to call instead."},
        {"params", "ue", "a function's reflected parameter signature",
            {{"object", "object|Class", One, "str", objectOrClass}, {"function", "[Owner:]Function", One, "str", function}},
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
