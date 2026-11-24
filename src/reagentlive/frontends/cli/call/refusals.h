// reagentlive CLI: the error kinds a console answers with, and the message
// templates every caller shares. The one place a kind is written: an error
// names its kind by ErrorKind, the reference page lists this catalogue
// (docs/commands/reference.md), and a host's own verbs may add kinds of
// their own. Messages are for AI agents: they name the key and what fits.
#pragma once
#include <iterator>
#include <string>
#include <string_view>

namespace reagentlive::cli {
enum class ErrorKind {
    Schema, Pack, Validation, Unsupported, File, Agent, Timeout, Attach, Busy, Startup, Read, Module, Unwind,
    Start, Label, Snapshot, Field, Layout, Chain, Host, Internal
};

struct ErrorKindInfo {
    ErrorKind kind;
    std::string_view name, meaning, message;    // message: the template, <placeholders> in angle brackets.
};
inline constexpr ErrorKindInfo ErrorKinds[] = {
    {ErrorKind::Schema, "schema", "the call does not fit its verb's row, or a builder refuses a value",
        "unknown key '<key>' (did you mean <key>?); <verb> takes <keys> | missing <key>: <type> | "
        "<key>: <type>, not <value> | '<key>' goes inside <object> | <key>: <why>"},
    {ErrorKind::Pack, "pack", "the verb's tool pack is outside this console's --packs",
        "<verb> is in the <pack> pack, outside this console's --packs <packs>: start it with <pack> in --packs"},
    {ErrorKind::Validation, "validation", "the target refused what the call asks (a guard, a state, a mismatch)",
        "<what> refused: <why>"},
    {ErrorKind::Unsupported, "unsupported", "the agent or host lacks the operation", "agent does not support <operation>"},
    {ErrorKind::File, "file", "a file the call names cannot be read or written", "cannot read|write <path>: <why>"},
    {ErrorKind::Agent, "agent", "the agent refused the request", "<op>: <reason>"},
    {ErrorKind::Timeout, "timeout", "the agent did not answer in time", "<op> timed out"},
    {ErrorKind::Attach, "attach", "the console cannot reach an agent", "no agent in <pid>: <why>"},
    {ErrorKind::Busy, "busy", "another console holds the agent", "another client is attached"},
    {ErrorKind::Startup, "startup", "the console could not start (arguments, packs, agent)", "<why>"},
    {ErrorKind::Read, "read", "memory the call needs is unreadable", "<address>: <why>"},
    {ErrorKind::Module, "module", "the address or name is in no loaded module", "the address lies in no loaded module"},
    {ErrorKind::Unwind, "unwind", "the exception table has no usable entry for the code", "<why>"},
    {ErrorKind::Start, "start", "a code address is no place to start (a walk or a save)", "nothing to save at <address>: <why>"},
    {ErrorKind::Label, "label", "a labels file or a label cannot be used", "<why>"},
    {ErrorKind::Snapshot, "snapshot", "get's baseline is missing or unreadable", "changed needs get with snap first (or a snapshot file)"},
    {ErrorKind::Field, "field", "the object has no such field, or it is of another kind", "<field> is <type>, not <wanted>"},
    {ErrorKind::Layout, "layout", "a layout the console relies on failed its check", "<why>"},
    {ErrorKind::Chain, "chain", "a pointer chain broke", "hop <n>: <reason>"},
    {ErrorKind::Host, "host", "a host verb returned nothing usable", "host command returned no result"},
    {ErrorKind::Internal, "internal", "a console fault: a result that does not match its row", "<why>"},
};
inline constexpr std::string_view KindName(ErrorKind kind) { return ErrorKinds[static_cast<size_t>(kind)].name; }

// The shared message templates.
namespace refusal {
inline std::string UnknownKey(std::string_view key, std::string_view nearest, std::string_view verb, std::string_view keys) {
    return "unknown key '" + std::string(key) + "'" + (nearest.empty() ? "" : " (did you mean " + std::string(nearest) + "?)") +
        "; " + std::string(verb) + " takes " + std::string(keys);
}
inline std::string Missing(std::string_view key, std::string_view takes, std::string_view help) {
    return "missing " + std::string(key) + ": " + std::string(takes) + (help.empty() ? "" : " (" + std::string(help) + ")");
}
inline std::string WrongType(std::string_view key, std::string_view takes, std::string_view value) {
    return std::string(key) + ": " + std::string(takes) + ", not " + std::string(value);
}
inline std::string Misplaced(std::string_view key, std::string_view object) {
    return "'" + std::string(key) + "' goes inside " + std::string(object) + ": {\"" + std::string(object) + "\": {\"" +
        std::string(key) + "\": ...}}";
}
inline std::string OutsidePacks(std::string_view verb, std::string_view pack, std::string_view packs) {
    return std::string(verb) + " is in the " + std::string(pack) + " pack, outside this console's --packs " +
        std::string(packs) + ": start it with " + std::string(pack) + " in --packs";
}
}
}
