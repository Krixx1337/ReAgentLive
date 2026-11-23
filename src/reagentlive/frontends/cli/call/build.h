// reagentlive CLI: a call to a Command. Host side only; pure, so tests cover it.
#pragma once
#include <span>
#include <string_view>
#include "reagentlive/frontends/cli/call/check.h"
#include "reagentlive/frontends/cli/call/command.h"
#include "reagentlive/frontends/cli/call/values.h"
#include "reagentlive/frontends/cli/call/verb.h"

// The verb table (verb.h) says what each verb takes: check.h accepts a JSON
// call's args against the verb's row, then Build hands them to the row's
// builder, which reads the typed values (values.h), checks ranges and
// combinations, and fills a Command. `modules`: the loaded modules, exe
// first, or empty while unknown ({"module", "rva"} addresses then fail).
namespace reagentlive::cli {
// Checked args to a Command by the row's builder. An error that is about one
// key starts with it (check.h KeyOf).
Parsed Build(const Verb& verb, const Args& args, std::span<const Module> modules = {});
// A whole call ({"verb", "args"}): the verb, the checks and the build; for
// tests and the no-target mode. Errors as RefusalJson's message.
Parsed BuildCall(std::span<const Verb> verbs, const Json& call, std::span<const Module> modules = {});
}
