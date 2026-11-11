// reagentlive Unreal pack: Blueprint bytecode: script (one function's anchored
// names, calls and properties) and refs (functions whose bytecode names a tag).
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
#include <vector>
#include "reagentlive/ue/agent_ue.h"
#include "SDK/CoreUObject_classes.hpp"
#include "reagentlive/ue/agent_ue_internal.h"

namespace reagentlive::ue {
namespace {
struct ScriptArray { const uint8_t* data; int32_t count, capacity; };

// The bytecode of a struct with a plausible Script array, copied with a guard.
bool ReadScript(const SDK::UStruct* type, const Host& host, std::vector<uint8_t>& out) {
    const auto* where = reinterpret_cast<const uint8_t*>(type) + ScriptOffset;
    ScriptArray script{};
    if (!host.readable(where, sizeof(script)) || !GuardedCopy(&script, where, sizeof(script))) return false;
    if (script.count <= 0 || script.capacity < script.count || static_cast<uint32_t>(script.count) > MaxScriptBytes ||
        !script.data || !host.readable(script.data, static_cast<size_t>(script.count))) return false;
    out.resize(static_cast<size_t>(script.count));
    return GuardedCopy(out.data(), script.data, out.size());
}

// "Owner:Name" of a function, or "Name" without a readable outer.
std::string FunctionText(const SDK::UObject* function, const Context& pack) {
    std::string name, owner;
    std::string text(FullName(function->Name, name));
    const SDK::UObject* outer = function->Outer;
    if (outer && pack.object(outer) && HeaderReadable(outer)) text = std::string(FullName(outer->Name, owner)) + ":" + text;
    return text;
}

// A property's "Type Name" when the pointer is an FField whose class name ends in "Property".
bool PropertyText(uint64_t pointer, const Host& host, std::string& out) {
    const auto* field = reinterpret_cast<const SDK::FField*>(pointer);
    if (!host.readable(field, sizeof(SDK::FField))) return false;
    const SDK::FFieldClass* type = field->ClassPrivate;
    if (!type || !host.readable(type, sizeof(SDK::FFieldClass))) return false;
    std::string typeName, fieldName;
    const auto typeText = FullName(type->Name, typeName);
    if (typeText.size() < 8 || !typeText.ends_with("Property")) return false;
    out = std::string(typeText) + " " + std::string(FullName(field->Name, fieldName));
    return true;
}

std::vector<ScriptEntry>* g_scriptRows; // Last scanned script, for paging.
const void* g_scriptOf;
uint32_t g_scriptSize;

// Confirms each anchored candidate and keeps one entry per value.
bool ScanFunction(const SDK::UStruct* type, const Host& host, const Context& pack) {
    std::vector<uint8_t> bytes;
    if (!ReadScript(type, host, bytes)) return false;
    auto& rows = *g_scriptRows;
    rows.clear();
    std::string text;
    ScanScript(bytes.data(), bytes.size(), [&](const ScriptCandidate& candidate) {
        for (auto& row : rows) {
            const bool same = row.kind == candidate.kind &&
                (candidate.kind == ScriptKind::Name ? static_cast<uint64_t>(row.name) == candidate.value : row.pointer == candidate.value);
            if (same) { ++row.count; return; }
        }
        if (rows.size() >= MaxScriptEntries) return;
        ScriptEntry entry{};
        entry.offset = candidate.offset;
        entry.count = 1;
        entry.kind = candidate.kind;
        entry.opcode = candidate.opcode;
        switch (candidate.kind) {
        case ScriptKind::Name: {
            size_t length = 0;
            entry.name = static_cast<int32_t>(candidate.value);
            if (!GuardedName(entry.name, entry.text, sizeof(entry.text), length) || !length) return;
            break;
        }
        case ScriptKind::Function: {
            const auto* object = reinterpret_cast<const SDK::UObject*>(candidate.value);
            const Ref ref = Identify(object, pack);
            if (!ref.address || !object->IsA(SDK::EClassCastFlags::Function)) return;
            entry.object = ref;
            entry.pointer = candidate.value;
            CopyText(entry.text, FunctionText(object, pack));
            break;
        }
        case ScriptKind::Property:
            if (!PropertyText(candidate.value, host, text)) return;
            entry.pointer = candidate.value;
            CopyText(entry.text, text);
            break;
        }
        rows.push_back(entry);
    });
    g_scriptOf = type;
    g_scriptSize = static_cast<uint32_t>(bytes.size());
    return true;
}

void ScriptPage(uint32_t from, Response& response) {
    const auto& rows = *g_scriptRows;
    auto* entries = reinterpret_cast<ScriptEntry*>(response.data);
    uint32_t count = 0, i = from;
    for (; i < rows.size() && count < MaxScriptEntriesPerPage; ++i) entries[count++] = rows[i];
    response.length = count * static_cast<uint32_t>(sizeof(ScriptEntry));
    response.detail = ScriptDetail(g_scriptSize, i < rows.size() ? i : 0);
}

// Refs: functions whose bytecode names one index, from one table scan.
struct RefsState {
    int32_t index = 0, cursor = 0;
    bool complete = false, capped = false;
    std::vector<RefsEntry> rows;
    std::vector<uint8_t> scratch;
};
RefsState* g_refs;

void RefsPage(uint32_t from, Response& response) {
    const auto& rows = g_refs->rows;
    auto* entries = reinterpret_cast<RefsEntry*>(response.data);
    uint32_t count = 0, i = from;
    for (; i < rows.size() && count < MaxRefsPerPage; ++i) entries[count++] = rows[i];
    response.length = count * static_cast<uint32_t>(sizeof(RefsEntry));
    response.detail = FindDetail(static_cast<uint32_t>(rows.size()), i < rows.size() ? i : 0);
    if (g_refs->capped) CopyText(response.reason, "capped");
}
}

Progress Script(const Request& request, Response& response, bool, const Host& host) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const Context& pack = Pack(host);
    if (!OnEngineThread(pack, response)) return Progress::Done;
    const SDK::UObject* object = ResolveRequestObject(request, pack);
    if (!object) { Refuse(response, Status::NotFound, "object gone or not a valid object"); return Progress::Done; }
    if (!object->IsA(SDK::EClassCastFlags::Struct)) { Refuse(response, Status::BadRequest, "not a function or struct"); return Progress::Done; }
    const auto* type = static_cast<const SDK::UStruct*>(object);
    if (!g_scriptRows) g_scriptRows = new (std::nothrow) std::vector<ScriptEntry>;
    if (!g_scriptRows) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
    try {
        if ((!request.args[1] || g_scriptOf != type) && !ScanFunction(type, host, pack)) {
            Refuse(response, Status::NotFound, "no bytecode (a native function, or an unreadable script)");
            return Progress::Done;
        }
    } catch (...) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
    ScriptPage(static_cast<uint32_t>(request.args[1]), response);
    return Progress::Done;
}

