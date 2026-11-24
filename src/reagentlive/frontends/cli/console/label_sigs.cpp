// reagentlive CLI console: labels-sign, labels-port and labels-check, byte
// signatures on a labels file's names so they survive a patch. Each is
// sig-check's matcher in a loop over the file's rows.
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/code/disasm.h"
#include "reagentlive/frontends/cli/code/label_sign.h"
#include "reagentlive/frontends/cli/code/xrefs.h"
#include "reagentlive/core/client.h"

#include "reagentlive/frontends/cli/console/internal.h"

namespace reagentlive::cli::detail {
namespace {
constexpr size_t MaxGlobalSites = 8; // Code references tried per global.

struct LabelsFile {
    std::string path;
    std::vector<std::string> lines;
    Module module;
    uint32_t timestamp = 0;
};

// The file (default: the host's names file, else the console's), its lines,
// and the module and build its rows are matched against.
std::expected<LabelsFile, std::pair<ErrorKind, std::string>> OpenLabels(Session& session, const Command& command) {
    LabelsFile file;
    file.path = !command.file.empty() ? command.file
        : !session.hostLabels.empty() ? session.hostLabels.string() : session.labels.string();
    if (file.path.empty()) return std::unexpected(std::pair(ErrorKind::File, "no labels file: give one"));
    std::ifstream in(file.path, std::ios::binary);
    if (!in) return std::unexpected(std::pair(ErrorKind::File, "cannot open " + file.path));
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) file.lines.push_back(std::move(line));
    }
    const auto modules = LoadedModules(session);
    const Module* module = command.target.empty() ? (modules.empty() ? nullptr : &modules.front())
                                                  : text::ModuleNamed(command.target, modules);
    if (!module) return std::unexpected(std::pair(ErrorKind::Module, "no loaded module with that name"));
    file.module = *module;
    const auto timestamp = ImageTimestamp(session, file.module);
    if (!timestamp) return std::unexpected(std::pair(ErrorKind::Module, "the module's PE header is unreadable"));
    file.timestamp = *timestamp;
    return file;
}

// The live bytes at an instruction boundary as a pattern, address fields
// masked, and where each whole instruction in them ends.
struct SiteBytes {
    sig::Pattern pattern;
    std::vector<size_t> ends;
};
SiteBytes Masked(Session& session, std::span<const uint8_t> bytes, uint64_t address) {
    const auto pattern = sig::FromBytes(bytes);
    SiteBytes out{sig::Masked(pattern, dis::ExactFields(pattern, bytes, address, LoadedModules(session))), {}};
    for (size_t at = 0; at < bytes.size();) {
        const auto instruction = dis::Decode(bytes.subspan(at), address + at);
        if (!instruction) break;
        at += instruction->bytes.size();
        out.ends.push_back(at);
    }
    return out;
}
std::optional<SiteBytes> ReadSite(Session& session, const Module& module, uint64_t address) {
    const uint64_t length = std::min<uint64_t>(sig::MaxPattern, module.base + module.size - address);
    auto bytes = session.client.Read(address, static_cast<uint32_t>(length));
    if (!bytes || bytes->empty()) return std::nullopt;
    return Masked(session, *bytes, address);
}

