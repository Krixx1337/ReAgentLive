// reagentlive core agent: memory search and xrefs, in host-tick slices or on
// the worker thread, and the search job that runs while other requests do.
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
#include <cstring>
#include "reagentlive/core/agent.h"
#include "reagentlive/core/agent_internal.h"

namespace reagentlive {
namespace detail {
int64_t Microseconds() {
    LARGE_INTEGER frequency, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    return now.QuadPart / frequency.QuadPart * 1000000 + now.QuadPart % frequency.QuadPart * 1000000 / frequency.QuadPart;
}
}

namespace {
using detail::Microseconds;
constexpr uintptr_t PageSize = 4096;
constexpr size_t SearchChunkPages = 64; // 256 KB between clock and cancel checks.

// One search, self-contained: the background worker must not read the shared
// request slot, which the next request overwrites. `hits` and `progress` are
// published with atomic_ref for the job's status, read while the worker runs,
// and so are `searchable` and `behind`.
struct SearchJob {
    uint8_t pattern[MaxSearchPattern];
    uint32_t length, align, flags, maxHits, hits;
    uintptr_t start, cursor, end, progress;
    // A job first measures its range's searchable bytes (`measured`: how far;
    // a plain search starts at end), so its status can say how far it got.
    uintptr_t measured;
    uint64_t counted, searchable, behind; // Counted so far, the total once measured, and those behind the cursor.
    bool relative;   // Xrefs: 4-byte fields landing on `target` (protocol.h).
    uint64_t target;
    uint64_t found[MaxSearchHits];
};
SearchJob* g_job;        // Game-thread search (heap, like State).
SearchJob* g_background; // Background search, owned by the worker while it runs.
SearchJob* g_searchJob;  // The search job (SearchJobStart), on the worker or in tick slices.

// The job's answer: its number, state, and whether the worker runs it.
struct JobState {
    uint32_t id;
    SearchJobState state;
    bool onWorker;
};
JobState g_jobState;

// Background worker: started once, on the first background search or job a
// host allows; it lives for the process, like the agent. Idle -> Running
// (agent) -> Done (worker) -> Idle (agent, after taking the result).
// g_workerJob is the search it runs.
enum : uint32_t { WorkerIdle, WorkerRunning, WorkerDone };
std::atomic<uint32_t> g_workerState{WorkerIdle};
std::atomic<bool> g_workerCancel{false};
// What the worker runs: a search (g_workerJob), or the image copy (g_copy),
// which goes back to Idle by itself and says it finished in g_copyDone.
enum : uint32_t { TaskSearch, TaskCopy };
std::atomic<uint32_t> g_workerTask{TaskSearch};
std::atomic<bool> g_copyDone{false};

// The image copy: [start, end) into the image window from its first byte,
// with one bit per page from start's page, set where it was copied.
struct CopyJob {
    uintptr_t start, end, cursor;
    uint8_t copied[ImageWindowPages / 8 + 1];
};
CopyJob g_copy;
HANDLE g_imageMapping;
uint8_t* g_imageView;
bool g_imageFailed;
uint32_t g_copiesOnWorker;
SearchJob* g_workerJob;
HANDLE g_workerWake;
bool g_workerFailed;
uint32_t g_threadsStarted; // Every thread the agent starts goes through EnsureWorker.

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
    return detail::InAgentState(address) || inside(g_imageView, ImageWindowBytes) || inside(g_job, sizeof(SearchJob)) ||
        inside(g_background, sizeof(SearchJob)) || inside(g_searchJob, sizeof(SearchJob)) || SectionView(address);
}
// Records a match; true once the page of hits is full.
bool Hit(SearchJob& job, uintptr_t at) {
    if (OwnBuffer(at)) return false;
    job.found[job.hits] = at;
    std::atomic_ref(job.hits).store(job.hits + 1, std::memory_order_release);
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
    job.start = job.cursor = job.progress = AlignUp(start, job.align);
    job.end = request.args[0] ? std::min<uintptr_t>(static_cast<uintptr_t>(request.args[0]), top) : top;
    job.measured = request.flags & SearchJobStart ? job.start : job.end;
    job.counted = job.searchable = job.behind = 0;
}
// A job's searchable bytes in its range, in the same slices as the scan:
// false while `deadline` stops it. Regions change meanwhile, so the total is
// as the walk found them.
bool Measure(SearchJob& job, int64_t deadline, const std::atomic<bool>* cancel) {
    while (job.measured < job.end) {
        if (deadline && Microseconds() >= deadline) return false;
        if (cancel && cancel->load(std::memory_order_relaxed)) return true;
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(job.measured), &info, sizeof(info))) break;
        const uintptr_t regionEnd = std::min(reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize, job.end);
        if (regionEnd <= job.measured) break;
        if (Searchable(info, job.flags)) job.counted += regionEnd - job.measured;
        job.measured = regionEnd;
    }
    job.measured = job.end;
    std::atomic_ref(job.searchable).store(job.counted, std::memory_order_relaxed);
    return true;
}
void Behind(SearchJob& job, uintptr_t from, uintptr_t to) {
    if (to > from) std::atomic_ref(job.behind).store(job.behind + (to - from), std::memory_order_relaxed);
}
// Scans until the range is done, the page of hits is full, `deadline` passes
// (0: none) or `cancel` is set. True once the job is finished.
bool RunJob(SearchJob& job, int64_t deadline, const std::atomic<bool>* cancel) {
    const bool paged = job.flags & SearchPaged;
    if (!Measure(job, deadline, cancel)) return false;
    while (job.cursor < job.end && job.hits < job.maxHits) {
        std::atomic_ref(job.progress).store(job.cursor, std::memory_order_relaxed);
        if (deadline && Microseconds() >= deadline) return false;
        if (cancel && cancel->load(std::memory_order_relaxed)) return true;
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(job.cursor), &info, sizeof(info))) { job.cursor = job.end; break; }
        const uintptr_t regionEnd = std::min(reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize, job.end);
        if (regionEnd <= job.cursor) { job.cursor = job.end; break; }
        if (!Searchable(info, job.flags)) { job.cursor = regionEnd; continue; }
        const uintptr_t before = job.cursor;
        // One chunk, then the clock again. The residency check looks one page
        // past the chunk so a match crossing into it is still found.
        const uintptr_t page = job.cursor & ~(PageSize - 1);
        const uintptr_t chunkEnd = std::min(regionEnd, page + SearchChunkPages * PageSize);
        uintptr_t limit = regionEnd;
        if (!paged) {
            const size_t pages = (std::min(regionEnd, chunkEnd + PageSize) - page + PageSize - 1) / PageSize;
            limit = std::min(regionEnd, ResidentEnd(page, pages));
            if (limit <= job.cursor) {
                job.cursor = AlignUp(page + PageSize, job.align);
                Behind(job, before, std::min(job.cursor, regionEnd));
                continue;
            }
        }
        const uintptr_t last = std::min(limit, chunkEnd);
        uintptr_t stop = last;
        if (!ScanSlice(job, job.cursor, last, limit, stop)) stop = regionEnd; // Freed meanwhile.
        job.cursor = stop > job.cursor ? stop : AlignUp(last, job.align);
        Behind(job, before, std::min(job.cursor, regionEnd));
    }
    std::atomic_ref(job.progress).store(job.cursor, std::memory_order_relaxed);
    return true;
}
void Answer(const SearchJob& job, Response& response) {
    memcpy(response.data, job.found, job.hits * sizeof(uint64_t));
    response.length = job.hits * static_cast<uint32_t>(sizeof(uint64_t));
    response.detail = job.cursor < job.end ? job.cursor : 0;
}

