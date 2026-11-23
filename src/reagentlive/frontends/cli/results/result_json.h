// reagentlive CLI: the core commands' results as typed structs (schema.h) and
// the shared envelopes, built from client results only (no I/O). The verb
// table (results/verbs.h) names each verb's type; docs/commands/results.md is
// generated from it. The Unreal pack's are in packs/ue/cli/results/result_json_ue.h.
//
// Conventions every result follows:
//   rows          the result's list.           total        how many there are in all, when rows may show fewer.
//   from, next_cursor   a page: where it began, and what to pass as --start for the next (absent when done).
//   capped        rows were cut at a hard limit; there is no cursor.
//   truncated     a value in the result is incomplete.
//   module, rva   beside an address inside a loaded module (absent elsewhere), so it opens in a static tool.
//   A key that does not apply is omitted; there are no nulls outside game values.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "reagentlive/frontends/cli/results/schema.h"
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/frontends/cli/values/json.h"

namespace reagentlive::cli {
// The loaded module holding an address, or null.
inline const Module* ModuleAt(uint64_t address, std::span<const Module> modules) {
    for (const auto& module : modules)
        if (address >= module.base && address - module.base < module.size) return &module;
    return nullptr;
}

namespace shape {
// `module` and `rva` beside an address inside a loaded module.
struct Location {
    std::optional<std::string> module;
    std::optional<HexNum> rva;
    template<class V> static void Describe(V& v) {
        v("module", &Location::module);
        v("rva", &Location::rva);
    }
};
}
inline shape::Location Locate(uint64_t address, std::span<const Module> modules) {
    const auto* module = address ? ModuleAt(address, modules) : nullptr;
    if (!module) return {};
    return {module->name, HexNum(address - module->base)};
}

namespace shape {
struct ModuleRow {
    std::string name;
    Addr base;
    uint64_t size = 0;
    template<class V> static void Describe(V& v) {
        v("name", &ModuleRow::name);
        v("base", &ModuleRow::base);
        v("size", &ModuleRow::size);
    }
};
struct Modules {
    std::vector<ModuleRow> rows;
    template<class V> static void Describe(V& v) { v("rows", &Modules::rows); }
};
struct Quit {
    bool quit = true;
    template<class V> static void Describe(V& v) { v("quit", &Quit::quit); }
};

// An object's outer: a function's or property's class, a subobject's owner.
struct ObjectOuter {
    std::string name;
    RefId ref;
    template<class V> static void Describe(V& v) {
        v("name", &ObjectOuter::name);
        v("ref", &ObjectOuter::ref);
    }
};
// An object as a pack names it: peek's object rows, describe, and every
// object a pack's result names. The core defines the shape; a pack fills it.
// class_size only from a single describe: the object's class size, which
// reaches past the last reflected field (native members, object tails).
// outer on find and describe rows; absent for a package.
struct Object {
    static constexpr std::string_view TypeName = "Object";
    RefId ref;
    std::string name, className;
    std::optional<uint32_t> classSize;
    std::optional<ObjectOuter> outer;
    template<class V> static void Describe(V& v) {
        v("ref", &Object::ref);
        v("name", &Object::name);
        v("class", &Object::className);
        v("class_size", &Object::classSize);
        v("outer", &Object::outer);
    }
};

// A resolved pointer chain: the base, each hop (where a pointer was read and
// what it held), and the final address; in an error, the broken hop's index
// and reason instead.
struct ChainHop {
    uint64_t hop = 0;
    Addr address, value;
    Location where;
    template<class V> static void Describe(V& v) {
        v("hop", &ChainHop::hop);
        v("address", &ChainHop::address);
        v("value", &ChainHop::value);
        v.flat("", &ChainHop::where);
    }
};
struct Chain {
    static constexpr std::string_view TypeName = "Chain";
    Addr base;
    std::vector<ChainHop> hops;
    std::optional<Addr> address;
    Location where;
    std::optional<uint64_t> broken;
    std::optional<std::string> reason;
    template<class V> static void Describe(V& v) {
        v("base", &Chain::base);
        v("hops", &Chain::hops);
        v("address", &Chain::address);
        v.flat("", &Chain::where);
        v("broken", &Chain::broken);
        v("reason", &Chain::reason);
    }
};

// read: a 4- or 8-byte read in every plain form.
struct ReadViews {
    std::optional<int32_t> i32;
    std::optional<uint32_t> u32;
    std::optional<Json> f32;
    std::optional<int64_t> i64;
    std::optional<uint64_t> u64;
    std::optional<Addr> ptr;
    Location ptrWhere;
    std::optional<Json> f64;
    template<class V> static void Describe(V& v) {
        v("i32", &ReadViews::i32);
        v("u32", &ReadViews::u32);
        v("f32", &ReadViews::f32);
        v("i64", &ReadViews::i64);
        v("u64", &ReadViews::u64);
        v("ptr", &ReadViews::ptr);
        v.flat("ptr_", &ReadViews::ptrWhere);
        v("f64", &ReadViews::f64);
    }
};
// read and peek: the memory an address lies in.
struct Region {
    static constexpr std::string_view TypeName = "Region";
    std::string kind;
    Addr base;
    HexNum size;
    std::string protect;
    template<class V> static void Describe(V& v) {
        v("kind", &Region::kind, "code|rodata|data|mapped|private|reserved|free");
        v("base", &Region::base);
        v("size", &Region::size);
        v("protect", &Region::protect);
    }
};
struct Read {
    Addr address;
    Location where;
    uint64_t size = 0;
    HexBytes hex;
    std::optional<ReadViews> views;
    std::optional<std::vector<Json>> values;    // --type <type>: one per element.
    std::optional<Json> layout;                 // --type "<layout>": {name: value}.
    std::optional<std::string> text;            // --type cstr|wstr.
    std::optional<uint64_t> length;
    std::optional<bool> terminated;
    std::optional<Region> region;
    std::optional<Chain> chain;
    template<class V> static void Describe(V& v) {
        v("address", &Read::address);
        v.flat("", &Read::where);
        v("size", &Read::size);
        v("hex", &Read::hex);
        v("views", &Read::views);
        v("values", &Read::values);
        v("layout", &Read::layout);
        v("text", &Read::text);
        v("length", &Read::length);
        v("terminated", &Read::terminated);
        v("region", &Read::region);
        v("chain", &Read::chain);
    }
};
// A timed write, as write --undo and --hold answer it and writes lists it.
// `id` is undo:N (--undo: the agent restores it) or hold:N (--hold: rewritten
// every tick). An --undo write adds what it restores (`original`), its owner
// and the tick memory changed; a --hold one whether it is guarded on an
// object and how often it wrote.
struct TimedWrite {
    static constexpr std::string_view TypeName = "TimedWrite";
    std::string id, mode;
    Addr address;
    HexBytes bytes;
    std::string state;
    uint32_t remainingMs = 0;
    std::optional<HexBytes> original;
    std::optional<Addr> owner, object;
    std::optional<uint64_t> changedTick;
    std::optional<std::string> guard;
    std::optional<bool> guarded;
    std::optional<uint32_t> writes;
    template<class V> static void Describe(V& v) {
        v("id", &TimedWrite::id);
        v("mode", &TimedWrite::mode, "undo|hold");
        v("address", &TimedWrite::address);
        v("bytes", &TimedWrite::bytes);
        v("state", &TimedWrite::state,
          "active|restored|conflict|abandoned|expired|object changed|not writable|memory differed from --expect|"
          "cleared|access faulted|unknown");
        v("remaining_ms", &TimedWrite::remainingMs);
        v("original", &TimedWrite::original);
        v("owner", &TimedWrite::owner);
        v("object", &TimedWrite::object);
        v("changed_tick", &TimedWrite::changedTick);
        v("guard", &TimedWrite::guard, "owner replaced or freed|object vtable changed|target unreadable|unknown");
        v("guarded", &TimedWrite::guarded);
        v("writes", &TimedWrite::writes);
    }
};
// writes: every timed write; with clear, `cleared` and the rows it ended.
struct Writes {
    std::vector<TimedWrite> rows;
    std::optional<uint64_t> cleared;
    template<class V> static void Describe(V& v) {
        v("rows", &Writes::rows);
        v("cleared", &Writes::cleared);
    }
};

struct Write {
    Addr address;
    Location where;
    uint64_t size = 0;
    std::optional<TimedWrite> timed;    // --undo, --hold.
    template<class V> static void Describe(V& v) {
        v("address", &Write::address);
        v.flat("", &Write::where);
        v("size", &Write::size);
        v("timed", &Write::timed);
    }
};
struct AddressRow {
    Addr address;
    Location where;
    std::optional<std::string> text;  // search text: the printable string around the hit.
    template<class V> static void Describe(V& v) {
        v("address", &AddressRow::address);
        v.flat("", &AddressRow::where);
        v("text", &AddressRow::text);
    }
};
struct Search {
    std::vector<AddressRow> rows;
    Addr from;
    std::optional<Addr> nextCursor;
    template<class V> static void Describe(V& v) {
        v("rows", &Search::rows);
        v("from", &Search::from);
        v("next_cursor", &Search::nextCursor);
    }
};

// watch: a range is at an address, or following a root at an offset from it.
struct WatchRange {
    static constexpr std::string_view TypeName = "WatchRange";
    std::optional<Addr> address;
    std::optional<HexNum> offset;
    uint32_t length = 0;
    std::optional<std::string> type;    // A typed range: f64, or i32[2] for several.
    bool guarded = false;
    template<class V> static void Describe(V& v) {
        v("address", &WatchRange::address);
        v("offset", &WatchRange::offset);
        v("length", &WatchRange::length);
        v("type", &WatchRange::type);
        v("guarded", &WatchRange::guarded);
    }
};
// One sample: a value per range, null while unreadable: the bytes as hex, or
// for a typed range a number (a list of them for [n]). Following a root,
// `lost` rows and `resumed` ones (with the new object's address) bound each gap.
struct WatchSample {
    static constexpr std::string_view TypeName = "WatchSample";
    uint64_t tick = 0, us = 0;
    std::optional<std::vector<Json>> values;
    std::optional<bool> lost, resumed;
    std::optional<Addr> root;
    template<class V> static void Describe(V& v) {
        v("tick", &WatchSample::tick);
        v("us", &WatchSample::us);
        v("values", &WatchSample::values);
        v("lost", &WatchSample::lost);
        v("resumed", &WatchSample::resumed);
        v("root", &WatchSample::root);
    }
};
struct WatchGap {
    uint64_t fromUs = 0;
    std::optional<uint64_t> toUs;
    template<class V> static void Describe(V& v) {
        v("from_us", &WatchGap::fromUs);
        v("to_us", &WatchGap::toUs);
    }
};
// A typed range's value over the capture: `element` for one of an [n] range.
// per_second: (last - first) over the time between them; absent with one sample.
struct WatchSummary {
    static constexpr std::string_view TypeName = "WatchSummary";
    uint32_t range = 0;
    std::optional<uint32_t> element;
    uint64_t samples = 0, changes = 0; // changes: samples whose value differs from the one before.
    double first = 0, last = 0, least = 0, greatest = 0;
    std::optional<double> perSecond;
    template<class V> static void Describe(V& v) {
        v("range", &WatchSummary::range);
        v("element", &WatchSummary::element);
        v("samples", &WatchSummary::samples);
        v("changes", &WatchSummary::changes);
        v("first", &WatchSummary::first);
        v("last", &WatchSummary::last);
        v("least", &WatchSummary::least);
        v("greatest", &WatchSummary::greatest);
        v("per_second", &WatchSummary::perSecond);
    }
};
// watch --then: what the call right after the start returned.
struct WatchThen {
    bool ok = false;
    std::optional<HexBytes> hex;
    std::optional<std::string> message;
    template<class V> static void Describe(V& v) {
        v("ok", &WatchThen::ok);
        v("hex", &WatchThen::hex);
        v("message", &WatchThen::message);
    }
};
constexpr auto WatchStops = "deadline|stopped|buffer full|object replaced";
struct Watch {
    uint32_t capacity = 0;
    bool complete = false;
    std::optional<std::string> stopped;
    std::vector<WatchRange> ranges;
    std::optional<WatchThen> then;
    std::vector<WatchSample> rows;
    std::optional<uint64_t> samples, dropped; // See WatchChanges.
    std::optional<std::vector<WatchSummary>> summary;
    template<class V> static void Describe(V& v) {
        v("capacity", &Watch::capacity);
        v("complete", &Watch::complete);
        v("stopped", &Watch::stopped, WatchStops);
        v("ranges", &Watch::ranges);
        v("then", &Watch::then);
        v("rows", &Watch::rows);
        v("samples", &Watch::samples);
        v("dropped", &Watch::dropped);
        v("summary", &Watch::summary);
    }
};
struct WatchStart {
    uint32_t capacity = 0, ms = 0, intervalMs = 0;
    std::optional<std::string> root;
    std::vector<WatchRange> ranges;
    template<class V> static void Describe(V& v) {
        v("capacity", &WatchStart::capacity);
        v("ms", &WatchStart::ms);
        v("interval_ms", &WatchStart::intervalMs);
        v("root", &WatchStart::root);
        v("ranges", &WatchStart::ranges);
    }
};
// watch-read and watch-stop: the agent's record of the capture.
struct WatchCapture {
    bool running = false;
    std::optional<std::string> stopped;
    uint32_t capacity = 0, ms = 0, intervalMs = 0;
    std::optional<std::string> root;
    std::vector<WatchRange> ranges;
    std::vector<WatchSample> rows;
    std::optional<uint64_t> samples, dropped; // See WatchChanges.
    std::optional<std::vector<WatchGap>> gaps;
    std::optional<std::vector<WatchSummary>> summary;
    template<class V> static void Describe(V& v) {
        v("running", &WatchCapture::running);
        v("stopped", &WatchCapture::stopped, WatchStops);
        v("capacity", &WatchCapture::capacity);
        v("ms", &WatchCapture::ms);
        v("interval_ms", &WatchCapture::intervalMs);
        v("root", &WatchCapture::root);
        v("ranges", &WatchCapture::ranges);
        v("rows", &WatchCapture::rows);
        v("samples", &WatchCapture::samples);
        v("dropped", &WatchCapture::dropped);
        v("gaps", &WatchCapture::gaps);
        v("summary", &WatchCapture::summary);
    }
};

// The rows a reader needs from a capture: the first sample, each sample that
// differs from the one before it (a value, or lost, resumed or a new root),
// and the last. Every transition keeps its time. Past `limit` rows the middle
// is dropped, half the limit kept from each end, and `dropped` counts it; the
// summary still covers every sample. `--all` skips this. Pure; tested.
constexpr size_t MaxWatchChangeRows = 200;
inline std::vector<WatchSample> WatchChanges(std::span<const WatchSample> rows, size_t limit, uint64_t& dropped) {
    dropped = 0;
    std::vector<WatchSample> kept;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        const bool marked = row.lost || row.resumed || row.root;
        if (i == 0 || i + 1 == rows.size() || marked || rows[i - 1].lost || row.values != rows[i - 1].values)
            kept.push_back(row);
    }
    if (limit < 2 || kept.size() <= limit) return kept;
    const size_t head = limit / 2, tail = limit - head;
    dropped = kept.size() - limit;
    std::vector<WatchSample> out(kept.begin(), kept.begin() + static_cast<std::ptrdiff_t>(head));
    out.insert(out.end(), kept.end() - static_cast<std::ptrdiff_t>(tail), kept.end());
    return out;
}

// The summary of each typed range's values, one entry per value (per element
// of an [n] range), from the samples that held a number for it.
inline std::vector<WatchSummary> SummariseWatch(std::span<const WatchRange> ranges, std::span<const WatchSample> rows) {
    std::vector<WatchSummary> out;
    for (uint32_t r = 0; r < ranges.size(); ++r) {
        if (!ranges[r].type) continue;
        const bool several = ranges[r].type->find('[') != std::string::npos;
        size_t elements = 0;
        for (const auto& row : rows)
            if (row.values && r < row.values->size() && (*row.values)[r].is_array())
                elements = std::max(elements, (*row.values)[r].size());
        if (!several) elements = 1;
        for (uint32_t e = 0; e < elements; ++e) {
            WatchSummary summary;
            summary.range = r;
            if (several) summary.element = e;
            uint64_t firstUs = 0, lastUs = 0;
            for (const auto& row : rows) {
                if (!row.values || r >= row.values->size()) continue;
                const Json& cell = (*row.values)[r];
                const Json* value = several ? (cell.is_array() && e < cell.size() ? &cell[e] : nullptr) : &cell;
                if (!value || !value->is_number()) continue;
                const double number = value->get<double>();
                if (!summary.samples) {
                    summary.first = summary.least = summary.greatest = number;
                    firstUs = row.us;
                }
                if (summary.samples && number != summary.last) ++summary.changes;
                summary.last = number;
                lastUs = row.us;
                summary.least = std::min(summary.least, number);
                summary.greatest = std::max(summary.greatest, number);
                ++summary.samples;
            }
            if (!summary.samples) continue;
            if (summary.samples > 1 && lastUs > firstUs)
                summary.perSecond = (summary.last - summary.first) * 1e6 / static_cast<double>(lastUs - firstUs);
            out.push_back(summary);
        }
    }
    return out;
}

// One confirmed label for the static tool; also a line of a labels file. The
// module's image size and timestamp tie it to one build.
struct Label {
    std::string module;
    uint64_t moduleSize = 0;
    uint32_t timestamp = 0;
    HexNum rva;
    std::string name, kind;
    template<class V> static void Describe(V& v) {
        v("module", &Label::module);
        v("module_size", &Label::moduleSize);
        v("timestamp", &Label::timestamp);
        v("rva", &Label::rva);
        v("name", &Label::name);
        v("kind", &Label::kind, "user|global|exec|vtable|interface");
    }
};

// peek: what one live qword holds. One label, the first that holds: object,
// module, heap (or another region kind), value. The name is only a hint, and
// only on a value row: almost any small number decodes to some name.
struct PeekRow {
    Addr address;
    Location where;
    std::string label;
    std::optional<HexNum> value;
    std::optional<Object> object;   // Filled by the pack.
    Location valueWhere;
    std::optional<std::vector<Json>> f32;
    std::optional<Json> f64;
    std::optional<std::string> nameHint;
    template<class V> static void Describe(V& v) {
        v("address", &PeekRow::address);
        v.flat("", &PeekRow::where);
        v("label", &PeekRow::label, "object|module|heap|value|unreadable|...");
        v("value", &PeekRow::value);
        v("object", &PeekRow::object);
        v.flat("value_", &PeekRow::valueWhere);
        v("f32", &PeekRow::f32);
        v("f64", &PeekRow::f64);
        v("name_hint", &PeekRow::nameHint);
    }
};
struct Peek {
    Addr address;
    Location where;
    std::vector<PeekRow> rows;
    std::optional<Label> labelled;
    std::optional<Region> region;
    std::optional<Chain> chain;
    template<class V> static void Describe(V& v) {
        v("address", &Peek::address);
        v.flat("", &Peek::where);
        v("region", &Peek::region);
        v("rows", &Peek::rows);
        v("labelled", &Peek::labelled);
        v("chain", &Peek::chain);
    }
};

struct BatchSpan {
    uint64_t index = 0;
    std::string command;
    template<class V> static void Describe(V& v) {
        v("index", &BatchSpan::index);
        v("command", &BatchSpan::command);
    }
};
// batch: one envelope per command in `rows` (each with `tick`, and
// `last_tick` when its responses spanned ticks); `spans_ticks` lists the
// commands during which the batch's tick moved.
struct Batch {
    bool allOk = false, sameTick = false;
    uint64_t firstTick = 0, lastTick = 0;
    std::vector<BatchSpan> spansTicks;
    std::vector<Json> rows;
    std::optional<bool> tickAccurate;
    template<class V> static void Describe(V& v) {
        v("all_ok", &Batch::allOk);
        v("same_tick", &Batch::sameTick);
        v("first_tick", &Batch::firstTick);
        v("last_tick", &Batch::lastTick);
        v("spans_ticks", &Batch::spansTicks);
        v("rows", &Batch::rows);
        v("tick_accurate", &Batch::tickAccurate);
    }
};

struct Status {
    uint32_t pid = 0;
    std::string build, version;
    std::string commit;          // The console's source commit (12 hex digits, -dirty), or unknown.
    uint32_t replacedAgents = 0;
    uint64_t ticks = 0, ticksPerSecond = 0, intervalMs = 0;
    bool ticking = false;
    std::string hostThread;
    std::optional<std::string> tickNote;
    std::optional<std::vector<std::string>> roots;
    std::vector<std::string> packs;
    std::string tableHash;
    template<class V> static void Describe(V& v) {
        v("pid", &Status::pid);
        v("build", &Status::build);
        v("version", &Status::version);
        v("commit", &Status::commit);
        v("replaced_agents", &Status::replacedAgents);
        v("ticks", &Status::ticks);
        v("ticks_per_second", &Status::ticksPerSecond);
        v("interval_ms", &Status::intervalMs);
        v("ticking", &Status::ticking);
        v("host_thread", &Status::hostThread, "engine|borrowed|own|unset");
        v("tick_note", &Status::tickNote);
        v("roots", &Status::roots);
        v("packs", &Status::packs);
        v("table_hash", &Status::tableHash);
    }
};

// help: one row per verb, with where it comes from (`origin`: the core, a
// pack or the host), `writes` on a verb that writes the target's memory or
// calls into it and `blocks` on one that holds the console for seconds.
// `help <verb>` adds the positionals and flags (each with its key, type and
// default), the result in notation, the named types it uses and the notes;
// `help --json` adds the result's type for programs, every named type once
// and each origin's title.
struct HelpFlag {
    std::string flag, key, takes, type;
    std::optional<std::string> value, help, fallback;
    std::optional<bool> required;
    template<class V> static void Describe(V& v) {
        v("flag", &HelpFlag::flag);
        v("key", &HelpFlag::key);
        v("takes", &HelpFlag::takes, "nothing|one|maybe|some|rest");
        v("type", &HelpFlag::type);
        v("required", &HelpFlag::required);
        v("value", &HelpFlag::value);
        v("default", &HelpFlag::fallback);
        v("help", &HelpFlag::help);
    }
};
struct HelpPositional {
    std::string key, name, arity, type;
    std::optional<std::string> help, fallback;
    template<class V> static void Describe(V& v) {
        v("key", &HelpPositional::key);
        v("name", &HelpPositional::name);
        v("arity", &HelpPositional::arity, "one|optional|some|any|rest");
        v("type", &HelpPositional::type);
        v("default", &HelpPositional::fallback);
        v("help", &HelpPositional::help);
    }
};
struct HelpRow {
    std::string verb, origin, usage, summary;
    std::optional<bool> writes, blocks;
    std::optional<std::vector<std::string>> aliases;
    std::optional<std::vector<HelpFlag>> flags;
    std::optional<std::string> result, notes;
    std::optional<std::vector<HelpPositional>> positionals;
    std::optional<std::map<std::string, std::string>> types;
    std::optional<Json> type;
    template<class V> static void Describe(V& v) {
        v("verb", &HelpRow::verb);
        v("origin", &HelpRow::origin);
        v("usage", &HelpRow::usage);
        v("summary", &HelpRow::summary);
        v("writes", &HelpRow::writes);
        v("blocks", &HelpRow::blocks);
        v("aliases", &HelpRow::aliases);
        v("flags", &HelpRow::flags);
        v("result", &HelpRow::result);
        v("notes", &HelpRow::notes);
        v("positionals", &HelpRow::positionals);
        v("types", &HelpRow::types);
        v("type", &HelpRow::type);
    }
};
// help --json: a named type's notation and its type for programs.
struct HelpType {
    std::string notation;
    Json type;
    template<class V> static void Describe(V& v) {
        v("notation", &HelpType::notation);
        v("type", &HelpType::type);
    }
};
struct HelpOrigin {
    std::string origin, title;
    template<class V> static void Describe(V& v) {
        v("origin", &HelpOrigin::origin);
        v("title", &HelpOrigin::title);
    }
};
struct Help {
    std::vector<HelpRow> rows;
    std::optional<std::vector<std::string>> notes;
    std::optional<std::string> tableHash;
    std::optional<std::map<std::string, HelpType>> types;
    std::optional<std::vector<HelpOrigin>> origins;
    template<class V> static void Describe(V& v) {
        v("rows", &Help::rows);
        v("notes", &Help::notes);
        v("table_hash", &Help::tableHash);
        v("types", &Help::types);
        v("origins", &Help::origins);
    }
};
}

