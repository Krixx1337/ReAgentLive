// reagentlive CLI, Unreal pack: the pack's rows of the verb table
// (call/verb.h). The core's rows and what reads them are in results/verbs.h.
#pragma once
#include <vector>
#include "reagentlive/packs/ue/cli/results/census.h"
#include "reagentlive/packs/ue/cli/results/result_json_ue.h"
#include "reagentlive/frontends/cli/call/build_internal.h"

namespace reagentlive::cli {
// The bounds the builders enforce, as the arg help below states them.
static_assert(ue::MaxCallsSlots == 8 && MaxRead / 4 == 1024);
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
             View("start limit")},
            Of<shape::Find>, build::Find, "Give text, cls or outer. Only the first page counts total. view.limit "
            "(1..1000) pages until that many rows or the end; next_cursor continues after the last row (view.start). "
            "cls uses the agent's class index; text alone walks the whole object table, so add cls when the class is "
            "known."},
        {"census", "ue", ToolPack::Observe, "live instances per class, most first; pages of 100 rows; diff: only the counts that changed",
            {cls,
             outer,
             {"diff", "bool", "only the classes whose count differs from the last census of that cls and outer"},
             View("match start limit", "100")},
            Of<shape::Census>, build::Census,
            "view.match keeps class names; view.limit is rows per page (0: all); view.start (next_cursor) pages the "
            "stored census without a new scan. Every census becomes the next diff's baseline. diff takes a new census and answers "
            "only the classes whose count changed, each row's count now with before, and before as the earlier census's classes "
            "and instances; refused with no earlier census under the same filter."},
        {"name-find", "ue", ToolPack::Explore, "FName indices whose text contains text (gameplay tags, row names)",
            {{"text", "text", "part of the pooled name", {}, true}, View("start")}, Of<shape::NameFind>, build::NameFind},
        {"name", "ue", ToolPack::Explore, "FName comparison indices as text (FName #N in dumps)",
            {{"indices", "int", "FName comparison indices (up to 1024)", {}, true, true}}, Of<shape::Names>, build::Name,
            "A row has no name when the index is not where a pool entry starts (inside an entry) or does not decode."},
        {"each", "ue", ToolPack::Explore, "a table over the live instances of a class: a value per column (up to 16 field paths)",
            {{"cls", "str", "a class or struct name", {}, true},
             {"paths", "path", "a field path per column (structs and object pointers, as get takes)", {}, true, true},
             {"seconds", "int", "one column: repeat for 1..60 s and report changes"},
             {"where", "filter", "only instances whose field's value contains the text, any case (a string, a name or "
              "text; else its JSON); the field need not be a column; Outer, Name and Class match the object's own "
              "(Class is the class's name: cls takes subclasses)"},
             outer,
             View("limit nonzero elements", "100")},
            Of<shape::Each>, build::Each,
            "rows: {ref, name, values} in the columns' order (name: the instance's); a failed cell is null with an errors entry. Spans inside the "
            "instance are read in passes of up to 64 bytes, the first in one tick; a path through a pointer is "
            "followed per instance (same_tick false). view.limit is rows shown (0: all); omitted counts the rest. "
            "Reads only: a function is refused, call-each calls one on every instance."},
        {"call", "ue", ToolPack::Modify, "call a reflected function with inputs by name (typed) or a raw block",
            {{"object", "str", objectOrDefault, {}, true, false, ref}, {"function", "str", function, {}, true},
             params, block,
             {"dry", "bool", "resolve the function and build the block; call nothing"},
             View("fields start limit nonzero", "100")},
            Of<shape::Call>, build::Call,
            "A raw block must match the function's size (func shows it as block_size); none sends a zeroed block when every "
            "parameter is an output. hex is the block after the call; a typed call adds outputs, a returned array "
            "as {count, rows, next_cursor} with each element decoded from its struct (nested arrays up to 256 "
            "elements, 3 deep; names resolved). A view pages a typed call's returned arrays (fields: the row members, "
            "start: next_cursor, limit: rows per page, 0 all; nonzero inside the outputs) and makes the call typed. kept: an output "
            "owns memory (a handle, a string), so the agent reuses the block and the next call releases the old values. "
            "dry answers mode dry: hex is the block that would be sent, with exec and pure; show it when asking to call. "
            "A call that faults in the engine is refused as faulted: the target may be left in a bad state.", true},
        {"call-pure", "ue", ToolPack::Observe, "call a const or pure function that keeps nothing (read-only); as call takes",
            {{"object", "str", objectOrDefault, {}, true, false, ref},
             {"function", "str", function, {}, true},
             params, block,
             {"dry", "bool", "resolve the function and build the block; call nothing"}},
            Of<shape::Call>, build::CallPure,
            "The agent refuses, before calling, a function that is neither const nor pure, or one with an output "
            "that owns memory (use call). Pure is the engine's flag, not a proof: a function can still change state. "
            "The result is call's; no view, since a returned array owns memory. Without params or block the call is "
            "typed all the same (outputs decoded, as call-each's); inputs are never zeroed for you: a function with "
            "inputs needs params. On every instance of a class: call-each."},
        {"call-each", "ue", ToolPack::Observe, "call a const or pure function with no inputs on every live instance of a class, in one tick: each's table",
            {{"cls", "str", "a class name: its live instances and its subclasses'", {}, true},
             {"function", "str", function, {}, true},
             {"seconds", "int", "repeat for 1..60 s and report changes"},
             outer,
             View("limit nonzero", "100")},
            Of<shape::Each>, build::EachCall,
            "call-pure's checks, per instance: answers as each does (mode call; rows {ref, name, values}, the one "
            "value its return value or {return, outputs}). The function takes no inputs and its block is 1..64 "
            "bytes. view.limit is rows shown (0: all); omitted counts the rest. A verb of its own: its result is "
            "each's table, not call's."},
        {"release", "ue", ToolPack::Modify, "one more call (zeroed inputs by default) that drops the kept block",
            {{"object", "ref", object, {}, true, false, ref}, {"function", "str", function, {}, true}, block},
            Of<shape::Call>, build::Release, {}, true},
        {"member-name", "ue", ToolPack::Native, "name an unreflected member in <console>.native.txt",
            {{"object", "str", objectOrClass, {}, true, false, ref}, {"offset", "int", "the member's offset in the object", {}, true},
             {"type", "str", "its type, with a count for an array: f64[3]", {}, true, false, "\"f32\""},
             {"name", "str", "the member's name (a trailing ? marks it unconfirmed)", {}, true}},
            Of<shape::NativeLabel>, build::MemberName, "get then shows it (native; stale after a patch)."},
        {"labels-export", "ue", ToolPack::Native, "names for a static-analysis tool (JSON lines, one build)",
            {{"file", "file", "the labels file (default: the console's, <console>.labels.jsonl)"},
             {"module", "str", "the module (default: the exe)"},
             {"kinds", "exec|vtable|interface|global", "which rows to derive (default: all)", {}, false, true}},
            Of<shape::ExportLabels>, build::LabelsExport,
            "Native exec thunks, vtables and interface implementations, merged into the file: names a static tool "
            "wrote back (user), globals peek's label confirmed, kinds not derived now and other builds' lines are "
            "kept; per address the highest rank wins (user, global, exec, interface, vtable). global derives nothing "
            "(peek's label writes those). After another tool's mappings: kinds [\"interface\"]."},
        {"assets-export", "ue", ToolPack::Native, "each object of the classes into its own JSON file: every nonzero field, and a table's rows",
            {{"dir", "file", "the folder to write (created when missing)", {}, true},
             {"cls", "str", "the classes (and their subclasses) to export, up to 16", {}, true, true},
             View("match")},
            Of<shape::ExportAssets>, build::AssetsExport,
            "Live instances only (no Default__ or archetype objects), up to 2000 per call (then capped). A file "
            "<dir>/<Class>/<name>.json holds {ref, name, class, get, rows}: get is get's result with nonzero, rows "
            "(a data or curve table) is rows' with limit 0; a read that failed is {error} in its place. "
            "<dir>/index.json lists every file; rows here shows the first 100. view.match keeps objects whose name "
            "contains the text. Run it again to refresh: files are replaced."},
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
             View("start")},
            Of<shape::Entries>, build::Entries, "Names and object pointers are resolved. Elements and slots up to 512 "
            "bytes; a bigger one is refused: get on the field decodes it."},
        {"rows", "ue", ToolPack::Explore, "a data table's rows decoded with its RowStruct, or a curve table's curves and their keys",
            {{"object", "str", objectOrDefault, {}, true, false, ref},
             {"at", "num", "a curve table: each row's value at this time"},
             View("match fields start limit nonzero", "100")},
            Of<shape::Rows>, build::Rows,
            "RowMap is unreflected: read as the stock UE5 TMap<FName, uint8*> right after RowStruct, its slot "
            "count checked against its allocation bits (refused when they disagree). Nested arrays up to 256 "
            "elements, 3 deep; names resolved. view.match keeps row names; view.limit is rows per page (0: all); total "
            "counts the rows match kept. A CurveTable: curve "
            "simple|rich, per row keys ([t, v]; rich: [t, v, interp, arrive, leave]) and a simple curve's interp; "
            "at adds value, evaluated per key interp (constant, linear, cubic), the end keys' values outside "
            "them."},
        {"get", "ue", ToolPack::Explore, "an object's fields with their values, or named paths through pointers and structs; types: the layout",
            {{"object", "str", "an object (a literal ref, \"0x...\", a Default__ name), or with types a class or struct by name", {}, true, false, ref},
             {"paths", "path", "a field path per value (up to 16); none: every field", {}, false, true},
             {"mode", "values|types|snap|changed", "values; types: offsets, types and sizes, no values (a class or "
              "struct: its own fields); snap: keep the object's bytes as the baseline; changed: only fields changed "
              "since snap, with before", "\"values\""},
             {"own", "bool", "only the fields the object's own class declares, none it inherits"},
             {"file", "file", "with snap or changed: the snapshot file (across runs)"},
             View("match nonzero elements")},
            Of<shape::Get>, build::Get,
            "No paths: every field, compact (name, offset, value; raw beside a recovered name, object for a pointer, "
            "preview for a container); check truncated on the result and its rows before treating values as "
            "complete. Paths: one row per path in order (path, value, address, holder when a pointer led there), "
            "Owner:Field where a name is ambiguous; read back to back, same_tick says whether in one tick; a bad "
            "path fails its row with error naming the closest fields. types: the layout (owner, type, offset, "
            "size and the container's parts), no values. snap answers only snap {size, path} and keeps the "
            "baseline; changed then lists the fields that differ, with before. view.match keeps field names or "
            "readable labels; view.nonzero leaves out zero fields and the zero members inside values. view.match, "
            "own and the modes but values take no paths (there nonzero only trims values); types reads no values. "
            "Options: one mode, not four flags, since the modes exclude each other; own and paths pick fields, file "
            "names the baseline."},
        {"line-trace", "ue", ToolPack::Observe, "the first thing a line hits: actor, component, physical material, distance (read-only)",
            {{"start", "point",
              "the start: a world point [x, y, z] in cm, an object's location (actor, component, camera manager) by "
              "its ref, or {\"object\": <ref>, \"socket\": \"Name\"} on a mesh component", {}, true},
             {"end", "point", "the end, as start takes", {}, true},
             {"channel", "int", "the trace type query (0..31): 0 visibility, 1 camera, N: TraceTypeQuery N+1", "0"},
             {"simple", "bool", "simple collision instead of complex"},
             {"ignore", "ref", "an actor the line passes through; also the world it runs in"}},
            Of<shape::Trace>, build::LineTrace,
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
            {{"slot", "int", "the recording, 0..7 (default: the last started)"},
             View("start")},
            Of<shape::Calls>, build::CallsRead,
            "values: in, out and return by name, read after the call (inputs as the function left them). Objects "
            "are named while they live; strings, arrays, sets and maps print their headers (the caller may have "
            "freed them). A recording past its time is restored first."},
        {"calls-stop", "ue", ToolPack::Modify, "end a recording now (restores the pointer) and return its calls",
            {{"slot", "int", "the recording, 0..7 (default: the last started)"}},
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
            {{"object", "str", "an object or class (a literal ref, \"0x...\", or a class name), or the Function itself (its ref, "
              "as name-refs gives it)", {}, true, false, ref},
             {"function", "str", "the function's name (Owner:Name to pick one; none when object is the Function)"},
             {"decode", "bool", "read the bytecode as statements with their offsets"},
             {"entry", "int", "with decode: one uber-graph event, from this offset to its return, pop or goto"},
             {"to", "int", "with decode: statements before this offset only; with entry, from entry to here, past returns"},
             View("match")},
            Of<shape::Script>, build::Script,
            "Each entry is read right after its opcode and confirmed live. decode reads every expression instead (no "
            "game thread): statements, the lists from the decoded operands, properties by their readable name (raw: the "
            "reflected one), functions as Owner.Function, math operators as infix; an opcode it does not know stops it "
            "(stopped). An uber graph adds events: each event's entry offset, from the calls that enter it. A big "
            "decode narrows: view.match keeps the statements whose text or op contains it (filtered counts them), "
            "entry and to an offset range; the lists then come from the statements kept. A Function ref as object "
            "(name-refs' rows) needs no function: its outer and name are read from it."},
        {"enum", "ue", ToolPack::Explore, "an enum's names and values, in order",
            {{"enum", "str", "an enum object (a literal ref, \"0x...\"), or an enum by name", {}, true}},
            Of<shape::Enum>, build::Enum,
            "Byte and enum properties print numbers; this names them. Names keep the enum's prefix (EFoo::Bar)."},
        {"game-thread", "ue", ToolPack::Modify, "arm or remove the host's game-thread tick through the game viewport's Draw (status shows it)",
            {{"action", "on|off", "on arms it, off puts the viewport's table back", {}, true}},
            Of<shape::GameThread>, build::GameThread,
            "call, call-pure, call-each, release, func's params, each, script, refs, exec entries and the recorder need the game thread. on swaps one pointer in "
            "the viewport object for a copy of its Draw table: confirm Draw first (the skill's references/unreal/game-thread.md). "
            "Agents whose host does not link reagentlive::ue-draw answer unsupported.", true},
        {"name-refs", "ue", ToolPack::Explore, "Blueprint functions whose bytecode names a pooled name (EX_NameConst)",
            {{"name", "str", "the exact pooled name", {}, false, false, "\"None\""},
             {"index", "int", "in place of name: its comparison index"}},
            Of<shape::Refs>, build::NameRefs, "Give name or index."},
        {"owner", "ue", ToolPack::Explore, "the object holding each address: the field, the Outer pointer or the container element",
            {{"addrs", "addr", "addresses, as search rows give them (up to 256)", {}, true, true},
             {"within", "ref", "objects whose arrays, sets and maps to check for an address no object holds (up to 16)",
              {}, false, true}},
            Of<shape::Owner>, build::Owner,
            "One object-table scan per 64 addresses: an address is held by the nearest object at or below it when the "
            "offset is inside its class's size. An address in heap storage (an array's elements) has no such owner; "
            "the arrays, sets and maps of the within objects and of the other rows' objects are checked for it "
            "(container). Needs the game thread."},
        {"route", "ue", ToolPack::Explore, "the shortest routes of reflected references from a root or object to an object",
            {{"object", "ref", "the object to reach", {}, true},
             {"sources", "str", "up to 4: host roots by name (status lists them) or objects (a literal ref or "
              "\"0x...\"); default: every root", {}, false, true, "[\"pawn\"]"},
             {"depth", "int", "hops at most (1..16)", "8"},
             {"budget", "int", "objects visited at most (1..1048576)", "20000"},
             {"outer", "bool", "also step from each object up to its Outer (child to parent only)"}},
            Of<shape::Route>, build::Route,
            "Breadth-first over object fields, arrays, sets and map values of objects or of structs holding them; "
            "no class, weak, soft or lazy pointers, and no containers inside a container's elements. Up to 4 "
            "routes of the shortest length. end says why there is none: depth or budget ran out, or none (every "
            "reachable object visited). A walk that runs out its budget takes seconds; lower budget to fail fast. "
            "A step into a set or map reads [Key] for a name or object key, else [slot N]. Objects can die while it "
            "walks: check a route with get (entries for an indexed step) before relying on it. Needs the game thread."},
        {"funcs", "ue", ToolPack::Explore, "functions on the class chain, then on the native interfaces it implements",
            {{"object", "str", objectOrClass, {}, true, false, ref}, View("match start")},
            Of<shape::Funcs>, build::Funcs,
            "view.match keeps function names. A row with via \"interface\" is declared only on that native interface: call refuses it on the object; func shows the implementation to call instead."},
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
