// reagentlive CLI console, Unreal pack: what the core asks of the pack
// (pack.h), and running the pack's verbs.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <vector>
#include "reagentlive/packs/ue/cli/results/call_text.h"
#include "reagentlive/packs/ue/cli/state/cache_ue.h"
#include "reagentlive/packs/ue/cli/values/format_ue.h"
#include "reagentlive/core/client.h"
#include "reagentlive/packs/ue/client_ue.h"

#include "reagentlive/packs/ue/cli/console/internal_ue.h"

namespace reagentlive::cli::detail {
namespace {
constexpr auto NoPack = "needs the Unreal pack: this host does not offer it (status lists the host's packs)";

// The host offers the pack when it answers any of the pack's ops.
bool HasUnreal(const Client& client) {
    for (uint32_t op = ue::op::Find; op <= ue::op::Entries; ++op)
        if (client.Supports(op)) return true;
    return false;
}

// census, or census with `diff`: only the classes whose count differs from the
// last census under the same `cls`. Either way the new census becomes the
// baseline; `view.match` and `view.limit` only shape what is printed.
void RunCensus(Session& session, const Command& command, std::vector<Ref>& refs, std::string_view jsonCommand) {
    const Request& request = command.request;
    const std::string className(request.length ? reinterpret_cast<const char*>(request.data) : "");
    // The filter a census is stored and compared under: its class and outer, as the args read.
    const std::string filter = command.outer.empty() ? className :
        (className.empty() ? "" : className + " ") + "outer " + command.outer;
    const CensusPage page{command.from, command.limit};
    ue::CensusResult census;
    int64_t ms = 0;
    if (page.from) {
        // A later page: the stored census, sliced; no scan, and the baselines stay.
        if (const auto problem = CensusPageProblem(Ue(session).census, Ue(session).censusBefore, filter, command.changed);
            !problem.empty()) return JsonError(session, jsonCommand, ErrorKind::Validation, problem);
        census = StoredCensus(*Ue(session).census);
    } else {
        if (command.changed)
            if (const auto problem = CensusBaselineProblem(Ue(session).census, filter); !problem.empty())
                return JsonError(session, jsonCommand, ErrorKind::Validation, problem);
        const auto started = Clock::now();
        auto scanned = ue::Census(session.client, className, {command.outerText, command.outerRef});
        if (!scanned) return CommandError(session, jsonCommand, scanned.error());
        // Nothing under a class filter: a name that is no class is refused, not counted as none.
        if (scanned->rows.empty() && !className.empty())
            if (auto type = ResolveClassAnyCase(session, className); !type)
                return CommandError(session, jsonCommand, type.error());
        ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
        census = std::move(*scanned);
        Ue(session).censusBefore = std::move(Ue(session).census);
        Ue(session).census = MakeCensusBaseline(filter, census);
    }
    refs.clear();
    if (command.changed) {
        auto changes = CensusChanges(*Ue(session).censusBefore, census);
        KeepMatching(changes, command.match, [](const CensusChange& c) -> const std::string& { return c.name; });
        const auto [first, shown] = CensusSlice(changes.size(), page);
        for (const auto& change : std::span(changes).subspan(first, shown)) refs.push_back(change.ref);
        Emit(session, jsonCommand, MakeCensusDiff(*Ue(session).censusBefore, census, changes, page, ms));
    } else {
        auto rows = census.rows;
        KeepMatching(rows, command.match, [](const ue::CensusRow& r) -> const std::string& { return r.type.name; });
        const auto [first, shown] = CensusSlice(rows.size(), page);
        for (const auto& row : std::span(rows).subspan(first, shown)) refs.push_back(row.type.ref);
        Emit(session, jsonCommand, MakeCensus(filter, census, rows, page, ms));
    }
}

void RunUnreal(Session& session, const Command& original, std::vector<Ref>& refs, StructCache& structs,
               const std::optional<Snapshot>& baseline, std::string_view jsonCommand) {
    Client& client = session.client;
    Command command = original;
    const Request& request = command.request;
    if (!client.Supports(request.op))
        return JsonError(session, jsonCommand, ErrorKind::Unsupported, "agent does not support this operation");
    if (command.kind == Kind::Each) return RunEach(session, command, refs, structs, jsonCommand);
    if (!command.className.empty()) {
        auto type = ResolveObjectOrClass(session, command.className);
        if (!type) return CommandError(session, jsonCommand, type.error());
        ue::SetRef(command.request, *type);
        if (command.className.starts_with("Default__")) command.className.clear(); // An object, not a class target.
    }
    if (command.kind == Kind::Dump) {
        Ref object = ue::GetRef(request);
        if (object.index < 0) { // An address alone: the object reads need its full ref, as describe gives it.
            auto described = ue::Describe(client, object);
            if (!described) return CommandError(session, jsonCommand, described.error());
            object = described->ref;
        }
        return PrintDump(session, object, refs, structs, command, baseline);
    }
    if (command.kind == Kind::Script) {
        if (!command.request.text[0]) { // object is the Function: its outer owns it, its name names it.
            auto described = ue::Describe(client, ue::GetRef(request));
            if (!described) return CommandError(session, jsonCommand, described.error());
            if (!std::string_view(described->className).ends_with("Function") || !described->outer.address)
                return JsonError(session, jsonCommand, ErrorKind::Validation, "function: object is a " +
                    described->className + ", not a Function: pass function, or the Function's own ref");
            ue::SetRef(command.request, described->outer);
            CopyText(command.request.text, described->name);
        }
        const auto object = ue::GetRef(request);
        auto function = ue::Function(client, object, request.text);
        if (!function && function.error().kind == Error::Kind::Agent && function.error().status == Status::NotFound) {
            // object may be the Function itself, given its name again: the same
            // name is taken, another is refused with what object already is.
            auto described = ue::Describe(client, object);
            if (described && std::string_view(described->className).ends_with("Function") && described->outer.address) {
                const std::string_view given(request.text);
                auto own = ue::Function(client, described->outer, described->name);
                const std::string full = own ? own->owner + ":" + own->name : described->name;
                if (given != described->name && given != full)
                    return JsonError(session, jsonCommand, ErrorKind::Validation, "function: object is already the "
                        "Function " + full + ": drop function");
                if (!own) return CommandError(session, jsonCommand, own.error());
                function = std::move(own);
                ue::SetRef(command.request, described->outer);
                CopyText(command.request.text, described->name);
            }
        }
        if (!function) return CommandError(session, jsonCommand, function.error());
        if (command.decode) return RunScriptDecode(session, command, jsonCommand, *function);
        if (!client.Supports(ue::op::Script)) return JsonError(session, jsonCommand, ErrorKind::Unsupported, "agent does not support script");
        auto script = ue::Script(client, Ref{static_cast<uintptr_t>(function->address), -1, 0});
        if (!script) return CommandError(session, jsonCommand, script.error());
        return Emit(session, jsonCommand, MakeScript(ue::GetRef(command.request), function->owner + ":" + function->name, *script));
    }
    if (command.kind == Kind::NameRefs) {
        auto index = static_cast<int32_t>(request.args[0]);
        std::string name = request.text;
        if (!index) { // Exact text: walk the name pool's matches until it appears.
            for (uint32_t from = 0;;) {
                auto found = ue::NameFind(client, name, from);
                if (!found) return CommandError(session, jsonCommand, found.error());
                for (const auto& match : found->names) if (match.name == name) { index = match.index; break; }
                if (index || !found->next) break;
                from = found->next;
            }
            if (!index) return JsonError(session, jsonCommand, ErrorKind::Validation, "no pooled name is exactly '" + name + "'");
        } else if (auto names = ue::Names(client, std::span(&index, 1)); names && !names->front().empty()) {
            name = names->front();
        }
        auto found = ue::Refs(client, index);
        if (!found) return CommandError(session, jsonCommand, found.error());
        return Emit(session, jsonCommand, MakeRefs(index, name, *found));
    }
    // release without a block: zeroed inputs of the function's size. A call
    // without one: the same, when the call fills every parameter.
    std::optional<ue::Signature> callSignature;
    if (request.op == ue::op::Call && command.kind != Kind::TypedCall && !request.length) {
        auto function = ue::Function(client, ue::GetRef(request), request.text);
        if (!function) return CommandError(session, jsonCommand, function.error());
        auto signature = ue::Parameters(client, function->address);
        if (!signature) return CommandError(session, jsonCommand, signature.error());
        if (signature->size > ue::MaxParams) return JsonError(session, jsonCommand, ErrorKind::Validation, "parameter block too large");
        if (command.kind != Kind::Release)
            if (const auto refusal = calls::ZeroBlockRefusal(*signature); !refusal.empty())
                return JsonError(session, jsonCommand, ErrorKind::Validation, refusal);
        command.request.length = signature->size;
        memset(command.request.data, 0, signature->size);
        callSignature = std::move(*signature);
    }
    if (command.kind == Kind::TypedCall) {
        const auto object = ue::GetRef(request);
        auto function = ue::Function(client, object, request.text);
        if (!function) return CommandError(session, jsonCommand, function.error());
        auto signature = ue::Parameters(client, function->address);
        if (!signature) return CommandError(session, jsonCommand, signature.error());
        return RunTypedCall(session, command, *function, *signature, structs, jsonCommand);
    }
    if (request.op == ue::op::Call && command.dry) {
        auto function = ue::Function(client, ue::GetRef(command.request), request.text);
        if (!function) return CommandError(session, jsonCommand, function.error());
        if (!callSignature) {
            auto signature = ue::Parameters(client, function->address);
            if (!signature) return CommandError(session, jsonCommand, signature.error());
            callSignature = std::move(*signature);
        }
        if (callSignature->size != command.request.length)
            return JsonError(session, jsonCommand, ErrorKind::Validation, "the block is " + std::to_string(command.request.length) +
                " bytes; the function's is " + std::to_string(callSignature->size));
        return Emit(session, jsonCommand, MakeDryCall(command.request,
            std::span(command.request.data, command.request.length), *function));
    }
    switch (request.op) {
    case ue::op::Find: {
        const std::string_view className = request.length ? reinterpret_cast<const char*>(request.data) : "";
        const auto from = static_cast<uint32_t>(request.args[1]);
        const ue::FindOptions options{className, from, false, (request.flags & ue::FindLive) != 0,
            {command.outerText, command.outerRef}};
        auto found = ue::Find(client, request.text, options);
        if (!found) return CommandError(session, jsonCommand, found.error());
        // Only a scan from slot 0 counts every match. A later page reuses the
        // total the first page of the same filter left; without one it counts
        // the table again, its rows dropped.
        const auto& outer = options.outer;
        const std::string key = std::format("{}\x1f{}\x1f{}\x1f{}\x1f{:x}#{}:{}", request.text, className, options.live,
            outer.text, outer.object.address, outer.object.index, outer.object.serial);
        auto& totals = Ue(session).findTotals;
        if (!from) {
            if (totals.size() >= 256) totals.clear();
            totals[key] = found->total;
        } else if (const auto memo = totals.find(key); memo != totals.end()) {
            found->total = memo->second;
        } else {
            auto counted = ue::Find(client, request.text, {className, 0, false, options.live, options.outer});
            if (!counted) return CommandError(session, jsonCommand, counted.error());
            found->total = totals[key] = counted->total;
        }
        // Nothing under a class filter: a name that is no class is refused, not read as no objects.
        if (found->objects.empty() && !from && !className.empty())
            if (auto type = ResolveClassAnyCase(session, std::string(className)); !type)
                return CommandError(session, jsonCommand, type.error());
        // `view.limit`: more pages until N rows or the end, then cut at N; the
        // cursor goes on from the slot after the last row kept.
        if (command.limit) {
            for (auto more = options; found->objects.size() < command.limit && found->next;) {
                more.from = found->next;
                auto page = ue::Find(client, request.text, more);
                if (!page) return CommandError(session, jsonCommand, page.error());
                found->objects.insert(found->objects.end(), page->objects.begin(), page->objects.end());
                found->next = page->next;
            }
            if (found->objects.size() > command.limit) {
                found->objects.resize(command.limit);
                found->next = static_cast<uint32_t>(found->objects.back().ref.index + 1);
            }
        }
        refs.clear();
        for (const auto& object : found->objects) refs.push_back(object.ref);
        // Each row's owner, named in one describe-many of the distinct outers.
        std::vector<Ref> outers;
        for (const auto& object : found->objects)
            if (object.outer.address && std::ranges::find(outers, object.outer) == outers.end()) outers.push_back(object.outer);
        std::vector<std::optional<ue::Object>> described;
        if (!outers.empty() && client.Supports(ue::op::DescribeMany))
            if (auto named = ue::DescribeMany(client, outers); named) described = std::move(*named);
        return Emit(session, jsonCommand, MakeFind(*found, from, described));
    }
    case ue::op::Call: {
        bool kept = false;
        auto block = ue::Call(client, ue::GetRef(request), request.text, std::span(request.data, request.length), 0,
            request.flags, &kept);
        if (!block) return CommandError(session, jsonCommand, block.error());
        auto record = MakeCall(request, *block, command.kind == Kind::Release ? "release" : "raw", kept);
        if (kept) {
            // Named from the parameters: fetched now unless the zeroed block already did.
            if (!callSignature)
                if (auto function = ue::Function(client, ue::GetRef(request), request.text); function)
                    if (auto signature = ue::Parameters(client, function->address); signature) callSignature = std::move(*signature);
            if (callSignature) record.keptReason = calls::KeptReason(*callSignature, structs);
        }
        return Emit(session, jsonCommand, std::move(record));
    }
    case ue::op::Fields: return PrintTypes(session, ue::GetRef(request), command, jsonCommand);
    case ue::op::Names: {
        std::vector<int32_t> indices(request.length / 4);
        memcpy(indices.data(), request.data, request.length);
        auto names = ue::Names(client, indices);
        if (!names) return CommandError(session, jsonCommand, names.error());
        return Emit(session, jsonCommand, MakeNames(indices, *names));
    }
    case ue::op::Census: return RunCensus(session, command, refs, jsonCommand);
    case ue::op::NameFind: {
        const auto from = static_cast<uint32_t>(request.args[1]);
        auto found = ue::NameFind(client, request.text, from);
        if (!found) return CommandError(session, jsonCommand, found.error());
        return Emit(session, jsonCommand, MakeNameFind(*found, from));
    }
    case ue::op::Function: return PrintFunction(session, ue::GetRef(request), request.text, jsonCommand);
    case ue::op::Functions: {
        auto functions = ue::Functions(client, ue::GetRef(request), request.text, request.length);
        if (!functions) return CommandError(session, jsonCommand, functions.error());
        return Emit(session, jsonCommand, MakeFuncs(ue::GetRef(request), *functions, request.length));
    }
    case ue::op::Describe: {
        auto object = ue::Describe(client, ue::GetRef(request));
        if (!object) return CommandError(session, jsonCommand, object.error());
        // One object: the rows name it.
        refs.assign(1, object->ref);
        auto row = MakeObject(*object);
        if (object->outer.address)
            if (auto outer = ue::Describe(client, object->outer); outer)
                row.outer = shape::ObjectOuter{outer->name, FullRef(outer->ref)};
        return Emit(session, jsonCommand, std::move(row));
    }
    }
    JsonError(session, jsonCommand, ErrorKind::Unsupported, "unsupported operation");
}
}

std::shared_ptr<PackState> MakePackState(Session& session) { return std::make_shared<PackState>(session); }

std::string LoadPackFiles(Session& session) {
    // Native members: <console>.native.txt beside the console, hand-owned (the
    // names file may be regenerated; nothing writes into it).
    PackState& state = Ue(session);
    {
        wchar_t module[MAX_PATH];
        const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
        if (length && length < MAX_PATH) state.nativeFile = std::filesystem::path(module).replace_extension(L".native.txt");
        if (std::ifstream file(state.nativeFile); !state.nativeFile.empty() && file) {
            std::string why;
            if (const size_t bad = state.native.Load(file, &why))
                return "malformed native file at line " + std::to_string(bad) + ": " + why;
        }
    }
    return {};
}

void LoadPackCache(Session& session, std::span<const CachePack> packs) {
    const auto pack = std::ranges::find(packs, UnrealCachePack, &CachePack::name);
    if (pack == packs.end()) return;
    auto data = LoadUnrealCache(pack->lines);
    if (!data) return; // Malformed: the pack starts fresh.
    PackState& state = Ue(session);
    for (auto& [name, ref] : data->classes) state.classRefs.push_back({std::move(name), ref, false});
    for (auto& [type, fields] : data->tables) state.tables[type] = std::make_shared<const std::vector<ue::Field>>(std::move(fields));
    state.census = std::move(data->census);
    state.censusBefore = std::move(data->censusBefore);
    state.baseline = std::move(data->snap);
}

std::vector<CachePack> SavePackCache(const Session& session) {
    const PackState& state = Ue(session);
    UnrealCache data;
    for (const auto& entry : state.classRefs)
        if (data.classes.size() < MaxCachedClasses) data.classes.emplace_back(entry.name, entry.ref);
    for (const auto& [type, fields] : state.tables)
        if (data.tables.size() < MaxCachedTables) data.tables.emplace_back(type, *fields);
    data.census = state.census;
    data.censusBefore = state.censusBefore;
    data.snap = state.baseline;
    return {{std::string(UnrealCachePack), SaveUnrealCache(data)}};
}

std::span<const Verb> PackVerbs() { return UnrealVerbs(); }
std::span<const Origin> PackOrigins() {
    static constexpr Origin origins[] = {UnrealOrigin};
    return origins;
}
std::span<const std::string> PackNotes() { return UnrealNotes(); }

std::vector<std::string> OfferedPacks(Session& session) {
    return HasUnreal(session.client) ? std::vector<std::string>{std::string(UnrealCachePack)} : std::vector<std::string>{};
}

std::optional<Json> PackGameThread(Session& session) {
    if (!HasUnreal(session.client) || !session.client.Supports(ue::op::GameThread)) return std::nullopt;
    Request request{};
    request.op = ue::op::GameThread;
    request.flags = static_cast<uint32_t>(ue::GameThreadAction::Status);
    auto state = SendGameThread(session, request);
    return state ? ToJson(*state) : Json{{"error", state.error().message}};
}

bool PackCommand(const Command& command) {
    switch (command.kind) {
    case Kind::TypedCall: case Kind::Release: case Kind::Dump: case Kind::Snap: case Kind::Each:
    case Kind::Get: case Kind::Script: case Kind::NameRefs: case Kind::Entries: case Kind::LabelsExport: case Kind::MemberName:
    case Kind::Layout: case Kind::LineTrace: case Kind::Calls: case Kind::Rows: case Kind::Enum: case Kind::GameThread:
    case Kind::AssetsExport: case Kind::Owner: case Kind::Route:
        return true;
    case Kind::Remote:
        return !command.className.empty() ||
            (command.request.op >= reagentlive::op::PackFirst && command.request.op < reagentlive::op::GameFirst);
    default: return false;
    }
}

// Commands that make several round trips run held (see Held in console.cpp).
bool PackHeld(const Command& command) {
    switch (command.kind) {
    case Kind::Dump: case Kind::Snap: case Kind::Get: case Kind::TypedCall: case Kind::Release:
    case Kind::Script: case Kind::Entries: case Kind::LabelsExport: case Kind::MemberName: case Kind::Layout:
    case Kind::LineTrace: case Kind::Calls: case Kind::Rows: case Kind::Enum: case Kind::Owner: case Kind::Route: return true;
    case Kind::Each: return !command.seconds;
    case Kind::Remote: break;
    default: return false;
    }
    if (!command.className.empty()) return true;
    const uint32_t op = command.request.op;
    if (op == ue::op::Call && !command.request.length) return true; // Looks up the function's size first.
    return op == ue::op::Fields || op == ue::op::Names || op == ue::op::Census ||
        op == ue::op::Functions || op == ue::op::Function;
}

void RunPackCommand(Session& session, const Command& given, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!HasUnreal(client)) return JsonError(session, jsonCommand, ErrorKind::Unsupported, NoPack);
    // get, rows and entries take a default object by name (Default__Class), as
    // call does: found here, before they run, and passed on as a ref.
    Command command = given;
    if ((command.kind == Kind::Get || command.kind == Kind::Snap || command.kind == Kind::Rows ||
         command.kind == Kind::Entries) &&
        command.className.starts_with("Default__")) {
        auto object = ResolveObjectOrClass(session, command.className);
        if (!object) return CommandError(session, jsonCommand, object.error());
        ue::SetRef(command.request, *object);
        command.className.clear();
    }
    if (command.kind == Kind::MemberName) {
        RunLabel(session, command, jsonCommand);
        return;
    }
    if (command.kind == Kind::LabelsExport) {
        RunExportLabels(session, command, jsonCommand);
        return;
    }
    if (command.kind == Kind::AssetsExport) {
        RunExportAssets(session, command, jsonCommand);
        return;
    }
    if (command.kind == Kind::Layout) {
        RunLayout(session, command, jsonCommand);
        return;
    }
    if (command.kind == Kind::Calls) {
        RunCalls(session, command, jsonCommand);
        return;
    }
    if (command.kind == Kind::LineTrace) {
        RunTrace(session, command, jsonCommand);
        return;
    }
    if (command.kind == Kind::Entries) {
        RunEntries(session, command, jsonCommand);
        return;
    }
    if (command.kind == Kind::Enum) {
        RunEnum(session, command, jsonCommand);
        return;
    }
    if (command.kind == Kind::Owner) {
        RunOwner(session, command, jsonCommand);
        return;
    }
    if (command.kind == Kind::Route) {
        RunRoute(session, command, jsonCommand);
        return;
    }
    if (command.kind == Kind::GameThread) {
        RunGameThread(session, command, jsonCommand);
        return;
    }
    if (command.kind == Kind::Rows) {
        RunRows(session, command, Ue(session).structs, jsonCommand);
        return;
    }
    if (command.kind == Kind::Get) {
        if (!client.Supports(ue::op::Fields)) JsonError(session, jsonCommand, ErrorKind::Unsupported, "get needs an agent with fields");
        else {
            std::vector<Ref> named; // The objects the rows name.
            RunGet(session, command, Ue(session).structs, named, jsonCommand);
        }
        return;
    }
    if (command.kind == Kind::Snap) {
        if (!client.Supports(ue::op::ReadObject) || !client.Supports(ue::op::Fields)) {
            JsonError(session, jsonCommand, ErrorKind::Unsupported, "get with snap needs an agent with fields and object reads");
            return;
        }
        Snap(session, ue::GetRef(command.request), command.file, Ue(session).baseline, jsonCommand);
        return;
    }
    std::vector<Ref> named; // The objects the rows name (each names its rows from them).
    RunUnreal(session, command, named, Ue(session).structs, Ue(session).baseline, jsonCommand);
}

