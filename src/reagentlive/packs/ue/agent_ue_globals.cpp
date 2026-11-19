// reagentlive Unreal pack: locating and checking the object table and the name
// pool (globals.h) before the first pack command, the object layout they are
// read with, and decoding names from the located pool. SDK-free: built with
// REAGENTLIVE_UE_SDK (reagentlive::ue) the layout comes from the host's SDK,
// without it (reagentlive::ue-base) from globals::StockShapes and the probes.
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
#include "reagentlive/packs/ue/agent_ue.h"
#include "reagentlive/packs/ue/globals.h"
#include "reagentlive/packs/ue/name_pool.h"
#ifdef REAGENTLIVE_UE_SDK
#include "SDK/CoreUObject_classes.hpp"
#endif
#include "reagentlive/packs/ue/agent_ue_base.h"

namespace reagentlive::ue {
namespace {
// A failed locate is tried again on a command after this long: an early
// injection can run before the engine has made its globals.
constexpr uint64_t RetryMilliseconds = 2000;
constexpr size_t ScanWindow = 64u << 10, MaxSections = 16;

struct Located {
    uint64_t table;
    uint64_t pool;
    int layout;
    bool ready, tried;
    uint64_t failedAt;
    char problem[MaxReason];
};
Located g_located;
ObjectLayout g_layout;

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

#ifdef REAGENTLIVE_UE_SDK
// The layout from the host's SDK: field positions, never addresses.
ObjectLayout SdkLayout() {
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
    shape.itemSerialOffset = static_cast<int32_t>(offsetof(SDK::FUObjectItem, SerialNumber));
    shape.objectIndexOffset = static_cast<uint32_t>(offsetof(SDK::UObject, Index));
    shape.objectClassOffset = static_cast<uint32_t>(offsetof(SDK::UObject, Class));
    shape.objectNameOffset = static_cast<uint32_t>(offsetof(SDK::UObject, Name));
    shape.objectFlagsOffset = static_cast<uint32_t>(offsetof(SDK::UObject, Flags));
    shape.nameNumberOffset = static_cast<uint32_t>(offsetof(SDK::FName, Number));
    return {shape, static_cast<int32_t>(offsetof(SDK::UStruct, SuperStruct)),
            static_cast<int32_t>(offsetof(SDK::UStruct, Size)), true};
}
// Template() tests these flags by value.
static_assert(static_cast<uint32_t>(SDK::EObjectFlags::ClassDefaultObject) == 0x10 &&
              static_cast<uint32_t>(SDK::EObjectFlags::ArchetypeObject) == 0x20);
#endif

// The layouts to try, in order: the SDK's, or each stock shape.
#ifdef REAGENTLIVE_UE_SDK
ObjectLayout g_candidates[1];
std::span<const ObjectLayout> Candidates() {
    g_candidates[0] = SdkLayout();
    return g_candidates;
}
#else
ObjectLayout g_candidates[std::size(globals::StockShapes)];
std::span<const ObjectLayout> Candidates() {
    for (size_t i = 0; i < std::size(g_candidates); ++i) g_candidates[i] = {globals::StockShapes[i]};
    return g_candidates;
}
#endif

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

void Append(const char* text, char*& at, char* end) {
    for (const char* c = text; *c; ++c) if (at < end) *at++ = *c;
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
// Each candidate layout is tried at every address; the first that checks wins.
void Locate(const Context& pack, const Host& host) {
    g_located.tried = true;
    const auto candidates = Candidates();
    Scan scan;
    bool scanned = false;
    uint64_t classOfClasses = 0, table = 0;
    const ObjectLayout* layoutFound = nullptr;
    const auto check = [&](uint64_t address) -> const char* {
        const char* problem = "table: no layout fits";
        for (const auto& candidate : candidates) {
            if (!(problem = globals::CheckTable(g_memory, address, candidate.shape, classOfClasses))) {
                layoutFound = &candidate;
                return nullptr;
            }
        }
        return problem;
    };
    const char* problem = pack.objectsGuess ? check(pack.objectsGuess) : "table: no guess";
    const char* tableSource = " (guess)";
    if (!problem) {
        table = pack.objectsGuess;
    } else {
        scanned = scan.Open(pack.image);
        if (!scanned) return Fail(problem, "; no scan");
        uint32_t headerSize = 0;
        for (const auto& candidate : candidates) headerSize = std::max(headerSize, candidate.shape.headerSize);
        table = scan.Find(headerSize, [&](uint64_t address, const uint8_t* bytes, size_t available) {
            bool plausible = false;
            for (const auto& candidate : candidates)
                plausible = plausible || (available >= candidate.shape.headerSize &&
                    !globals::HeaderProblem(globals::ParseHeader(bytes, candidate.shape), candidate.shape));
            return plausible && !check(address);
        });
        if (!table) return Fail(problem, "; scan: none");
        tableSource = " (scan)";
    }
    ObjectLayout layout = *layoutFound;
    const auto& shape = layout.shape;
    int32_t className = -1;
    if (!g_memory.Get(classOfClasses + shape.objectNameOffset, className)) return Fail("table: class name unreadable", "");
    int poolLayout = -1;
    uint64_t pool = 0;
    problem = pack.namesGuess ? globals::CheckPool(g_memory, pack.namesGuess, className, poolLayout) : "pool: no guess";
    const char* poolSource = " (guess)";
    if (!problem) {
        pool = pack.namesGuess;
    } else {
        if (!scanned && !(scanned = scan.Open(pack.image))) return Fail(problem, "; no scan");
        constexpr size_t header = names::BlocksOffset + sizeof(uint64_t);
        pool = scan.Find(header, [&](uint64_t address, const uint8_t* bytes, size_t available) {
            if (available < header || !globals::PoolHeaderPlausible(bytes)) return false;
            return !globals::CheckPool(g_memory, address, className, poolLayout);
        });
        if (!pool) return Fail(problem, "; scan: none");
        poolSource = " (scan)";
    }
    if (!layout.fromSdk) {
        // No SDK: the engine's class tree gives SuperStruct and UStruct::Size.
        uint64_t chain[3]{};
        layout.superStruct = globals::ProbeSuperStruct(g_memory, pool, names::Layouts[poolLayout], shape, classOfClasses, chain);
        layout.structSize = globals::ProbeStructSize(g_memory, shape, layout.superStruct, chain[1], chain[2]);
    }
    g_layout = layout;
    g_located.table = table;
    g_located.pool = pool;
    g_located.layout = poolLayout;
    g_located.ready = true;
    if (host.log) {
        char text[256];
        char* at = text;
        char* const end = text + sizeof(text) - 1;
        AppendHex("Unreal pack: object table ", table, tableSource, at, end);
        AppendHex(", name pool ", pool, poolSource, at, end);
        AppendHex(layout.fromSdk ? ", layout from the SDK: item " : ", layout probed: item ", shape.itemSize, "", at, end);
        AppendHex(", object at ", shape.itemObjectOffset, "", at, end);
        if (layout.superStruct >= 0) AppendHex(", super ", static_cast<uint32_t>(layout.superStruct), "", at, end);
        else Append(", super unknown", at, end);
        if (layout.structSize >= 0) AppendHex(", size ", static_cast<uint32_t>(layout.structSize), "", at, end);
        else Append(", size unknown", at, end);
        *at = 0;
        host.log(LogLevel::Info, 0, text);
    }
}
}

const ObjectLayout& Layout() { return g_layout; }

namespace {
// The item of a slot, or null: outside the table or an empty chunk.
const uint8_t* ItemAt(int32_t index) {
    const auto& shape = g_layout.shape;
    const auto* table = reinterpret_cast<const uint8_t*>(g_located.table);
    if (!table || index < 0) return nullptr;
    int32_t num;
    memcpy(&num, table + shape.numOffset, sizeof(num));
    if (index >= num) return nullptr;
    const uint8_t* items;
    memcpy(&items, table + shape.objectsOffset, sizeof(items));
    uint32_t slot = static_cast<uint32_t>(index);
    if (shape.perChunk) {
        memcpy(&items, items + uint64_t{slot / shape.perChunk} * sizeof(void*), sizeof(items));
        slot %= shape.perChunk;
    }
    return items ? items + uint64_t{slot} * shape.itemSize : nullptr;
}
}

int32_t TableNum() {
    int32_t num = 0;
    if (g_located.table) memcpy(&num, reinterpret_cast<const uint8_t*>(g_located.table) + g_layout.shape.numOffset, sizeof(num));
    return num;
}

Object* TableObject(int32_t index) {
    const uint8_t* item = ItemAt(index);
    if (!item) return nullptr;
    Object* object;
    memcpy(&object, item + g_layout.shape.itemObjectOffset, sizeof(object));
    return object;
}

bool TableItem(int32_t index, Object*& object, int32_t& serial) {
    const uint8_t* item = ItemAt(index);
    if (!item) return false;
    memcpy(&object, item + g_layout.shape.itemObjectOffset, sizeof(object));
    serial = 0;
    if (g_layout.shape.itemSerialOffset >= 0) memcpy(&serial, item + g_layout.shape.itemSerialOffset, sizeof(serial));
    return true;
}

int64_t g_scanMicroseconds = FindBudgetMicroseconds;

bool PackReady(const Context& pack, const Host& host, Response& response, Access access) {
    g_scanMicroseconds = pack.scanMicroseconds ? std::min(pack.scanMicroseconds, MaxScanMicroseconds) : FindBudgetMicroseconds;
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
    if (!g_located.ready || !Readable(reinterpret_cast<uint64_t>(object), globals::ObjectHeaderBytes(g_layout.shape))) return false;
    const int32_t index = ObjectIndex(object);
    return index >= 0 && ObjectClass(object) && TableObject(index) == object;
}
}
