# ReAgentLive agent procedures

Instructions an AI agent follows on the user's machine; humans: see the
[README](../../README.md).

## Which procedure

| The user wants | Follow |
|---|---|
| A first install (no `%LOCALAPPDATA%\reagentlive\setup.json`) | [setup.md](setup.md) |
| An update of an existing install | [update.md](update.md) |

The setup record's fields: [setup.md](setup.md#10-write-the-setup-record).

## Rules every procedure shares

1. Run every step yourself; ask the user only where a step says to.
2. Ask before installing software. Write outside the clone only where a
   step names the file (MCP config, skill link, setup record); ask before
   any other write there.
3. End with the procedure's report block.
