// reagentlive CLI console: Field tables, struct layouts, fields and dump.
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
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/state/cache.h"
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/frontends/cli/values/format.h"
#include "reagentlive/frontends/cli/values/json_output.h"
#include "reagentlive/frontends/cli/values/layout_json.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/core/client.h"
#include "reagentlive/ue/client_ue.h"

#include "reagentlive/frontends/cli/console/internal.h"

namespace reagentlive::cli::detail {

std::vector<ue::Field> SortedByOffset(std::vector<ue::Field> fields) {
    std::stable_sort(fields.begin(), fields.end(), [](const ue::Field& a, const ue::Field& b) { return a.offset < b.offset; });
    return fields;
}

size_t DumpEnd(std::span<const ue::Field> fields) {
    size_t end = 0;
    for (const auto& f : fields) end = std::max(end, std::min<size_t>(size_t{f.offset} + f.size, MaxDumpBytes));
    return end;
}

std::span<const uint8_t> FieldBytes(std::span<const uint8_t> bytes, const ue::Field& field) {
    if (size_t{field.offset} + field.size > bytes.size()) return {};
    return bytes.subspan(field.offset, field.size);
}


Result<std::vector<ue::Field>> ObjectFields(Session& session, const Ref& object) {
    Client& client = session.client;
    const ue::KnownTable known = [&](int32_t type) -> const std::vector<ue::Field>* {
        const auto found = session.tables.find(type);
        return found == session.tables.end() ? nullptr : &found->second;
    };
    auto table = ue::Fields(client, object, known);
    if (!table) return std::unexpected(table.error());
    if (!table->cached && table->type >= 0 && (session.tables.size() < MaxCachedTables || session.tables.contains(table->type)))
        session.tables[table->type] = table->fields;
    return std::move(table->fields);
}

std::string FieldValue(const ue::Field& field, std::span<const uint8_t> bytes, StructCache& structs) {
    return FormatField(field, bytes, structs);
}

namespace {
// An array's element as a field of its own, so struct elements expand too.
ue::Field ElementField(const ue::Field& array) {
    ue::Field element{};
    element.size = array.elementSize;
    element.type = array.elementType;
    element.structType = array.structType;
    element.structName = array.structName;
    return element;
}

// One previewed element's parts: an array's element, a set's element, or a
// map's key and value, each at its offset in a slot of `stride` bytes.
struct Part { const char* name; ue::Field field; uint32_t offset; };
struct ElementLayout { uint32_t stride; std::vector<Part> parts; };
ElementLayout ElementParts(const ue::Field& container) {
    if (container.type == "ArrayProperty") return {container.elementSize, {{"value", ElementField(container), 0}}};
    if (container.type == "SetProperty") return {container.stride, {{"value", ElementField(container), 0}}};
    ue::Field value{};
    value.size = container.valueSize;
    value.type = container.valueType;
    value.structType = container.valueStructType;
    value.structName = container.valueStructName;
    return {container.stride, {{"key", ElementField(container), 0}, {"value", std::move(value), container.valueOffset}}};
}
// Occupied slots of a set or map field, or -1 when its header is not a valid one.
int64_t SetCount(std::span<const uint8_t> bytes) {
    ue::SparseHeader header{};
    if (bytes.size() < sizeof(header)) return -1;
    memcpy(&header, bytes.data(), sizeof(header));
    return ue::SparseHeaderValid(header) ? header.num - header.numFree : -1;
}
}

void PrintFields(Session& session, const Ref& object, bool instance, std::string_view jsonCommand) {
    auto fields = ObjectFields(session, object);
    if (!fields) return CommandError(session, jsonCommand, fields.error());
    const auto sorted = SortedByOffset(std::move(*fields));
    auto result = FieldsJson(object, sorted, [&](const ue::Field& field) { return session.names.Display(field); });
    const auto native = NativeMembers(session, object, sorted, instance);
    for (const auto& check : native.shown) result["fields"].push_back(NativeFieldJson(*check.member, check.stale));
    SortRowsByOffset(result["fields"]);
    if (!native.refused.empty()) result["native_refused"] = NativeRefusedJson(native.refused);
    return JsonResult(session, jsonCommand, std::move(result));
}

void RememberRef(std::vector<Ref>& refs, const Ref& object) {
    if (std::ranges::find(refs, object) == refs.end()) refs.push_back(object);
}

Ref PointerRef(std::string_view type, std::span<const uint8_t> bytes) {
    if (bytes.size() != 8) return {};
    if (type == "ObjectProperty" || type == "ClassProperty") {
        uint64_t address;
        memcpy(&address, bytes.data(), sizeof(address));
        return ue::AddressRef(address);
    }
    if (type == "WeakObjectProperty") {
        int32_t index, serial;
        memcpy(&index, bytes.data(), sizeof(index));
        memcpy(&serial, bytes.data() + sizeof(index), sizeof(serial));
        if (index >= 0 && serial > 0) return {0, index, serial};
    }
    return {};
}

// Fields plus their current values. Resolve only a bounded set of distinct pointers per dump.
// Filters (--match, --nonzero, --changed) drop rows before any lookup for them.
void PrintDump(Session& session, const Ref& object, std::vector<Ref>& refs, StructCache& structs, const Command& command,
               const std::optional<Snapshot>& sessionBaseline) {
    Client& client = session.client;
    constexpr size_t MaxDescriptions = 32;
    struct Description {
        Ref target;
        std::optional<ue::Object> object;
    };
    std::vector<Description> descriptions;
    bool describe = client.Supports(ue::op::Describe);
    const bool describeMany = client.Supports(ue::op::DescribeMany);
    bool limited = false;
    constexpr size_t MaxArrays = 16;
    size_t fieldLookups = 0, elementLookups = 0;
    // Collecting: new targets wait in `pending` for one DescribeMany; otherwise
    // (an agent without it) each is described at once.
    bool collecting = false;
    std::vector<size_t> pending;
    const auto describePointer = [&](std::string_view type, std::span<const uint8_t> value, size_t& lookups)
        -> const ue::Object* {
        const auto target = PointerRef(type, value);
        if (describe && (target.address || target.serial)) {
            auto found = std::ranges::find(descriptions, target, &Description::target);
            if (found == descriptions.end() && lookups < MaxDescriptions) {
                ++lookups;
                if (collecting) {
                    pending.push_back(descriptions.size());
                    descriptions.push_back({target, std::nullopt});
                    return nullptr;
                }
                auto result = ue::Describe(client, target);
                if (!result && result.error().kind == Error::Kind::Timeout) {
                    describe = false;
                    limited = true;
                }
                descriptions.push_back({target, result ? std::optional(std::move(*result)) : std::nullopt});
                found = std::prev(descriptions.end());
            }
            if (found == descriptions.end()) limited = true;
            else if (found->object) return &*found->object;
        }
        return nullptr;
    };
    const auto describePending = [&] {
        if (pending.empty()) return;
        std::vector<Ref> targets;
        for (const size_t slot : pending) targets.push_back(descriptions[slot].target);
        auto found = ue::DescribeMany(client, targets);
        if (found) {
            for (size_t i = 0; i < pending.size() && i < found->size(); ++i)
                descriptions[pending[i]].object = std::move((*found)[i]);
        } else {
            limited = true;
            if (found.error().kind == Error::Kind::Timeout) describe = false;
        }
        pending.clear();
    };
    std::optional<Snapshot> loaded;
    const Snapshot* baseline = nullptr;
    if (command.changed) {
        if (!command.file.empty() && !(loaded = LoadSnapshot(command.file)))
            return JsonError(session, "dump", "snapshot", "cannot read snapshot file " + command.file);
        baseline = loaded ? &*loaded : sessionBaseline ? &*sessionBaseline : nullptr;
        if (!baseline) return JsonError(session, "dump", "snapshot", "--changed needs a snap first (or a snapshot file)");
        if (baseline->object != object) return JsonError(session, "dump", "snapshot", "the snapshot is of another object ref");
    }
    auto fields = ObjectFields(session, object);
    if (!fields) return CommandError(session, "dump", fields.error());
    const auto sorted = SortedByOffset(std::move(*fields));
    if (baseline && sorted != baseline->fields)
        return JsonError(session, "dump", "snapshot", "reflected field layout changed; take a new snapshot");
    // Native members past the last reflected field widen the read.
    const auto native = NativeMembers(session, object, sorted, command.className.empty());
    size_t end = DumpEnd(sorted);
    for (const auto& check : native.shown)
        end = std::max(end, std::min<size_t>(size_t{check.member->field.offset} + check.member->field.Size(), MaxDumpBytes));
    auto bytes = client.ReadRange(object.address, end);
    if (!bytes) return CommandError(session, "dump", bytes.error());
    std::vector<bool> keep(sorted.size(), true);
    size_t shown = 0;
    for (size_t i = 0; i < sorted.size(); ++i) {
        const auto& f = sorted[i];
        const auto data = FieldBytes(*bytes, f);
        if (!command.match.empty())
            keep[i] = text::ContainsNoCase(session.names.Display(f), command.match) ||
                text::ContainsNoCase(f.name, command.match);
        if (command.nonzero && keep[i]) {
            keep[i] = f.type == "BoolProperty" && f.boolMask ? !data.empty() && (data[0] & f.boolMask) :
                std::ranges::any_of(data, [](uint8_t b) { return b != 0; });
        }
        if (baseline && keep[i]) {
            const auto old = FieldBytes(baseline->bytes, f);
            keep[i] = f.type == "BoolProperty" && f.boolMask && !old.empty() && !data.empty() ?
                (old[0] & f.boolMask) != (data[0] & f.boolMask) : !std::ranges::equal(old, data);
        }
        shown += keep[i];
    }

    // Reuse the captured previews when collecting names and printing the dump.
    struct Preview {
        size_t fieldIndex;
        Result<ue::ArrayPreview> elements;
    };
    std::vector<Preview> previews;
    size_t arrayLimitAt = sorted.size();
    if (client.Supports(ue::op::PreviewArray)) {
        for (size_t i = 0; i < sorted.size(); ++i) {
            const auto& f = sorted[i];
            const auto fieldBytes = FieldBytes(*bytes, f);
            ue::ArrayHeader header{};
            const bool set = f.type == "SetProperty" || f.type == "MapProperty";
            if (!keep[i]) continue;
            if (set) {
                if (!f.stride || !client.Supports(ue::op::Entries) || SetCount(fieldBytes) <= 0) continue;
            } else if (f.type != "ArrayProperty" || fieldBytes.size() != sizeof(header) ||
                f.elementType.empty() || !format_detail::Load(fieldBytes, header) ||
                !ue::ArrayPreviewBytes(header, f.elementSize)) continue;
            if (previews.size() == MaxArrays) {
                arrayLimitAt = i;
                break;
            }
            Result<ue::ArrayPreview> elements = std::unexpected(Error{});
            if (set) {
                const auto kind = f.type == "MapProperty" ? ue::ContainerKind::Map : ue::ContainerKind::Set;
                auto page = ue::Entries(client, object, f.offset, kind, f.stride, 0);
                if (page) {
                    const size_t kept = std::min<size_t>(page->bytes.size() / f.stride, ue::MaxArrayPreviewElements);
                    page->bytes.resize(kept * f.stride);
                    elements = ue::ArrayPreview{page->total, std::move(page->bytes)};
                } else {
                    elements = std::unexpected(page.error());
                }
            } else {
                elements = ue::PreviewArray(client, object, f);
            }
            const bool timedOut = !elements && elements.error().kind == Error::Kind::Timeout;
            previews.push_back({i, std::move(elements)});
            if (timedOut) break;
        }
    }

    std::vector<int32_t> indices;
    std::unordered_map<int32_t, size_t> nameSlots;
    std::vector<std::string> names;
    if (client.Supports(ue::op::Names)) {
        const auto collectName = [&](int32_t index) -> std::string_view {
            if (index >= 0 && nameSlots.try_emplace(index, indices.size()).second)
                indices.push_back(index);
            return {};
        };
        for (size_t i = 0; i < sorted.size(); ++i)
            if (keep[i]) FormatField(sorted[i], FieldBytes(*bytes, sorted[i]), structs, 0, collectName);
        for (const auto& preview : previews) {
            if (!preview.elements) continue;
            const auto layout = ElementParts(sorted[preview.fieldIndex]);
            const auto data = std::span(preview.elements->bytes);
            for (size_t offset = 0; offset < data.size(); offset += layout.stride)
                for (const auto& part : layout.parts)
                    FormatField(part.field, data.subspan(offset + part.offset, part.field.size), structs, 0, collectName);
        }
        if (auto result = ue::Names(client, indices); result) names = std::move(*result);
    }
    const auto lookupName = [&](int32_t index) -> std::string_view {
        const auto slot = nameSlots.find(index);
        if (slot == nameSlots.end() || slot->second >= names.size()) return {};
        return names[slot->second];
    };

    // Every pointer the rows will name, in two requests: fields, then array elements.
    if (describeMany) {
        collecting = true;
        for (size_t i = 0; i < sorted.size(); ++i)
            if (keep[i]) describePointer(sorted[i].type, FieldBytes(*bytes, sorted[i]), fieldLookups);
        describePending();
        for (const auto& captured : previews) {
            if (!captured.elements) continue;
            const auto layout = ElementParts(sorted[captured.fieldIndex]);
            const auto data = std::span(captured.elements->bytes);
            for (size_t offset = 0; offset < data.size(); offset += layout.stride)
                for (const auto& part : layout.parts)
                    describePointer(part.field.type, data.subspan(offset + part.offset, part.field.size), elementLookups);
        }
        describePending();
        collecting = false;
    }

    const auto modules = LoadedModules(session);
    Json rows = Json::array();
    bool truncated = sorted.size() >= ue::MaxFieldStart;
    auto preview = previews.begin();
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (!keep[i]) continue;
        const auto& field = sorted[i];
        const auto data = FieldBytes(*bytes, field);
        bool partial = false;
        Json row = FieldMetadata(field);
        row["label"] = session.names.Display(field);
        row["value"] = JsonFieldValue(field, data, structs, lookupName, partial, session.Style());
        if (baseline) {
            const auto old = FieldBytes(baseline->bytes, field);
            row["before"] = JsonFieldValue(field, old, structs, lookupName, partial, session.Style(), 1);
        }
        AddLocation(row, PointerRef(field.type, data).address, modules, "value_");
        if (const auto* named = describePointer(field.type, data, fieldLookups)) {
            row["object"] = ObjectJson(*named);
            RememberRef(refs, named->ref);
        }
        if (preview != previews.end() && preview->fieldIndex == i) {
            const auto& captured = *preview++;
            if (captured.elements) {
                const auto& elements = *captured.elements;
                const auto layout = ElementParts(field);
                const bool map = layout.parts.size() == 2;
                Json values = Json::array();
                const auto elementBytes = std::span(elements.bytes);
                for (size_t offset = 0; offset < elementBytes.size(); offset += layout.stride) {
                    Json item = Json::object();
                    for (const auto& part : layout.parts) {
                        const auto element = elementBytes.subspan(offset + part.offset, part.field.size);
                        item[part.name] = JsonFieldValue(part.field, element, structs, lookupName, partial, session.Style(), 1);
                        if (const auto* named = describePointer(part.field.type, element, elementLookups)) {
                            item[map ? std::string(part.name) + "_object" : std::string("object")] = ObjectJson(*named);
                            RememberRef(refs, named->ref);
                        }
                    }
                    values.push_back(std::move(item));
                }
                partial |= values.size() < elements.total;
                row["preview"] = {{"total", elements.total}, {"elements", std::move(values)}};
            } else {
                partial = true;
                row["preview_error"] = captured.elements.error().message;
                if (captured.elements.error().kind == Error::Kind::Timeout) describe = false;
            }
        } else if (field.type == "ArrayProperty") {
            ue::ArrayHeader header{};
            if (format_detail::Load(data, header) && header.count != 0) {
                partial = true;
                row["preview_error"] = i >= arrayLimitAt ? "array preview limit reached" : "array preview unavailable";
            }
        }
        row["truncated"] = partial;
        truncated |= partial;
        rows.push_back(std::move(row));
    }
    // Native members, decoded by their type and filtered like the rest.
    for (const auto& check : native.shown) {
        const auto& field = check.member->field;
        const auto at = [&](const std::vector<uint8_t>& from) {
            return size_t{field.offset} + field.Size() <= from.size() ?
                std::span(from).subspan(field.offset, field.Size()) : std::span<const uint8_t>{};
        };
        const auto data = at(*bytes);
        bool keepRow = command.match.empty() || text::ContainsNoCase(field.name, command.match);
        if (keepRow && command.nonzero) keepRow = std::ranges::any_of(data, [](uint8_t b) { return b != 0; });
        if (keepRow && baseline) {
            const auto old = at(baseline->bytes);
            keepRow = !old.empty() && !std::ranges::equal(old, data);
        }
        if (!keepRow) continue;
        ++shown;
        Json row = NativeFieldJson(*check.member, check.stale);
        row["value"] = data.empty() ? Json(nullptr) : layout::Value(field, data);
        if (baseline) {
            const auto old = at(baseline->bytes);
            row["before"] = old.empty() ? Json(nullptr) : layout::Value(field, old);
        }
        row["truncated"] = data.empty();
        truncated |= data.empty();
        rows.push_back(std::move(row));
    }
    SortRowsByOffset(rows);
    const size_t total = sorted.size() + native.shown.size();
    const bool filtered = !command.match.empty() || command.nonzero || command.changed;
    auto result = DumpJson(object, std::move(rows), truncated, limited,
        filtered ? std::optional(std::pair<size_t, size_t>(shown, total)) : std::nullopt);
    if (!native.refused.empty()) result["native_refused"] = NativeRefusedJson(native.refused);
    return JsonResult(session, "dump", std::move(result));
}
}
