# Tools

**TL;DR:** Scripts you run from a clone or a project's `reagentlive/`
submodule, by hand or through an agent procedure. Nothing here is built
into the agent or loaded into the target. Each script's header comment
gives its usage; the page linked for each one explains why it exists and
when to run it.

| Script | What | Who runs it | Explained in |
|---|---|---|---|
| `new.ps1` | makes a project in one command: checks the machine, creates the folder and the submodule, runs `project.py init` and `build_project.ps1 -Check`; runs from the web before any clone exists, and asks a person for missing arguments | the [setup](../docs/agents/setup.md#3-create-and-build-the-project) procedure, or the user | [setup](../docs/agents/setup.md#3-create-and-build-the-project) |
| `project.py` | makes a project from a kit (`init`), or links a project's skill again (`link`) | the [setup](../docs/agents/setup.md#3-create-and-build-the-project) procedure, or the user | [starting from a kit](../docs/hosting.md#starting-from-a-kit) |
| `build_project.ps1` | builds a project in Visual Studio's x64 environment; with `-Check`, loads its DLL into a throwaway Python process and checks its `status` | the [setup](../docs/agents/setup.md#4-build-and-check) and [update](../docs/agents/update.md#4-update) procedures, or the user | [setup](../docs/agents/setup.md#4-build-and-check) |
| `update.ps1` | updates a project's submodule (or every project with `-All`): a preview that only reads (a person is then asked whether to apply), `-Apply` (move, rebuild, check) and `-Rollback` | the [update](../docs/agents/update.md) procedure, or the user | [update](../docs/agents/update.md#2-show-what-is-coming) |
| `ida_setup.py` | checks whether IDA's side of the [IDA loader](../docs/commands/ida-loader.md) is set up; with `--install`, sets it up | the user once, or the setup and update procedures | [IDA loader](../docs/commands/ida-loader.md) |
| `fetch_timing.py` | times the IDA loader's loop, hop by hop, and counts the databases an IDA MCP session opened | a developer measuring the IDA loop | [IDA loader](../docs/commands/ida-loader.md) |

A new script goes here when a person or a procedure runs it from the
clone. Code that ships in the console, the frontends or a test belongs
under `src/` or `tests/` ([contributing](../docs/contributing.md)).
