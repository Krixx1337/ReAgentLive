// reagentlive CLI: a JSON call's arguments against its verb's row. Every
// key must be the row's, every required one given, every value of its type
// (verb.h's vocabulary); anything else is refused before the call runs, with
// the failing key's part of the schema and a corrected example built from
// the call itself, so a caller fixes it in one step.
#pragma once
#include <algorithm>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/call/refusals.h"
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
    if (arg.type == "obj") {
        std::string keys;
        for (const auto& field : arg.fields) keys += (keys.empty() ? "" : ", ") + std::string(field.key) + (field.required ? "" : "?");
        return "an object {" + keys + "}";
    }
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
    if (arg.type == "obj") {
        // Its required keys; with none, its first.
        Json out = Json::object();
        for (const auto& field : arg.fields)
            if (field.required) out[std::string(field.key)] = Example(field);
        if (out.empty() && !arg.fields.empty()) out[std::string(arg.fields.front().key)] = Example(arg.fields.front());
        return out;
    }
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
    if (arg.type == "obj") {
        if (!value.is_object()) return false;
        for (const auto& [key, item] : value.items()) {
            const auto field = std::ranges::find(arg.fields, std::string_view(key), &Arg::key);
            if (field == arg.fields.end() || !Fits(*field, item)) return false;
        }
        return std::ranges::all_of(arg.fields, [&](const Arg& field) { return !field.required || value.contains(field.key); });
    }
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

namespace check {
// One value against its arg; an object's keys as a call's are, named by their path ("view.limit").
inline std::optional<Refusal> Value(const Arg& arg, const Json& value, const std::string& path) {
    const auto wrong = [](const Arg& at, const Json& item, const std::string& key) {
        return Refusal{refusal::WrongType(key, Takes(at), item.dump(-1, ' ', true, Json::error_handler_t::replace)), key};
    };
    if (arg.type != "obj") {
        if (!Fits(arg, value)) return wrong(arg, value, path);
        return std::nullopt;
    }
    if (!value.is_object()) return wrong(arg, value, path);
    for (const auto& [key, item] : value.items()) {
        const auto field = std::ranges::find(arg.fields, std::string_view(key), &Arg::key);
        if (field == arg.fields.end()) {
            std::string keys;
            for (const auto& each : arg.fields) keys += (keys.empty() ? "" : ", ") + std::string(each.key);
            return Refusal{"unknown key '" + path + "." + key + "'; " + path + " takes " + (keys.empty() ? "none" : keys), path};
        }
        if (!Fits(*field, item)) return wrong(*field, item, path + "." + key);
    }
    for (const auto& field : arg.fields)
        if (field.required && !value.contains(field.key))
            return Refusal{refusal::Missing(path + "." + std::string(field.key), Takes(field), field.help),
                path + "." + std::string(field.key)};
    return std::nullopt;
}
}

// A call's args checked against the row: unknown keys, then missing required
// ones, then each value's type. An object's keys are then also set as the
// verb's own (view's limit as limit), so a builder reads them by name; a row
// never uses one name twice (a table test).
inline std::expected<Args, Refusal> CheckCall(const Verb& verb, const Json& given) {
    using Out = std::expected<Args, Refusal>;
    if (!given.is_null() && !given.is_object()) return Out(std::unexpect, Refusal{"args is an object of named arguments", {}});
    Args args;
    if (given.is_object()) {
        for (const auto& [key, value] : given.items()) {
            const Arg* arg = FindArg(verb, key);
            if (arg) continue;
            // An object's key given as the verb's own: say where it goes.
            if (const auto path = KeyPath(verb, key); !path.empty()) {
                const auto parent = path.substr(0, path.find('.'));
                return Out(std::unexpect, Refusal{refusal::Misplaced(key, parent), path});
            }
            const Arg* closest = check::Nearest(verb, key);
            return Out(std::unexpect, Refusal{refusal::UnknownKey(key, closest ? closest->key : std::string_view{}, verb.name,
                ArgKeys(verb)), key});
        }
    }
    for (const auto& arg : verb.args) {
        const std::string key(arg.key);
        if (!given.is_object() || !given.contains(key)) {
            if (arg.required)
                return Out(std::unexpect, Refusal{refusal::Missing(key, check::Takes(arg), arg.help), key});
            continue;
        }
        const Json& value = given[key];
        if (auto refusal = check::Value(arg, value, key)) return Out(std::unexpect, std::move(*refusal));
        args.values[key] = value;
        if (arg.type == "obj")
            for (const auto& [inner, item] : value.items()) args.values[inner] = item;
    }
    return args;
}

