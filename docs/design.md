# ReAgentLive design

*Home of:* why ReAgentLive is built this way: its principles, goals and what is deliberately not built; the index of the design pages, which hold each decision with its rejected alternatives.

**TL;DR:** Read this before proposing a change: most "why not just..."
ideas were tried or weighed here, and the entry says why they lost. Each
decision states what was chosen, what was rejected and the evidence. It
describes reasons, not behaviour: what a command does lives on its command
page.

Current behaviour lives in the [command pages](INDEX.md#commands),
[protocol](protocol.md) and [hosting](hosting.md); the ranked backlog in
[ideas](ideas.md).

| Page | Decisions |
|---|---|
| [console costs](design/console-costs.md) | round trips and holds, output weight and dead ends, the usage log |
| [architecture](design/architecture.md) | place among other tools, layers, frontends, source layout |
| [Unreal pack](design/unreal.md) | layouts and globals, thread kinds, off-thread reads, calls, class index, stock layout groups, the Draw tick |
| [native code](design/native.md) | light reversing, signatures and the fixed labels signer, the fast lane, native objects, search jobs |
| [tool interface](tool-interface.md) | the verb table, JSON-only calls, generated functions, MCP tools, tool packs |

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
- **Two opt-in pointer swaps, never code.** The Unreal pack's
  `calls-start` swaps one `UFunction`'s native pointer (heap data) for a
  fixed thunk that records and forwards each call, and puts it back with a
  compare-and-swap; a pointer something else replaced since is left alone
  and the thunk keeps forwarding. A host opts in by listing the ops; each
  recording is a write the host approves. Taken because "what did the game
  pass to this function" has no read-only route, a watch sees only state,
  and the swap touches nothing the engine does not itself rewrite. The
  second is the host helper `reagentlive::ue-draw`: when `game-thread` arms it, it
  writes one pointer in the game viewport object, pointing it at a private
  copy of its interface vtable
  ([decision](design/unreal.md#a-game-thread-tick-through-the-viewports-draw)). Code, the
  game's own vtables and imports are never written by either.
- **One safety boundary, set by a person, held by the console:** every
  verb that writes or calls is marked and sits in the `modify` pack, and
  the console's `--packs` ceiling refuses it, whichever frontend sends it
  ([tool packs](tool-interface.md#tool-packs)).
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

**Goals**

- Drop the core into any Windows process that has a safe place to call
  `Tick()`.
- Add an engine through a pack and a game through host glue, without
  touching core.
- Work under any injection style: mapped image, LoadLibrary or ASI loader.

**Non-goals**

- Other platforms, network transport, protocol negotiation, plugin DLLs,
  scripting languages.
- Packs for engines nobody is using yet (Unity/IL2CPP, Source, custom).
- Use against online or anti-cheat-protected games.

## Not now

- A transport interface, or a channel split out of agent and client (add
  either with a second real transport).
- Async/futures/callbacks in the client API.
- A C ABI or native Python bindings over `Client` (the Python client drives
  the console instead).
- A GUI frontend until the CLI stops being enough.
- Plugin DLLs or runtime loading of packs.
- Protocol negotiation: `ProtocolRevision` is folded into the layout sum,
  and the supported-op list advertises ops ([handshake](protocol.md#handshake)).
- Packs for other engines until a real project needs one.
- Package-manager packaging (`install()`, `find_package`): the library is
  its own repository, consumed as a submodule or with `FetchContent` at a
  pinned tag, and each consumer compiles its sources with its own flags
  ([hosting](hosting.md#using-it-in-your-project)).
