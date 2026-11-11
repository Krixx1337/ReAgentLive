// reagentlive core agent.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include "reagentlive/core/agent.h"

namespace reagentlive {
namespace {
// Heap, not a global: large zeroed globals grow mapped images.
struct State {
    Request request;   // Copied at start: the client may rewrite the shared slot.
    Response response;
};
Section* g_section;
State* g_state;
bool g_openFailed;
uint32_t g_handled;
// A request still running across ticks (Progress::Continue).
const Command* g_active;
uint32_t g_activeSeq;
uint32_t g_agentId; // This instance, in Section::owner while it answers.
bool g_dormant;     // Another agent in this process took the section over.

void Log(const Host& host, LogLevel level, unsigned status, const char* format, ...) {
    if (!host.log) return;
    char text[256];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (written >= 0) host.log(level, status, text);
}

bool Open(const Host& host, std::span<const Command> commands) {
    g_openFailed = true; // One attempt: retrying every tick would spam the log.
    if (!host.sectionName || !host.readable) return false;
    if (host.thread == HostThread::Unset) {
        Log(host, LogLevel::Warning, 0, "Dev agent: the host did not declare which thread it ticks on (Host::thread)");
        return false;
    }
    g_state = static_cast<State*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(State)));
    HANDLE mapping = g_state ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Section), host.sectionName) : nullptr;
    void* view = mapping ? MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Section)) : nullptr;
    if (!view) {
        const DWORD error = GetLastError();
        if (mapping) CloseHandle(mapping);
        if (g_state) HeapFree(GetProcessHeap(), 0, g_state);
        g_state = nullptr;
        Log(host, LogLevel::Warning, error, "Dev agent: shared section unavailable (error %lu)", error);
        return false;
    }
    // The mapping stays open for the process.
    auto* section = static_cast<Section*>(view);
    // An earlier agent in this process (the host injected again) already
    // answers here: take over, it goes quiet when it sees the new owner.
    const bool replacing = std::atomic_ref(section->magic).load(std::memory_order_acquire) == Magic &&
        section->agentPid == GetCurrentProcessId();
    g_agentId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_state) >> 4) ^ GetTickCount() ^ 0x9e3779b9u;
    if (!g_agentId) g_agentId = 1;
    section->layout = host.layout;
    std::atomic_ref(section->hostThread).store(static_cast<uint32_t>(host.thread), std::memory_order_relaxed);
    CopyText(section->build, __DATE__ " " __TIME__);
    if (replacing) std::atomic_ref(section->takeovers).fetch_add(1, std::memory_order_relaxed);
    std::atomic_ref(section->owner).store(g_agentId, std::memory_order_release);
    section->agentPid = GetCurrentProcessId();
    uint32_t count = 0;
    for (const Command& command : commands)
        if (command.run && count < MaxOps) section->ops[count++] = command.op;
    section->opCount = count;
    g_handled = std::atomic_ref(section->requestSeq).load(std::memory_order_acquire);
    std::atomic_ref(section->responseSeq).store(g_handled, std::memory_order_release);
    std::atomic_ref(section->magic).store(Magic, std::memory_order_release); // Last: marks the header valid.
    g_section = section;
    g_openFailed = false;
    Log(host, LogLevel::Info, 0, replacing ? "Dev agent: took over %ls from an earlier agent in this process" :
        "Dev agent: listening on %ls", host.sectionName);
    return true;
}

// Page preflight for writes: committed, writable, no guard. Not a lifetime guarantee.
bool Writable(uintptr_t cursor, size_t bytes) {
    if (!cursor || bytes > UINTPTR_MAX - cursor) return false;
    const uintptr_t end = cursor + bytes;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            return false;
        const uintptr_t next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= cursor) return false;
        cursor = next;
    }
    return true;
}


void Finish(const Host& host, uint32_t seq, const char* name, bool logSuccess = true) {
    const Response& response = g_state->response;
    Response& shared = g_section->response;
    shared.status = response.status;
    shared.length = response.length <= MaxRead ? response.length : 0;
    shared.detail = response.detail;
    shared.tick = std::atomic_ref(g_section->heartbeat).load(std::memory_order_relaxed);
    memcpy(shared.reason, response.reason, sizeof(shared.reason));
    shared.reason[sizeof(shared.reason) - 1] = 0;
    if (shared.length) memcpy(shared.data, response.data, shared.length);
    std::atomic_ref(g_section->responseSeq).store(seq, std::memory_order_release);
    g_handled = seq;
    g_active = nullptr;
    if (response.status == Status::Ok && !logSuccess) return;
    const Request& request = g_state->request;
    Log(host, response.status == Status::Ok ? LogLevel::Info : LogLevel::Warning, static_cast<unsigned>(response.status),
        "Dev agent: %s 0x%llx len=%u -> %s%s%s", name, static_cast<unsigned long long>(request.address), request.length,
        StatusText(response.status), response.reason[0] ? ": " : "", response.reason);
}
}

