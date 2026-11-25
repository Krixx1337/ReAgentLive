# ReAgentLive agent procedures

Instructions an AI agent follows on the user's machine; humans: see the
[README](../../README.md).

## Which procedure

| The user wants | Follow |
|---|---|
| A new project from a kit, a fresh clone of a project made earlier, or to replace an install from before projects (`%LOCALAPPDATA%\reagentlive\setup.json`) | [setup.md](setup.md) |
| An update of the ReAgentLive in a project (its `reagentlive/` submodule) | [update.md](update.md) |

The project record's fields: [setup.md](setup.md#3-create-the-project).

## Rules every procedure shares

1. Run every step yourself; ask the user only where a step says to.
2. Ask before installing software. Write outside the project only where a
   step names the file (an MCP config, the IDA side); ask before any other
   write there.
3. Never edit the project's `reagentlive/` submodule: an update moves its
   commit.
4. End with the procedure's report block.
