// reagentlive CLI: Parse's verbs by area, one file each. Each answers nullopt for a
// verb that is not its own.
#pragma once
#include <optional>
#include <span>
#include <string_view>
#include "reagentlive/frontends/cli/syntax/parse.h"

namespace reagentlive::cli::parse {
std::optional<Parsed> Memory(std::string_view verb, std::string_view rest, std::span<const Ref> refs,
                             std::span<const Module> modules);
std::optional<Parsed> Unreal(std::string_view verb, std::string_view rest, std::span<const Ref> refs,
                             std::span<const Module> modules);
std::optional<Parsed> Code(std::string_view verb, std::string_view rest, std::span<const Ref> refs,
                           std::span<const Module> modules);
}
