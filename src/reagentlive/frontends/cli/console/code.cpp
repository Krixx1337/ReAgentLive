// reagentlive CLI console: the fast lane's code questions about one address.
// disasm (decoded in the console from one read) and xrefs (the agent's
// relative-reference scan and pointer search, kinds named here), both with
// function bounds from the module's exception directory; touches (the
// [reg + disp] uses of struct offsets in the code around an address).
#include <algorithm>
#include <array>
#include <map>
#include <cstring>
#include <chrono>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/code/code_json.h"
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/code/disasm.h"
#include "reagentlive/frontends/cli/code/function_save.h"
#include "reagentlive/frontends/cli/code/pe_names.h"
#include "reagentlive/frontends/cli/code/unwind.h"
#include "reagentlive/frontends/cli/code/xrefs.h"
#include "reagentlive/frontends/cli/results/labels.h"
#include "reagentlive/core/client.h"

#include "reagentlive/frontends/cli/console/internal.h"

namespace reagentlive::cli::detail {
std::expected<const image::Layout*, std::string> LayoutOf(Session& session, const Module& module) {
    auto found = session.layouts.find(module.base);
    if (found == session.layouts.end()) {
        auto layout = image::ReadLayout(AgentRead(session), module);
        if (!layout) return std::unexpected(layout.error());
        found = session.layouts.emplace(module.base, std::move(*layout)).first;
    }
    return &found->second;
}

bool InCode(Session& session, uint64_t address) {
    const Module* module = ModuleAt(address, LoadedModules(session));
    if (!module) return false;
    auto layout = LayoutOf(session, *module);
    if (!layout) return false;
    const auto* section = (*layout)->At(address - module->base);
    return section && section->Exec();
}

std::expected<std::optional<unwind::Function>, std::string> FunctionAt(Session& session, uint64_t address) {
    const auto modules = LoadedModules(session);
    const Module* module = ModuleAt(address, modules);
    if (!module) return std::nullopt;
    auto& table = session.unwindTables[module->base];
    if (!table) {
        auto layout = LayoutOf(session, *module);
        if (!layout) return std::unexpected(layout.error());
        table = std::make_unique<unwind::Table>(AgentRead(session), *module, (*layout)->exception);
    }
    return table->Lookup(address);
}

namespace {
// The end of straight-line code at an address: its entry's (or chunk's) end.
std::optional<uint64_t> EntryEnd(Session& session, uint64_t address) {
    auto function = FunctionAt(session, address);
    if (!function || !*function) return std::nullopt;
    return (*function)->chunk ? (*function)->chunk->end : (*function)->range.end;
}

}

namespace {
// The piece of code no exception-table entry lists that holds `address`, cut
// as touches cuts such code (at returns and int3 padding), swept from the
// window's start. Empty when an entry holds it or it lies outside the
// executable sections.
// `follows`: set to whether the address lies within save::JunkBytes of the
// last end of straight-line code in its piece (touch::NearEnd): a start.
std::expected<std::optional<touch::Piece>, std::string> UnlistedPiece(Session& session, const Module& module,
                                                                      uint64_t address, bool* follows = nullptr) {
    auto layout = LayoutOf(session, module);
    if (!layout) return std::unexpected(layout.error());
    const auto* section = (*layout)->At(address - module.base);
    if (!section || !section->Exec()) return std::nullopt;
    const uint64_t start = module.base + section->rva, end = start + section->size;
    auto& table = session.unwindTables[module.base];
    if (!table) table = std::make_unique<unwind::Table>(AgentRead(session), module, (*layout)->exception);
    const uint64_t from = std::max(start, address - std::min(address, touch::PieceReach));
    auto ranges = table->Between(from, std::min(end, address + touch::PieceReach));
    if (!ranges) return std::unexpected(ranges.error());
    std::vector<touch::Piece> entries;
    for (const auto& range : *ranges) entries.push_back({range.start, range.end});
    // An entry that began before the window and runs into it.
    if (auto first = FunctionAt(session, from); first && *first) {
        const auto& held = (*first)->chunk ? *(*first)->chunk : (*first)->range;
        entries.push_back({held.start, held.end});
    }
    const auto window = touch::Window(address, start, end, entries);
    if (!window) return std::nullopt;
    // Unreadable parts stay int3 and decode to nothing.
    std::vector<uint8_t> code(static_cast<size_t>(window->end - window->start), 0xcc);
    const auto read = AgentRead(session);
    for (uint64_t at = window->start; at < window->end;) {
        std::vector<ReadSpan> spans{{at, std::min<uint64_t>(MaxRead - sizeof(Status), window->end - at)}};
        at += spans[0].length;
        auto items = read(spans);
        if (!items) return std::unexpected(items.error());
        if (!items->empty() && (*items)[0])
            std::copy((*items)[0]->begin(), (*items)[0]->end(), code.begin() + (spans[0].address - window->start));
    }
    const auto piece = touch::PieceAt(code, window->start, address);
    if (piece && follows) *follows = touch::NearEnd(code, window->start, piece->start, address, save::JunkBytes);
    return piece;
}
} // namespace
void RunDisasm(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(op::ReadBatch)) return JsonError(session, jsonCommand, "unsupported", "disasm needs read-batch");
    const Client::Hold hold(client);
    uint64_t address = command.request.address;
    auto function = FunctionAt(session, address);
    if (!function) return JsonError(session, jsonCommand, "unwind", function.error());
    // --function with no entry holding the address (a leaf function, or an
    // image whose directory leaves code out): walk from the address instead.
    const bool walk = command.function && !*function;
    if (command.function && *function && !command.resume) address = (*function)->range.start;
    const auto modules = LoadedModules(session);
    // A walk from well inside unlisted code lists part of a function: say
    // where its piece starts, and refuse to save that part unasked.
    std::optional<touch::Piece> piece;
    if (const Module* module = walk ? ModuleAt(address, modules) : nullptr) {
        bool follows = false;
        auto held = UnlistedPiece(session, *module, address, &follows);
        if (!held) return JsonError(session, jsonCommand, "unwind", held.error());
        if (*held && !follows) piece = *held;
    }
    if (piece && !command.file.empty() && !command.resume)
        return JsonError(session, jsonCommand, "start", "the address lies " + save::Hex(address - piece->start) +
            " bytes into unlisted code that starts at " + save::Hex(piece->start) + ": save from the function's start "
            "(junk bytes may lead the piece), or pass --resume to save from the address");
    // Read-only: inside a loaded module, in a section that is not writable.
    const dis::ConstantFn constant = [&session, &modules](uint64_t at) {
        const Module* module = ModuleAt(at, modules);
        if (!module) return false;
        auto layout = LayoutOf(session, *module);
        if (!layout) return false;
        const auto* section = (*layout)->At(at - module->base);
        return section && !(section->flags & image::Writable);
    };
    // [rip + disp] targets in a module's import table: the DLL function the slot holds.
    std::map<uint64_t, std::optional<pe::ImportDirectories>> directories;
    // One function listed from `start`, walking (no entry) or bounded by its
    // entry, with strings, import names and constant values added.
    const auto list = [&](uint64_t start, uint64_t count, bool walking,
                          bool follow) -> std::expected<dis::Listing, std::string> {
        dis::Options options;
        options.count = count;
        options.follow = follow;
        options.walk = walking;
        if (command.function) options.bytes = dis::MaxFunctionBytes;
        if (command.function && !walking) options.bound = [&session](uint64_t at) { return EntryEnd(session, at); };
        auto listing = dis::List(AgentRead(session), start, options);
        if (!listing) return std::unexpected(listing.error());
        if (auto strings = dis::AddStrings(AgentRead(session), listing->rows, modules); !strings)
            return std::unexpected(strings.error());
        for (auto& row : listing->rows) {
            if (!row.target || !row.ripTarget || row.string) continue;
            const Module* module = ModuleAt(*row.target, modules);
            if (!module) continue;
            auto known = directories.find(module->base);
            if (known == directories.end())
                known = directories.emplace(module->base, pe::ReadImportDirectories(AgentRead(session), *module)).first;
            if (known->second) row.import = pe::ImportAt(AgentRead(session), *module, *known->second, *row.target);
        }
        if (auto values = dis::AddValues(AgentRead(session), listing->rows, constant); !values)
            return std::unexpected(values.error());
        return listing;
    };
    auto listing = list(address, command.limit, walk, command.follow);
    if (!listing) return JsonError(session, jsonCommand, "read", listing.error());
    // An entry whose start does not decode (a packed image's, a few bytes
    // early): list from the address given instead, still bounded by the entry.
    if (listing->rows.empty() && address != command.request.address) {
        address = command.request.address;
        listing = list(address, command.limit, false, command.follow);
        if (!listing) return JsonError(session, jsonCommand, "read", listing.error());
    }
    auto out = MakeDisasm(address, *listing, *function, modules, session.meta, command.function);
    if (piece) out.piece = shape::AddressRange{piece->start, piece->end};
    if (command.file.empty()) return Emit(session, jsonCommand, std::move(out));

