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
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/code/code_json.h"
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/code/disasm.h"
#include "reagentlive/frontends/cli/code/function_save.h"
#include "reagentlive/frontends/cli/code/jump_table.h"
#include "reagentlive/frontends/cli/code/pe_names.h"
#include "reagentlive/frontends/cli/code/unwind.h"
#include "reagentlive/frontends/cli/code/xrefs.h"
#include "reagentlive/frontends/cli/results/labels.h"
#include "reagentlive/core/client.h"

#include "reagentlive/frontends/cli/console/internal.h"
#include "reagentlive/frontends/cli/console/pack.h"

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
std::map<uint64_t, std::string> LabelNames(const Module& module, uint32_t timestamp,
                                           std::span<const std::filesystem::path> files) {
    std::vector<LabelRow> rows;
    for (const auto& path : files) {
        if (path.empty()) continue;
        std::ifstream in(path);
        for (std::string text; in && std::getline(in, text);)
            if (auto row = ParseLabel(text, module, timestamp)) rows.push_back(std::move(*row));
    }
    std::map<uint64_t, std::string> labels;
    for (auto& row : MergeLabels(std::move(rows))) labels[module.base + row.rva] = std::move(row.name);
    return labels;
}

std::map<uint64_t, std::string> LabelNames(Session& session, const Module& module, uint32_t timestamp) {
    const std::filesystem::path files[] = {session.labels, session.hostLabels};
    return LabelNames(module, timestamp, files);
}

