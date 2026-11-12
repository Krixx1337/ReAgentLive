// The whole verb table for the tests (the core's rows and the Unreal
// pack's), and Parse over it.
#pragma once
#include <span>
#include <string_view>
#include <vector>
#include "doctest.h"
#include "reagentlive/frontends/cli/results/ue/verbs_ue.h"
#include "reagentlive/frontends/cli/results/verbs.h"

inline const std::vector<reagentlive::cli::Verb>& AllVerbs() {
    static const std::vector<reagentlive::cli::Verb> verbs = [] {
        auto all = reagentlive::cli::CoreVerbs();
        for (const auto& verb : reagentlive::cli::UnrealVerbs()) all.push_back(verb);
        return all;
    }();
    return verbs;
}

inline std::span<const reagentlive::cli::Origin> AllOrigins() {
    static const reagentlive::cli::Origin origins[] = {reagentlive::cli::CoreOrigin, reagentlive::cli::UnrealOrigin,
                                                       reagentlive::cli::HostOrigin};
    return origins;
}

// The result as JSON, after checking it against the type its struct describes.
template<class T> reagentlive::cli::Json Checked(const T& result) {
    const reagentlive::cli::Json json = reagentlive::cli::ToJson(result);
    CHECK(reagentlive::cli::schema::Problem(reagentlive::cli::schema::Of<T>(), json) == "");
    return json;
}

inline reagentlive::cli::Parsed Parse(std::string_view line, std::span<const reagentlive::Ref> refs,
                                      std::span<const reagentlive::Module> modules = {}) {
    return reagentlive::cli::Parse(AllVerbs(), line, refs, modules);
}