// Copies image pages until the range is done, `deadline` passes (0: none)
// or `cancel` is set; true once finished. Pages outside an image, or not
// readable, stay zero in the window and unmarked.
bool RunCopy(CopyJob& job, int64_t deadline, const std::atomic<bool>* cancel) {
    const uintptr_t first = job.start & ~(PageSize - 1);
    const auto mark = [&](uintptr_t from, uintptr_t to) {
        for (uintptr_t page = from & ~(PageSize - 1); page < to; page += PageSize) {
            const size_t index = (page - first) / PageSize;
            job.copied[index / 8] = static_cast<uint8_t>(job.copied[index / 8] | (1u << (index % 8)));
        }
    };
    while (job.cursor < job.end) {
        if (deadline && Microseconds() >= deadline) return false;
        if (cancel && cancel->load(std::memory_order_relaxed)) return true;
        const uintptr_t at = job.cursor;
        MEMORY_BASIC_INFORMATION info{};
        const bool known = VirtualQuery(reinterpret_cast<const void*>(at), &info, sizeof(info)) != 0;
        const uintptr_t regionEnd = known ?
            std::min(reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize, job.end) : job.end;
        const uintptr_t stop = std::max(at + 1, std::min(regionEnd, (at & ~(PageSize - 1)) + SearchChunkPages * PageSize));
        uint8_t* out = g_imageView + (at - job.start);
        if (known && info.Type == MEM_IMAGE && Searchable(info, 0)) {
            if (GuardedCopy(out, reinterpret_cast<const void*>(at), stop - at)) {
                mark(at, stop);
            } else { // Freed meanwhile: page by page.
                for (uintptr_t page = at; page < stop;) {
                    const uintptr_t next = std::min(stop, (page & ~(PageSize - 1)) + PageSize);
                    uint8_t* to = g_imageView + (page - job.start);
                    if (GuardedCopy(to, reinterpret_cast<const void*>(page), next - page)) mark(page, next);
                    else memset(to, 0, next - page);
                    page = next;
                }
            }
        } else {
            memset(out, 0, stop - at);
        }
        job.cursor = stop;
    }
    return true;
}

