// reagentlive CLI console: Class lookup, each and get.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include "reagentlive/frontends/cli/results/ue/call_text.h"
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/state/ue/cache_ue.h"
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/frontends/cli/values/ue/format_ue.h"
#include "reagentlive/frontends/cli/values/ue/json_output.h"
#include "reagentlive/frontends/cli/values/layout_json.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/core/client.h"
#include "reagentlive/ue/client_ue.h"

#include "reagentlive/frontends/cli/console/ue/internal_ue.h"

namespace reagentlive::cli::detail {

Result<Ref> ResolveClass(Session& session, const std::string& name) {
    Client& client = session.client;
    for (auto entry = Ue(session).classRefs.begin(); entry != Ue(session).classRefs.end(); ++entry) {
        if (entry->name != name) continue;
        if (entry->verified) return entry->ref;
        auto object = ue::Describe(client, entry->ref);
        if (object && object->ref == entry->ref && object->name == name) {
            entry->verified = true;
            return entry->ref;
        }
        if (!object && object.error().kind != Error::Kind::Agent) return std::unexpected(object.error());
        Ue(session).classRefs.erase(entry);
        break;
    }
    uint32_t from = 0;
    for (int page = 0; page < 64; ++page) {
        auto found = ue::Find(client, name, {"Struct", from});
        if (!found) return std::unexpected(found.error());
        for (const auto& object : found->objects) {
            if (object.name != name) continue;
            Ue(session).classRefs.push_back({name, object.ref, true});
            return object.ref;
        }
        if (!found->next) break;
        from = found->next;
    }
    return std::unexpected(Error{Error::Kind::Agent, Status::NotFound, 0, {}, "no class or struct named " + name});
}

namespace {
// The value of one each row: the field's, or the call's return value plus out parameters.
Json EachValue(const std::optional<ue::Field>& field, const ue::Signature& signature, std::span<const uint8_t> bytes,
               StructCache& structs, const ValueStyle& style, bool& truncated) {
    const format_detail::UnresolvedName names;
    if (field) return JsonFieldValue(*field, bytes, structs, names, truncated, style);
    Json value = nullptr, outputs = Json::object();
    for (const auto& parameter : signature.parameters) {
        if (!(parameter.parameterFlags & (ue::Output | ue::Return))) continue;
        auto json = JsonFieldValue(parameter, FieldBytes(bytes, parameter), structs, names, truncated, style);
        if (parameter.parameterFlags & ue::Return) value = std::move(json);
        else outputs[parameter.name] = std::move(json);
    }
    if (outputs.empty()) return value;
    return {{"return", std::move(value)}, {"outputs", std::move(outputs)}};
}
}

namespace {
// A field by [Owner:]Name, readable or reflected; nullptr when missing or ambiguous (why says which).
const ue::Field* FindField(const Names& names, std::span<const ue::Field> fields, std::string_view wanted, std::string& why) {
    const auto colon = wanted.find(':');
    const auto matches = [&](const ue::Field& field) {
        if (colon == std::string_view::npos) return names.Matches(field, wanted);
        return wanted.substr(0, colon) == field.owner && names.Matches(field, wanted.substr(colon + 1));
    };
    const auto found = std::ranges::find_if(fields, matches);
    if (found == fields.end()) {
        why = "no field " + std::string(wanted);
        return nullptr;
    }
    if (std::ranges::find_if(std::next(found), fields.end(), matches) != fields.end()) {
        why = "ambiguous field " + std::string(wanted) + " (use Owner:Field)";
        return nullptr;
    }
    return &*found;
}

bool IsPointer(const ue::Field& field) {
    return (field.type == "ObjectProperty" || field.type == "ClassProperty" || field.type == "WeakObjectProperty") &&
        field.size == 8;
}

// Where a field path leads: the object holding its last field and what to
// read there, or why it leads nowhere. `stopAtPointer` (each on a class)
// stops at the first object pointer with the path past it in `rest`, since
// there is no instance yet to read the pointer from.
struct PathTarget {
    std::string error;
    ue::Field field;
    Ref object{};
    uint32_t offset = 0, size = 0;        // What to read in `object`.
    uint64_t address = 0;
    std::optional<NativeMember> native;   // A member from the native file, decoded by its type.
    bool stale = false;
    bool pointer = false;                 // stopAtPointer: `field` is the pointer, `rest` the path past it.
    std::string rest;
};

PathTarget ResolvePath(Session& session, StructCache& structs, Ref object, std::string_view path, bool stopAtPointer) {
    Client& client = session.client;
    PathTarget out;
    uint32_t base = 0; // Offset of the current struct inside the object.
    // Shared with the session's cache, not copied.
    FieldList objectFields;
    const std::vector<ue::Field>* table = nullptr;
    std::string_view rest = path;
    while (out.error.empty()) {
        if (!table) {
            auto fields = ObjectTable(session, object);
            if (!fields) { out.error = fields.error().message; break; }
            objectFields = std::move(*fields);
            table = objectFields.get();
        }
        const size_t dot = rest.find('.');
        const auto name = rest.substr(0, dot);
        rest = dot == std::string_view::npos ? std::string_view{} : rest.substr(dot + 1);
        const auto* field = FindField(session.names, *table, name, out.error);
        if (!field) {
            // A native member, as the last step on the object itself.
            if (!stopAtPointer && rest.empty() && table == objectFields.get() && !Ue(session).native.Empty()) {
                const auto native = NativeMembers(session, object, *objectFields, object.index >= 0);
                const auto named = [&](const NativeCheck& c) { return NativeMatches(*c.member, name); };
                if (const auto refused = std::ranges::find_if(native.refused, named); refused != native.refused.end()) {
                    out.error = std::string(name) + " (native file line " + std::to_string(refused->member->line) +
                        ") " + refused->problem;
                } else if (const auto shown = std::ranges::find_if(native.shown, named); shown != native.shown.end()) {
                    const auto& member = *shown->member;
                    out.error.clear();
                    out.native = member;
                    out.stale = shown->stale;
                    out.offset = member.field.offset;
                    out.size = member.field.Size();
                    out.address = object.address + member.field.offset;
                    out.object = object;
                }
            }
            break;
        }
        const uint32_t offset = base + field->offset;
        if (rest.empty()) {
            if (!field->size || field->size > MaxRead) { out.error = "field size outside 1.." + std::to_string(MaxRead); break; }
            out.field = *field;
            out.offset = offset;
            out.size = field->size;
            out.address = object.address + offset;
            out.object = object;
            break;
        }
        if (field->type == "StructProperty" && field->structType.address) {
            table = structs(field->structType);
            if (!table) { out.error = "struct layout unavailable: " + std::string(name); break; }
            base = offset;
            continue;
        }
        if (IsPointer(*field)) {
            if (stopAtPointer) {
                out.pointer = true;
                out.field = *field;
                out.offset = offset;
                out.size = 8;
                out.rest = rest;
                break;
            }
            auto bytes = object.index >= 0 ? ue::ReadObject(client, object, offset, 8) : client.Read(object.address + offset, 8);
            if (!bytes) { out.error = bytes.error().message; break; }
            Ref next = PointerRef(field->type, *bytes);
            if (!next.address && !next.serial) { out.error = "null pointer at " + std::string(name); break; }
            if (!next.address) {
                auto described = ue::Describe(client, next);
                if (!described) { out.error = described.error().message; break; }
                next = described->ref;
            }
            object = next;
            base = 0;
            table = nullptr;
            continue;
        }
        out.error = "cannot follow " + std::string(name) + " (" + field->type + ")";
    }
    return out;
}

// FNames in a set of decoded values, resolved in one lookup: collect, then name.
struct NameBatch {
    std::vector<int32_t> indices;
    std::vector<std::string> names;
    void Add(const ue::Field& field, std::span<const uint8_t> bytes, StructCache& structs) {
        const auto collect = [&](int32_t index) -> std::string_view {
            if (index >= 0 && std::ranges::find(indices, index) == indices.end()) indices.push_back(index);
            return {};
        };
        FormatField(field, bytes, structs, 0, collect);
    }
    void Resolve(Client& client) {
        if (!indices.empty() && client.Supports(ue::op::Names))
            if (auto result = ue::Names(client, indices); result) names = std::move(*result);
    }
    std::string_view operator()(int32_t index) const {
        const auto at = std::ranges::find(indices, index);
        const auto slot = static_cast<size_t>(at - indices.begin());
        return at == indices.end() || slot >= names.size() ? std::string_view{} : std::string_view(names[slot]);
    }
};
}

// each: one row per live instance, a value per column, the columns in the
// order asked. A function is the only column (one call per instance). Field
// paths are resolved on the class: the spans they need inside the instance
// (a field, or the pointer a path follows) are read by as few agent passes as
// fit MaxEachValue bytes each, the first in one tick and the rest over its
// instance list; a path's part past a pointer is resolved and read per
// instance, as get does. A cell that fails is null, with an `errors` entry.
void RunEach(Session& session, const Command& command, std::vector<Ref>& refs, StructCache& structs,
             std::string_view jsonCommand) {
    Client& client = session.client;
    const auto fail = [&](std::string message) { JsonError(session, jsonCommand, "validation", message); };
    auto type = ResolveClass(session, command.className);
    if (!type) return CommandError(session, jsonCommand, type.error());
    const auto& paths = command.fields;
    shape::Each record;
    record.className = command.className;
    record.classRef = FullRef(*type);
    bool truncated = false;
    refs.clear();

    // --seconds: the one column polled again until the deadline, its changes reported.
    const auto watch = [&](ue::EachQuery query, const std::optional<ue::Field>& field, const ue::Signature& signature,
                           std::vector<std::pair<Ref, std::vector<uint8_t>>> last) {
        std::vector<shape::EachChange> changes;
        std::optional<Error> pollError;
        size_t polls = 0;
        const auto start = Clock::now();
        const auto deadline = start + std::chrono::seconds(command.seconds);
        query.reuse = true;
        std::fprintf(stderr, "  watching %zu instances for %u s\n", last.size(), command.seconds);
        while (Clock::now() < deadline) {
            Sleep(100);
            auto again = ue::Each(client, query);
            if (!again) { pollError = again.error(); break; }
            ++polls;
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
            const auto tick = client.Heartbeat();
            for (const auto& row : again->rows) {
                auto previous = std::ranges::find(last, row.object, &std::pair<Ref, std::vector<uint8_t>>::first);
                if (previous == last.end()) { last.emplace_back(row.object, row.value); continue; }
                if (previous->second == row.value) continue;
                shape::EachChange change{ms, tick, FullRef(row.object),
                    EachValue(field, signature, previous->second, structs, Style(session), truncated),
                    EachValue(field, signature, row.value, structs, Style(session), truncated)};
                std::fprintf(stderr, "  +%lld ms  %s  %s -> %s\n", static_cast<long long>(ms), RefText(row.object).c_str(),
                    change.before.dump().c_str(), change.after.dump().c_str());
                std::fflush(stderr);
                previous->second = row.value;
                changes.push_back(std::move(change));
            }
        }
        record.seconds = command.seconds;
        record.polls = polls;
        record.changes = std::move(changes);
        if (pollError) record.pollError = pollError->message;
    };

    // A function: the only column, called on each instance.
    if (paths.size() == 1) {
        auto function = ue::Function(client, *type, paths[0]);
        if (function) {
            auto parameters = ue::Parameters(client, function->address);
            if (!parameters) return CommandError(session, jsonCommand, parameters.error());
            const ue::Signature signature = std::move(*parameters);
            if (!ue::EachFunctionAllowed(function->flags)) return fail("each calls only const or pure functions");
            if (auto refusal = calls::EachRefusal(signature, structs); !refusal.empty()) return fail(std::move(refusal));
            if (!signature.size || signature.size > ue::MaxEachValue)
                return fail(function->owner + ":" + function->name + "'s parameter block is " +
                    std::to_string(signature.size) + " bytes; each takes 1.." + std::to_string(ue::MaxEachValue) +
                    " per instance (call it on one object instead)");
            const std::string target = function->owner + ":" + function->name;
            ue::EachQuery query{command.className};
            query.function = target;
            query.size = signature.size;
            auto result = ue::Each(client, query);
            if (!result) return CommandError(session, jsonCommand, result.error());
            std::vector<std::pair<Ref, std::vector<uint8_t>>> last;
            for (const auto& row : result->rows) {
                refs.push_back(row.object);
                shape::EachRow item{FullRef(row.object), {EachValue(std::nullopt, signature, row.value, structs,
                    Style(session), truncated)}, {}};
                if (session.meta) item.hex = HexBytes(row.value);
                record.rows.push_back(std::move(item));
                last.emplace_back(row.object, row.value);
            }
            record.columns = {target};
            record.mode = "call";
            record.capped = result->capped;
            record.sameTick = true;
            if (command.seconds) watch(query, std::nullopt, signature, std::move(last));
            record.truncated = truncated;
            return Emit(session, jsonCommand, std::move(record));
        }
        if (function.error().kind != Error::Kind::Agent || function.error().status != Status::NotFound)
            return CommandError(session, jsonCommand, function.error());
    }

    // Field paths, resolved on the class; one past a pointer stops there for now.
    std::vector<PathTarget> columns;
    for (const auto& path : paths) {
        auto column = ResolvePath(session, structs, *type, path, true);
        if (!column.error.empty())
            return fail(paths.size() == 1 ? "neither a function nor a field of " + command.className + ": " + path +
                " (" + column.error + ")" : path + ": " + column.error + " (a function is each's only column)");
        if (column.size > ue::MaxEachValue)
            return fail(path + " is " + std::to_string(column.size) + " bytes; each reads 1.." +
                std::to_string(ue::MaxEachValue) + " per instance (get reads it on one object)");
        columns.push_back(std::move(column));
    }
    if (command.seconds && (columns.size() != 1 || columns.front().pointer))
        return fail("--seconds watches one field or function, not a path through a pointer");

    // The spans inside the instance, in passes of at most MaxEachValue bytes.
    struct Pass {
        uint32_t start = 0, end = 0;
        std::vector<std::pair<Ref, std::vector<uint8_t>>> rows;
    };
    std::vector<std::pair<uint32_t, uint32_t>> spans;
    for (const auto& column : columns) spans.emplace_back(column.offset, column.offset + column.size);
    std::vector<Pass> passes;
    for (const auto& [begin, end] : shape::EachPasses(std::move(spans), ue::MaxEachValue)) passes.push_back({begin, end, {}});
    bool capped = false;
    ue::EachQuery first{command.className};
    for (size_t i = 0; i < passes.size(); ++i) {
        ue::EachQuery query{command.className};
        query.offset = passes[i].start;
        query.size = passes[i].end - passes[i].start;
        query.reuse = i > 0;
        if (!i) first = query;
        auto result = ue::Each(client, query);
        if (!result) return CommandError(session, jsonCommand, result.error());
        capped |= result->capped;
        for (auto& row : result->rows) passes[i].rows.emplace_back(row.object, std::move(row.value));
    }
    // A column's bytes in one instance, from the pass that holds its span; empty when the instance left.
    const auto inObject = [&](const Ref& object, const PathTarget& column) -> std::span<const uint8_t> {
        for (const auto& pass : passes) {
            if (column.offset < pass.start || column.offset + column.size > pass.end) continue;
            const auto found = std::ranges::find(pass.rows, object, &std::pair<Ref, std::vector<uint8_t>>::first);
            const size_t at = column.offset - pass.start;
            if (found == pass.rows.end() || found->second.size() < at + column.size) return {};
            return std::span(found->second).subspan(at, column.size);
        }
        return {};
    };

    // Each cell: the field and bytes to decode, or why there are none.
    struct Cell {
        std::string error;
        ue::Field field;
        std::vector<uint8_t> bytes;
        std::optional<NativeMember> native;
    };
    const bool pointers = std::ranges::any_of(columns, &PathTarget::pointer);
    const Client::Hold hold(client);
    std::vector<std::vector<Cell>> cells;
    for (const auto& entry : passes.front().rows) {
        const Ref& object = entry.first;
        auto& row = cells.emplace_back();
        for (const auto& column : columns) {
            Cell& cell = row.emplace_back();
            const auto data = inObject(object, column);
            if (data.size() != column.size) { cell.error = "the instance was gone by a later pass"; continue; }
            if (!column.pointer) {
                cell.field = column.field;
                cell.bytes.assign(data.begin(), data.end());
                continue;
            }
            Ref next = PointerRef(column.field.type, data);
            if (!next.address && !next.serial) { cell.error = "null pointer at " + column.field.name; continue; }
            if (!next.address) {
                auto described = ue::Describe(client, next);
                if (!described) { cell.error = described.error().message; continue; }
                next = described->ref;
            }
            const auto target = ResolvePath(session, structs, next, column.rest, false);
            if (!target.error.empty()) { cell.error = target.error; continue; }
            auto bytes = target.object.index >= 0 ? ue::ReadObject(client, target.object, target.offset, target.size) :
                client.Read(target.object.address + target.offset, target.size);
            if (!bytes) { cell.error = bytes.error().message; continue; }
            cell.field = target.field;
            cell.native = target.native;
            cell.bytes = std::move(*bytes);
        }
    }
    NameBatch names;
    for (const auto& row : cells)
        for (const auto& cell : row)
            if (cell.error.empty() && !cell.native) names.Add(cell.field, cell.bytes, structs);
    names.Resolve(client);

    std::vector<shape::EachError> errors;
    for (size_t i = 0; i < cells.size(); ++i) {
        const Ref& object = passes.front().rows[i].first;
        refs.push_back(object);
        shape::EachRow item{FullRef(object), {}, {}};
        for (size_t c = 0; c < cells[i].size(); ++c) {
            const Cell& cell = cells[i][c];
            if (!cell.error.empty()) {
                item.values.emplace_back(nullptr);
                errors.push_back({FullRef(object), static_cast<uint32_t>(c), cell.error});
            } else if (cell.native) {
                item.values.push_back(layout::Value(cell.native->field, cell.bytes));
            } else {
                item.values.push_back(JsonFieldValue(cell.field, cell.bytes, structs, names, truncated, Style(session)));
            }
        }
        if (session.meta && columns.size() == 1 && !columns.front().pointer) item.hex = HexBytes(cells[i].front().bytes);
        record.rows.push_back(std::move(item));
    }
    record.columns = paths;
    record.mode = "field";
    record.capped = capped;
    record.sameTick = passes.size() == 1 && !pointers;
    if (!errors.empty()) record.errors = std::move(errors);
    if (command.seconds) {
        std::vector<std::pair<Ref, std::vector<uint8_t>>> last = passes.front().rows;
        watch(first, columns.front().field, {}, std::move(last));
    }
    record.truncated = truncated;
    Emit(session, jsonCommand, std::move(record));
}

// get: each path resolved through the field tables, following object
// pointers and descending into structs; then every value read back to back.
// The lookups may span ticks. The value reads have no console work between
// them, so under the hold they fit the agent's short wait for the next
// request and land in one host tick; same_tick says whether they did.
void RunGet(Session& session, const Command& command, StructCache& structs, std::vector<Ref>& refs,
            std::string_view jsonCommand) {
    Client& client = session.client;
    struct Value : PathTarget {
        std::vector<uint8_t> bytes;
    };
    const Client::Hold hold(client);
    const Ref root = ue::GetRef(command.request);
    std::vector<Value> values;
    for (const auto& path : command.fields) values.push_back({ResolvePath(session, structs, root, path, false), {}});
    // The values, back to back. A replaced object is refused by its read.
    uint64_t firstTick = 0;
    for (auto& value : values) {
        if (!value.error.empty() || !value.size) continue;
        auto bytes = value.object.index >= 0 ? ue::ReadObject(client, value.object, value.offset, value.size) :
            client.Read(value.object.address + value.offset, value.size);
        if (!firstTick) firstTick = hold.LastTick();
        if (!bytes) { value.error = bytes.error().message; continue; }
        value.bytes = std::move(*bytes);
    }
    const bool sameTick = firstTick == hold.LastTick();
    // FNames in the values, resolved in one lookup.
    NameBatch lookupName;
    for (const auto& value : values)
        if (value.error.empty() && !value.native) lookupName.Add(value.field, std::span(value.bytes), structs);
    lookupName.Resolve(client);
    const auto modules = LoadedModules(session);
    shape::Get result;
    bool failed = false;
    for (const auto& value : values) {
        shape::GetRow row;
        if (!value.error.empty()) {
            failed = true;
            row.error = value.error;
            result.rows.push_back(std::move(row));
            continue;
        }
        row.address = Addr(value.address);
        row.where = Locate(value.address, modules);
        // The holder only when a pointer led there; otherwise it is the ref asked about.
        if (!(value.object == root)) row.object = ReusableRef(value.object);
        if (session.meta) row.hex = HexBytes(value.bytes);
        if (value.native) {
            row.value = layout::Value(value.native->field, value.bytes);
            row.native = true;
            if (value.stale) row.stale = true;
            if (session.meta) row.type = NativeType(value.native->field);
            result.rows.push_back(std::move(row));
            continue;
        }
        bool partial = false;
        row.value = JsonFieldValue(value.field, std::span(value.bytes), structs, lookupName, partial, Style(session));
        if (partial) row.truncated = true;
        if (session.meta) row.type = value.field.type;
        const auto target = PointerRef(value.field.type, value.bytes);
        row.valueWhere = Locate(target.address, modules);
        if (target.address) RememberRef(refs, target);
        result.rows.push_back(std::move(row));
    }
    session.failed |= failed;
    result.ref = ReusableRef(root);
    result.sameTick = sameTick;
    if (!TickAccurate(session)) result.tickAccurate = false;
    Emit(session, jsonCommand, std::move(result));
}

void RunEntries(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(ue::op::Entries)) return JsonError(session, jsonCommand, "unsupported", "agent does not support entries");
    const auto object = ue::GetRef(command.request);
    auto fields = ObjectFields(session, object);
    if (!fields) return CommandError(session, jsonCommand, fields.error());
    std::string why;
    const auto* field = FindField(session.names, *fields, command.target, why);
    if (!field) return JsonError(session, jsonCommand, "field", why);
    ue::ContainerKind kind{};
    uint32_t stride = 0;
    if (field->type == "ArrayProperty" && field->elementSize) {
        kind = ue::ContainerKind::Array;
        stride = field->elementSize;
    } else if (field->type == "SetProperty" || field->type == "MapProperty") {
        kind = field->type == "MapProperty" ? ue::ContainerKind::Map : ue::ContainerKind::Set;
        stride = field->stride;
        if (!stride) return JsonError(session, jsonCommand, "layout", "the element layout is unknown (failed its check); read the header raw");
    } else {
        return JsonError(session, jsonCommand, "field", field->name + " is " + field->type + ", not an array, set or map");
    }
    auto page = ue::Entries(client, object, field->offset, kind, stride, command.limit);
    if (!page) return CommandError(session, jsonCommand, page.error());
    // Arrays hold elements; set and map slots hold the key at 0 and a map's value after it.
    ue::Field key{};
    key.size = field->elementSize;
    key.type = field->elementType;
    key.structType = field->structType;
    key.structName = field->structName;
    ue::Field value{};
    value.size = field->valueSize;
    value.type = field->valueType;
    value.structType = field->valueStructType;
    value.structName = field->valueStructName;
    const bool map = kind == ue::ContainerKind::Map;
    const auto bytes = std::span<const uint8_t>(page->bytes);
    const size_t count = bytes.size() / stride;
    const auto keyOf = [&](size_t i) { return bytes.subspan(i * stride, key.size); };
    const auto valueOf = [&](size_t i) { return bytes.subspan(i * stride + field->valueOffset, value.size); };
    // FNames in one lookup, object pointers in one describe-many.
    std::vector<int32_t> indices;
    std::vector<std::string> names;
    StructCache& structs = Ue(session).structs;
    if (client.Supports(ue::op::Names)) {
        const auto collect = [&](int32_t index) -> std::string_view {
            if (index >= 0 && std::ranges::find(indices, index) == indices.end()) indices.push_back(index);
            return {};
        };
        for (size_t i = 0; i < count; ++i) {
            FormatField(key, keyOf(i), structs, 0, collect);
            if (map) FormatField(value, valueOf(i), structs, 0, collect);
        }
        if (!indices.empty())
            if (auto result = ue::Names(client, indices); result) names = std::move(*result);
    }
    const auto lookupName = [&](int32_t index) -> std::string_view {
        const auto at = std::ranges::find(indices, index);
        const auto slot = static_cast<size_t>(at - indices.begin());
        return at == indices.end() || slot >= names.size() ? std::string_view{} : std::string_view(names[slot]);
    };
    std::vector<Ref> targets;
    for (size_t i = 0; i < count; ++i) {
        for (const auto target : {PointerRef(key.type, keyOf(i)), map ? PointerRef(value.type, valueOf(i)) : Ref{}})
            if ((target.address || target.serial) && targets.size() < ue::MaxDescribeMany &&
                std::ranges::find(targets, target) == targets.end()) targets.push_back(target);
    }
    std::vector<std::optional<ue::Object>> described;
    if (!targets.empty() && client.Supports(ue::op::DescribeMany))
        if (auto result = ue::DescribeMany(client, targets); result) described = std::move(*result);
    const auto objectOf = [&](const Ref& target) -> const ue::Object* {
        const auto at = std::ranges::find(targets, target);
        const auto slot = static_cast<size_t>(at - targets.begin());
        return at == targets.end() || slot >= described.size() || !described[slot] ? nullptr : &*described[slot];
    };
    shape::Entries result;
    bool partial = false;
    // One part of a row: its value, and the object a pointer names.
    const auto part = [&](const ue::Field& of, std::span<const uint8_t> data, std::optional<shape::Object>& named) {
        if (const auto* found = objectOf(PointerRef(of.type, data))) {
            named = MakeObject(*found);
            RememberRef(session.refs, found->ref);
        }
        return JsonFieldValue(of, data, structs, lookupName, partial, Style(session), 1);
    };
    for (size_t i = 0; i < count; ++i) {
        shape::EntryRow row;
        if (map) {
            row.key = part(key, keyOf(i), row.keyObject);
            row.value = part(value, valueOf(i), row.valueObject);
        } else {
            row.value = part(key, keyOf(i), row.valueObject);
        }
        result.rows.push_back(std::move(row));
    }
    result.ref = ReusableRef(object);
    result.field = field->name;
    result.kind = kind == ue::ContainerKind::Array ? "array" : map ? "map" : "set";
    result.total = page->total;
    result.from = command.limit;
    if (page->next) result.nextCursor = page->next;
    result.truncated = partial;
    Emit(session, jsonCommand, std::move(result));
}
}
