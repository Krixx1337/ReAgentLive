// reagentlive CLI: docs/commands/results.md as text, generated from what
// `help` with `json` prints (the verb table), so the page cannot drift from the
// console. tests/table writes it and a test fails when the file is stale. Pure.
#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/values/json.h"

namespace reagentlive::cli {
// A row's verb and arg keys, as the results page lists them: `read {addr, len?, type?}`
// (`?` marks an optional key, `[]` a list).
// An object arg lists its keys the same way: `view{start?, limit?}?`.
inline std::string ArgKeys(const Json& args) {
    std::string keys;
    for (const auto& arg : args) {
        keys += (keys.empty() ? "" : ", ") + arg.at("key").get<std::string>();
        if (arg.contains("fields")) keys += "{" + ArgKeys(arg.at("fields")) + "}";
        if (arg.value("list", false)) keys += "[]";
        if (!arg.value("required", false)) keys += "?";
    }
    return keys;
}
inline std::string Signature(const Json& row) {
    const std::string keys = ArgKeys(row.value("args", Json::array()));
    return row.at("verb").get<std::string>() + (keys.empty() ? "" : " {" + keys + "}");
}

// docs/commands/reference.md: every verb in full (args with their help, the
// result, notes and an example call), the general notes, the shared view
// keys and the error kinds. `table` is `help` with `json`; `extra` holds what the
// table does not: {notes: [str], view: [arg], errors: [{kind, meaning, message}]}.
inline std::string ReferenceMarkdown(const Json& table, const Json& extra) {
    const auto cell = [](std::string text) {
        for (size_t at = 0; (at = text.find('|', at)) != std::string::npos; at += 2) text.insert(at, "\\");
        for (size_t at = 0; (at = text.find('\n', at)) != std::string::npos;) text.replace(at, 1, " ");
        return text;
    };
    const auto heading = [&](const std::string& origin) {
        for (const auto& item : table.value("origins", Json::array()))
            if (item.at("origin") == origin) return item.at("title").get<std::string>();
        return origin;
    };
    const auto type = [](const Json& arg) {
        std::string text = arg.contains("enum") ? std::string() : arg.at("type").get<std::string>();
        if (arg.contains("enum"))
            for (const auto& word : arg.at("enum")) text += (text.empty() ? "" : "|") + word.get<std::string>();
        return arg.value("list", false) ? "[" + text + "]" : text;
    };
    // One table row per arg; an object's keys follow it by path (view.limit).
    std::function<void(std::string&, const Json&, const std::string&)> argRows = [&](std::string& out, const Json& args,
                                                                                    const std::string& prefix) {
        for (const auto& arg : args) {
            const std::string key = prefix + arg.at("key").get<std::string>();
            const std::string fallback = arg.contains("default") ? "`" + cell(arg.at("default").dump()) + "`" : "";
            out += "| `" + key + "` | `" + cell(type(arg)) + "` | " + (arg.value("required", false) ? "yes" : "") + " | " +
                fallback + " | " + cell(arg.value("help", std::string())) + " |\n";
            if (arg.contains("fields")) argRows(out, arg.at("fields"), key + ".");
        }
    };
    std::string out =
        "# Verb reference\n\n"
        "<!-- Generated from the verb table (tests/table: `reagentlive-table --write-reference "
        "docs/commands/reference.md`). Do not edit: change the verb's row, `ViewKeys`, `GeneralNotes` or "
        "`ErrorKinds`. -->\n\n"
        "Every verb as the console's table holds it: its args with their help, its result, its notes and one\n"
        "example call. A call is `{\"verb\": ..., \"args\": {...}}` with the keys below; `help` with `verb` prints the\n"
        "same for one verb. Result keys and the notation: [result shapes](results.md); the contract:\n"
        "[JSON-only contract](../tool-interface.md#json-only-contract).\n\n"
        "## General notes\n\n";
    for (const auto& note : extra.value("notes", Json::array())) out += "- " + note.get<std::string>() + "\n";
    out += "\n## View\n\nThe shared output-shaping object: one schema, the same on every verb that takes `view`. A\n"
           "verb takes the keys that apply to its result (its row lists them); a key it does not take is refused.\n\n"
           "| Key | Type | Required | Default | What |\n|---|---|---|---|---|\n";
    argRows(out, extra.value("view", Json::array()), "");
    out += "\n## Error kinds\n\nAn error's `kind`, and the shape of its `message` (`<...>` stands for a value).\n\n"
           "| Kind | When | Message |\n|---|---|---|\n";
    for (const auto& kind : extra.value("errors", Json::array()))
        out += "| `" + kind.at("kind").get<std::string>() + "` | " + cell(kind.at("meaning").get<std::string>()) + " | " +
            cell(kind.at("message").get<std::string>()) + " |\n";
    std::string origin;
    for (const auto& row : table.at("rows")) {
        const auto rowOrigin = row.at("origin").get<std::string>();
        if (rowOrigin != origin) {
            origin = rowOrigin;
            out += "\n## " + heading(origin) + "\n";
        }
        out += "\n### " + row.at("verb").get<std::string>() + "\n\n" + row.at("summary").get<std::string>() + ".\n\n";
        out += "Pack `" + row.at("pack").get<std::string>() + "`" +
            (row.value("writes", false) ? "; **writes or calls**" : "") +
            (row.value("blocks", false) ? "; blocks (left out of MCP)" : "") + ".\n\n";
        if (!row.value("args", Json::array()).empty()) {
            out += "| Key | Type | Required | Default | What |\n|---|---|---|---|---|\n";
            argRows(out, row.at("args"), "");
            out += "\n";
        }
        out += "Result: `" + cell(row.at("result").get<std::string>()) + "`\n\n";
        if (row.contains("notes")) out += row.at("notes").get<std::string>() + "\n\n";
        out += "```json\n" + row.at("example").dump() + "\n```\n";
    }
    return out;
}

// `table`: the result of `help` with `json` ({rows: [{verb, origin, summary, args, result, ...}], types, origins}).
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
        "<!-- Generated from `help` with `json` (tests/table: `reagentlive-table --write docs/commands/results.md`). "
        "Do not edit: change the verb's row or its result struct. -->\n\n"
        "What each verb takes and the keys of its `result`, as the console itself reports them:\n"
        "`help` lists the verbs, `help` with `verb` shows one (args, result, notes, an example call) and\n"
        "`help` with `json` prints this whole table for programs. `status` carries `table_hash`, which\n"
        "changes whenever a row does. The Command column lists each verb's arg keys: `?` marks an optional\n"
        "one, `[]` a list, `{...}` an object's keys (`view` is the shared output-shaping object).\n\n"
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
        "- A page carries `from` (where it began) and `next_cursor` (pass it as `view.start`; absent when done).\n"
        "- `capped`: the rows were cut at a hard limit and there is no cursor. `truncated`: a value in the\n"
        "  result is incomplete.\n"
        "- `module` and `rva` stand beside an address inside a loaded module, so it opens at the same RVA\n"
        "  in a static-analysis tool; other prefixes (`value_`, `target_`) locate the address in that key.\n"
        "- A key that does not apply is omitted; results hold no nulls outside game values.\n"
        "- An arg may add optional keys and never changes a key's type.\n"
        "- **Writes or calls.** marks a verb that writes the target's memory or calls into it; every other verb\n"
        "  only reads (`help` with `json` carries it as `writes`).\n"
        "- **Pack** is the workflow a verb serves (`help` with `json` lists them under `packs`): `explore`, `native`,\n"
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
    out += "\n## Named types\n\nTypes that several results share, as `help` with `json` lists them under `types`.\n\n"
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
