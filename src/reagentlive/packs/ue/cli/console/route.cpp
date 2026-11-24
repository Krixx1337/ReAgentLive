// reagentlive CLI console, Unreal pack: owner and route, where objects live.
// The agent finds the objects (owner's table scan, route's walk); the
// console names every slot through the field tables.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/core/client.h"
#include "reagentlive/packs/ue/client_ue.h"
#include "reagentlive/packs/ue/cli/console/internal_ue.h"

namespace reagentlive::cli::detail {
namespace {
constexpr unsigned MaxSlotDepth = 8;

bool ObjectPointer(std::string_view type) {
    return type == "ObjectProperty" || type == "ObjectPtrProperty" || type == "InterfaceProperty" ||
        type == "ClassProperty" || type == "WeakObjectProperty";
}

// A slot named: the path by readable names and by reflected ones, the
// innermost field and where it starts in the object.
struct Slot {
    std::string path, raw, type;
    const ue::Field* field = nullptr;
    uint32_t start = 0;
};

void Append(Slot& slot, std::string_view readable, std::string_view reflected) {
    slot.path += (slot.path.empty() ? "" : ".") + std::string(readable);
    slot.raw += (slot.raw.empty() ? "" : ".") + std::string(reflected);
}

// The field covering `offset` among `fields`, into structs; a fixed array
// of pointers gets its element's index. False when no field covers it.
bool NameSlot(Session& session, std::span<const ue::Field> fields, uint32_t offset, Slot& slot, unsigned depth = 0) {
    for (const auto& field : fields) {
        if (offset < field.offset || offset - field.offset >= std::max<uint32_t>(field.size, 1)) continue;
        const uint32_t within = offset - field.offset;
        const auto readable = session.names.Find(field.owner, field.offset, field.size, field.boolMask);
        Append(slot, readable.empty() ? std::string_view(field.name) : readable, field.name);
        slot.start += field.offset;
        slot.field = &field;
        slot.type = field.type;
        if (field.type == "StructProperty" && field.structType.address && depth < MaxSlotDepth)
            if (const auto* members = Ue(session).structs(field.structType))
                if (Slot inner = slot; NameSlot(session, *members, within, inner, depth + 1)) {
                    slot = std::move(inner);
                    return true;
                }
        const uint32_t element = field.type == "InterfaceProperty" ? 16 : 8;
        if (ObjectPointer(field.type) && field.size > element) {
            const auto index = "[" + std::to_string(within / element) + "]";
            slot.path += index;
            slot.raw += index;
        }
        return true;
    }
    return false;
}

// The step into element `index` of the container at `offset` in `holder`:
// an array's [index]; a set's or map's [Key] when the key is a name or an
// object (entries lists them by key), else [slot N] (a sparse slot, free
// slots counted, so not entries' row number).
std::string ElementStep(Session& session, const Ref& holder, const ue::Field& container, uint32_t offset, uint32_t index) {
    if (container.type == "ArrayProperty") return "[" + std::to_string(index) + "]";
    const std::string slot = "[slot " + std::to_string(index) + "]";
    const auto kind = container.type == "MapProperty" ? ue::ContainerKind::Map : ue::ContainerKind::Set;
    if (!container.stride || container.stride > ue::MaxArrayElementBytes) return slot;
    auto page = ue::Entries(session.client, holder, offset, kind, container.stride, index);
    if (!page || page->bytes.size() < 8) return slot;
    if (container.elementType == "NameProperty") {
        int32_t name = 0;
        memcpy(&name, page->bytes.data(), sizeof(name));
        auto names = ue::Names(session.client, std::span(&name, 1));
        if (names && !names->front().empty()) return "[" + names->front() + "]";
    } else if (container.elementType == "ObjectProperty" || container.elementType == "ObjectPtrProperty") {
        uint64_t key = 0;
        memcpy(&key, page->bytes.data(), sizeof(key));
        if (auto object = ue::Describe(session.client, ue::AddressRef(key)); object) return "[" + object->name + "]";
    }
    return slot;
}

// `slot` continued into a container's element (`at`, ElementStep), then the
// member at `inner` in the element (a map's value past its value offset).
void NameElement(Session& session, Slot& slot, const std::string& at, uint32_t inner) {
    const ue::Field& container = *slot.field;
    slot.path += at;
    slot.raw += at;
    const bool map = container.type == "MapProperty";
    const Ref element = map ? container.valueStructType : container.structType;
    slot.type = map ? container.valueType : container.elementType;
    if (map && inner < container.valueOffset) return; // Keys are not followed.
    const uint32_t member = map ? inner - container.valueOffset : inner;
    if (!element.address) return;
    if (const auto* members = Ue(session).structs(element)) {
        Slot named;
        if (NameSlot(session, *members, member, named)) {
            slot.path += "." + named.path;
            slot.raw += "." + named.raw;
            slot.type = named.type;
        }
    }
}

// The path in `object`'s class to the pointer one hop reads.
std::string HopField(Session& session, const ue::RouteHop& hop) {
    if (hop.kind == static_cast<uint32_t>(ue::HopKind::Outer)) return "Outer";
    auto fields = ObjectTable(session, hop.holder);
    Slot slot;
    if (!fields || !NameSlot(session, **fields, hop.offset, slot)) {
        char text[16];
        snprintf(text, sizeof(text), "+0x%x", hop.offset);
        return text;
    }
    if (hop.kind != static_cast<uint32_t>(ue::HopKind::Field)) {
        const auto& type = slot.field->type;
        if (type != "ArrayProperty" && type != "SetProperty" && type != "MapProperty") {
            char text[16];
            snprintf(text, sizeof(text), "+0x%x", hop.offset);
            return text;
        }
        NameElement(session, slot, ElementStep(session, hop.holder, *slot.field, hop.offset, hop.index), hop.inner);
    }
    return slot.path;
}

// Container fields at the top of a class: the address's element, if any is in one.
bool InContainer(Session& session, const Ref& object, uint64_t address, Slot& out) {
    auto fields = ObjectTable(session, object);
    if (!fields) return false;
    for (const auto& field : **fields) {
        const bool array = field.type == "ArrayProperty", sparse = field.type == "SetProperty" || field.type == "MapProperty";
        const uint32_t stride = array ? field.elementSize : field.stride;
        if ((!array && !sparse) || !stride) continue;
        const uint32_t header = array ? sizeof(ue::ArrayHeader) : sizeof(ue::SparseHeader);
        auto bytes = ue::ReadObject(session.client, object, field.offset, header);
        if (!bytes || bytes->size() != header) continue;
        uint64_t data = 0;
        int32_t count = 0;
        memcpy(&data, bytes->data(), sizeof(data));
        memcpy(&count, bytes->data() + sizeof(data), sizeof(count));
        if (!data || count <= 0 || address < data || (address - data) / stride >= static_cast<uint64_t>(count)) continue;
        Slot slot;
        const auto readable = session.names.Find(field.owner, field.offset, field.size, field.boolMask);
        Append(slot, readable.empty() ? std::string_view(field.name) : readable, field.name);
        slot.field = &field;
        slot.type = field.type;
        NameElement(session, slot, ElementStep(session, object, field, field.offset, static_cast<uint32_t>((address - data) / stride)),
                    static_cast<uint32_t>((address - data) % stride));
        out = std::move(slot);
        return true;
    }
    return false;
}
}

void RunOwner(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(ue::op::Owner)) return JsonError(session, jsonCommand, ErrorKind::Unsupported, "agent does not support owner");
    session.input.reads = true;
    auto owners = ue::Owners(client, command.addresses);
    if (!owners) return CommandError(session, jsonCommand, owners.error());
    // Every object named once: the owners and the within objects.
    std::vector<Ref> refs;
    for (const auto& entry : *owners)
        if (entry.object.address && std::ranges::find(refs, entry.object) == refs.end()) refs.push_back(entry.object);
    for (const auto& ref : command.within) refs.push_back(ref);
    auto described = ue::DescribeMany(client, refs);
    if (!described) return CommandError(session, jsonCommand, described.error());
    const auto objectOf = [&](const Ref& ref) -> std::optional<ue::Object> {
        for (const auto& object : *described)
            if (object && object->ref.address == ref.address) return object;
        return std::nullopt;
    };
    // Candidates for addresses no object holds: the within objects, then the owners.
    std::vector<Ref> candidates;
    for (const auto& object : *described)
        if (object && std::ranges::find(candidates, object->ref) == candidates.end()) candidates.push_back(object->ref);
    std::ranges::stable_partition(candidates, [&](const Ref& ref) {
        return std::ranges::any_of(command.within, [&](const Ref& w) { return w.address == ref.address; });
    });
    shape::Owner out;
    for (size_t i = 0; i < owners->size(); ++i) {
        const auto& entry = (*owners)[i];
        shape::OwnerRow row;
        row.address = command.addresses[i];
        if (entry.object.address) {
            if (const auto object = objectOf(entry.object)) row.object = MakeObject(*object);
            row.offset = HexNum(entry.offset);
            if (entry.flags & ue::OwnerOuter) {
                row.outer = true;
                row.path = "Outer";
            } else if (auto fields = ObjectTable(session, entry.object); fields) {
                if (Slot slot; NameSlot(session, **fields, entry.offset, slot)) {
                    row.path = slot.path;
                    if (slot.raw != slot.path) row.raw = slot.raw;
                    row.type = slot.type;
                }
            }
        } else {
            for (const auto& candidate : candidates)
                if (Slot slot; InContainer(session, candidate, row.address.value, slot)) {
                    if (const auto object = objectOf(candidate)) row.object = MakeObject(*object);
                    row.container = true;
                    row.path = slot.path;
                    if (slot.raw != slot.path) row.raw = slot.raw;
                    row.type = slot.type;
                    break;
                }
        }
        if (row.object) ++out.owned;
        out.rows.push_back(std::move(row));
    }
    Emit(session, jsonCommand, std::move(out));
}

