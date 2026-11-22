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
#include <cctype>
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
#include "reagentlive/packs/ue/cli/results/call_text.h"
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/packs/ue/cli/state/cache_ue.h"
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/packs/ue/cli/values/format_ue.h"
#include "reagentlive/packs/ue/cli/values/json_output.h"
#include "reagentlive/frontends/cli/values/layout_json.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/core/client.h"
#include "reagentlive/packs/ue/client_ue.h"

#include "reagentlive/packs/ue/cli/console/internal_ue.h"
#include "reagentlive/packs/ue/cli/console/rows.h"
#include "reagentlive/frontends/cli/console/pack.h"
#include "reagentlive/packs/ue/cli/values/watch_field.h"

namespace reagentlive::cli::detail {

Result<Ref> ResolveClass(Session& session, const std::string& name) {
    Client& client = session.client;
    for (auto entry = Ue(session).classRefs.begin(); entry != Ue(session).classRefs.end(); ++entry) {
        if (entry->name != name) continue;
        if (entry->verified) return entry->ref;
        auto object = ue::Describe(client, entry->ref);
        if (object && object->ref == entry->ref && ue::SameWireName(object->name, name, ue::FindNameBytes)) {
            entry->verified = true;
            return entry->ref;
        }
        if (!object && object.error().kind != Error::Kind::Agent) return std::unexpected(object.error());
        Ue(session).classRefs.erase(entry);
        break;
    }
    // Exact: the agent compares the whole name and stops at the first class or
    // struct of that name instead of scanning (and counting) the whole table.
    // The reply's name is cut to the wire field, so it is compared that far.
    uint32_t from = 0;
    for (int page = 0; page < 64; ++page) {
        auto found = ue::Find(client, name, {"Struct", from, true});
        if (!found) return std::unexpected(found.error());
        for (const auto& object : found->objects) {
            if (!ue::SameWireName(object.name, name, ue::FindNameBytes)) continue;
            Ue(session).classRefs.push_back({name, object.ref, true});
            return object.ref;
        }
        if (!found->next) break;
        from = found->next;
    }
    return std::unexpected(Error{Error::Kind::Agent, Status::NotFound, 0, {}, "no class or struct named " + name});
}

// A default object (Default__Class) by exact name, kept for the session.
Result<Ref> DefaultObject(Session& session, const std::string& name) {
    auto& known = Ue(session).defaultObjects;
    if (const auto found = std::ranges::find(known, name, &ClassRef::name); found != known.end()) return found->ref;
    auto found = ue::Find(session.client, name, {"", 0, true});
    if (!found) return std::unexpected(found.error());
    for (const auto& object : found->objects)
        if (ue::SameWireName(object.name, name, ue::FindNameBytes)) { // The agent compared it whole.
            known.push_back({name, object.ref, true});
            return object.ref;
        }
    return std::unexpected(Error{Error::Kind::Agent, Status::NotFound, 0, {}, "no object named " + name});
}

// A pooled name's comparison index by text, kept for the session.
Result<int32_t> NameIndex(Session& session, std::string_view text) {
    auto& known = Ue(session).nameIndices;
    std::string key(text); // FNames compare without case: every spelling shares one entry.
    std::ranges::transform(key, key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (const auto found = known.find(key); found != known.end()) return found->second;
    for (uint32_t from = 0;;) {
        auto found = ue::NameFind(session.client, text, from);
        if (!found) return std::unexpected(found.error());
        // FNames compare without case.
        for (const auto& match : found->names)
            if (text::SameText(match.name, text)) {
                known.emplace(key, match.index);
                return match.index;
            }
        if (!found->next) break;
        from = found->next;
    }
    return std::unexpected(Error{Error::Kind::Agent, Status::NotFound, 0, {}, "no pooled name is '" + std::string(text) + "'"});
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
    std::string pseudo;                   // each: Outer, Name or Class, from the instance itself.
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

// The text each --where matches: a string, a name's or a text's string, else the JSON.
std::string MatchText(const Json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_object())
        for (const char* key : {"text", "name"})
            if (value.contains(key) && value[key].is_string()) return value[key].get<std::string>();
    return value.dump();
}
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
    session.input.reads = true; // Fields, or a function that must be const or pure: safe to run again.
    const auto fail = [&](std::string message) { JsonError(session, jsonCommand, "validation", message); };
    auto type = ResolveClass(session, command.className);
    if (!type) return CommandError(session, jsonCommand, type.error());
    // --where's field is read as one more column, hidden unless asked for.
    std::vector<std::string> paths = command.fields;
    std::optional<size_t> whereColumn;
    if (!command.whereField.empty()) {
        const auto at = std::ranges::find_if(paths, [&](const std::string& p) { return text::SameText(p, command.whereField); });
        if (at == paths.end()) paths.push_back(command.whereField);
        whereColumn = static_cast<size_t>(std::ranges::find_if(paths, [&](const std::string& p) {
            return text::SameText(p, command.whereField); }) - paths.begin());
    }
    const bool hiddenWhere = whereColumn && *whereColumn >= command.fields.size();
    uint64_t shownRows = 0, omittedRows = 0;
    const auto room = [&] {
        if (command.limit && shownRows >= command.limit) { ++omittedRows; return false; }
        ++shownRows;
        return true;
    };
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
    if (paths.size() == 1 && !whereColumn) {
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
            query.outer = {command.outerText, command.outerRef};
            query.function = target;
            query.size = signature.size;
            auto result = ue::Each(client, query);
            if (!result) return CommandError(session, jsonCommand, result.error());
            std::vector<std::pair<Ref, std::vector<uint8_t>>> last;
            for (const auto& row : result->rows) {
                last.emplace_back(row.object, row.value);
                if (!room()) continue;
                refs.push_back(row.object);
                shape::EachRow item{FullRef(row.object), {EachValue(std::nullopt, signature, row.value, structs,
                    Style(session), truncated)}, {}};
                if (session.meta) item.hex = HexBytes(row.value);
                record.rows.push_back(std::move(item));
            }
            if (omittedRows) record.omitted = omittedRows;
            record.columns = {target};
            record.mode = "call";
            record.capped = result->capped;
            record.sameTick = true;
            if (command.seconds) watch(query, std::nullopt, signature, std::move(last));
            record.truncated = truncated;
            if (command.nonzero)
                for (auto& row : record.rows)
                    for (auto& value : row.values) KeepNonZero(value);
            return Emit(session, jsonCommand, std::move(record));
        }
        if (function.error().kind != Error::Kind::Agent || function.error().status != Status::NotFound)
            return CommandError(session, jsonCommand, function.error());
    }

