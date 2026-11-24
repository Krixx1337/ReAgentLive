// reagentlive Unreal pack: where objects live: owner (the object holding an
// address, from one table scan) and route (the shortest reflected references
// from a source to an object, a breadth-first walk over ticks).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "reagentlive/packs/ue/agent_ue.h"
#include "reagentlive/packs/ue/name_pool.h"
#include "reagentlive/packs/ue/agent_ue_internal.h"

namespace reagentlive::ue {
namespace {
// Owner: the addresses sorted, and per sorted address the nearest object
// at or below it seen so far (its table slot).
struct OwnerState {
    std::vector<uint64_t> sorted;
    std::vector<uint32_t> order;     // sorted[i] is the request's address order[i].
    std::vector<uint64_t> best;
    std::vector<int32_t> slot;
    int32_t cursor = 0;
};
OwnerState* g_owner;

// The owner of sorted address i, or false when the nearest object does not hold it.
bool OwnerOf(size_t i, OwnerEntry& entry, const Context& pack, const Host& host) {
    const auto& state = *g_owner;
    if (!state.best[i]) return false;
    const auto* object = reinterpret_cast<const Object*>(state.best[i]);
    const Ref ref = Identify(object, pack);
    if (!ref.address || ref.index != state.slot[i] || !pack.object(object)) return false;
    const Object* type = ObjectClass(object);
    if (!type || !pack.object(type) || !host.readable(type, StructBytes())) return false;
    const int32_t size = StructSize(type);
    const uint64_t offset = state.sorted[i] - state.best[i];
    if (size <= 0 || offset >= static_cast<uint64_t>(size)) return false;
    entry.object = ref;
    entry.offset = static_cast<uint32_t>(offset);
    entry.size = static_cast<uint32_t>(size);
    if (entry.offset == globals::ObjectOuterOffset(Layout().shape)) entry.flags |= OwnerOuter;
    return true;
}
}

Progress Owner(const Request& request, Response& response, bool first, const Host& host) {
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response) || !LayoutReady(GroupStruct | GroupClass, host, response))
        return Progress::Done;
    if (first) {
        if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
        if (!g_owner) g_owner = new (std::nothrow) OwnerState;
        if (!g_owner) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
        auto& state = *g_owner;
        const size_t count = request.length / sizeof(uint64_t);
        try {
            std::vector<std::pair<uint64_t, uint32_t>> pairs(count);
            for (size_t i = 0; i < count; ++i) {
                memcpy(&pairs[i].first, request.data + i * sizeof(uint64_t), sizeof(uint64_t));
                pairs[i].second = static_cast<uint32_t>(i);
            }
            std::ranges::sort(pairs);
            state.sorted.resize(count);
            state.order.resize(count);
            for (size_t i = 0; i < count; ++i) {
                state.sorted[i] = pairs[i].first;
                state.order[i] = pairs[i].second;
            }
            state.best.assign(count, 0);
            state.slot.assign(count, -1);
        } catch (...) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
        state.cursor = 0;
    }
    auto& state = *g_owner;
    Budget budget;
    ForgetRegions();
    // Only pointer compares per slot: objects are dereferenced once, at the end.
    const int32_t count = TableNum();
    for (unsigned checked = 0; state.cursor < count; ++state.cursor, ++checked) {
        if ((checked & 1023) == 1023 && budget.Spent()) return Progress::Continue;
        const Object* object = TableObject(state.cursor);
        if (!object) continue;
        const auto at = reinterpret_cast<uint64_t>(object);
        Covered(state.sorted.data(), state.sorted.size(), at, [&](size_t i) {
            if (at > state.best[i]) {
                state.best[i] = at;
                state.slot[i] = state.cursor;
            }
        });
    }
    auto* entries = reinterpret_cast<OwnerEntry*>(response.data);
    uint64_t found = 0;
    for (size_t i = 0; i < state.sorted.size(); ++i) {
        OwnerEntry entry{};
        if (OwnerOf(i, entry, pack, host)) ++found;
        else entry = {};
        entries[state.order[i]] = entry;
    }
    response.length = static_cast<uint32_t>(state.sorted.size() * sizeof(OwnerEntry));
    response.detail = found;
    return Progress::Done;
}

