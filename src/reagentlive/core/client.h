// reagentlive client: typed C++ access to an agent from another process (or thread).
#pragma once
#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/core/protocol.h"

// Every frontend (CLI, GUI, tools, tests) talks to the agent through this API,
// never through the section directly. Synchronous: one request in flight per
// Client; not thread-safe. Results are values or an Error, never exceptions
// for agent or transport failures.
namespace reagentlive {
struct Error {
    enum class Kind { Agent, Timeout, Attach, Busy } kind;
    Status status;          // Kind::Agent: the agent's refusal.
    uint64_t detail;        // Kind::Agent: op-specific number (expected size, fault address).
    std::string reason;     // Kind::Agent: the agent's reason text, may be empty.
    std::string message;    // Readable summary.
};
template<class T> using Result = std::expected<T, Error>;
struct Module { std::string name; uint64_t base, size; };

struct ReadItem { Status status; std::vector<uint8_t> bytes; };
struct ReadBatchResult { uint64_t tick; std::vector<ReadItem> items; };
struct SearchQuery {
    std::vector<uint8_t> pattern;   // 1..MaxSearchPattern bytes.
    uint64_t from = 0, to = 0;      // to = 0: the top of the address space.
    uint32_t alignment = 1;         // 1, 2, 4, 8 or 16.
    bool writableOnly = false;      // Skip read-only and code pages.
    bool privateOnly = false;       // Heaps and stacks only: no images or file mappings.
    bool paged = false;             // Also pages outside the working set (slow).
    bool background = false;        // Agent worker thread instead of host-tick slices (host opt-in).
    uint32_t maxHits = 64;          // 1..MaxSearchHits.
};
struct SearchResult { std::vector<uint64_t> hits; uint64_t resume; }; // resume 0: range done.
// Relative references to `target` (protocol.h, Xrefs): fields in [from, to).
struct XrefQuery {
    uint64_t target = 0;
    uint64_t from = 0, to = 0;      // to is required (a module's end, usually).
    bool background = false;        // Agent worker thread instead of host-tick slices (host opt-in).
    uint32_t maxHits = MaxSearchHits;
};
// Watch rows as recorded: header plus each range's bytes (empty when that
// range was unreadable or its guard changed).
struct WatchRow { WatchRowHeader header; std::vector<std::vector<uint8_t>> ranges; };
struct WatchRows { std::vector<WatchRow> rows; bool running; };
// A pointer chain: base->a->b is *(base + a) + b; every offset but the last
// reads a pointer. Each hop is where a pointer was read and what it held.
// `broken`: the hop that was unreadable or held null (`reason` says which);
// `address` is then not meaningful.
struct ChainHop { uint64_t address, value; };
struct PointerChain {
    uint64_t address = 0;
    std::vector<ChainHop> hops;
    std::optional<size_t> broken;
    std::string reason;
};
constexpr size_t MaxChainHops = 16;
// Round trips since attach and time spent waiting for responses.
struct ClientStats { uint64_t trips, waitMicroseconds; };

class Client {
public:
    // Opens the named section; refuses one without magic or with another layout,
    // and refuses while another client is attached (one request slot per agent).
    static Result<Client> Attach(std::wstring_view sectionName, uint32_t layout);
    Client(Client&&) noexcept;
    Client& operator=(Client&&) noexcept;
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    ~Client();

    void SetTimeout(std::chrono::milliseconds timeout) { timeout_ = timeout; }
    std::chrono::milliseconds Timeout() const { return timeout_; }
    // Called about once a second while a request waits longer than a second;
    // ticking says whether the agent's heartbeat moved since the last call.
    using WaitNotice = void (*)(std::chrono::milliseconds waited, bool ticking);
    void SetWaitNotice(WaitNotice notice) { notice_ = notice; }
    uint32_t AgentPid() const;
    std::string AgentBuild() const;  // The answering agent's compile time.
    uint32_t AgentTakeovers() const; // Agents replaced in this process (re-injections).
    uint32_t AgentOwner() const;     // The answering agent instance; new on every injection.
    HostThread AgentHostThread() const; // Where the host says it ticks; Engine means ticks are game moments.
    uint64_t Heartbeat() const;   // Agent ticks so far.
    ClientStats Stats() const { return stats_; }

    // Raw exchange for pack and game ops. Transport failures are errors; the
    // agent's status is in the response. Waits by spinning briefly, then
    // sleeping in 1 ms steps (the client raises the timer resolution).
    Result<Response> Send(const Request& request, std::chrono::milliseconds timeout);
    Result<Response> Send(const Request& request) { return Send(request, timeout_); }