void RunDisasm(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(op::ReadBatch)) return JsonError(session, jsonCommand, ErrorKind::Unsupported, "disasm needs read-batch");
    const Client::Hold hold(client);
    uint64_t address = command.request.address;
    auto function = FunctionAt(session, address);
    if (!function) return JsonError(session, jsonCommand, ErrorKind::Unwind, function.error());
    // `function` with no entry holding the address (a leaf function, or an
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
        if (!held) return JsonError(session, jsonCommand, ErrorKind::Unwind, held.error());
        if (*held && !follows) piece = *held;
    }
    if (piece && !command.file.empty() && !command.resume)
        return JsonError(session, jsonCommand, ErrorKind::Start, "address is " + save::Hex(address - piece->start) +
            " bytes into unlisted code starting at " + save::Hex(piece->start) + ": save from there (junk bytes may lead), "
            "or pass it as view.cursor");
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
        options.bytes = command.function ? dis::MaxFunctionBytes :
            std::clamp<size_t>(count * DisasmBytes, dis::MaxBytes, dis::MaxFunctionBytes);
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
    if (!listing) return JsonError(session, jsonCommand, ErrorKind::Read, listing.error());
    // An entry whose start does not decode (a packed image's, a few bytes
    // early): list from the address given instead, still bounded by the entry.
    if (listing->rows.empty() && address != command.request.address) {
        address = command.request.address;
        listing = list(address, command.limit, false, command.follow);
        if (!listing) return JsonError(session, jsonCommand, ErrorKind::Read, listing.error());
    }
    // An entry shorter than one instruction (a one-byte decoy over real
    // code): no entry holds the address, so walk from it as for unlisted code.
    if (command.function && !walk && dis::ShorterThanOne(*listing)) {
        listing = list(address, command.limit, true, command.follow);
        if (!listing) return JsonError(session, jsonCommand, ErrorKind::Read, listing.error());
    }
    auto out = MakeDisasm(address, *listing, *function, modules, session.meta, command.function);
    if (piece) out.piece = shape::AddressRange{piece->start, piece->end};
    if (command.file.empty()) return Emit(session, jsonCommand, std::move(out));
    if (listing->rows.empty()) {
        std::string bound;
        if (*function) {
            const auto& entry = (*function)->chunk ? *(*function)->chunk : (*function)->range;
            bound = " (entry " + save::Hex(entry.start) + ".." + save::Hex(entry.end) + ")";
        }
        return JsonError(session, jsonCommand, ErrorKind::Start, "nothing to save at " + save::Hex(address) + ": " +
            listing->stop + bound + "; save from an address disasm lists");
    }

    const Module* module = ModuleAt(address, modules);
    if (!module) return JsonError(session, jsonCommand, ErrorKind::Module, "save needs code inside a loaded module");
    const auto timestamp = ImageTimestamp(session, *module);
    if (!timestamp) return JsonError(session, jsonCommand, ErrorKind::File, "the module's PE header is unreadable");
    // Switch tables in a part: each site, its entries' bytes and the targets
    // inside the module's code. The part's rows stop where a table after the
    // jump starts (its bytes are data, not code), so the table is saved as data.
    struct Table { jumptab::Site site; std::vector<uint8_t> index, entries; std::vector<uint64_t> targets; };
    const auto readSpan = [&](uint64_t at, uint64_t length) -> std::optional<std::vector<uint8_t>> {
        const ReadSpan span{at, length};
        auto items = AgentRead(session)(std::span(&span, 1));
        if (!items || items->empty() || !items->front()) return std::nullopt;
        return std::move(*items->front());
    };
    const auto findTables = [&](dis::Listing& part) {
        std::vector<Table> found;
        for (const auto& site : jumptab::Find(part.rows)) {
            Table table{site};
            if (site.indexTable) {
                auto index = readSpan(*site.indexTable, site.indexCount);
                if (!index) continue;
                table.index = std::move(*index);
                table.site.count = jumptab::CountFromIndex(table.index);
            }
            if (!table.site.count || table.site.count > jumptab::MaxCases) continue;
            auto entries = readSpan(site.table, uint64_t{table.site.count} * 4);
            if (!entries) continue;
            table.entries = std::move(*entries);
            for (const uint64_t target : jumptab::Targets(table.site, table.entries))
                if (target - module->base < module->size && InCode(session, target)) table.targets.push_back(target);
            if (!table.targets.empty()) found.push_back(std::move(table));
        }
        uint64_t cut = UINT64_MAX;
        for (const auto& table : found)
            for (const uint64_t at : {table.site.table, table.site.indexTable.value_or(0)})
                if (at > table.site.jump) cut = std::min(cut, at);
        std::erase_if(part.rows, [cut](const dis::Instruction& row) { return row.address + row.bytes.size() > cut; });
        return found;
    };
    // The saved parts: each function, then its chained chunks (owners[i] is
    // 0 for a function, else the function's start), and each part's tables.
    std::vector<uint64_t> starts{address}, owners{0};
    std::vector<dis::Listing> listings;
    std::vector<std::vector<Table>> tables;
    tables.push_back(findTables(*listing));
    listings.push_back(std::move(*listing));
    const auto held = [&](uint64_t at) {
        return std::ranges::any_of(listings, [at](const dis::Listing& part) {
            return !part.rows.empty() && part.rows.front().address <= at &&
                at < part.rows.back().address + part.rows.back().bytes.size();
        });
    };
    // A switch table's bytes: never a jump target to list.
    const auto inTable = [&](uint64_t at) {
        return std::ranges::any_of(tables, [at](const std::vector<Table>& part) {
            return std::ranges::any_of(part, [at](const Table& table) {
                return (at >= table.site.table && at - table.site.table < table.entries.size()) ||
                    (table.site.indexTable && at >= *table.site.indexTable && at - *table.site.indexTable < table.index.size());
            });
        });
    };
    // Chunks of the function at `owner`: the jump and switch targets of its
    // listed parts that they do not hold, each listed to its own end: in an
    // entry chained to its entry (from that entry's start), in its own entry
    // (from the target: code past a table or a cut), or, for a switch
    // target, in code no entry lists (walked from the target). Its targets
    // still unsaved are `left` (jumpouts_left).
    size_t chunks = 0;
    bool capped = false;
    std::vector<uint64_t> left;
    const auto addChunks = [&](uint64_t owner) -> std::expected<void, std::string> {
        auto entry = FunctionAt(session, owner);
        if (!entry) return std::unexpected(entry.error());
        const std::optional<uint64_t> primary = *entry ? std::optional((*entry)->range.start) : std::nullopt;
        std::vector<uint64_t> missed;
        for (size_t part = 0; part < listings.size(); ++part) {
            if (starts[part] != owner && owners[part] != owner) continue;
            std::vector<std::pair<uint64_t, bool>> targets; // A target, and whether a table gave it.
            for (const uint64_t target : save::Jumps(listings[part].rows)) targets.push_back({target, false});
            for (const auto& table : tables[part])
                for (const uint64_t target : table.targets) targets.push_back({target, true});
            for (const auto& [target, fromTable] : targets) {
                if (held(target) || inTable(target)) continue;
                auto chained = FunctionAt(session, target);
                if (!chained) return std::unexpected(chained.error());
                const bool ours = primary && *chained && (*chained)->range.start == *primary;
                uint64_t from = 0;
                bool walking = false;
                if (ours && (*chained)->chunk) from = (*chained)->chunk->start;
                else if (ours) from = target;
                else if (!*chained && fromTable) { from = target; walking = true; }
                else continue;
                if (chunks == save::MaxChunks) {
                    capped = true;
                    missed.push_back(target);
                    continue;
                }
                auto chunk = list(from, MaxFunctionDisasm, walking, false);
                if (!chunk || chunk->rows.empty()) {
                    missed.push_back(target);
                    continue;
                }
                tables.push_back(findTables(*chunk));
                starts.push_back(from);
                owners.push_back(owner);
                listings.push_back(std::move(*chunk));
                ++chunks;
                if (!held(target)) missed.push_back(target);
            }
        }
        for (const uint64_t target : missed)
            if (!held(target) && std::ranges::find(left, target) == left.end()) left.push_back(target);
        return {};
    };
    if (auto added = addChunks(address); !added) return JsonError(session, jsonCommand, ErrorKind::Unwind, added.error());
    // callees: breadth-first, the functions each level calls directly or
    // reaches by a tail jump (their chunks too), each once, down to the depth
    // asked; the caps (functions, bytes) hold for the whole save.
    std::vector<shape::SkippedCall> skipped;
    std::map<uint64_t, uint32_t> depths{{address, 0}};
    const auto skip = [&](uint64_t target, const char* why) {
        if (std::ranges::none_of(skipped, [target](const shape::SkippedCall& one) { return one.address.value == target; }))
            skipped.push_back({target, why});
    };
    const auto saveBytes = [&] {
        size_t bytes = 0;
        for (const auto& part : listings)
            for (const auto& row : part.rows) bytes += row.bytes.size();
        return bytes;
    };
    std::vector<uint64_t> level{address};
    for (uint32_t depth = 1; command.calleeDepth && !level.empty(); ++depth) {
        // Calls, then tail jumps out of every part this level holds.
        std::vector<save::Range> parts;
        for (const auto& part : listings)
            if (!part.rows.empty())
                parts.push_back({part.rows.front().address, part.rows.back().address + part.rows.back().bytes.size()});
        std::vector<uint64_t> targets;
        const auto add = [&](uint64_t target) {
            if (!depths.contains(target) && std::ranges::find(targets, target) == targets.end()) targets.push_back(target);
        };
        for (const uint64_t fn : level)
            for (size_t part = 0; part < listings.size(); ++part)
                if (starts[part] == fn || owners[part] == fn)
                    for (const uint64_t target : save::Callees(listings[part].rows, *module, fn)) add(target);
        for (const uint64_t fn : level)
            for (size_t part = 0; part < listings.size(); ++part)
                if (starts[part] == fn || owners[part] == fn)
                    for (const uint64_t target : save::TailJumps(listings[part].rows, *module, parts)) add(target);
        std::vector<uint64_t> next;
        for (const uint64_t target : targets) {
            if (depth > command.calleeDepth) { skip(target, "depth"); continue; }
            if (depths.size() > save::MaxCallees) { skip(target, "count"); continue; }
            if (saveBytes() >= save::MaxSaveBytes) { skip(target, "bytes"); continue; }
            // A call target is a start, even where an entry says otherwise
            // (one that begins a few bytes early); the entry bounds the end.
            auto entry = FunctionAt(session, target);
            if (!entry) return JsonError(session, jsonCommand, ErrorKind::Unwind, entry.error());
            auto callee = list(target, MaxFunctionDisasm, !*entry, false);
            // A one-byte decoy entry over the start bounds the listing to nothing:
            // walk from the start instead, as the function asked for does.
            if (callee && *entry && dis::ShorterThanOne(*callee)) callee = list(target, MaxFunctionDisasm, true, false);
            if (!callee || callee->rows.empty()) { skip(target, "unreadable"); continue; }
            tables.push_back(findTables(*callee));
            starts.push_back(target);
            owners.push_back(0);
            listings.push_back(std::move(*callee));
            depths[target] = depth;
            next.push_back(target);
            if (auto added = addChunks(target); !added) return JsonError(session, jsonCommand, ErrorKind::Unwind, added.error());
        }
        if (depth > command.calleeDepth) break;
        level = std::move(next);
    }
    // A table that one part found may lie inside another (a chunk that runs
    // through it): every part stops where a table starts inside it.
    for (auto& part : listings) {
        if (part.rows.empty()) continue;
        const uint64_t first = part.rows.front().address;
        uint64_t cut = UINT64_MAX;
        for (const auto& found : tables)
            for (const auto& table : found)
                for (const uint64_t at : {table.site.table, table.site.indexTable.value_or(0)})
                    if (at > first) cut = std::min(cut, at);
        std::erase_if(part.rows, [cut](const dis::Instruction& row) { return row.address + row.bytes.size() > cut; });
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
        if (!items) return JsonError(session, jsonCommand, ErrorKind::Read, items.error());
        for (size_t i = 0; i < n && i < items->size(); ++i)
            if ((*items)[i]) data.push_back({spans[first + i].address, std::move(*(*items)[i])});
    }
    // Switch tables are data whole: other data keeps only what they leave.
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> tableRuns;
    std::vector<shape::JumpTable> tableRows;
    for (const auto& part : tables)
        for (const auto& table : part) {
            tableRuns.push_back({table.site.table, table.entries});
            if (table.site.indexTable) tableRuns.push_back({*table.site.indexTable, table.index});
            tableRows.push_back({table.site.jump, table.site.table, static_cast<uint32_t>(table.targets.size())});
        }
    if (!tableRuns.empty()) {
        tableRuns = save::JoinedRuns(std::move(tableRuns));
        std::vector<std::pair<uint64_t, std::vector<uint8_t>>> kept;
        for (const auto& [at, bytes] : data)
            for (auto& rest : save::Uncovered(at, bytes, tableRuns)) kept.push_back(std::move(rest));
        data = std::move(kept);
        for (auto& run : tableRuns) data.push_back(std::move(run));
    }
    // `save.labels`, else the console's labels file and the host's names file,
    // those that exist: the names this build's rows give, by address (the
    // highest-ranked row per address).
    if (!command.labels.empty() && !std::ifstream(command.labels))
        return JsonError(session, jsonCommand, ErrorKind::File, "cannot read " + command.labels);
    const std::filesystem::path given[] = {command.labels};
    std::map<uint64_t, std::string> labels = command.labels.empty() ?
        LabelNames(session, *module, *timestamp) : LabelNames(*module, *timestamp, given);
    std::vector<save::Saved> saved;
    for (size_t i = 0; i < listings.size(); ++i)
        if (!listings[i].rows.empty()) saved.push_back({starts[i], listings[i].rows, owners[i]});
    // The layouts of the classes the saved functions' labels name, as layout rows.
    std::vector<std::string> methods;
    for (size_t i = 0; i < listings.size(); ++i)
        if (!owners[i] && !listings[i].rows.empty())
            if (const auto label = labels.find(starts[i]); label != labels.end()) methods.push_back(label->second);
    auto layouts = PackSaveLayouts(session, methods);
    if (!layouts) return CommandError(session, jsonCommand, layouts.error());
    const auto utf8 = [](const std::filesystem::path& path) {
        const auto text = path.u8string();
        return std::string(text.begin(), text.end());
    };
    const save::Console console{utf8(session.executable), utf8(session.labels),
        utf8(!session.hostLabels.empty() ? session.hostLabels : session.labels)};
    std::string text = save::Lines(*module, *timestamp, saved, data, labels, console);
    for (size_t at = 0; at < layouts->lines.size();) {
        const size_t end = layouts->lines.find('\n', at);
        Json row = Json::parse(layouts->lines.substr(at, end - at));
        row["row"] = "layout";
        text += row.dump() + "\n";
        at = end == std::string::npos ? layouts->lines.size() : end + 1;
    }
    // A missing parent folder is made, as assets-export makes its own.
    if (const auto parent = std::filesystem::path(command.file).parent_path(); !parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
    }
    std::ofstream file(command.file, std::ios::binary | std::ios::trunc);
    if (!file || !(file << text))
        return JsonError(session, jsonCommand, ErrorKind::File, "cannot write " + command.file);
    KeepSaveSummary(out, command.file);
    uint64_t rows = 0;
    for (const auto& one : listings) rows += one.rows.size();
    out.savedRows = rows;
    for (size_t i = 0; i < listings.size(); ++i) {
        if (listings[i].rows.empty()) continue;
        const auto& last = listings[i].rows.back();
        const shape::SavedFunction one{starts[i], last.address + last.bytes.size(), listings[i].rows.size()};
        if (owners[i]) {
            if (!out.chunks) out.chunks.emplace();
            out.chunks->push_back(one);
        } else if (command.calleeDepth) {
            if (!out.functions) out.functions.emplace();
            out.functions->push_back(one);
            out.functions->back().depth = depths[starts[i]];
        }
    }
    if (!skipped.empty()) out.skipped = std::move(skipped);
    if (!tableRows.empty()) out.tables = std::move(tableRows);
    if (!left.empty()) {
        std::ranges::sort(left);
        out.jumpoutsLeft.emplace(left.begin(), left.end());
    }
    if (capped) out.capped = true;
    if (!layouts->owners.empty()) out.layouts = shape::SavedLayouts{std::move(layouts->owners), layouts->types};
    Emit(session, jsonCommand, std::move(out));
}

