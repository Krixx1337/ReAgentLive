// reagentlive CLI: interactive console over the client API (core + Unreal pack).
#pragma once
#include <cstdint>
#include <span>
#include <string_view>
#include <nlohmann/json_fwd.hpp>
#include "reagentlive/frontends/cli/call/verb.h"

namespace reagentlive { class Client; }
namespace reagentlive::cli {
uint32_t FindProcess(std::wstring_view exeName);
struct ConsoleConfig {
    std::wstring_view sectionPrefix;   // Section name = prefix + target pid.
    std::wstring_view exeName;         // Target found by exe name when no pid is given.
    uint32_t layout;                   // Same sum as the agent's Host::layout.
    // The host's own verbs: rows of the verb table (call/verb.h; a result
    // type comes from results/schema.h) with origin "host", so they get
    // help, schema refusals and checked results like the core's. `build`
    // stays null: the console checks a call's args against the row
    // (call/check.h) and hands them to hostCommand, which sets an envelope
    // with ok and result or error. A result that does not match its row's
    // type is refused as an internal error.
    std::span<const Verb> verbs = {};
    bool (*hostCommand)(Client&, const Verb& verb, const Args& args, nlohmann::json& envelope) = nullptr;
    // Readable names file (names.h format) next to the console exe; loaded
    // when present. "--names <file>" overrides it and must exist.
    std::wstring_view namesFile = {};
    // Where names people confirm go (labels format: `user` rows from a static
    // tool, `global` rows from peek with `label`): absolute, or relative to the
    // console exe. A host keeps it under version control; derived names stay
    // in the console's own <console>.labels.jsonl. Empty: that file holds both.
    std::wstring_view labelsFile = {};
};
// Arguments: [pid] [-c '<call or list of calls>' | -c @<file>] [--names <file>]
// [--packs <list>] (the tool packs whose verbs run; default all, explore always).
// Calls from -c and stdin emit JSON Lines and exit 1 if any call failed. A
// call is {"verb": ..., "args": {...}}; a stdin line holds one call or a
// list of them, a -c file one call per line or a list. -c calls that are all
// `help` or `usage` are answered with no target running. --check attaches to
// nothing: each call is checked as it would be before it runs and answered
// ok ({"checked": true}) or with its refusal.
int RunConsole(const ConsoleConfig& config, int argc, wchar_t** argv);
}