bool GuardedCopy(void* destination, const void* source, size_t bytes) {
    __try {
        memcpy(destination, source, bytes);
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ?
                EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

namespace {
Status ReadBytes(const Host& host, uint64_t address, size_t length, uint8_t* destination) {
    const void* source = reinterpret_cast<const void*>(address);
    if (!host.readable(source, length)) return Status::NotReadable;
    return GuardedCopy(destination, source, length) ? Status::Ok : Status::Faulted;
}
}

Progress Read(const Request& request, Response& response, bool, const Host& host) {
    if ((response.status = Check(request)) != Status::Ok) return Progress::Done;
    response.status = ReadBytes(host, request.address, request.length, response.data);
    if (response.status == Status::Ok) response.length = request.length;
    if (response.status == Status::Faulted) response.detail = request.address;
    return Progress::Done;
}

Progress ReadBatch(const Request& request, Response& response, bool, const Host& host) {
    if ((response.status = Check(request)) != Status::Ok) return Progress::Done;
    ReadSpan reads[MaxBatchReads];
    memcpy(reads, request.data, request.length * sizeof(ReadSpan));
    const auto size = ReadBatchSize(std::span(reads, request.length));
    if (!size) {
        Refuse(response, Status::BadRequest, "invalid read batch");
        return Progress::Done;
    }
    size_t offset = request.length * sizeof(Status);
    for (uint32_t i = 0; i < request.length; ++i) {
        const auto& read = reads[i];
        auto* destination = response.data + offset;
        const auto status = ReadBytes(host, read.address, read.length, destination);
        if (status != Status::Ok) memset(destination, 0, read.length);
        memcpy(response.data + i * sizeof(Status), &status, sizeof(status));
        offset += read.length;
    }
    response.length = static_cast<uint32_t>(size);
    response.detail = std::atomic_ref(g_section->heartbeat).load(std::memory_order_relaxed);
    return Progress::Done;
}

Progress Write(const Request& request, Response& response, bool, const Host&) {
    if ((response.status = Check(request)) != Status::Ok) return Progress::Done;
    if (!Writable(request.address, request.length)) { response.status = Status::NotWritable; return Progress::Done; }
    if (request.flags & WriteExpect) {
        // Compare first: memory reused between the client's read and this
        // write must not be overwritten.
        if (!GuardedCopy(response.data, reinterpret_cast<const void*>(request.address), request.length)) {
            response.status = Status::Faulted;
            response.detail = request.address;
            return Progress::Done;
        }
        if (memcmp(response.data, request.data + request.length, request.length)) {
            Refuse(response, Status::Refused, "memory differs from the expected bytes");
            response.length = request.length; // What is there now.
            return Progress::Done;
        }
    }
    if (!GuardedCopy(reinterpret_cast<void*>(request.address), request.data, request.length)) {
        response.status = Status::Faulted;
        response.detail = request.address;
    }
    return Progress::Done;
}

// Loaded modules, exe first, one page per request (length = first index).
Progress Modules(const Request& request, Response& response, bool, const Host&) {
    HMODULE modules[1024];
    DWORD bytes = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes)) {
        Refuse(response, Status::Refused, "module list unavailable", GetLastError());
        return Progress::Done;
    }
    const uint32_t total = static_cast<uint32_t>(std::min<size_t>(bytes / sizeof(HMODULE), std::size(modules)));
    auto* entries = reinterpret_cast<ModuleEntry*>(response.data);
    uint32_t count = 0;
    for (uint32_t i = request.length; i < total && count < MaxModulesPerPage; ++i) {
        MODULEINFO info{};
        wchar_t wide[MAX_PATH]{};
        ModuleEntry& entry = entries[count++];
        entry = {};
        if (K32GetModuleInformation(GetCurrentProcess(), modules[i], &info, sizeof(info))) {
            entry.base = reinterpret_cast<uint64_t>(info.lpBaseOfDll);
            entry.size = info.SizeOfImage;
        }
        if (K32GetModuleBaseNameW(GetCurrentProcess(), modules[i], wide, MAX_PATH))
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, entry.name, sizeof(entry.name) - 1, nullptr, nullptr);
    }
    response.length = count * static_cast<uint32_t>(sizeof(ModuleEntry));
    response.detail = total;
    return Progress::Done;
}