    const Module* module = ModuleAt(address, modules);
    if (!module) return JsonError(session, jsonCommand, "module", "--save needs code inside a loaded module");
    const auto timestamp = ImageTimestamp(session, *module);
    if (!timestamp) return JsonError(session, jsonCommand, "file", "the module's PE header is unreadable");
    // The saved parts: each function, then its chained chunks (owners[i] is
    // 0 for a function, else the function's start).
    std::vector<uint64_t> starts{address}, owners{0};
    std::vector<dis::Listing> listings;
    listings.push_back(std::move(*listing));
    const auto held = [&](uint64_t at) {
        return std::ranges::any_of(listings, [at](const dis::Listing& part) {
            return !part.rows.empty() && part.rows.front().address <= at &&
                at < part.rows.back().address + part.rows.back().bytes.size();
        });
    };
    // Chained chunks of the function at `owner`: jumps out of its listed
    // parts into entries chained to its entry, each listed to its own end.
    size_t chunks = 0;
    const auto addChunks = [&](uint64_t owner) -> std::expected<void, std::string> {
        auto entry = FunctionAt(session, owner);
        if (!entry) return std::unexpected(entry.error());
        if (!*entry) return {};
        const uint64_t primary = (*entry)->range.start;
        for (size_t part = 0; part < listings.size(); ++part) {
            if (starts[part] != owner && owners[part] != owner) continue;
            for (const uint64_t target : save::Jumps(listings[part].rows)) {
                if (chunks == save::MaxChunks || held(target)) continue;
                auto chained = FunctionAt(session, target);
                if (!chained) return std::unexpected(chained.error());
                if (!*chained || !(*chained)->chunk || (*chained)->range.start != primary) continue;
                auto chunk = list((*chained)->chunk->start, MaxFunctionDisasm, false, false);
                if (!chunk || chunk->rows.empty()) continue;
                starts.push_back((*chained)->chunk->start);
                owners.push_back(owner);
                listings.push_back(std::move(*chunk));
                ++chunks;
            }
        }
        return {};
    };
    if (auto added = addChunks(address); !added) return JsonError(session, jsonCommand, "unwind", added.error());
    // --callees: each function the first calls directly (its chunks too), once, up to the caps.
    std::vector<Addr> skipped;
    size_t calleeBytes = 0;
    if (command.callees) {
        // Calls, then tail jumps out of the function and its chunks.
        std::vector<save::Range> parts;
        for (const auto& part : listings)
            if (!part.rows.empty())
                parts.push_back({part.rows.front().address, part.rows.back().address + part.rows.back().bytes.size()});
        std::vector<uint64_t> targets;
        for (size_t part = 0; part < listings.size(); ++part)
            for (const uint64_t target : save::Callees(listings[part].rows, *module, address))
                if (std::ranges::find(targets, target) == targets.end()) targets.push_back(target);
        for (size_t part = 0; part < listings.size(); ++part)
            for (const uint64_t target : save::TailJumps(listings[part].rows, *module, parts))
                if (std::ranges::find(targets, target) == targets.end()) targets.push_back(target);
        for (const uint64_t target : targets) {
            if (starts.size() > save::MaxCallees || calleeBytes >= save::MaxCalleeBytes) {
                skipped.push_back(target);
                continue;
            }
            // A call target is a start, even where an entry says otherwise
            // (one that begins a few bytes early); the entry bounds the end.
            auto entry = FunctionAt(session, target);
            if (!entry) return JsonError(session, jsonCommand, "unwind", entry.error());
            const uint64_t start = target;
            if (std::ranges::find(starts, start) != starts.end()) continue;
            auto callee = list(start, MaxFunctionDisasm, !*entry, false);
            if (!callee || callee->rows.empty()) {
                skipped.push_back(target);
                continue;
            }
            for (const auto& row : callee->rows) calleeBytes += row.bytes.size();
            starts.push_back(start);
            owners.push_back(0);
            listings.push_back(std::move(*callee));
            if (auto added = addChunks(start); !added) return JsonError(session, jsonCommand, "unwind", added.error());
        }
    }
    std::vector<save::Range> ranges;
    for (const auto& one : listings) {
        const auto found = save::DataRanges(one.rows, constant);
        ranges.insert(ranges.end(), found.begin(), found.end());
    }
    std::vector<ReadSpan> spans;
    for (const auto& range : save::Merge(std::move(ranges))) spans.push_back({range.start, range.end - range.start});
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> data;
    const auto read = AgentRead(session);
    for (size_t first = 0; first < spans.size(); first += MaxBatchReads) {
        const size_t n = std::min(MaxBatchReads, spans.size() - first);
        auto items = read(std::span(spans).subspan(first, n));
        if (!items) return JsonError(session, jsonCommand, "read", items.error());
        for (size_t i = 0; i < n && i < items->size(); ++i)
            if ((*items)[i]) data.push_back({spans[first + i].address, std::move(*(*items)[i])});
    }
    // --labels, else the console's labels file and the host's names file,
    // those that exist: the names this build's rows give, by address (the
    // highest-ranked row per address).
    std::map<uint64_t, std::string> labels;
    {
        std::vector<std::filesystem::path> files;
        if (!command.labels.empty()) files.push_back(command.labels);
        else files = {session.labels, session.hostLabels};
        std::vector<LabelRow> rows;
        for (const auto& path : files) {
            if (path.empty()) continue;
            std::ifstream in(path);
            if (!in && !command.labels.empty()) return JsonError(session, jsonCommand, "file", "cannot read " + command.labels);
            for (std::string text; in && std::getline(in, text);)
                if (auto row = ParseLabel(text, *module, *timestamp)) rows.push_back(std::move(*row));
        }
        for (auto& row : MergeLabels(std::move(rows))) labels[module->base + row.rva] = std::move(row.name);
    }
    std::vector<save::Saved> saved;
    for (size_t i = 0; i < listings.size(); ++i) saved.push_back({starts[i], listings[i].rows, owners[i]});
    std::ofstream file(command.file, std::ios::binary | std::ios::trunc);
    if (!file || !(file << save::Lines(*module, *timestamp, saved, data, labels)))
        return JsonError(session, jsonCommand, "file", "cannot write " + command.file);
    KeepSaveSummary(out, command.file);
    uint64_t rows = 0;
    for (const auto& one : listings) rows += one.rows.size();
    out.savedRows = rows;
    for (size_t i = 0; i < listings.size(); ++i) {
        const auto& last = listings[i].rows.back();
        const shape::SavedFunction one{starts[i], last.address + last.bytes.size(), listings[i].rows.size()};
        if (owners[i]) {
            if (!out.chunks) out.chunks.emplace();
            out.chunks->push_back(one);
        } else if (command.callees) {
            if (!out.functions) out.functions.emplace();
            out.functions->push_back(one);
        }
    }
    if (!skipped.empty()) out.skipped = std::move(skipped);
    Emit(session, jsonCommand, std::move(out));
}