// A global's reference signature: the first code site loading it (lea, mov,
// any [rip + disp32] operand) whose signature grows unique and resolves to it.
std::expected<std::pair<LabelSig, uint64_t>, std::string> SignGlobal(Session& session, sig::Checker& checker,
                                                                    const Module& module, uint64_t global) {
    Client& client = session.client;
    if (!client.Supports(op::Xrefs)) return std::unexpected("the agent has no xrefs");
    XrefQuery query;
    query.target = global;
    query.from = module.base;
    query.to = module.base + module.size;
    auto fields = ScanAll(session, query, xref::MaxRelative, [&client](const XrefQuery& q, auto timeout) {
        return client.Xrefs(q, timeout);
    });
    if (!fields) return std::unexpected("xrefs: " + fields.error().message);
    std::string reason = "no code loads it";
    size_t tried = 0;
    for (const uint64_t field : fields->first) {
        if (tried == MaxGlobalSites) break;
        if (field < module.base + xref::Before) continue;
        auto window = client.Read(field - xref::Before, xref::Window);
        if (!window || window->size() != xref::Window) continue;
        const auto hit = xref::Classify(field, std::span<const uint8_t, xref::Window>(window->data(), xref::Window), global);
        if (!hit.instruction || hit.kind == "call" || hit.kind == "jmp" || hit.kind == "jcc") continue;
        ++tried;
        const uint64_t site = *hit.instruction;
        auto bytes = client.Read(site, static_cast<uint32_t>(std::min<uint64_t>(sig::MaxPattern, module.base + module.size - site)));
        if (!bytes) continue;
        auto instruction = dis::Decode(*bytes, site);
        if (!instruction) continue;
        std::optional<sig::Resolve> resolve;
        for (const auto& f : dis::AddressFields(*instruction, 0))
            if (std::string_view(f.kind) == "rip" && f.target == global && site + f.offset == field)
                resolve = sig::Resolve{static_cast<uint32_t>(instruction->bytes.size()), static_cast<uint32_t>(f.offset), 4};
        if (!resolve) continue;
        const auto full = Masked(session, *bytes, site);
        auto grown = GrowSignature(full.pattern, resolve->next, full.ends, [&](const sig::Pattern& cut) {
            return checker.Run(sig::Check{cut, global, 0, resolve}, module);
        });
        if (grown.pattern) return std::pair(LabelSig{std::move(*grown.pattern), 0, resolve}, site);
        reason = "no load site unique: " + grown.reason;
    }
    return std::unexpected(reason);
}

std::expected<void, std::string> Rewrite(const std::string& path, std::span<const std::string> lines) {
    const auto temporary = std::filesystem::path(path).concat(".tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        for (const auto& line : lines) out << line << '\n';
        if (!out) return std::unexpected("cannot write " + path);
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) return std::unexpected("cannot replace " + path + ": " + error.message());
    return {};
}
}

void RunLabelsSign(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(op::Search) || !client.Supports(op::ReadBatch))
        return JsonError(session, jsonCommand, ErrorKind::Unsupported, "labels-sign needs an agent with search and read-batch");
    auto file = OpenLabels(session, command);
    if (!file) return JsonError(session, jsonCommand, file.error().first, file.error().second);
    const Module& module = file->module;
    const Client::Hold hold(client);
    sig::Checker checker(AgentSearch(session), AgentRead(session), LoadedModules(session));
    shape::LabelsSign result;
    result.path = file->path;
    result.module = module.name;
    result.timestamp = file->timestamp;
    auto lines = file->lines;
    bool changed = false;
    for (auto& line : lines) {
        const auto row = ParseLabel(line, module, file->timestamp);
        if (!row || (row->kind != "user" && row->kind != "global")) continue;
        if (row->extra.contains("sig") && !command.all) { ++result.skipped; continue; }
        if (FailedBefore(row->extra, file->timestamp) && !command.all) { ++result.failedBefore; continue; }
        const uint64_t address = module.base + row->rva;
        shape::LabelSignRow out{row->name, row->kind, row->rva, "unsigned", {}, {}, {}};
        std::optional<LabelSig> signature;
        if (row->kind == "user") {
            if (const auto full = ReadSite(session, module, address)) {
                auto grown = GrowSignature(full->pattern, 0, full->ends, [&](const sig::Pattern& cut) {
                    return checker.Run(sig::Check{cut, address, 0, std::nullopt}, module);
                });
                if (grown.pattern) signature = LabelSig{std::move(*grown.pattern)};
                else out.reason = grown.reason;
            } else {
                out.reason = "its bytes are unreadable";
            }
        } else if (auto global = SignGlobal(session, checker, module, address)) {
            signature = std::move(global->first);
            out.site = global->second - module.base;
        } else {
            out.reason = global.error();
        }
        // Signed: the sig, no failure mark. Not: the mark, so later calls on
        // this build skip the row.
        auto json = Json::parse(line);
        if (signature) {
            json["sig"] = LabelSigJson(*signature);
            json.erase("sig_failed");
            out.status = "signed";
            out.length = signature->pattern.Size();
            ++result.signedRows;
        } else {
            json["sig_failed"] = {{"timestamp", file->timestamp}, {"reason", out.reason.value_or("")}};
            ++result.unsignedRows;
        }
        if (auto text = json.dump(); text != line) {
            line = std::move(text);
            changed = true;
        }
        result.rows.push_back(std::move(out));
    }
    result.searches = checker.Searches();
    if (changed)
        if (auto written = Rewrite(file->path, lines); !written) return JsonError(session, jsonCommand, ErrorKind::File, written.error());
    Emit(session, jsonCommand, std::move(result));
}

