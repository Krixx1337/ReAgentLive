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
#include <fstream>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/results/ue/call_text.h"
#include "reagentlive/frontends/cli/state/ue/cache_ue.h"
#include "reagentlive/frontends/cli/values/ue/format_ue.h"
#include "reagentlive/core/client.h"
#include "reagentlive/ue/client_ue.h"

#include "reagentlive/frontends/cli/console/ue/internal_ue.h"

namespace reagentlive::cli::detail {
namespace {
constexpr auto NoPack = "needs the Unreal pack: this host does not offer it (status lists the host's packs)";

// The host offers the pack when it answers any of the pack's ops.
bool HasUnreal(const Client& client) {
    for (uint32_t op = ue::op::Find; op <= ue::op::Entries; ++op)
        if (client.Supports(op)) return true;
    return false;
}

// census, or with --changed only the classes whose count differs from the
// last census under the same --class. Either way the new census becomes the
// baseline; --match and --limit only shape what is printed.
void RunCensus(Session& session, const Command& command, std::vector<Ref>& refs, std::string_view jsonCommand) {
    const Request& request = command.request;
    const std::string filter(request.length ? reinterpret_cast<const char*>(request.data) : "");
    const CensusPage page{command.from, command.limit};
    ue::CensusResult census;
    int64_t ms = 0;
    if (page.from) {
        // A later page: the stored census, sliced; no scan, and the baselines stay.
        if (const auto problem = CensusPageProblem(Ue(session).census, Ue(session).censusBefore, filter, command.changed);
            !problem.empty()) return JsonError(session, jsonCommand, "validation", problem);
        census = StoredCensus(*Ue(session).census);
    } else {
        if (command.changed)
            if (const auto problem = CensusBaselineProblem(Ue(session).census, filter); !problem.empty())
                return JsonError(session, jsonCommand, "validation", problem);
        const auto started = Clock::now();
        auto scanned = ue::Census(session.client, filter);
        if (!scanned) return CommandError(session, jsonCommand, scanned.error());
        ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
        census = std::move(*scanned);
        Ue(session).censusBefore = std::move(Ue(session).census);
        Ue(session).census = MakeCensusBaseline(filter, census);
    }
    refs.clear();
    Json result;
    if (command.changed) {
        auto changes = CensusChanges(*Ue(session).censusBefore, census);
        KeepMatching(changes, command.match, [](const CensusChange& c) -> const std::string& { return c.name; });
        const auto [first, shown] = CensusSlice(changes.size(), page);
        for (const auto& change : std::span(changes).subspan(first, shown)) refs.push_back(change.ref);
        result = CensusChangedJson(*Ue(session).censusBefore, census, changes, page, ms);
    } else {
        auto rows = census.rows;
        KeepMatching(rows, command.match, [](const ue::CensusRow& r) -> const std::string& { return r.type.name; });
        const auto [first, shown] = CensusSlice(rows.size(), page);
        for (const auto& row : std::span(rows).subspan(first, shown)) refs.push_back(row.type.ref);
        result = CensusJson(filter, census, rows, page, ms);
    }
    JsonResult(session, jsonCommand, std::move(result));
}

void RunUnreal(Session& session, const Command& original, std::vector<Ref>& refs, StructCache& structs,
               const std::optional<Snapshot>& baseline, std::string_view jsonCommand) {
    Client& client = session.client;
    Command command = original;
    const Request& request = command.request;
    if (!client.Supports(request.op))
        return JsonError(session, jsonCommand, "unsupported", "agent does not support this operation");
    if (command.kind == Kind::Each) return RunEach(session, command, refs, structs, jsonCommand);
    if (!command.className.empty()) {
        auto type = ResolveClass(session, command.className);
        if (!type) return CommandError(session, jsonCommand, type.error());
        ue::SetRef(command.request, *type);
    }
    if (command.kind == Kind::Dump) return PrintDump(session, ue::GetRef(request), refs, structs, command, baseline);
    if (command.kind == Kind::Script) {
        if (!client.Supports(ue::op::Script)) return JsonError(session, jsonCommand, "unsupported", "agent does not support --script");
        const auto object = ue::GetRef(request);
        auto function = ue::Function(client, object, request.text);
        if (!function) return CommandError(session, jsonCommand, function.error());
        auto script = ue::Script(client, Ref{static_cast<uintptr_t>(function->address), -1, 0});
        if (!script) return CommandError(session, jsonCommand, script.error());
        return JsonResult(session, jsonCommand, ScriptJson(object, function->owner + ":" + function->name, *script));
    }
    if (command.kind == Kind::Refs) {
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
            if (!index) return JsonError(session, jsonCommand, "validation", "no pooled name is exactly '" + name + "'");
        } else if (auto names = ue::Names(client, std::span(&index, 1)); names && !names->front().empty()) {
            name = names->front();
        }
        auto found = ue::Refs(client, index);
        if (!found) return CommandError(session, jsonCommand, found.error());
        return JsonResult(session, jsonCommand, RefsJson(index, name, *found));
    }
    // release without a block: zeroed inputs of the function's size. A call
    // without one: the same, when the call fills every parameter.
    std::optional<ue::Signature> callSignature;
    if (request.op == ue::op::Call && command.kind != Kind::TypedCall && !request.length) {
        auto function = ue::Function(client, ue::GetRef(request), request.text);
        if (!function) return CommandError(session, jsonCommand, function.error());
        auto signature = ue::Parameters(client, function->address);
        if (!signature) return CommandError(session, jsonCommand, signature.error());
        if (signature->size > ue::MaxParams) return JsonError(session, jsonCommand, "validation", "parameter block too large");
        if (command.kind != Kind::Release)
            if (const auto refusal = calls::ZeroBlockRefusal(*signature); !refusal.empty())
                return JsonError(session, jsonCommand, "validation", refusal);
        command.request.length = signature->size;
        memset(command.request.data, 0, signature->size);
        callSignature = std::move(*signature);
    }
    if (request.op == ue::op::Parameters || command.kind == Kind::TypedCall) {
        const auto object = ue::GetRef(request);
        auto function = ue::Function(client, object, request.text);
        if (!function) return CommandError(session, jsonCommand, function.error());
        auto signature = ue::Parameters(client, function->address);
        if (!signature) return CommandError(session, jsonCommand, signature.error());
        if (command.kind == Kind::TypedCall)
            return RunTypedCall(session, command, function->address, *signature, structs, jsonCommand);
        return PrintParameters(session, *function, *signature, object);
    }
    switch (request.op) {
    case ue::op::Find: {
        const std::string_view className = request.length ? reinterpret_cast<const char*>(request.data) : "";
        const auto from = static_cast<uint32_t>(request.args[1]);
        auto found = ue::Find(client, request.text, {className, from});
        if (!found) return CommandError(session, jsonCommand, found.error());
        refs.clear();
        for (const auto& object : found->objects) refs.push_back(object.ref);
        return JsonResult(session, "find", FindJson(*found, from));
    }
    case ue::op::Call: {
        bool kept = false;
        auto block = ue::Call(client, ue::GetRef(request), request.text, std::span(request.data, request.length), 0,
            request.flags, &kept);
        if (!block) return CommandError(session, jsonCommand, block.error());
        auto record = CallJson(request, *block, command.kind == Kind::Release ? "release" : "raw", kept);
        if (kept) {
            // Named from the parameters: fetched now unless the zeroed block already did.
            if (!callSignature)
                if (auto function = ue::Function(client, ue::GetRef(request), request.text); function)
                    if (auto signature = ue::Parameters(client, function->address); signature) callSignature = std::move(*signature);
            if (callSignature) record["kept_reason"] = calls::KeptReason(*callSignature, structs);
        }
        return JsonResult(session, jsonCommand, std::move(record));
    }
    case ue::op::Fields: return PrintFields(session, ue::GetRef(request), command.className.empty(), jsonCommand);
    case ue::op::Names: {
        std::vector<int32_t> indices(request.length / 4);
        memcpy(indices.data(), request.data, request.length);
        auto names = ue::Names(client, indices);
        if (!names) return CommandError(session, jsonCommand, names.error());
        return JsonResult(session, jsonCommand, NamesJson(indices, *names));
    }
    case ue::op::Census: return RunCensus(session, command, refs, jsonCommand);
    case ue::op::NameFind: {
        const auto from = static_cast<uint32_t>(request.args[1]);
        auto found = ue::NameFind(client, request.text, from);
        if (!found) return CommandError(session, jsonCommand, found.error());
        return JsonResult(session, jsonCommand, NameFindJson(*found, from));
    }
    case ue::op::Function: return PrintFunction(session, ue::GetRef(request), request.text, jsonCommand);
    case ue::op::Functions: {
        auto functions = ue::Functions(client, ue::GetRef(request), request.text, request.length);
        if (!functions) return CommandError(session, jsonCommand, functions.error());
        return JsonResult(session, jsonCommand, FunctionsJson(ue::GetRef(request), *functions, request.length));
    }
    case ue::op::Describe: {
        auto object = ue::Describe(client, ue::GetRef(request));
        if (!object) return CommandError(session, jsonCommand, object.error());
        RememberRef(refs, object->ref);
        return JsonResult(session, jsonCommand, ObjectJson(*object));
    }
    }
    JsonError(session, jsonCommand, "unsupported", "unsupported operation");
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
    return {{std::string(UnrealCachePack), SaveUnrealCache(data)}};
}

std::string PackHelp() {
    return
        "\n  Unreal pack (objects, fields and calls; refused when the host does not offer it):\n"
        "  find [text] [--class C] [--from N]   names containing text and/or instances of class C or a subclass;\n"
        "                                       24 per page (results become $1..$N), prints the next page's cursor;\n"
        "                                       only the first page counts the total\n"
        "  census [--class C] [--match text] [--changed] [--limit N] [--from N]\n"
        "                                       live instances per class (most first, pages of 100 rows, 0 = all;\n"
        "                                       --from next_cursor pages the stored census without a new scan);\n"
        "                                       --match keeps class names containing text; --changed prints only\n"
        "                                       classes whose count differs from the last census of that --class;\n"
        "                                       class refs become $1..$N\n"
        "  name-find <text> [--from N]          FName indices whose text contains text (gameplay tags, row names)\n"
        "  each <Class> <Field|[Owner:]Function> [--seconds N]\n"
        "                                       one value per live instance, read in one tick; functions must be\n"
        "                                       const or pure with outputs only; --seconds repeats and prints changes\n"
        "  call <object> <[Class:]Function> [hex params]\n"
        "  call <object> <[Class:]Function> Name=value [...] | --typed\n"
        "                                       params must match the function's size; prints the block after the call;\n"
        "                                       kept: true when outputs own memory (handles, strings): the agent\n"
        "                                       reuses the block so the next call releases the old values\n"
        "  release <object> <[Class:]Function> [hex params]\n"
        "                                       one more call (zeroed inputs by default) that drops the kept block\n"
        "  fields <object|Class>                reflected properties by offset; a class or struct lists its own\n"
        "  dump <object|Class> [--match text] [--nonzero] [--changed [file]]\n"
        "                                       fields with their current values (use on instances, incl. Default__\n"
        "                                       objects); filters keep matching names, non-zero values, or fields\n"
        "                                       changed since snap (or a snapshot file)\n"
        "  label <object|Class> <offset> <type>[n] <Name>\n"
        "                                       name an unreflected member in <console>.native.txt; fields, dump\n"
        "                                       and get then show it (native, stale after a patch)\n"
        "  export-labels <file> [--module M] [--kinds exec,vtable,interface,global]\n"
        "                                       names for a static-analysis tool (JSON lines, one build): native\n"
        "                                       exec thunks, vtables, interface implementations, and the globals\n"
        "                                       peek --label put in the file (always kept); import with\n"
        "                                       import_labels.py. After another tool's mappings: interface,global\n"
        "  entries <object> <[Owner:]Field> [--from N]\n"
        "                                       one page of an array, set or map: elements, or key and value;\n"
        "                                       names and object pointers resolved; prints the next page's cursor\n"
        "  get <object> <Field[.Field...]>...   named fields, following object pointers and structs (up to 16 paths)\n"
        "  describe <address|$N>                object name, class and reusable ref\n"
        "  name <index>...                      FName comparison indices as text (FName #N in dumps)\n"
        "  func <object|class> <[Owner:]Function>  reflected metadata and exec entry address\n"
        "  func <object|class> <[Owner:]Function> --script\n"
        "                                       a Blueprint function's bytecode: the names, functions and\n"
        "                                       properties right after their opcodes, each confirmed live\n"
        "  refs <name|#index>                   Blueprint functions whose bytecode names it (EX_NameConst)\n"
        "  funcs <object|class> [text] [--from N]   list functions on the class chain\n"
        "  params <object|class> <[Owner:]Function>  reflected parameter signature\n"
        "  snap <ref> [file]                    save a baseline for dump --changed (file: across runs)\n"
        "  record <ref> <seconds> <file> <field>...  sample 1..8 named fields for 1..10 seconds after countdown\n"
        "  Refs: $N from the last find, or the printed literal 0x<address>#<index>:<serial> (works across runs).\n"
        "  A bare class or struct name works where <object|Class> is shown; the first lookup scans the table.\n"
        "  Struct values print as {Name: value}; --meta at the end of a line adds their member layout.\n";
}

void AddPackStatus(Session& session, Json& status) {
    status["packs"] = HasUnreal(session.client) ? Json::array({UnrealCachePack}) : Json::array();
}

bool PackCommand(const Command& command) {
    switch (command.kind) {
    case Kind::TypedCall: case Kind::Release: case Kind::Dump: case Kind::Snap: case Kind::Record: case Kind::Each:
    case Kind::Get: case Kind::Script: case Kind::Refs: case Kind::Entries: case Kind::ExportLabels: case Kind::Label:
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
    case Kind::Script: case Kind::Entries: case Kind::ExportLabels: case Kind::Label: return true;
    case Kind::Each: return !command.seconds;
    case Kind::Remote: break;
    default: return false;
    }
    if (!command.className.empty()) return true;
    const uint32_t op = command.request.op;
    if (op == ue::op::Call && !command.request.length) return true; // Looks up the function's size first.
    return op == ue::op::Fields || op == ue::op::Names || op == ue::op::Census ||
        op == ue::op::Functions || op == ue::op::Parameters || op == ue::op::Function;
}

void RunPackCommand(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!HasUnreal(client)) return JsonError(session, jsonCommand, "unsupported", NoPack);
    if (command.kind == Kind::Label) {
        RunLabel(session, command, jsonCommand);
        return;
    }
    if (command.kind == Kind::ExportLabels) {
        RunExportLabels(session, command, jsonCommand);
        return;
    }
    if (command.kind == Kind::Entries) {
        RunEntries(session, command, jsonCommand);
        return;
    }
    if (command.kind == Kind::Get) {
        if (!client.Supports(ue::op::Fields)) JsonError(session, jsonCommand, "unsupported", "get needs an agent with fields");
        else RunGet(session, command, Ue(session).structs, session.refs, jsonCommand);
        return;
    }
    if (command.kind == Kind::Snap || command.kind == Kind::Record) {
        if (!client.Supports(ue::op::ReadObject) || !client.Supports(ue::op::Fields)) {
            JsonError(session, jsonCommand, "unsupported", "this command needs an agent with fields and object reads");
            return;
        }
        const auto object = ue::GetRef(command.request);
        if (command.kind == Kind::Record) Record(session, object, command, Ue(session).structs, jsonCommand);
        else Snap(session, object, command.file, Ue(session).baseline, jsonCommand);
        return;
    }
    RunUnreal(session, command, session.refs, Ue(session).structs, Ue(session).baseline, jsonCommand);
}