Progress Refs(const Request& request, Response& response, bool first, const Host& host) {
    const Context& pack = Pack(host);
    if (!OnEngineThread(pack, response)) return Progress::Done;
    const auto index = static_cast<int32_t>(request.args[0]);
    if (first) {
        if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
        if (!g_refs) g_refs = new (std::nothrow) RefsState;
        if (!g_refs) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
        if (request.args[1] && g_refs->complete && g_refs->index == index) {
            RefsPage(static_cast<uint32_t>(request.args[1]), response);
            return Progress::Done;
        }
        g_refs->rows.clear();
        g_refs->index = index;
        g_refs->cursor = 0;
        g_refs->complete = g_refs->capped = false;
    }
    auto& state = *g_refs;
    uint8_t pattern[1 + sizeof(int32_t)] = {expr::NameConst};
    memcpy(pattern + 1, &index, sizeof(index));
    Budget budget;
    g_region = {};
    std::string scratch;
    try {
        const int32_t count = SDK::UObject::GObjects->Num();
        for (unsigned checked = 0; state.cursor < count; ++state.cursor, ++checked) {
            if ((checked & 15) == 15 && budget.Spent()) return Progress::Continue;
            SDK::UObject* object = SDK::UObject::GObjects->GetByIndex(state.cursor);
            if (!object || !HeaderReadable(object) || object->Index != state.cursor ||
                !object->IsA(SDK::EClassCastFlags::Function)) continue;
            if (!ReadScript(static_cast<const SDK::UStruct*>(object), host, state.scratch)) continue;
            const auto& bytes = state.scratch;
            uint32_t hits = 0, firstHit = 0;
            for (size_t at = 0; at + ScriptNameOperand < bytes.size(); ++at) {
                if (bytes[at] != expr::NameConst || memcmp(bytes.data() + at, pattern, sizeof(pattern))) continue;
                if (!hits++) firstHit = static_cast<uint32_t>(at);
            }
            if (!hits) continue;
            if (state.rows.size() >= MaxRefs) { state.capped = true; break; }
            RefsEntry entry{};
            entry.function = Identify(object, pack);
            entry.offset = firstHit;
            entry.count = hits;
            std::string name;
            CopyText(entry.name, FullName(object->Name, name));
            if (const SDK::UObject* outer = object->Outer; outer && pack.object(outer) && HeaderReadable(outer))
                CopyText(entry.owner, FullName(outer->Name, name));
            state.rows.push_back(entry);
        }
    } catch (...) {
        state.rows.clear();
        Refuse(response, Status::Refused, "out of memory");
        return Progress::Done;
    }
    state.complete = true;
    RefsPage(static_cast<uint32_t>(request.args[1]), response);
    return Progress::Done;
}
}