void RunXrefs(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(op::Xrefs) || !client.Supports(op::Search) || !client.Supports(op::ReadBatch))
        return JsonError(session, jsonCommand, ErrorKind::Unsupported, "xrefs needs an agent with xrefs, search and read-batch");
    const uint64_t target = command.request.address;
    const Module* found = command.target.empty() ? ModuleAt(target, LoadedModules(session)) :
        text::ModuleNamed(command.target, LoadedModules(session));
    if (!found && !command.target.empty() && RefreshModules(session))
        found = text::ModuleNamed(command.target, LoadedModules(session));
    if (!found) return JsonError(session, jsonCommand, ErrorKind::Module, command.target.empty() ?
        "the address lies in no loaded module; name one with module" : "no loaded module with that name");
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
        if (!items) return JsonError(session, jsonCommand, ErrorKind::Read, items.error());
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
        if (!function) return JsonError(session, jsonCommand, ErrorKind::Unwind, function.error());
        functions[i] = *function;
    }
    // Code hits no entry lists: the piece holding the instruction, as touches gives it.
    std::vector<std::optional<touch::Piece>> pieces(hits.size());
    for (size_t i = 0; i < hits.size(); ++i) {
        if (functions[i] || hits[i].kind == "unknown" || hits[i].kind == "pointer") continue;
        auto piece = UnlistedPiece(session, module, hits[i].instruction.value_or(hits[i].field));
        if (!piece) return JsonError(session, jsonCommand, ErrorKind::Unwind, piece.error());
        pieces[i] = *piece;
    }
    Emit(session, jsonCommand, MakeXrefs(target, module, hits, functions, command.all,
        fields->second || pointers->second, ms, LoadedModules(session), pieces));
}
void RunTouches(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(op::ReadBatch)) return JsonError(session, jsonCommand, ErrorKind::Unsupported, "touches needs read-batch");
    const uint64_t address = command.request.address;
    const Module* found = ModuleAt(address, LoadedModules(session));
    if (!found) return JsonError(session, jsonCommand, ErrorKind::Module, "the address lies in no loaded module");
    const Module module = *found;
    auto layout = LayoutOf(session, module);
    if (!layout) return JsonError(session, jsonCommand, ErrorKind::Unwind, layout.error());
    auto& table = session.unwindTables[module.base];
    if (!table) table = std::make_unique<unwind::Table>(AgentRead(session), module, (*layout)->exception);
    const Client::Hold hold(client);
    const auto started = Clock::now();
    const uint64_t from = address - std::min(address - module.base, command.range), to = address + command.range;
    auto ranges = table->Between(from, to);
    if (!ranges) return JsonError(session, jsonCommand, ErrorKind::Unwind, ranges.error());
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
    // One copy of the code from the lowest scanned byte to the highest (the image
    // window, else MaxRead reads); unreadable parts stay int3 and decode to nothing.
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
        auto read = ImageBlocks(session, low, high, 0xcc, high - low, [&](uint64_t at, std::span<const uint8_t> block) {
            std::ranges::copy(block, code.begin() + static_cast<ptrdiff_t>(at - low));
            return true;
        });
        if (!read) return JsonError(session, jsonCommand, ErrorKind::Read, read.error());
    }
    const touch::Bases bases{command.bases};
    std::map<uint64_t, touch::Row> functions;
    for (const auto& range : *ranges) {
        if (!touch::Bounds(range.start, range.end)) continue; // A decoy: its byte is scanned with the unlisted code.
        auto uses = touch::InCode(std::span(code).subspan(static_cast<size_t>(range.start - low),
            static_cast<size_t>(range.end - range.start)), range.start, command.offsets, bases);
        if (uses.empty()) continue;
        auto function = FunctionAt(session, range.start);
        if (!function) return JsonError(session, jsonCommand, ErrorKind::Unwind, function.error());
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
                static_cast<size_t>(piece.end - piece.start)), piece.start, command.offsets, bases);
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
    touch::Rank(rows, command.access);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    Emit(session, jsonCommand, MakeTouches(address, command.offsets, command.range, from, to, ranges->size(), pieces,
        code.size(), rows, ms, LoadedModules(session)));
}