DWORD WINAPI SearchWorker(void*) {
    for (;;) {
        WaitForSingleObject(g_workerWake, INFINITE);
        if (g_workerState.load(std::memory_order_acquire) != WorkerRunning) continue;
        if (g_workerTask.load(std::memory_order_acquire) == TaskCopy) {
            RunCopy(g_copy, 0, &g_workerCancel);
            g_copyDone.store(true, std::memory_order_release);
            g_workerState.store(WorkerIdle, std::memory_order_release);
            continue;
        }
        RunJob(*g_workerJob, 0, &g_workerCancel);
        g_workerState.store(WorkerDone, std::memory_order_release);
    }
}
// Starts the worker once; false if the host forbids it (checked on every
// use, so a started worker takes nothing once the opt-in is off) or it
// cannot start.
bool EnsureWorker(const Host& host) {
    if (!host.backgroundSearch) return false;
    if (g_workerWake) return true;
    if (g_workerFailed) return false;
    g_workerFailed = true;
    HANDLE wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE thread = wake ? CreateThread(nullptr, 64 * 1024, &SearchWorker, nullptr, 0, nullptr) : nullptr;
    if (!thread) {
        if (wake) CloseHandle(wake);
        return false;
    }
    CloseHandle(thread); // Runs for the process; the agent never unloads.
    ++g_threadsStarted;
    g_workerWake = wake;
    g_workerFailed = false;
    return true;
}
void RunOnWorker(SearchJob& job) {
    g_workerCancel.store(false);
    g_workerJob = &job;
    g_workerTask.store(TaskSearch, std::memory_order_relaxed);
    g_workerState.store(WorkerRunning, std::memory_order_release);
    SetEvent(g_workerWake);
}
bool CopyOnWorker() {
    return g_workerTask.load(std::memory_order_relaxed) == TaskCopy &&
        g_workerState.load(std::memory_order_acquire) == WorkerRunning;
}
bool JobOnWorker() {
    return g_jobState.onWorker && g_jobState.state == SearchJobState::Running;
}
// The worker finished the job: Done, or Stopped when cancelled.
void TakeWorkerJob() {
    if (!JobOnWorker() || g_workerJob != g_searchJob || g_workerState.load(std::memory_order_acquire) != WorkerDone) return;
    g_jobState.state = g_workerCancel.load() ? SearchJobState::Stopped : SearchJobState::Done;
    g_workerState.store(WorkerIdle, std::memory_order_release);
}

