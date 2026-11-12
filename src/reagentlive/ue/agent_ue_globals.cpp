// reagentlive Unreal pack: locating and checking the object table and the name
// pool (globals.h) before the first pack command, and decoding names from
// the located pool.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
#include <charconv>
#include <cstddef>
#include <new>
#include <string_view>
#include "reagentlive/ue/agent_ue.h"
#include "reagentlive/ue/globals.h"
#include "reagentlive/ue/name_pool.h"
#include "SDK/CoreUObject_classes.hpp"
#include "reagentlive/ue/agent_ue_internal.h"

namespace reagentlive::ue {
namespace {
// A failed locate is tried again on a command after this long: an early
// injection can run before the engine has made its globals.
constexpr uint64_t RetryMilliseconds = 2000;
constexpr size_t ScanWindow = 64u << 10, MaxSections = 16;

struct Located {
    const SDK::TUObjectArray* table;
    uint64_t pool;
    int layout;
    bool ready, tried;
    uint64_t failedAt;
    char problem[MaxReason];
};
Located g_located;

bool Readable(uint64_t address, size_t bytes) {
    uint64_t cursor = address;
    if (!cursor || bytes > UINT64_MAX - cursor) return false;
    const uint64_t end = cursor + bytes;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                              PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return false;
        const uint64_t next = reinterpret_cast<uint64_t>(info.BaseAddress) + info.RegionSize;
        if (next <= cursor) return false;
        cursor = next;
    }
    return true;
}

// Preflight, then a copy that survives the memory being freed in between.
bool Copy(const void*, uint64_t address, void* out, size_t bytes) {
    return Readable(address, bytes) && GuardedCopy(out, reinterpret_cast<const void*>(address), bytes);
}
constexpr globals::Memory g_memory{&Copy, nullptr};

// The table's field positions, from the host's SDK: layouts, never addresses.
globals::TableShape Shape() {
    using Table = SDK::TUObjectArray;
    globals::TableShape shape{};
    shape.headerSize = static_cast<uint32_t>(sizeof(Table));
    shape.objectsOffset = static_cast<uint32_t>(offsetof(Table, Objects));
    shape.maxOffset = static_cast<uint32_t>(offsetof(Table, MaxElements));
    shape.numOffset = static_cast<uint32_t>(offsetof(Table, NumElements));
    if constexpr (requires { Table::ElementsPerChunk; }) {
        shape.numChunksOffset = static_cast<int32_t>(offsetof(Table, NumChunks));
        shape.maxChunksOffset = static_cast<int32_t>(offsetof(Table, MaxChunks));
        shape.perChunk = static_cast<uint32_t>(Table::ElementsPerChunk);
    } else {
        shape.numChunksOffset = shape.maxChunksOffset = -1;
    }
    shape.itemSize = static_cast<uint32_t>(sizeof(SDK::FUObjectItem));
    shape.itemObjectOffset = static_cast<uint32_t>(offsetof(SDK::FUObjectItem, Object));
    shape.objectIndexOffset = static_cast<uint32_t>(offsetof(SDK::UObject, Index));
    shape.objectClassOffset = static_cast<uint32_t>(offsetof(SDK::UObject, Class));
    shape.objectNameOffset = static_cast<uint32_t>(offsetof(SDK::UObject, Name));
    return shape;
}

// The image's writable sections, scanned in windows of one heap buffer.
struct Scan {
    globals::Section sections[MaxSections];
    size_t count = 0;
    uint8_t* buffer = nullptr;
    ~Scan() { delete[] buffer; }
    bool Open(uintptr_t image) {
        if (!image) image = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        count = globals::WritableSections(g_memory, image, sections, MaxSections);
        buffer = new (std::nothrow) uint8_t[ScanWindow];
        return count && buffer;
    }
    template<class Visit> uint64_t Find(size_t overlap, Visit&& visit) {
        for (size_t i = 0; i < count; ++i)
            if (const uint64_t hit = globals::ScanSection(g_memory, sections[i], {buffer, ScanWindow}, overlap, visit)) return hit;
        return 0;
    }
};

void AppendHex(std::string_view label, uint64_t value, const char* source, char*& at, char* end) {
    for (const char c : label) if (at < end) *at++ = c;
    if (end - at > 18) {
        *at++ = '0';
        *at++ = 'x';
        at = std::to_chars(at, end, value, 16).ptr;
    }
    for (const char* c = source; *c; ++c) if (at < end) *at++ = *c;
}

void Fail(const char* problem, const char* scan) {
    std::string_view text = problem;
    char* at = g_located.problem;
    char* const end = g_located.problem + sizeof(g_located.problem) - 1;
    for (const char c : std::string_view("pack off: ")) *at++ = c;
    for (const char c : text) if (at < end) *at++ = c;
    for (const char* c = scan; *c; ++c) if (at < end) *at++ = *c;
    *at = 0;
    g_located.failedAt = GetTickCount64();
}

// Guess first, then the image's writable sections. Both globals must pass.
void Locate(const Context& pack, const Host& host) {
    g_located.tried = true;
    const auto shape = Shape();
    Scan scan;
    bool scanned = false;
    uint64_t classOfClasses = 0, table = 0;
    const char* problem = pack.objectsGuess ? globals::CheckTable(g_memory, pack.objectsGuess, shape, classOfClasses)
                                            : "table: no guess";
    const char* tableSource = " (guess)";
    if (!problem) {
        table = pack.objectsGuess;
    } else {
        scanned = scan.Open(pack.image);
        if (!scanned) return Fail(problem, "; no scan");
        table = scan.Find(shape.headerSize, [&](uint64_t address, const uint8_t* bytes, size_t available) {
            if (available < shape.headerSize ||
                globals::HeaderProblem(globals::ParseHeader(bytes, shape), shape)) return false;
            return !globals::CheckTable(g_memory, address, shape, classOfClasses);
        });
        if (!table) return Fail(problem, "; scan: none");
        tableSource = " (scan)";
    }
    int32_t className = -1;
    if (!g_memory.Get(classOfClasses + shape.objectNameOffset, className)) return Fail("table: class name unreadable", "");
    int layout = -1;
    uint64_t pool = 0;
    problem = pack.namesGuess ? globals::CheckPool(g_memory, pack.namesGuess, className, layout) : "pool: no guess";
    const char* poolSource = " (guess)";
    if (!problem) {
        pool = pack.namesGuess;
    } else {
        if (!scanned && !(scanned = scan.Open(pack.image))) return Fail(problem, "; no scan");
        constexpr size_t header = names::BlocksOffset + sizeof(uint64_t);
        pool = scan.Find(header, [&](uint64_t address, const uint8_t* bytes, size_t available) {
            if (available < header || !globals::PoolHeaderPlausible(bytes)) return false;
            return !globals::CheckPool(g_memory, address, className, layout);
        });
        if (!pool) return Fail(problem, "; scan: none");
        poolSource = " (scan)";
    }
    g_located.table = reinterpret_cast<const SDK::TUObjectArray*>(table);
    g_located.pool = pool;
    g_located.layout = layout;
    g_located.ready = true;
    if (host.log) {
        char text[160];
        char* at = text;
        char* const end = text + sizeof(text) - 1;
        AppendHex("Unreal pack: object table ", table, tableSource, at, end);
        AppendHex(", name pool ", pool, poolSource, at, end);
        *at = 0;
        host.log(LogLevel::Info, 0, text);
    }
}
}