namespace {
// Reads inside one module through the image window, a whole window per
// request and kept: the function lookups for a sweep's thousands of hits
// read the exception directory and unwind data at memory speed, not a host
// tick per read. A span outside the module, or without the window: the
// agent's reads.
sig::ReadFn ImageRead(Session& session, const Module& module) {
    if (!session.client.Supports(op::ImageCopy)) return AgentRead(session);
    auto blocks = std::make_shared<std::map<uint64_t, ImageBlock>>();
    return [&session, module, blocks, agent = AgentRead(session)](std::span<const ReadSpan> spans)
               -> std::expected<std::vector<std::optional<std::vector<uint8_t>>>, std::string> {
        std::vector<std::optional<std::vector<uint8_t>>> items;
        const uint64_t moduleEnd = module.base + module.size;
        for (const auto& span : spans) {
            if (span.address < module.base || span.address >= moduleEnd || span.length > moduleEnd - span.address) {
                auto one = agent(std::span(&span, 1));
                if (!one) return std::unexpected(one.error());
                items.push_back(one->empty() ? std::nullopt : std::move(one->front()));
                continue;
            }
            std::vector<uint8_t> out;
            bool readable = true;
            for (uint64_t at = span.address; readable && at < span.address + span.length;) {
                const uint64_t start = module.base + (at - module.base) / ImageWindowBytes * ImageWindowBytes;
                auto block = blocks->find(start);
                if (block == blocks->end()) {
                    const uint64_t end = std::min(start + ImageWindowBytes, moduleEnd);
                    auto copied = session.client.CopyImage(start, static_cast<uint32_t>(end - start), std::chrono::seconds(30));
                    if (!copied) return std::unexpected("image copy: " + copied.error().message);
                    block = blocks->emplace(start, std::move(*copied)).first;
                }
                const auto& bytes = block->second.bytes;
                const uint64_t to = std::min(span.address + span.length, start + bytes.size());
                for (uint64_t page = (at - start) / 0x1000; page <= (to - 1 - start) / 0x1000; ++page)
                    readable = readable && page < block->second.copied.size() && block->second.copied[page];
                out.insert(out.end(), bytes.begin() + static_cast<ptrdiff_t>(at - start), bytes.begin() + static_cast<ptrdiff_t>(to - start));
                at = to;
            }
            items.push_back(readable ? std::optional(std::move(out)) : std::nullopt);
        }
        return items;
    };
}
}

