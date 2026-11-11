// reagentlive client.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
#include <timeapi.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <utility>
#include "reagentlive/core/client.h"

namespace reagentlive {
namespace {
Error Fail(Error::Kind kind, std::string message) { return {kind, Status::Ok, 0, {}, std::move(message)}; }
}

Error AgentError(const Response& response) {
    const std::string reason(response.reason, strnlen(response.reason, sizeof(response.reason)));
    std::string message = StatusText(response.status);
    if (!reason.empty()) message += ": " + reason;
    if (response.detail && (response.status == Status::Faulted || response.status == Status::NotReadable ||
                            response.status == Status::NotWritable)) {
        char text[32];
        snprintf(text, sizeof(text), " at 0x%llx", static_cast<unsigned long long>(response.detail));
        message += text;
    } else if (response.detail && response.status != Status::Ok) {
        message += " (" + std::to_string(response.detail) + ")";
    }
    return {Error::Kind::Agent, response.status, response.detail, reason, std::move(message)};
}

Result<Client> Client::Attach(std::wstring_view sectionName, uint32_t layout) {
    const std::wstring name(sectionName);
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (!mapping) return std::unexpected(Fail(Error::Kind::Attach, "no agent section (error " + std::to_string(GetLastError()) + ")"));
    auto* section = static_cast<Section*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Section)));
    CloseHandle(mapping); // The view keeps the section alive.
    if (!section) return std::unexpected(Fail(Error::Kind::Attach, "cannot map the section (error " + std::to_string(GetLastError()) + ")"));
    if (std::atomic_ref(section->magic).load(std::memory_order_acquire) != Magic || section->layout != layout) {
        UnmapViewOfFile(section);
        return std::unexpected(Fail(Error::Kind::Attach, "agent layout differs; build agent and client from the same source"));
    }
    // Two clients would overwrite each other's request and both time out. The
    // first holder creates the named object; Windows closes it if the client dies.
    HANDLE guard = CreateMutexW(nullptr, FALSE, (name + L"-client").c_str());
    if (!guard || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (guard) CloseHandle(guard);
        UnmapViewOfFile(section);
        return std::unexpected(Fail(Error::Kind::Busy, "another client is attached to this agent (one console at a time)"));
    }
    return Client(section, guard);
}

void Client::Release() {
    if (section_) UnmapViewOfFile(section_);
    if (guard_) CloseHandle(guard_);
    if (timerRaised_) timeEndPeriod(1);
    section_ = nullptr;
    guard_ = nullptr;
    timerRaised_ = false;
}
Client::Client(Client&& other) noexcept
    : section_(std::exchange(other.section_, nullptr)), guard_(std::exchange(other.guard_, nullptr)),
      timeout_(other.timeout_), notice_(other.notice_), stats_(other.stats_),
      timerRaised_(std::exchange(other.timerRaised_, false)) {}
Client& Client::operator=(Client&& other) noexcept {
    if (this != &other) {
        Release();
        section_ = std::exchange(other.section_, nullptr);
        guard_ = std::exchange(other.guard_, nullptr);
        timeout_ = other.timeout_;
        notice_ = other.notice_;
        stats_ = other.stats_;
        timerRaised_ = std::exchange(other.timerRaised_, false);
    }
    return *this;
}
Client::~Client() { Release(); }

uint32_t Client::AgentPid() const { return section_ ? section_->agentPid : 0; }
std::string Client::AgentBuild() const {
    if (!section_) return {};
    char text[MaxBuildText];
    memcpy(text, section_->build, sizeof(text));
    return std::string(text, strnlen(text, sizeof(text)));
}
uint32_t Client::AgentTakeovers() const {
    return section_ ? std::atomic_ref(section_->takeovers).load(std::memory_order_relaxed) : 0;
}
uint64_t Client::Heartbeat() const {
    return section_ ? std::atomic_ref(section_->heartbeat).load(std::memory_order_relaxed) : 0;
}

