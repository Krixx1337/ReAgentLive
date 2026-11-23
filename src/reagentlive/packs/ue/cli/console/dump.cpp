// reagentlive CLI console: field tables, struct layouts, and get with no paths (every field, or --types).
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
#include "reagentlive/packs/ue/cli/state/cache_ue.h"
#include "reagentlive/frontends/cli/call/build.h"
#include "reagentlive/packs/ue/cli/values/format_ue.h"
#include "reagentlive/packs/ue/cli/values/json_output.h"
#include "reagentlive/frontends/cli/values/layout_json.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/core/client.h"
#include "reagentlive/packs/ue/client_ue.h"

#include "reagentlive/packs/ue/cli/console/internal_ue.h"
#include "reagentlive/packs/ue/cli/console/rows.h"

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

Result<FieldList> ObjectTable(Session& session, const Ref& object) {
    auto& tables = Ue(session).tables;
    const ue::KnownTable known = [&](int32_t type) -> const std::vector<ue::Field>* {
        const auto found = tables.find(type);
        return found == tables.end() ? nullptr : found->second.get();
    };
    auto table = ue::Fields(session.client, object, known);
    if (!table) return std::unexpected(table.error());
    if (table->cached) return tables.at(table->type);
    auto list = std::make_shared<const std::vector<ue::Field>>(std::move(table->fields));
    if (table->type >= 0 && (tables.size() < MaxCachedTables || tables.contains(table->type))) tables[table->type] = list;
    return list;
}

Result<std::vector<ue::Field>> ObjectFields(Session& session, const Ref& object) {
    auto table = ObjectTable(session, object);
    if (!table) return std::unexpected(table.error());
    return **table;
}

namespace {
// One previewed element's parts: an array's element, a set's element, or a
// map's key and value, each at its offset in a slot of `stride` bytes.
struct Part { const char* name; ue::Field field; uint32_t offset; };
struct ElementLayout { uint32_t stride; std::vector<Part> parts; };
ElementLayout ElementParts(const ue::Field& container) {
    if (container.type == "ArrayProperty") return {container.elementSize, {{"value", Element(container), 0}}};
    if (container.type == "SetProperty") return {container.stride, {{"value", MapKey(container), 0}}};
    return {container.stride, {{"key", MapKey(container), 0}, {"value", MapValue(container), container.valueOffset}}};
}
// Occupied slots of a set or map field, or -1 when its header is not a valid one.
int64_t SetCount(std::span<const uint8_t> bytes) {
    ue::SparseHeader header{};
    if (bytes.size() < sizeof(header)) return -1;
    memcpy(&header, bytes.data(), sizeof(header));
    return ue::SparseHeaderValid(header) ? header.num - header.numFree : -1;
}
// A set or map row's value, as an array's: its header (allocation, slots,
// occupied count); the preview lists the elements. Null when the header is
// not a valid one, so the caller keeps the raw bytes.
std::optional<Json> SparseValue(std::span<const uint8_t> bytes) {
    ue::SparseHeader header{};
    if (bytes.size() < sizeof(header)) return std::nullopt;
    memcpy(&header, bytes.data(), sizeof(header));
    if (!ue::SparseHeaderValid(header)) return std::nullopt;
    char address[24];
    snprintf(address, sizeof(address), "0x%llx", static_cast<unsigned long long>(header.data));
    return Json{{"address", header.data ? Json(address) : Json(nullptr)}, {"capacity", header.max},
                {"count", header.num - header.numFree}};
}
}

// --own: the class whose fields are kept: the named class, else the instance's.
Result<std::string> OwnClass(Session& session, const Ref& object, const Command& command) {
    if (!command.className.empty()) return command.className;
    auto described = ue::Describe(session.client, object);
    if (!described) return std::unexpected(described.error());
    return described->className;
}

