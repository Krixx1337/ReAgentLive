// reagentlive CLI: result objects of the core commands and the shared
// envelopes, built from client results only (no I/O), so one test pins their
// keys against the table in docs/commands/results.md ("Result shapes"). The
// Unreal pack's are in results/ue/result_json_ue.h.
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
#include "reagentlive/frontends/cli/values/json.h"

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

// peek: what one live qword holds. The console fills it from one read,
// readability checks and, with an engine pack, one describe per
// pointer-shaped value and one name lookup.
struct PeekValue {
    uint64_t address = 0, value = 0;
    bool read = false;                  // False: this qword could not be read.
    std::optional<Json> object;         // The value is a live object: the pack's description of it.
    bool readable = false;              // The value points to readable memory.
    std::string name;                   // The pack's name for the low 32 bits, empty if none.
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
        row["object"] = *v.object;
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
