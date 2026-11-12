// reagentlive CLI: the verb table's row. One row says what a console verb
// takes (positionals and flags) and returns (a result type); parsing, help,
// the docs table and the Python client's strictness all read it. Rows are
// plain data in constant lists: the core's in results/verbs.h, the Unreal
// pack's in results/ue/verbs_ue.h, a host's own in ConsoleConfig::verbs.
// No JSON here; results/schema.h builds types from C++ structs.
#pragma once
#include <algorithm>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/syntax/command.h"
#include "reagentlive/frontends/cli/syntax/text.h"

namespace reagentlive::cli {
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

// An argument's type: what a value must be, for the checks here, a JSON
// call and the generated Python functions. A small vocabulary:
//   addr     an address: 0x hex or decimal, $N[+off], base+off, <module>+off, ->off chains
//   ref      an object: a literal ref, an address, or $N in a typed line
//   int      a number: 0x hex or decimal
//   dur      a duration: N seconds, or Nms, Ns, Nm
//   bytes    hex pairs, spaced or packed: 00 00 80 3f
//   pattern  a byte signature: 48 8b ?? 89, or 0x48, 0x8B, -1, 0x89
//   text     free text; the tokens of a list are joined by spaces
//   str      one token: a name, a class, a field path
//   path     a file path
//   layout   read --as: a type or a layout
//   range    <addr>:<len>, or following a root <offset>:<len>
//   span     an address, then a length
//   line     a command line, verbatim
//   calls    batch's commands
//   a|b      one of the words
//   bool     a switch: a flag that takes nothing
// bytes, pattern and text are one value however many tokens they span (a
// JSON call gives them as one string); the other types take one token each,
// and a positional that takes several takes a JSON list.
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
}

// How many tokens a positional takes. Rest: the remainder of the line, verbatim.
enum class Arity { One, Optional, Some, Any, Rest };
struct Positional {
    std::string_view key;       // Its name in a JSON call and a generated function: "addr", "data".
    std::string_view name;      // As usage shows it, without brackets: "addr", "hex bytes".
    Arity arity = Arity::One;
    std::string_view type = "str";
    std::string_view help = {}; // One line: what to put there.
    std::string_view fallback = {}; // The default when left out, as text; empty: none, or not one value.
};
// What follows a flag: nothing (a switch), one token, one token unless the
// next is a flag or the line ends, every token up to the next flag, or the
// remainder of the line.
enum class Takes { Nothing, One, Maybe, Some, Rest };
struct Flag {
    std::string_view name;      // "--from"; its key is the name without the dashes ("from").
    Takes takes = Takes::Nothing;
    std::string_view type = "bool";
    std::string_view value = {};    // Placeholder in usage: "N", "addr".
    std::string_view help = {};
    std::string_view fallback = {};
    bool required = false;
};
inline std::string_view FlagKey(const Flag& flag) { return flag.name.substr(2); }

// A line's tokens sorted by its verb's row: positionals in order, flags by name.
struct Args {
    struct Given { std::string_view flag; std::vector<std::string> values; std::string rest; };
    // batch's commands from a JSON call: each one's verb and its call as JSON text.
    struct Call { std::string verb, text; };
    std::vector<std::string> positionals;
    std::string rest;           // The Arity::Rest positional.
    std::vector<Given> flags;
    std::vector<Call> calls;

    const Given* Find(std::string_view flag) const {
        const auto found = std::ranges::find(flags, flag, &Given::flag);
        return found == flags.end() ? nullptr : &*found;
    }
    bool Has(std::string_view flag) const { return Find(flag) != nullptr; }
    // The flag's one value; empty when the flag is absent or took none.
    std::string_view Value(std::string_view flag) const {
        const auto* given = Find(flag);
        if (!given) return {};
        return given->values.empty() ? std::string_view(given->rest) : std::string_view(given->values.front());
    }
    // Positional `index`, empty past the end.
    std::string_view At(size_t index) const {
        return index < positionals.size() ? std::string_view(positionals[index]) : std::string_view{};
    }
    // Positionals from `index` on, joined by one space ("01 02 ff", a pattern, search text).
    std::string From(size_t index) const {
        std::string text;
        for (size_t i = index; i < positionals.size(); ++i) {
            if (!text.empty()) text += ' ';
            text += positionals[i];
        }
        return text;
    }
    static std::string Joined(const Given& given) {
        std::string text;
        for (const auto& value : given.values) {
            if (!text.empty()) text += ' ';
            text += value;
        }
        return text;
    }
};