HostThread Client::AgentHostThread() const {
    const uint32_t value = section_ ? std::atomic_ref(section_->hostThread).load(std::memory_order_relaxed) : 0;
    return value <= static_cast<uint32_t>(HostThread::Own) ? static_cast<HostThread>(value) : HostThread::Unset;
}

uint32_t Client::AgentOwner() const {
    return section_ ? std::atomic_ref(section_->owner).load(std::memory_order_acquire) : 0;
}

// While held, the agent waits only briefly for the next request; a
// descheduled client thread would miss it, so the outermost hold raises it.
Client::Hold::Hold(Client& client) : client_(client) {
    if (client_.holdDepth_++) return;
    client_.holdFirstTick_ = client_.holdLastTick_ = client_.holdSpanTick_ = 0;
    client_.holdPriority_ = GetThreadPriority(GetCurrentThread());
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
}
Client::Hold::~Hold() {
    if (!--client_.holdDepth_ && client_.holdPriority_ != THREAD_PRIORITY_ERROR_RETURN)
        SetThreadPriority(GetCurrentThread(), client_.holdPriority_);
}
uint64_t Client::Hold::FirstTick() const { return client_.holdFirstTick_; }
uint64_t Client::Hold::LastTick() const { return client_.holdLastTick_; }
void Client::Hold::BeginSpan() { client_.holdSpanTick_ = 0; }
uint64_t Client::Hold::SpanFirstTick() const { return client_.holdSpanTick_; }

namespace {
int64_t NowMicroseconds() {
    LARGE_INTEGER frequency, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    return now.QuadPart / frequency.QuadPart * 1000000 + now.QuadPart % frequency.QuadPart * 1000000 / frequency.QuadPart;
}
// Spinning catches a response within microseconds; after this, sleep instead
// of burning a core. Held requests spin longer: the agent waits only briefly
// for the next one, and a client that woke late from a sleep would miss every
// later tick too. Past that (a long scan), sleep.
constexpr int64_t SpinMicroseconds = 2000, HeldSpinMicroseconds = 50000;
}

Result<Response> Client::Send(const Request& request, std::chrono::milliseconds timeout) {
    if (!section_) return std::unexpected(Fail(Error::Kind::Attach, "not attached"));
    if (!timerRaised_) {
        // Sleep(1) otherwise rounds up to the ~15.6 ms system timer. This
        // process only; released at exit.
        timeBeginPeriod(1);
        timerRaised_ = true;
    }
    const uint32_t seq = std::atomic_ref(section_->requestSeq).load(std::memory_order_relaxed) + 1;
    memcpy(&section_->request, &request, sizeof(request));
    section_->request.hold = holdDepth_ ? RequestHold : 0;
    const int64_t sent = NowMicroseconds();
    std::atomic_ref(section_->requestSeq).store(seq, std::memory_order_release);
    ++stats_.trips;
    const ULONGLONG start = GetTickCount64();
    const ULONGLONG deadline = start + static_cast<ULONGLONG>(timeout.count());
    ULONGLONG nextNotice = start + 1000;
    uint64_t lastBeat = Heartbeat(), beatAtStart = lastBeat;
    while (std::atomic_ref(section_->responseSeq).load(std::memory_order_acquire) != seq) {
        if (NowMicroseconds() - sent < (holdDepth_ ? HeldSpinMicroseconds : SpinMicroseconds)) {
            YieldProcessor();
            continue;
        }
        const ULONGLONG now = GetTickCount64();
        if (now > deadline) {
            stats_.waitMicroseconds += static_cast<uint64_t>(NowMicroseconds() - sent);
            const bool ticking = Heartbeat() != beatAtStart;
            return std::unexpected(Fail(Error::Kind::Timeout, "timed out after " + std::to_string(timeout.count()) +
                (ticking ? " ms; the agent is ticking, so the request is still running or was replaced"
                         : " ms; the agent is not ticking")));
        }
        if (notice_ && now >= nextNotice) {
            const uint64_t beat = Heartbeat();
            notice_(std::chrono::milliseconds(now - start), beat != lastBeat);
            lastBeat = beat;
            nextNotice = now + 1000;
        }
        Sleep(1);
    }
    stats_.waitMicroseconds += static_cast<uint64_t>(NowMicroseconds() - sent);
    Response response{};
    response.status = section_->response.status;
    response.length = section_->response.length <= MaxRead ? section_->response.length : 0;
    response.detail = section_->response.detail;
    response.tick = section_->response.tick;
    if (holdDepth_) {
        if (!holdFirstTick_) holdFirstTick_ = response.tick;
        if (!holdSpanTick_) holdSpanTick_ = response.tick;
        holdLastTick_ = response.tick;
    }
    memcpy(response.reason, section_->response.reason, sizeof(response.reason));
    response.reason[sizeof(response.reason) - 1] = 0;
    memcpy(response.data, section_->response.data, response.length);
    return response;
}