namespace {
constexpr uintptr_t PageSize = 4096;
constexpr size_t SearchChunkPages = 64; // 256 KB between clock and cancel checks.

// One search, self-contained: the background worker must not read the shared
// request slot, which the next request overwrites.
struct SearchJob {
    uint8_t pattern[MaxSearchPattern];
    uint32_t length, align, flags, maxHits, hits;
    uintptr_t cursor, end;
    bool relative;   // Xrefs: 4-byte fields landing on `target` (protocol.h).
    uint64_t target;
    uint64_t found[MaxSearchHits];
};
SearchJob* g_job;        // Game-thread search (heap, like State).
SearchJob* g_background; // Owned by the worker while it runs.

// Background worker: started once, on the first background search a host
// allows; it lives for the process, like the agent. Idle -> Running (agent)
// -> Done (worker) -> Idle (agent, after copying the hits).
enum : uint32_t { WorkerIdle, WorkerRunning, WorkerDone };
std::atomic<uint32_t> g_workerState{WorkerIdle};
std::atomic<bool> g_workerCancel{false};
HANDLE g_workerWake;
bool g_workerFailed;

bool Searchable(const MEMORY_BASIC_INFORMATION& info, uint32_t flags) {
    // Write-combined and uncached pages are GPU or device mappings: very slow
    // to read from the CPU, and never the data an investigation looks for.
    if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS | PAGE_NOCACHE | PAGE_WRITECOMBINE)))
        return false;
    if ((flags & SearchPrivate) && info.Type != MEM_PRIVATE) return false;
    const DWORD access = info.Protect & 0xFF;
    const bool writable = access == PAGE_READWRITE || access == PAGE_WRITECOPY || access == PAGE_EXECUTE_READWRITE ||
        access == PAGE_EXECUTE_WRITECOPY;
    return (flags & SearchWritable) ? writable : writable || access == PAGE_READONLY || access == PAGE_EXECUTE_READ;
}
// Another view of this process's section: an agent injected earlier mapped
// it too, and its view shows the current request, pattern included.
bool SectionView(uintptr_t address) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) || info.Type != MEM_MAPPED)
        return false;
    const auto base = reinterpret_cast<uintptr_t>(info.AllocationBase);
    uint32_t header[3]{}; // magic, layout, agentPid
    return address - base < sizeof(Section) && GuardedCopy(header, info.AllocationBase, sizeof(header)) &&
        header[0] == Magic && header[2] == GetCurrentProcessId();
}
// The agent's own copies of the pattern are not results.
bool OwnBuffer(uintptr_t address) {
    const auto inside = [address](const void* base, size_t size) {
        const auto start = reinterpret_cast<uintptr_t>(base);
        return base && address >= start && address < start + size;
    };
    return inside(g_state, sizeof(State)) || inside(g_section, sizeof(Section)) || inside(g_job, sizeof(SearchJob)) ||
        inside(g_background, sizeof(SearchJob)) || SectionView(address);
}
// Records a match; true once the page of hits is full.
bool Hit(SearchJob& job, uintptr_t at) {
    if (OwnBuffer(at)) return false;
    job.found[job.hits++] = at;
    return job.hits == job.maxHits;
}
// Scanners: positions [begin, last) whose pattern fits below `limit`; `stop`
// is where to continue.
template<class Word> void ScanWords(SearchJob& job, uintptr_t begin, uintptr_t last, uintptr_t limit, Word value,
                                    uintptr_t& stop) {
    for (uintptr_t at = begin; at < last && at + sizeof(Word) <= limit; at += sizeof(Word))
        if (*reinterpret_cast<const Word*>(at) == value && Hit(job, at)) { stop = at + sizeof(Word); return; }
    stop = last;
}
void ScanBytes(SearchJob& job, uintptr_t begin, uintptr_t last, uintptr_t limit, uintptr_t& stop) {
    const uint8_t* pattern = job.pattern;
    const size_t length = job.length;
    const uintptr_t align = job.align;
    stop = last;
    for (uintptr_t at = begin; at < last && at + length <= limit; at += align) {
        if (align == 1) { // Jump to the next first-byte candidate (vectorised in the CRT).
            const void* next = memchr(reinterpret_cast<const void*>(at), pattern[0], last - at);
            if (!next) return;
            at = reinterpret_cast<uintptr_t>(next);
            if (at + length > limit) return;
        } else if (*reinterpret_cast<const uint8_t*>(at) != pattern[0]) continue;
        if (!memcmp(reinterpret_cast<const void*>(at), pattern, length) && Hit(job, at)) {
            stop = at + align;
            return;
        }
    }
}
// Every byte: the signed 32-bit value there, measured from its end, lands
// on the target or 1, 2 or 4 bytes after it (an immediate follows).
void ScanRelative(SearchJob& job, uintptr_t begin, uintptr_t last, uintptr_t limit, uintptr_t& stop) {
    const uint64_t target = job.target;
    for (uintptr_t at = begin; at < last && at + 4 <= limit; ++at) {
        int32_t value;
        memcpy(&value, reinterpret_cast<const void*>(at), 4);
        const uint64_t slack = target - (static_cast<uint64_t>(at) + 4) - static_cast<uint64_t>(static_cast<int64_t>(value));
        if ((slack <= 2 || slack == 4) && Hit(job, at)) { stop = at + 1; return; }
    }
    stop = last;
}
// False on a fault (the range was freed after the region check).
bool ScanSlice(SearchJob& job, uintptr_t begin, uintptr_t last, uintptr_t limit, uintptr_t& stop) {
    __try {
        if (job.relative) {
            ScanRelative(job, begin, last, limit, stop);
        } else if (job.length == 8 && job.align == 8) {
            uint64_t value;
            memcpy(&value, job.pattern, 8);
            ScanWords(job, begin, last, limit, value, stop);
        } else if (job.length == 4 && job.align == 4) {
            uint32_t value;
            memcpy(&value, job.pattern, 4);
            ScanWords(job, begin, last, limit, value, stop);
        } else {
            ScanBytes(job, begin, last, limit, stop);
        }
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ?
                EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}
// End of the run of resident pages from `page` (page aligned), checking at
// most `pages` pages; `page` itself when the first is not resident. Reading a
// non-resident page would make the OS fault it in (or create it, for memory
// that was committed but never touched).
uintptr_t ResidentEnd(uintptr_t page, size_t pages) {
    PSAPI_WORKING_SET_EX_INFORMATION info[SearchChunkPages + 1]{};
    for (size_t i = 0; i < pages; ++i) info[i].VirtualAddress = reinterpret_cast<void*>(page + i * PageSize);
    if (!K32QueryWorkingSetEx(GetCurrentProcess(), info, static_cast<DWORD>(pages * sizeof(info[0]))))
        return page + pages * PageSize;
    size_t valid = 0;
    while (valid < pages && info[valid].VirtualAttributes.Valid) ++valid;
    return page + valid * PageSize;
}
int64_t Microseconds() {
    LARGE_INTEGER frequency, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    return now.QuadPart / frequency.QuadPart * 1000000 + now.QuadPart % frequency.QuadPart * 1000000 / frequency.QuadPart;
}
uintptr_t AlignUp(uintptr_t value, uintptr_t align) { return (value + align - 1) & ~(align - 1); }

void StartJob(SearchJob& job, const Request& request) {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto top = reinterpret_cast<uintptr_t>(system.lpMaximumApplicationAddress) + 1;
    job.relative = request.op == op::Xrefs;
    job.target = 0;
    if (job.relative) memcpy(&job.target, request.data, 8);
    job.length = job.relative ? 4 : request.length;
    if (!job.relative) memcpy(job.pattern, request.data, request.length);
    job.align = job.relative ? 1 : SearchAlignment(request.flags);
    job.flags = request.flags;
    job.maxHits = static_cast<uint32_t>(request.args[1]);
    job.hits = 0;
    const uintptr_t start = std::max<uintptr_t>(static_cast<uintptr_t>(request.address),
        reinterpret_cast<uintptr_t>(system.lpMinimumApplicationAddress));
    job.cursor = AlignUp(start, job.align);
    job.end = request.args[0] ? std::min<uintptr_t>(static_cast<uintptr_t>(request.args[0]), top) : top;
}
// Scans until the range is done, the page of hits is full, `deadline` passes
// (0: none) or `cancel` is set. True once the job is finished.
bool RunJob(SearchJob& job, int64_t deadline, const std::atomic<bool>* cancel) {
    const bool paged = job.flags & SearchPaged;
    while (job.cursor < job.end && job.hits < job.maxHits) {
        if (deadline && Microseconds() >= deadline) return false;
        if (cancel && cancel->load(std::memory_order_relaxed)) return true;
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(job.cursor), &info, sizeof(info))) { job.cursor = job.end; break; }
        const uintptr_t regionEnd = std::min(reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize, job.end);
        if (regionEnd <= job.cursor) { job.cursor = job.end; break; }
        if (!Searchable(info, job.flags)) { job.cursor = regionEnd; continue; }
        // One chunk, then the clock again. The residency check looks one page
        // past the chunk so a match crossing into it is still found.
        const uintptr_t page = job.cursor & ~(PageSize - 1);
        const uintptr_t chunkEnd = std::min(regionEnd, page + SearchChunkPages * PageSize);
        uintptr_t limit = regionEnd;
        if (!paged) {
            const size_t pages = (std::min(regionEnd, chunkEnd + PageSize) - page + PageSize - 1) / PageSize;
            limit = std::min(regionEnd, ResidentEnd(page, pages));
            if (limit <= job.cursor) { job.cursor = AlignUp(page + PageSize, job.align); continue; }
        }
        const uintptr_t last = std::min(limit, chunkEnd);
        uintptr_t stop = last;
        if (!ScanSlice(job, job.cursor, last, limit, stop)) stop = regionEnd; // Freed meanwhile.
        job.cursor = stop > job.cursor ? stop : AlignUp(last, job.align);
    }
    return true;
}
void Answer(const SearchJob& job, Response& response) {
    memcpy(response.data, job.found, job.hits * sizeof(uint64_t));
    response.length = job.hits * static_cast<uint32_t>(sizeof(uint64_t));
    response.detail = job.cursor < job.end ? job.cursor : 0;
}

