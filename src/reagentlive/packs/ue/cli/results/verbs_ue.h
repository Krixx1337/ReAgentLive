// reagentlive CLI, Unreal pack: the pack's rows of the verb table
// (call/verb.h). The core's rows and what reads them are in results/verbs.h.
#pragma once
#include <vector>
#include "reagentlive/packs/ue/cli/results/census.h"
#include "reagentlive/packs/ue/cli/results/result_json_ue.h"
#include "reagentlive/frontends/cli/call/build_internal.h"

namespace reagentlive::cli {
// The pack's origin: its id on every row and the title headings show.
inline constexpr Origin UnrealOrigin{"ue", "Unreal pack"};

inline const std::vector<Verb>& UnrealVerbs() {
    using schema::Of;
    constexpr std::string_view object = "an object: a literal ref or \"0x...\"";
    constexpr std::string_view objectOrDefault = "an object: a literal ref, \"0x...\" or a Default__ name";
    constexpr std::string_view objectOrClass = "an object (a literal ref, \"0x...\"), or a class or struct by name";
    constexpr std::string_view function = "the function, as Owner:Function where the name is ambiguous";
    constexpr std::string_view ref = "\"0x1f2e3d4c50#123:4\""; // An example: a type's own ("Name") is no object.
    // Args several verbs take, written once so their help cannot drift apart.
    static const Arg cls{"cls", "str", "instances of the class or a subclass"};
    static const Arg outer{"outer", "str",
        "objects with this object up their outer chain (a literal ref or \"0x...\"), or an outer whose name contains the text"};
    static const Arg nonzero{"nonzero", "bool", "leave out zero members inside values (0, false, empty, None)"};
    static const Arg elements{"elements", "int", "elements shown per array, set or map (1..256)", "8"};
    static const Arg cursor{"start", "int", "a page's next_cursor"};
    static const Arg pageLimit{"limit", "int", "rows per page (0: all)", "100"};
    static const Arg classMatch{"match", "str", "keep class names containing the text, any case"};
    static const Arg fieldMatch{"match", "str", "field names or readable labels containing the text, any case"};
    static const Arg params{"params", "params",
        "the inputs by name, which makes a typed call ({} for none): a struct as a list in member order or an object "
        "by member name, an object as its ref, a name as its text (or \"#index[:number]\"), null for None"};
    static const Arg block{"block", "bytes", "the raw parameter block as hex pairs (none: zeroed)"};
    static const std::vector<Verb> verbs = {
        {"find", "ue", ToolPack::Explore, "objects whose name contains text and/or instances of a class; 24 per page, or limit",
            {{"text", "text", "part of the object's name, any case", {}, false, false, "\"Player\""},
             cls,
             {"live", "bool", "live instances only: no Default__ or archetype objects"},
             outer,
             cursor,
             {"limit", "int", "rows at most (1..1000), over as many pages as it takes"}},
            Of<shape::Find>, build::Find, "Give text, cls or outer. Only the first page counts total. limit pages "
            "until that many rows or the end; next_cursor continues after the last row. cls uses the agent's class "
            "index; text alone walks the whole object table, so add cls when the class is known."},
        {"census", "ue", ToolPack::Observe, "live instances per class, most first; pages of 100 rows; diff: only the counts that changed",
            {cls,
             outer,
             classMatch,
             pageLimit,
             {"start", "int", "next_cursor: pages the stored census without a new scan"},
             {"diff", "bool", "only the classes whose count differs from the last census of that cls and outer"}},
            Of<shape::Census>, build::Census,
            "Every census becomes the next diff's baseline. diff takes a new census and answers classes and "
            "instances as {before, after}, rows with before, after and change; refused with no earlier census under "
            "the same filter."},
        {"name-find", "ue", ToolPack::Explore, "FName indices whose text contains text (gameplay tags, row names)",
            {{"text", "text", "part of the pooled name", {}, true}, cursor}, Of<shape::NameFind>, build::NameFind},
        {"name", "ue", ToolPack::Explore, "FName comparison indices as text (FName #N in dumps)",
            {{"indices", "int", "FName comparison indices", {}, true, true}}, Of<shape::Names>, build::Name,
            "A row has no name when the index is not where a pool entry starts (inside an entry) or does not decode."},
        {"each", "ue", ToolPack::Explore, "a table over the live instances of a class: a value per column (up to 16 field paths)",
            {{"cls", "str", "a class or struct name", {}, true},
             {"paths", "path", "a field path per column (structs and object pointers, as get takes)", {}, true, true},
             {"seconds", "int", "one column: repeat for 1..60 s and report changes"},
             {"where", "filter", "only instances whose field's value contains the text, any case (a string, a name or "
              "text; else its JSON); the field need not be a column; Outer, Name and Class match the object's own "
              "(Class is the class's name: cls takes subclasses)"},
             outer,
             {"limit", "int", "rows shown (0: all); omitted counts the rest", "100"},
             nonzero,
             elements},
            Of<shape::Each>, build::Each,
            "rows: {ref, name, values} in the columns' order (name: the instance's); a failed cell is null with an errors entry. Spans inside the "
            "instance are read in passes of up to 64 bytes, the first in one tick; a path through a pointer is "
            "followed per instance (same_tick false). Reads only: a function is refused, call-pure with each calls "
            "one on every instance."},
        {"call", "ue", ToolPack::Modify, "call a reflected function with inputs by name (typed) or a raw block",
            {{"object", "str", objectOrDefault, {}, true, false, ref}, {"function", "str", function, {}, true},
             params, block,
             {"dry", "bool", "resolve the function and build the block; call nothing"},
             {"fields", "path", "a returned array's row members to keep (a typed call)", {}, false, true},
             {"start", "int", "a returned array's first row: its next_cursor"},
             {"limit", "int", "a returned array's rows per page (0: all)", "100"},
             {"nonzero", "bool", "as get takes it, inside the outputs (a typed call)"}},
            Of<shape::Call>, build::Call,
            "A raw block must match the function's size (func shows it as block_size); none sends a zeroed block when every "
            "parameter is an output. hex is the block after the call; a typed call adds outputs, a returned array "
            "as {count, rows, next_cursor} with each element decoded from its struct (nested arrays up to 256 "
            "elements, 3 deep; names resolved). fields, start, limit and nonzero make the call typed. kept: an output "
            "owns memory (a handle, a string), so the agent reuses the block and the next call releases the old values. "
            "dry answers mode dry: hex is the block that would be sent, with exec and pure; show it when asking to call. "
            "A call that faults in the engine is refused as faulted: the target may be left in a bad state.", true},
        {"call-pure", "ue", ToolPack::Observe, "call a const or pure function that keeps nothing (read-only); as call takes, or each on every instance",
            {{"object", "str", "an object (a literal ref, \"0x...\" or a Default__ name); with each, a class", {}, true, false, ref},
             {"function", "str", function, {}, true},
             params, block,
             {"dry", "bool", "resolve the function and build the block; call nothing"},
             {"each", "bool", "call it on every live instance of the class or a subclass, in one tick: each's table"},
             {"seconds", "int", "with each: repeat for 1..60 s and report changes"},
             outer,
             {"limit", "int", "with each: rows shown (0: all); omitted counts the rest", "100"},
             nonzero},
            Of<shape::CallPure>, build::CallPure,
            "The agent refuses, before calling, a function that is neither const nor pure, or one with an output "
            "that owns memory (use call). Pure is the engine's flag, not a proof: a function can still change state. "
            "each answers as each does (mode call; rows {ref, name, values}, the one value its return value or "
            "{return, outputs}): the function takes no inputs and its block is 1..64 bytes."},
        {"release", "ue", ToolPack::Modify, "one more call (zeroed inputs by default) that drops the kept block",
            {{"object", "ref", object, {}, true, false, ref}, {"function", "str", function, {}, true}, block},
            Of<shape::Call>, build::Release, {}, true},
        {"label", "ue", ToolPack::Native, "name an unreflected member in <console>.native.txt",
            {{"object", "str", objectOrClass, {}, true, false, ref}, {"offset", "int", "the member's offset in the object", {}, true},
             {"type", "str", "its type, with a count for an array: f64[3]", {}, true, false, "\"f32\""},
             {"name", "str", "the member's name (a trailing ? marks it unconfirmed)", {}, true}},
            Of<shape::NativeLabel>, build::Label, "get then shows it (native; stale after a patch)."},
        {"export-labels", "ue", ToolPack::Native, "names for a static-analysis tool (JSON lines, one build)",
            {{"file", "file", "the labels file (default: the console's, <console>.labels.jsonl)"},
             {"module", "str", "the module (default: the exe)"},
             {"kinds", "exec|vtable|interface|global", "which rows to derive (default: all)", {}, false, true}},
            Of<shape::ExportLabels>, build::ExportLabels,
            "Native exec thunks, vtables and interface implementations, merged into the file: names a static tool "
            "wrote back (user), globals peek's label confirmed, kinds not derived now and other builds' lines are "
            "kept; per address the highest rank wins (user, global, exec, interface, vtable). After another tool's "
            "mappings: kinds [\"interface\", \"global\"]."},
        {"export-assets", "ue", ToolPack::Native, "each object of the classes into its own JSON file: every nonzero field, and a table's rows",
            {{"dir", "file", "the folder to write (created when missing)", {}, true},
             {"cls", "str", "the classes (and their subclasses) to export, up to 16", {}, true, true},
             {"match", "str", "only objects whose name contains the text, any case"}},
            Of<shape::ExportAssets>, build::ExportAssets,
            "Live instances only (no Default__ or archetype objects), up to 2000 per call (then capped). A file "
            "<dir>/<Class>/<name>.json holds {ref, name, class, get, rows}: get is get's result with nonzero, rows "
            "(a data or curve table) is rows' with limit 0; a read that failed is {error} in its place. "
            "<dir>/index.json lists every file; rows here shows the first 100. Run it again to refresh: files are replaced."},
        {"layout-export", "ue", ToolPack::Native, "classes', structs' and enums' reflected layouts into a file (JSON lines): for layout-check and a static tool's types",
            {{"file", "file", "the file to write", {}, true},
             {"classes", "str", "class, struct or enum names (up to 256)", {}, true, true},
             {"closure", "int", "also the types their fields reference, to this depth (1..8), and each one's super chain"}},
            Of<shape::LayoutExport>, build::LayoutExport,
            "A class or struct line (name, super, size) then one field line per field it declares: owner, field, "
            "offset, size, type, and the struct, class or enum it names, with element, key and value parts for "
            "containers; an enum line holds its values. missing lists names that are no class, struct or enum; "
            "capped: closure stopped at 1024 types."},
        {"layout-check", "ue", ToolPack::Native, "a file of expected fields against the live layout; only the mismatches",
            {{"file", "file", "the file layout-export wrote", {}, true}}, Of<shape::LayoutCheck>, build::LayoutCheck,
            "Lines as layout-export writes them; offset as a number or \"0x..\", size and type optional. problem: "
            "no class, no field, offset, size or type; ok when every line holds. Rows stop at 200 (capped)."},
        {"entries", "ue", ToolPack::Explore, "one page of an array, set or map: elements, or key and value",
            {{"object", "str", objectOrDefault, {}, true, false, ref},
             {"field", "str", "the field, as Owner:Field where the name is ambiguous", {}, true},
             cursor},
            Of<shape::Entries>, build::Entries, "Names and object pointers are resolved."},
        {"rows", "ue", ToolPack::Explore, "a data table's rows decoded with its RowStruct, or a curve table's curves and their keys",
            {{"object", "str", objectOrDefault, {}, true, false, ref},
             {"match", "str", "row names containing the text, any case"},
             {"fields", "path", "the row members to keep (paths into structs and arrays)", {}, false, true},
             {"at", "num", "a curve table: each row's value at this time"},
             cursor,
             pageLimit,
             nonzero},
            Of<shape::Rows>, build::Rows,
            "RowMap is unreflected: read as the stock UE5 TMap<FName, uint8*> right after RowStruct, its slot "
            "count checked against its allocation bits (refused when they disagree). Nested arrays up to 256 "
            "elements, 3 deep; names resolved. total counts the rows match kept. A CurveTable: curve "
            "simple|rich, per row keys ([t, v]; rich: [t, v, interp, arrive, leave]) and a simple curve's interp; "
            "at adds value, evaluated per key interp (constant, linear, cubic), the end keys' values outside "
            "them."},
        {"get", "ue", ToolPack::Explore, "an object's fields with their values, or named paths through pointers and structs; types: the layout",
            {{"object", "str", "an object (a literal ref, \"0x...\", a Default__ name), or with types a class or struct by name", {}, true, false, ref},
             {"paths", "path", "a field path per value (up to 16); none: every field", {}, false, true},
             fieldMatch,
             {"nonzero", "bool", "non-zero fields, without the zero members inside their values"},
             {"own", "bool", "only the fields the object's own class declares, none it inherits"},
             {"types", "bool", "offsets, types and sizes, no values (a class or struct: its own fields)"},
             elements,
             {"snap", "bool", "keep the object's bytes as the baseline for changed"},
             {"changed", "bool", "only fields changed since snap; adds before"},
             {"file", "file", "with snap or changed: the snapshot file (across runs)"}},
            Of<shape::Get>, build::Get,
            "No paths: every field, compact (name, offset, value; raw beside a recovered name, object for a pointer, "
            "preview for a container); check truncated on the result and its rows before treating values as "
            "complete. Paths: one row per path in order (path, value, address, holder when a pointer led there), "
            "Owner:Field where a name is ambiguous; read back to back, same_tick says whether in one tick; a bad "
            "path fails its row with error naming the closest fields. types: the layout (owner, type, offset, "
            "size and the container's parts), no values. snap answers only snap {size, path} and keeps the "
            "baseline; changed then lists the fields that differ, with before. match, own, snap and "
            "changed take no paths (there nonzero only trims values); types reads no values."},
        {"trace", "ue", ToolPack::Observe, "the first thing a line hits: actor, component, physical material, distance (read-only)",
            {{"start", "point",
              "the start: a world point [x, y, z] in cm, an object's location (actor, component, camera manager) by "
              "its ref, or {\"object\": <ref>, \"socket\": \"Name\"} on a mesh component", {}, true},
             {"end", "point", "the end, as start takes", {}, true},
             {"channel", "int", "the trace type query: 0 visibility, 1 camera, N: TraceTypeQuery N+1", "0"},
             {"simple", "bool", "simple collision instead of complex"},
             {"ignore", "ref", "an actor the line passes through; also the world it runs in"}},
            Of<shape::Trace>, build::Trace,
            "KismetSystemLibrary.LineTraceSingle on the game thread, decoded by GameplayStatics.BreakHitResult; it "
            "changes no state. The world comes from ignore, else from an end's object: two points need ignore. "
            "Distance and points are in cm; distance counts from start. A socket needs a mesh component (get the "
            "actor's Mesh)."},
        {"calls-start", "ue", ToolPack::Modify, "record a reflected function's calls, with their arguments, for a while",
            {{"owner", "str", "whose function: an object or a class by name (object narrows to one object)", {}, true},
             {"function", "str", function, {}, true},
             {"duration", "dur", "how long, in seconds (at most 60)", {}, true},
             {"object", "ref", "only calls on this object"},
             {"dry", "bool", "resolve and show the pointer that would be swapped; write nothing"}},
            Of<shape::CallsStart>, build::CallsStart,
            "Swaps the function's native pointer (UFunction::Func, heap data; no code, vtable or import) for a "
            "recording thunk that forwards every call, and puts it back at the end or on calls-stop (only while it "
            "still holds the thunk). Calls through ProcessEvent (C++ callers, RPCs, Blueprint events, delegates) "
            "carry their parameters after the call; Blueprint graphs calling native code show only the object "
            "(via vm); direct C++ calls are not seen. 256 rows per recording; older ones are dropped.", true},
        {"calls-read", "ue", ToolPack::Modify, "a recording's calls so far, decoded by the function's parameters",
            {{"slot", "int", "the recording (default: the last started)"},
             {"start", "int", "next_cursor: only calls after it"}},
            Of<shape::Calls>, build::CallsRead,
            "values: in, out and return by name, read after the call (inputs as the function left them). Objects "
            "are named while they live; strings, arrays, sets and maps print their headers (the caller may have "
            "freed them). A recording past its time is restored first."},
        {"calls-stop", "ue", ToolPack::Modify, "end a recording now (restores the pointer) and return its calls",
            {{"slot", "int", "the recording (default: the last started)"}},
            Of<shape::Calls>, build::CallsStop,
            "state replaced: something else swapped the pointer since; it was left alone and the thunk keeps forwarding."},
        {"describe", "ue", ToolPack::Explore, "an object's name, class and reusable ref",
            {{"object", "ref", "the object: a literal ref or \"0x...\"", {}, false, false, ref},
             {"index", "int", "in place of object: an object-table slot (a weak pointer's), with serial"},
             {"serial", "int", "with index: the slot's serial number"}},
            Of<shape::Object>, build::Describe, "Give object, or index and serial."},
        {"func", "ue", ToolPack::Explore, "a function's reflected metadata, exec entry address and parameter signature",
            {{"object", "str", objectOrClass, {}, true, false, ref}, {"function", "str", function, {}, true}},
            Of<shape::Func>, build::Func,
            "params lists the parameters in block order (direction in, out, in ref, in/out ref or return) and block_size "
            "is the call block's size; reading them needs the game thread, so without it params_error says why instead."},
        {"script", "ue", ToolPack::Explore, "a Blueprint function's bytecode: the names, functions and properties it uses; decode: as statements",
            {{"object", "str", objectOrClass, {}, true, false, ref}, {"function", "str", function, {}, true},
             {"decode", "bool", "read the bytecode as statements with their offsets"},
             {"entry", "int", "with decode: one uber-graph event, from this offset to its return, pop or goto"}},
            Of<shape::Script>, build::Script,
            "Each entry is read right after its opcode and confirmed live. decode reads every expression instead (no "
            "game thread): statements, the lists from the decoded operands, properties by their readable name (raw: the "
            "reflected one), functions as Owner.Function, math operators as infix; an opcode it does not know stops it "
            "(stopped). An uber graph adds events: each event's entry offset, from the calls that enter it."},
        {"enum", "ue", ToolPack::Explore, "an enum's names and values, in order",
            {{"enum", "str", "an enum object (a literal ref, \"0x...\"), or an enum by name", {}, true}},
            Of<shape::Enum>, build::Enum,
            "Byte and enum properties print numbers; this names them. Names keep the enum's prefix (EFoo::Bar)."},
        {"game-thread", "ue", ToolPack::Modify, "arm or remove the host's game-thread tick through the game viewport's Draw (status shows it)",
            {{"action", "on|off", "on arms it, off puts the viewport's table back", {}, true}},
            Of<shape::GameThread>, build::GameThread,
            "call, call-pure, release, func's params, each, script, refs, exec entries and the recorder need the game thread. on swaps one pointer in "
            "the viewport object for a copy of its Draw table: confirm Draw first (the skill's references/unreal/game-thread.md). "
            "Agents whose host does not link reagentlive::ue-draw answer unsupported.", true},
        {"refs", "ue", ToolPack::Explore, "Blueprint functions whose bytecode names a pooled name (EX_NameConst)",
            {{"name", "str", "the exact pooled name", {}, false, false, "\"None\""},
             {"index", "int", "in place of name: its comparison index"}},
            Of<shape::Refs>, build::Refs, "Give name or index."},
        {"funcs", "ue", ToolPack::Explore, "functions on the class chain, then on the native interfaces it implements",
            {{"object", "str", objectOrClass, {}, true, false, ref}, {"text", "str", "keep function names containing it"}, cursor},
            Of<shape::Funcs>, build::Funcs,
            "A row with via \"interface\" is declared only on that native interface: call refuses it on the object; func shows the implementation to call instead."},
    };
    return verbs;
}

// What applies to every verb of the pack; `help` prints these under the rows.
inline const std::vector<std::string>& UnrealNotes() {
    static const std::vector<std::string> notes = {
        "Unreal pack verbs are refused when the host does not offer the pack (status lists the packs).",
        "Refs: pass the literal 0x<address>#<index>:<serial> a result printed (find, census, each, describe, get, "
        "entries); it works across runs.",
        "A bare class or struct name works where an arg says so; the first lookup scans the table.",
        "Struct values print as {Name: value}; meta adds their member layout.",
    };
    return notes;
}
}
