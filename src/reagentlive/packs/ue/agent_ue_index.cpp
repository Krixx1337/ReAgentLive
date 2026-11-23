// reagentlive Unreal pack: the class index over the located object table
// (class_index.h), for find --cls and each. SDK-free (reagentlive::ue-base).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
#include <new>
#include <string>
#include <string_view>
#include "reagentlive/packs/ue/agent_ue_base.h"
#include "reagentlive/packs/ue/class_index.h"

namespace reagentlive::ue {
namespace {
// Heap-allocated on first use (no dynamic initialisation). Null after an
// allocation failure: every slot is then a candidate, as a full walk.
index::ClassIndex* g_index;
bool g_indexFailed;

struct TableSource {
    std::string scratch;
    int32_t Count() { return TableNum(); }
    const void* Object(int32_t slot) { return TableObject(slot); }
    const void* ClassOf(const void* object, int32_t slot) {
        const auto* header = static_cast<const ue::Object*>(object);
        if (!HeaderReadable(header) || ObjectIndex(header) != slot) return nullptr;
        return ObjectClass(header);
    }
    bool Named(const void* type, std::string_view name) {
        return ClassNamed(static_cast<const ue::Object*>(type), name, scratch);
    }
};

void Drop() {
    delete g_index;
    g_index = nullptr;
    g_indexFailed = true;
}
}

bool StartClassIndex(std::string_view wanted) {
    if (!g_index && !g_indexFailed) g_index = new (std::nothrow) index::ClassIndex;
    if (!g_index) return false;
    try {
        g_index->Start(wanted, GetTickCount64());
        return true;
    } catch (...) {
        Drop();
        return false;
    }
}

bool ClassIndexReady(const Budget& budget) {
    if (!g_index) return true;
    try {
        TableSource source;
        return g_index->Step(source, [&budget] { return budget.Spent(); });
    } catch (...) {
        Drop();
        return true;
    }
}

int32_t NextIndexed(int32_t from, int32_t end) {
    if (!g_index) return from < end ? from : end;
    return g_index->Next(from, end);
}
}