// peek: what one live qword holds. The console fills it from one read,
// readability checks and, with an engine pack, one describe per
// pointer-shaped value and one name lookup.
struct PeekValue {
    uint64_t address = 0, value = 0;
    bool read = false;                  // False: this qword could not be read.
    std::optional<shape::Object> object; // The value is a live object, as the pack names it.
    bool readable = false;              // The value points to readable memory.
    std::string name;                   // The pack's name for the low 32 bits, empty if none.
    std::string region;                 // Readable pointer: its region kind when the agent has region.
};

// Values worth a describe or a readability check: user-mode, past the null page.
inline bool PointerShaped(uint64_t value) { return value >= 0x10000 && value < 0x7fffffff0000; }

inline shape::PeekRow MakePeekRow(const PeekValue& v, std::span<const Module> modules) {
    shape::PeekRow row;
    row.address = v.address;
    row.where = Locate(v.address, modules);
    if (!v.read) {
        row.label = "unreadable";
        return row;
    }
    row.value = HexNum(v.value);
    if (v.object) {
        row.label = "object";
        row.object = *v.object;
    } else if (ModuleAt(v.value, modules)) {
        row.label = "module";
        row.valueWhere = Locate(v.value, modules);
    } else if (v.readable && PointerShaped(v.value)) {
        // Private memory is heap (or a stack); a mapped view or anything else says so.
        row.label = v.region.empty() || v.region == "private" ? "heap" : v.region;
    } else {
        uint8_t bytes[8];
        memcpy(bytes, &v.value, sizeof(bytes));
        row.label = "value";
        row.f32 = std::vector<Json>{JsonNumber<float>(std::span(bytes, 4)), JsonNumber<float>(std::span(bytes + 4, 4))};
        row.f64 = JsonNumber<double>(std::span(bytes, 8));
        if (!v.name.empty()) row.nameHint = v.name;
    }
    return row;
}

