// reagentlive CLI, Unreal pack: result objects of the object, field and
// function commands, built from client results only (no I/O), so one test
// pins their keys against docs/commands/results.md ("Result shapes").
#pragma once
#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/frontends/cli/values/ue/json_output.h"
#include "reagentlive/frontends/cli/state/ue/native_names.h"
#include "reagentlive/ue/client_ue.h"

namespace reagentlive::cli {
// class_size only from a single describe: the object's class size, which
// reaches past the last reflected field (native members, object tails).
inline Json ObjectJson(const ue::Object& object) {
    Json row = {{"ref", RefText(object.ref)}, {"name", object.name}, {"class", object.className}};
    if (object.size) row["class_size"] = object.size;
    return row;
}

inline Json FindJson(const ue::FindResult& found, uint32_t from) {
    Json objects = Json::array();
    for (const auto& object : found.objects) objects.push_back(ObjectJson(object));
    return {{"objects", std::move(objects)}, {"total", from ? Json(nullptr) : Json(found.total)},
        {"from", from}, {"next_cursor", Cursor(found.next)}, {"truncated", from != 0 || found.next != 0}};
}

// The agent lists functions as "Owner:Name".
inline Json FunctionsJson(const Ref& ref, const ue::FunctionList& list, uint32_t from) {
    Json rows = Json::array();
    for (const auto& text : list.names) {
        const auto colon = text.find(':');
        rows.push_back(colon == std::string::npos ? Json{{"owner", nullptr}, {"name", text}} :
            Json{{"owner", text.substr(0, colon)}, {"name", text.substr(colon + 1)}});
    }
    return {{"ref", ReusableRef(ref)}, {"functions", std::move(rows)}, {"from", from},
        {"next_cursor", Cursor(list.next)}, {"truncated", from != 0 || list.next != 0}};
}

// `label(field)` is the readable name shown beside an obfuscated one.
template<class Label>
Json FieldsJson(const Ref& ref, std::span<const ue::Field> sorted, const Label& label) {
    Json rows = Json::array();
    for (const auto& field : sorted) {
        auto row = FieldMetadata(field);
        row["label"] = label(field);
        rows.push_back(std::move(row));
    }
    return {{"ref", ReusableRef(ref)}, {"fields", std::move(rows)}, {"truncated", sorted.size() >= ue::MaxFieldStart}};
}

// A member from the native file as a field row: `native` marks it, `stale`
// when it was labelled under another build of the main image.
inline Json NativeFieldJson(const NativeMember& member, bool stale) {
    Json row = {{"name", member.field.name}, {"owner", member.owner}, {"type", NativeType(member.field)},
        {"offset", member.field.offset}, {"size", member.field.Size()}, {"label", member.field.name}, {"native", true}};
    if (stale) row["stale"] = true;
    return row;
}

// Field rows (reflected and native) in offset order.
inline void SortRowsByOffset(Json& rows) {
    std::stable_sort(rows.begin(), rows.end(),
        [](const Json& a, const Json& b) { return a.value("offset", 0u) < b.value("offset", 0u); });
}

// `modules`: the loaded modules, or empty with `modulesError` when they could not be listed.
inline Json FunctionJson(const Ref& ref, const ue::FunctionInfo& function, std::span<const Module> modules,
                         std::string_view modulesError = {}) {
    Json result = {{"ref", ReusableRef(ref)}, {"function", function.owner + ":" + function.name},
        {"address", format_detail::Pointer(function.address)}, {"flags", function.flags},
        {"exec", function.exec ? Json(format_detail::Pointer(function.exec)) : Json(nullptr)},
        {"executable", function.executable}};
    if (function.interface) {
        // Calls are refused: the stub would run another function on this object.
        result["interface"] = {{"offset", format_detail::Pointer(function.interfaceOffset)},
            {"slot", function.slot >= 0 ? Json(function.slot / 8) : Json(nullptr)},
            {"implementation", function.implementation ? Json(format_detail::Pointer(function.implementation)) : Json(nullptr)}};
        AddLocation(result["interface"], function.implementation, modules, "implementation_");
    }
    if (!function.exec) return result;
    if (!modulesError.empty()) result["module_error"] = modulesError;
    AddLocation(result, function.exec, modules);
    return result;
}

inline const char* ParameterDirection(uint64_t flags) {
    return flags & ue::Return ? "return" :
        flags & ue::Reference ? (flags & ue::Output ? "in/out ref" : "in ref") :
        flags & ue::Output ? "out" : "in";
}

inline Json ParamsJson(const Ref& ref, const ue::FunctionInfo& function, const ue::Signature& signature) {
    Json parameters = Json::array();
    for (const auto& parameter : signature.parameters) {
        auto row = FieldMetadata(parameter);
        row["direction"] = ParameterDirection(parameter.parameterFlags);
        row["flags"] = parameter.parameterFlags;
        parameters.push_back(std::move(row));
    }
    return {{"ref", ReusableRef(ref)}, {"function", function.owner + ":" + function.name},
        {"address", format_detail::Pointer(function.address)}, {"block_size", signature.size},
        {"parameters", std::move(parameters)}};
}

inline Json NamesJson(std::span<const int32_t> indices, std::span<const std::string> names) {
    Json rows = Json::array();
    for (size_t i = 0; i < indices.size(); ++i)
        rows.push_back({{"index", indices[i]},
            {"name", i < names.size() && !names[i].empty() ? Json(names[i]) : Json(nullptr)}});
    return {{"names", std::move(rows)}};
}

inline Json NameFindJson(const ue::NameFindResult& found, uint32_t from) {
    Json rows = Json::array();
    for (const auto& name : found.names) rows.push_back({{"index", name.index}, {"name", name.name}});
    return {{"names", std::move(rows)}, {"from", from}, {"next_cursor", Cursor(found.next)}};
}

// `rows`: one object per shown field (FieldMetadata plus label, value,
// truncated, and object, preview, preview_error or before when they apply).
inline Json DumpJson(const Ref& ref, Json rows, bool truncated, bool lookupLimited,
                     std::optional<std::pair<size_t, size_t>> filtered) {
    Json result = {{"ref", ReusableRef(ref)}, {"fields", std::move(rows)}, {"truncated", truncated},
        {"object_lookup_limited", lookupLimited}};
    if (filtered) result["filtered"] = {{"shown", filtered->first}, {"total", filtered->second}};
    return result;
}

// func --script: the confirmed anchored entries grouped by kind. `offset` is
// the first opcode's offset in the bytecode, `count` how often it occurs.
inline Json ScriptJson(const Ref& ref, std::string_view function, const ue::ScriptResult& script) {
    Json names = Json::array(), functions = Json::array(), properties = Json::array();
    for (const auto& entry : script.entries) {
        Json row = {{"offset", entry.offset}, {"count", entry.count}};
        switch (entry.kind) {
        case ue::ScriptKind::Name:
            row["index"] = entry.name;
            row["name"] = entry.text;
            names.push_back(std::move(row));
            break;
        case ue::ScriptKind::Function:
            row["ref"] = RefText(entry.object);
            row["function"] = entry.text;
            functions.push_back(std::move(row));
            break;
        case ue::ScriptKind::Property:
            row["address"] = format_detail::Pointer(entry.pointer);
            row["property"] = entry.text;
            properties.push_back(std::move(row));
            break;
        }
    }
    return {{"ref", ReusableRef(ref)}, {"function", function}, {"size", script.size}, {"names", std::move(names)},
        {"functions", std::move(functions)}, {"properties", std::move(properties)}};
}

inline Json RefsJson(int32_t index, std::string_view name, const ue::RefsResult& refs) {
    Json rows = Json::array();
    for (const auto& row : refs.rows)
        rows.push_back({{"ref", RefText(row.function)}, {"owner", row.owner}, {"name", row.name},
            {"offset", row.offset}, {"count", row.count}});
    return {{"index", index}, {"name", name.empty() ? Json(nullptr) : Json(name)}, {"functions", std::move(rows)},
        {"capped", refs.capped}};
}

}
