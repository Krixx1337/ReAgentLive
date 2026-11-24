// Shared by the call tests: modules and refs the calls use, and helpers that
// build a call or read why it was refused.
#pragma once
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/call/build.h"
#include "doctest.h"
#include "verbs.h"

namespace call_support {
using namespace reagentlive;
using namespace reagentlive::cli;
inline const std::vector<Module> NoModules;
inline const std::vector<Module> Loaded{{"app.exe", 0x7ff600000000, 0x100000}, {"engine.dll", 0x7ffa00000000, 0x2000}};
// The literal refs the calls below use as "0x1000#5:9" and "0x2000#7:0".
inline const Ref One{0x1000, 5, 9}, Two{0x2000, 7, 0};

inline const Verb& Row(std::string_view name) {
    const Verb* verb = FindVerb(AllVerbs(), name);
    REQUIRE_MESSAGE(verb != nullptr, name);
    return *verb;
}

// A refused call's message; a call that builds fails the check.
inline std::string Why(const Json& call, std::span<const Module> modules = {}) {
    const auto parsed = BuildCall(AllVerbs(), call, modules);
    CHECK_MESSAGE(!parsed.has_value(), call.dump());
    return parsed ? std::string() : parsed.error();
}
inline std::string Why(const char* call, std::span<const Module> modules = {}) { return Why(Json::parse(call), modules); }

// Whether a call passes the schema checks and its builder.
inline bool Builds(const Json& call, std::span<const Module> modules = {}) {
    const auto built = BuildCall(AllVerbs(), call, modules);
    if (!built) MESSAGE(call.dump() << ": " << built.error());
    return built.has_value();
}

// The error object a console answers for a refused call: the schema's
// refusal, else the builder's, keyed by the key its message starts with.
inline Json Refused(std::string_view text) {
    const Json call = Json::parse(text);
    const Verb& verb = Row(call["verb"].get<std::string>());
    const Json given = call.value("args", Json());
    Refusal refusal;
    if (auto args = CheckCall(verb, given); !args) {
        refusal = args.error();
    } else {
        const auto built = Build(verb, *args);
        REQUIRE_MESSAGE(!built.has_value(), text);
        refusal = {built.error(), KeyOf(verb, built.error())};
    }
    return RefusalJson(verb, given, refusal);
}
}