    // While a Hold is alive, every request asks the agent to wait briefly for
    // the next one and run it in the same host tick (within the host's
    // budget). Best effort: the ticks show whether every response shared one.
    // Nests; the outermost one records the ticks.
    class Hold {
    public:
        explicit Hold(Client& client);
        ~Hold();
        Hold(const Hold&) = delete;
        Hold& operator=(const Hold&) = delete;
        uint64_t FirstTick() const; // 0 until a response arrived.
        uint64_t LastTick() const;
        bool SameTick() const { return FirstTick() == LastTick(); }
        // A span inside the hold (one command of a batch): its first
        // response's tick, 0 until one arrived since BeginSpan.
        void BeginSpan();
        uint64_t SpanFirstTick() const;
    private:
        Client& client_;
    };

    Result<std::vector<uint8_t>> Read(uint64_t address, uint32_t length);
    Result<ReadBatchResult> ReadBatch(std::span<const ReadSpan> reads);
    // Any length: split into MaxRead requests.
    Result<std::vector<uint8_t>> ReadRange(uint64_t address, size_t length);
    Result<void> Write(uint64_t address, std::span<const uint8_t> bytes);
    // Writes only while memory still holds `expected` (same size). Refused
    // otherwise; the error's reason says so and `found` gets what is there.
    Result<void> WriteIf(uint64_t address, std::span<const uint8_t> bytes, std::span<const uint8_t> expected,
                         std::vector<uint8_t>* found = nullptr);
    // One page of matches; scans across agent ticks, so give it a long timeout.
    Result<SearchResult> Search(const SearchQuery& query, std::chrono::milliseconds timeout);
    // One page of field addresses whose rel32 lands on the target; resume like Search.
    Result<SearchResult> Xrefs(const XrefQuery& query, std::chrono::milliseconds timeout);
    // Pins: the agent rewrites the bytes every tick until the pin stops.
    // Refused pins come back as errors whose detail is the pin id.
    Result<PinState> PinAdd(const PinSpec& spec);
    Result<std::vector<PinState>> PinList();
    Result<uint32_t> PinClear(uint32_t id = 0); // 0: all; returns how many.
    // Trials: one write the agent undoes (protocol.h). Clear restores active
    // ones and returns the final state of each cleared trial.
    Result<TrialState> TrialAdd(const TrialSpec& spec);
    Result<std::vector<TrialState>> TrialList();
    Result<std::vector<TrialState>> TrialClear(uint32_t id = 0); // 0: all.
    // Watch: start sampling; read the rows so far (all pages); stop early.
    Result<uint32_t> WatchStart(const WatchSpec& spec); // Rows the buffer holds.
    Result<WatchRows> WatchRead(const WatchSpec& spec, uint32_t first = 0);
    Result<uint32_t> WatchStop();
    // The running or last capture's spec and state, for a client that did not start it.
    Result<WatchInfo> WatchStatus();
    // The roots the host offers to follow (empty: none).
    Result<std::vector<std::string>> WatchRoots();
    // What the memory at an address is (one VirtualQuery in the target).
    Result<RegionInfo> Region(uint64_t address);
    // Follows a pointer chain with held reads (one per hop). A null or
    // unreadable hop is a result (`broken`), not an error.
    Result<PointerChain> ResolveChain(uint64_t base, std::span<const uint64_t> offsets);
    // Loaded modules, the process exe first (all pages).
    Result<std::vector<Module>> Modules();
    // Ops the agent handles, from the section header.
    std::vector<uint32_t> SupportedOps() const;
    bool Supports(uint32_t op) const;

private:
    Client(Section* section, void* guard) : section_(section), guard_(guard) {}
    void Release();
    Section* section_ = nullptr;
    void* guard_ = nullptr; // Named object held while attached; marks the agent as in use.
    std::chrono::milliseconds timeout_{5000};
    WaitNotice notice_ = nullptr;
    ClientStats stats_{};
    uint32_t holdDepth_ = 0;
    uint64_t holdFirstTick_ = 0, holdLastTick_ = 0, holdSpanTick_ = 0;
    int holdPriority_ = 0; // Thread priority restored when the outermost Hold ends.
    bool timerRaised_ = false;
};

// Error for a refused response (status != Ok).
Error AgentError(const Response& response);
}