struct ParseContext {
    std::span<const Ref> refs;
    std::span<const Module> modules;
};

struct Verb {
    std::string_view name;
    std::string_view origin;                // "core", a pack's name ("ue"), or "host".
    std::string_view summary;               // One line.
    std::vector<Positional> positionals;
    std::vector<Flag> flags;
    schema::Type (*result)() = nullptr;     // schema::Of<T>; null: an empty object.
    // Args to a command. Null for a host's verb: the host parses its own line.
    Parsed (*parse)(const Args& args, const ParseContext& context) = nullptr;
    std::string_view notes = {};            // More for `help <verb>`.
    std::vector<std::string_view> aliases = {};
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

// "read <addr> [len] [--as <type>]": the verb's usage line, from its row.
inline std::string Usage(const Verb& verb) {
    std::string text(verb.name);
    for (const auto& positional : verb.positionals) {
        const std::string name(positional.name);
        switch (positional.arity) {
        case Arity::One: text += " <" + name + ">"; break;
        case Arity::Optional: text += " [" + name + "]"; break;
        case Arity::Some: text += " <" + name + ">..."; break;
        case Arity::Any: text += " [" + name + "]..."; break;
        case Arity::Rest: text += " " + name; break;
        }
    }
    for (const auto& flag : verb.flags) {
        text += (flag.required ? " " : " [") + std::string(flag.name);
        const std::string value(flag.value);
        switch (flag.takes) {
        case Takes::Nothing: break;
        case Takes::One: text += " <" + value + ">"; break;
        case Takes::Maybe: text += " [" + value + "]"; break;
        case Takes::Some: text += " <" + value + ">..."; break;
        case Takes::Rest: text += " " + value; break;
        }
        if (!flag.required) text += "]";
    }
    return text;
}

inline const Verb* FindVerb(std::span<const Verb> verbs, std::string_view name) {
    for (const auto& verb : verbs) {
        if (verb.name == name) return &verb;
        if (std::ranges::find(verb.aliases, name) != verb.aliases.end()) return &verb;
    }
    return nullptr;
}

// Why the arguments do not fit the row; empty when they do: the counts
// against the arities, the required flags, and the types that need none of
// the target's state (an int is a number, an enum's value one of its words).
// A typed line and a JSON call both pass here, so both are checked the same way.
inline std::string CheckArgs(const Verb& verb, const Args& args) {
    const auto rest0 = std::ranges::find(verb.positionals, Arity::Rest, &Positional::arity);
    const size_t restAt = static_cast<size_t>(rest0 - verb.positionals.begin());
    size_t least = 0, most = 0;
    bool unbounded = false;
    for (const auto& positional : verb.positionals) {
        switch (positional.arity) {
        case Arity::One: ++least; ++most; break;
        case Arity::Optional: ++most; break;
        case Arity::Some: ++least; unbounded = true; break;
        case Arity::Any: unbounded = true; break;
        case Arity::Rest: break;
        }
    }
    if (rest0 != verb.positionals.end() && verb.positionals[restAt].arity == Arity::Rest && args.rest.empty() &&
        args.positionals.size() >= restAt) {
        // A required remainder (batch's body, a call line) that is missing.
        return ("missing " + std::string(verb.positionals[restAt].name));
    }
    if (args.positionals.size() < least) return ("too few arguments");
    for (const auto& flag : verb.flags)
        if (flag.required && !args.Has(flag.name)) return ("missing " + std::string(flag.name));
    if (!unbounded && args.positionals.size() > most) return ("too many arguments");
    const auto bad = [](std::string_view type, std::string_view value) {
        uint64_t number = 0;
        if (type == "int") return !text::Number(value, number);
        if (arg::Enum(type)) return !arg::OneOf(type, value);
        return false;
    };
    const auto wrong = [](std::string_view name, std::string_view type, const std::string& value) {
        return std::string(name) + ": '" + value + "' is not " +
            (arg::Enum(type) ? "one of " + std::string(type) : std::string("a number"));
    };
    // A positional's tokens in order: one each, a list takes the rest.
    size_t at = 0;
    for (const auto& positional : verb.positionals) {
        const size_t left = args.positionals.size() - std::min(at, args.positionals.size());
        const size_t count = positional.arity == Arity::One || positional.arity == Arity::Optional ?
            std::min<size_t>(1, left) : positional.arity == Arity::Rest ? 0 : left;
        for (size_t i = at; i < at + count; ++i)
            if (bad(positional.type, args.positionals[i])) return wrong(positional.name, positional.type, args.positionals[i]);
        at += count;
    }
    for (const auto& given : args.flags) {
        const auto flag = std::ranges::find(verb.flags, given.flag, &Flag::name);
        if (flag == verb.flags.end()) continue;
        for (const auto& value : given.values)
            if (bad(flag->type, value)) return wrong(flag->name, flag->type, value);
    }
    return {};
}

// Sorts what follows the verb into the row's positionals and flags. A token
// in double quotes is taken exactly (spaces, ';' and "--" included) and is
// never a flag. Errors end with the verb's usage line.
inline std::expected<Args, std::string> Tokenise(const Verb& verb, std::string_view rest) {
    Args args;
    const auto fail = [&](std::string why) {
        return std::expected<Args, std::string>(std::unexpect, why + "; usage: " + Usage(verb));
    };
    const auto flagNamed = [&](std::string_view token) -> const Flag* {
        const auto found = std::ranges::find(verb.flags, token, &Flag::name);
        return found == verb.flags.end() ? nullptr : &*found;
    };
    // The next token and whether it was quoted; false at the end of the line or an open quote.
    struct Token { std::string text; bool quoted = false; };
    bool open = false;
    const auto next = [&](std::string_view& from, Token& token) {
        from = text::Trim(from);
        if (from.empty()) return false;
        bool unterminated = false;
        if (auto quoted = text::Quoted(from, unterminated)) {
            token = {std::move(*quoted), true};
            return true;
        }
        if (unterminated) { open = true; return false; }
        token = {std::string(text::Next(from)), false};
        return true;
    };
    const auto peekFlag = [&](std::string_view from) {
        Token token;
        return next(from, token) && !token.quoted && flagNamed(token.text);
    };
    const auto rest0 = std::ranges::find(verb.positionals, Arity::Rest, &Positional::arity);
    const size_t restAt = static_cast<size_t>(rest0 - verb.positionals.begin());
    while (true) {
        rest = text::Trim(rest);
        if (rest0 != verb.positionals.end() && args.positionals.size() == restAt && !peekFlag(rest)) {
            args.rest = std::string(rest);
            rest = {};
            break;
        }
        Token token;
        if (!next(rest, token)) break;
        const Flag* flag = token.quoted ? nullptr : flagNamed(token.text);
        if (!flag) {
            if (!token.quoted && token.text.starts_with("--")) return fail("unknown flag " + token.text);
            args.positionals.push_back(std::move(token.text));
            continue;
        }
        if (args.Has(flag->name)) return fail(std::string(flag->name) + " given twice");
        Args::Given given{flag->name, {}, {}};
        switch (flag->takes) {
        case Takes::Nothing: break;
        case Takes::One:
            if (peekFlag(rest) || !next(rest, token)) return fail(std::string(flag->name) + " needs a value");
            given.values.push_back(std::move(token.text));
            break;
        case Takes::Maybe:
            if (!peekFlag(rest) && next(rest, token)) given.values.push_back(std::move(token.text));
            break;
        case Takes::Some:
            while (!peekFlag(rest) && next(rest, token)) given.values.push_back(std::move(token.text));
            if (given.values.empty() && !open) return fail(std::string(flag->name) + " needs a value");
            break;
        case Takes::Rest:
            given.rest = std::string(text::Trim(rest));
            rest = {};
            if (given.rest.empty()) return fail(std::string(flag->name) + " needs a value");
            break;
        }
        args.flags.push_back(std::move(given));
    }
    if (open) return fail("the quote is not closed");
    if (auto problem = CheckArgs(verb, args); !problem.empty()) return fail(problem);
    return args;
}
}