Result<std::vector<uint8_t>> Client::Read(uint64_t address, uint32_t length) {
    Request request{};
    request.op = op::Read;
    request.address = address;
    request.length = length;
    auto response = Send(request);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
    return std::vector<uint8_t>(response->data, response->data + response->length);
}

Result<ReadBatchResult> Client::ReadBatch(std::span<const ReadSpan> reads) {
    const auto size = ReadBatchSize(reads);
    if (!size) return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "invalid read batch"});
    Request request{};
    request.op = op::ReadBatch;
    request.length = static_cast<uint32_t>(reads.size());
    memcpy(request.data, reads.data(), reads.size_bytes());
    auto response = Send(request);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
    if (response->length != size)
        return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "invalid read batch response"});

    ReadBatchResult result{response->detail, {}};
    size_t offset = reads.size() * sizeof(Status);
    for (size_t i = 0; i < reads.size(); ++i) {
        Status status;
        memcpy(&status, response->data + i * sizeof(Status), sizeof(status));
        auto& item = result.items.emplace_back(status, std::vector<uint8_t>{});
        if (status == Status::Ok)
            item.bytes.assign(response->data + offset, response->data + offset + reads[i].length);
        offset += reads[i].length;
    }
    return result;
}

Result<std::vector<uint8_t>> Client::ReadRange(uint64_t address, size_t length) {
    std::vector<uint8_t> bytes;
    bytes.reserve(length);
    while (bytes.size() < length) {
        const auto chunk = static_cast<uint32_t>(std::min(MaxRead, length - bytes.size()));
        auto part = Read(address + bytes.size(), chunk);
        if (!part) return std::unexpected(part.error());
        bytes.insert(bytes.end(), part->begin(), part->end());
    }
    return bytes;
}

Result<void> Client::Write(uint64_t address, std::span<const uint8_t> bytes) {
    if (bytes.size() > MaxWrite) return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "write larger than MaxWrite"});
    Request request{};
    request.op = op::Write;
    request.address = address;
    request.length = static_cast<uint32_t>(bytes.size());
    memcpy(request.data, bytes.data(), bytes.size());
    auto response = Send(request);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
    return {};
}

Result<SearchResult> Client::Search(const SearchQuery& query, std::chrono::milliseconds timeout) {
    Request request{};
    request.op = op::Search;
    request.address = query.from;
    request.args[0] = query.to;
    request.args[1] = query.maxHits;
    request.flags = (query.alignment & 0xFF) | (query.writableOnly ? SearchWritable : 0) |
        (query.privateOnly ? SearchPrivate : 0) | (query.paged ? SearchPaged : 0) |
        (query.background ? SearchBackground : 0);
    request.length = static_cast<uint32_t>(std::min(query.pattern.size(), sizeof(request.data)));
    if (query.pattern.size() > MaxSearchPattern || Check(request) != Status::Ok)
        return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "invalid search"});
    memcpy(request.data, query.pattern.data(), request.length);
    auto response = Send(request, timeout);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
    const size_t count = response->length / sizeof(uint64_t);
    if (response->length % sizeof(uint64_t) || count > query.maxHits)
        return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "invalid search response"});
    SearchResult result{std::vector<uint64_t>(count), response->detail};
    memcpy(result.hits.data(), response->data, response->length);
    return result;
}