// The key a builder's message is about: a message that starts with one of
// the row's keys ("count must be 1..64") names it; an object's key by its
// path ("limit must be ..." is view.limit).
inline std::string KeyOf(const Verb& verb, std::string_view message) {
    std::string_view best;
    const auto consider = [&](std::string_view key) {
        if (!message.starts_with(key)) return;
        const char next = message.size() > key.size() ? message[key.size()] : ' ';
        if ((next == ' ' || next == ':' || next == ',') && key.size() > best.size()) best = key;
    };
    for (const auto& arg : verb.args) {
        consider(arg.key);
        for (const auto& field : arg.fields) consider(field.key);
    }
    return best.empty() ? std::string() : KeyPath(verb, best);
}

// One call that the row accepts, built from the given one: its values that
// fit are kept, a misspelt key moves to the key it was meant for, the key at
// fault is dropped (an optional one) or replaced by an example of its type,
// and every required key left out gets one.
inline Json ExampleCall(const Verb& verb, const Json& given = Json(), std::string_view fault = {}) {
    Json args = Json::object();
    if (given.is_object()) {
        for (const auto& [key, value] : given.items()) {
            // An object's key given as the verb's own moves into its object.
            if (!FindArg(verb, key)) {
                const auto path = KeyPath(verb, key);
                const Arg* field = path.empty() ? nullptr : FindKey(verb, path);
                if (field) {
                    // Moved even when it is the key at fault: its place was the fault, not its value.
                    const auto parent = path.substr(0, path.find('.'));
                    if (!given.contains(parent) && check::Fits(*field, value)) args[parent][key] = value;
                    continue;
                }
            }
            const Arg* arg = FindArg(verb, key);
            if (!arg) arg = check::Nearest(verb, key);
            if (!arg || args.contains(arg->key) || (arg->key != key && given.contains(arg->key))) continue;
            // An object at fault keeps the keys of it that fit; another arg at fault is dropped.
            if (arg->key == fault && !(arg->type == "obj" && value.is_object())) continue;
            if (arg->type == "obj" && value.is_object()) {
                // Its keys that fit, less the one at fault.
                Json kept = Json::object();
                for (const auto& [inner, item] : value.items()) {
                    const auto field = std::ranges::find(arg->fields, std::string_view(inner), &Arg::key);
                    if (field != arg->fields.end() && std::string(arg->key) + "." + inner != fault && check::Fits(*field, item))
                        kept[inner] = item;
                }
                for (const auto& field : arg->fields)
                    if (field.required && !kept.contains(field.key)) kept[std::string(field.key)] = check::Example(field);
                if (!kept.empty()) args[std::string(arg->key)] = std::move(kept);
                continue;
            }
            if (!check::Fits(*arg, value)) continue;
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
    if (arg.type == "obj") {
        Json fields = Json::array();
        for (const auto& field : arg.fields) fields.push_back(ArgJson(field));
        out["fields"] = std::move(fields);
    }
    return out;
}

// A refused call's error: kind schema, the message, the key at fault and its
// part of the schema (else the whole row's args), and the corrected example.
inline Json RefusalJson(const Verb& verb, const Json& given, const Refusal& refusal) {
    Json error = {{"kind", KindName(ErrorKind::Schema)}, {"message", refusal.message}};
    const Arg* arg = refusal.key.empty() ? nullptr : FindKey(verb, refusal.key);
    if (arg) {
        error["key"] = refusal.key;
        error["arg"] = ArgJson(*arg);
    } else {
        if (!refusal.key.empty()) error["key"] = refusal.key;
        Json args = Json::array();
        for (const auto& each : verb.args) args.push_back(ArgJson(each));
        error["args"] = std::move(args);
    }
    error["example"] = ExampleCall(verb, given, arg ? std::string_view(refusal.key) : std::string_view{});
    return error;
}
}
