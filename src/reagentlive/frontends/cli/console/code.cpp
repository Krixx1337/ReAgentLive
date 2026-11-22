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

// One page of a scan on the agent's worker thread when the host allows it,
// else in host-tick slices; a refusal for background is remembered.
template<class Query, class Scan>
Result<SearchResult> ScanPage(Session& session, Query query, Scan scan) {
    const auto timeout = std::max(session.client.Timeout(), std::chrono::milliseconds(180000));
    if (!session.sliceSearch) {
        query.background = true;
        auto found = scan(query, timeout);
        if (found || found.error().kind != Error::Kind::Agent || found.error().status != Status::Refused ||
            found.error().reason.find("background") == std::string::npos) return found;
        session.sliceSearch = true;
    }
    query.background = false;
    return scan(query, timeout);
}

// Every hit in [from, to) up to `cap`; true in the pair when the cap cut it.
template<class Query, class Scan>
Result<std::pair<std::vector<uint64_t>, bool>> ScanAll(Session& session, Query query, size_t cap, Scan scan) {
    std::vector<uint64_t> hits;
    while (true) {
        query.maxHits = static_cast<uint32_t>(std::min<size_t>(MaxSearchHits, cap + 1 - hits.size()));
        auto page = ScanPage(session, query, scan);
        if (!page) return std::unexpected(page.error());
        hits.insert(hits.end(), page->hits.begin(), page->hits.end());
        if (hits.size() > cap) {
            hits.resize(cap);
            return std::pair(std::move(hits), true);
        }
        if (!page->resume || page->resume <= query.from) return std::pair(std::move(hits), false);
        query.from = page->resume;
    }
}
}

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
    dis::Options options;
    options.count = command.limit;
    options.follow = command.follow;
    options.walk = walk;
    if (command.function) options.bytes = dis::MaxFunctionBytes;
    if (command.function && !walk) options.bound = [&session](uint64_t at) { return EntryEnd(session, at); };
    auto listing = dis::List(AgentRead(session), address, options);
    if (!listing) return JsonError(session, jsonCommand, "read", listing.error());
    const auto modules = LoadedModules(session);
    if (auto strings = dis::AddStrings(AgentRead(session), listing->rows, modules); !strings)
        return JsonError(session, jsonCommand, "read", strings.error());
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
        return JsonError(session, jsonCommand, "read", values.error());
    if (!command.file.empty()) {
        const Module* module = ModuleAt(address, modules);
        if (!module) return JsonError(session, jsonCommand, "module", "--save needs code inside a loaded module");
        const auto timestamp = ImageTimestamp(session, *module);
        if (!timestamp) return JsonError(session, jsonCommand, "file", "the module's PE header is unreadable");
        std::vector<ReadSpan> spans;
        for (const auto& range : save::DataRanges(listing->rows, constant)) spans.push_back({range.start, range.end - range.start});
        std::vector<std::pair<uint64_t, std::vector<uint8_t>>> data;
        const auto read = AgentRead(session);
        for (size_t first = 0; first < spans.size(); first += MaxBatchReads) {
            const size_t n = std::min(MaxBatchReads, spans.size() - first);
            auto items = read(std::span(spans).subspan(first, n));
            if (!items) return JsonError(session, jsonCommand, "read", items.error());
            for (size_t i = 0; i < n && i < items->size(); ++i)
                if ((*items)[i]) data.push_back({spans[first + i].address, std::move(*(*items)[i])});
        }
        std::ofstream file(command.file, std::ios::binary | std::ios::trunc);
        if (!file || !(file << save::Lines(*module, *timestamp, address, listing->rows, data)))
            return JsonError(session, jsonCommand, "file", "cannot write " + command.file);
    }
    auto out = MakeDisasm(address, *listing, *function, modules, session.meta, command.function);
    if (!command.file.empty()) out.saved = command.file;
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
    Emit(session, jsonCommand, MakeXrefs(target, module, hits, functions, command.all,
        fields->second || pointers->second, ms, LoadedModules(session)));
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
        std::vector<unwind::Range> covered = *ranges;
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