// Background phases across ticks: a previous search the worker still runs
// (abandoned by the client) is cancelled first, then this one starts and
// the handler waits for it. A running job is not cancelled: the request is
// refused, and the client scans in slices instead.
enum class Phase { Draining, Running };
Phase g_phase;
Progress BackgroundSearch(const Request& request, Response& response, bool first, const Host& host) {
    if (first) {
        if (!EnsureWorker(host)) {
            Refuse(response, Status::Refused, host.backgroundSearch ? "background search thread unavailable" :
                "host does not allow background search");
            return Progress::Done;
        }
        if (JobOnWorker()) {
            Refuse(response, Status::Refused, "background worker busy with a search job");
            return Progress::Done;
        }
        if (!g_background && !(g_background = static_cast<SearchJob*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SearchJob))))) {
            Refuse(response, Status::Refused, "out of memory");
            return Progress::Done;
        }
        g_phase = Phase::Draining;
        if (g_workerState.load(std::memory_order_acquire) == WorkerRunning) g_workerCancel.store(true);
    }
    if (g_phase == Phase::Draining) {
        if (g_workerState.load(std::memory_order_acquire) == WorkerRunning) return Progress::Continue;
        StartJob(*g_background, request);
        RunOnWorker(*g_background);
        g_phase = Phase::Running;
        return Progress::Continue;
    }
    if (g_workerState.load(std::memory_order_acquire) != WorkerDone) return Progress::Continue;
    Answer(*g_background, response);
    g_workerState.store(WorkerIdle, std::memory_order_release);
    return Progress::Done;
}

// The job's info and its hits so far.
void JobAnswer(Response& response) {
    const SearchJob& job = *g_searchJob;
    const uint32_t hits = std::atomic_ref(const_cast<uint32_t&>(job.hits)).load(std::memory_order_acquire);
    const bool running = g_jobState.state == SearchJobState::Running;
    const uint64_t searchable = std::atomic_ref(const_cast<uint64_t&>(job.searchable)).load(std::memory_order_relaxed);
    const uint64_t behind = std::atomic_ref(const_cast<uint64_t&>(job.behind)).load(std::memory_order_relaxed);
    const auto scanned = searchable ?
        static_cast<uint32_t>(static_cast<double>(std::min(behind, searchable)) / searchable * SearchScannedWhole) : 0u;
    SearchJobInfo info{g_jobState.id, static_cast<uint32_t>(g_jobState.state), hits, scanned, job.start,
        running ? std::atomic_ref(const_cast<uintptr_t&>(job.progress)).load(std::memory_order_relaxed) : job.cursor, job.end};
    memcpy(response.data, &info, sizeof(info));
    memcpy(response.data + sizeof(info), job.found, hits * sizeof(uint64_t));
    response.length = static_cast<uint32_t>(sizeof(info) + hits * sizeof(uint64_t));
    response.detail = g_jobState.id;
}

// Start: a running job is stopped first (on the worker: cancelled and
// drained across ticks), then this one starts and the answer is immediate.
Progress StartSearchJob(const Request& request, Response& response, bool first, const Host& host) {
    if (first) {
        if (!g_searchJob && !(g_searchJob = static_cast<SearchJob*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SearchJob))))) {
            Refuse(response, Status::Refused, "out of memory");
            return Progress::Done;
        }
        if (JobOnWorker()) g_workerCancel.store(true);
        else if (g_jobState.state == SearchJobState::Running) g_jobState.state = SearchJobState::Stopped;
    }
    if (JobOnWorker()) {
        TakeWorkerJob();
        if (JobOnWorker()) return Progress::Continue;
    }
    StartJob(*g_searchJob, request);
    // Only an idle worker takes it: one still finishing an abandoned
    // background search leaves the job to tick slices.
    const bool worker = EnsureWorker(host) && g_workerState.load(std::memory_order_acquire) == WorkerIdle;
    g_jobState = {g_jobState.id + 1 ? g_jobState.id + 1 : 1, SearchJobState::Running, worker};
    if (worker) RunOnWorker(*g_searchJob);
    JobAnswer(response);
    return Progress::Done;
}