void PrintTypes(Session& session, const Ref& object, const Command& command, std::string_view jsonCommand) {
    auto fields = ObjectFields(session, object);
    if (!fields) return CommandError(session, jsonCommand, fields.error());
    const auto sorted = SortedByOffset(std::move(*fields));
    std::string ownClass;
    if (command.own) {
        auto own = OwnClass(session, object, command);
        if (!own) return CommandError(session, jsonCommand, own.error());
        ownClass = std::move(*own);
    }
    auto result = MakeTypes(object, sorted, [&](const ue::Field& field) {
        return session.names.Find(field.owner, field.offset, field.size, field.boolMask);
    });
    const auto native = NativeMembers(session, object, sorted, command.className.empty());
    for (const auto& check : native.shown) result.rows.push_back(MakeNativeTypeRow(*check.member, check.stale));
    SortRowsByOffset(result.rows);
    const size_t total = result.rows.size();
    std::erase_if(result.rows, [&](const shape::GetRow& row) {
        if (command.own && !ue::SameWireName(row.owner.value_or(""), ownClass, ue::FindClassBytes)) return true;
        return !command.match.empty() && !text::ContainsNoCase(row.name.value_or(""), command.match) &&
            !text::ContainsNoCase(row.raw.value_or(""), command.match);
    });
    if (!command.match.empty() || command.own) result.filtered = shape::Filtered{result.rows.size(), total};
    if (!native.refused.empty()) result.nativeRefused = MakeNativeRefused(native.refused);
    return Emit(session, jsonCommand, std::move(result));
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

// A container field's first `count` elements, read as pages of entries (an
// agent without entries: an array's 8-element preview).
Result<ue::ArrayPreview> PreviewElements(Client& client, const Ref& object, const ue::Field& field, ue::ContainerKind kind,
                                         uint32_t stride, uint32_t count) {
    if (!client.Supports(ue::op::Entries)) {
        if (kind != ue::ContainerKind::Array || !client.Supports(ue::op::PreviewArray))
            return std::unexpected(Error{Error::Kind::Agent, Status::Refused, 0, {}, "agent cannot read container elements"});
        return ue::PreviewArray(client, object, field);
    }
    ue::ArrayPreview out{};
    for (uint32_t first = 0;;) {
        auto page = ue::Entries(client, object, field.offset, kind, stride, first);
        if (!page) return std::unexpected(page.error());
        out.total = page->total;
        out.bytes.insert(out.bytes.end(), page->bytes.begin(), page->bytes.end());
        if (out.bytes.size() / stride >= count || !page->next || page->bytes.empty()) break;
        first = page->next;
    }
    out.bytes.resize(std::min<size_t>(out.bytes.size() / stride, count) * stride);
    return out;
}

// Fields plus their current values, every value through the shared reader.
// Filters (--match, --nonzero, --changed, --own) drop rows before any lookup for them.
void PrintDump(Session& session, const Ref& object, std::vector<Ref>& refs, StructCache& structs, const Command& command,
               const std::optional<Snapshot>& sessionBaseline) {
    Client& client = session.client;
    constexpr size_t MaxArrays = 16;
    std::optional<Snapshot> loaded;
    const Snapshot* baseline = nullptr;
    if (command.changed) {
        if (!command.file.empty() && !(loaded = LoadSnapshot(command.file)))
            return JsonError(session, "get", "snapshot", "cannot read snapshot file " + command.file);
        baseline = loaded ? &*loaded : sessionBaseline ? &*sessionBaseline : nullptr;
        if (!baseline) return JsonError(session, "get", "snapshot", "changed needs get with snap first (or a snapshot file)");
        if (baseline->object != object) return JsonError(session, "get", "snapshot", "the snapshot is of another object ref");
    }
    auto fields = ObjectFields(session, object);
    if (!fields) return CommandError(session, "get", fields.error());
    const auto sorted = SortedByOffset(std::move(*fields));
    if (baseline && sorted != baseline->fields)
        return JsonError(session, "get", "snapshot", "reflected field layout changed; take a new snapshot");
    // --own: the class that declares the kept fields; an instance's is looked up.
    std::string ownClass;
    if (command.own) {
        auto own = OwnClass(session, object, command);
        if (!own) return CommandError(session, "get", own.error());
        ownClass = std::move(*own);
    }
    // Native members past the last reflected field widen the read.
    const auto native = NativeMembers(session, object, sorted, command.className.empty());
    size_t end = DumpEnd(sorted);
    for (const auto& check : native.shown)
        end = std::max(end, std::min<size_t>(size_t{check.member->field.offset} + check.member->field.Size(), MaxDumpBytes));
    auto bytes = ReadObjectBytes(client, object, end);
    if (!bytes) return CommandError(session, "get", bytes.error());
    std::vector<bool> keep(sorted.size(), true);
    size_t shown = 0;
    for (size_t i = 0; i < sorted.size(); ++i) {
        const auto& f = sorted[i];
        const auto data = FieldBytes(*bytes, f);
        if (command.own) keep[i] = ue::SameWireName(f.owner, ownClass, ue::FindClassBytes); // An instance's class name is cut.
        if (!command.match.empty() && keep[i])
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

    // Top-level arrays, sets and maps: their first elements (--elements, else
    // the preview's 8), read once and printed beside the header.
    const uint32_t previewCount = command.elements ? command.elements : RowReader::PreviewNested;
    struct Preview {
        size_t fieldIndex;
        Result<ue::ArrayPreview> elements;
    };
    std::vector<Preview> previews;
    size_t arrayLimitAt = sorted.size();
    for (size_t i = 0; i < sorted.size(); ++i) {
        const auto& f = sorted[i];
        if (!keep[i]) continue;
        const auto fieldBytes = FieldBytes(*bytes, f);
        ue::ContainerKind kind = ue::ContainerKind::Array;
        uint32_t stride = f.elementSize;
        if (IsContainer(f.type)) {
            if (!f.stride || SetCount(fieldBytes) <= 0) continue;
            kind = f.type == "MapProperty" ? ue::ContainerKind::Map : ue::ContainerKind::Set;
            stride = f.stride;
        } else {
            ue::ArrayHeader header{};
            if (f.type != "ArrayProperty" || fieldBytes.size() != sizeof(header) || f.elementType.empty() ||
                !format_detail::Load(fieldBytes, header) || !ue::ArrayPreviewBytes(header, f.elementSize)) continue;
        }
        if (previews.size() == MaxArrays) {
            arrayLimitAt = i;
            break;
        }
        auto elements = PreviewElements(client, object, f, kind, stride, previewCount);
        const bool timedOut = !elements && elements.error().kind == Error::Kind::Timeout;
        previews.push_back({i, std::move(elements)});
        if (timedOut) break;
    }

    // Two readers: `live` for field values, container headers and preview
    // elements, `held` for `before`, a copy: nothing is read through it.
    // Both collect, resolve, then print.
    RowReader live(session, structs, previewCount);
    RowReader held(session, structs, previewCount);
    for (RowReader* reader : {&live, &held}) {
        reader->KeepTopPointers(); // A pointer field is named on its row (`object`).
        reader->Meta(session.meta);
    }
    held.Snapshot();
    const auto header = [](const ue::Field& field) { return field.type == "ArrayProperty" || IsContainer(field.type); };
    const auto value = [&](const ue::Field& field, std::span<const uint8_t> data, bool& partial) {
        return header(field) ? live.Header(field, data, partial) : live.Value(field, data, partial);
    };
    const auto parts = [&](const Preview& preview, auto&& visit) {
        if (!preview.elements) return;
        const auto layout = ElementParts(sorted[preview.fieldIndex]);
        const auto data = std::span(preview.elements->bytes);
        for (size_t offset = 0; offset + layout.stride <= data.size(); offset += layout.stride)
            for (const auto& part : layout.parts) visit(offset, part, data.subspan(offset + part.offset, part.field.size));
    };
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (!keep[i]) continue;
        bool ignored = false;
        const auto data = FieldBytes(*bytes, sorted[i]);
        value(sorted[i], data, ignored);
        live.Named(sorted[i].type, data);
        if (baseline) held.Value(sorted[i], FieldBytes(baseline->bytes, sorted[i]), ignored);
    }
    for (const auto& preview : previews)
        parts(preview, [&](size_t, const Part& part, std::span<const uint8_t> element) {
            bool ignored = false;
            live.Value(part.field, element, ignored);
            live.Named(part.field.type, element);
        });
    live.Resolve();
    held.Resolve();

    const auto modules = LoadedModules(session);
    // A compact row's name: the readable one where it was recovered, the reflected one beside it as raw.
    const auto placed = [&](shape::GetRow& row, const ue::Field& field) {
        const auto readable = session.names.Find(field.owner, field.offset, field.size, field.boolMask);
        row.name = readable.empty() ? field.name : std::string(readable);
        if (!readable.empty() && readable != field.name) row.raw = field.name;
        row.offset = field.offset;
        if (session.meta) row.meta = MakeFieldInfo(field);
    };
    const auto named = [&](const ue::Object* found) -> std::optional<shape::Object> {
        if (!found) return std::nullopt;
        RememberRef(refs, found->ref);
        return MakeObject(*found);
    };
    shape::Get result;
    result.ref = ReusableRef(object);
    result.capped = sorted.size() >= ue::MaxFieldStart;
    bool truncated = false;
    auto preview = previews.begin();
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (!keep[i]) continue;
        const auto& field = sorted[i];
        const auto data = FieldBytes(*bytes, field);
        bool partial = false;
        shape::GetRow row;
        placed(row, field);
        const auto sparse = !session.meta && IsContainer(field.type) ? SparseValue(data) : std::nullopt;
        row.value = sparse ? *sparse : value(field, data, partial);
        if (baseline) row.before = held.Value(field, FieldBytes(baseline->bytes, field), partial);
        row.valueWhere = Locate(PointerRef(field.type, data).address, modules);
        row.object = named(live.Named(field.type, data));
        if (preview != previews.end() && preview->fieldIndex == i) {
            const auto& captured = *preview++;
            if (captured.elements) {
                const auto layout = ElementParts(field);
                const bool map = layout.parts.size() == 2;
                shape::Preview page;
                page.total = captured.elements->total;
                const auto elementBytes = std::span(captured.elements->bytes);
                for (size_t offset = 0; offset + layout.stride <= elementBytes.size(); offset += layout.stride) {
                    shape::PreviewElement item;
                    for (const auto& part : layout.parts) {
                        const auto element = elementBytes.subspan(offset + part.offset, part.field.size);
                        const bool key = std::string_view(part.name) == "key";
                        auto decoded = live.Value(part.field, element, partial);
                        if (key) item.key = std::move(decoded);
                        else item.value = std::move(decoded);
                        // A top-level pointer element is named beside it (`object`), as a pointer field is.
                        (map ? (key ? item.keyObject : item.valueObject) : item.object) = named(live.Named(part.field.type, element));
                    }
                    page.elements.push_back(std::move(item));
                }
                partial |= page.elements.size() < page.total;
                row.preview = std::move(page);
            } else {
                partial = true;
                row.previewError = captured.elements.error().message;
            }
        } else if (field.type == "ArrayProperty") {
            ue::ArrayHeader arrayHeader{};
            if (format_detail::Load(data, arrayHeader) && arrayHeader.count != 0) {
                partial = true;
                row.previewError = i >= arrayLimitAt ? "array preview limit reached" : "array preview unavailable";
            }
        }
        if (partial) row.truncated = true;
        truncated |= partial;
        result.rows.push_back(std::move(row));
    }
    // Native members, decoded by their type and filtered like the rest.
    for (const auto& check : native.shown) {
        const auto& field = check.member->field;
        const auto at = [&](const std::vector<uint8_t>& from) {
            return size_t{field.offset} + field.Size() <= from.size() ?
                std::span(from).subspan(field.offset, field.Size()) : std::span<const uint8_t>{};
        };
        const auto data = at(*bytes);
        bool keepRow = (!command.own || ue::SameWireName(check.member->owner, ownClass, ue::FindClassBytes)) &&
            (command.match.empty() || text::ContainsNoCase(field.name, command.match));
        if (keepRow && command.nonzero) keepRow = std::ranges::any_of(data, [](uint8_t b) { return b != 0; });
        if (keepRow && baseline) {
            const auto old = at(baseline->bytes);
            keepRow = !old.empty() && !std::ranges::equal(old, data);
        }
        if (!keepRow) continue;
        ++shown;
        shape::GetRow row;
        row.name = field.name;
        row.offset = field.offset;
        if (session.meta) row.meta = MakeFieldInfo(*check.member);
        row.native = true;
        if (check.stale) row.stale = true;
        row.value = data.empty() ? Json(nullptr) : layout::Value(field, data);
        if (baseline) {
            const auto old = at(baseline->bytes);
            row.before = old.empty() ? Json(nullptr) : layout::Value(field, old);
        }
        if (data.empty()) row.truncated = true;
        truncated |= data.empty();
        result.rows.push_back(std::move(row));
    }
    SortRowsByOffset(result.rows);
    // --nonzero also leaves out the zero members inside the values it keeps, as get and each do.
    if (command.nonzero)
        for (auto& row : result.rows)
            if (row.value) KeepNonZero(*row.value);
    const size_t total = sorted.size() + native.shown.size();
    result.truncated = truncated;
    if (const uint32_t unnamed = live.Unnamed() + held.Unnamed()) result.unnamed = unnamed;
    if (!command.match.empty() || command.nonzero || command.changed || command.own) result.filtered = shape::Filtered{shown, total};
    if (!native.refused.empty()) result.nativeRefused = MakeNativeRefused(native.refused);
    if (!TickAccurate(session)) result.tickAccurate = false;
    return Emit(session, "get", std::move(result));
}
}