// Guards an address given relative to an object on that object's identity,
// read now, so a pin or watch stops when the object is freed or replaced.
Result<Guard> ObjectGuard(Session& session, uint64_t object) {
    Client& client = session.client;
    Guard guard{};
    if (!object) return guard;
    auto bytes = client.Read(object + ue::ObjectIdentityOffset, ue::ObjectIdentityLength);
    if (!bytes) return std::unexpected(bytes.error());
    guard.address = object + ue::ObjectIdentityOffset;
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

bool PackCall(const Command& command) { return command.kind == Kind::Remote && command.request.op == ue::op::Call; }

Json RunPackCall(Session& session, const Command& call) {
    const Request& request = call.request;
    auto block = ue::Call(session.client, ue::GetRef(request), request.text, std::span(request.data, request.length));
    return block ? Json{{"ok", true}, {"hex", format_detail::Hex(*block, block->size())}} :
        Json{{"ok", false}, {"message", block.error().message}};
}

void DescribePeek(Session& session, std::span<PeekValue> values) {
    Client& client = session.client;
    if (client.Supports(ue::op::Describe)) {
        for (auto& value : values) {
            if (!value.read || !PointerShaped(value.value) || value.value & 7) continue;
            auto object = ue::Describe(client, ue::AddressRef(value.value));
            if (object) value.object = ObjectJson(*object);
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
