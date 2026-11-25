# Design: native code

*Home of:* why ReAgentLive reads native code the way it does: light reversing and where the static tool takes over, the fast lane, signatures and the fixed labels signer, native objects, search jobs; with the rejected alternatives.

**TL;DR:** ReAgentLive answers what only a running process can, from live bytes
and in well under a second, and hands whole functions, types and call
graphs to a static tool. It checks signatures rather than making them,
except for one fixed loop. Part of [design](README.md).

## Light reversing

- **Scope:** ReAgentLive answers what only a running process can (the value,
  object or function at a live address, what a global holds once the engine
  filled it, which implementation a live object dispatches to). A static
  tool answers callers, references, whole functions and types
  ([build or borrow](architecture.md#place-among-other-tools)). The bridge
  is `module` and `rva` both ways, `peek` for live globals, and
  `labels-export` for names.
- **No decoder in the agent:** it would grow the injected image for
  display-only work. The agent reads bytes; the console decodes them.
- **Signatures: the agent makes them, ReAgentLive verifies them.** Making
  one is judgement (which instructions are distinctive, which operands move,
  whether a caller is the better place) that an agent does from the static
  tool's disassembly. What it cannot know alone is the result in the image
  the host really scans, so `sig-check` is a strict checker whose feedback
  names the next correction ([fast lane](../commands/fast-lane.md)). It runs
  in the console on the exact `search` and `read-batch`; the agent is
  unchanged. Unique means one match in the whole module image, stricter than
  a host scanner that reads only code. Rejected: a signature maker for
  signatures that matter, in the console or as a static-tool script (two
  earlier drafts; the checker's feedback covers the mistakes a generator
  would prevent); wildcards in the agent's `search` (anchor plus masked
  compare needs none); scanning the exe on disk (the static tool and a
  host's offline verifier do that); writing into host source files.
- **The one exception is the labels mass signer**
  ([below](#labels-sign-is-one-fixed-loop)).
- **Rejected:** call graphs, decompiling, types and naming in ReAgentLive;
  symbol loading; ReAgentLive talking to the static tool directly (the agent
  driving both is the bridge).

## labels-sign is one fixed loop

`labels-sign` signs a labels file's names so `labels-port` can find them
after a patch: cheap, the same every time, and a miss costs one re-find, so
quality is not worth judgement there. The line: ReAgentLive checks, stores
and ports signatures, and the only ones it makes come from that loop.
Rejected: anything that makes its signatures better (later starts, caller
fallbacks, ranking, looser masks, a generator's tiers), and a way to store
a hand-made one (the row is one JSON line and `sig-check` with `file` checks
it). A name the loop cannot sign is signed by hand or left unsigned; a
failure is never a reason to extend the loop.

## Fast lane

A static tool answers a question about one address only after analysing the
whole image: hours for a large game, again after every patch, and longer on
a packed image whose exception directory hides or fakes functions. The fast
lane answers code questions from live bytes in well under a second, with no
database:

| Question | Command |
|---|---|
| What are the instructions here, or the whole function? | `disasm` (`whole`) |
| What points here? | `xrefs` |
| Which code near here uses these member offsets? | `touches` |
| Is this pattern unique, and does it land here? | `sig-check` |
| Decompile this function and what it calls | `fetch` in IDA: `disasm` with `whole` and `save` (`callees`), added and decompiled in one call |

- **The fence is invasiveness, not size.** Anything that reads live bytes
  and hands its result on is in scope, however much analysis it does:
  following calls and jumps, naming from a labels file, bounded sweeps.
  Out for good: hooks or patches in executable code, and debugging
  (breakpoints, debug registers, exception handlers in the target); see
  [principles](principles.md#principles). Decompiling, types and
  whole-program call graphs are borrowed, not rebuilt.
- **A whole function and its constants** (`disasm` with `whole`): decoded
  linearly to its entry's end, with the values its read-only loads read and
  the strings it points at. Reading every instruction by hand cost more
  calls than any other step in a live session.
- **Saves for the static tool** (`save`): a function's bytes, constants and
  chunks and, with `callees`, every function it calls, named from a labels
  file; a small IDA loader builds one database from them. The decompiler is
  borrowed: a database of the whole image takes hours, a save seconds, and
  `fetch` makes it one call. Callees stop at a fixed depth: deeper is a call
  graph, the static tool's job.
- **Function bounds** come from the exception directory, a table lookup.
  Code it leaves out is never called a function: `xrefs` and `touches` label
  it `unlisted`. Packed images fake the directory; each trick is handled
  where it was met, with the evidence on the command page.
- **Sweeps:** bounded, read-only ones are in (`touches` over a range,
  `disp-scan` over the code in pages). An image-wide sweep of one-address
  questions (every vtable, every function) stays a script over the Python
  client: its output is a file, not a reply.
- **Whole images through the image window.** Reading code 4 KB per held
  request through host ticks was too slow for a game's whole image, and a
  search per anchor worse. `disp-scan` and `labels-check` now read the
  image through a second mapping, a block per request, copied by the search
  worker when the host allows it (tick slices otherwise), and match in the
  console. The copy is limited to image pages, which do not change under a
  read except data sections, so the rule against off-tick reads (torn
  values) still holds for values. Rejected: decoding in the agent, a byte
  prefilter in the agent (an instruction's start is unknown from a
  displacement's bytes), a larger `Response` (every request would carry it),
  and `ReadProcessMemory` ([console costs](console-costs.md#round-trips)).
- **Where it lands.** Only the relative-reference scan behind `xrefs` is a
  core agent op, since it reads memory only and works with any host. The
  decoder (Zydis, pinned) links into the console alone; a configure check
  fails when an agent target links it.
- **Not built, no case yet:** structured operands, struct recovery from
  offsets, and walking backwards from an unrecognised reference to its
  instruction start (unreliable on x86 without analysis). **Out for good:**
  hooking a function to log its callers (a code hook). Callers through
  virtual or pointer dispatch are in scope through a pointer swap
  ([native calls](#native-calls)).

## Native objects

Much of a game is native and unreflected even in a reflected engine, and a
game with no engine pack is nothing else. The engine-agnostic side
therefore answers what a native object is from live bytes, without a type
system; the commands are in [native objects](../commands/native-objects.md).

- **`vtable` lists slots until the first entry outside an executable
  section of a loaded module;** a table is never inferred from neighbouring
  bytes.
- **Chains are followed on reads only.** Writes, holds and undo writes
  refuse chains: a write goes where the caller read, never through pointers
  that may have moved since.
- **`region` is a core op** because only the target can ask Windows about
  its own memory; the console names the numbers. Not a verb of its own:
  every question it answered started from a read or a peek. A stack is not
  claimed: private read-write memory is `private`.
- **`vtable`'s `rtti`: class names where the image has them.** Declined while
  the one host checked had none; built because a target with no engine pack
  is the case native objects exist for. A one-address answer from bytes,
  console only, refusing instead of guessing when the slot before the table
  holds no locator. An arg of `vtable`, not a verb. Not used to label `peek`
  rows: one more read per row.
- **Calls are opt-in and deferred.** `call-native` waits for a live case a
  read cannot replace ([backlog](../ideas.md#17--call-native-caution)); a
  host will enable it only where engine calls are safe. Out for good: code
  hooks, native tracing and breakpoints (the call recorders swap a pointer
  in writable data, never code); calling by symbol name (shipping images
  have none).
- **Rejected:** a struct or type system in ReAgentLive (layouts come from the
  engine pack, the host's SDK or the static tool; `read`'s `type` covers ad
  hoc ones).

## Native calls

`native-calls-start` records the calls made through one code pointer: a
vtable entry of one object, or a cell (a field or a global in a writable
section) that holds a pointer into a module's code. `calls-read` reads it,
as it reads every recorder.

- **What it sees, and what not.** Only calls that load that pointer: a
  virtual call on that object, or a call through that cell. A direct call,
  a devirtualised or inlined one, and the same function called through
  another object's table never read it. The rows say what the call passed,
  not what it returned.
- **A vtable entry through a private copy.** The object's vptr is pointed
  at a copy of its table (`core/vtable_copy.h`) and the entry swapped
  there: the table stays read-only and shared, other instances are
  untouched, and several entries of one object coexist. Stopping the last
  one puts the vptr back by compare-and-swap.
- **A cell only in writable data.** Read-only, executable and guard pages,
  read-only image sections and the import address table are refused, as is
  a value that is not code in a loaded module or is already a thunk. No
  protection is ever changed.
- **One generic thunk per slot, in MASM** (x64 MSVC has no inline
  assembly): it saves rcx, rdx, r8, r9, rax, r10, r11 and xmm0-5, calls the
  recorder and jumps to the original with the caller's registers and stack
  exactly as they were, so any signature forwards, stack arguments and
  hidden return pointers included. It has unwind data, uses RIP-relative
  addresses only and imports nothing, so it fits mapped images. The upper
  halves of ymm registers are not saved: the recorder uses no vector
  instructions.
- **The recorder calls nothing.** No allocation, lock, log or system call:
  the thread id comes from the TEB, the time from the time-stamp counter
  (converted to microseconds against the performance counter on read). One
  optional copy of up to 64 bytes at an argument register runs under a
  fault guard. The deadline is the agent's tick sweep.
- **Rejected:** return values by rewriting the return address (breaks stack
  walks and unwinding; the thunk tail-jumps instead), and a C++ thunk per
  signature (the generic one needs no signature).

## Search jobs

- **Every search is a job.** A blocking search over the whole address space
  ran for minutes, timed out and lost its work, holding the console's one
  request slot meanwhile. Now the agent keeps one job, scanned on the worker
  or between requests, and the console polls it up to `wait`. Rejected: a
  job option beside the blocking form (two result shapes for one verb); a
  job kept in the console (gone when a one-shot console exits); separate
  status and stop verbs. The console's own scans (`xrefs`, `sig-check`,
  `labels-sign`) stay blocking: they cover one module and need every page.
  Image copies share the worker but never stop a running job.
