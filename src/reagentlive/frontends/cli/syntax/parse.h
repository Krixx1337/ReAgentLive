// reagentlive CLI: console syntax for the core and Unreal pack ops.
#pragma once
#include <span>
#include <string_view>
#include "reagentlive/frontends/cli/syntax/command.h"
#include "reagentlive/frontends/cli/syntax/text.h"
#include "reagentlive/frontends/cli/syntax/verb.h"

// A console line to a Command. Host side only; pure, so tests cover it. The
// verb table (verb.h) says what each verb takes: Parse finds the line's row,
// sorts the rest of the line into the row's positionals and flags, and hands
// them to the row's parser. `help` lists the verbs; `help <verb>` shows one.
// Numbers: 0x-prefixed hex, otherwise decimal. <addr> and <object> accept $N
// (result N of the last find) or a literal ref 0x<address>#<index>:<serial>
// as the console prints it, so refs survive between console runs. Addresses
// also accept +offset ($2+0x120), "base" for the exe (base+0x3ef8ee0) and a
// loaded module's name (game.exe+0x3ef8ee0) once the console has the module
// list; a module offset must lie inside the image. A token in double quotes
// is taken exactly.
namespace reagentlive::cli {
// verbs: the table (the core's rows, a pack's, a host's). modules: the
// loaded modules, exe first, or empty while unknown ("base" and module names
// then fail). An error names what is wrong and ends with the verb's usage line.
Parsed Parse(std::span<const Verb> verbs, std::string_view line, std::span<const Ref> refs,
             std::span<const Module> modules = {});
}