Result<SearchResult> Client::Xrefs(const XrefQuery& query, std::chrono::milliseconds timeout) {
    Request request{};
    request.op = op::Xrefs;
    request.address = query.from;
    request.args[0] = query.to;
    request.args[1] = query.maxHits;
    request.flags = query.background ? SearchBackground : 0;
    request.length = sizeof(query.target);
    memcpy(request.data, &query.target, sizeof(query.target));
    if (Check(request) != Status::Ok)
        return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "invalid xrefs request"});
    auto response = Send(request, timeout);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
    const size_t count = response->length / sizeof(uint64_t);
    if (response->length % sizeof(uint64_t) || count > query.maxHits)
        return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "invalid xrefs response"});
    SearchResult result{std::vector<uint64_t>(count), response->detail};
    memcpy(result.hits.data(), response->data, response->length);
    return result;
}

Result<RegionInfo> Client::Region(uint64_t address) {
    Request request{};
    request.op = op::Region;
    request.address = address;
    auto response = Send(request);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
    if (response->length != sizeof(RegionInfo))
        return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "invalid region response"});
    RegionInfo region;
    memcpy(&region, response->data, sizeof(region));
    return region;
}

Result<PointerChain> Client::ResolveChain(uint64_t base, std::span<const uint64_t> offsets) {
    PointerChain chain;
    chain.address = base;
    if (offsets.size() > MaxChainHops)
        return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "pointer chain too long"});
    const Hold hold(*this);
    for (size_t i = 0; i < offsets.size(); ++i) {
        const uint64_t at = chain.address + offsets[i];
        if (i + 1 == offsets.size()) {
            chain.address = at;
            break;
        }
        auto bytes = Read(at, 8);
        if (!bytes) {
            if (bytes.error().kind != Error::Kind::Agent) return std::unexpected(bytes.error());
            chain.hops.push_back({at, 0});
            chain.broken = i;
            chain.reason = bytes.error().message;
            return chain;
        }
        uint64_t value = 0;
        memcpy(&value, bytes->data(), 8);
        chain.hops.push_back({at, value});
        if (!value) {
            chain.broken = i;
            chain.reason = "null pointer";
            return chain;
        }
        chain.address = value;
    }
    return chain;
}

Result<void> Client::WriteIf(uint64_t address, std::span<const uint8_t> bytes, std::span<const uint8_t> expected,
                             std::vector<uint8_t>* found) {
    if (bytes.size() > MaxWrite || expected.size() != bytes.size())
        return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "expected bytes must match the write size"});
    Request request{};
    request.op = op::Write;
    request.flags = WriteExpect;
    request.address = address;
    request.length = static_cast<uint32_t>(bytes.size());
    memcpy(request.data, bytes.data(), bytes.size());
    memcpy(request.data + bytes.size(), expected.data(), expected.size());
    auto response = Send(request);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) {
        if (found && response->length == bytes.size()) found->assign(response->data, response->data + response->length);
        return std::unexpected(AgentError(*response));
    }
    return {};
}

namespace {
Result<Response> Exchange(Client& client, const Request& request) {
    auto response = client.Send(request);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
    return response;
}
Error BadResponse(const char* what) { return Error{Error::Kind::Agent, Status::BadRequest, 0, {}, what}; }
}

