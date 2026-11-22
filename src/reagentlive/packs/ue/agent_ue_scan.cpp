// reagentlive Unreal pack: table scans over the host's SDK with cached pages: each and exec entries.
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
#include "reagentlive/packs/ue/agent_ue.h"
#include "reagentlive/packs/ue/name_pool.h"
#include "reagentlive/packs/ue/agent_ue_internal.h"

namespace reagentlive::ue {
namespace {
// Each: the instance list of the last class, and the rows of the last result.
struct EachState {
    std::string className, function; // className: the whole filter (class, outer text, outer object).
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
bool EachCallable(const Object* function, const Host& host) {
    if (!EachFunctionAllowed(FunctionFlags(function))) return false;
    unsigned children = 0;
    FieldPtr field = ChildProperties(function);
    for (; field && children < MaxChildren && host.readable(field, PropertyBytes()); field = FieldNext(field), ++children) {
        const uint64_t propertyFlags = PropertyFlags(field);
        if ((propertyFlags & flags::Parm) && !EachParameterAllowed(propertyFlags, PlainData(field, host))) return false;
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
    const Object* lastType = nullptr;
    const Object* function = nullptr;
    const Object* checked = nullptr;
    state.rows.assign(state.instances.size() * size_t{stride}, 0);
    state.rowCount = 0;
    for (const Ref& ref : state.instances) {
        Object* object = Resolve(ref, pack);
        if (!object) continue; // Gone since the scan.
        uint8_t* row = state.rows.data() + size_t{state.rowCount} * stride;
        if (call) {
            if (ObjectClass(object) != lastType) {
                lastType = ObjectClass(object);
                function = FindFunction(lastType, owner, name, pack);
            }
            if (!function) continue;
            if (function != checked) {
                if (!host.readable(function, FunctionBytes()) || !EachCallable(function, host)) {
                    Refuse(response, Status::Refused, "only const or pure functions with plain outputs only");
                    return false;
                }
                const int32_t functionSize = StructSize(function);
                if (functionSize < 0 || static_cast<uint32_t>(functionSize) != size) {
                    Refuse(response, Status::BadRequest, "parameter block size differs from the function's",
                        static_cast<uint64_t>(functionSize < 0 ? 0 : functionSize));
                    return false;
                }
                checked = function;
            }
            alignas(16) uint8_t params[MaxEachValue] = {};
            if (!GuardedProcessEvent(object, function, params)) {
                Refuse(response, Status::Faulted, "call faulted: the target may be in a bad state",
                    reinterpret_cast<uintptr_t>(object));
                return false;
            }
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
    if (!PackReady(pack, host, response) ||
        !LayoutReady(request.text[0] ? GroupsCall : GroupClass, host, response)) return Progress::Done;
    const std::string_view className = ClassArgument(request);
    // The instance list is kept per filter: class, outer text and outer object.
    std::string filter;
    try {
        filter.assign(className);
        filter.push_back('\0');
        filter.append(detail::FilterOuter(request));
        filter.push_back('\0');
        filter.append(std::to_string(request.address));
    } catch (...) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
    if (first) {
        if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
        if (!OuterAlive(request, pack, response)) return Progress::Done;
        if (!g_each) g_each = new (std::nothrow) EachState;
        if (!g_each) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
        auto& state = *g_each;
        const bool same = state.className == filter;
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
                state.className = filter;
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
            const int32_t count = TableNum();
            g_region = {};
            std::string scratch;
            for (unsigned checked = 0; state.cursor < count; ++state.cursor, ++checked) {
                if ((checked & 63) == 63 && budget.Spent()) return Progress::Continue;
                Object* object = TableObject(state.cursor);
                if (!object || !HeaderReadable(object) || ObjectIndex(object) != state.cursor || Template(object) ||
                    !ClassMatches(object, className, scratch) ||
                    !OuterMatches(request, object, scratch)) continue;
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
    std::vector<const Object*> functions;
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
        const Object* function = functions[i];
        if (!pack.object(function)) continue; // Zeroed row: gone.
        entry.exec = ExecFunction(function);
        CopyText(entry.name, FullName(ObjectName(function), name));
        if (const Object* outer = ObjectOuter(function); outer && pack.object(outer) && HeaderReadable(outer))
            CopyText(entry.owner, FullName(ObjectName(outer), name));
    }
    response.length = count * static_cast<uint32_t>(sizeof(ExecEntry));
    response.detail = FindDetail(static_cast<uint32_t>(functions.size()), i < functions.size() ? i : 0);
}
}

Progress ExecEntries(const Request& request, Response& response, bool first, const Host& host) {
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response) || !LayoutReady(GroupClass | GroupFunction, host, response))
        return Progress::Done;
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
        const int32_t count = TableNum();
        for (unsigned checked = 0; state.cursor < count; ++state.cursor, ++checked) {
            if ((checked & 63) == 63 && budget.Spent()) return Progress::Continue;
            const Object* object = TableObject(state.cursor);
            if (!object || !HeaderReadable(object) || ObjectIndex(object) != state.cursor ||
                !IsA(object, cast::Function)) continue;
            const Object* function = object;
            if (!(FunctionFlags(function) & flags::FunctionNative) || !ExecFunction(function)) continue;
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
