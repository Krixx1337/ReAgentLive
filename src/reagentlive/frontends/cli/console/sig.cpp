// reagentlive CLI console: sig-check, sig-file and sig-bytes, byte signatures against the live image.
#include <algorithm>
#include <chrono>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/code/disasm.h"
#include "reagentlive/frontends/cli/code/signature.h"
#include "reagentlive/frontends/cli/code/signature_json.h"
#include "reagentlive/core/client.h"

#include "reagentlive/frontends/cli/console/internal.h"

namespace reagentlive::cli::detail {
namespace {
// The module a signature is for: --module names any loaded one (one loaded
// since the list was read is found by one re-read); without it, the exe.
const Module* SigModule(Session& session, const std::string& name) {
    if (name.empty()) {
        const auto modules = LoadedModules(session);
        return modules.empty() ? nullptr : &modules.front();
    }
    const auto* module = text::ModuleNamed(name, LoadedModules(session));
    if (!module && RefreshModules(session)) module = text::ModuleNamed(name, LoadedModules(session));
    return module;
}

// Anchor pages: the whole image, resident or not (an untouched code page is
// still part of what a host's scanner reads). On the agent's worker thread
// when the host allows it, so faulting pages in costs the game nothing;
// otherwise in host-tick slices.
sig::SearchFn AgentSearch(Session& session) {
    return [&session](uint64_t from, uint64_t to, std::span<const uint8_t> anchor, size_t maxHits)
               -> std::expected<sig::SearchPage, std::string> {
        Client& client = session.client;
        SearchQuery query;
        query.pattern.assign(anchor.begin(), anchor.end());
        query.from = from;
        query.to = to;
        query.paged = true;
        query.maxHits = static_cast<uint32_t>(std::clamp<size_t>(maxHits, 1, MaxSearchHits));
        const auto timeout = std::max(client.Timeout(), std::chrono::milliseconds(180000));
        Result<SearchResult> found = std::unexpected(Error{});
        if (!session.sliceSearch) {
            query.background = true;
            found = client.Search(query, timeout);
            if (!found && found.error().kind == Error::Kind::Agent && found.error().status == Status::Refused &&
                found.error().reason.find("background") != std::string::npos) {
                session.sliceSearch = true;
            }
        }
        if (session.sliceSearch) {
            query.background = false;
            found = client.Search(query, timeout);
        }
        if (!found) return std::unexpected("search: " + found.error().message);
        return sig::SearchPage{std::move(found->hits), found->resume};
    };
}

}

sig::ReadFn AgentRead(Session& session) {
    return [&session](std::span<const ReadSpan> spans)
               -> std::expected<std::vector<std::optional<std::vector<uint8_t>>>, std::string> {
        auto batch = session.client.ReadBatch(spans);
        if (!batch) return std::unexpected("read: " + batch.error().message);
        std::vector<std::optional<std::vector<uint8_t>>> items;
        for (auto& item : batch->items)
            items.push_back(item.status == Status::Ok ? std::optional(std::move(item.bytes)) : std::nullopt);
        return items;
    };
}

namespace {
// With --expect (no --resolve) the match starts at an instruction boundary
// the reader gave: warnings come from decoding the live bytes there, the
// byte heuristic only past where decoding stops.
std::expected<void, std::string> DecodeWarnings(Session& session, const sig::Check& check, sig::CheckResult& result) {
    if (!check.expect || check.resolve) return {};
    const uint64_t wanted = *check.expect + check.offset;
    std::optional<std::vector<uint8_t>> bytes;
    for (const auto& match : result.matches)
        if (match.address == wanted) bytes = match.bytes;
    if (!bytes) {
        const ReadSpan span{wanted, check.pattern.Size() + 15}; // The last instruction may run past the pattern.
        auto items = AgentRead(session)(std::span(&span, 1));
        if (!items) return std::unexpected(items.error());
        if (items->empty() || !items->front()) return {}; // Unreadable: keep the heuristic's.
        bytes = std::move(*items->front());
    }
    size_t decoded = 0;
    result.warnings = dis::ExactFields(check.pattern, *bytes, wanted, LoadedModules(session), &decoded);
    result.decoded = decoded;
    return {};
}

void RunSigFile(Session& session, const Command& command, std::string_view jsonCommand) {
    std::ifstream file(command.file, std::ios::binary);
    if (!file) return JsonError(session, jsonCommand, "file", "cannot open " + command.file);
    sig::Checker checker(AgentSearch(session), AgentRead(session), LoadedModules(session));
    std::vector<std::optional<sig::CheckResult>> outcomes;
    shape::SigFile results;
    results.file = command.file;
    std::string line;
    for (size_t number = 1; std::getline(file, line); ++number) {
        const auto trimmed = text::Trim(line);
        if (trimmed.empty() || trimmed.starts_with('#')) continue;
        shape::SigFileRow row;
        row.line = number;
        const auto failed = [&](std::string message) {
            row.error = std::move(message);
            results.rows.push_back(std::move(row));
            outcomes.emplace_back();
        };
        auto entry = ParseSigLine(trimmed);
        if (!entry) { failed(entry.error()); continue; }
        const Module* module = SigModule(session, entry->module);
        if (!module) { failed(entry->module.empty() ? "modules unavailable" : "no loaded module " + entry->module); continue; }
        const Module chosen = *module;
        if (entry->rva && *entry->rva >= chosen.size) { failed("rva lies past the module"); continue; }
        const sig::Check check{entry->pattern, entry->rva ? std::optional(chosen.base + *entry->rva) : std::nullopt,
            entry->offset, entry->resolve};
        auto result = checker.Run(check, chosen);
        if (!result) { failed(result.error()); continue; }
        if (auto decoded = DecodeWarnings(session, check, *result); !decoded) { failed(decoded.error()); continue; }
        row.ok = true;
        row.result = MakeSigCheck(check, *result, chosen, LoadedModules(session));
        results.rows.push_back(std::move(row));
        outcomes.push_back(std::move(*result));
    }
    if (results.rows.empty()) return JsonError(session, jsonCommand, "file", "no patterns in " + command.file);
    if (const auto winner = sig::Winner(outcomes)) results.winner = results.rows[*winner].line;
    results.searches = checker.Searches();
    Emit(session, jsonCommand, std::move(results));
}

void RunSigBytes(Session& session, const Command& command, std::string_view jsonCommand) {
    const Request& request = command.request;
    auto bytes = session.client.Read(request.address, request.length);
    if (!bytes) return CommandError(session, jsonCommand, bytes.error());
    // The address is the reader's instruction boundary: decode from it.
    ExactFields exact;
    exact.fields = dis::ExactFields(sig::FromBytes(*bytes), *bytes, request.address, LoadedModules(session), &exact.decoded);
    Emit(session, jsonCommand, MakeSigBytes(request.address, *bytes, LoadedModules(session), exact));
}
}

void RunSig(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (command.sigMode == Command::SigMode::Bytes) return RunSigBytes(session, command, jsonCommand);
    if (!client.Supports(op::Search) || !client.Supports(op::ReadBatch))
        return JsonError(session, jsonCommand, "unsupported", "sig needs an agent with search and read-batch");
    // Held: the window reads (8 per request) follow each other in as few ticks as the budget allows.
    const Client::Hold hold(client);
    if (command.sigMode == Command::SigMode::File) return RunSigFile(session, command, jsonCommand);
    const Module* module = SigModule(session, command.target);
    if (!module) return JsonError(session, jsonCommand, "module", command.target.empty() ?
        "modules unavailable" : "no loaded module with that name");
    const Module chosen = *module; // The list may be re-read while checking.
    const sig::Check check{command.pattern, command.expect, command.offset, command.resolve};
    auto result = sig::Checker(AgentSearch(session), AgentRead(session), LoadedModules(session)).Run(check, chosen);
    if (!result) return JsonError(session, jsonCommand, "validation", result.error());
    if (auto decoded = DecodeWarnings(session, check, *result); !decoded)
        return JsonError(session, jsonCommand, "read", decoded.error());
    Emit(session, jsonCommand, MakeSigCheck(check, *result, chosen, LoadedModules(session)));
}
}
