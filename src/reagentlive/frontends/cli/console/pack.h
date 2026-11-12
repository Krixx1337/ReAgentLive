// reagentlive CLI console: what the core console asks of an engine pack. The
// core files reach the pack only through these; console/ue/pack.cpp
// implements them for the Unreal pack. With one pack they are plain
// functions; a second pack would turn them into a table (docs/design.md).
#pragma once
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/state/cache.h"
#include "reagentlive/frontends/cli/console/internal.h"

namespace reagentlive::cli::detail {
// Startup: the pack's state for this run, then its files beside the console
// (a non-empty error stops the console).
std::shared_ptr<PackState> MakePackState(Session& session);
std::string LoadPackFiles(Session& session);
// The cache file: the pack's blocks, kept by the core without reading them.
void LoadPackCache(Session& session, std::span<const CachePack> packs);
std::vector<CachePack> SavePackCache(const Session& session);
// help: the pack's verbs. status: `packs`, the packs this host offers.
std::string PackHelp();
void AddPackStatus(Session& session, Json& status);
// Pack verbs: whether a parsed command is one, whether it runs held (several
// round trips), and running it. Refused with a clear message when the host
// does not offer the pack.
bool PackCommand(const Command& command);
bool PackHeld(const Command& command);
void RunPackCommand(Session& session, const Command& command, std::string_view jsonCommand);

// What core commands borrow from the pack.
// pin, watch, write --trial on an object-relative address: the object's
// identity bytes, read now, so the agent stops when the object is replaced.
Result<Guard> ObjectGuard(Session& session, uint64_t object);
// write --trial: the owner's size, or why the object cannot own a trial.
std::expected<uint32_t, std::string> TrialOwnerSize(Session& session, const Ref& owner);
// watch --then: whether the command is a call the pack runs, and running it.
bool PackCall(const Command& command);
Json RunPackCall(Session& session, const Command& call);
// peek: which pointer-shaped values are live objects, then name hints for value rows.
void DescribePeek(Session& session, std::span<PeekValue> values);
void NamePeek(Session& session, std::span<PeekValue> values, std::span<const Module> modules);
// --replay <file>: print a record file.
int Replay(const std::filesystem::path& path);
}
