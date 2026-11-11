// reagentlive Unreal pack: table and pool scans with cached pages: census, name-find and each.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
#include <algorithm>
#include <charconv>
#include <cstring>
#include <iterator>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "reagentlive/ue/agent_ue.h"
#include "reagentlive/ue/name_pool.h"
#include "SDK/CoreUObject_classes.hpp"
#include "reagentlive/ue/agent_ue_internal.h"

namespace reagentlive::ue {
namespace {
// Census: instances per exact class from one table scan, kept for paging.
struct CensusRow {
    const SDK::UStruct* type;
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

Progress Census(const Request& request, Response& response, bool first, const Host& host) {
    const Context& pack = Pack(host);
    if (!OnEngineThread(pack, response)) return Progress::Done;
    const std::string_view wanted = ClassArgument(request);
    if (first) {
        if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
        if (!g_census) g_census = new (std::nothrow) CensusState;
        if (!g_census) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
        if (request.args[1] && g_census->complete && g_census->filter == wanted) {
            CensusPage(static_cast<uint32_t>(request.args[1]), response);
            return Progress::Done;
        }
        try {
            g_census->filter.assign(wanted);
            g_census->rows.clear();
            g_census->slots.clear();
        } catch (...) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
        g_census->cursor = 0;
        g_census->complete = false;
        ResetClassCache();
    }
    auto& state = *g_census;
    Budget budget;
    const int32_t count = SDK::UObject::GObjects->Num();
    g_region = {};
    std::string scratch;
    try {
        for (unsigned checked = 0; state.cursor < count; ++state.cursor, ++checked) {
            if ((checked & 63) == 63 && budget.Spent()) return Progress::Continue;
            SDK::UObject* object = SDK::UObject::GObjects->GetByIndex(state.cursor);
            if (!object || !HeaderReadable(object) || object->Index != state.cursor || Template(object)) continue;
            if (!wanted.empty() && !ClassMatches(object, wanted, scratch)) continue;
            const SDK::UStruct* type = object->Class;
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
            row.name = row.ref.address ? std::string(FullName(row.type->Name, name)) : "?";
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
// Name pool scan state. The layout is picked once per process.
int g_nameLayout = -1; // Index into names::Layouts; -2 = none matched.
struct NameScan { uint32_t block, offset, used, matches; };
NameScan g_nameScan;

struct Pool {
    names::PoolHeader header;
    const uint8_t* const* blocks;
};

bool ReadPool(const Host& host, Pool& out) {
    const auto* pool = reinterpret_cast<const uint8_t*>(SDK::InSDKUtils::GetImageBase() + SDK::Offsets::GNames);
    if (!host.readable(pool, names::BlocksOffset)) return false;
    memcpy(&out.header, pool, sizeof(out.header));
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

// The layout whose first entries decode to the engine's own names.
int PickLayout(const Pool& pool, const Host& host) {
    for (int candidate = 0; candidate < static_cast<int>(std::size(names::Layouts)); ++candidate) {
        const auto& layout = names::Layouts[candidate];
        const auto block = BlockBytes(pool, 0, layout, host);
        uint32_t offset = 0, agreed = 0;
        names::Entry entry{};
        for (; agreed < 32 && names::EntryAt(block, offset, layout, entry); ++agreed) {
            char pooled[MaxNameFindText + 1], engine[MaxNameFindText + 1];
            names::Text(block, entry, layout, pooled, sizeof(pooled));
            size_t length = 0;
            if (!GuardedName(names::Index(0, offset, layout), engine, sizeof(engine), length) ||
                strcmp(pooled, engine) != 0) break;
            const uint64_t next = names::NextOffset(entry, layout);
            if (next > UINT32_MAX) break;
            offset = static_cast<uint32_t>(next);
        }
        if (agreed >= 8) return candidate;
    }
    return -2;
}
}

Progress NameFind(const Request& request, Response& response, bool first, const Host& host) {
    if (first) {
        if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
        const Context& pack = Pack(host);
        if (!OnEngineThread(pack, response)) return Progress::Done;
    }
    Pool pool{};
    if (!ReadPool(host, pool)) { Refuse(response, Status::NotReadable, "name pool not readable"); return Progress::Done; }
    if (g_nameLayout == -1) g_nameLayout = PickLayout(pool, host);
    if (g_nameLayout < 0) { Refuse(response, Status::Unsupported, "name pool layout not recognised"); return Progress::Done; }
    const auto& layout = names::Layouts[g_nameLayout];
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

namespace {
// Each: the instance list of the last class, and the rows of the last result.
struct EachState {
    std::string className, function;
    uint64_t args0 = 0;
    std::vector<Ref> instances;
    std::vector<uint8_t> rows;
    uint32_t rowCount = 0;
    int32_t cursor = 0;
    bool listed = false, capped = false, ready = false;
};
EachState* g_each;

void EachPage(uint32_t from, Response& response) {
    const uint32_t stride = EachStride(EachSize(g_each->args0));
    const uint32_t perPage = static_cast<uint32_t>(MaxRead / stride);
    const uint32_t count = from < g_each->rowCount ? std::min(perPage, g_each->rowCount - from) : 0;
    if (count) memcpy(response.data, g_each->rows.data() + size_t{from} * stride, size_t{count} * stride);
    response.length = count * stride;
    const uint32_t next = from + count;
    response.detail = FindDetail(g_each->rowCount, next < g_each->rowCount ? next : 0);
    if (g_each->capped) CopyText(response.reason, "capped");
}

// Read-only by contract: const or pure, every parameter a plain output.
bool EachCallable(const SDK::UFunction* function, const Host& host) {
    if (!EachFunctionAllowed(static_cast<uint32_t>(function->FunctionFlags))) return false;
    unsigned children = 0;
    const SDK::FField* field = function->ChildProperties;
    for (; field && children < MaxChildren && host.readable(field, sizeof(SDK::FProperty)); field = field->Next, ++children) {
        const auto* property = static_cast<const SDK::FProperty*>(field);
        const auto propertyFlags = static_cast<uint64_t>(property->PropertyFlags);
        if ((propertyFlags & flags::Parm) && !EachParameterAllowed(propertyFlags, PlainData(property, host))) return false;
    }
    return !field;
}

// Reads or calls once per listed instance, all in this tick.
bool EachEvaluate(const Request& request, Response& response, const Host& host) {
    const Context& pack = Pack(host); // Each checked the thread this tick.
    auto& state = *g_each;
    const uint32_t size = EachSize(request.args[0]), offset = EachOffset(request.args[0]);
    const uint32_t stride = EachStride(size);
    const bool call = request.text[0] != 0;
    const auto [owner, name] = SplitFunction(request.text);
    const SDK::UStruct* lastType = nullptr;
    SDK::UFunction* function = nullptr;
    const SDK::UFunction* checked = nullptr;
    state.rows.assign(state.instances.size() * size_t{stride}, 0);
    state.rowCount = 0;
    for (const Ref& ref : state.instances) {
        SDK::UObject* object = Resolve(ref, pack);
        if (!object) continue; // Gone since the scan.
        uint8_t* row = state.rows.data() + size_t{state.rowCount} * stride;
        if (call) {
            if (object->Class != lastType) {
                lastType = object->Class;
                function = FindFunction(lastType, owner, name, pack);
            }
            if (!function) continue;
            if (function != checked) {
                if (!host.readable(function, sizeof(SDK::UFunction)) || !EachCallable(function, host)) {
                    Refuse(response, Status::Refused, "only const or pure functions with plain outputs only");
                    return false;
                }
                if (function->Size < 0 || static_cast<uint32_t>(function->Size) != size) {
                    Refuse(response, Status::BadRequest, "parameter block size differs from the function's",
                        static_cast<uint64_t>(function->Size < 0 ? 0 : function->Size));
                    return false;
                }
                checked = function;
            }
            alignas(16) uint8_t params[MaxEachValue] = {};
            object->ProcessEvent(function, params);
            memcpy(row + sizeof(Ref), params, size);
        } else {
            const auto* source = reinterpret_cast<const uint8_t*>(object) + offset;
            if (!host.readable(source, size) || !GuardedCopy(row + sizeof(Ref), source, size)) continue;
        }
        memcpy(row, &ref, sizeof(Ref));
        ++state.rowCount;
    }
    return true;
}
}

Progress Each(const Request& request, Response& response, bool first, const Host& host) {
    const Context& pack = Pack(host);
    if (!OnEngineThread(pack, response)) return Progress::Done;
    const std::string_view className = ClassArgument(request);
    if (first) {
        if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
        if (!g_each) g_each = new (std::nothrow) EachState;
        if (!g_each) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
        auto& state = *g_each;
        const bool same = state.className == className;
        if (request.args[1]) {
            if (state.ready && same && state.function == request.text && state.args0 == request.args[0]) {
                EachPage(static_cast<uint32_t>(request.args[1]), response);
                return Progress::Done;
            }
            Refuse(response, Status::BadRequest, "no matching result to page; start at row 0");
            return Progress::Done;
        }
        state.ready = false;
        if (!(same && state.listed && (request.flags & EachReuse))) {
            try {
                state.className.assign(className);
                state.instances.clear();
            } catch (...) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
            state.listed = state.capped = false;
            state.cursor = 0;
            ResetClassCache();
        }
    }
    auto& state = *g_each;
    try {
        if (!state.listed) {
            Budget budget;
            const int32_t count = SDK::UObject::GObjects->Num();
            g_region = {};
            std::string scratch;
            for (unsigned checked = 0; state.cursor < count; ++state.cursor, ++checked) {
                if ((checked & 63) == 63 && budget.Spent()) return Progress::Continue;
                SDK::UObject* object = SDK::UObject::GObjects->GetByIndex(state.cursor);
                if (!object || !HeaderReadable(object) || object->Index != state.cursor || Template(object) ||
                    !ClassMatches(object, className, scratch)) continue;
                const Ref ref = Identify(object, pack);
                if (!ref.address) continue;
                if (state.instances.size() == MaxEachInstances) { state.capped = true; break; }
                state.instances.push_back(ref);
            }
            state.listed = true;
        }
        if (!EachEvaluate(request, response, host)) return Progress::Done;
        state.function.assign(request.text);
    } catch (...) {
        state.listed = false;
        Refuse(response, Status::Refused, "out of memory");
        return Progress::Done;
    }
    state.args0 = request.args[0];
    state.ready = true;
    EachPage(0, response);
    return Progress::Done;
}
}

namespace reagentlive::ue {
namespace {
// ExecEntries: native functions from one table scan. Pages are formatted on
// demand from the function pointers (native functions live for the process).
struct ExecState {
    int32_t cursor = 0;
    bool complete = false;
    std::vector<const SDK::UFunction*> functions;
};
ExecState* g_exec;

void ExecPage(uint32_t from, Response& response, const Context& pack) {
    const auto& functions = g_exec->functions;
    auto* entries = reinterpret_cast<ExecEntry*>(response.data);
    uint32_t count = 0, i = from;
    std::string name;
    for (; i < functions.size() && count < MaxExecPerPage; ++i) {
        ExecEntry& entry = entries[count++];
        entry = {};
        const SDK::UFunction* function = functions[i];
        if (!pack.object(function)) continue; // Zeroed row: gone.
        entry.exec = reinterpret_cast<uintptr_t>(function->ExecFunction);
        CopyText(entry.name, FullName(function->Name, name));
        if (const SDK::UObject* outer = function->Outer; outer && pack.object(outer) && HeaderReadable(outer))
            CopyText(entry.owner, FullName(outer->Name, name));
    }
    response.length = count * static_cast<uint32_t>(sizeof(ExecEntry));
    response.detail = FindDetail(static_cast<uint32_t>(functions.size()), i < functions.size() ? i : 0);
}
}

Progress ExecEntries(const Request& request, Response& response, bool first, const Host& host) {
    const Context& pack = Pack(host);
    if (!OnEngineThread(pack, response)) return Progress::Done;
    if (first) {
        if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
        if (!g_exec) g_exec = new (std::nothrow) ExecState;
        if (!g_exec) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
        if (request.args[1] && g_exec->complete) {
            ExecPage(static_cast<uint32_t>(request.args[1]), response, pack);
            return Progress::Done;
        }
        g_exec->functions.clear();
        g_exec->cursor = 0;
        g_exec->complete = false;
    }
    auto& state = *g_exec;
    Budget budget;
    g_region = {};
    try {
        const int32_t count = SDK::UObject::GObjects->Num();
        for (unsigned checked = 0; state.cursor < count; ++state.cursor, ++checked) {
            if ((checked & 63) == 63 && budget.Spent()) return Progress::Continue;
            SDK::UObject* object = SDK::UObject::GObjects->GetByIndex(state.cursor);
            if (!object || !HeaderReadable(object) || object->Index != state.cursor ||
                !object->IsA(SDK::EClassCastFlags::Function)) continue;
            const auto* function = static_cast<const SDK::UFunction*>(object);
            if (!(static_cast<uint32_t>(function->FunctionFlags) & flags::FunctionNative) || !function->ExecFunction) continue;
            if (state.functions.size() >= MaxExecEntries) break;
            state.functions.push_back(function);
        }
    } catch (...) {
        state.functions.clear();
        Refuse(response, Status::Refused, "out of memory");
        return Progress::Done;
    }
    state.complete = true;
    ExecPage(static_cast<uint32_t>(request.args[1]), response, pack);
    return Progress::Done;
}
}