DWORD WINAPI SearchWorker(void*) {
    for (;;) {
        WaitForSingleObject(g_workerWake, INFINITE);
        if (g_workerState.load(std::memory_order_acquire) != WorkerRunning) continue;
        RunJob(*g_background, 0, &g_workerCancel);
        g_workerState.store(WorkerDone, std::memory_order_release);
    }
}
// Starts the worker once; false if the host forbids it or it cannot start.
bool EnsureWorker(const Host& host) {
    if (g_workerWake) return true;
    if (!host.backgroundSearch || g_workerFailed) return false;
    g_workerFailed = true;
    g_background = static_cast<SearchJob*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SearchJob)));
    HANDLE wake = g_background ? CreateEventW(nullptr, FALSE, FALSE, nullptr) : nullptr;
    HANDLE thread = wake ? CreateThread(nullptr, 64 * 1024, &SearchWorker, nullptr, 0, nullptr) : nullptr;
    if (!thread) {
        if (wake) CloseHandle(wake);
        if (g_background) HeapFree(GetProcessHeap(), 0, g_background);
        g_background = nullptr;
        return false;
    }
    CloseHandle(thread); // Runs for the process; the agent never unloads.
    g_workerWake = wake;
    g_workerFailed = false;
    return true;
}
// Background phases across ticks: a previous job (abandoned by the client)
// is cancelled first, then this one starts and the handler waits for it.
enum class Phase { Draining, Running };
Phase g_phase;
Progress BackgroundSearch(const Request& request, Response& response, bool first, const Host& host) {
    if (first) {
        if (!EnsureWorker(host)) {
            Refuse(response, Status::Refused, host.backgroundSearch ? "background search thread unavailable" :
                "host does not allow background search");
            return Progress::Done;
        }
        g_phase = Phase::Draining;
        if (g_workerState.load(std::memory_order_acquire) == WorkerRunning) g_workerCancel.store(true);
    }
    if (g_phase == Phase::Draining) {
        if (g_workerState.load(std::memory_order_acquire) == WorkerRunning) return Progress::Continue;
        g_workerCancel.store(false);
        StartJob(*g_background, request);
        g_workerState.store(WorkerRunning, std::memory_order_release);
        SetEvent(g_workerWake);
        g_phase = Phase::Running;
        return Progress::Continue;
    }
    if (g_workerState.load(std::memory_order_acquire) != WorkerDone) return Progress::Continue;
    Answer(*g_background, response);
    g_workerState.store(WorkerIdle, std::memory_order_release);
    return Progress::Done;
}
}

Progress Search(const Request& request, Response& response, bool first, const Host& host) {
    if (first && (response.status = Check(request)) != Status::Ok) return Progress::Done;
    if (request.flags & SearchBackground) return BackgroundSearch(request, response, first, host);
    if (first) {
        if (!g_job) g_job = static_cast<SearchJob*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SearchJob)));
        if (!g_job) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
        StartJob(*g_job, request);
    }
    if (!RunJob(*g_job, Microseconds() + SearchSliceMicroseconds, nullptr)) return Progress::Continue;
    Answer(*g_job, response);
    return Progress::Done;
}

Progress Region(const Request& request, Response& response, bool, const Host&) {
    if ((response.status = Check(request)) != Status::Ok) return Progress::Done;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(request.address), &info, sizeof(info))) {
        Refuse(response, Status::NotFound, "outside the address space", GetLastError());
        return Progress::Done;
    }
    RegionInfo region{reinterpret_cast<uint64_t>(info.BaseAddress), info.RegionSize,
        reinterpret_cast<uint64_t>(info.AllocationBase), info.State, info.Protect, info.AllocationProtect, info.Type};
    memcpy(response.data, &region, sizeof(region));
    response.length = sizeof(region);
    return Progress::Done;
}