void RunLabelsPort(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(op::Search) || !client.Supports(op::ReadBatch))
        return JsonError(session, jsonCommand, ErrorKind::Unsupported, "labels-port needs an agent with search and read-batch");
    auto file = OpenLabels(session, command);
    if (!file) return JsonError(session, jsonCommand, file.error().first, file.error().second);
    const Module& module = file->module;
    const auto plan = PlanPort(file->lines, module, file->timestamp);
    const Client::Hold hold(client);
    sig::Checker checker(AgentSearch(session), AgentRead(session), LoadedModules(session));
    shape::LabelsPort result;
    result.path = file->path;
    result.module = module.name;
    result.timestamp = file->timestamp;
    result.present = plan.present.size();
    result.unsignedNames = plan.unsignedNames;
    std::vector<std::string> added;
    for (const auto& candidate : plan.candidates) {
        const auto outcome = PortResult(candidate.sig,
            checker.Run(sig::Check{candidate.sig.pattern, std::nullopt, 0, candidate.sig.resolve}, module), module);
        shape::LabelPortRow row{candidate.name, candidate.kind, candidate.from, outcome.status, {}, {}, {}};
        if (outcome.status == "ported") {
            row.rva = *outcome.rva;
            // A new row: the signature and its origin, never the old row's sig_failed.
            auto line = ToJson(MakeLabel(module, file->timestamp, module.base + *outcome.rva, candidate.name,
                candidate.kind.empty() ? "user" : candidate.kind));
            line["sig"] = candidate.sigJson;
            line["ported_from"] = candidate.from;
            added.push_back(line.dump());
            ++result.ported;
        } else if (outcome.status == "ambiguous") {
            row.matches = outcome.matches;
            ++result.ambiguous;
        } else {
            row.reason = outcome.reason;
            ++result.missing;
        }
        result.rows.push_back(std::move(row));
    }
    result.searches = checker.Searches();
    if (!added.empty()) {
        auto lines = file->lines;
        lines.insert(lines.end(), added.begin(), added.end());
        if (auto written = Rewrite(file->path, lines); !written) return JsonError(session, jsonCommand, ErrorKind::File, written.error());
    }
    Emit(session, jsonCommand, std::move(result));
}

// labels-check: this build's signed rows, each searched in the image (no
// expect: where it lands is the answer) and compared with its row. Read-only.
void RunLabelsCheck(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(op::Search) || !client.Supports(op::ReadBatch))
        return JsonError(session, jsonCommand, ErrorKind::Unsupported, "labels-check needs an agent with search and read-batch");
    auto file = OpenLabels(session, command);
    if (!file) return JsonError(session, jsonCommand, file.error().first, file.error().second);
    const Module& module = file->module;
    const Client::Hold hold(client);
    sig::Checker checker(AgentSearch(session), AgentRead(session), LoadedModules(session));
    shape::LabelsCheck result;
    result.path = file->path;
    result.module = module.name;
    result.timestamp = file->timestamp;
    for (const auto& line : file->lines) {
        const auto row = ParseLabel(line, module, file->timestamp);
        if (!row) continue;
        const auto found = row->extra.find("sig");
        if (found == row->extra.end()) {
            if (row->kind == "user" || row->kind == "global") ++result.unsignedRows;
            continue;
        }
        shape::LabelCheckRow out{row->name, row->kind, row->rva, "invalid", {}, {}, {}};
        const auto signature = ParseLabelSig(*found);
        if (!signature) {
            out.reason = "its sig is not one sig-check takes";
            ++result.invalid;
            result.rows.push_back(std::move(out));
            continue;
        }
        const auto outcome = CheckLabel(*signature,
            checker.Run(sig::Check{signature->pattern, std::nullopt, 0, signature->resolve}, module), module, row->rva);
        out.status = outcome.status;
        if (outcome.found) out.found = *outcome.found;
        if (outcome.status == "ambiguous") out.matches = outcome.matches;
        if (!outcome.reason.empty()) out.reason = outcome.reason;
        if (outcome.status == "ok") ++result.ok;
        else if (outcome.status == "moved") ++result.moved;
        else if (outcome.status == "ambiguous") ++result.ambiguous;
        else ++result.missing;
        result.rows.push_back(std::move(out));
    }
    result.searches = checker.Searches();
    Emit(session, jsonCommand, std::move(result));
}
}