const SDK::TUObjectArray* Table() { return g_located.table; }

bool PackReady(const Context& pack, const Host& host, Response& response, Access access) {
    const bool offThread = access == Access::Read && pack.offThreadReads && pack.gameThread && !pack.gameThread();
    if (const char* problem = offThread ? nullptr : EngineThreadProblem(pack)) {
        Refuse(response, Status::Refused, problem);
        return false;
    }
    if (!g_located.ready && (!g_located.tried || GetTickCount64() - g_located.failedAt >= RetryMilliseconds)) {
        Locate(pack, host);
        if (!g_located.ready && host.log) host.log(LogLevel::Warning, 0, g_located.problem);
    }
    if (g_located.ready) return true;
    Refuse(response, Status::Refused, g_located.problem);
    return false;
}

bool StillSame(const Ref& before, const Context& pack, Response& response) {
    if (pack.gameThread() || Resolve(before, pack)) return true;
    response.length = 0;
    Refuse(response, Status::NotFound, "object changed during the read");
    return false;
}

namespace {
// No objects with destructors here: SEH needs a frame without unwinding.
// Under /EHsc a fault skips the destructors of the frames it leaves; a
// handler's strings leak, nothing else is held.
Progress FaultGuarded(Handler body, const Request& request, Response& response, bool first, const Host& host, bool& faulted) {
    __try {
        return body(request, response, first, host);
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ?
                EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        faulted = true;
        return Progress::Done;
    }
}
}

Progress GuardedRead(Handler body, const Request& request, Response& response, bool first, const Host& host) {
    const Context& pack = Pack(host);
    if (!pack.offThreadReads || !pack.gameThread || pack.gameThread()) return body(request, response, first, host);
    bool faulted = false;
    const Progress progress = FaultGuarded(body, request, response, first, host, faulted);
    if (!faulted) return progress;
    g_region = {};
    response.length = 0;
    Refuse(response, Status::NotReadable, "fault during an off-thread read");
    if (host.log) host.log(LogLevel::Warning, static_cast<unsigned>(Status::NotReadable), "Unreal pack: fault during an off-thread read");
    return Progress::Done;
}

bool PoolName(int32_t index, char* out, size_t capacity, size_t& length) {
    if (!g_located.ready) return false;
    return globals::DecodeName(g_memory, g_located.pool, names::Layouts[g_located.layout], index, out, capacity, length);
}

uint64_t NamePool(const names::Layout*& layout) {
    if (!g_located.ready) return 0;
    layout = &names::Layouts[g_located.layout];
    return g_located.pool;
}

bool ObjectInTable(const SDK::UObject* object) {
    const auto* table = g_located.table;
    if (!table || !Readable(reinterpret_cast<uint64_t>(object), sizeof(SDK::UObject)) || object->Index < 0 || !object->Class) return false;
    return table->GetByIndex(object->Index) == object;
}
}