Progress Xrefs(const Request& request, Response& response, bool first, const Host& host) {
    return Search(request, response, first, host); // Check and StartJob tell the ops apart.
}

namespace {
// Pins and the watch run at the start of every tick, before requests, so
// they keep their cadence while a long request is in flight.
struct PinSlot {
    PinState state;
    uint64_t endMs;
    bool used;
};
PinSlot* g_pins; // MaxPins, heap.
uint32_t g_nextPinId;

struct WatchState {
    WatchSpec spec;
    uint32_t rowSize, rows, maxRows;
    int64_t startUs, endUs, nextUs;
    bool running;
    WatchEnd stop;
    // Following a root: its current object and identity, whether one is
    // known, whether the lost row is written, and whether the next row is
    // the first on a new object.
    uint64_t rootAddress;
    Guard rootGuard;
    bool rootKnown, lostWritten, resumed;
    uint8_t* buffer; // WatchBufferBytes, kept once allocated.
};
WatchState* g_watch;

template<class T> T* HeapNew(size_t count = 1) {
    return static_cast<T*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(T) * count));
}
bool GuardHolds(const Host& host, const Guard& guard) {
    if (!guard.address) return true;
    uint8_t now[MaxGuard];
    const void* where = reinterpret_cast<const void*>(guard.address);
    return host.readable(where, guard.length) && GuardedCopy(now, where, guard.length) &&
        !memcmp(now, guard.bytes, guard.length);
}
// One write of a pin; stops it for good when it can no longer apply.
void ApplyPin(const Host& host, PinSlot& slot) {
    PinState& pin = slot.state;
    if (pin.stop != PinStop::Active) return;
    const uint64_t now = GetTickCount64();
    pin.remaining = now < slot.endMs ? static_cast<uint32_t>(slot.endMs - now) : 0;
    const PinSpec& spec = pin.spec;
    void* target = reinterpret_cast<void*>(spec.address);
    if (!pin.remaining) { pin.stop = PinStop::Expired; return; }
    if (!GuardHolds(host, spec.guard)) { pin.stop = PinStop::GuardChanged; return; }
    if (!Writable(spec.address, spec.length)) { pin.stop = PinStop::NotWritable; return; }
    if (!pin.writes && spec.hasExpected) {
        uint8_t current[MaxPinBytes];
        if (!GuardedCopy(current, target, spec.length)) { pin.stop = PinStop::Faulted; return; }
        if (memcmp(current, spec.expected, spec.length)) { pin.stop = PinStop::Unexpected; return; }
    }
    if (!GuardedCopy(target, spec.bytes, spec.length)) { pin.stop = PinStop::Faulted; return; }
    ++pin.writes;
}
void EndWatch(WatchState& watch, WatchEnd stop) {
    watch.running = false;
    watch.stop = stop;
}
// Following a root: true while its object is known (asked again once its
// guard fails); a new object marks the next row resumed.
bool FindRoot(const Host& host, WatchState& watch) {
    if (watch.rootKnown && GuardHolds(host, watch.rootGuard)) return true;
    watch.rootKnown = false;
    uint64_t address = 0;
    Guard guard{};
    if (!host.root || !host.root(host, watch.spec.root, address, guard) || !address || !GuardValid(guard)) return false;
    watch.resumed = watch.rows != 0; // The first object of a capture is no resumption.
    watch.rootAddress = address;
    watch.rootGuard = guard;
    watch.rootKnown = true;
    watch.lostWritten = false;
    return true;
}
void SampleWatch(const Host& host) {
    WatchState& watch = *g_watch;
    const int64_t now = Microseconds();
    if (now >= watch.endUs) return EndWatch(watch, WatchEnd::Deadline);
    if (now < watch.nextUs) return;
    // The next sample: one interval on, or now when the ticks fell behind.
    watch.nextUs = std::max(watch.nextUs + int64_t{watch.spec.interval} * 1000, now);
    if (watch.rows >= watch.maxRows) return EndWatch(watch, WatchEnd::BufferFull);
    const bool follow = watch.spec.root[0] != 0;
    WatchRowHeader header{std::atomic_ref(g_section->heartbeat).load(std::memory_order_relaxed),
        static_cast<uint64_t>(now - watch.startUs), 0, 0, 0};
    uint8_t* row = watch.buffer + static_cast<size_t>(watch.rows) * watch.rowSize;
    if (follow && !FindRoot(host, watch)) {
        // One lost row marks the gap; nothing is sampled until the root is back.
        if (watch.lostWritten) return;
        memset(row, 0, watch.rowSize);
        header.flags = WatchRowLost;
        memcpy(row, &header, sizeof(header));
        watch.lostWritten = true;
        ++watch.rows;
        return;
    }
    if (follow) {
        header.root = watch.rootAddress;
        if (watch.resumed) header.flags = WatchRowResumed;
        watch.resumed = false;
    }
    size_t at = sizeof(header);
    for (uint32_t i = 0; i < watch.spec.count; ++i) {
        const WatchRange& range = watch.spec.ranges[i];
        if (!GuardHolds(host, range.guard)) return EndWatch(watch, WatchEnd::ObjectReplaced); // Its object is gone.
        const void* source = reinterpret_cast<const void*>(follow ? watch.rootAddress + range.address : range.address);
        uint8_t* destination = row + at;
        const size_t padded = (range.length + 7) & ~size_t{7};
        memset(destination, 0, padded);
        if (host.readable(source, range.length) && GuardedCopy(destination, source, range.length))
            header.readable |= 1u << i;
        else
            memset(destination, 0, padded);
        at += padded;
    }
    memcpy(row, &header, sizeof(header));
    ++watch.rows;
}
// Whether the host offers a root by this name.
bool HostRoot(const Host& host, const char* name) {
    if (!host.root || !host.rootNames) return false;
    for (const char* offered = host.rootNames; *offered; offered += strlen(offered) + 1)
        if (!strcmp(offered, name)) return true;
    return false;
}
// Trials (protocol.h): one write each, undone by the agent.
struct TrialSlot {
    TrialState state;
    uint64_t endMs;
    bool used;
};
TrialSlot* g_trials; // MaxTrials, heap.
uint32_t g_nextTrialId;