// Guards an address given relative to an object on that object's identity,
// read now, so a pin or watch stops when the object is freed or replaced.
Result<Guard> ObjectGuard(Session& session, uint64_t object) {
    Client& client = session.client;
    Guard guard{};
    if (!object) return guard;
    auto bytes = client.Read(object + ue::layout::StockUE5.objectIndex, ue::ObjectIdentityLength);
    if (!bytes) return std::unexpected(bytes.error());
    guard.address = object + ue::layout::StockUE5.objectIndex;
    guard.length = ue::ObjectIdentityLength;
    memcpy(guard.bytes, bytes->data(), bytes->size());
    return guard;
}

// Trials need a live actor or component as owner: not a class default object,
// archetype or asset. Returns the owner's size, or an error text: its class
// size from the agent, which covers native members past the last reflected
// field, else the end of its reflected fields.
std::expected<uint32_t, std::string> TrialOwnerSize(Session& session, const Ref& owner) {
    Client& client = session.client;
    if (!HasUnreal(client)) return std::unexpected(NoPack);
    auto object = ue::Describe(client, owner);
    if (!object) return std::unexpected(object.error().message);
    if (object->name.starts_with("Default__") || object->name.ends_with("_GEN_VARIABLE"))
        return std::unexpected("owner is a class default object or an archetype");
    auto fields = ObjectFields(session, owner);
    if (!fields) return std::unexpected(fields.error().message);
    const bool live = std::ranges::any_of(*fields, [](const ue::Field& field) {
        return field.owner == "Actor" || field.owner == "ActorComponent";
    });
    if (!live) return std::unexpected("owner is not an actor or a component (" + object->className + ")");
    const size_t reflected = DumpEnd(*fields);
    if (!reflected) return std::unexpected("owner has no reflected fields");
    // A class size smaller than its own reflected fields is not believed.
    return object->size >= reflected ? object->size : static_cast<uint32_t>(reflected);
}

