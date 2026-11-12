// reagentlive core: wire format between an in-process agent and its clients.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <span>
#include <string_view>

// One shared-memory section, created by the agent, opened by a client.
// Fixed-size POD only: the client writes a request and bumps requestSeq; the
// agent's tick answers and sets responseSeq to the same value. One request in
// flight. No engine, game or Windows types.
namespace reagentlive {
constexpr uint32_t Magic = 0x4b4c5644; // "DVLK"
constexpr size_t MaxRead = 4096;
constexpr size_t MaxWrite = 256;
constexpr size_t MaxText = 128;
constexpr size_t MaxReason = 64;
constexpr size_t MaxOps = 64;

// Op ranges: core 1-255, engine packs 256-4095, games 4096 and up.
namespace op {
constexpr uint32_t CoreFirst = 1, PackFirst = 256, GameFirst = 4096;
constexpr uint32_t Read = 1, Write = 2, Modules = 3, ReadBatch = 4, Search = 5, Pin = 6, Watch = 7, Trial = 8, Xrefs = 9, Region = 10;
}

// Small and generic on purpose. Pack-specific detail goes in Response::reason
// and Response::detail, not in new codes.
enum class Status : uint32_t {
    Ok, BadRequest, NotReadable, NotWritable, Faulted, NotFound, Refused, Unsupported,
};

// ReadBatch reserves one status per item in the response buffer.
struct ReadSpan { uint64_t address, length; };
constexpr size_t MaxBatchReads = 8;

// Zero means invalid. Validate the entire batch before any memory access.
inline size_t ReadBatchSize(std::span<const ReadSpan> reads) {
    if (reads.empty() || reads.size() > MaxBatchReads) return 0;
    size_t size = reads.size() * sizeof(Status);
    for (const auto& read : reads) {
        if (!read.address || !read.length || read.length > MaxRead - size ||
            read.length > UINT64_MAX - read.address) return 0;
        size += static_cast<size_t>(read.length);
    }
    return size;
}

// Search: address = first byte to scan (0 = lowest), args[0] = end, exclusive
// (0 = top of the address space), args[1] = most hits wanted, data = the byte
// pattern (length bytes), flags = alignment in the low byte (0 or 1 = any)
// plus the Search* filters below. Scans committed, readable, cacheable pages
// that are resident in the working set, in time slices across host ticks:
// reading untouched or paged-out pages would make the OS fault each one in.
// A match that spans two regions, or reaches a page outside the working set,
// is missed. Response: hit addresses (uint64 each), detail = where to resume,
// 0 once the range is done.
constexpr size_t MaxSearchPattern = 64;
constexpr size_t MaxSearchHits = MaxRead / sizeof(uint64_t);
constexpr uint32_t SearchWritable = 0x100; // Skip read-only and code pages.
constexpr uint32_t SearchPrivate = 0x200;  // Process-private memory only (heaps, stacks); no images or file mappings.
constexpr uint32_t SearchPaged = 0x400;    // Also pages outside the working set (slow: each is faulted in).
constexpr uint32_t SearchBackground = 0x800; // Scan on the agent's worker thread, if the host allows it.
constexpr uint32_t SearchFlags = SearchWritable | SearchPrivate | SearchPaged | SearchBackground;
constexpr uint32_t SearchAlignment(uint32_t flags) { return (flags & 0xFF) ? (flags & 0xFF) : 1; }

// Xrefs: every 4-byte field that, read as a signed offset from its own end,
// lands on a target: how calls, jumps and RIP-relative operands encode one.
// An instruction with an immediate after its displacement measures from its
// own end, so a field also counts when it lands 1, 2 or 4 bytes before the
// target. address = first byte, args[0] = end (exclusive, required), args[1]
// = most hits, data = the target (8 bytes), flags = 0 or SearchBackground.
// Resident pages only, in time slices or on the worker like Search; same
// response: field addresses, detail = where to resume, 0 once done.
constexpr uint32_t XrefSlack[] = {0, 1, 2, 4};

// Region: what the memory at `address` is (VirtualQuery). Response: one
// RegionInfo; NotFound outside the address space. Values are the Windows
// MEM_* and PAGE_* numbers, passed through.
struct RegionInfo {
    uint64_t base, size, allocationBase;
    uint32_t state, protect, allocationProtect, type;
};

// Write with flags = WriteExpect: data holds the new bytes, then as many
// expected bytes; the agent writes only while memory still holds the expected
// bytes, else refuses and returns what it found.
constexpr uint32_t WriteExpect = 1;

// A guard: bytes elsewhere that must still hold a known value (an object's
// identity next to the field written or sampled). When it changes, the pin
// stops and the watch marks the range unreadable.
constexpr size_t MaxGuard = 16;
struct Guard {
    uint64_t address;     // 0 = none.
    uint32_t length, reserved;
    uint8_t bytes[MaxGuard];
};

// Pin: the agent writes the same bytes every host tick for a while ("freeze").
// flags = PinAction. Add: data = PinSpec (length = sizeof), detail = pin id.
// List: data = PinState per slot in use (length = count x size). Clear:
// args[0] = pin id, 0 = all. A pin stops for good when it expires, its guard
// changes, its memory is no longer writable, or (first write only) memory
// differs from `expected`; List shows why.
constexpr size_t MaxPins = 8;
constexpr size_t MaxPinBytes = 64;
constexpr uint32_t MaxPinMilliseconds = 60000;
enum class PinAction : uint32_t { Add = 1, List = 2, Clear = 3 };
enum class PinStop : uint32_t { Active, Expired, GuardChanged, NotWritable, Unexpected, Cleared, Faulted };
struct PinSpec {
    uint64_t address;
    uint32_t length, milliseconds;
    uint32_t hasExpected, reserved;
    uint8_t bytes[MaxPinBytes], expected[MaxPinBytes];
    Guard guard;
};
struct PinState {
    PinSpec spec;
    uint32_t id, writes;
    PinStop stop;
    uint32_t remaining; // Milliseconds left while active.
};
static_assert(MaxPins * sizeof(PinState) <= MaxRead);

// Trial: one write the agent undoes by itself. flags = TrialAction.
// Add: data = TrialSpec, detail = trial id. The agent checks the guards and
// that memory holds `expected`, saves those bytes and writes once. Each tick
// it rechecks:
// - a failed guard ends the trial as Abandoned and nothing is written (the
//   memory may belong to something else now);
// - memory that no longer holds the trial bytes ends it as Conflict (the game
//   changed it) and is left alone.
// Expiry and Clear restore the saved bytes (Restored), checked the same way.
// Expiry is the guarantee: a console that dies cannot clear, the time limit
// still restores. List: TrialState per slot in use; finished trials stay until
// cleared or their slot is reused. Clear: args[0] = id, 0 = all; data = the
// final TrialState of each cleared trial.
//
// Guards: `owner` is the identity of a live object (required). The target lies
// inside [ownerAddress, ownerAddress + ownerSize), or, with `object` set,
// within MaxTrialObjectRange bytes after `object`, a native object whose
// first 8 bytes (its vtable) must point into a loaded module and are checked
// every tick. No target inside a loaded module (code, vtables, static data).
// Which owners are allowed (class checks) is the client's job; the agent
// enforces only what it can see.
constexpr size_t MaxTrials = 8;
constexpr size_t MaxTrialBytes = 64;
constexpr uint32_t MaxTrialMilliseconds = 60000;
constexpr uint64_t MaxTrialObjectRange = 0x1000;
enum class TrialAction : uint32_t { Add = 1, List = 2, Clear = 3 };
enum class TrialStop : uint32_t { Active, Restored, Conflict, Abandoned };
// Why a trial was abandoned or refused.
enum class TrialGuard : uint32_t { None, Owner, Object, Unreadable };
struct TrialSpec {
    uint64_t address;
    uint32_t length, milliseconds;
    uint8_t bytes[MaxTrialBytes], expected[MaxTrialBytes];
    Guard owner;
    uint64_t ownerAddress;
    uint32_t ownerSize, reserved;
    uint64_t object; // 0: the target is inside the owner.
};
struct TrialState {
    TrialSpec spec;
    uint8_t original[MaxTrialBytes];
    uint64_t vtable;      // The object's first 8 bytes at the start.
    uint64_t changedTick; // Agent heartbeat when the state last changed.
    uint32_t id;
    TrialStop stop;
    TrialGuard guard;     // Abandoned: which guard failed.
    uint32_t remaining;   // Milliseconds left while active.
};
static_assert(MaxTrials * sizeof(TrialState) <= MaxRead);

// Watch: the agent samples up to MaxWatchRanges ranges into its own buffer,
// every host tick or every `interval` ms (at most once per tick), so samples
// line up per tick and cost no round trips. The capture belongs to the agent
// instance: it outlives the client that started it, and a newer agent taking
// over ends it. flags = WatchAction.
// - Start: data = WatchSpec; replaces any earlier watch; detail = rows the
//   buffer holds. A capture that cannot fit (one row per interval for the
//   whole duration) is refused with detail = the longest duration in ms that
//   would. Every tick: at most MaxWatchMilliseconds; with an interval, at
//   most MaxWatchCaptureMilliseconds.
// - Read: args[0] = first row; the response packs whole rows (WatchRowSize
//   bytes each: WatchRowHeader, then each range's bytes in order, padded to
//   8); detail = rows so far | WatchRunning while it still samples.
// - Stop: ends sampling early; rows stay readable until the next Start.
// - Info: data = WatchInfo, the spec and state, so a later client can read.
// - Roots: data = the host's root names, each NUL-terminated (none: empty).
// A guarded range whose guard fails stops the capture (`object replaced`).
// Following a root (`root` set): ranges are offsets from the object the
// host's root callback names, guarded on the identity it returns. When the
// guard fails the agent asks again and continues on the new object; while
// there is none it writes one row marked lost and samples nothing.
constexpr size_t MaxWatchRanges = 8;
constexpr uint32_t MaxWatchRange = 512, MaxWatchRowBytes = 2048;
constexpr uint32_t MaxWatchMilliseconds = 30000;
constexpr uint32_t MaxWatchCaptureMilliseconds = 3600000;
constexpr size_t WatchBufferBytes = size_t{4} << 20;
constexpr uint64_t WatchRunning = uint64_t{1} << 63;
constexpr size_t MaxRootName = 32;
enum class WatchAction : uint32_t { Start = 1, Read = 2, Stop = 3, Info = 4, Roots = 5 };
enum class WatchEnd : uint32_t { Running, Deadline, Stopped, BufferFull, ObjectReplaced };
struct WatchRange {
    uint64_t address;     // Following a root: the offset from it.
    uint32_t length, reserved;
    Guard guard;
};
struct WatchSpec {
    uint32_t count, milliseconds;
    uint32_t interval, reserved;  // Milliseconds between samples; 0 = every host tick.
    char root[MaxRootName];       // A host root to follow; empty = absolute addresses.
    WatchRange ranges[MaxWatchRanges];
};
// Per row: agent heartbeat, microseconds since Start, one bit per range that
// was readable with its guard intact (unreadable ranges are zero-filled),
// WatchRow* flags, and the root's address the row was sampled at (0 unless
// following one).
constexpr uint32_t WatchRowLost = 1, WatchRowResumed = 2;
struct WatchRowHeader {
    uint64_t tick, microseconds;
    uint32_t readable, flags;
    uint64_t root;
};
struct WatchInfo {
    WatchSpec spec;
    uint32_t rows, maxRows;
    WatchEnd stop;
    uint32_t reserved;
};
static_assert(sizeof(WatchInfo) <= MaxRead && sizeof(PinSpec) <= MaxRead);
constexpr uint32_t WatchRowSize(const WatchSpec& spec) {
    uint32_t size = sizeof(WatchRowHeader);
    for (uint32_t i = 0; i < spec.count && i < MaxWatchRanges; ++i) size += (spec.ranges[i].length + 7) & ~7u;
    return size;
}
constexpr uint32_t WatchMaxRows(const WatchSpec& spec) { return static_cast<uint32_t>(WatchBufferBytes / WatchRowSize(spec)); }
// The longest duration (ms) an interval capture fits in the buffer: one row
// at the start and one per interval after it.
constexpr uint64_t WatchLongestFit(const WatchSpec& spec) {
    return spec.interval ? uint64_t{WatchMaxRows(spec) - 1} * spec.interval : MaxWatchMilliseconds;
}
constexpr bool WatchFits(const WatchSpec& spec) { return spec.milliseconds <= WatchLongestFit(spec); }
constexpr bool GuardValid(const Guard& guard) {
    return !guard.address ? guard.length == 0 : guard.length && guard.length <= MaxGuard;
}
constexpr bool WatchSpecValid(const WatchSpec& spec) {
    const uint32_t longest = spec.interval ? MaxWatchCaptureMilliseconds : MaxWatchMilliseconds;
    if (!spec.count || spec.count > MaxWatchRanges || !spec.milliseconds || spec.milliseconds > longest ||
        spec.interval > spec.milliseconds || spec.reserved) return false;
    bool terminated = false;
    for (const char c : spec.root) terminated |= c == 0;
    if (!terminated) return false;
    const bool follow = spec.root[0] != 0;
    for (uint32_t i = 0; i < spec.count; ++i) {
        const auto& range = spec.ranges[i];
        // Offsets from a root may be 0; the root brings its own guard.
        if ((!follow && !range.address) || (follow && (range.guard.address || range.address >= MaxRead * 16)) ||
            !range.length || range.length > MaxWatchRange || !GuardValid(range.guard)) return false;
    }
    return WatchRowSize(spec) <= MaxWatchRowBytes;
}
constexpr bool PinSpecValid(const PinSpec& spec) {
    return spec.address && spec.length && spec.length <= MaxPinBytes && spec.milliseconds &&
        spec.milliseconds <= MaxPinMilliseconds && spec.hasExpected <= 1 && GuardValid(spec.guard);
}
// The spec's own bounds and target range; guards are checked live. Pure; tested.
constexpr bool TrialSpecValid(const TrialSpec& spec) {
    if (!spec.address || !spec.length || spec.length > MaxTrialBytes || !spec.milliseconds ||
        spec.milliseconds > MaxTrialMilliseconds || !spec.owner.address || !GuardValid(spec.owner) ||
        !spec.ownerAddress || spec.address > UINT64_MAX - spec.length) return false;
    const uint64_t end = spec.address + spec.length;
    if (spec.object) return spec.address >= spec.object && end - spec.object <= MaxTrialObjectRange;
    return spec.ownerSize && spec.address >= spec.ownerAddress && spec.ownerAddress <= UINT64_MAX - spec.ownerSize &&
        end <= spec.ownerAddress + spec.ownerSize;
}

// Read: address, length. Write: address, length, data. Modules: length = index
// of the first module of this page. Packs document their own use of address,
// args, length, text and data. hold = RequestHold: more requests follow; the
// agent waits briefly for the next one and runs it in the same host tick, up
// to the host's per-wait and per-tick budgets. Any op; flags stay op-specific.
constexpr uint32_t RequestHold = 1;
struct Request {
    uint32_t op, flags;
    uint64_t address;
    uint64_t args[2];
    uint32_t length, hold, reserved[2]; // Keeps data 16-byte aligned without hidden padding.
    char text[MaxText];
    alignas(16) uint8_t data[MaxRead];
};
// detail: op-specific number (Faulted = address, Modules = total, ...).
// tick: the agent heartbeat when the request finished, for every op.
// reason: optional short text for a refusal; empty when not set.
struct Response {
    Status status;
    uint32_t length;
    uint64_t detail;
    uint64_t tick, reserved; // reserved keeps data 16-byte aligned without hidden padding.
    char reason[MaxReason];
    alignas(16) uint8_t data[MaxRead];
};
// An engine object: address, the engine's slot for it and that slot's
// serial, so a replaced object is told apart from the one a ref named.
// index < 0 means address only (the pack identifies it); serial 0 = not
// assigned yet. Core commands treat it as an address; packs give it meaning.
struct Ref {
    uintptr_t address;
    int32_t index, serial;
    bool operator==(const Ref&) const = default;
};
// On the wire a ref is Request::address plus args[0] (index low, serial high).
inline void SetRef(Request& request, const Ref& ref) {
    request.address = ref.address;
    request.args[0] = static_cast<uint32_t>(ref.index) | static_cast<uint64_t>(static_cast<uint32_t>(ref.serial)) << 32;
}
inline Ref GetRef(const Request& request) {
    return {static_cast<uintptr_t>(request.address), static_cast<int32_t>(static_cast<uint32_t>(request.args[0])),
            static_cast<int32_t>(static_cast<uint32_t>(request.args[0] >> 32))};
}
// A raw address as a ref the pack identifies.
inline Ref AddressRef(uint64_t address) { return {static_cast<uintptr_t>(address), -1, 0}; }
// One loaded module; the process exe comes first.
struct ModuleEntry {
    uint64_t base, size;
    char name[48];
};
constexpr size_t MaxModulesPerPage = MaxRead / sizeof(ModuleEntry);
static_assert(MaxWrite <= MaxRead);

// Which thread the host ticks on, as the host states it (the core cannot
// tell). Engine: the engine's own thread, so a tick is a moment of the game.
// Borrowed (a thread the game runs for something else, such as the frame
// present) or Own: a tick is only a host tick. Unset is refused at open.
enum class HostThread : uint32_t { Unset, Engine, Borrowed, Own };
constexpr const char* HostThreadText(HostThread thread) {
    switch (thread) {
    case HostThread::Engine: return "engine";
    case HostThread::Borrowed: return "borrowed";
    case HostThread::Own: return "own";
    case HostThread::Unset: break;
    }
    return "unset";
}

// Sequence and heartbeat fields are accessed only through std::atomic_ref.
// ops lists the op numbers the agent handles, written once before magic.
// owner: the agent instance that answers. A second agent in the same process
// (the host injected again) opens the same section and takes it over; the
// earlier one sees another owner and stops answering. build: the agent's
// compile time, so a client can tell which build answers.
constexpr size_t MaxBuildText = 24;
struct Section {
    uint32_t magic, layout, agentPid, opCount;
    uint64_t heartbeat; // Agent ticks.
    uint32_t requestSeq, responseSeq;
    uint32_t owner, takeovers;
    uint32_t hostThread, reserved; // HostThread, as the host declared it.
    char build[MaxBuildText];
    uint32_t ops[MaxOps];
    Request request;
    Response response;
};
// Core part of the layout check. The host adds each pack's part; agent and
// client must agree on the sum, or the attach is refused.
// Every struct that crosses the section is in the sum, so a console built
// before one changed is refused instead of misreading it: add new ones here.
constexpr uint32_t LayoutCore = static_cast<uint32_t>(sizeof(Section) * 31 + sizeof(Request) * 7 +
    sizeof(Response) * 3 + sizeof(ModuleEntry) * 13 + offsetof(Section, request) + offsetof(Request, text) * 5 +
    sizeof(ReadSpan) * 17 + sizeof(Guard) * 19 + sizeof(PinSpec) * 23 + sizeof(PinState) * 29 +
    sizeof(TrialSpec) * 37 + sizeof(TrialState) * 41 + sizeof(WatchSpec) * 43 + sizeof(WatchRowHeader) * 47 +
    sizeof(RegionInfo) * 53 + sizeof(WatchInfo) * 59);

constexpr const char* StatusText(Status status) {
    switch (status) {
    case Status::Ok: return "ok";
    case Status::BadRequest: return "bad request";
    case Status::NotReadable: return "memory not readable";
    case Status::NotWritable: return "memory not writable";
    case Status::Faulted: return "access faulted";
    case Status::NotFound: return "not found";
    case Status::Refused: return "refused";
    case Status::Unsupported: return "unsupported op";
    }
    return "unknown status";
}

// Text fields are NUL-terminated inside their buffer.
template<size_t N> bool Terminated(const char (&text)[N]) {
    for (char c : text) if (!c) return true;
    return false;
}

// Copies at most size-1 bytes and terminates.
template<size_t N> void CopyText(char (&out)[N], std::string_view text) {
    const size_t count = text.size() < N - 1 ? text.size() : N - 1;
    for (size_t i = 0; i < count; ++i) out[i] = text[i];
    out[count] = 0;
}

// Refusal with a status, optional reason text and detail number.
inline void Refuse(struct Response& response, Status status, std::string_view reason = {}, uint64_t detail = 0) {
    response.status = status;
    response.detail = detail;
    CopyText(response.reason, reason);
}

// Bounds of the core ops; other ops are bad requests. Pure; shared with tests.
inline Status Check(const Request& request) {
    switch (request.op) {
    case op::Read:
        return request.address && request.length && request.length <= MaxRead ? Status::Ok : Status::BadRequest;
    case op::Write:
        return request.address && request.length && request.length <= MaxWrite && (request.flags & ~WriteExpect) == 0 ?
            Status::Ok : Status::BadRequest;
    case op::ReadBatch:
        return request.length && request.length <= MaxBatchReads ? Status::Ok : Status::BadRequest;
    case op::Modules:
        return Status::Ok;
    case op::Search: {
        const uint32_t align = SearchAlignment(request.flags);
        const bool alignOk = align == 1 || align == 2 || align == 4 || align == 8 || align == 16;
        return request.length && request.length <= MaxSearchPattern && alignOk &&
            !(request.flags & ~(SearchFlags | 0xFFu)) && request.args[1] && request.args[1] <= MaxSearchHits &&
            (!request.args[0] || request.args[0] > request.address) ? Status::Ok : Status::BadRequest;
    }
    case op::Region:
        return request.length || request.flags ? Status::BadRequest : Status::Ok;
    case op::Xrefs:
        return request.length == 8 && !(request.flags & ~SearchBackground) && request.args[1] &&
            request.args[1] <= MaxSearchHits && request.args[0] > request.address ? Status::Ok : Status::BadRequest;
    case op::Pin:
        switch (static_cast<PinAction>(request.flags)) {
        case PinAction::Add: {
            if (request.length != sizeof(PinSpec)) return Status::BadRequest;
            PinSpec spec;
            memcpy(&spec, request.data, sizeof(spec));
            return PinSpecValid(spec) ? Status::Ok : Status::BadRequest;
        }
        case PinAction::List: return request.length ? Status::BadRequest : Status::Ok;
        case PinAction::Clear: return request.length || request.args[0] > UINT32_MAX ? Status::BadRequest : Status::Ok;
        }
        return Status::BadRequest;
    case op::Trial:
        switch (static_cast<TrialAction>(request.flags)) {
        case TrialAction::Add: {
            if (request.length != sizeof(TrialSpec)) return Status::BadRequest;
            TrialSpec spec;
            memcpy(&spec, request.data, sizeof(spec));
            return TrialSpecValid(spec) ? Status::Ok : Status::BadRequest;
        }
        case TrialAction::List: return request.length ? Status::BadRequest : Status::Ok;
        case TrialAction::Clear: return request.length || request.args[0] > UINT32_MAX ? Status::BadRequest : Status::Ok;
        }
        return Status::BadRequest;
    case op::Watch:
        switch (static_cast<WatchAction>(request.flags)) {
        case WatchAction::Start: {
            if (request.length != sizeof(WatchSpec)) return Status::BadRequest;
            WatchSpec spec;
            memcpy(&spec, request.data, sizeof(spec));
            return WatchSpecValid(spec) ? Status::Ok : Status::BadRequest;
        }
        case WatchAction::Read: return request.length || request.args[0] > UINT32_MAX ? Status::BadRequest : Status::Ok;
        case WatchAction::Stop: case WatchAction::Info: case WatchAction::Roots:
            return request.length ? Status::BadRequest : Status::Ok;
        }
        return Status::BadRequest;
    }
    return Status::BadRequest;
}
}