inline shape::Label MakeLabel(const Module& module, uint32_t timestamp, uint64_t address, std::string_view name,
                              std::string_view kind) {
    return {module.name, module.size, timestamp, HexNum(address - module.base), std::string(name), std::string(kind)};
}

inline shape::ModuleRow MakeModule(const Module& module) { return {module.name, module.base, module.size}; }

inline shape::Chain MakeChain(uint64_t base, const PointerChain& chain, std::span<const Module> modules) {
    shape::Chain out;
    out.base = base;
    for (size_t i = 0; i < chain.hops.size(); ++i)
        out.hops.push_back({i, chain.hops[i].address, chain.hops[i].value, Locate(chain.hops[i].address, modules)});
    if (chain.broken) {
        out.broken = *chain.broken;
        out.reason = chain.reason;
    } else {
        out.address = Addr(chain.address);
        out.where = Locate(chain.address, modules);
    }
    return out;
}

// batch: where each command ran. `before` is the batch's last tick when the
// command started (0 before any response); first and last are its own
// responses' ticks (0 when it sent no request).
struct BatchItemTicks { uint64_t before = 0, first = 0, last = 0; };

inline shape::Batch MakeBatch(std::vector<Json> results, std::span<const BatchItemTicks> ticks, uint64_t first,
                              uint64_t last) {
    shape::Batch batch;
    for (size_t i = 0; i < results.size() && i < ticks.size(); ++i) {
        const auto& item = ticks[i];
        if (!item.first) continue;
        results[i]["tick"] = item.first;
        if (item.last != item.first) results[i]["last_tick"] = item.last;
        if (item.last > (item.before ? item.before : item.first))
            batch.spansTicks.push_back({i, results[i].value("command", "")});
    }
    batch.allOk = std::ranges::all_of(results, [](const Json& r) { return r.value("ok", false); });
    batch.sameTick = first == last;
    batch.firstTick = first;
    batch.lastTick = last;
    batch.rows = std::move(results);
    return batch;
}

// Envelopes: every line is {command, ok, result} or {command, ok, error}.
inline Json ResultEnvelope(std::string_view command, Json result) {
    return {{"command", command}, {"ok", true}, {"result", std::move(result)}};
}
// Validation and console errors: kind and message only.
inline Json ErrorEnvelope(std::string_view command, std::string_view kind, std::string_view message) {
    return {{"command", command}, {"ok", false}, {"error", {{"kind", kind}, {"message", message}}}};
}
// Client errors add the agent's status, detail and reason.
inline Json ErrorEnvelope(std::string_view command, const Error& error) {
    const char* kind = error.kind == Error::Kind::Timeout ? "timeout" :
        error.kind == Error::Kind::Attach ? "attach" : error.kind == Error::Kind::Busy ? "busy" : "agent";
    return {{"command", command}, {"ok", false}, {"error", {
        {"kind", kind}, {"message", error.message}, {"status", static_cast<uint32_t>(error.status)},
        {"detail", error.detail}, {"reason", error.reason}}}};
}
}
