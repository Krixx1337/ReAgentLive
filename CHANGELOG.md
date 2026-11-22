# Changelog

ReAgentLive follows semantic versioning. Before 1.0 the API and the wire
layout can change in any minor release; agent and client are always built
from the same revision, and the handshake's layout check refuses a
mismatched pair. The version lives in `src/reagentlive/version.h`.

## Unreleased

- **One decoder for every value:** sets and maps decode at any depth
  (`{count, rows: [{key, value}]}`), in `rows`, typed calls, `get`, `each`
  and `dump`; object, class and weak pointers inside values are named
  (`{name, class, ref}`, `gone` or `unverified`), with `unnamed` counting
  those past the cap. `each` takes set and map columns past 64 bytes, and
  `--elements N` lifts the 8-element preview on `get`, `each` and `dump`.
  `--fields` reaches into maps (`Damage.value`).
- **Object filters:** `--outer <object|text>` on `find`, `each` and
  `census`, applied in the agent's scan; `Outer`, `Name` and `Class` as
  `each` columns and `--where` fields; `find` and `describe` rows name the
  object's `outer`. Protocol: `FindEntry` carries the outer, describe many
  takes 28 refs, and the filter rides in `data` and `address` (rebuild and
  re-inject the agent).
- **`--nonzero` marks what it emptied:** an element or value whose members
  were all zero prints `{"zeroed": N}`, not `{}`.
- **Fix: decoding a struct read freed memory** when a nested struct type
  was not in the console's struct cache yet (the cache grew and moved the
  member list being walked): names went unresolved on first sight, and
  values could be wrong. The cache no longer moves its entries.
- **The Python session names why a console did not start:** its exit code
  and the end of its stderr, so the MCP adapter's error says it too.

- **Compact names and tags:** an FName prints as its text (`Name_N`,
  `#index` when unresolved) and a gameplay tag as its name, not
  `{index, number, name}` (kept under `--meta`); a text value as its
  string (`{text, via}` under `--meta`). Breaking for scripts.
- **One decoder for compact values:** `get`, `each` and `dump` read arrays
  inside structs (8 elements, 3 deep), show tag containers as lists of tags
  and convert text at any depth, as returned arrays and `rows` do.
