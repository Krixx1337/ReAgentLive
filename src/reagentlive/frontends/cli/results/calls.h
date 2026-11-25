// reagentlive CLI: result structs (results/schema.h) of the pointer-swap
// recorders: native-calls-start, and calls-read for every kind (a pack's
// rows fill the keys its kind has, through console/pack.h). Pure: tested.
#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "reagentlive/core/protocol.h"
#include "reagentlive/frontends/cli/results/result_json.h"

namespace reagentlive::cli {
namespace shape {
// native-calls-start: the recording armed (or with `dry`, what would be swapped).
struct NativeCallsStart {
    uint32_t slot = 0;
    std::string target;                 // vtable|cell
    Addr cell, original, thunk;
    Location originalWhere;
    std::optional<Addr> table;          // vtable: the object's own table.
    std::optional<uint32_t> entry;      // vtable: the entry index.
    uint64_t durationMs = 0;
    bool dry = false;
    std::string note;
    template<class V> static void Describe(V& v) {
        v("slot", &NativeCallsStart::slot);
        v("target", &NativeCallsStart::target, "vtable|cell");
        v("cell", &NativeCallsStart::cell);
        v("original", &NativeCallsStart::original);
        v.flat("original_", &NativeCallsStart::originalWhere);
        v("table", &NativeCallsStart::table);
        v("entry", &NativeCallsStart::entry);
        v("thunk", &NativeCallsStart::thunk);
        v("duration_ms", &NativeCallsStart::durationMs);
        v("dry", &NativeCallsStart::dry);
        v("note", &NativeCallsStart::note);
    }
};
// One parameter of a recorded call, decoded by a pack's reflection.
struct CallsValue {
    std::string name, direction;
    Json value;
    bool truncated = false;
    template<class V> static void Describe(V& v) {
        v("name", &CallsValue::name);
        v("direction", &CallsValue::direction, "in|out|return");
        v("value", &CallsValue::value);
        v("truncated", &CallsValue::truncated);
    }
};
struct CallsRow {
    uint64_t sequence = 0;
    double atMs = 0;                    // Since the recording began.
    uint32_t thread = 0;
    // calls-start (a pack's): the calling object and its parameters.
    std::optional<RefId> ref;
    std::optional<std::string> objectName, objectClass, via;
    std::optional<std::vector<CallsValue>> values;
    // native-calls-start: rcx, rdx, r8, r9; xmm0..3's low 8 bytes; where the
    // call returns (module, rva, a labels name); the guarded copy.
    std::optional<std::vector<HexNum>> args, xmm;
    std::optional<Addr> ret;
    Location retWhere;
    std::optional<std::string> retName;
    std::optional<HexBytes> bytes;
    std::optional<bool> copyFaulted;
    template<class V> static void Describe(V& v) {
        v("sequence", &CallsRow::sequence);
        v("at_ms", &CallsRow::atMs);
        v("thread", &CallsRow::thread);
        v("ref", &CallsRow::ref);
        v("object_name", &CallsRow::objectName);
        v("object_class", &CallsRow::objectClass);
        v("via", &CallsRow::via, "event|vm");
        v("values", &CallsRow::values);
        v("args", &CallsRow::args);
        v("xmm", &CallsRow::xmm);
        v("ret", &CallsRow::ret);
        v.flat("ret_", &CallsRow::retWhere);
        v("ret_name", &CallsRow::retName);
        v("bytes", &CallsRow::bytes);
        v("copy_faulted", &CallsRow::copyFaulted);
    }
};
// calls-read, with or without stop: any recording's state and its rows.
struct Calls {
    std::string recorder, state;        // recorder: the verb that started it.
    uint32_t slot = 0;
    uint64_t calls = 0, dropped = 0;
    std::optional<std::string> function;    // calls-start: Owner:Name.
    std::optional<uint64_t> events, vm;     // calls-start: by path.
    std::optional<Addr> target, original;   // native-calls-start: the object or cell, and the code swapped.
    Location originalWhere;
    std::optional<uint64_t> copied, faulted; // native-calls-start with copy.
    std::vector<CallsRow> rows;
    std::optional<uint64_t> nextCursor;
    std::optional<uint32_t> unnamed;    // Distinct pointers past the lookup cap, left as addresses.
    template<class V> static void Describe(V& v) {
        v("recorder", &Calls::recorder, "calls-start|native-calls-start");
        v("state", &Calls::state, "recording|expired|finished|replaced");
        v("slot", &Calls::slot);
        v("calls", &Calls::calls);
        v("dropped", &Calls::dropped);
        v("function", &Calls::function);
        v("events", &Calls::events);
        v("vm", &Calls::vm);
        v("target", &Calls::target);
        v("original", &Calls::original);
        v.flat("original_", &Calls::originalWhere);
        v("copied", &Calls::copied);
        v("faulted", &Calls::faulted);
        v("rows", &Calls::rows);
        v("next_cursor", &Calls::nextCursor);
        v("unnamed", &Calls::unnamed);
    }
};
}

inline const char* RecorderStateText(uint32_t state) {
    switch (static_cast<RecorderState>(state)) {
    case RecorderState::Recording: return "recording";
    case RecorderState::Expired: return "expired";
    case RecorderState::Finished: return "finished";
    case RecorderState::Replaced: return "replaced";
    case RecorderState::Free: break;
    }
    return "free";
}

// What every kind's result shares: state, slot, counts, the rows' sequence,
// time and thread, and the cursor. A kind's own keys are its builder's.
inline shape::Calls MakeCallsBase(const RecorderStatus& status, std::span<const RecorderCall> calls, uint64_t after,
                                  uint64_t last) {
    shape::Calls out;
    out.state = RecorderStateText(status.state);
    out.slot = status.slot;
    out.calls = status.written;
    // Rows the agent no longer held when read: before the first one returned.
    const uint64_t first = calls.empty() ? last + 1 : calls.front().row.sequence;
    out.dropped = first > after + 1 ? first - after - 1 : 0;
    for (const auto& call : calls) {
        shape::CallsRow row;
        row.sequence = call.row.sequence;
        row.atMs = static_cast<double>(call.row.microseconds) / 1000.0;
        row.thread = call.row.thread;
        out.rows.push_back(std::move(row));
    }
    if (last < status.written) out.nextCursor = last;
    return out;
}

// A native-calls-start recording: its registers and copy per row. Return
// addresses are located in `modules`; `names` gives each a labels name
// (empty: none).
template<class Name>
inline shape::Calls MakeNativeCalls(const RecorderStatus& status, std::span<const RecorderCall> calls, uint64_t after,
                                    uint64_t last, std::span<const Module> modules, Name&& names) {
    shape::Calls out = MakeCallsBase(status, calls, after, last);
    out.recorder = "native-calls-start";
    out.target = Addr(status.target);
    out.original = Addr(status.original);
    out.originalWhere = Locate(status.original, modules);
    if (status.payload > sizeof(NativeCall)) {
        out.copied = status.counts[NativeCopiedCount];
        out.faulted = status.counts[NativeFaultedCount];
    }
    for (size_t i = 0; i < calls.size(); ++i) {
        const auto& payload = calls[i].payload;
        if (payload.size() < sizeof(NativeCall)) continue;
        NativeCall call{};
        memcpy(&call, payload.data(), sizeof(call));
        auto& row = out.rows[i];
        row.args = std::vector<HexNum>(std::begin(call.args), std::end(call.args));
        row.xmm = std::vector<HexNum>(std::begin(call.xmm), std::end(call.xmm));
        row.ret = Addr(call.returnAddress);
        row.retWhere = Locate(call.returnAddress, modules);
        if (std::string name = names(call.returnAddress); !name.empty()) row.retName = std::move(name);
        if (calls[i].row.tag & NativeCopied)
            row.bytes = HexBytes(std::span<const uint8_t>(payload).subspan(sizeof(NativeCall)));
        else if (status.payload > sizeof(NativeCall))
            row.copyFaulted = true;
    }
    return out;
}
}
