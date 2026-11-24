# Design principles

*Home of:* the principles ReAgentLive is built on, its goals and non-goals, and what is deliberately not built now.

**TL;DR:** The rules every design decision follows: read freely but never
write code, let the host own the thread, one console as the command layer,
and grow only when a real project needs it. Check here before proposing a
feature: the non-goals and the "not now" list say what was ruled out. Part
of [design](README.md).

## Principles

- **Read freely; never write code.** The line is invasiveness. Reading and
  analysing live memory of any size is in scope; hooks or patches in
  executable code and debugging (breakpoints, debug registers, exception
  handlers in the target) are out for good. Writes to data are opt-in and
  approved one by one.
- **The core never hooks, and starts no threads unless the host opts in.**
  The host calls `Tick()` from a thread it trusts. The one exception is
  the search worker, allowed only by `Host::backgroundSearch`: a single
  thread that runs search jobs, the console's internal scans and image
  copies (reads only, no engine calls) while the host tick only polls.
- **Two opt-in pointer swaps, never code.** The Unreal pack's `calls-start`
  swaps one `UFunction`'s native pointer (heap data) for a thunk that records
  and forwards each call, and puts it back with a compare-and-swap; "what did
  the game pass to this function" has no read-only route. The host helper
  `reagentlive::ue-draw` points the game viewport object at a private copy of
  its interface vtable ([decision](unreal.md#a-game-thread-tick-through-the-viewports-draw)).
  A host opts in by listing the ops, and each is a write the host approves.
  Code, the game's own vtables and imports are never written.
- **One safety boundary, set by a person, held by the console:** every
  verb that writes or calls is marked and sits in the `modify` pack, and
  the console's `--packs` ceiling refuses it, whichever frontend sends it
  ([tool packs](../tool-interface.md#tool-packs)).
- **Extensible by static tables, not plugins:** agent handlers and CLI verbs
  are constant arrays; op ranges separate core, packs and games.
- **One command layer:** the console. It sits on the C++ client API,
  which sits on the transport; Python and MCP drive the console's JSON
  calls, and only the console and raw C++ callers link the client. No
  frontend touches the wire protocol.
- **One transport** (shared memory). No transport interface until a second
  real transport exists.
- **Grow on demand:** a pack, frontend or feature is added when a real
  project needs it.

## Goals and non-goals

**Goals:** drop the core into any Windows process that has a safe place to
call `Tick()`; add an engine through a pack and a game through host glue,
without touching core; work under any injection style (mapped image,
LoadLibrary, ASI loader).

**Non-goals:** other platforms, network transport, protocol negotiation,
plugin DLLs, scripting languages, packs for engines nobody is using yet,
use against online or anti-cheat-protected games.

## Not now

Each is added when a real second case exists. The single home of what is
out of scope; [ideas](../ideas.md) holds what waits for a trigger.

- A transport interface, or a channel split out of agent and client.
- Async/futures/callbacks in the client API.
- A C ABI or native Python bindings over `Client` (the Python client drives
  the console instead).
- A GUI frontend until the CLI stops being enough.
- Plugin DLLs or runtime loading of packs.
- Protocol negotiation: `ProtocolRevision` is folded into the layout sum,
  and the supported-op list advertises ops ([handshake](../protocol.md#handshake)).
- Packs for other engines until a real project needs one.
- Package-manager packaging (`install()`, `find_package`): the library is
  its own repository, consumed as a submodule or with `FetchContent` at a
  pinned tag, each consumer compiling its sources with its own flags
  ([hosting](../hosting.md#using-it-in-your-project)).
