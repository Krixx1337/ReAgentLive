# ReAgentLive design

**TL;DR:** Read this before proposing a change: most "why not just..."
ideas were tried or weighed here, and the entry says why they lost. Each
decision states what was chosen, what was rejected and the evidence. It
describes reasons, not behaviour: what a command does lives on its command
page.

Current behaviour lives in the [command pages](../commands/README.md),
[protocol](../protocol.md) and [hosting](../hosting.md); the ranked backlog in
[ideas](../ideas.md).

| Page | Decisions |
|---|---|
| [principles](principles.md) | the principles, goals and non-goals, and what is not built now |
| [console costs](console-costs.md) | round trips and holds, output weight and dead ends, the usage log |
| [architecture](architecture.md) | place among other tools, layers, frontends, source layout |
| [Unreal pack](unreal.md) | layouts and globals, thread kinds, off-thread reads, calls, class index, stock layout groups, the Draw tick |
| [native code](native.md) | light reversing, signatures and the fixed labels signer, the fast lane, native objects, search jobs |
| [tool interface](../tool-interface.md) | the verb table, JSON-only calls, generated functions, MCP tools, tool packs |