uint64_t Heartbeat() { return std::atomic_ref(g_section->heartbeat).load(std::memory_order_relaxed); }
// Inside a loaded module's image (code, vtables, static data).
bool InModule(uint64_t address) {
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(address), &module) && module;
}
// The guards of a trial; None when all hold.
TrialGuard TrialGuardFailed(const Host& host, const TrialState& trial) {
    if (!GuardHolds(host, trial.spec.owner)) return TrialGuard::Owner;
    if (trial.spec.object) {
        uint64_t vtable = 0;
        const void* object = reinterpret_cast<const void*>(trial.spec.object);
        if (!host.readable(object, sizeof(vtable)) || !GuardedCopy(&vtable, object, sizeof(vtable)) ||
            vtable != trial.vtable) return TrialGuard::Object;
    }
    if (!host.readable(reinterpret_cast<const void*>(trial.spec.address), trial.spec.length) ||
        !Writable(trial.spec.address, trial.spec.length)) return TrialGuard::Unreadable;
    return TrialGuard::None;
}
void EndTrial(TrialState& trial, TrialStop stop, TrialGuard guard = TrialGuard::None) {
    trial.stop = stop;
    trial.guard = guard;
    trial.remaining = 0;
    trial.changedTick = Heartbeat();
}
// One check of an active trial; restores when `restore` (expiry or clear).
void CheckTrial(const Host& host, TrialSlot& slot, bool restore) {
    TrialState& trial = slot.state;
    if (trial.stop != TrialStop::Active) return;
    const uint64_t now = GetTickCount64();
    trial.remaining = now < slot.endMs ? static_cast<uint32_t>(slot.endMs - now) : 0;
    if (const auto failed = TrialGuardFailed(host, trial); failed != TrialGuard::None)
        return EndTrial(trial, TrialStop::Abandoned, failed);
    void* target = reinterpret_cast<void*>(trial.spec.address);
    uint8_t current[MaxTrialBytes];
    if (!GuardedCopy(current, target, trial.spec.length)) return EndTrial(trial, TrialStop::Abandoned, TrialGuard::Unreadable);
    if (memcmp(current, trial.spec.bytes, trial.spec.length)) return EndTrial(trial, TrialStop::Conflict);
    if (!restore && trial.remaining) return;
    if (!GuardedCopy(target, trial.original, trial.spec.length))
        return EndTrial(trial, TrialStop::Abandoned, TrialGuard::Unreadable);
    EndTrial(trial, TrialStop::Restored);
}

void TickServices(const Host& host) {
    if (g_pins)
        for (size_t i = 0; i < MaxPins; ++i)
            if (g_pins[i].used) ApplyPin(host, g_pins[i]);
    if (g_trials)
        for (size_t i = 0; i < MaxTrials; ++i)
            if (g_trials[i].used) CheckTrial(host, g_trials[i], false);
    if (g_watch && g_watch->running) SampleWatch(host);
}
}

Progress Trial(const Request& request, Response& response, bool, const Host& host) {
    if ((response.status = Check(request)) != Status::Ok) return Progress::Done;
    if (!g_trials && !(g_trials = HeapNew<TrialSlot>(MaxTrials))) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
    switch (static_cast<TrialAction>(request.flags)) {
    case TrialAction::Add: {
        TrialSlot* slot = nullptr;
        for (size_t i = 0; i < MaxTrials && !slot; ++i) if (!g_trials[i].used) slot = &g_trials[i];
        for (size_t i = 0; i < MaxTrials && !slot; ++i) if (g_trials[i].state.stop != TrialStop::Active) slot = &g_trials[i];
        if (!slot) { Refuse(response, Status::Refused, "all trials active; clear one first"); return Progress::Done; }
        TrialState trial{};
        memcpy(&trial.spec, request.data, sizeof(TrialSpec));
        const TrialSpec& spec = trial.spec;
        if (InModule(spec.address)) { Refuse(response, Status::Refused, "target inside a loaded module"); return Progress::Done; }
        if (spec.object) {
            const void* object = reinterpret_cast<const void*>(spec.object);
            if (!host.readable(object, sizeof(trial.vtable)) || !GuardedCopy(&trial.vtable, object, sizeof(trial.vtable)) ||
                !InModule(trial.vtable)) {
                Refuse(response, Status::Refused, "object's first 8 bytes are not a vtable in a loaded module");
                return Progress::Done;
            }
        }
        if (const auto failed = TrialGuardFailed(host, trial); failed != TrialGuard::None) {
            Refuse(response, Status::Refused, failed == TrialGuard::Owner ? "owner guard bytes differ" :
                failed == TrialGuard::Object ? "object vtable changed" : "target not readable and writable");
            return Progress::Done;
        }
        void* target = reinterpret_cast<void*>(spec.address);
        if (!GuardedCopy(trial.original, target, spec.length)) { Refuse(response, Status::Refused, "target faulted"); return Progress::Done; }
        if (memcmp(trial.original, spec.expected, spec.length)) {
            memcpy(response.data, trial.original, spec.length);
            response.length = spec.length;
            Refuse(response, Status::Refused, "memory differs from the expected bytes");
            return Progress::Done;
        }
        if (!GuardedCopy(target, spec.bytes, spec.length)) {
            GuardedCopy(target, trial.original, spec.length); // A partial copy cannot be told apart; put back what was read.
            Refuse(response, Status::Refused, "write faulted");
            return Progress::Done;
        }
        trial.id = ++g_nextTrialId ? g_nextTrialId : ++g_nextTrialId;
        trial.changedTick = Heartbeat();
        trial.remaining = spec.milliseconds;
        *slot = {trial, GetTickCount64() + spec.milliseconds, true};
        memcpy(response.data, &slot->state, sizeof(TrialState));
        response.length = sizeof(TrialState);
        response.detail = trial.id;
        return Progress::Done;
    }
    case TrialAction::List: {
        uint32_t count = 0;
        const uint64_t now = GetTickCount64();
        for (size_t i = 0; i < MaxTrials; ++i) {
            if (!g_trials[i].used) continue;
            TrialState& state = g_trials[i].state;
            if (state.stop == TrialStop::Active) state.remaining = now < g_trials[i].endMs ? static_cast<uint32_t>(g_trials[i].endMs - now) : 0;
            memcpy(response.data + count++ * sizeof(TrialState), &state, sizeof(TrialState));
        }
        response.length = count * static_cast<uint32_t>(sizeof(TrialState));
        response.detail = count;
        return Progress::Done;
    }
    case TrialAction::Clear: {
        uint32_t cleared = 0;
        for (size_t i = 0; i < MaxTrials; ++i) {
            TrialSlot& slot = g_trials[i];
            if (!slot.used || (request.args[0] && slot.state.id != request.args[0])) continue;
            CheckTrial(host, slot, true); // Active ones restore now.
            memcpy(response.data + cleared++ * sizeof(TrialState), &slot.state, sizeof(TrialState));
            slot = {};
        }
        response.length = cleared * static_cast<uint32_t>(sizeof(TrialState));
        response.detail = cleared;
        if (request.args[0] && !cleared) Refuse(response, Status::NotFound, "no trial with that id");
        return Progress::Done;
    }
    }
    response.status = Status::BadRequest;
    return Progress::Done;
}

