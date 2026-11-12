// reagentlive CLI console without an engine pack (REAGENTLIVE_CONSOLE_UE off):
// pack.h answered for "no pack". No verbs, no state, no cache block, nothing
// offered; the core rows that need a pack leave the table (TableRows).
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "reagentlive/frontends/cli/console/internal.h"
#include "reagentlive/frontends/cli/console/pack.h"

namespace reagentlive::cli::detail {
namespace {
constexpr auto NoPack = "needs an engine pack: this console was built without one";
}

struct PackState {};

std::shared_ptr<PackState> MakePackState(Session&) { return std::make_shared<PackState>(); }
std::string LoadPackFiles(Session&) { return {}; }
void LoadPackCache(Session&, std::span<const CachePack>) {}
std::vector<CachePack> SavePackCache(const Session&) { return {}; }

std::span<const Verb> PackVerbs() { return {}; }
std::span<const Origin> PackOrigins() { return {}; }
std::span<const std::string> PackNotes() { return {}; }
std::vector<std::string> OfferedPacks(Session&) { return {}; }

// No row parses to a pack command; a raw op in the pack range is refused.
bool PackCommand(const Command& command) {
    return command.kind == Kind::Remote && (!command.className.empty() ||
        (command.request.op >= reagentlive::op::PackFirst && command.request.op < reagentlive::op::GameFirst));
}
bool PackHeld(const Command&) { return false; }
void RunPackCommand(Session& session, const Command&, std::string_view jsonCommand) {
    JsonError(session, jsonCommand, "unsupported", NoPack);
}

// An object-relative pin or watch has no identity to guard on.
Result<Guard> ObjectGuard(Session&, uint64_t object) {
    if (!object) return Guard{};
    return std::unexpected(Error{Error::Kind::Agent, Status::Unsupported, 0, {},
        "an object-relative address needs an engine pack's identity guard: this console was built without one"});
}
std::expected<uint32_t, std::string> TrialOwnerSize(Session&, const Ref&) { return std::unexpected(NoPack); }
bool PackCall(const Command&) { return false; }
shape::WatchThen RunPackCall(Session&, const Command&) { return {false, std::nullopt, NoPack}; }
void DescribePeek(Session&, std::span<PeekValue>) {}
void NamePeek(Session&, std::span<PeekValue>, std::span<const Module>) {}

int Replay(const std::filesystem::path&) {
    std::puts("--replay needs an engine pack: this console was built without one");
    return 1;
}
}