void DescribePeek(Session& session, std::span<PeekValue> values) {
    Client& client = session.client;
    if (client.Supports(ue::op::Describe)) {
        for (auto& value : values) {
            if (!value.read || !PointerShaped(value.value) || value.value & 7) continue;
            auto object = ue::Describe(client, ue::AddressRef(value.value));
            if (object) value.object = MakeObject(*object);
            else if (object.error().kind == Error::Kind::Timeout) break;
        }
    }
}

void NamePeek(Session& session, std::span<PeekValue> values, std::span<const Module> modules) {
    Client& client = session.client;
    if (client.Supports(ue::op::Names)) {
        std::vector<int32_t> indices;
        std::vector<size_t> named;
        for (size_t i = 0; i < values.size(); ++i) {
            const auto& value = values[i];
            const auto index = static_cast<int32_t>(value.value & 0xffffffff);
            if (!value.read || value.object || ModuleAt(value.value, modules) ||
                (value.readable && PointerShaped(value.value)) || index <= 0) continue;
            indices.push_back(index);
            named.push_back(i);
        }
        if (!indices.empty())
            if (auto names = ue::Names(client, indices); names)
                for (size_t i = 0; i < named.size() && i < names->size(); ++i) values[named[i]].name = (*names)[i];
    }
}
}
