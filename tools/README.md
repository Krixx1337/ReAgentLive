# Tools

**TL;DR:** Scripts you run from a clone, by hand or through an agent
procedure. Nothing here is built into the agent or loaded into the target.
Each script's header comment gives its usage; the page linked for each one
explains why it exists and when to run it.

| Script | What | Who runs it | Explained in |
|---|---|---|---|
| `build_examples.ps1` | builds the examples in Visual Studio's x64 environment; with `-Check`, loads each DLL into a throwaway Python process and checks its `status` | the [setup](../docs/agents/setup.md#3-clone-and-build) and [update](../docs/agents/update.md#3-pull-and-rebuild) procedures | [setup](../docs/agents/setup.md#7-verify) |
| `ida_setup.py` | checks whether IDA's side of the [IDA loader](../docs/commands/ida-loader.md) is set up; with `--install`, sets it up | the user once, or the setup and update procedures | [IDA loader](../docs/commands/ida-loader.md) |
| `fetch_timing.py` | times the IDA loader's loop, hop by hop, and counts the databases an IDA MCP session opened | a developer measuring the IDA loop | [IDA loader](../docs/commands/ida-loader.md) |

A new script goes here when a person or a procedure runs it from the
clone. Code that ships in the console, the frontends or a test belongs
under `src/` or `tests/` ([contributing](../docs/contributing.md)).
