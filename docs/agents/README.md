# ReAgentLive agent procedures

Instructions an AI agent follows on the user's machine; humans: see the
[README](../../README.md).

## Which procedure

| The user wants | Follow |
|---|---|
| ReAgentLive in a project: the user's own (a mod), a new one from an example, a fresh clone of a project wired earlier, or replacing an install from before projects (`%LOCALAPPDATA%\reagentlive\setup.json`) | [setup.md](setup.md) |
| An update of the ReAgentLive in a project (its `reagentlive/` submodule) | [update.md](update.md) |

The project record's fields: [setup.md](setup.md#4-wire-the-agent);
the machine record's (the injector, the IDA side, the improvement
mode): [setup.md](setup.md#1-check-the-machine).

## Rules every procedure shares

1. Run every step yourself; ask the user only where a step says to.
2. Ask before installing software. Write outside the project only where a
   step names the file (the machine record, the injector, an MCP config,
   the IDA side); ask before any other write there.
3. Never edit the project's `reagentlive/` submodule: an update moves its
   commit.
4. End with the procedure's report block.
