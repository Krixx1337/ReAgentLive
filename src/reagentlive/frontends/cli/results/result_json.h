// reagentlive CLI: result objects of the lookup commands, built from client
// results only (no I/O), so one test pins their keys against the table in
// docs/commands/results.md ("Result shapes").
#pragma once
#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/frontends/cli/values/json_output.h"
#include "reagentlive/frontends/cli/state/native_names.h"

namespace reagentlive::cli {
// A literal ref when the object is identified, else its address.
inline std::string ReusableRef(const Ref& object) {
    return object.index >= 0 ? RefText(object) : format_detail::Pointer(object.address);
}

// The loaded module holding an address, or null.
inline const Module* ModuleAt(uint64_t address, std::span<const Module> modules) {
    for (const auto& module : modules)
        if (address >= module.base && address - module.base < module.size) return &module;
    return nullptr;
}

// Hex with 0x0 for zero (format_detail::Pointer prints "null").
inline std::string HexNumber(uint64_t value) {
    char text[24];
    snprintf(text, sizeof(text), "0x%" PRIx64, value);
    return text;
}

// Adds `<prefix>module` and `<prefix>rva` beside an address inside a loaded
// module, so it opens at the same RVA in a static-analysis tool.
inline void AddLocation(Json& object, uint64_t address, std::span<const Module> modules, std::string_view prefix = {}) {
    const auto* module = address ? ModuleAt(address, modules) : nullptr;
    if (!module) return;
    const std::string key(prefix);
    object[key + "module"] = module->name;
    object[key + "rva"] = HexNumber(address - module->base);
}

// class_size only from a single describe: the object's class size, which
// reaches past the last reflected field (native members, object tails).
inline Json ObjectJson(const ue::Object& object) {
    Json row = {{"ref", RefText(object.ref)}, {"name", object.name}, {"class", object.className}};
    if (object.size) row["class_size"] = object.size;
    return row;
}

// peek: what one live qword holds. The console fills it from one read, one
// describe per pointer-shaped value, readability checks and one name lookup.
struct PeekValue {
    uint64_t address = 0, value = 0;
    bool read = false;                  // False: this qword could not be read.
    std::optional<ue::Object> object;   // The value is a live object.
    bool readable = false;              // The value points to readable memory.
    std::string name;                   // The low 32 bits as an FName, empty if none.
    std::string region;                 // Readable pointer: its region kind when the agent has region.
};

// Values worth a describe or a readability check: user-mode, past the null page.
inline bool PointerShaped(uint64_t value) { return value >= 0x10000 && value < 0x7fffffff0000; }

// One label, the first that holds: object, module, heap, value. The name is
// only a hint, and only on a value row: almost any small number decodes to
// some name, so deciding it is one is the reader's job.
inline Json PeekRowJson(const PeekValue& v, std::span<const Module> modules) {
    Json row = {{"address", format_detail::Pointer(v.address)}};
    AddLocation(row, v.address, modules);
    if (!v.read) {
        row["label"] = "unreadable";
        return row;
    }
    row["value"] = HexNumber(v.value);
    if (v.object) {
        row["label"] = "object";
        row["object"] = ObjectJson(*v.object);
    } else if (ModuleAt(v.value, modules)) {
        row["label"] = "module";
        AddLocation(row, v.value, modules, "value_");
    } else if (v.readable && PointerShaped(v.value)) {
        // Private memory is heap (or a stack); a mapped view or anything else says so.
        row["label"] = v.region.empty() || v.region == "private" ? "heap" : v.region;
    } else {
        uint8_t bytes[8];
        memcpy(bytes, &v.value, sizeof(bytes));
        row["label"] = "value";
        row["f32"] = Json::array({JsonNumber<float>(std::span(bytes, 4)), JsonNumber<float>(std::span(bytes + 4, 4))});
        row["f64"] = JsonNumber<double>(std::span(bytes, 8));
        if (!v.name.empty()) row["name_hint"] = v.name;
    }
    return row;
}

// One confirmed label for the static tool, as a JSON Lines row. The module's
// image size and timestamp tie it to one build.
inline Json LabelJson(const Module& module, uint32_t timestamp, uint64_t address, std::string_view name,
                      std::string_view kind) {
    return {{"module", module.name}, {"module_size", module.size}, {"timestamp", timestamp},
        {"rva", HexNumber(address - module.base)}, {"name", name}, {"kind", kind}};
}

inline Json Cursor(uint32_t next) { return next ? Json(next) : Json(nullptr); }

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

// batch: where each command ran. `before` is the batch's last tick when the
// command started (0 before any response); first and last are its own
// responses' ticks (0 when it sent no request).
struct BatchItemTicks { uint64_t before = 0, first = 0, last = 0; };

// The batch result. Each envelope gets `tick` (and `last_tick` when its
// responses spanned ticks); `spans_ticks` lists the commands during which the
// batch's tick moved: their last response is later than the tick the batch
// was in when they started.
inline Json BatchJson(std::vector<Json> results, std::span<const BatchItemTicks> ticks, uint64_t first, uint64_t last) {
    Json spans = Json::array();
    for (size_t i = 0; i < results.size() && i < ticks.size(); ++i) {
        const auto& item = ticks[i];
        if (!item.first) continue;
        results[i]["tick"] = item.first;
        if (item.last != item.first) results[i]["last_tick"] = item.last;
        if (item.last > (item.before ? item.before : item.first))
            spans.push_back({{"index", i}, {"command", results[i].value("command", "")}});
    }
    const bool allOk = std::ranges::all_of(results, [](const Json& r) { return r.value("ok", false); });
    return {{"count", results.size()}, {"all_ok", allOk}, {"same_tick", first == last}, {"first_tick", first},
        {"last_tick", last}, {"spans_ticks", std::move(spans)}, {"results", std::move(results)}};
}

// Envelopes: every line is {command, ok, result} or {command, ok, error}.
inline Json ResultEnvelope(std::string_view command, Json result) {
    return {{"command", command}, {"ok", true}, {"result", std::move(result)}};
}
// Validation and console errors: kind and message only.
inline Json ErrorEnvelope(std::string_view command, std::string_view kind, std::string_view message) {
    return {{"command", command}, {"ok", false}, {"error", {{"kind", kind}, {"message", message}}}};
}
// Client errors add the agent's status, detail and reason.
inline Json ErrorEnvelope(std::string_view command, const Error& error) {
    const char* kind = error.kind == Error::Kind::Timeout ? "timeout" :
        error.kind == Error::Kind::Attach ? "attach" : error.kind == Error::Kind::Busy ? "busy" : "agent";
    return {{"command", command}, {"ok", false}, {"error", {
        {"kind", kind}, {"message", error.message}, {"status", static_cast<uint32_t>(error.status)},
        {"detail", error.detail}, {"reason", error.reason}}}};
}
}