Progress Pin(const Request& request, Response& response, bool, const Host& host) {
    if ((response.status = Check(request)) != Status::Ok) return Progress::Done;
    if (!g_pins && !(g_pins = HeapNew<PinSlot>(MaxPins))) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
    switch (static_cast<PinAction>(request.flags)) {
    case PinAction::Add: {
        // A free slot, else the first stopped one.
        PinSlot* slot = nullptr;
        for (size_t i = 0; i < MaxPins && !slot; ++i) if (!g_pins[i].used) slot = &g_pins[i];
        for (size_t i = 0; i < MaxPins && !slot; ++i) if (g_pins[i].state.stop != PinStop::Active) slot = &g_pins[i];
        if (!slot) { Refuse(response, Status::Refused, "all pins active; clear one first"); return Progress::Done; }
        *slot = {};
        slot->used = true;
        memcpy(&slot->state.spec, request.data, sizeof(PinSpec));
        slot->state.id = ++g_nextPinId ? g_nextPinId : ++g_nextPinId;
        slot->endMs = GetTickCount64() + slot->state.spec.milliseconds;
        ApplyPin(host, *slot); // At once, so a refusal reaches the client.
        memcpy(response.data, &slot->state, sizeof(PinState));
        response.length = sizeof(PinState);
        response.detail = slot->state.id;
        if (slot->state.stop != PinStop::Active)
            Refuse(response, Status::Refused, slot->state.stop == PinStop::Unexpected ? "memory differs from the expected bytes" :
                slot->state.stop == PinStop::GuardChanged ? "guard bytes differ" : "memory not writable", slot->state.id);
        return Progress::Done;
    }
    case PinAction::List: {
        uint32_t count = 0;
        for (size_t i = 0; i < MaxPins; ++i) {
            if (!g_pins[i].used) continue;
            PinState& state = g_pins[i].state;
            const uint64_t now = GetTickCount64();
            if (state.stop == PinStop::Active) state.remaining = now < g_pins[i].endMs ? static_cast<uint32_t>(g_pins[i].endMs - now) : 0;
            memcpy(response.data + count++ * sizeof(PinState), &state, sizeof(PinState));
        }
        response.length = count * static_cast<uint32_t>(sizeof(PinState));
        response.detail = count;
        return Progress::Done;
    }
    case PinAction::Clear: {
        uint32_t cleared = 0;
        for (size_t i = 0; i < MaxPins; ++i)
            if (g_pins[i].used && (!request.args[0] || g_pins[i].state.id == request.args[0])) { g_pins[i] = {}; ++cleared; }
        response.detail = cleared;
        if (request.args[0] && !cleared) Refuse(response, Status::NotFound, "no pin with that id");
        return Progress::Done;
    }
    }
    response.status = Status::BadRequest;
    return Progress::Done;
}