- **Output budgets replace the large-result `hint`:** a `--caller tool`
  console prints at most 25,000 characters a result (`--budget N`, `--big`;
  a JSON call's `budget` and `big`). Past it, `digest` stands in for
  `result`: size, main list, largest parts, and `next`, the line narrowed
  (`--fields`, `--limit`, `--nonzero`), or `file`, the whole envelope of a
  line that may have changed state. The MCP tools take `budget` and return
  the digest; the Python session returns a `Digest`. Breaking for tools.
- **`stats` sees the budget and the sessions:** the usage log records a
  digested result's full size (`digest`); `stats` counts `digested` calls
  and the characters they `saved`, and no longer marks a digested call
  `unfiltered`. `stats --sessions` lists the log's sessions newest first,
  `stats --session <id>` reports one. One-shot runs join the cached
  session only with its caller, so a shell's checks stay out of a tool's.
- **`--nonzero` on `get`, `each`, `rows` and `call`:** leaves out the zero
  members inside values (elements stay); on `call` it implies a typed call.
- **`describe` makes its object `$1`.** A typed call's `function` is
  owner-qualified, as `--dry` prints it.

- **Tags and names by text in typed calls:** a struct holding only a name
  (a gameplay tag) takes the name alone, `ResourceTag=A.B.C`, also inside
  structs; a name the pool lacks is refused naming it.
- **Returned arrays as rows:** a typed `call` decodes an array output's
  elements with their reflected struct, nested arrays and names included,
  paged with `--start`/`--limit` and picked with `--fields a,b.c`
  ([named calls](docs/commands/unreal.md#named-calls)).
- **Text values:** `get`, `each` and `dump` show a `TextProperty` as its
  string through `KismetTextLibrary.Conv_TextToString`, or say why not
  ([text values](docs/commands/unreal.md#text-values)).
- **`rows <data table>`:** a table's unreflected `RowMap`, each row decoded
  with its `RowStruct` ([rows](docs/commands/unreal.md#rows)).
- **`each --where Field~text` and `--limit`** (100 rows by default, then
  `omitted`). Breaking for scripts that expected every row.
- **A one-hop chain's refusal** (`read X->0x20`) spells both fixes with the
  line's own address.

- **Member offsets print in hex** (`"offset": "0x2e0"`), as SDK dumps and
  the other hex keys do: `dump`, `fields`, `label`, `script`, `refs` and
  native-member refusals, plus `fields`' `value_offset`. Breaking for scripts
  that read them as numbers; the table hash changes.
- **One name for the object argument:** the positional of `dump`, `fields`,
  `label`, `func`, `funcs`, `params` and `script` is `object` (was
  `target`), as on `get`, `describe` and `entries`. `calls-start`'s is
  `owner`, beside its `--object` filter. Breaking for JSON calls and
  generated modules.
- **`find --live`** leaves out class default and archetype objects
  (`FindLive`), as `census` does. An agent built before it refuses the flag.
- **The Unreal pack without an SDK:** `reagentlive::ue-base`
  ([without an SDK](docs/commands/unreal.md#without-an-sdk)) builds with no
  Dumper-7 SDK and answers `find`, `describe`, object range reads, `name`,
  `census` and `name-find` in any UE5 game: the object layout is one of
  UE5's stock shapes, checked like the globals, and `SuperStruct` and
  `UStruct::Size` are probed from the engine's class tree.
  `reagentlive::ue` links it and builds it with the SDK as the layout's
  source, so SDK hosts change nothing.
- **`census` and `name-find` answer off the engine's thread** with
  `Context::offThreadReads`, under the fault guard like `find`.

- **Engine packs live in `src/reagentlive/packs/<id>/`** ([contributing](docs/contributing.md#source-layout)),
  breaking for hosts: the Unreal pack's headers are
  `reagentlive/packs/ue/...` (was `reagentlive/ue/...`) and its CLI parts
  moved from `frontends/cli/<layer>/ue/` to `packs/ue/cli/<layer>/`. Each
  pack defines its targets in its `pack.cmake`; `REAGENTLIVE_PACKS`
  (default `ue`, empty for none) replaces `REAGENTLIVE_CONSOLE_UE`. Each
  pack owns an op slot of 256 (`op::PackSlot`); the Unreal ops keep their
  values. Target names (`reagentlive::ue`, `::ue-client`) are unchanged.
- **MCP tools made from the table's rows** ([clients](docs/client.md#mcp-tools)):
  the adapter runs on the SDK's low-level server and builds each tool's
  input and output schema from its row, with nothing private from the SDK
  and no generated code. Arguments are checked against the row before a
  console starts, and a null argument counts as left out.
  `--output-schemas full` is gone (`compact` and `off` remain).
- **The Python session names no verb but `batch` and `watch`'s rows:**
  `read_bytes`, `read_struct`, `read_batch_bytes`, `get_values`,
  `name_map`, `call_bytes` and `Session.ok` are gone. Use the generated
  functions with `unhex` and `f32`...; `s.send` stays as the raw exchange.
- **A busy agent is waited for:** a console asks again for up to 2 s
  before refusing, and the refusal's kind is `busy`, not `startup`.
- **Stricter generator:** a type the generator or the adapter does not
  know stops it, in place of a silent `Any` or `str`.
- The console reads each input line once (main loop, `batch` items and
  the no-target mode alike). Design: the self-describing console and the
  tool interface moved to [tool interface](docs/tool-interface.md).
- **Compact `disasm` rows:** the listing names its module once; rows keep
  `rva`, `bytes`, `text`, `target`, `target_rva`, `string` and `import`,
  and `target_module` only for another module. `--meta` adds each row's
  `module` and `fields`, as before. Callers that read `fields` or a row's
  `module` change once. One session's `disasm` output was half its total.
- **Call recorder** ([unreal](docs/commands/unreal.md#call-recorder)):
  `calls-start <object|Class> <[Owner:]Function> <duration>` records a
  reflected function's calls with their parameters, decoded by name;
  `calls-read` pages them, `calls-stop` ends it early. It swaps the
  function's native pointer (`UFunction::Func`, heap data) for a fixed
  thunk by compare-and-swap, and restores it only if it still holds the
  thunk (`state: replaced` otherwise). No code, vtable or import is
  written. Up to 8 recordings of at most 60 s, 256 rows each; `--dry`
  writes nothing; `--object` keeps one object's calls. A host offers it by
  listing the ops, and each start is a write it approves. Calls made
  directly from C++ are not seen; a Blueprint calling native code gives
  the object only (`via: vm`). Wire: `CallsStart`, `CallsRead`,
  `CallsStop`. Design: [principles](docs/design.md#principles).
- **`trace <start> <end>`**: a line trace through
  `KismetSystemLibrary.LineTraceSingle`, its hit decoded by
  `GameplayStatics.BreakHitResult`, both signatures checked by parameter
  type and order first (UE4 and UE5). Ends are points, objects or
  `object@Socket`; `--ignore`, `--channel`, `--simple`. Read-only: no agent
  op and no `writes` mark.
- **FName inputs as text** in typed calls (`Name=Head`), looked up once
  per session through `name-find`; `#index[:number]` still works.
- **`watch` returns changes** ([memory](docs/commands/memory.md#watch)):
  `watch`, `watch-read` and `watch-stop` give the first sample, each one
  that differs from the one before (with its time) and the last, the
  middle past 200 rows dropped and counted; the summary still covers every
  sample and adds changes per value. `--all` returns every sample. A range
  may be `<object>.<Field[.Field]>`, resolved once through `get`'s path
  walk and typed from reflection. Callers that read every sample pass
  `--all`.
- **`funcs` lists interface functions** (`via: interface`) after the
  class's own chain; `call` refuses them and `func` gives the
  implementation. Wire: `FunctionNameEntry` gains `interface`.
- **Typed call inputs** for `call` and `call-pure`: bools (a bitfield bool
  sets only its bit), every integer width with range checks, finite floats
  and doubles, and plain structs as `(Member=value, ...)` or positional,
  nested. A const reference counts as an input. Wire: `ParameterFlag`
  gains `Const`; rebuild the agent with the console.
- Fixed: `func` found no slot or implementation for an interface stub
  whose call sits past 160 bytes; it now scans the stub's whole function
  from its module's exception directory, up to 512 bytes.
- Fixed: typed calls refused a function returning a const reference
  (`GetWindVector`); only a non-const in/out reference is refused.

## 0.3.0

Sessions that cost less and reach further: a tool list half the size,
engine calls that survive a fault, class lookups without a table scan,
bulk layout checks after a patch, and names and narrowing for native
code with no engine pack. The Unreal pack finds its globals after a game
patch, and a console can be built without it.

Cheaper agent sessions: fewer wrong-looking answers, less output, fewer
turns ([design](docs/design.md#output-weight-and-dead-ends)). Every caller
breaks once: three argument names, and the `dump`, `get`, `each` and
`watch` results changed.

- **A tool list half the size** ([clients](docs/client.md#mcp-tools)): the
  MCP adapter advertises compact result schemas by default (top-level keys
  and types, widened; the console still checks every result in full), about
  63 KB in place of 126 on one host's console. `--output-schemas
  full|compact|off`, `--families <origins>` and `--read-only` cut it further.
- **Safer engine calls** ([unreal](docs/commands/unreal.md)): an access
  fault inside `ProcessEvent` (`call`, `each`) is answered `access faulted`
  instead of ending the game. New `call-pure`: the agent refuses, before
  calling, anything not const or pure or with an output that owns memory,
  so its tool is read-only. `call --dry` builds the block and calls
  nothing (`mode: dry`, with `exec` and `pure`). Wire: `CallPure`.
- **Class names without a table scan each:** a bare class name (`fields
  Character`) is an exact `find` that stops at the first match
  (`FindExact`); an older agent still answers, slower. Hosts may let scans
  run longer per tick (`Context::scanMicroseconds`).
- **Layouts in bulk:** `layout-export <file> <Class>...` writes the fields
  classes declare as JSON lines; `layout-check <file>` answers only the
  lines the live layout does not hold.
- **Names the image carries:** `rtti <addr>` names an MSVC object's class
  and bases from its type information; `disasm` rows add `import`
  (`KERNELBASE.dll!Sleep`) for a call or jump through the import table.
- **Raw narrowing over one range:** `mem-snap <addr> <len>` (up to 1 MiB,
  kept beside the cache file) and `mem-diff
  changed|unchanged|increased|decreased <type> [--keep]`.

- **No answer that points the wrong way:**
  - A pointer chain of one `->off` (it follows no pointer) is refused,
    from a typed line and a JSON call alike, with the form that follows
    one: `<addr>->0->0x30`.
  - No argument is a Python keyword: `--class` is `--cls`, `--from` is
    `--start`, `read --as` is `read --type`, and `each`'s class is `cls`;
    every form spells them the same. A table test refuses a keyword.
  - An unknown JSON key is checked first and named with the keys the verb
    takes; an unknown flag lists the verb's flags; `read-batch` and `each`
    state the limit they hit.
- **Compact rows:** a `dump` row is `name` (the recovered readable one,
  `raw` beside it), `offset` and `value`, with `meta` (the layout) on
  `--meta` and `truncated` only when true; `dump --own` keeps the fields the
  object's own class declares. A `get` row is `value` and `address`, with
  `object` only when the path crossed a pointer; `path` and `ok` are gone
  (rows keep the order asked; a failed path keeps its place as `error`).
- **`each` is a table:** up to 16 field paths (structs and object pointers,
  as `get` takes) or one function; `columns`, then `rows` of `{ref,
  values}`, `errors` for failed cells and `same_tick`. Spans inside the
  instance share passes of 64 bytes over one scan.
- **A console without the Unreal pack:** `REAGENTLIVE_CONSOLE_UE=OFF`
  builds the console from core files and an empty pack seam
  (`console/no_pack.cpp`). Its table, `help`, generated functions and MCP
  tools hold no pack verbs and no core verb marked `needsPack` (`trial`).
  The default console's table and hash are unchanged.
- **MCP tools that follow a rebuilt console** ([design](docs/tool-interface.md#tool-interface)):
  the adapter starts a console per tool call (one at a time) instead of
  keeping one open, so a rebuild links and the next call runs it. Tools
  are made in memory from the console's `help --json`; `--module` is
  gone. Each start reads `help --hash` (new: the table's hash alone, no
  target needed); on a change the tools are made again before the call
  runs and the client is told the list changed. New console flag
  `--session <id>` names the usage-log session (the adapter passes one
  per server). The last `snap` is kept in the cache file, so `dump
  --changed` works across runs. `Session(..., table=)` reads only the
  hash when it already has the table.
- **Usage log and `stats`** ([console](docs/commands/console.md#usage-log-and-stats),
  [design](docs/design.md#usage-log)): the console appends one line of
  facts per call to `<console>.usage.jsonl` (verb, flag names, input hash,
  sizes, refusal kind, time; never values). `stats` ranks a session's
  verbs by output with `unfiltered`, `repeated` and `refused` marks and
  the largest calls; `--all` covers the log and lists verbs never called.
  `--caller tool|script|shell` (the Python session passes `script`, the
  MCP adapter `tool`) separates context cost; `--no-usage` turns it off.
  With no target the console answers `help` and `stats` and refuses the
  rest as `startup`, then ends. The cache file is format 9 (it names the
  usage session one-shot runs share).
- **Typed watches:** a range may be `<addr>:f64` or `<addr>:i32[2]`; its
  samples are numbers and the result adds a `summary` per value (first,
  last, least, greatest, `per_second`). The type is kept in the agent's
  copy of the spec (`WatchRange::reserved` is now `note`, same layout), so
  `watch-read` in a later run decodes it.
- **An Unreal pack that survives a game patch**
  ([before the first command](docs/commands/unreal.md#before-the-first-command)):
  the pack takes only types from the SDK and finds the object table and
  name pool itself, from the host's guess or a scan of the image's
  writable sections, checking both before its first command. A failed
  check is `pack off: <reason>`; core commands keep working. Names come
  from the located pool instead of the SDK's `FName::ToString`.
  `Context` gains `objectsGuess`, `namesGuess` and `image` (all optional);
  `ue::ObjectInTable` is an object check for hosts without one. The name
  pool's hashed layout honours its kind byte (kind-2 entries are 22
  characters).
- **Pack reads off the engine's thread, opt-in, being measured**
  ([unreal](docs/commands/unreal.md#off-the-engines-thread)):
  `Context::offThreadReads` lets `find`, `describe`, `fields`, object
  reads, array previews, `entries` and `names` run on a borrowed thread,
  each under a fault guard with the object's identity checked before and
  after. Everything that runs engine code stays refused. `dump` and
  `entries` results carry `tick_accurate: false` off the engine's thread,
  as `get` and batches already did.

## 0.2.0

AI agents get MCP tools: one tool per console verb, generated from the
console's own verb table, with checked arguments and typed results. Every
caller of the console breaks once: verbs, result keys and the Python client
changed (below).

- **Typed calls and MCP tools, generated from the verb table**
  ([design](docs/tool-interface.md#tool-interface), [clients](docs/client.md)):
  - JSON calls: a line starting with `{` is `{verb, args}` with named
    arguments, checked against the row like a typed line; literal refs
    only (`$N` is refused). `batch` takes a list of calls.
  - Every argument in the table has a key, a type and a default;
    `int` and enum values are checked before the parser runs. `help
    --json` adds `types` (named types such as `Chain`, `Object`,
    `ue.Name`), `origins` (each with its title) and `blocks` on `watch`
    and `record`. `Object` is a core type a pack fills (`peek`'s rows).
  - `reagentlive_gen.py` writes typed Python functions for one console,
    a host's verbs included, refusing to run against another table.
    The session's hand-written per-verb methods and its paging helpers
    (`find_all`, `search_addresses`, ...) are gone; `s.call(verb, args)`
    sends any verb.
  - `frontends/mcp/reagentlive_mcp.py` serves those functions as MCP tools
    (needs the MCP Python SDK), read-only or destructive per `writes`;
    blocking verbs are left out, and a tool error carries the console's
    whole error object (a refused `write --expect` keeps `found`).
    Checked live in an AI client: read-only lookups, guarded writes, pins,
    trials and engine calls.
  - Hosts: `ConsoleConfig::hostCommand` takes the verb's row and its
    checked `Args` in place of the line; rows give keys and types.

- **The console describes itself; every caller breaks once.** One verb
  table says what each command takes and returns
  ([design](docs/tool-interface.md#self-describing-console)):
  - `help` lists the verbs, `help <verb>` shows flags, arguments and the
    result's keys, `help --json` prints the table (no target needed);
    `status` carries `table_hash`. A usage error ends with the verb's
    usage line; unknown flags are refused.
  - Modes are verbs: `sig-check`, `sig-file`, `sig-bytes`, `watch-start`,
    `watch-read`, `watch-stop`, `pin-list`, `pin-clear`, `trial`,
    `trial-list`, `trial-clear`, `census-diff`, `script`. `peek --label`
    names with `--name`.
  - Result keys: a result's list is `rows`; pages carry `from` and
    `next_cursor`; `capped` (rows cut) and `truncated` (a value
    incomplete) each mean one thing; keys that do not apply are omitted
    (no nulls); counts that repeated `rows` are gone, `total` counts all
    matches. `read --as "<layout>"` fills `layout`.
  - Results are typed structs written from one field list, checked
    against their row before printing; `docs/commands/results.md` is
    generated from the table.
  - The Python client's results are strict (`ResultKeyError` on a key the
    type lacks) with one method per verb; reshaping helpers are named for
    what they return.
  - `ConsoleConfig::hostHelp` is gone: a host gives rows
    (`ConsoleConfig::verbs`) for its own verbs.

- The console keeps the core and the Unreal pack apart: each `frontends/cli`
  layer has a `ue/` folder for the pack's part, the core reaches the pack
  only through `console/pack.h`, and the layout test enforces both.
- `status` lists the packs the host offers (`packs`); a pack's verbs on a
  host without it are refused with a message that says so; `help` groups
  the verbs into core and Unreal pack.
- `Ref` moved to the core protocol. The cache file keeps each pack's state
  in its own block (format 8; older files are dropped).
- `get` resolves every path, then reads the values back to back, so they
  land in one host tick (`same_tick`) instead of splitting over the agent's
  hold wait; warm field tables are shared instead of copied.
- Fixed: an address-only `$N` (a pointer `get` followed) made the next run
  drop the whole cache file; it is now stored as its address.

## 0.1.0

First versioned release.

- In-process agent with a shared-memory transport: reads, same-tick
  batches, guarded writes, pins, trial writes, `watch` captures (blocking,
  detached, following a root), `search` and `region`.
- Unreal pack: object lookup, reflected fields, containers, calls with kept
  results, `census`, `each`, `name-find`, bytecode `refs`, labels for a
  static tool.
- Fast lane: `disasm`, `xrefs`, `touches`, `sig --check` and `sig --bytes`
  from live bytes, with function bounds from the exception directory.
- Native objects: `vtable`, pointer chains, strings, `region`.
- Console with JSON Lines output, `batch`, `get`, a cache file between runs,
  and quoted `search text`; Python session client; IDA label importer.
- Own-thread example host and generic console: memory, the fast lane and
  native objects in any x64 process.
- The `reagentlive` agent skill.
