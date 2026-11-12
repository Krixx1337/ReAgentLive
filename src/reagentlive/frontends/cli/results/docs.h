// reagentlive CLI: docs/commands/results.md as text, generated from what
// `help --json` prints (the verb table), so the page cannot drift from the
// console. tests/table writes it and a test fails when the file is stale. Pure.
#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/values/json.h"

namespace reagentlive::cli {
// `table`: the result of `help --json` ({rows: [{verb, pack, usage, summary, result, ...}]}).
inline std::string ResultsMarkdown(const Json& table) {
    const auto cell = [](std::string text) {
        // A table cell: `|` would end it.
        for (size_t at = 0; (at = text.find('|', at)) != std::string::npos; at += 2) text.insert(at, "\\");
        return text;
    };
    const auto heading = [](const std::string& origin) {
        return origin == "core" ? std::string("Core") : origin == "ue" ? std::string("Unreal pack") :
            origin == "host" ? std::string("Host") : origin;
    };
    std::string out =
        "# Result shapes\n\n"
        "<!-- Generated from `help --json` (tests/table: `reagentlive-table --write docs/commands/results.md`). "
        "Do not edit: change the verb's row or its result struct. -->\n\n"
        "What each command takes and the keys of its `result`, as the console itself reports them:\n"
        "`help` lists the verbs, `help <verb>` shows one (flags, result, notes) and `help --json` prints\n"
        "this whole table for programs. `status` carries `table_hash`, which changes whenever a row does.\n\n"
        "## Notation\n\n"
        "| Type | Meaning |\n|---|---|\n"
        "| `addr` | an address as `\"0x...\"` text |\n"
        "| `hex` | a number as `\"0x...\"` text: an rva, an offset, a size |\n"
        "| `bytes` | `\"01 02 ff\"`, in memory order |\n"
        "| `ref` | an object ref; pass it verbatim to another command, a separate run included |\n"
        "| `bool`, `u32`, `u64`, `i32`, `i64`, `f64`, `str` | plain JSON values |\n"
        "| `a\\|b` | a string that is one of the listed words (`...`: others occur) |\n"
        "| `[T]` | a list; `[T\\|null]`: elements may be null |\n"
        "| `{<key>: T}` | a map |\n"
        "| `{k: T, k?: T}` | an object; `?` marks a key that is omitted when it does not apply |\n"
        "| `value` | game data: dynamic, shaped by what was read |\n\n"
        "## Conventions\n\n"
        "- `rows` is a result's list; `total` is how many there are in all when `rows` may show fewer.\n"
        "- A page carries `from` (where it began) and `next_cursor` (pass it as `--from`; absent when done).\n"
        "- `capped`: the rows were cut at a hard limit and there is no cursor. `truncated`: a value in the\n"
        "  result is incomplete.\n"
        "- `module` and `rva` stand beside an address inside a loaded module, so it opens at the same RVA\n"
        "  in a static-analysis tool; other prefixes (`value_`, `target_`) locate the address in that key.\n"
        "- A key that does not apply is omitted; results hold no nulls outside game values.\n"
        "- A flag may add optional keys and never changes a key's type.\n"
        "- **Writes or calls.** marks a verb that writes the target's memory or calls into it; every other verb\n"
        "  only reads (`help --json` carries it as `writes`).\n";
    std::string origin;
    for (const auto& row : table.at("rows")) {
        const auto rowOrigin = row.at("origin").get<std::string>();
        if (rowOrigin != origin) {
            origin = rowOrigin;
            out += "\n## " + heading(origin) + "\n\n| Command | What it does | `result` |\n|---|---|---|\n";
        }
        out += "| `" + cell(row.at("usage").get<std::string>()) + "` | " + cell(row.at("summary").get<std::string>()) +
            (row.value("writes", false) ? " **Writes or calls.**" : "") +
            " | `" + cell(row.at("result").get<std::string>()) + "` |\n";
    }
    out +=
        "\n## Envelopes\n\n"
        "Every line is an envelope: `command`, `ok`, and `result` or `error`. Errors carry `kind` and\n"
        "`message`; client errors (`agent`, `timeout`, `attach`, `busy`) also carry `status`, `detail` and\n"
        "`reason`. A refused `write --expect` adds `found`; a broken pointer chain adds `chain` with\n"
        "`broken` and `reason`. Any envelope can carry `timing` when the line ended in `--timing`. A\n"
        "usage error ends with the verb's usage line. A result that does not match its row's type is an\n"
        "`internal` error, never printed as a result.\n\n"
        "## Game values\n\n"
        "A `value` holds what the game's data is. Addresses are hex strings, null for a null pointer.\n"
        "FNames carry `index`, `number` and `name`. Structs expand to `{Name: value}` (`members` with\n"
        "`--meta`). Arrays and strings show their header, and `dump` adds a `preview` for top-level\n"
        "containers. Unsupported layouts give a bounded `hex` prefix. A non-finite float is\n"
        "`{\"non_finite\": \"nan\"}`. Check `truncated` on the result and its rows before treating values as\n"
        "complete.\n";
    return out;
}
}