// disp-scan: a linear sweep of the main module's executable sections from
// `view.cursor` (default: the first), one block at a time, until the time budget
// or the hit cap ends the page; next_cursor resumes it. Blocks come through
// the agent's image window when it has one (its worker copies them while the
// host tick only polls), else in MaxRead reads.
void RunDispScan(Session& session, const Command& command, std::string_view jsonCommand) {
    constexpr auto Budget = std::chrono::seconds(8);
    constexpr size_t MaxHits = 128, MaxGroupHits = size_t{1} << 16;
    Client& client = session.client;
    if (!client.Supports(op::ReadBatch)) return JsonError(session, jsonCommand, ErrorKind::Unsupported, "disp-scan needs read-batch");
    const auto modules = LoadedModules(session);
    if (modules.empty()) return JsonError(session, jsonCommand, ErrorKind::Module, "modules unavailable");
    const Module module = modules.front();
    auto layout = LayoutOf(session, module);
    if (!layout) return JsonError(session, jsonCommand, ErrorKind::Unwind, layout.error());
    std::vector<unwind::Range> code;
    uint64_t codeBytes = 0;
    for (const auto& section : (*layout)->sections)
        if (section.Exec() && section.size) {
            code.push_back({module.base + section.rva, module.base + section.rva + section.size});
            codeBytes += section.size;
        }
    std::ranges::sort(code, {}, &unwind::Range::start);
    if (code.empty()) return JsonError(session, jsonCommand, ErrorKind::Module, "the module has no executable section");
    uint64_t at = command.request.address ? command.request.address : code.front().start;
    if (std::ranges::none_of(code, [&](const unwind::Range& r) { return r.start <= at && at < r.end; }))
        return JsonError(session, jsonCommand, ErrorKind::Validation, "view.cursor: not in the main module's code (pass a next_cursor)");
    // Function starts for every hit (same_base) or row: lookups through the image window.
    unwind::Table table(ImageRead(session, module), module, (*layout)->exception);
    const touch::Form form{command.offsets, command.access, command.width, command.imm, touch::Bases{command.bases, true}};
    const size_t limit = command.limit ? command.limit : MaxHits;
    const size_t most = command.sameBase ? MaxGroupHits : limit;
    const uint64_t chunk = client.Supports(op::ImageCopy) ? ImageWindowBytes : 0x10000;
    const Client::Hold hold(client);
    const auto started = Clock::now();
    Clock::duration reading{};
    std::vector<touch::Hit> hits;
    std::vector<std::optional<uint64_t>> starts; // same_base: each hit's function start, inside the budget.
    const auto startOf = [&](uint64_t address) -> std::optional<uint64_t> {
        auto function = table.Lookup(address);
        if (!function || !*function) return std::nullopt;
        return (*function)->range.start;
    };
    const uint64_t from = at;
    bool done = false;
    ProgressLine progress("bytes", codeBytes);
    while (true) {
        const auto section = std::ranges::find_if(code, [&](const unwind::Range& r) { return at < r.end; });
        if (section == code.end()) { done = true; break; }
        at = std::max(at, section->start);
        const uint64_t end = std::min<uint64_t>(section->end, at + chunk);
        std::vector<uint8_t> bytes; // Unreadable stays int3: nothing decodes.
        const auto readStart = Clock::now();
        auto read = ImageBlocks(session, at, end, 0xcc, chunk, [&](uint64_t, std::span<const uint8_t> block) {
            bytes.assign(block.begin(), block.end());
            return true;
        });
        reading += Clock::now() - readStart;
        if (!read) return JsonError(session, jsonCommand, ErrorKind::Read, read.error());
        bool late = false;
        {
            // The sweep and the function lookups are console-side work: the slot is free meanwhile.
            const Client::Turn turn(client);
            const size_t stopped = touch::Sweep(bytes, at, form, end == section->end, hits, most);
            at += std::max<size_t>(stopped, 1);
            // Out of time while resolving: the page ends at the first hit left (an instruction start).
            if (command.sameBase)
                while (starts.size() < hits.size() && !(late = Clock::now() - started >= Budget))
                    starts.push_back(startOf(hits[starts.size()].address));
        }
        {
            uint64_t behind = 0;
            for (const auto& r : code) behind += r.end <= at ? r.end - r.start : r.start < at ? at - r.start : 0;
            progress.Done(behind);
        }
        if (late) {
            at = hits[starts.size()].address;
            hits.resize(starts.size());
            break;
        }
        if (hits.size() >= most || Clock::now() - started >= Budget) break;
    }
    // same_base groups need whole functions: a page cut inside one resumes at its start.
    if (command.sameBase && !done) at = touch::KeepWhole(hits, starts, from, at, startOf(at));
    std::vector<touch::Group> groups;
    if (command.sameBase) {
        groups = touch::SameBase(hits, starts, form.disps);
        // Past view.limit rows: whole functions up to it (one at least), the page resuming at the next.
        size_t rows = 0, keep = 0;
        while (keep < groups.size() && (!keep || rows + groups[keep].hits.size() <= limit)) rows += groups[keep++].hits.size();
        if (keep < groups.size() && groups[keep].function > from) {
            at = groups[keep].function;
            groups.resize(keep);
            done = false;
        }
    }
    shape::DispScan out;
    for (const uint32_t disp : form.disps) out.disps.push_back(HexNum(disp));
    out.access = form.access;
    out.module = module.name;
    out.from = from;
    out.to = done ? code.back().end : at;
    uint64_t behind = 0;
    for (const auto& r : code) behind += r.end <= at ? r.end - r.start : r.start < at ? at - r.start : 0;
    out.scanned = done ? 1.0 : static_cast<double>(static_cast<int64_t>(static_cast<double>(behind) * 1000 / static_cast<double>(codeBytes))) / 1000;
    if (!done) out.nextCursor = Addr(at);
    const auto row = [&](const touch::Hit& hit, std::optional<uint64_t> function) {
        shape::DispHit made{hit.address, HexNum(hit.address - module.base), HexNum(hit.disp), hit.base, hit.access, hit.width,
            hit.text, {}};
        if (function) made.function = HexNum(*function - module.base);
        return made;
    };
    if (command.sameBase) {
        out.functions.emplace();
        for (const auto& group : groups) {
            out.functions->push_back({HexNum(group.function - module.base), group.base, group.hits.size()});
            for (const auto& hit : group.hits) out.rows.push_back(row(hit, group.function));
        }
    } else {
        for (const auto& hit : hits) out.rows.push_back(row(hit, startOf(hit.address)));
    }
    out.total = out.rows.size();
    out.scanMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    out.readMs = std::chrono::duration_cast<std::chrono::milliseconds>(reading).count();
    Emit(session, jsonCommand, std::move(out));
}
}
