// reagentlive CLI: docs/commands/results.md as text, generated from what
// `help --json` prints (the verb table), so the page cannot drift from the
// console. tests/table writes it and a test fails when the file is stale. Pure.
#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/values/json.h"

namespace reagentlive::cli {
// A row's verb and arg keys, as the results page lists them: `read {addr, len?, type?}`
// (`?` marks an optional key, `[]` a list).
inline std::string Signature(const Json& row) {
    std::string keys;
    for (const auto& arg : row.value("args", Json::array())) {
        keys += (keys.empty() ? "" : ", ") + arg.at("key").get<std::string>();
        if (arg.value("list", false)) keys += "[]";
        if (!arg.value("required", false)) keys += "?";
    }
    return row.at("verb").get<std::string>() + (keys.empty() ? "" : " {" + keys + "}");
}

// `table`: the result of `help --json` ({rows: [{verb, origin, summary, args, result, ...}], types, origins}).
// Headings are the origins' own titles: this file names no pack.
inline std::string ResultsMarkdown(const Json& table) {
    const auto cell = [](std::string text) {
        // A table cell: `|` would end it.
        for (size_t at = 0; (at = text.find('|', at)) != std::string::npos; at += 2) text.insert(at, "\\");
        return text;
    };
    const auto heading = [&](const std::string& origin) {
        for (const auto& item : table.value("origins", Json::array()))
            if (item.at("origin") == origin) return item.at("title").get<std::string>();
        return origin;
    };
    std::string out =
        "# Result shapes\n\n"
        "<!-- Generated from `help --json` (tests/table: `reagentlive-table --write docs/commands/results.md`). "
        "Do not edit: change the verb's row or its result struct. -->\n\n"
        "What each verb takes and the keys of its `result`, as the console itself reports them:\n"
        "`help` lists the verbs, `help` with `verb` shows one (args, result, notes, an example call) and\n"
        "`help` with `json` prints this whole table for programs. `status` carries `table_hash`, which\n"
        "changes whenever a row does. The Command column lists each verb's arg keys: `?` marks an optional\n"
        "one, `[]` a list.\n\n"
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
        "| `value` | game data: dynamic, shaped by what was read |\n"
        "| `Name` | a named type, defined once under [Named types](#named-types); a pack's carry its prefix |\n\n"
        "## Conventions\n\n"
        "- `rows` is a result's list; `total` is how many there are in all when `rows` may show fewer.\n"
        "- A page carries `from` (where it began) and `next_cursor` (pass it as `start`; absent when done).\n"
        "- `capped`: the rows were cut at a hard limit and there is no cursor. `truncated`: a value in the\n"
        "  result is incomplete.\n"
        "- `module` and `rva` stand beside an address inside a loaded module, so it opens at the same RVA\n"
        "  in a static-analysis tool; other prefixes (`value_`, `target_`) locate the address in that key.\n"
        "- A key that does not apply is omitted; results hold no nulls outside game values.\n"
        "- An arg may add optional keys and never changes a key's type.\n"
        "- **Writes or calls.** marks a verb that writes the target's memory or calls into it; every other verb\n"
        "  only reads (`help --json` carries it as `writes`).\n"
        "- **Pack** is the workflow a verb serves (`help --json` lists them under `packs`): `explore`, `native`,\n"
        "  `observe` or `modify`. A console's `--packs` and the MCP adapter's packs pick by it; every verb marked\n"
        "  writes or calls is in `modify`.\n";
    std::string origin;
    for (const auto& row : table.at("rows")) {
        const auto rowOrigin = row.at("origin").get<std::string>();
        if (rowOrigin != origin) {
            origin = rowOrigin;
            out += "\n## " + heading(origin) + "\n\n| Command | Pack | What it does | `result` |\n|---|---|---|---|\n";
        }
        out += "| `" + cell(Signature(row)) + "` | " + row.at("pack").get<std::string>() + " | " +
            cell(row.at("summary").get<std::string>()) +
            (row.value("writes", false) ? " **Writes or calls.**" : "") +
            " | `" + cell(row.at("result").get<std::string>()) + "` |\n";
    }
    out += "\n## Named types\n\nTypes that several results share, as `help --json` lists them under `types`.\n\n"
           "| Type | Definition |\n|---|---|\n";
    for (const auto& [name, named] : table.value("types", Json::object()).items())
        out += "| `" + name + "` | `" + cell(named.at("notation").get<std::string>()) + "` |\n";
    out +=
        "\n## Envelopes\n\n"
        "Every line is an envelope: `command`, `ok`, and `result` or `error`. Errors carry `kind` and\n"
        "`message`; client errors (`agent`, `timeout`, `attach`, `busy`) also carry `status`, `detail` and\n"
        "`reason`. A refused `write` with `expect` adds `found`; a broken pointer chain adds `chain` with\n"
        "`broken` and `reason`. Any envelope can carry `timing` when the call set `timing`. A call refused\n"
        "before it ran has kind `schema`: `key` (the key at fault, when one is), `arg` (that key's part of\n"
        "the schema) or `args` (the verb's whole list), and `example`, a corrected `{verb, args}` built from\n"
        "the call. A result past the call's output budget is not printed: `digest` stands in for `result`,\n"
        "with `ok` true: `{chars, budget, rows?: {path, count}, largest: [{path, chars}], narrow?: [key],\n"
        "next?: str, file?: str}`. `next` is the call narrowed, as JSON text; `file` holds the whole envelope\n"
        "of a call that must not run again. A result that does not match its row's type is an `internal`\n"
        "error, never printed as a result.\n\n"
        "## Game values\n\n"
        "A `value` holds what the game's data is. Addresses are hex strings, null for a null pointer.\n"
        "An FName is its text (`Name_N` for an instance, `#index` when the pool has none), and a struct\n"
        "holding only a name (a gameplay tag) is that name; `meta` gives `index`, `number` and `name`.\n"
        "Structs expand to `{Name: value}` (`members` with `meta`). Arrays and strings show their header, and `get` with no paths adds a `preview` for top-level\n"
        "containers. Unsupported layouts give a bounded `hex` prefix. A non-finite float is\n"
        "`{\"non_finite\": \"nan\"}`. Check `truncated` on the result and its rows before treating values as\n"
        "complete.\n";
    return out;
}
}
