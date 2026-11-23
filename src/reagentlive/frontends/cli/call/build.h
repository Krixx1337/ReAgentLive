// reagentlive CLI: a call to a Command. Host side only; pure, so tests cover it.
#pragma once
#include <expected>
#include <optional>
#include <span>
#include <string>
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
// A call's own keys (verb, args, meta, timing, max_chars, big); why not, or empty.
std::string CallProblem(const Json& call);
// The verb `name` with its args, as the console checks it before it runs:
// the row's schema (check.h), then its builder. A refusal is the error object
// a console answers (kind schema, the key's schema, a corrected example).
std::expected<Command, Json> BuildChecked(std::span<const Verb> verbs, std::string_view name, const Json& args,
                                          std::span<const Module> modules);
// With no target: a call, or a plan step ({verb, args?, expect?, repeat?}),
// checked as above. A host row (no builder) by its schema alone; a batch's
// calls each; every module the call names counts as loaded. nullopt when it
// passes, else the refusal. The console's --check and reagentlive-table
// --check-calls answer with it, so docs' examples meet the real checks.
std::optional<Json> CheckWithoutTarget(std::span<const Verb> verbs, const Json& call);
}
