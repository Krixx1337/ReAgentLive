// reagentlive CLI: a plan file for `run` (J5). A plan is a JSON list of
// steps, each {verb, args?, expect?, repeat?: {every, for}}; the console runs
// every step as its own call (the packs ceiling applies per step) and writes
// one NDJSON line per result, then a report line. The file is the
// investigation's replay key: run it again to repeat it. Pure; tests cover it.
#pragma once
#include <cmath>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

namespace reagentlive::cli::plan {
using Json = nlohmann::json;

inline constexpr size_t MaxSteps = 256;
inline constexpr double MaxRepeatSeconds = 600;     // A step repeats for at most this long.
inline constexpr size_t MaxRunsPerStep = 1000;      // for / every, at most.

struct Step {
    std::string verb;
    Json call;                      // {"verb", "args"} as the console takes it.
    std::optional<Json> expect;     // A subset of the envelope ({"ok": true, "result": {...}}).
    double every = 0, seconds = 0;  // repeat: every `every` seconds for `seconds`; 0: once.
    size_t Runs() const { return every > 0 ? static_cast<size_t>(std::floor(seconds / every)) + 1 : 1; }
};

// The steps of a plan's text; why not, naming the step (step 3: ...).
inline std::expected<std::vector<Step>, std::string> Parse(std::string_view text) {
    using Out = std::expected<std::vector<Step>, std::string>;
    const Json plan = Json::parse(text, nullptr, false);
    if (plan.is_discarded()) return Out(std::unexpect, "plan: not JSON");
    if (!plan.is_array() || plan.empty())
        return Out(std::unexpect, "plan: a JSON list of steps, each {\"verb\", \"args\"?, \"expect\"?, \"repeat\"?}");
    if (plan.size() > MaxSteps) return Out(std::unexpect, "plan: at most " + std::to_string(MaxSteps) + " steps");
    std::vector<Step> steps;
    for (size_t i = 0; i < plan.size(); ++i) {
        const Json& item = plan[i];
        const std::string at = "plan step " + std::to_string(i + 1) + ": ";
        if (!item.is_object()) return Out(std::unexpect, at + "a step is an object {\"verb\", \"args\"?, \"expect\"?, \"repeat\"?}");
        for (const auto& [key, _] : item.items())
            if (key != "verb" && key != "args" && key != "expect" && key != "repeat")
                return Out(std::unexpect, at + "unknown key '" + key + "' (verb, args, expect, repeat)");
        if (!item.contains("verb") || !item["verb"].is_string()) return Out(std::unexpect, at + "verb is the verb's name");
        Step step;
        step.verb = item["verb"].get<std::string>();
        if (step.verb == "run" || step.verb == "quit") return Out(std::unexpect, at + step.verb + " is not allowed in a plan");
        if (item.contains("args") && !item["args"].is_object()) return Out(std::unexpect, at + "args is an object");
        step.call = {{"verb", step.verb}, {"args", item.value("args", Json::object())}};
        if (item.contains("expect")) {
            if (!item["expect"].is_object()) return Out(std::unexpect, at + "expect is an object: a subset of the envelope");
            step.expect = item["expect"];
        }
        if (item.contains("repeat")) {
            const Json& repeat = item["repeat"];
            if (!repeat.is_object() || repeat.size() != 2 || !repeat.contains("every") || !repeat.contains("for") ||
                !repeat["every"].is_number() || !repeat["for"].is_number())
                return Out(std::unexpect, at + "repeat is {\"every\": seconds, \"for\": seconds}");
            step.every = repeat["every"].get<double>();
            step.seconds = repeat["for"].get<double>();
            if (!(step.every > 0) || !(step.seconds > 0) || step.seconds > MaxRepeatSeconds)
                return Out(std::unexpect, at + "repeat: every above 0, for 0.." + std::to_string(static_cast<int>(MaxRepeatSeconds)) + " s");
            if (step.Runs() > MaxRunsPerStep)
                return Out(std::unexpect, at + "repeat: at most " + std::to_string(MaxRunsPerStep) + " runs (for / every)");
        }
        steps.push_back(std::move(step));
    }
    return steps;
}

// Where `actual` departs from `expected`: every key of an expected object
// must be in the actual one and match; a list matches element by element
// at the same length; scalars are equal (numbers by value). Empty: it holds.
inline std::string Mismatch(const Json& expected, const Json& actual, const std::string& path = "") {
    const auto show = [](const Json& value) {
        std::string text = value.dump(-1, ' ', true, Json::error_handler_t::replace);
        return text.size() > 80 ? text.substr(0, 77) + "..." : text;
    };
    const std::string here = path.empty() ? std::string("envelope") : path;
    if (expected.is_object()) {
        if (!actual.is_object()) return here + ": wanted an object, got " + show(actual);
        for (const auto& [key, value] : expected.items()) {
            const std::string inner = path.empty() ? key : path + "." + key;
            if (!actual.contains(key)) return inner + ": wanted " + show(value) + ", absent";
            if (auto why = Mismatch(value, actual[key], inner); !why.empty()) return why;
        }
        return {};
    }
    if (expected.is_array()) {
        if (!actual.is_array() || actual.size() != expected.size())
            return here + ": wanted a list of " + std::to_string(expected.size()) + ", got " + show(actual);
        for (size_t i = 0; i < expected.size(); ++i)
            if (auto why = Mismatch(expected[i], actual[i], here + "[" + std::to_string(i) + "]"); !why.empty()) return why;
        return {};
    }
    const bool same = expected.is_number() && actual.is_number() ? expected.get<double>() == actual.get<double>() : expected == actual;
    return same ? std::string() : here + ": wanted " + show(expected) + ", got " + show(actual);
}

// What a run adds up, for its report line.
struct Tally {
    struct StepTime {
        size_t step = 0;
        std::string verb;
        uint64_t calls = 0;
        double ms = 0;
    };
    uint64_t calls = 0, ok = 0, failed = 0, expectMet = 0, expectFailed = 0;
    std::map<std::string, uint64_t> refused;    // By error kind.
    std::vector<StepTime> steps;
    double ms = 0;

    // One step's envelope; its `expect` line, if any, comes back.
    void Count(const Json& envelope) {
        ++calls;
        if (envelope.value("ok", false)) ++ok;
        else {
            ++failed;
            const Json error = envelope.value("error", Json::object());
            ++refused[error.is_object() ? error.value("kind", std::string("error")) : std::string("error")];
        }
    }
};
}
