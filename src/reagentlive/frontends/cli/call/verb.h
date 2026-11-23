// reagentlive CLI: the verb table's row. One row says what a console verb
// takes (named, typed args) and returns (a result type); the checks of a
// JSON call (check.h), help, the docs table, the Python functions and the
// MCP schemas all read it. Rows are plain data in constant lists: the core's
// in results/verbs.h, the Unreal pack's in packs/ue/cli/results/verbs_ue.h, a
// host's own in ConsoleConfig::verbs. results/schema.h builds result types
// from C++ structs.
#pragma once
#include <algorithm>
#include <cstdint>
#include <expected>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
#include "reagentlive/frontends/cli/call/command.h"
#include "reagentlive/frontends/cli/call/values.h"

namespace reagentlive::cli {
using Json = nlohmann::json;

namespace schema {
// A result type. A small vocabulary, not a schema language:
//   addr    an address as "0x..." text      hex    a number as "0x..." text (rva, offset, size)
//   bytes   "01 02 ff", in memory order     ref    an object ref, reusable in another command
//   bool, u32, u64, i32, i64, f64, str      a|b    a string that is one of the listed words
//   [T]     a list ([T|null]: elements may be null)       {<key>: T}  a map
//   {k: T, k?: T}  an object; `?` marks a key that is omitted when it does not apply
//   value   game data: dynamic, shaped by what was read
struct Field;
//   Name    a named type: help --json lists it once under `types`, a pack's
//           under its own prefix (ue.Name); a struct names itself with TypeName.
struct Type {
    enum class Kind { Bool, U32, U64, I32, I64, F64, Str, Addr, Hex, Bytes, Ref, Value, Enum, List, Map, Struct };
    Kind kind = Kind::Value;
    std::string name;           // A named type's name; its definition stays here too.
    std::string values;         // Enum: "a|b|c"; a last word "..." leaves it open.
    std::vector<Type> element;  // List, Map: the element type (one entry).
    bool nullable = false;      // List: an element may be null.
    std::vector<Field> fields;  // Struct, in declaration order.
};
struct Field {
    std::string key;
    bool optional = false;
    Type type;
};

inline std::string_view ScalarName(Type::Kind kind) {
    switch (kind) {
    case Type::Kind::Bool: return "bool";
    case Type::Kind::U32: return "u32";
    case Type::Kind::U64: return "u64";
    case Type::Kind::I32: return "i32";
    case Type::Kind::I64: return "i64";
    case Type::Kind::F64: return "f64";
    case Type::Kind::Str: return "str";
    case Type::Kind::Addr: return "addr";
    case Type::Kind::Hex: return "hex";
    case Type::Kind::Bytes: return "bytes";
    case Type::Kind::Ref: return "ref";
    default: return "value";
    }
}

// The compact notation `help <verb>` prints: {address: addr, kind: code|data, module?: str}.
// A named type prints its name; `full` prints the outer one's definition.
inline std::string Notation(const Type& type, bool full = false) {
    if (!type.name.empty() && !full) return type.name;
    switch (type.kind) {
    case Type::Kind::Enum: return type.values;
    case Type::Kind::List:
        return "[" + (type.element.empty() ? std::string("value") : Notation(type.element.front())) +
            (type.nullable ? "|null]" : "]");
    case Type::Kind::Map:
        return "{<key>: " + (type.element.empty() ? std::string("value") : Notation(type.element.front())) + "}";
    case Type::Kind::Struct: {
        std::string text = "{";
        for (const auto& field : type.fields) {
            if (text.size() > 1) text += ", ";
            text += field.key + (field.optional ? "?: " : ": ") + Notation(field.type);
        }
        return text + "}";
    }
    default: return std::string(ScalarName(type.kind));
    }
}
}

// An argument's type: what a value in a JSON call must be, for the checks in
// check.h, `help`, the generated Python functions and the MCP schemas. A
// small vocabulary, not a schema language:
//   bool     true or false
//   int      a whole number, 0 or more: a JSON integer, or "0x..." hex text (64-bit values)
//   num      a JSON number
//   dur      seconds, a JSON number above 0 (0.5: 500 ms)
//   str      a string: a name, a class, an id
//   text     free text, spaces kept
//   file     a file or folder path
//   bytes    hex pairs in one string: "00 00 80 3f"
//   pattern  a byte signature in one string: "48 8b ?? 89"
//   layout   read's element type or layout in one string: "f32", "u8 phase, pad 3, f64[3] start"
//   addr     an address: "0x..." hex, a literal ref, {"module": "x.dll", "rva": N} or {"base": <addr>, "offset": N}
//   chain    an addr, or a pointer chain {"base": <addr>, "chain": [N, N, ...]}
//   ref      an object: a literal ref 0x<address>#<index>:<serial>, or "0x..." to identify
//   path     a field path, one name per step: ["Info", "Owner"]
//   filter   {"field": <path>, "contains": "text"}
//   params   a call's inputs: {"Name": value}
//   range    a watch range: {"addr": <addr>, "len": N} and the other forms watch-start lists
//   point    a trace end: [x, y, z] in cm, a ref, or {"object": <ref>, "socket": "Name"}
//   scalar   a string or a number
//   calls    batch's: a list of {"verb": "...", "args": {...}}
//   a|b      one of the words
// An arg marked `list` takes a JSON list of its type.
namespace arg {
inline bool Enum(std::string_view type) { return type.find('|') != std::string_view::npos; }
inline bool OneOf(std::string_view type, std::string_view word) {
    while (!type.empty()) {
        const size_t bar = type.find('|');
        if (type.substr(0, bar) == word) return true;
        type = bar == std::string_view::npos ? std::string_view{} : type.substr(bar + 1);
    }
    return false;
}
// Every type the vocabulary has; a row naming another fails the table tests.
inline constexpr std::string_view Types[] = {"bool", "int", "num", "dur", "str", "text", "file", "bytes", "pattern",
    "layout", "addr", "chain", "ref", "path", "filter", "params", "range", "point", "scalar", "calls"};
inline bool Known(std::string_view type) { return Enum(type) || std::ranges::find(Types, type) != std::end(Types); }
}

// The workflow a verb serves: what a session loads (the MCP adapter) and
// what a console allows (--packs). Every row names one. A verb marked
// `writes` is in Modify; the other three hold none.
enum class ToolPack { Explore, Native, Observe, Modify };
struct ToolPackInfo {
    ToolPack pack;
    std::string_view name, job;
};
inline constexpr ToolPackInfo ToolPacks[] = {
    {ToolPack::Explore, "explore", "find objects and read their state and data (always on)"},
    {ToolPack::Native, "native", "read code and keep names across builds"},
    {ToolPack::Observe, "observe", "watch behaviour over time without changing it"},
    {ToolPack::Modify, "modify", "change the target or call into it"},
};
inline std::string_view ToolPackName(ToolPack pack) { return ToolPacks[static_cast<size_t>(pack)].name; }
inline const ToolPackInfo* FindToolPack(std::string_view name) {
    for (const auto& info : ToolPacks)
        if (info.name == name) return &info;
    return nullptr;
}
// A set of packs as bits (1 << pack).
using ToolPackSet = uint32_t;
inline constexpr ToolPackSet AllToolPacks = (1u << std::size(ToolPacks)) - 1;
inline constexpr ToolPackSet ToolPackBit(ToolPack pack) { return 1u << static_cast<uint32_t>(pack); }
// "explore,native" as a set; explore is always in it. Unknown names: the error.
inline std::expected<ToolPackSet, std::string> ParseToolPacks(std::string_view list) {
    ToolPackSet set = ToolPackBit(ToolPack::Explore);
    while (!list.empty()) {
        const size_t comma = list.find(',');
        const auto name = text::Trim(list.substr(0, comma));
        list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
        if (name.empty()) continue;
        const auto* info = FindToolPack(name);
        if (!info) {
            std::string known;
            for (const auto& each : ToolPacks) known += (known.empty() ? "" : ", ") + std::string(each.name);
            return std::unexpected("unknown pack '" + std::string(name) + "' (packs: " + known + ")");
        }
        set |= ToolPackBit(info->pack);
    }
    return set;
}
inline std::string ToolPackList(ToolPackSet set) {
    std::string text;
    for (const auto& info : ToolPacks)
        if (set & ToolPackBit(info.pack)) text += (text.empty() ? "" : ",") + std::string(info.name);
    return text;
}

// One argument of a verb: its key in a JSON call, its type, a line of help,
// its default (as JSON text; empty: none), whether a call must give it, and
// a value for examples where its type's own would be refused (JSON text).
struct Arg {
    std::string_view key;
    std::string_view type = "str";
    std::string_view help = {};
    std::string_view fallback = {};
    bool required = false;
    bool list = false;          // A JSON list of `type`.
    std::string_view example = {};
};

// A call's arguments once check.h accepted them: every key is one of the
// row's and every value has its type, so a builder reads them without
// checking shapes again (ranges and combinations stay the builder's).
struct Args {
    Json values = Json::object();

