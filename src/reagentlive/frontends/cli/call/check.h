// reagentlive CLI: a JSON call's arguments against its verb's row. Every
// key must be the row's, every required one given, every value of its type
// (verb.h's vocabulary); anything else is refused before the call runs, with
// the failing key's part of the schema and a corrected example built from
// the call itself, so a caller fixes it in one step.
#pragma once
#include <algorithm>
#include <expected>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/call/verb.h"

namespace reagentlive::cli {
namespace check {
// What a value of the type looks like, for messages.
inline std::string Takes(const Arg& arg) {
    const auto one = [](std::string_view type) -> std::string {
        if (arg::Enum(type)) return "one of " + std::string(type);
        if (type == "bool") return "true or false";
        if (type == "int") return "a whole number, or \"0x...\" text";
        if (type == "num") return "a number";
        if (type == "dur") return "seconds, a number above 0";
        if (type == "bytes") return "hex pairs in one string (\"00 00 80 3f\")";
        if (type == "pattern") return "a byte signature in one string (\"48 8b ?? 89\")";
        if (type == "addr") return "an address: \"0x...\", a literal ref, {\"module\", \"rva\"} or {\"base\", \"offset\"}";
        if (type == "chain") return "an address, or a pointer chain {\"base\": <addr>, \"chain\": [N, N, ...]}";
        if (type == "ref") return "a literal ref 0x<address>#<index>:<serial>, or \"0x...\"";
        if (type == "path") return "a field path, a list of names: [\"Field\", \"Inner\"]";
        if (type == "filter") return "{\"field\": [\"Name\"], \"contains\": \"text\"}";
        if (type == "params") return "an object of input name to value";
        if (type == "range") return "a range object ({\"addr\": <addr>, \"len\": N}, ...)";
        if (type == "point") return "[x, y, z], a ref, or {\"object\": <ref>, \"socket\": \"Name\"}";
        if (type == "scalar") return "a string or a number";
        if (type == "calls") return "a list of {\"verb\": ..., \"args\": {...}}";
        return "a string";
    };
    return arg.list ? "a list, each " + one(arg.type) : one(arg.type);
}

// A value of the type, for examples.
inline Json Example(std::string_view type) {
    if (arg::Enum(type)) return std::string(type.substr(0, type.find('|')));
    if (type == "bool") return true;
    if (type == "int") return 16;
    if (type == "num") return 1.5;
    if (type == "dur") return 5;
    if (type == "text") return "text";
    if (type == "file") return "out.json";
    if (type == "bytes") return "00 00 80 3f";
    if (type == "pattern") return "48 8b ?? 89";
    if (type == "layout") return "f32";
    if (type == "addr" || type == "chain") return "0x140001000";
    if (type == "ref") return "0x1f2e3d4c50#123:4";
    if (type == "path") return Json::array({"Field"});
    if (type == "filter") return {{"field", Json::array({"Name"})}, {"contains", "text"}};
    if (type == "params") return {{"Name", 1}};
    if (type == "range") return {{"addr", "0x140001000"}, {"len", 4}};
    if (type == "point") return Json::array({0, 0, 0});
    if (type == "scalar") return 1;
    if (type == "calls") return Json::array({{{"verb", "read"}, {"args", {{"addr", "0x140001000"}}}}});
    return "Name";
}
inline Json Example(const Arg& arg) {
    if (!arg.example.empty()) return Json::parse(arg.example);
    return arg.list ? Json::array({Example(arg.type)}) : Example(arg.type);
}

inline bool IsInt(const Json& value) {
    uint64_t number = 0;
    if (value.is_number_unsigned()) return true;
    if (value.is_number_integer()) return value.get<int64_t>() >= 0;
    if (!value.is_string()) return false;
    const auto text = value.get<std::string>();
    return text.starts_with("0x") && text::Number(text, number);
}
inline bool IsPath(const Json& value) {
    return value.is_array() && !value.empty() && std::ranges::all_of(value, [](const Json& step) { return step.is_string(); });
}
// An object holding exactly these keys (the ones after `optional` may be left out).
inline bool Keys(const Json& value, std::initializer_list<std::string_view> keys, size_t optional = SIZE_MAX) {
    if (!value.is_object()) return false;
    size_t index = 0;
    for (const auto key : keys) {
        if (index++ < optional && !value.contains(key)) return false;
    }
    for (const auto& [key, _] : value.items())
        if (std::ranges::find(keys, std::string_view(key)) == keys.end()) return false;
    return true;
}
// Whether one value (not a list's) has the type.
inline bool Fits(std::string_view type, const Json& value) {
    if (arg::Enum(type)) return value.is_string() && arg::OneOf(type, value.get<std::string>());
    if (type == "bool") return value.is_boolean();
    if (type == "int") return IsInt(value);
    if (type == "num") return value.is_number();
    if (type == "dur") return value.is_number() && value.get<double>() > 0;
    if (type == "path") return IsPath(value);
    if (type == "filter") return Keys(value, {"field", "contains"}) && IsPath(value["field"]) && value["contains"].is_string();
    if (type == "params" || type == "range") return value.is_object();
    if (type == "scalar") return value.is_string() || value.is_number();
    if (type == "point") {
        if (value.is_string()) return true;
        if (value.is_array()) return value.size() == 3 && std::ranges::all_of(value, [](const Json& v) { return v.is_number(); });
        return Keys(value, {"object", "socket"}) && value["object"].is_string() && value["socket"].is_string();
    }
    if (type == "calls")
        return value.is_array() && std::ranges::all_of(value, [](const Json& call) {
            return Keys(call, {"verb", "args"}, 1) && call["verb"].is_string() && (!call.contains("args") || call["args"].is_object());
        });
    if (type == "addr" || type == "chain") {
        if (value.is_string()) return true;
        if (Keys(value, {"module", "rva"})) return value["module"].is_string() && IsInt(value["rva"]);
        if (Keys(value, {"base", "offset"})) return Fits("addr", value["base"]) && IsInt(value["offset"]);
        if (type == "chain" && Keys(value, {"base", "chain"}))
            return Fits("addr", value["base"]) && value["chain"].is_array() && !value["chain"].empty() &&
                std::ranges::all_of(value["chain"], IsInt);
        return false;
    }
    return value.is_string(); // str, text, file, bytes, pattern, layout, ref.
}
inline bool Fits(const Arg& arg, const Json& value) {
    if (!arg.list) return Fits(arg.type, value);
    return value.is_array() && !value.empty() && std::ranges::all_of(value, [&](const Json& item) { return Fits(arg.type, item); });
}

// Edits between two words (ASCII, any case): how near a misspelt key is.
inline size_t Distance(std::string_view a, std::string_view b) {
    std::vector<size_t> row(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        size_t diagonal = row[0];
        row[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            const size_t above = row[j];
            const bool same = std::tolower(static_cast<unsigned char>(a[i - 1])) == std::tolower(static_cast<unsigned char>(b[j - 1]));
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (same ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}
// The row's key nearest a given one, or null when none is close (more than half the word changed).
inline const Arg* Nearest(const Verb& verb, std::string_view key) {
    const Arg* best = nullptr;
    size_t distance = SIZE_MAX;
    for (const auto& arg : verb.args) {
        const size_t d = Distance(key, arg.key);
        if (d < distance) { distance = d; best = &arg; }
    }
    return best && distance <= std::max<size_t>(1, std::max(key.size(), best->key.size()) / 2) ? best : nullptr;
}
// The verbs nearest a name, up to three.
inline std::vector<std::string> NearestVerbs(std::span<const Verb> verbs, std::string_view name) {
    std::vector<std::pair<size_t, std::string_view>> scored;
    for (const auto& verb : verbs) {
        const size_t d = Distance(name, verb.name);
        if (d <= std::max<size_t>(2, name.size() / 3) || verb.name.starts_with(name) || name.starts_with(verb.name))
            scored.emplace_back(d, verb.name);
    }
    std::ranges::sort(scored);
    std::vector<std::string> out;
    for (size_t i = 0; i < scored.size() && i < 3; ++i) out.emplace_back(scored[i].second);
    return out;
}
}

// Why a call is refused: the message, and the key at fault when there is one.
struct Refusal {
    std::string message;
    std::string key;
};

// The row's keys, for an unknown key's message.
inline std::string ArgKeys(const Verb& verb) {
    std::string keys;
    for (const auto& arg : verb.args) keys += (keys.empty() ? "" : ", ") + std::string(arg.key);
    return keys.empty() ? std::string("none") : keys;
}

// A call's args checked against the row: unknown keys, then missing required
// ones, then each value's type.
inline std::expected<Args, Refusal> CheckCall(const Verb& verb, const Json& given) {
    using Out = std::expected<Args, Refusal>;
    if (!given.is_null() && !given.is_object()) return Out(std::unexpect, Refusal{"args is an object of named arguments", {}});
    Args args;
    if (given.is_object()) {
        for (const auto& [key, value] : given.items()) {
            const Arg* arg = FindArg(verb, key);
            if (arg) continue;
            const Arg* closest = check::Nearest(verb, key);
            return Out(std::unexpect, Refusal{"unknown key '" + key + "'" +
                (closest ? " (did you mean " + std::string(closest->key) + "?)" : "") + "; " + std::string(verb.name) +
                " takes " + ArgKeys(verb), key});
        }
    }
    for (const auto& arg : verb.args) {
        const std::string key(arg.key);
        if (!given.is_object() || !given.contains(key)) {
            if (arg.required)
                return Out(std::unexpect, Refusal{"missing " + key + ": " + check::Takes(arg) +
                    (arg.help.empty() ? "" : " (" + std::string(arg.help) + ")"), key});
            continue;
        }
        const Json& value = given[key];
        if (!check::Fits(arg, value))
            return Out(std::unexpect, Refusal{key + ": " + check::Takes(arg) + ", not " +
                value.dump(-1, ' ', true, Json::error_handler_t::replace), key});
        args.values[key] = value;
    }
    return args;
}

// The key a builder's message is about: a message that starts with one of
// the row's keys ("count must be 1..64") names it.
inline std::string KeyOf(const Verb& verb, std::string_view message) {
    const Arg* best = nullptr;
    for (const auto& arg : verb.args) {
        if (!message.starts_with(arg.key)) continue;
        const char next = message.size() > arg.key.size() ? message[arg.key.size()] : ' ';
        if ((next == ' ' || next == ':' || next == ',') && (!best || arg.key.size() > best->key.size())) best = &arg;
    }
    return best ? std::string(best->key) : std::string();
}

// One call that the row accepts, built from the given one: its values that
// fit are kept, a misspelt key moves to the key it was meant for, the key at
// fault is dropped (an optional one) or replaced by an example of its type,
// and every required key left out gets one.
inline Json ExampleCall(const Verb& verb, const Json& given = Json(), std::string_view fault = {}) {
    Json args = Json::object();
    if (given.is_object()) {
        for (const auto& [key, value] : given.items()) {
            const Arg* arg = FindArg(verb, key);
            if (!arg) arg = check::Nearest(verb, key);
            if (!arg || arg->key == fault || args.contains(arg->key) || (arg->key != key && given.contains(arg->key)) ||
                !check::Fits(*arg, value)) continue;
            args[std::string(arg->key)] = value;
        }
    }
    // A call that kept nothing also gets the optional args that have an
    // example: a verb that needs one of several (find's text or cls) still
    // gets a call it takes.
    const bool bare = args.empty();
    for (const auto& arg : verb.args)
        if ((arg.required || (bare && !arg.example.empty())) && !args.contains(arg.key))
            args[std::string(arg.key)] = check::Example(arg);
    return {{"verb", verb.name}, {"args", std::move(args)}};
}

// An argument as help --json lists it.
inline Json ArgJson(const Arg& arg) {
    Json out = {{"key", arg.key}, {"type", arg::Enum(arg.type) ? std::string_view("str") : arg.type}};
    if (arg.required) out["required"] = true;
    if (arg.list) out["list"] = true;
    if (!arg.fallback.empty()) {
        Json parsed = Json::parse(arg.fallback, nullptr, false);
        out["default"] = parsed.is_discarded() ? Json(arg.fallback) : parsed;
    }
    if (arg::Enum(arg.type)) {
        Json words = Json::array();
        for (std::string_view rest = arg.type; !rest.empty();) {
            const size_t bar = rest.find('|');
            words.push_back(rest.substr(0, bar));
            rest = bar == std::string_view::npos ? std::string_view{} : rest.substr(bar + 1);
        }
        out["enum"] = std::move(words);
    }
    if (!arg.help.empty()) out["help"] = arg.help;
    return out;
}

// A refused call's error: kind schema, the message, the key at fault and its
// part of the schema (else the whole row's args), and the corrected example.
inline Json RefusalJson(const Verb& verb, const Json& given, const Refusal& refusal) {
    Json error = {{"kind", "schema"}, {"message", refusal.message}};
    const Arg* arg = refusal.key.empty() ? nullptr : FindArg(verb, refusal.key);
    if (arg) {
        error["key"] = refusal.key;
        error["arg"] = ArgJson(*arg);
    } else {
        if (!refusal.key.empty()) error["key"] = refusal.key;
        Json args = Json::array();
        for (const auto& each : verb.args) args.push_back(ArgJson(each));
        error["args"] = std::move(args);
    }
    error["example"] = ExampleCall(verb, given, arg ? std::string_view(arg->key) : std::string_view{});
    return error;
}
}