void RunXrefs(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(op::Xrefs) || !client.Supports(op::Search) || !client.Supports(op::ReadBatch))
        return JsonError(session, jsonCommand, "unsupported", "xrefs needs an agent with xrefs, search and read-batch");
    const uint64_t target = command.request.address;
    const Module* found = command.target.empty() ? ModuleAt(target, LoadedModules(session)) :
        text::ModuleNamed(command.target, LoadedModules(session));
    if (!found && !command.target.empty() && RefreshModules(session))
        found = text::ModuleNamed(command.target, LoadedModules(session));
    if (!found) return JsonError(session, jsonCommand, "module", command.target.empty() ?
        "the address lies in no loaded module; name one with --module" : "no loaded module with that name");
    const Module module = *found; // The list may be re-read below.

    const auto started = Clock::now();
    XrefQuery relative;
    relative.target = target;
    relative.from = module.base;
    relative.to = module.base + module.size;
    auto fields = ScanAll(session, relative, xref::MaxRelative, [&client](const XrefQuery& q, auto timeout) {
        return client.Xrefs(q, timeout);
    });
    if (!fields) return CommandError(session, jsonCommand, fields.error());
    SearchQuery pointer;
    pointer.pattern.resize(8);
    memcpy(pointer.pattern.data(), &target, 8);
    pointer.alignment = 8;
    pointer.from = module.base;
    pointer.to = module.base + module.size;
    auto pointers = ScanAll(session, pointer, xref::MaxPointers, [&client](const SearchQuery& q, auto timeout) {
        return client.Search(q, timeout);
    });
    if (!pointers) return CommandError(session, jsonCommand, pointers.error());
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();

    // Kinds from the bytes in front of each field, read MaxBatchReads per request.
    const Client::Hold hold(client);
    std::vector<xref::Hit> hits;
    const auto read = AgentRead(session);
    const auto& relatives = fields->first;
    for (size_t first = 0; first < relatives.size(); first += MaxBatchReads) {
        std::vector<ReadSpan> spans;
        std::vector<size_t> pad;
        for (size_t i = first; i < std::min(relatives.size(), first + MaxBatchReads); ++i) {
            const uint64_t from = std::max(module.base, relatives[i] - xref::Before);
            spans.push_back({from, relatives[i] + 4 - from});
            pad.push_back(static_cast<size_t>(from - (relatives[i] - xref::Before)));
        }
        auto items = read(spans);
        if (!items) return JsonError(session, jsonCommand, "read", items.error());
        for (size_t i = 0; i < spans.size(); ++i) {
            const uint64_t field = relatives[first + i];
            if (i >= items->size() || !(*items)[i]) {
                hits.push_back({field, "unknown", std::nullopt});
                continue;
            }
            std::array<uint8_t, xref::Window> window{};
            std::copy((*items)[i]->begin(), (*items)[i]->end(), window.begin() + pad[i]);
            hits.push_back(xref::Classify(field, window, target));
        }
    }
    for (const uint64_t at : pointers->first) hits.push_back({at, "pointer", std::nullopt});
    std::ranges::sort(hits, {}, &xref::Hit::field);

    std::vector<std::optional<unwind::Function>> functions(hits.size());
    for (size_t i = 0; i < hits.size(); ++i) {
        if (hits[i].kind == "unknown" && !command.all) continue;
        auto function = FunctionAt(session, hits[i].instruction.value_or(hits[i].field));
        if (!function) return JsonError(session, jsonCommand, "unwind", function.error());
        functions[i] = *function;
    }
    // Code hits no entry lists: the piece holding the instruction, as touches gives it.
    std::vector<std::optional<touch::Piece>> pieces(hits.size());
    for (size_t i = 0; i < hits.size(); ++i) {
        if (functions[i] || hits[i].kind == "unknown" || hits[i].kind == "pointer") continue;
        auto piece = UnlistedPiece(session, module, hits[i].instruction.value_or(hits[i].field));
        if (!piece) return JsonError(session, jsonCommand, "unwind", piece.error());
        pieces[i] = *piece;
    }
    Emit(session, jsonCommand, MakeXrefs(target, module, hits, functions, command.all,
        fields->second || pointers->second, ms, LoadedModules(session), pieces));
}
void RunTouches(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(op::ReadBatch)) return JsonError(session, jsonCommand, "unsupported", "touches needs read-batch");
    const uint64_t address = command.request.address;
    const Module* found = ModuleAt(address, LoadedModules(session));
    if (!found) return JsonError(session, jsonCommand, "module", "the address lies in no loaded module");
    const Module module = *found;
    auto layout = LayoutOf(session, module);
    if (!layout) return JsonError(session, jsonCommand, "unwind", layout.error());
    auto& table = session.unwindTables[module.base];
    if (!table) table = std::make_unique<unwind::Table>(AgentRead(session), module, (*layout)->exception);
    const Client::Hold hold(client);
    const auto started = Clock::now();
    const uint64_t from = address - std::min(address - module.base, command.range), to = address + command.range;
    auto ranges = table->Between(from, to);
    if (!ranges) return JsonError(session, jsonCommand, "unwind", ranges.error());
    // The executable sections' parts of [from, to): code no entry lists is scanned too.
    std::vector<unwind::Range> executable;
    for (const auto& section : (*layout)->sections) {
        if (!section.Exec()) continue;
        const uint64_t start = std::max(from, module.base + section.rva);
        const uint64_t end = std::min(to, module.base + section.rva + section.size);
        if (start < end) executable.push_back({start, end});
    }
    std::ranges::sort(executable, {}, &unwind::Range::start);
    // Executable code outside every entry. An entry that began before `from`
    // and runs into it is skipped like a listed one.
    std::vector<unwind::Range> gaps;
    {
        std::vector<unwind::Range> covered;
        for (const auto& range : *ranges)
            if (touch::Bounds(range.start, range.end)) covered.push_back(range);
        if (auto first = FunctionAt(session, from); first && *first) {
            const auto& held = (*first)->chunk ? *(*first)->chunk : (*first)->range;
            covered.push_back({from, held.end});
        }
        std::ranges::sort(covered, {}, &unwind::Range::start);
        for (const auto& span : executable) {
            uint64_t at = span.start;
            for (const auto& entry : covered) {
                if (entry.end <= at || entry.start >= span.end) continue;
                if (entry.start > at) gaps.push_back({at, entry.start});
                at = std::max(at, entry.end);
            }
            if (at < span.end) gaps.push_back({at, span.end});
        }
    }
    // One copy of the code from the lowest scanned byte to the highest, MaxRead
    // bytes per request (a batch's total); unreadable parts stay int3 and decode to nothing.
    std::vector<uint8_t> code;
    uint64_t low = 0;
    if (!ranges->empty() || !gaps.empty()) {
        low = UINT64_MAX;
        uint64_t high = 0;
        for (const auto* list : {&*ranges, &gaps})
            for (const auto& range : *list) {
                low = std::min(low, range.start);
                high = std::max(high, range.end);
            }
        code.assign(static_cast<size_t>(high - low), 0xcc);
        const auto read = AgentRead(session);
        for (uint64_t at = low; at < high;) {
            std::vector<ReadSpan> spans;
            spans.push_back({at, std::min<uint64_t>(MaxRead - sizeof(Status), high - at)});
            at += spans.back().length;
            auto items = read(spans);
            if (!items) return JsonError(session, jsonCommand, "read", items.error());
            for (size_t i = 0; i < spans.size() && i < items->size(); ++i)
                if ((*items)[i]) std::copy((*items)[i]->begin(), (*items)[i]->end(), code.begin() + (spans[i].address - low));
        }
    }
    std::map<uint64_t, touch::Row> functions;
    for (const auto& range : *ranges) {
        if (!touch::Bounds(range.start, range.end)) continue; // A decoy: its byte is scanned with the unlisted code.
        auto uses = touch::InCode(std::span(code).subspan(static_cast<size_t>(range.start - low),
            static_cast<size_t>(range.end - range.start)), range.start, command.offsets);
        if (uses.empty()) continue;
        auto function = FunctionAt(session, range.start);
        if (!function) return JsonError(session, jsonCommand, "unwind", function.error());
        const unwind::Range whole = *function ? (*function)->range : unwind::Range{range.start, range.end};
        auto& row = functions[whole.start];
        row.start = whole.start;
        row.end = whole.end;
        row.uses.insert(row.uses.end(), uses.begin(), uses.end());
    }
    size_t pieces = 0;
    for (const auto& gap : gaps) {
        const auto bytes = std::span(code).subspan(static_cast<size_t>(gap.start - low), static_cast<size_t>(gap.end - gap.start));
        for (const auto& piece : touch::Pieces(bytes, gap.start)) {
            ++pieces;
            auto uses = touch::InCode(bytes.subspan(static_cast<size_t>(piece.start - gap.start),
                static_cast<size_t>(piece.end - piece.start)), piece.start, command.offsets);
            if (uses.empty()) continue;
            auto& row = functions[piece.start];
            row.start = piece.start;
            row.end = piece.end;
            row.listed = false;
            row.uses = std::move(uses);
        }
    }
    std::vector<touch::Row> rows;
    for (auto& [start, row] : functions) rows.push_back(std::move(row));
    touch::Rank(rows, command.readsOnly);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    Emit(session, jsonCommand, MakeTouches(address, command.offsets, command.range, from, to, ranges->size(), pieces,
        code.size(), rows, ms, LoadedModules(session)));
}
}
