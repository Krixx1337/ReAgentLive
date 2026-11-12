// reagentlive CLI: interactive console over the client API (core + Unreal pack).
#pragma once
#include <cstdint>
#include <span>
#include <string_view>
#include <nlohmann/json_fwd.hpp>
#include "reagentlive/frontends/cli/syntax/verb.h"

namespace reagentlive { class Client; }
namespace reagentlive::cli {
uint32_t FindProcess(std::wstring_view exeName);
struct ConsoleConfig {
    std::wstring_view sectionPrefix;   // Section name = prefix + target pid.
    std::wstring_view exeName;         // Target found by exe name when no pid is given.
    uint32_t layout;                   // Same sum as the agent's Host::layout.
    // The host's own verbs: rows of the verb table (syntax/verb.h; a result
    // type comes from results/schema.h), so they get help, usage errors and
    // checked results like the core's. `parse` stays null: a line whose verb
    // has a row here goes to hostCommand whole, which sets an envelope with
    // ok and result or error. A result that does not match its row's type is
    // refused as an internal error.
    std::span<const Verb> verbs = {};
    bool (*hostCommand)(Client&, std::string_view, nlohmann::json& envelope) = nullptr;
    // Readable names file (names.h format) next to the console exe; loaded
    // when present. "--names <file>" overrides it and must exist.
    std::wstring_view namesFile = {};
};
// Arguments: [pid] [-c "command; command ..."] [--names <file>].
// Commands from -c and stdin emit JSON Lines and exit 1 if any command failed.
// -c lines that are all `help` are answered from the verb table, with no
// target running.
// "--replay <file>" shows a capture without attaching.
int RunConsole(const ConsoleConfig& config, int argc, wchar_t** argv);
}