Progress Watch(const Request& request, Response& response, bool, const Host& host) {
    if ((response.status = Check(request)) != Status::Ok) return Progress::Done;
    switch (static_cast<WatchAction>(request.flags)) {
    case WatchAction::Start: {
        WatchSpec spec;
        memcpy(&spec, request.data, sizeof(spec));
        if (spec.root[0] && !HostRoot(host, spec.root)) {
            Refuse(response, Status::NotFound, host.root ? "the host offers no root by that name" : "the host offers no roots");
            return Progress::Done;
        }
        if (!WatchFits(spec)) {
            Refuse(response, Status::Refused, "the capture does not fit the buffer");
            response.detail = WatchLongestFit(spec);
            return Progress::Done;
        }
        if (!g_watch && !(g_watch = HeapNew<WatchState>())) { Refuse(response, Status::Refused, "out of memory"); return Progress::Done; }
        if (!g_watch->buffer && !(g_watch->buffer = HeapNew<uint8_t>(WatchBufferBytes))) {
            Refuse(response, Status::Refused, "out of memory");
            return Progress::Done;
        }
        WatchState& watch = *g_watch;
        uint8_t* buffer = watch.buffer;
        watch = {};
        watch.buffer = buffer;
        watch.spec = spec;
        watch.rowSize = WatchRowSize(spec);
        watch.maxRows = WatchMaxRows(spec);
        watch.startUs = watch.nextUs = Microseconds();
        watch.endUs = watch.startUs + int64_t{spec.milliseconds} * 1000;
        watch.running = true;
        response.detail = watch.maxRows;
        return Progress::Done;
    }
    case WatchAction::Info: {
        if (!g_watch || !g_watch->rowSize) { Refuse(response, Status::NotFound, "no watch started"); return Progress::Done; }
        const WatchInfo info{g_watch->spec, g_watch->rows, g_watch->maxRows, g_watch->stop, 0};
        memcpy(response.data, &info, sizeof(info));
        response.length = sizeof(info);
        return Progress::Done;
    }
    case WatchAction::Roots: {
        size_t length = 0;
        if (host.root && host.rootNames)
            for (const char* name = host.rootNames; *name && length + strlen(name) + 1 <= MaxRead; name += strlen(name) + 1) {
                memcpy(response.data + length, name, strlen(name) + 1);
                length += strlen(name) + 1;
            }
        response.length = static_cast<uint32_t>(length);
        return Progress::Done;
    }
    case WatchAction::Read: {
        if (!g_watch || !g_watch->rowSize) { Refuse(response, Status::NotFound, "no watch started"); return Progress::Done; }
        const WatchState& watch = *g_watch;
        const uint64_t first = request.args[0];
        if (first > watch.rows) { response.status = Status::BadRequest; return Progress::Done; }
        const uint32_t count = std::min<uint32_t>(watch.rows - static_cast<uint32_t>(first),
            static_cast<uint32_t>(MaxRead / watch.rowSize));
        memcpy(response.data, watch.buffer + first * watch.rowSize, size_t{count} * watch.rowSize);
        response.length = count * watch.rowSize;
        response.detail = watch.rows | (watch.running ? WatchRunning : 0);
        return Progress::Done;
    }
    case WatchAction::Stop:
        if (g_watch && g_watch->running) EndWatch(*g_watch, WatchEnd::Stopped);
        response.detail = g_watch ? g_watch->rows : 0;
        return Progress::Done;
    }
    response.status = Status::BadRequest;
    return Progress::Done;
}

namespace {
// A handler's C++ exception (a container out of memory) must not unwind into
// the host's frame: it is answered as a refusal. Access faults stay with each
// handler's own guards; this does not catch them.
Progress Invoke(const Command& command, bool first, const Host& host) {
    try {
        return command.run(g_state->request, g_state->response, first, host);
    } catch (...) {
        g_state->response.length = 0;
        Refuse(g_state->response, Status::Refused, "handler exception");
        return Progress::Done;
    }
}

// Starts the request at seq (an unfinished one for an older seq is
// abandoned). False while it continues on later ticks.
bool RunNew(const Host& host, std::span<const Command> commands, uint32_t seq) {
    g_active = nullptr;
    memcpy(&g_state->request, &g_section->request, sizeof(Request));
    g_state->response = {};
    g_state->response.status = Status::Ok;
    const Command* command = nullptr;
    for (const Command& candidate : commands)
        if (candidate.op == g_state->request.op && candidate.run) { command = &candidate; break; }
    if (!command) {
        g_state->response.status = Status::Unsupported;
        Finish(host, seq, "unknown");
        return true;
    }
    if (Invoke(*command, true, host) == Progress::Continue) {
        g_active = command;
        g_activeSeq = seq;
        return false;
    }
    Finish(host, seq, command->name, command->logSuccess);
    return true;
}
}

void Tick(const Host& host, std::span<const Command> commands) {
    if (!g_section && (g_openFailed || !Open(host, commands))) return;
    if (g_dormant) return;
    if (std::atomic_ref(g_section->owner).load(std::memory_order_acquire) != g_agentId) {
        g_dormant = true; // Replaced by a newer agent in this process; for good.
        // Its active trials restore once, under the same guards as at expiry:
        // nothing else would ever undo them.
        if (g_trials)
            for (size_t i = 0; i < MaxTrials; ++i)
                if (g_trials[i].used) CheckTrial(host, g_trials[i], true);
        Log(host, LogLevel::Info, 0, "Dev agent: a newer agent in this process took over; this one stops");
        return;
    }
    std::atomic_ref(g_section->heartbeat).fetch_add(1, std::memory_order_relaxed);
    if (host.thread != HostThread::Unset)
        std::atomic_ref(g_section->hostThread).store(static_cast<uint32_t>(host.thread), std::memory_order_relaxed);
    TickServices(host);
    uint32_t seq = std::atomic_ref(g_section->requestSeq).load(std::memory_order_acquire);
    if (seq == g_handled) return;
    if (g_active && g_activeSeq == seq) {
        if (Invoke(*g_active, false, host) == Progress::Continue) return;
        Finish(host, seq, g_active->name, g_active->logSuccess);
    } else if (!RunNew(host, commands, seq)) {
        return;
    }
    // Hold: the client sends more; wait for each next request within the
    // budgets and run it in this tick. The tick budget counts waits and
    // handlers after the first request.
    const int64_t tickEnd = Microseconds() + host.holdTickMicroseconds;
    while (g_state->request.hold == RequestHold && host.holdWaitMicroseconds && host.holdTickMicroseconds) {
        const int64_t now = Microseconds();
        const int64_t waitEnd = std::min(now + int64_t{host.holdWaitMicroseconds}, tickEnd);
        while ((seq = std::atomic_ref(g_section->requestSeq).load(std::memory_order_acquire)) == g_handled &&
               Microseconds() < waitEnd)
            YieldProcessor();
        if (seq == g_handled || !RunNew(host, commands, seq) || Microseconds() >= tickEnd) return;
    }
}
}
