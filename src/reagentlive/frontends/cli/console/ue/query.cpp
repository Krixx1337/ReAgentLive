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

void RunEach(Session& session, const Command& command, std::vector<Ref>& refs, StructCache& structs,
             std::string_view jsonCommand) {
    Client& client = session.client;
    const auto fail = [&](std::string message) { JsonError(session, jsonCommand, "validation", message); };
    auto type = ResolveClass(session, command.className);
    if (!type) return CommandError(session, jsonCommand, type.error());
    ue::EachQuery query{command.className};
    std::optional<ue::Field> field;
    ue::Signature signature;
    std::string target;
    auto function = ue::Function(client, *type, command.target);
    if (function) {
        auto parameters = ue::Parameters(client, function->address);
        if (!parameters) return CommandError(session, jsonCommand, parameters.error());
        signature = std::move(*parameters);
        if (!ue::EachFunctionAllowed(function->flags)) return fail("each calls only const or pure functions");
        if (auto refusal = calls::EachRefusal(signature, structs); !refusal.empty()) return fail(std::move(refusal));
        if (!signature.size || signature.size > ue::MaxEachValue)
            return fail("parameter block must be 1.." + std::to_string(ue::MaxEachValue) + " bytes");
        target = function->owner + ":" + function->name;
        query.function = target;
        query.size = signature.size;
    } else if (function.error().kind == Error::Kind::Agent && function.error().status == Status::NotFound) {
        auto fields = ObjectFields(session, *type);
        if (!fields) return CommandError(session, jsonCommand, fields.error());
        const std::string_view wanted = command.target;
        const auto matches = [&](const ue::Field& candidate) {
            const auto colon = wanted.find(':');
            if (colon == std::string_view::npos) return session.names.Matches(candidate, wanted);
            return wanted.substr(0, colon) == candidate.owner && session.names.Matches(candidate, wanted.substr(colon + 1));
        };
        const auto found = std::ranges::find_if(*fields, matches);
        if (found == fields->end())
            return fail("neither a function nor a field of " + command.className + ": " + command.target);
        if (std::ranges::find_if(std::next(found), fields->end(), matches) != fields->end())
            return fail("ambiguous field " + command.target + " (use Owner:Field)");
        if (!found->size || found->size > ue::MaxEachValue)
            return fail("field must be 1.." + std::to_string(ue::MaxEachValue) + " bytes");
        field = *found;
        target = found->owner + ":" + found->name;
        query.offset = found->offset;
        query.size = found->size;
    } else {
        return CommandError(session, jsonCommand, function.error());
    }
    auto result = ue::Each(client, query);
    if (!result) return CommandError(session, jsonCommand, result.error());
    refs.clear();
    bool truncated = false;
    Json rows = Json::array();
    std::vector<std::pair<Ref, std::vector<uint8_t>>> last;
    for (const auto& row : result->rows) {
        refs.push_back(row.object);
        Json item = {{"ref", RefText(row.object)}, {"value", EachValue(field, signature, row.value, structs, Style(session), truncated)}};
        if (session.meta) item["hex"] = format_detail::Hex(row.value, row.value.size());
        rows.push_back(std::move(item));
        last.emplace_back(row.object, row.value);
    }
    Json record = {{"class", command.className}, {"class_ref", RefText(*type)}, {"target", target},
        {"mode", field ? "field" : "call"}, {"count", result->rows.size()}, {"capped", result->capped},
        {"rows", std::move(rows)}, {"truncated", truncated}};
    if (command.seconds) {
        Json changes = Json::array();
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
                Json change = {{"ms", ms}, {"tick", tick}, {"ref", RefText(row.object)},
                    {"before", EachValue(field, signature, previous->second, structs, Style(session), truncated)},
                    {"after", EachValue(field, signature, row.value, structs, Style(session), truncated)}};
                std::fprintf(stderr, "  +%lld ms  %s  %s -> %s\n", static_cast<long long>(ms), RefText(row.object).c_str(),
                    change["before"].dump().c_str(), change["after"].dump().c_str());
                std::fflush(stderr);
                previous->second = row.value;
                changes.push_back(std::move(change));
            }
        }
        record["seconds"] = command.seconds;
        record["polls"] = polls;
        record["changes"] = std::move(changes);
        if (pollError) record["poll_error"] = pollError->message;
    }
    JsonResult(session, jsonCommand, std::move(record));
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
}