Result<PinState> Client::PinAdd(const PinSpec& spec) {
    Request request{};
    request.op = op::Pin;
    request.flags = static_cast<uint32_t>(PinAction::Add);
    request.length = sizeof(spec);
    memcpy(request.data, &spec, sizeof(spec));
    if (Check(request) != Status::Ok) return std::unexpected(BadResponse("invalid pin"));
    auto response = Exchange(*this, request);
    if (!response) return std::unexpected(response.error());
    if (response->length != sizeof(PinState)) return std::unexpected(BadResponse("invalid pin response"));
    PinState state;
    memcpy(&state, response->data, sizeof(state));
    return state;
}

Result<std::vector<PinState>> Client::PinList() {
    Request request{};
    request.op = op::Pin;
    request.flags = static_cast<uint32_t>(PinAction::List);
    auto response = Exchange(*this, request);
    if (!response) return std::unexpected(response.error());
    const size_t count = response->length / sizeof(PinState);
    if (response->length % sizeof(PinState) || count > MaxPins) return std::unexpected(BadResponse("invalid pin list"));
    std::vector<PinState> pins(count);
    memcpy(pins.data(), response->data, response->length);
    return pins;
}

Result<uint32_t> Client::PinClear(uint32_t id) {
    Request request{};
    request.op = op::Pin;
    request.flags = static_cast<uint32_t>(PinAction::Clear);
    request.args[0] = id;
    auto response = Exchange(*this, request);
    if (!response) return std::unexpected(response.error());
    return static_cast<uint32_t>(response->detail);
}

namespace {
Result<std::vector<TrialState>> TrialStates(const Response& response) {
    const size_t count = response.length / sizeof(TrialState);
    if (response.length % sizeof(TrialState) || count > MaxTrials) return std::unexpected(BadResponse("invalid trial list"));
    std::vector<TrialState> trials(count);
    if (count) memcpy(trials.data(), response.data, response.length);
    return trials;
}
}

Result<TrialState> Client::TrialAdd(const TrialSpec& spec) {
    Request request{};
    request.op = op::Trial;
    request.flags = static_cast<uint32_t>(TrialAction::Add);
    request.length = sizeof(spec);
    memcpy(request.data, &spec, sizeof(spec));
    if (Check(request) != Status::Ok) return std::unexpected(BadResponse("invalid trial: bounds, owner guard or target range"));
    auto response = Exchange(*this, request);
    if (!response) return std::unexpected(response.error());
    if (response->length != sizeof(TrialState)) return std::unexpected(BadResponse("invalid trial response"));
    TrialState state;
    memcpy(&state, response->data, sizeof(state));
    return state;
}

Result<std::vector<TrialState>> Client::TrialList() {
    Request request{};
    request.op = op::Trial;
    request.flags = static_cast<uint32_t>(TrialAction::List);
    auto response = Exchange(*this, request);
    if (!response) return std::unexpected(response.error());
    return TrialStates(*response);
}

Result<std::vector<TrialState>> Client::TrialClear(uint32_t id) {
    Request request{};
    request.op = op::Trial;
    request.flags = static_cast<uint32_t>(TrialAction::Clear);
    request.args[0] = id;
    auto response = Exchange(*this, request);
    if (!response) return std::unexpected(response.error());
    return TrialStates(*response);
}

Result<uint32_t> Client::WatchStart(const WatchSpec& spec) {
    Request request{};
    request.op = op::Watch;
    request.flags = static_cast<uint32_t>(WatchAction::Start);
    request.length = sizeof(spec);
    memcpy(request.data, &spec, sizeof(spec));
    if (Check(request) != Status::Ok) return std::unexpected(BadResponse("invalid watch"));
    auto response = Exchange(*this, request);
    if (!response) return std::unexpected(response.error());
    return static_cast<uint32_t>(response->detail);
}

