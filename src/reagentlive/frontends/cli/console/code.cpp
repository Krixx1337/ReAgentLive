// reagentlive CLI console: the fast lane's code questions about one address.
// disasm (decoded in the console from one read) and xrefs (the agent's
// relative-reference scan and pointer search, kinds named here), both with
// function bounds from the module's exception directory.
#include <algorithm>
#include <array>
#include <map>
#include <cstring>
#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/code/code_json.h"
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/code/disasm.h"
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
    if (command.function) {
        if (!*function) return JsonError(session, jsonCommand, "validation",
            "no exception-table entry holds that address (a leaf function or data); list from an address instead");
        address = (*function)->range.start;
    }
    dis::Options options;
    options.count = command.limit;
    options.follow = command.follow;
    if (command.function) options.bound = [&session](uint64_t at) { return EntryEnd(session, at); };
    auto listing = dis::List(AgentRead(session), address, options);
    if (!listing) return JsonError(session, jsonCommand, "read", listing.error());
    const auto modules = LoadedModules(session);
    if (auto strings = dis::AddStrings(AgentRead(session), listing->rows, modules); !strings)
        return JsonError(session, jsonCommand, "read", strings.error());
    Emit(session, jsonCommand, MakeDisasm(address, *listing, *function, modules));
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
    // One copy of the code from the first entry to the last, MaxRead bytes
    // per request (a batch's total); unreadable parts stay int3 and decode to nothing.
    std::vector<uint8_t> code;
    uint64_t low = 0;
    if (!ranges->empty()) {
        low = ranges->front().start;
        uint64_t high = low;
        for (const auto& range : *ranges) high = std::max(high, range.end);
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
    std::vector<touch::Row> rows;
    for (auto& [start, row] : functions) rows.push_back(std::move(row));
    touch::Rank(rows);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    Emit(session, jsonCommand, MakeTouches(address, command.offsets, command.range, from, to, ranges->size(),
        code.size(), rows, ms, LoadedModules(session)));
}
}