void RunRoute(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(ue::op::Route)) return JsonError(session, jsonCommand, ErrorKind::Unsupported, "agent does not support route");
    session.input.reads = true;
    ue::RouteSpec spec;
    memcpy(&spec, command.request.data, sizeof(spec));
    if (!spec.sources) { // Every root the host offers.
        auto roots = client.WatchRoots();
        if (!roots) return CommandError(session, jsonCommand, roots.error());
        for (const auto& root : *roots)
            if (spec.sources < ue::MaxRouteSources && !root.empty() && root.size() < ue::MaxRouteRoot)
                CopyText(spec.roots[spec.sources++], root);
        if (!spec.sources)
            return JsonError(session, jsonCommand, ErrorKind::Validation, "sources: the host offers no roots; give objects");
    }
    const Ref target = ue::GetRef(command.request);
    auto found = ue::Route(client, target, spec);
    if (!found) return CommandError(session, jsonCommand, found.error());
    // Names: the target, each route's holders (its source first) and the objects reached.
    std::vector<Ref> refs{target};
    for (const auto& route : found->routes)
        for (const auto& hop : route.hops) refs.push_back(hop.holder);
    auto described = ue::DescribeMany(client, refs);
    if (!described) return CommandError(session, jsonCommand, described.error());
    const auto objectOf = [&](size_t i, const Ref& ref) {
        const auto& object = (*described)[i];
        return object ? MakeObject(*object) : shape::Object{FullRef(ref), {}, {}, {}, {}};
    };
    static constexpr const char* Ends[] = {"found", "depth", "budget", "none"};
    shape::Route out;
    out.object = objectOf(0, target);
    out.end = Ends[found->header.end];
    out.visited = found->header.visited;
    out.depth = found->header.depth;
    size_t at = 1;
    for (const auto& route : found->routes) {
        shape::RouteRow row;
        if (spec.roots[route.source][0]) row.root = spec.roots[route.source];
        row.source = route.hops.empty() ? out.object : objectOf(at, route.hops.front().holder);
        row.path = row.root ? *row.root : row.source.ref.text;
        for (size_t h = 0; h < route.hops.size(); ++h) {
            shape::RouteStep step;
            step.field = HopField(session, route.hops[h]);
            step.object = h + 1 < route.hops.size() ? objectOf(at + h + 1, route.hops[h + 1].holder) : out.object;
            row.path += "." + step.field;
            row.steps.push_back(std::move(step));
        }
        at += route.hops.size();
        out.rows.push_back(std::move(row));
    }
    Emit(session, jsonCommand, std::move(out));
}
}