namespace {
// A property as route follows it.
enum class Follow : uint8_t { None, Object, Struct, Array, Set, Map };
// Where pointers sit in an object of one class: a field's pointer, or a
// container whose elements hold pointers at `inner` offsets.
struct Edge {
    HopKind kind;
    uint32_t offset, stride = 0;
    std::vector<uint32_t> inner;
};
constexpr size_t MaxEdgesPerType = 4096;
constexpr size_t MaxContainerBytes = size_t{1} << 20; // Copied per container, per visit.

struct RouteNode {
    const Object* object;
    Ref ref;
    int32_t parent;      // -1 for a source.
    RouteHop hop;        // From the parent to this object.
    uint32_t depth, source;
};
struct RouteState {
    RouteSpec spec{};
    Ref target{};
    std::vector<RouteNode> nodes;
    size_t head = 0;
    std::unordered_set<uint64_t> seen;
    std::unordered_map<const Object*, std::vector<Edge>> edges;      // By class.
    std::unordered_map<FieldClassPtr, Follow> kinds;                 // By property class.
    std::vector<std::pair<size_t, RouteHop>> found;                  // The holder's node and the last hop.
    uint32_t foundDepth = 0, deepest = 0;
    bool budgetHit = false, depthHit = false;
    std::vector<uint8_t> scratch;
};
RouteState* g_route;

Follow KindOf(FieldPtr field, const Host& host) {
    const FieldClassPtr type = FieldClass(field);
    auto& kinds = g_route->kinds;
    if (const auto known = kinds.find(type); known != kinds.end()) return known->second;
    Follow kind = Follow::None;
    if (host.readable(type, FieldClassBytes())) {
        const auto name = NameOf(FieldClassName(type));
        if (name == "ObjectProperty" || name == "ObjectPtrProperty" || name == "InterfaceProperty") kind = Follow::Object;
        else if (name == "StructProperty") kind = Follow::Struct;
        else if (name == "ArrayProperty") kind = Follow::Array;
        else if (name == "SetProperty") kind = Follow::Set;
        else if (name == "MapProperty") kind = Follow::Map;
    }
    kinds.emplace(type, kind);
    return kind;
}

void TypeEdges(const Object* type, uint32_t base, unsigned depth, bool containers, std::vector<Edge>& out,
               const Context& pack, const Host& host);

// Pointer offsets inside one element of a container whose element is `property`.
std::vector<uint32_t> ElementPointers(FieldPtr property, unsigned depth, const Context& pack, const Host& host) {
    std::vector<uint32_t> offsets;
    if (!host.readable(property, TypedBytes())) return offsets;
    const Follow kind = KindOf(property, host);
    if (kind == Follow::Object) offsets.push_back(0);
    if (kind == Follow::Struct && depth < MaxRouteStructDepth) {
        std::vector<Edge> members;
        TypeEdges(StructOf(property), 0, depth + 1, false, members, pack, host);
        for (const auto& member : members) offsets.push_back(member.offset);
    }
    return offsets;
}

// A set's or map's slot layout from its property; false when it fails its check.
bool SlotLayout(FieldPtr field, bool map, ContainerLayout& layout, const Host& host) {
    const auto* base = reinterpret_cast<const uint8_t*>(field);
    if (!host.readable(base, static_cast<uint32_t>(map ? Engine().mapLayout : Engine().setLayout) + sizeof(ContainerLayout)))
        return false;
    layout = {};
    if (map) memcpy(&layout, base + Engine().mapLayout, sizeof(layout));
    else memcpy(&layout.hashNextIdOffset, base + Engine().setLayout, 3 * sizeof(int32_t));
    const FieldPtr key = map ? MapKey(field) : SetElement(field);
    if (!host.readable(key, PropertyBytes()) || ElementSize(key) <= 0) return false;
    uint32_t valueSize = 0;
    if (map) {
        const FieldPtr value = MapValue(field);
        if (!host.readable(value, PropertyBytes()) || ElementSize(value) <= 0) return false;
        valueSize = static_cast<uint32_t>(ElementSize(value));
    }
    return ContainerLayoutValid(layout, static_cast<uint32_t>(ElementSize(key)), valueSize);
}

// The pointers of a class or struct chain at `base`: object fields (each
// element of a fixed array), structs flattened, and with `containers`
// arrays, sets and map values whose elements hold pointers.
void TypeEdges(const Object* type, uint32_t base, unsigned depth, bool containers, std::vector<Edge>& out,
               const Context& pack, const Host& host) {
    unsigned chain = 0;
    for (; type && chain < MaxClassDepth && pack.object(type) && host.readable(type, StructBytes());
         type = SuperStruct(type), ++chain) {
        unsigned children = 0;
        for (FieldPtr field = ChildProperties(type); field && children < MaxChildren && out.size() < MaxEdgesPerType &&
             host.readable(field, PropertyBytes()); field = FieldNext(field), ++children) {
            const int32_t offset = PropertyOffset(field), dim = ArrayDim(field), size = ElementSize(field);
            if (offset < 0 || dim <= 0 || size <= 0 || static_cast<uint64_t>(offset) + uint64_t{base} +
                static_cast<uint64_t>(dim) * static_cast<uint64_t>(size) > MaxObjectBytes) continue;
            const uint32_t at = base + static_cast<uint32_t>(offset);
            switch (KindOf(field, host)) {
            case Follow::Object:
                for (int32_t k = 0; k < dim; ++k) out.push_back({HopKind::Field, at + static_cast<uint32_t>(k * size)});
                break;
            case Follow::Struct:
                if (depth < MaxRouteStructDepth && host.readable(field, TypedBytes()))
                    for (int32_t k = 0; k < dim; ++k)
                        TypeEdges(StructOf(field), at + static_cast<uint32_t>(k * size), depth + 1, containers, out, pack, host);
                break;
            case Follow::Array: {
                if (!containers || dim != 1 || !host.readable(field, TypedBytes())) break;
                const FieldPtr inner = ArrayInner(field);
                if (!host.readable(inner, PropertyBytes()) || ElementSize(inner) <= 0) break;
                auto pointers = ElementPointers(inner, depth, pack, host);
                if (!pointers.empty())
                    out.push_back({HopKind::Array, at, static_cast<uint32_t>(ElementSize(inner)), std::move(pointers)});
                break;
            }
            case Follow::Set:
            case Follow::Map: {
                const bool map = KindOf(field, host) == Follow::Map;
                ContainerLayout layout{};
                if (!containers || dim != 1 || !host.readable(field, TypedBytes()) || !SlotLayout(field, map, layout, host))
                    break;
                auto pointers = ElementPointers(map ? MapValue(field) : SetElement(field), depth, pack, host);
                if (map) for (auto& pointer : pointers) pointer += static_cast<uint32_t>(layout.valueOffset);
                if (!pointers.empty())
                    out.push_back({map ? HopKind::Map : HopKind::Set, at, static_cast<uint32_t>(layout.stride), std::move(pointers)});
                break;
            }
            case Follow::None:
                break;
            }
        }
    }
}

const std::vector<Edge>& EdgesOf(const Object* type, const Context& pack, const Host& host) {
    auto& edges = g_route->edges;
    if (const auto known = edges.find(type); known != edges.end()) return known->second;
    std::vector<Edge> list;
    TypeEdges(type, 0, 0, true, list, pack, host);
    return edges.emplace(type, std::move(list)).first->second;
}

// Copies `bytes` from live memory into the scratch buffer; false when unreadable.
bool CopyOut(const uint8_t* from, size_t bytes, const Host& host) {
    auto& scratch = g_route->scratch;
    scratch.resize(bytes);
    return !bytes || (host.readable(from, bytes) && GuardedCopy(scratch.data(), from, bytes));
}

// visit(pointer, hop) for every pointer an edge of `object` holds.
template<class Visit> void EdgePointers(const Object* object, const Ref& holder, const Edge& edge, const Host& host,
                                        Visit&& visit) {
    const auto* base = reinterpret_cast<const uint8_t*>(object) + edge.offset;
    RouteHop hop{holder, static_cast<uint32_t>(edge.kind), edge.offset, 0, 0};
    if (edge.kind == HopKind::Field) {
        uint64_t pointer = 0;
        if (GuardedCopy(&pointer, base, sizeof(pointer))) visit(pointer, hop);
        return;
    }
    const auto elements = [&](const uint8_t* data, uint32_t slots, const auto& used) {
        // Slots past the copied ones are dropped, so `used` sees only those.
        if (!edge.stride) return;
        slots = static_cast<uint32_t>(std::min<size_t>(slots, MaxContainerBytes / edge.stride));
        if (!data || !slots || !CopyOut(data, size_t{slots} * edge.stride, host)) return;
        const auto& bytes = g_route->scratch;
        for (uint32_t slot = 0; slot < slots; ++slot) {
            if (!used(slot)) continue;
            for (const uint32_t inner : edge.inner) {
                if (inner + sizeof(uint64_t) > edge.stride) continue;
                uint64_t pointer = 0;
                memcpy(&pointer, bytes.data() + size_t{slot} * edge.stride + inner, sizeof(pointer));
                hop.index = slot;
                hop.inner = inner;
                visit(pointer, hop);
            }
        }
    };
    if (edge.kind == HopKind::Array) {
        ArrayHeader header{};
        if (!GuardedCopy(&header, base, sizeof(header)) || header.count <= 0 || header.capacity < header.count) return;
        const auto count = std::min(static_cast<uint32_t>(header.count), MaxRouteElements);
        elements(reinterpret_cast<const uint8_t*>(header.data), count, [](uint32_t) { return true; });
        return;
    }
    SparseHeader header{};
    if (!GuardedCopy(&header, base, sizeof(header)) || !SparseHeaderValid(header) || !header.num) return;
    const uint32_t num = std::min(static_cast<uint32_t>(header.num), MaxRouteElements), words = (num + 31) / 32;
    std::vector<uint32_t> bits(header.inlineBits, header.inlineBits + 4);
    if (header.secondaryBits) {
        bits.resize(words);
        if (!host.readable(reinterpret_cast<const void*>(header.secondaryBits), words * sizeof(uint32_t)) ||
            !GuardedCopy(bits.data(), reinterpret_cast<const void*>(header.secondaryBits), words * sizeof(uint32_t))) return;
    }
    elements(reinterpret_cast<const uint8_t*>(header.data), num,
             [&bits](uint32_t slot) { return SlotAllocated(bits.data(), slot); });
}

// Visits one node's pointers; new objects join the walk.
void Expand(size_t index, const Context& pack, const Host& host) {
    auto& state = *g_route;
    const RouteNode node = state.nodes[index];
    const Object* object = Resolve(node.ref, pack);
    if (!object) return; // Freed since it was found.
    const Object* type = ObjectClass(object);
    // A class freed and another allocated at its address would reuse the
    // cached edges: the class must identify, and every edge lie inside the
    // object, readable to its class size.
    if (!type || !pack.object(type) || !Identify(type, pack).address || !host.readable(type, StructBytes())) return;
    const int32_t size = StructSize(type);
    if (size <= 0 || !host.readable(object, static_cast<size_t>(size))) return;
    const auto visit = [&](uint64_t pointer, const RouteHop& hop) {
        if (!pointer || (pointer & 7)) return;
        if (pointer == state.target.address) {
            if (state.found.size() < MaxRoutes) state.found.push_back({index, hop});
            state.foundDepth = node.depth + 1;
            return;
        }
        if (!state.found.empty() || state.seen.contains(pointer)) return;
        if (state.seen.size() >= state.spec.budget) { state.budgetHit = true; return; }
        const auto* next = reinterpret_cast<const Object*>(pointer);
        if (!pack.object(next)) return;
        const Ref ref = Identify(next, pack);
        if (!ref.address) return;
        state.seen.insert(pointer);
        state.nodes.push_back({next, ref, static_cast<int32_t>(index), hop, node.depth + 1, node.source});
        state.deepest = std::max(state.deepest, node.depth + 1);
    };
    for (const auto& edge : EdgesOf(type, pack, host)) {
        const uint32_t span = edge.kind == HopKind::Field ? sizeof(uint64_t) :
            edge.kind == HopKind::Array ? sizeof(ArrayHeader) : sizeof(SparseHeader);
        if (edge.offset + span <= static_cast<uint32_t>(size)) EdgePointers(object, node.ref, edge, host, visit);
    }
    if (state.spec.flags & RouteOuter)
        visit(reinterpret_cast<uint64_t>(ObjectOuter(object)),
              {node.ref, static_cast<uint32_t>(HopKind::Outer), globals::ObjectOuterOffset(Layout().shape), 0, 0});
}

// The sources as nodes; false when none resolves.
bool StartRoute(const Host& host, const Context& pack) {
    auto& state = *g_route;
    for (uint32_t i = 0; i < state.spec.sources; ++i) {
        Ref ref = state.spec.from[i];
        if (state.spec.roots[i][0]) {
            uint64_t address = 0;
            Guard guard{};
            if (!host.root || !host.root(host, state.spec.roots[i], address, guard)) continue;
            ref = AddressRef(address);
        }
        if (ref.index < 0) ref = Identify(reinterpret_cast<const Object*>(ref.address), pack);
        const Object* object = Resolve(ref, pack);
        if (!object || state.seen.contains(ref.address)) continue;
        state.seen.insert(ref.address);
        state.nodes.push_back({object, ref, -1, {}, 0, i});
    }
    return !state.nodes.empty();
}

void RouteAnswer(Response& response) {
    const auto& state = *g_route;
    RouteHeader header{};
    header.visited = static_cast<uint32_t>(state.seen.size());
    header.routes = static_cast<uint32_t>(state.found.size());
    header.depth = header.routes ? state.foundDepth : state.deepest;
    header.end = static_cast<uint32_t>(header.routes ? RouteEnd::Found : state.budgetHit ? RouteEnd::Budget :
                                       state.depthHit ? RouteEnd::Depth : RouteEnd::None);
    size_t at = sizeof(header);
    for (const auto& [holder, last] : state.found) {
        RouteHop hops[MaxRouteDepth];
        uint32_t count = 0;
        hops[count++] = last;
        size_t node = holder;
        for (; state.nodes[node].parent >= 0 && count < MaxRouteDepth; node = static_cast<size_t>(state.nodes[node].parent))
            hops[count++] = state.nodes[node].hop;
        std::reverse(hops, hops + count);
        const RoutePath path{state.nodes[node].source, count};
        memcpy(response.data + at, &path, sizeof(path));
        at += sizeof(path);
        memcpy(response.data + at, hops, count * sizeof(RouteHop));
        at += count * sizeof(RouteHop);
    }
    memcpy(response.data, &header, sizeof(header));
    response.length = static_cast<uint32_t>(at);
    response.detail = header.routes;
}
}