Result<WatchRows> Client::WatchRead(const WatchSpec& spec, uint32_t first) {
    const uint32_t rowSize = WatchRowSize(spec);
    WatchRows result{{}, false};
    Request request{};
    request.op = op::Watch;
    request.flags = static_cast<uint32_t>(WatchAction::Read);
    for (uint64_t next = first;;) {
        request.args[0] = next;
        auto response = Exchange(*this, request);
        if (!response) return std::unexpected(response.error());
        const uint64_t total = response->detail & ~WatchRunning;
        result.running = (response->detail & WatchRunning) != 0;
        if (response->length % rowSize || total < next) return std::unexpected(BadResponse("invalid watch page"));
        const uint32_t count = response->length / rowSize;
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* row = response->data + size_t{i} * rowSize;
            WatchRow& out = result.rows.emplace_back();
            memcpy(&out.header, row, sizeof(out.header));
            size_t at = sizeof(WatchRowHeader);
            for (uint32_t r = 0; r < spec.count; ++r) {
                const uint32_t length = spec.ranges[r].length;
                if (out.header.readable & (1u << r)) out.ranges.emplace_back(row + at, row + at + length);
                else out.ranges.emplace_back();
                at += (length + 7) & ~7u;
            }
        }
        next += count;
        if (!count || next >= total) break;
    }
    return result;
}

Result<uint32_t> Client::WatchStop() {
    Request request{};
    request.op = op::Watch;
    request.flags = static_cast<uint32_t>(WatchAction::Stop);
    auto response = Exchange(*this, request);
    if (!response) return std::unexpected(response.error());
    return static_cast<uint32_t>(response->detail);
}

Result<WatchInfo> Client::WatchStatus() {
    Request request{};
    request.op = op::Watch;
    request.flags = static_cast<uint32_t>(WatchAction::Info);
    auto response = Exchange(*this, request);
    if (!response) return std::unexpected(response.error());
    WatchInfo info{};
    if (response->length != sizeof(info)) return std::unexpected(BadResponse("invalid watch info"));
    memcpy(&info, response->data, sizeof(info));
    if (!WatchSpecValid(info.spec) || info.rows > info.maxRows) return std::unexpected(BadResponse("invalid watch info"));
    return info;
}

Result<std::vector<std::string>> Client::WatchRoots() {
    Request request{};
    request.op = op::Watch;
    request.flags = static_cast<uint32_t>(WatchAction::Roots);
    auto response = Exchange(*this, request);
    if (!response) return std::unexpected(response.error());
    std::vector<std::string> names;
    const auto* text = reinterpret_cast<const char*>(response->data);
    for (size_t at = 0; at < response->length;) {
        const size_t length = strnlen(text + at, response->length - at);
        if (at + length == response->length) return std::unexpected(BadResponse("invalid root names"));
        names.emplace_back(text + at, length);
        at += length + 1;
    }
    return names;
}

Result<std::vector<Module>> Client::Modules() {
    std::vector<Module> modules;
    Request request{};
    request.op = op::Modules;
    for (;;) {
        request.length = static_cast<uint32_t>(modules.size());
        auto response = Send(request);
        if (!response) return std::unexpected(response.error());
        if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
        const size_t count = std::min(response->length / sizeof(ModuleEntry), MaxModulesPerPage);
        for (size_t i = 0; i < count; ++i) {
            ModuleEntry entry;
            memcpy(&entry, response->data + i * sizeof(ModuleEntry), sizeof(entry));
            modules.push_back({std::string(entry.name, strnlen(entry.name, sizeof(entry.name))), entry.base, entry.size});
        }
        if (!count || modules.size() >= response->detail) break;
    }
    return modules;
}

std::vector<uint32_t> Client::SupportedOps() const {
    if (!section_) return {};
    const uint32_t count = std::min<uint32_t>(section_->opCount, static_cast<uint32_t>(MaxOps));
    return std::vector<uint32_t>(section_->ops, section_->ops + count);
}

bool Client::Supports(uint32_t op) const {
    const auto ops = SupportedOps();
    return std::find(ops.begin(), ops.end(), op) != ops.end();
}
}
