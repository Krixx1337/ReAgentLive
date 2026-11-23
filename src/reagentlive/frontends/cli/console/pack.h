// reagentlive CLI console: what the core console asks of an engine pack. The
// core files reach the pack only through these; packs/ue/cli/console/pack.cpp
// implements them for the Unreal pack, console/no_pack.cpp for a console
// built without one (REAGENTLIVE_PACKS empty). With one pack they are
// plain functions; a second pack would turn them into a table (docs/design.md).
// A pack's verbs are rows of the verb table like the core's (PackVerbs).
#pragma once
#include <cstdint>
#include <expected>
#include <optional>
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
// The pack's rows of the verb table and the notes `help` prints under them;
// its origin, none without a pack. status: `packs`, the packs this host offers.
std::span<const Verb> PackVerbs();
std::span<const Origin> PackOrigins();
std::span<const std::string> PackNotes();
std::vector<std::string> OfferedPacks(Session& session);
// status: `game_thread`, the pack's game-thread tick (its own result type),
// or nullopt when the host has none.
std::optional<Json> PackGameThread(Session& session);
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
// watch, watch-start: a range named <object>.<Field[.Field]>: the field's
// address, size and watch element type (WatchTypeCode) from reflection, and
// the object holding it (the range's guard), or why it cannot be watched.
struct WatchFieldRange { uint64_t address, object; uint32_t length, type; };
std::expected<WatchFieldRange, std::string> PackWatchField(Session& session, const Ref& object, std::string_view path);
// write --undo: the owner's size, or why the object cannot own a trial.
std::expected<uint32_t, std::string> TrialOwnerSize(Session& session, const Ref& owner);
// peek: which pointer-shaped values are live objects, then name hints for value rows.
void DescribePeek(Session& session, std::span<PeekValue> values);
void NamePeek(Session& session, std::span<PeekValue> values, std::span<const Module> modules);
}