    // Field paths, resolved on the class; one past a pointer stops there for now.
    std::vector<PathTarget> columns;
    for (const auto& path : paths) {
        auto column = ResolvePath(session, structs, *type, path, true);
        // Outer, Name and Class name the instance itself when no field takes the name.
        if (!column.error.empty())
            for (const char* pseudo : {"Outer", "Name", "Class"})
                if (text::SameText(path, pseudo)) column = PathTarget{.pseudo = pseudo};
        if (!column.error.empty())
            return fail(paths.size() == 1 && !whereColumn ? "neither a function nor a field of " + command.className + ": " + path +
                " (" + column.error + ")" : path + ": " + column.error + " (a function is each's only column)");
        // A set or map column reads its header in the pass; the decoder reads its elements per instance.
        if (IsContainer(column.field.type) && !column.pointer && column.size > ue::MaxEachValue)
            column.size = sizeof(ue::SparseHeader);
        if (column.size > ue::MaxEachValue)
            return fail(path + " is " + std::to_string(column.size) + " bytes; each reads 1.." +
                std::to_string(ue::MaxEachValue) + " per instance (get reads it on one object)");
        columns.push_back(std::move(column));
    }
    if (command.seconds && (columns.size() != 1 || columns.front().pointer || !columns.front().pseudo.empty()))
        return fail("--seconds watches one field or function, not a path through a pointer");

