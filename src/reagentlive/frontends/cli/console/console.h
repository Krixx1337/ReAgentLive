// reagentlive CLI: interactive console over the client API (core + Unreal pack).
#pragma once
#include <cstdint>
#include <string_view>
#include <nlohmann/json_fwd.hpp>

namespace reagentlive { class Client; }
namespace reagentlive::cli {
uint32_t FindProcess(std::wstring_view exeName);
struct ConsoleConfig {
    std::wstring_view sectionPrefix;   // Section name = prefix + target pid.
    std::wstring_view exeName;         // Target found by exe name when no pid is given.
    uint32_t layout;                   // Same sum as the agent's Host::layout.
    // Handled commands set an envelope with ok and result or error.
    bool (*hostCommand)(Client&, std::string_view, nlohmann::json& envelope) = nullptr;
    // Readable names file (names.h format) next to the console exe; loaded
    // when present. "--names <file>" overrides it and must exist.
    std::wstring_view namesFile = {};
    std::string_view hostHelp = {};
};
// Arguments: [pid] [-c "command; command ..."] [--names <file>].
// Commands from -c and stdin emit JSON Lines and exit 1 if any command failed.
// "--replay <file>" shows a capture without attaching.
int RunConsole(const ConsoleConfig& config, int argc, wchar_t** argv);
}
