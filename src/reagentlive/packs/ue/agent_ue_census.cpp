// reagentlive Unreal pack: the cached table and pool scans that need no SDK:
// census and name-find (reagentlive::ue-base).
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
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "reagentlive/packs/ue/agent_ue.h"
#include "reagentlive/packs/ue/name_pool.h"
#include "reagentlive/packs/ue/agent_ue_base.h"

namespace reagentlive::ue {
namespace {
// Census: instances per exact class from one table scan, kept for paging.
struct CensusRow {
    const Object* type;
    uint32_t count, samples;
    Ref sample[MaxCensusSamples];
    Ref ref;
    std::string name;
};
struct CensusState {
    std::string filter;
    std::vector<CensusRow> rows;
    std::unordered_map<const void*, uint32_t> slots;
    int32_t cursor = 0;
    bool complete = false;
};
CensusState* g_census;

void CensusPage(uint32_t from, Response& response) {
    auto* entries = reinterpret_cast<CensusEntry*>(response.data);
    const auto& rows = g_census->rows;
    uint32_t count = 0, i = from;
    for (; i < rows.size() && count < MaxCensusPerPage; ++i) {
        CensusEntry& entry = entries[count++];
        entry = {};
        entry.type = rows[i].ref;
        entry.count = rows[i].count;
        entry.samples = rows[i].samples;
        memcpy(entry.sample, rows[i].sample, sizeof(entry.sample));
        CopyText(entry.name, rows[i].name);
    }
    response.length = count * static_cast<uint32_t>(sizeof(CensusEntry));
    response.detail = FindDetail(static_cast<uint32_t>(rows.size()), i < rows.size() ? i : 0);
}
}

Progress CensusBody(const Request& request, Response& response, bool first, const Host& host) {
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response, Access::Read)) return Progress::Done;
    const std::string_view wanted = ClassArgument(request);
    // A page of the stored census answers only the same filter: class, outer text and outer object.
    std::string filter;
    try {
        filter.assign(wanted);
        filter.push_back('\0');
        filter.append(detail::FilterOuter(request));
        filter.push_back('\0');
        filter.append(std::to_string(request.address));
    } catch (...) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
    if (first) {
        if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
        if (!OuterAlive(request, pack, response)) return Progress::Done;
        if (!g_census) g_census = new (std::nothrow) CensusState;
        if (!g_census) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
        if (request.args[1] && g_census->complete && g_census->filter == filter) {
            CensusPage(static_cast<uint32_t>(request.args[1]), response);
            return Progress::Done;
        }
        try {
            g_census->filter = filter;
            g_census->rows.clear();
            g_census->slots.clear();
        } catch (...) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
        g_census->cursor = 0;
        g_census->complete = false;
        ResetClassCache();
    }
    auto& state = *g_census;
    Budget budget;
    const int32_t count = TableNum();
    g_region = {};
    std::string scratch;
    try {
        for (unsigned checked = 0; state.cursor < count; ++state.cursor, ++checked) {
            if ((checked & 63) == 63 && budget.Spent()) return Progress::Continue;
            Object* object = TableObject(state.cursor);
            if (!object || !HeaderReadable(object) || ObjectIndex(object) != state.cursor || Template(object)) continue;
            if (!wanted.empty() && !ClassMatches(object, wanted, scratch)) continue;
            if (!OuterMatches(request, object, scratch)) continue;
            const Object* type = ObjectClass(object);
            if (!type) continue;
            const auto [slot, added] = state.slots.try_emplace(type, static_cast<uint32_t>(state.rows.size()));
            if (added) state.rows.push_back({type, 0, 0, {}, {}, {}});
            CensusRow& row = state.rows[slot->second];
            ++row.count;
            if (row.samples < MaxCensusSamples) {
                if (const Ref ref = Identify(object, pack); ref.address) row.sample[row.samples++] = ref;
            }
        }
        std::string name;
        for (auto& row : state.rows) {
            row.ref = Identify(row.type, pack);
            row.name = row.ref.address ? std::string(FullName(ObjectName(row.type), name)) : "?";
        }
        std::sort(state.rows.begin(), state.rows.end(), [](const CensusRow& a, const CensusRow& b) {
            return a.count != b.count ? a.count > b.count : a.name < b.name;
        });
    } catch (...) {
        state.rows.clear();
        Refuse(response, Status::Refused, "out of memory");
        return Progress::Done;
    }
    state.complete = true;
    CensusPage(static_cast<uint32_t>(request.args[1]), response);
    return Progress::Done;
}