Progress Route(const Request& request, Response& response, bool first, const Host& host) {
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response) || !LayoutReady(GroupsFields, host, response)) return Progress::Done;
    if (first) {
        if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
        if (!g_route) g_route = new (std::nothrow) RouteState;
        if (!g_route) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
        auto& state = *g_route;
        state.nodes.clear();
        state.seen.clear();
        state.found.clear();
        state.edges.clear();     // Classes may have been freed and their addresses reused since.
        state.kinds.clear();
        state.scratch.clear();
        state.scratch.shrink_to_fit();
        state.head = 0;
        state.foundDepth = state.deepest = 0;
        state.budgetHit = state.depthHit = false;
        memcpy(&state.spec, request.data, sizeof(state.spec));
        const Object* target = ResolveRequestObject(request, pack);
        if (!target) { Refuse(response, Status::NotFound, "object gone or not a valid object"); return Progress::Done; }
        state.target = Identify(target, pack);
        try {
            if (!StartRoute(host, pack)) {
                Refuse(response, Status::NotFound, "no source resolves (a gone object, or a root the host has not now)");
                return Progress::Done;
            }
        } catch (...) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
        // A source that is the target: a route of no hops.
        for (const auto& node : state.nodes)
            if (node.ref.address == state.target.address) {
                RouteHeader header{1, static_cast<uint32_t>(state.seen.size()), 0, static_cast<uint32_t>(RouteEnd::Found)};
                const RoutePath path{node.source, 0};
                memcpy(response.data, &header, sizeof(header));
                memcpy(response.data + sizeof(header), &path, sizeof(path));
                response.length = sizeof(header) + sizeof(path);
                response.detail = 1;
                return Progress::Done;
            }
    }
    auto& state = *g_route;
    Budget budget;
    ForgetRegions();
    try {
        for (unsigned checked = 0; state.head < state.nodes.size(); ++state.head, ++checked) {
            if (checked && budget.Spent()) return Progress::Continue; // A node can hold large arrays.
            const uint32_t depth = state.nodes[state.head].depth;
            // Breadth-first: once a route is found, nodes past its holders' level only make longer ones.
            if (!state.found.empty() && depth + 1 > state.foundDepth) break;
            if (depth >= state.spec.depth) { state.depthHit = true; continue; }
            Expand(state.head, pack, host);
        }
    } catch (...) {
        state.nodes.clear();
        Refuse(response, Status::Refused, "out of memory");
        return Progress::Done;
    }
    if (!Resolve(state.target, pack)) {
        Refuse(response, Status::NotFound, "the object was freed during the walk");
        return Progress::Done;
    }
    RouteAnswer(response);
    return Progress::Done;
}
}