// The image window: created on the first copy, kept for the process.
bool OpenImageWindow(const Host& host) {
    if (g_imageView) return true;
    if (g_imageFailed) return false;
    g_imageFailed = true;
    wchar_t name[128];
    size_t length = 0;
    for (const wchar_t* part : {host.sectionName, ImageWindowSuffix})
        for (; *part; ++part) {
            if (length + 1 >= std::size(name)) return false;
            name[length++] = *part;
        }
    name[length] = 0;
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        static_cast<DWORD>(ImageWindowBytes), name);
    void* view = mapping ? MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, ImageWindowBytes) : nullptr;
    if (!view) {
        if (mapping) CloseHandle(mapping);
        return false;
    }
    g_imageMapping = mapping;
    g_imageView = static_cast<uint8_t*>(view);
    g_imageFailed = false;
    return true;
}
void CopyAnswer(Response& response) {
    const size_t pages = (g_copy.end - 1) / PageSize - g_copy.start / PageSize + 1;
    response.length = static_cast<uint32_t>((pages + 7) / 8);
    memcpy(response.data, g_copy.copied, response.length);
    response.detail = g_copy.end - g_copy.start;
}
// An abandoned copy still on the worker is cancelled and drained first (it
// writes the window); then the copy runs on an idle worker, else in slices.
enum class CopyPhase { Draining, Worker, Ticks };
CopyPhase g_copyPhase;
Progress CopyImage(const Request& request, Response& response, bool first, const Host& host) {
    if (first) {
        if (!OpenImageWindow(host)) {
            Refuse(response, Status::Refused, "image window unavailable");
            return Progress::Done;
        }
        g_copyPhase = CopyPhase::Draining;
        if (CopyOnWorker()) g_workerCancel.store(true);
    }
    if (g_copyPhase == CopyPhase::Draining) {
        if (CopyOnWorker()) return Progress::Continue;
        g_copy.start = g_copy.cursor = static_cast<uintptr_t>(request.address);
        g_copy.end = g_copy.start + request.length;
        memset(g_copy.copied, 0, sizeof(g_copy.copied));
        if (EnsureWorker(host) && g_workerState.load(std::memory_order_acquire) == WorkerIdle) {
            ++g_copiesOnWorker;
            g_copyDone.store(false, std::memory_order_relaxed);
            g_workerCancel.store(false);
            g_workerTask.store(TaskCopy, std::memory_order_relaxed);
            g_workerState.store(WorkerRunning, std::memory_order_release);
            SetEvent(g_workerWake);
            g_copyPhase = CopyPhase::Worker;
            return Progress::Continue;
        }
        g_copyPhase = CopyPhase::Ticks;
    }
    if (g_copyPhase == CopyPhase::Worker) {
        if (!g_copyDone.load(std::memory_order_acquire)) return Progress::Continue;
    } else if (!RunCopy(g_copy, Microseconds() + SearchSliceMicroseconds, nullptr)) {
        return Progress::Continue;
    }
    CopyAnswer(response);
    return Progress::Done;
}

Progress SearchJobRequest(const Request& request, Response& response, bool first) {
    if (first && (!g_searchJob || !g_jobState.id || request.args[0] != g_jobState.id)) {
        Refuse(response, Status::NotFound, "no such search job (only the last one is kept)");
        return Progress::Done;
    }
    if (request.flags & SearchJobStop) {
        if (JobOnWorker()) {
            g_workerCancel.store(true);
            TakeWorkerJob();
            if (JobOnWorker()) return Progress::Continue;
        } else if (g_jobState.state == SearchJobState::Running) {
            g_jobState.state = SearchJobState::Stopped;
        }
    }
    TakeWorkerJob();
    JobAnswer(response);
    return Progress::Done;
}
}

namespace detail {
uint32_t ThreadsStarted() { return g_threadsStarted; }
uint32_t ImageCopiesOnWorker() { return g_copiesOnWorker; }

void TickSearchJob() {
    if (!g_searchJob || g_jobState.state != SearchJobState::Running) return;
    if (g_jobState.onWorker) { TakeWorkerJob(); return; }
    if (RunJob(*g_searchJob, Microseconds() + SearchSliceMicroseconds, nullptr)) g_jobState.state = SearchJobState::Done;
}
}

Progress Search(const Request& request, Response& response, bool first, const Host& host) {
    if (first && (response.status = Check(request)) != Status::Ok) return Progress::Done;
    if (request.flags & (SearchJobStatus | SearchJobStop)) return SearchJobRequest(request, response, first);
    if (request.flags & SearchJobStart) return StartSearchJob(request, response, first, host);
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

Progress ImageCopy(const Request& request, Response& response, bool first, const Host& host) {
    if (first && (response.status = Check(request)) != Status::Ok) return Progress::Done;
    return CopyImage(request, response, first, host);
}

Progress Xrefs(const Request& request, Response& response, bool first, const Host& host) {
    return Search(request, response, first, host); // Check and StartJob tell the ops apart.
}
}