namespace {
struct NameScan { uint32_t block, offset, used, matches; };
NameScan g_nameScan;

struct Pool {
    names::PoolHeader header;
    const uint8_t* const* blocks;
};

bool ReadPool(uint64_t address, const Host& host, Pool& out) {
    const auto* pool = reinterpret_cast<const uint8_t*>(address);
    if (!address || !host.readable(pool, names::BlocksOffset) || !GuardedCopy(&out.header, pool, sizeof(out.header))) return false;
    if (out.header.currentBlock >= names::MaxBlocks ||
        !host.readable(pool + names::BlocksOffset, (out.header.currentBlock + 1) * sizeof(void*))) return false;
    out.blocks = reinterpret_cast<const uint8_t* const*>(pool + names::BlocksOffset);
    return true;
}

// A block's used bytes, readable, or an empty span.
std::span<const uint8_t> BlockBytes(const Pool& pool, uint32_t block, const names::Layout& layout, const Host& host) {
    const uint64_t size = block == pool.header.currentBlock ? pool.header.currentByteCursor : names::BlockBytes(layout);
    const uint8_t* data = pool.blocks[block];
    if (!data || !size || size > names::BlockBytes(layout) || !host.readable(data, static_cast<size_t>(size))) return {};
    return {data, static_cast<size_t>(size)};
}
}

void NameStarts(const int32_t* indices, uint32_t count, bool* starts, const Host& host) {
    for (uint32_t i = 0; i < count; ++i) starts[i] = false;
    const names::Layout* located = nullptr;
    Pool pool{};
    if (count > MaxNames || !ReadPool(NamePool(located), host, pool)) return;
    const auto& layout = *located;
    // In index order, one walk per block from its first entry.
    uint8_t order[MaxNames];
    for (uint32_t i = 0; i < count; ++i) order[i] = static_cast<uint8_t>(i);
    std::sort(order, order + count, [&](uint8_t a, uint8_t b) { return indices[a] < indices[b]; });
    uint32_t walked = UINT32_MAX;
    std::span<const uint8_t> block;
    uint64_t cursor = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const int32_t index = indices[order[i]];
        if (index < 0 || names::Block(index) > pool.header.currentBlock) continue;
        if (names::Block(index) != walked) {
            walked = names::Block(index);
            block = BlockBytes(pool, walked, layout, host);
            cursor = 0;
        }
        const uint32_t target = names::Offset(index, layout);
        if (cursor < block.size()) cursor = names::EntryAtOrAfter(block, static_cast<uint32_t>(cursor), target, layout);
        starts[order[i]] = cursor == target && cursor < block.size();
    }
}

Progress NameFindBody(const Request& request, Response& response, bool first, const Host& host) {
    if (first) {
        if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
        const Context& pack = Pack(host);
        if (!PackReady(pack, host, response, Access::Read)) return Progress::Done;
    }
    // PackReady located the pool and its layout on the first page.
    const names::Layout* located = nullptr;
    Pool pool{};
    if (!ReadPool(NamePool(located), host, pool)) { Refuse(response, Status::NotReadable, "name pool not readable"); return Progress::Done; }
    const auto& layout = *located;
    if (first) {
        const auto cursor = static_cast<int32_t>(request.args[1]);
        g_nameScan = {names::Block(cursor), names::Offset(cursor, layout), 0, 0};
    }
    auto& scan = g_nameScan;
    const std::string_view query(request.text);
    Budget budget;
    char text[MaxNameFindText + 1];
    for (unsigned checked = 0; scan.block <= pool.header.currentBlock; ++scan.block, scan.offset = 0) {
        const auto block = BlockBytes(pool, scan.block, layout, host);
        names::Entry entry{};
        while (names::EntryAt(block, scan.offset, layout, entry)) {
            if ((++checked & 255) == 0 && budget.Spent()) return Progress::Continue;
            const size_t length = names::Text(block, entry, layout, text, sizeof(text));
            if (NameMatches({text, length}, query)) {
                const int32_t index = names::Index(scan.block, scan.offset, layout);
                if (scan.matches == MaxNameFindResults || scan.used + sizeof(index) + length + 1 > MaxRead) {
                    response.length = scan.used;
                    response.detail = static_cast<uint32_t>(index);
                    return Progress::Done;
                }
                memcpy(response.data + scan.used, &index, sizeof(index));
                memcpy(response.data + scan.used + sizeof(index), text, length + 1);
                scan.used += static_cast<uint32_t>(sizeof(index) + length + 1);
                ++scan.matches;
            }
            const uint64_t next = names::NextOffset(entry, layout);
            if (next >= block.size()) break;
            scan.offset = static_cast<uint32_t>(next);
        }
    }
    response.length = scan.used;
    response.detail = 0;
    return Progress::Done;
}

// Off the engine's thread (Context::offThreadReads) these run under a fault guard.
Progress Census(const Request& request, Response& response, bool first, const Host& host) {
    return GuardedRead(&CensusBody, request, response, first, host);
}
Progress NameFind(const Request& request, Response& response, bool first, const Host& host) {
    return GuardedRead(&NameFindBody, request, response, first, host);
}
}