// get: each path resolved through the field tables, following object
// pointers and descending into structs; then every value read back to back.
// The lookups may span ticks. The value reads have no console work between
// them, so under the hold they fit the agent's short wait for the next
// request and land in one host tick; same_tick says whether they did.
void RunGet(Session& session, const Command& command, StructCache& structs, std::vector<Ref>& refs,
            std::string_view jsonCommand) {
    Client& client = session.client;
    struct Value {
        std::string path, error;
        ue::Field field;
        std::vector<uint8_t> bytes;
        uint64_t address = 0;
        Ref object{};
        uint32_t offset = 0, size = 0;        // What to read in `object`, once every path is resolved.
        std::optional<NativeMember> native;   // A member from the native file, decoded by its type.
        bool stale = false;
    };
    const Client::Hold hold(client);
    const Ref root = ue::GetRef(command.request);
    std::vector<Value> values;
    for (const auto& path : command.fields) {
        Value& out = values.emplace_back();
        out.path = path;
        Ref object = root;
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
                if (rest.empty() && table == objectFields.get() && !Ue(session).native.Empty()) {
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
            if ((field->type == "ObjectProperty" || field->type == "ClassProperty" || field->type == "WeakObjectProperty") &&
                field->size == 8) {
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
    }
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
    std::vector<int32_t> indices;
    std::vector<std::string> names;
    if (client.Supports(ue::op::Names)) {
        const auto collect = [&](int32_t index) -> std::string_view {
            if (index >= 0 && std::ranges::find(indices, index) == indices.end()) indices.push_back(index);
            return {};
        };
        for (const auto& value : values)
            if (value.error.empty() && !value.native) FormatField(value.field, std::span(value.bytes), structs, 0, collect);
        if (!indices.empty())
            if (auto result = ue::Names(client, indices); result) names = std::move(*result);
    }
    const auto lookupName = [&](int32_t index) -> std::string_view {
        const auto at = std::ranges::find(indices, index);
        const auto slot = static_cast<size_t>(at - indices.begin());
        return at == indices.end() || slot >= names.size() ? std::string_view{} : std::string_view(names[slot]);
    };
    const auto modules = LoadedModules(session);
    Json rows = Json::array();
    bool failed = false;
    for (const auto& value : values) {
        if (!value.error.empty()) {
            failed = true;
            rows.push_back({{"path", value.path}, {"ok", false}, {"error", value.error}});
            continue;
        }
        if (value.native) {
            Json row = {{"path", value.path}, {"ok", true}, {"value", layout::Value(value.native->field, value.bytes)},
                {"address", format_detail::Pointer(value.address)}, {"object", ReusableRef(value.object)},
                {"truncated", false}, {"native", true}};
            if (value.stale) row["stale"] = true;
            if (session.meta) {
                row["type"] = NativeType(value.native->field);
                row["hex"] = format_detail::Hex(value.bytes, value.bytes.size());
            }
            AddLocation(row, value.address, modules);
            rows.push_back(std::move(row));
            continue;
        }
        bool partial = false;
        Json row = {{"path", value.path}, {"ok", true},
            {"value", JsonFieldValue(value.field, std::span(value.bytes), structs, lookupName, partial, Style(session))},
            {"address", format_detail::Pointer(value.address)}, {"object", ReusableRef(value.object)}, {"truncated", partial}};
        if (session.meta) {
            row["type"] = value.field.type;
            row["hex"] = format_detail::Hex(value.bytes, value.bytes.size());
        }
        AddLocation(row, value.address, modules);
        AddLocation(row, PointerRef(value.field.type, value.bytes).address, modules, "value_");
        if (const auto target = PointerRef(value.field.type, value.bytes); target.address) RememberRef(refs, target);
        rows.push_back(std::move(row));
    }
    session.failed |= failed;
    Json result = {{"ref", ReusableRef(root)}, {"values", std::move(rows)}, {"same_tick", sameTick}};
    if (!TickAccurate(session)) result["tick_accurate"] = false;
    JsonResult(session, jsonCommand, std::move(result));
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
    Json rows = Json::array();
    bool partial = false;
    const auto add = [&](Json& row, const char* name, const ue::Field& part, std::span<const uint8_t> data) {
        row[name] = JsonFieldValue(part, data, structs, lookupName, partial, Style(session), 1);
        if (const auto* named = objectOf(PointerRef(part.type, data))) {
            row[std::string(name) + "_object"] = ObjectJson(*named);
            RememberRef(session.refs, named->ref);
        }
    };
    for (size_t i = 0; i < count; ++i) {
        Json row = Json::object();
        if (map) {
            add(row, "key", key, keyOf(i));
            add(row, "value", value, valueOf(i));
        } else {
            add(row, "value", key, keyOf(i));
        }
        rows.push_back(std::move(row));
    }
    const char* kindName = kind == ue::ContainerKind::Array ? "array" : map ? "map" : "set";
    JsonResult(session, jsonCommand, {{"ref", ReusableRef(object)}, {"field", field->name}, {"kind", kindName},
        {"total", page->total}, {"from", command.limit}, {"entries", std::move(rows)},
        {"next_cursor", Cursor(page->next)}, {"truncated", partial}});
}
}
