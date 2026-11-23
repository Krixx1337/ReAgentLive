# Design: native code

*Home of:* why ReAgentLive reads native code the way it does: light reversing and where the static tool takes over, the fast lane, signatures and the fixed labels signer, native objects, search jobs; with the rejected alternatives.

**TL;DR:** ReAgentLive answers what only a running process can, from live bytes
and in well under a second, and hands whole functions, types and call
graphs to a static tool. It checks signatures rather than making them,
except for one fixed loop. Part of [design](../design.md).

## Light reversing

- **Scope:** ReAgentLive answers what only a running process can (the value,
  object or function at a live address, what a global holds once the engine
  filled it, which implementation a live object dispatches to). A static
  tool answers callers, references, whole functions and types. The bridge
  is `module` and `rva` both ways, `peek` for live globals, and
  `export-labels` for names.
- **No decoder in the agent:** it would grow the injected image for
  display-only work. The agent reads bytes; the console decodes them.
- **Signatures: the agent makes them, ReAgentLive verifies them.** One
  exception, below: the labels mass signer. Making one is judgement (which instructions are distinctive, which operands move,
  whether a caller is the better place) that an agent does from the static
  tool's disassembly without a generator. What it cannot know alone is the
  result in the image the host really scans, DLLs with no database
  included, so `sig-check` is a strict checker whose feedback names the
  next correction: `mismatches` at the expected address, `diverges_at` per
  extra hit, `warnings` for unmasked addresses (a few byte patterns, not a
  decoder), `resolve` for reference signatures, `file` for many at
  once, and `sig-bytes` as a start without a static tool. With a boundary
  the reader gives (`expect`, `sig-bytes`) the warnings and masks come from
  the console's decoder, and the byte patterns only past where decoding
  stops. It runs in the
  console on the exact `search` (anchor) and `read-batch` (mask compare);
  the agent is unchanged. Unique means one match in the whole module image,
  stricter than a host scanner that reads only code. Rejected: a signature
  maker for signatures that matter, in the console or as a static-tool
  script (two earlier drafts had one each; the checker's feedback covers the
  mistakes a generator would prevent; revisit a script only if hand-made
  signatures keep needing many attempts), wildcards in the agent's `search` (the anchor plus a masked
  compare needs none; take it up only if a masked search over the heap, a
  struct found by shape, is wanted twice), scanning the exe on disk (the
  static tool and a host's offline verifier do that) and writing into
  host source files (the agent or the user pastes).
- **The labels mass signer** is the one exception ([below](#labels-sign-is-one-fixed-loop)).
- **Rejected:** call graphs, decompiling, types and naming in ReAgentLive (the
  static tool has the whole image); symbol loading; ReAgentLive talking to the
  static tool directly (the agent driving both is the bridge).


## labels-sign is one fixed loop

`labels-sign` signs a
labels file's names so `labels-port` can find them after a patch: cheap,
the same every time, and a miss costs one re-find, so quality is not worth
judgement there. The line: ReAgentLive checks, stores and ports
signatures, and the only ones it makes come from that loop. Rejected:
anything that makes its signatures better (later starts, caller
fallbacks, ranking, looser masks, a generator's tiers), and a way to store
a hand-made one (a pattern option; the row is one JSON line and
`sig-check` with `file` checks it). A name the loop cannot sign is signed by hand or left
unsigned; a failure is never a reason to extend the loop.

## Fast lane

A static tool answers a question about one address only after analysing the
whole image: hours for a large game, again after every patch, and longer on
a packed image whose exception directory hides or fakes functions. The fast
lane answers code questions from live bytes in well under a second, with no
database:

| Question | Command |
|---|---|
| What are the instructions here, or the whole function? | `disasm` (`function`) |
| What points here? | `xrefs` |
| Which code near here uses these member offsets? | `touches` |
| Is this pattern unique, and does it land here? | `sig-check` |
| Decompile this function and what it calls | `fetch` in IDA: `disasm` with `function` and `save` (`callees`), added and decompiled in one call |

- **The fence is invasiveness, not size.** Anything that reads live bytes
  and hands its result on is in scope, however much analysis it does:
  following calls and jumps, naming from a labels file, bounded sweeps.
  Out for good: hooks or patches in executable code, and debugging
  (breakpoints, debug registers, exception handlers in the target); see
  [principles](../design.md#principles). What the static tool already does well
  (decompiling, types, whole-program call graphs) is borrowed, not rebuilt.
- **A whole function and its constants** (`disasm` with `function`): decoded
  linearly to its entry's end, with the values its read-only loads read
  and the strings it points at. Reading every instruction by hand cost more
  calls than any other step in a live session.
- **Saves for the static tool** (`save`): the bytes and constants of a
  function, its chunks (chained entries, switch targets) and, with `callees`, every function it
  calls, named from a labels file; a small IDA loader builds one database
  from them and `add_saved` grows it. The decompiler is borrowed: a
  database of the whole image takes hours, a save seconds. Measured on a
  packed image: a handler and 16 callees in one save (0.75 s) and three tool
  calls to readable pseudocode, against about forty for seven functions one
  at a time. `fetch` makes it one call from IDA (the console's save,
  `add_saved`, `decompile_to`), and the save carries the layouts of its
  methods' classes, so `this->Field` shows in the first decompile. Callees
  stop at 3 levels inside the save's caps: deeper is a call graph, the
  static tool's job.
- **Function bounds** come from the exception directory, a table lookup.
  Code it leaves out is never called a function: `xrefs` and `touches`
  label it `unlisted`, with the bounds of the piece between returns and
  padding. Packed images fake the directory (one-byte decoys at a fixed
  stride, entries a few bytes before the code, functions split into chained
  chunks); each trick is handled where it was met, with the evidence on the
  command page.
- **Sweeps:** bounded, read-only ones are in (`touches` over a range, the
  piece around one address). An image-wide sweep (every vtable, every
  function) stays a script over the Python client: it is the one-address
  chain run thousands of times, and its output is a file, not a reply.
- **Where it lands.** Only the relative-reference scan behind `xrefs` is a
  core agent op (with a client call), since it reads memory only and works
  with any host. The decoder (Zydis, pinned, formatter on) links into the
  console alone; a configure check fails when an agent target links it.
  Kinds, bounds, pieces, saves and `sig` feedback are console work.
- **Not built, no case yet:** structured operands, struct recovery from
  offsets, and walking backwards from an unrecognised reference to its
  instruction start (unreliable on x86 without analysis; `disasm`
  with `function` shows it in place). **Out for good:** hooking a function to
  log its callers (a code hook; `xrefs` gives the static callers, and
  `calls-start` records reflected calls through a pointer swap).

## Native objects

Much of a game is native and unreflected even in a reflected engine (value
objects behind a handle, interface parts, engine internals), and a game
with no engine pack is nothing else. The engine-agnostic side therefore
answers what a native object is from live bytes, without a type system.

- **`vtable`** lists slots until the first entry outside an executable
  section of a loaded module. The section table is read once per module,
  by the same header reader as the fast lane's function bounds, and cached.
  A table is never inferred from neighbouring bytes.
- **Pointer chains are a client library call**, so an outside program
  follows a chain of offsets the same way the console does; the console only reads
  the `{"base", "chain"}` value. Writes, holds and undo writes refuse chains: a write goes where the
  caller read, never through pointers that may have moved since.
- **Strings** (`read`'s `type` `cstr` or `wstr`) escape what they cannot print, so a dump
  never hides a byte.
- **`region` is a core op** because only the target can ask Windows about
  its own memory; the agent passes `VirtualQuery`'s numbers through and the
  console names them, as the `region` that `read` and `peek` add. Not a verb
  of its own: every question it answered started from a read or a peek. A stack is not claimed: private read-write memory is
  `private`, since telling a thread's stack from a heap needs every
  thread's limits.
- **Calls are opt-in and deferred.** `call-native` (the one item that can
  crash the target) waits for a live case a read cannot replace, or an
  outside program on a game with no engine pack
  ([backlog](../ideas.md#17--call-native-caution)); a host will enable it only where engine
  calls are safe, and approval stays the host's rule. Out for good: code
  hooks, native tracing and breakpoints (the call recorder swaps a
  `UFunction` pointer, never code); calling by symbol name (shipping images have
  none); floating-point arguments and callbacks until a case needs them.
- **`vtable`'s `rtti`: class names where the image has them.** Declined while the
  one host checked (an engine game) had none for its game classes; built
  because a target with no engine pack is the case native objects exist
  for, and most native programs and libraries are built with type
  information. It is a one-address answer from bytes (locator, descriptor,
  hierarchy), console only, and refuses instead of guessing when the slot
  before the table holds no locator. An arg of `vtable`, not a verb: the
  same input and the same question. Not used to label `peek` rows: one more
  read per row for what one `vtable` with `rtti` answers.
- **Rejected:** a struct or type system in ReAgentLive (layouts come from
  the engine pack, the host's SDK or the static tool; `read`'s `type`
  covers ad hoc ones).


## Search jobs

- **Every search is a job.** A blocking search over the whole address
  space ran for minutes, timed out and lost its work, holding the
  console's one request slot meanwhile. Now the agent keeps one job,
  scanned on the worker or between requests, and the console polls it up
  to `wait`; the default range is the main module. Rejected: a job
  option beside the blocking form (two result shapes for one verb); a job
  kept in the console (gone when a one-shot console exits, and the
  agent's slot still busy); separate status and stop verbs (one
  `search-job` with `stop`). The console's own scans (`xrefs`, `sig-check`,
  `labels-sign`) stay blocking: they cover one module and need every page.
