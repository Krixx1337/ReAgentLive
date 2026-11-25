# Tools

**TL;DR:** Scripts you run from a clone or a project's `reagentlive/`
submodule, by hand or through an agent procedure. Nothing here is built
into the agent or loaded into the target. Each script's header comment
gives its usage; the page linked for each one explains why it exists and
when to run it.

| Script | What | Who runs it | Explained in |
|---|---|---|---|
| `agent_link.py` | wires an AI agent to a host project: the project record, the MCP files, an `AGENTS.md` block, the skill links, the machine's list of projects; run again, it updates them | the [setup](../docs/agents/setup.md#4-wire-the-agent) procedure, or the user | [an agent in your project](../docs/hosting.md#an-agent-in-your-project) |
| `build_project.ps1` | builds a project (the record's `build`, or CMake) in Visual Studio's x64 environment; with `-Check`, loads its DLL into a throwaway Python process and checks its `status` | the [setup](../docs/agents/setup.md#5-build-and-check) and [update](../docs/agents/update.md#4-update) procedures, or the user | [setup](../docs/agents/setup.md#5-build-and-check) |
| `update.ps1` | updates a project's submodule: a preview that only reads (a person is then asked whether to apply), `-Apply` (move, rebuild, check) and `-Rollback` | the [update](../docs/agents/update.md) procedure, or the user | [update](../docs/agents/update.md#2-show-what-is-coming) |
| `ida_setup.py` | checks whether IDA's side of the [IDA loader](../docs/commands/ida-loader.md) is set up; with `--install`, sets it up | the user once, or the setup and update procedures | [IDA loader](../docs/commands/ida-loader.md) |
| `fetch_timing.py` | times the IDA loader's loop, hop by hop, and counts the databases an IDA MCP session opened | a developer measuring the IDA loop | [IDA loader](../docs/commands/ida-loader.md) |

A new script goes here when a person or a procedure runs it from the
clone. Code that ships in the console, the frontends or a test belongs
under `src/` or `tests/` ([contributing](../docs/contributing.md)).
