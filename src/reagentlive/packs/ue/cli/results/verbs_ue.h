// reagentlive CLI, Unreal pack: the pack's rows of the verb table
// (call/verb.h). The core's rows and what reads them are in results/verbs.h.
#pragma once
#include <vector>
#include "reagentlive/packs/ue/cli/results/census.h"
#include "reagentlive/packs/ue/cli/results/result_ue_objects.h"
#include "reagentlive/packs/ue/cli/results/result_ue_fields.h"
#include "reagentlive/packs/ue/cli/results/result_ue_calls.h"
#include "reagentlive/packs/ue/cli/results/result_ue_code.h"
#include "reagentlive/frontends/cli/call/build_internal.h"

namespace reagentlive::cli {
// The bounds the builders enforce, as the arg help below states them.
static_assert(ue::MaxCallsSlots == 8 && MaxRead / 4 == 1024);
// The pack's origin: its id on every row and the title headings show.
inline constexpr Origin UnrealOrigin{"ue", "Unreal pack"};

inline const std::vector<Verb>& UnrealVerbs() {
    using schema::Of;
    constexpr std::string_view object = "an object: ref or \"0x...\"";
    constexpr std::string_view objectOrDefault = "an object: ref, \"0x...\" or Default__ name";
    constexpr std::string_view objectOrClass = "an object (ref or \"0x...\"), or a class or struct by name";
    constexpr std::string_view function = "the function, as Owner:Function where the name is ambiguous";
    constexpr std::string_view ref = "\"0x1f2e3d4c50#123:4\""; // An example: a type's own ("Name") is no object.
    // Args several verbs take, written once so their help cannot drift apart.
    static const Arg cls{"cls", "str", "instances of the class or a subclass"};
    static const Arg outer{"outer", "str",
        "objects under this outer: a ref or \"0x...\", or an outer's name text"};
    static const Arg params{"params", "params",
        "inputs by name (typed call; {} for none): struct as a list in member order or by member name, object as "
        "ref, name as text or \"#index[:number]\", null for None"};
    static const Arg block{"block", "bytes", "the raw parameter block as hex pairs (none: zeroed)"};
    static const std::vector<Verb> verbs = {
        {"find", "ue", ToolPack::Explore, "objects by name text and/or class; 24 per page, or limit",
            {{"text", "text", "part of the object's name, any case", {}, false, false, "\"Player\""},
             cls,
             {"live", "bool", "live instances only: no Default__ or archetype objects"},
             outer,
             View("start limit")},
            Of<shape::Find>, build::Find, "Give text, cls or outer. Only the first page counts total. view.limit 1..1000; next_cursor continues."},
        {"census", "ue", ToolPack::Observe, "live instances per class, most first; 100 rows a page; diff: only changed counts",
            {cls,
             outer,
             {"diff", "bool", "only the classes whose count differs from the last census of that cls and outer"},
             View("match start limit", "100")},
            Of<shape::Census>, build::Census,
            "view.start (next_cursor) pages the stored census without a rescan. Every census is the next diff's "
            "baseline; diff is refused with no earlier census under the same filter."},
        {"name-find", "ue", ToolPack::Explore, "FName indices whose text contains text (gameplay tags, row names)",
            {{"text", "text", "part of the pooled name", {}, true}, View("start")}, Of<shape::NameFind>, build::NameFind},
        {"name", "ue", ToolPack::Explore, "FName comparison indices as text (FName #N in dumps)",
            {{"indices", "int", "FName comparison indices (up to 1024)", {}, true, true}}, Of<shape::Names>, build::Name,
            "No name where the index is not an entry start or does not decode."},
        {"each", "ue", ToolPack::Explore, "a table over a class's live instances: a value per column (up to 16 paths)",
            {{"cls", "str", "a class or struct name", {}, true},
             {"paths", "path", "a field path per column, as get takes", {}, true, true},
             {"duration", "int", "one column: repeat for 1..60 s and report changes"},
             {"where", "filter", "only instances whose field's value contains the text, any case (JSON text if not a string or "
              "name); Outer, Name and Class match the object's own"},
             outer,
             View("limit nonzero elements", "100")},
            Of<shape::Each>, build::Each,
            "Read-only: a function is refused (call-each calls one). A path through a pointer is followed per "
            "instance (same_tick false). A failed cell is null with an errors entry."},
        {"call", "ue", ToolPack::Modify, "call a reflected function with inputs by name (typed) or a raw block",
            {{"object", "str", objectOrDefault, {}, true, false, ref}, {"function", "str", function, {}, true},
             params, block,
             {"dry", "bool", "resolve the function and build the block; call nothing"},
             View("fields start limit nonzero", "100")},
            Of<shape::Call>, build::Call,
            "A raw block must equal the function's size (func: block_size). dry: show its block when asking to call. "
            "A fault in the engine answers faulted: treat the target as suspect.", true},
        {"call-pure", "ue", ToolPack::Observe, "call a const or pure function that keeps nothing; read-only; args as call",
            {{"object", "str", objectOrDefault, {}, true, false, ref},
             {"function", "str", function, {}, true},
             params, block,
             {"dry", "bool", "resolve the function and build the block; call nothing"}},
            Of<shape::Call>, build::CallPure,
            "Refused before the call: a function neither const nor pure, or with an output that owns memory (use "
            "call). Pure is the engine's flag, not a proof. No view. Inputs are never zeroed: a function with "
            "inputs needs params."},
        {"call-each", "ue", ToolPack::Observe, "call a const or pure function with no inputs on every live instance of a class, in one tick",
            {{"cls", "str", "a class name: its live instances and its subclasses'", {}, true},
             {"function", "str", function, {}, true},
             {"duration", "int", "repeat for 1..60 s and report changes"},
             outer,
             View("limit nonzero", "100")},
            Of<shape::Each>, build::EachCall,
            "call-pure's checks per instance. The function takes no inputs; its block is 1..64 bytes."},
        {"release", "ue", ToolPack::Modify, "one more call (zeroed inputs by default) that drops the kept block",
            {{"object", "str", objectOrDefault, {}, true, false, ref}, {"function", "str", function, {}, true}, block},
            Of<shape::Call>, build::Release, {}, true},
        {"member-name", "ue", ToolPack::Native, "name an unreflected member in <console>.native.txt",
            {{"object", "str", objectOrClass, {}, true, false, ref}, {"offset", "int", "the member's offset in the object", {}, true},
             {"type", "str", "its type, with a count for an array: f64[3]", {}, true, false, "\"f32\""},
             {"name", "str", "the member's name (a trailing ? marks it unconfirmed)", {}, true}},
            Of<shape::NativeLabel>, build::MemberName, "get then shows it (native; stale after a patch)."},
        {"labels-export", "ue", ToolPack::Native, "names for a static tool (JSON lines, one build)",
            {{"file", "file", "the labels file (default: the console's, <console>.labels.jsonl)"},
             {"module", "str", "the module (default: the exe)"},
             {"kinds", "exec|vtable|interface|global", "which rows to derive (default: all)", {}, false, true}},
            Of<shape::ExportLabels>, build::LabelsExport,
            "Merged into the file: names written back by a static tool and globals confirmed by peek's label are "
            "kept, as are other builds' lines and kinds not derived now. global derives nothing."},
        {"assets-export", "ue", ToolPack::Native, "each live object of the classes into its own JSON file: nonzero fields, table rows",
            {{"dir", "file", "the folder to write (created when missing)", {}, true},
             {"classes", "str", "the classes (and their subclasses) to export, up to 16", {}, true, true},
             View("match")},
            Of<shape::ExportAssets>, build::AssetsExport,
            "Up to 2000 objects per call (capped). Files <dir>/<Class>/<name>.json and <dir>/index.json; files are "
            "replaced on rerun."},
        {"layout-export", "ue", ToolPack::Native, "reflected class, struct and enum layouts to a JSON-lines file",
            {{"file", "file", "the file to write", {}, true},
             {"classes", "str", "class, struct or enum names (up to 256)", {}, true, true},
             {"closure", "int", "also the types their fields reference, to this depth (1..8), and each one's super chain"}},
            Of<shape::LayoutExport>, build::LayoutExport,
            "missing lists names that are no class, struct or enum; capped: closure stopped at 1024 types."},
        {"layout-check", "ue", ToolPack::Native, "a file of expected fields against the live layout; only the mismatches",
            {{"file", "file", "the file layout-export wrote", {}, true}}, Of<shape::LayoutCheck>, build::LayoutCheck,
            "Lines as layout-export writes them; offset a number or \"0x..\"; size and type optional. Rows stop at 200 (capped)."},
        {"entries", "ue", ToolPack::Explore, "one page of an array, set or map: elements, or key and value",
            {{"object", "str", objectOrDefault, {}, true, false, ref},
             {"field", "str", "the field, as Owner:Field where the name is ambiguous", {}, true},
             View("start")},
            Of<shape::Entries>, build::Entries, "Elements and slots up to 512 bytes; a bigger one is refused: get the field."},
        {"rows", "ue", ToolPack::Explore, "a data table's rows decoded by RowStruct, or a curve table's curves and keys",
            {{"object", "str", objectOrDefault, {}, true, false, ref},
             {"at", "num", "a curve table: each row's value at this time"},
             View("match fields start limit nonzero", "100")},
            Of<shape::Rows>, build::Rows,
            "view.match keeps row names. Nested arrays up to 256 elements, 3 deep. A CurveTable gives per row keys "
            "([t, v]; rich: [t, v, interp, arrive, leave]); at adds the value at that time."},
        {"get", "ue", ToolPack::Explore, "an object's fields with values, or named paths; types: the layout",
            {{"object", "str", "an object (ref, \"0x...\", Default__ name), or with types a class or struct by name", {}, true, false, ref},
             {"paths", "path", "a field path per value (up to 16); none: every field", {}, false, true},
             {"mode", "values|types|snap|changed", "values; types: layout, no values (class or struct: its own fields); "
              "snap: keep bytes as baseline; changed: fields changed since snap, with before", "\"values\""},
             {"own", "bool", "only the fields the object's own class declares, none it inherits"},
             {"file", "file", "with snap or changed: the snapshot file (across runs)"},
             View("match nonzero elements")},
            Of<shape::Get>, build::Get,
            "No paths: every field, compact; check truncated before treating values as complete. Paths: one row per "
            "path (path, value, address), Owner:Field where ambiguous; a bad path fails its row naming the closest "
            "fields. snap answers only {size, path}. view.match, own and the modes but values take no paths; types "
            "reads no values."},
        {"line-trace", "ue", ToolPack::Observe, "the first thing a line hits: actor, component, material, distance (read-only)",
            {{"start", "point",
              "a world point [x, y, z] in cm, an object's location (ref), or {\"object\": <ref>, \"socket\": \"Name\"} on a mesh", {}, true},
             {"end", "point", "the end, as start takes", {}, true},
             {"channel", "int", "the trace type query (0..31): 0 visibility, 1 camera, N: TraceTypeQuery N+1", "0"},
             {"simple", "bool", "simple collision instead of complex"},
             {"ignore", "ref", "an actor the line passes through; also the world it runs in"}},
            Of<shape::Trace>, build::LineTrace,
            "Runs on the game thread; changes no state. Two points need ignore (the world comes from ignore, else "
            "an end's object). cm; distance from start. A socket needs a mesh component."},
        {"calls-start", "ue", ToolPack::Modify, "record a reflected function's calls, with their arguments, for a while",
            {{"object", "str", "whose function: an instance (ref or \"0x...\": records only its calls) or a class (ref, \"0x...\" or name: every instance)", {}, true, false, ref},
             {"function", "str", function, {}, true},
             {"duration", "dur", "how long, in seconds (at most 60)", {}, true},
             {"dry", "bool", "resolve and show the pointer that would be swapped; write nothing"}},
            Of<shape::CallsStart>, build::CallsStart,
            "Swaps the function's native pointer (UFunction::Func, heap data) for a forwarding thunk; restores it at "
            "the end or on calls-read with stop. Sees ProcessEvent calls (C++, RPCs, Blueprint events, delegates) with "
            "parameters; Blueprint-to-native calls show only the object; direct C++ calls are not seen. 256 rows "
            "per recording.", true},
        {"calls-read", "ue", ToolPack::Modify, "a recording's calls so far, decoded by the function's parameters; stop ends it first",
            {{"slot", "int", "the recording, 0..7 (default: the last started)"},
             {"stop", "bool", "end the recording now (restores the pointer) and return its calls"},
             View("start")},
            Of<shape::Calls>, build::CallsRead,
            "values: in, out and return by name, read after the call. Objects named while they live; strings and "
            "containers print headers only. A recording past its time is restored first. state replaced: something "
            "else swapped the pointer since; it was left alone and the thunk keeps forwarding."},
        {"describe", "ue", ToolPack::Explore, "an object's name, class and reusable ref",
            {{"object", "ref", "the object: ref or \"0x...\"", {}, false, false, ref},
             {"slot", "int", "in place of object: an object-table slot (a weak pointer's), with serial"},
             {"serial", "int", "with slot: the slot's serial number"}},
            Of<shape::Object>, build::Describe, "Give object, or slot and serial."},
        {"func", "ue", ToolPack::Explore, "a function's reflected metadata, exec entry address and parameter signature",
            {{"object", "str", objectOrClass, {}, true, false, ref}, {"function", "str", function, {}, true}},
            Of<shape::Func>, build::Func,
            "params in block order (direction in, out, in ref, in/out ref, return); block_size is the call block's size. "
            "Without the game thread, params_error says why."},
        {"script", "ue", ToolPack::Explore, "a Blueprint function's bytecode: names, functions, properties it uses; decode: as statements",
            {{"object", "str", "an object or class (ref, \"0x...\", class name), or the Function itself (ref, as name-refs gives it)", {}, true, false, ref},
             {"function", "str", "the function's name (Owner:Name to pick one; none when object is the Function)"},
             {"decode", "bool", "read the bytecode as statements with their offsets"},
             {"entry", "int", "with decode: one event from this offset to its return, pop or goto"},
             {"to", "int", "with decode: statements before this offset; with entry, to here, past returns"},
             View("match")},
            Of<shape::Script>, build::Script,
            "decode reads every expression (no game thread) and stops at an unknown opcode (stopped); an uber graph "
            "adds events. view.match keeps statements whose text or op contains it; entry and to bound an offset "
            "range. A Function ref (name-refs' rows) as object needs no function."},
        {"enum", "ue", ToolPack::Explore, "an enum's names and values, in order",
            {{"enum", "str", "an enum object (ref or \"0x...\"), or an enum by name", {}, true}},
            Of<shape::Enum>, build::Enum,
            "Names keep the enum's prefix (EFoo::Bar)."},
        {"game-thread", "ue", ToolPack::Modify, "arm or remove the host's game-thread tick (status shows it)",
            {{"action", "on|off", "on arms it, off puts the viewport's table back", {}, true}},
            Of<shape::GameThread>, build::GameThread,
            "Needed by call, call-pure, call-each, release, func's params, each, script, refs, owner, route, exec "
            "entries and the recorder. on swaps one pointer in the viewport object for a copy of its Draw table; "
            "confirm Draw first (skill: references/unreal/game-thread.md). Unsupported when the host does not link "
            "reagentlive::ue-draw.", true},
        {"name-refs", "ue", ToolPack::Explore, "Blueprint functions whose bytecode names a pooled name (EX_NameConst)",
            {{"name", "str", "the exact pooled name", {}, false, false, "\"None\""},
             {"name_index", "int", "in place of name: its comparison index"}},
            Of<shape::Refs>, build::NameRefs, "Give name or name_index."},
        {"owner", "ue", ToolPack::Explore, "the object holding each address: the field, the Outer pointer or the container element",
            {{"addrs", "addr", "addresses, as search rows give them (up to 256)", {}, true, true},
             {"within", "ref", "objects whose arrays, sets and maps to check for an address no object holds (up to 16)",
              {}, false, true}},
            Of<shape::Owner>, build::Owner,
            "One object-table scan per 64 addresses. An address held by heap storage has no owner: the containers of "
            "the within objects and of other rows' objects are checked (container). Needs the game thread."},
        {"route", "ue", ToolPack::Explore, "the shortest routes of reflected references from a root or object to an object",
            {{"object", "ref", "the object to reach", {}, true},
             {"sources", "str", "up to 4: host roots by name (status lists them) or objects; default: every root", {}, false, true, "[\"pawn\"]"},
             {"depth", "int", "hops at most (1..16)", "8"},
             {"budget", "int", "objects visited at most (1..1048576)", "20000"},
             {"up", "bool", "also step from each object up to its Outer (child to parent only)"}},
            Of<shape::Route>, build::Route,
            "Breadth-first over object fields, arrays, sets and map values; no class, weak, soft or lazy pointers, no "
            "containers inside container elements. Up to 4 shortest routes; end says why none: depth, budget or none. "
            "Objects can die mid-walk: verify a route with get (entries for indexed steps). Needs the game thread."},
        {"funcs", "ue", ToolPack::Explore, "functions on the class chain, then on the native interfaces it implements",
            {{"object", "str", objectOrClass, {}, true, false, ref}, View("match start")},
            Of<shape::Funcs>, build::Funcs,
            "A row with via \"interface\" exists only on that interface: call refuses it; use func's implementation."},
    };
    return verbs;
}

// What applies to every verb of the pack; `help` prints these under the rows.
inline const std::vector<std::string>& UnrealNotes() {
    static const std::vector<std::string> notes = {
        "Refs: pass the literal 0x<address>#<index>:<serial> a result printed; it works across runs.",
        "A bare class or struct name works where an arg says so.",
    };
    return notes;
}
}