    // The spans inside the instance, in passes of at most MaxEachValue bytes.
    struct Pass {
        uint32_t start = 0, end = 0;
        std::vector<std::pair<Ref, std::vector<uint8_t>>> rows;
    };
    std::vector<std::pair<uint32_t, uint32_t>> spans;
    for (const auto& column : columns)
        if (column.pseudo.empty()) spans.emplace_back(column.offset, column.offset + column.size);
    if (spans.empty()) spans.emplace_back(0, 8); // Only Outer, Name or Class: a pass still lists the instances.
    std::vector<Pass> passes;
    for (const auto& [begin, end] : shape::EachPasses(std::move(spans), ue::MaxEachValue)) passes.push_back({begin, end, {}});
    bool capped = false;
    ue::EachQuery first{command.className};
    for (size_t i = 0; i < passes.size(); ++i) {
        ue::EachQuery query{command.className};
        query.outer = {command.outerText, command.outerRef};
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
        std::optional<Json> fixed;            // Outer, Name, Class: the value itself.
    };
    const bool pointers = std::ranges::any_of(columns, &PathTarget::pointer);
    const Client::Hold hold(client);
    std::vector<std::vector<Cell>> cells;
    for (const auto& entry : passes.front().rows) {
        const Ref& object = entry.first;
        auto& row = cells.emplace_back();
        for (const auto& column : columns) {
            Cell& cell = row.emplace_back();
            if (!column.pseudo.empty()) continue; // Filled from the describe below.
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
    // Outer, Name and Class: every instance described, then the distinct outers named.
    if (std::ranges::any_of(columns, [](const PathTarget& column) { return !column.pseudo.empty(); })) {
        std::vector<Ref> instances;
        for (const auto& entry : passes.front().rows) instances.push_back(entry.first);
        auto described = ue::DescribeMany(client, instances);
        if (!described) return CommandError(session, jsonCommand, described.error());
        std::vector<Ref> outers;
        for (const auto& object : *described)
            if (object && object->outer.address && std::ranges::find(outers, object->outer) == outers.end())
                outers.push_back(object->outer);
        std::vector<std::optional<ue::Object>> outerObjects;
        if (!outers.empty())
            if (auto named = ue::DescribeMany(client, outers); named) outerObjects = std::move(*named);
        for (size_t i = 0; i < cells.size(); ++i)
            for (size_t c = 0; c < columns.size(); ++c) {
                if (columns[c].pseudo.empty()) continue;
                Cell& cell = cells[i][c];
                const auto& object = i < described->size() ? (*described)[i] : std::nullopt;
                if (!object) { cell.error = "the instance was gone"; continue; }
                if (columns[c].pseudo == "Name") cell.fixed = object->name;
                else if (columns[c].pseudo == "Class") cell.fixed = object->className;
                else {
                    cell.fixed = nullptr;
                    const auto at = std::ranges::find(outers, object->outer);
                    const auto slot = static_cast<size_t>(at - outers.begin());
                    if (at != outers.end() && slot < outerObjects.size() && outerObjects[slot])
                        cell.fixed = Json{{"name", outerObjects[slot]->name}, {"ref", RefText(outerObjects[slot]->ref)}};
                }
            }
    }
    // Every value through the decoder (nested containers, tag lists, text
    // and pointers at any depth; --meta the reflected layout).
    RowReader reader(session, structs, command.elements ? command.elements : RowReader::PreviewNested);
    reader.Meta(session.meta);
    for (const auto& row : cells)
        for (const auto& cell : row) {
            if (!cell.error.empty() || cell.native || cell.fixed) continue;
            bool ignored = false;
            reader.Value(cell.field, cell.bytes, ignored);
        }
    reader.Resolve();

    std::vector<shape::EachError> errors;
    const auto value = [&](const Cell& cell) -> Json {
        if (cell.fixed) return *cell.fixed;
        if (cell.native) return layout::Value(cell.native->field, cell.bytes);
        return reader.Value(cell.field, cell.bytes, truncated);
    };
    uint64_t matched = 0;
    for (size_t i = 0; i < cells.size(); ++i) {
        const Ref& object = passes.front().rows[i].first;
        if (whereColumn) {
            const Cell& cell = cells[i][*whereColumn];
            if (!cell.error.empty() || !text::ContainsNoCase(MatchText(value(cell)), command.match)) continue;
            ++matched;
        }
        if (!room()) continue;
        refs.push_back(object);
        shape::EachRow item{FullRef(object), {}, {}};
        for (size_t c = 0; c < cells[i].size(); ++c) {
            if (hiddenWhere && c == *whereColumn) continue;
            const Cell& cell = cells[i][c];
            if (!cell.error.empty()) {
                item.values.emplace_back(nullptr);
                errors.push_back({FullRef(object), static_cast<uint32_t>(c), cell.error});
            } else {
                item.values.push_back(value(cell));
            }
        }
        if (session.meta && columns.size() == 1 && !columns.front().pointer) item.hex = HexBytes(cells[i].front().bytes);
        record.rows.push_back(std::move(item));
    }
    record.columns = command.fields;
    if (whereColumn) record.matched = matched;
    if (omittedRows) record.omitted = omittedRows;
    record.mode = "field";
    record.capped = capped;
    // Container elements are read per instance after the pass (--meta prints headers only).
    const bool elements = !session.meta && std::ranges::any_of(columns, [](const PathTarget& column) {
        return column.field.type == "ArrayProperty" || IsContainer(column.field.type); });
    record.sameTick = passes.size() == 1 && !pointers && !elements;
    if (!errors.empty()) record.errors = std::move(errors);
    if (command.seconds) {
        std::vector<std::pair<Ref, std::vector<uint8_t>>> last = passes.front().rows;
        watch(first, columns.front().field, {}, std::move(last));
    }
    record.truncated = truncated;
    if (command.nonzero)
        for (auto& row : record.rows)
            for (auto& cell : row.values) KeepNonZero(cell);
    if (reader.Unnamed()) record.unnamed = reader.Unnamed();
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
    // A gone object fails the command, as it does for every verb; a bad path fails its row.
    if (const auto table = ObjectTable(session, root); !table) return CommandError(session, jsonCommand, table.error());
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
    // Every value through the decoder, as each does; get names a top-level pointer on its row.
    RowReader reader(session, structs, command.elements ? command.elements : RowReader::PreviewNested);
    reader.KeepTopPointers();
    reader.Meta(session.meta);
    for (const auto& value : values) {
        if (!value.error.empty() || value.native) continue;
        bool ignored = false;
        reader.Value(value.field, std::span(value.bytes), ignored);
    }
    reader.Resolve();
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
        row.value = reader.Value(value.field, std::span(value.bytes), partial);
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
    if (command.nonzero)
        for (auto& row : result.rows)
            if (row.value) KeepNonZero(*row.value);
    if (reader.Unnamed()) result.unnamed = reader.Unnamed();
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
    // FNames in one lookup, object pointers in one describe-many. Elements
    // decode through the shared decoder (RowReader), as dump's previews do:
    // pointers nested in an element are named, texts converted. A top-level
    // pointer element keeps its object beside it.
    StructCache& structs = Ue(session).structs;
    RowReader reader(session, structs, RowReader::PreviewNested);
    reader.KeepTopPointers();
    reader.Meta(session.meta);
    {
        bool ignored = false;
        for (size_t i = 0; i < count; ++i) {
            reader.Value(key, keyOf(i), ignored);
            if (map) reader.Value(value, valueOf(i), ignored);
        }
    }
    reader.Resolve();
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
        return reader.Value(of, data, partial);
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
    if (reader.Unnamed()) result.unnamed = reader.Unnamed();
    if (!TickAccurate(session)) result.tickAccurate = false;
    Emit(session, jsonCommand, std::move(result));
}

std::expected<WatchFieldRange, std::string> PackWatchField(Session& session, const Ref& object, std::string_view path) {
    StructCache& structs = Ue(session).structs; // The session's, so a watch reuses known structs.
    const auto target = ResolvePath(session, structs, object, path, false);
    if (!target.error.empty()) return std::unexpected(target.error);
    if (target.native) return std::unexpected(std::string(path) + " is a native member: watch it as <addr>:<type>");
    if (!target.size || target.size > MaxWatchRange)
        return std::unexpected(std::string(path) + " is " + std::to_string(target.size) + " bytes; a range is 1.." +
            std::to_string(MaxWatchRange));
    const auto* members = target.field.type == "StructProperty" && target.field.structType.address ?
        structs(target.field.structType) : nullptr;
    return WatchFieldRange{target.address, target.object.address, target.size, watch_field::FieldType(target.field, members)};
}
}