    const Json& At(std::string_view key) const {
        static const Json none;
        const auto found = values.find(key);
        return found == values.end() ? none : *found;
    }
    // Given; a bool only when true.
    bool Has(std::string_view key) const {
        const Json& value = At(key);
        return !value.is_null() && !(value.is_boolean() && !value.get<bool>());
    }
    // A string argument's text; empty when absent.
    std::string Str(std::string_view key) const {
        const Json& value = At(key);
        return value.is_string() ? value.get<std::string>() : std::string();
    }
    // An int argument (a number or "0x..." text); `fallback` when absent.
    uint64_t Int(std::string_view key, uint64_t fallback = 0) const { return Number(At(key), fallback); }
    static uint64_t Number(const Json& value, uint64_t fallback = 0) {
        if (value.is_number_unsigned()) return value.get<uint64_t>();
        if (value.is_number_integer() && value.get<int64_t>() >= 0) return static_cast<uint64_t>(value.get<int64_t>());
        uint64_t out = 0;
        if (value.is_string() && text::Number(value.get<std::string>(), out)) return out;
        return fallback;
    }
    // A dur argument in milliseconds (at least 1); 0 when absent.
    uint64_t Milliseconds(std::string_view key) const {
        const Json& value = At(key);
        if (!value.is_number()) return 0;
        const double ms = value.get<double>() * 1000 + 0.5;
        return ms < 1 ? 1 : ms > 1e15 ? uint64_t{1} << 50 : static_cast<uint64_t>(ms);
    }
    // A list argument's strings.
    std::vector<std::string> Strs(std::string_view key) const {
        std::vector<std::string> out;
        const Json& value = At(key);
        if (value.is_array())
            for (const auto& item : value)
                if (item.is_string()) out.push_back(item.get<std::string>());
        return out;
    }
};

struct ParseContext {
    std::span<const Module> modules;
};

struct Verb {
    std::string_view name;
    std::string_view origin;                // "core", a pack's name ("ue"), or "host".
    ToolPack pack;                          // The workflow it serves; writes implies Modify.
    std::string_view summary;               // One line.
    std::vector<Arg> args;
    schema::Type (*result)() = nullptr;     // schema::Of<T>; null: an empty object.
    // Checked args to a command. Null for a host's verb: the host builds its own.
    Parsed (*build)(const Args& args, const ParseContext& context) = nullptr;
    std::string_view notes = {};            // More for `help <verb>`.
    // Writes the target's memory or calls into it: a caller may allow the
    // other verbs without asking and ask first for these.
    bool writes = false;
    // Holds the console for seconds by design (a blocking capture): a tool
    // client leaves it out, since a call would outlast its timeout.
    bool blocks = false;
};

// Where verbs come from, as a caller sees it: "core", a pack's id or "host",
// with a title for headings. The core, each pack and the host give one each.
struct Origin {
    std::string_view id, title;
};

inline const Verb* FindVerb(std::span<const Verb> verbs, std::string_view name) {
    for (const auto& verb : verbs)
        if (verb.name == name) return &verb;
    return nullptr;
}
inline const Arg* FindArg(const Verb& verb, std::string_view key) {
    const auto found = std::ranges::find(verb.args, key, &Arg::key);
    return found == verb.args.end() ? nullptr : &*found;
}
}
