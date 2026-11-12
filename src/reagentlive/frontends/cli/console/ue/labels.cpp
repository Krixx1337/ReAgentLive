// reagentlive CLI console: export-labels, names ReAgentLive knows for a static-analysis tool.
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "reagentlive/frontends/cli/results/labels.h"

#include "reagentlive/frontends/cli/console/ue/internal_ue.h"

namespace reagentlive::cli::detail {
namespace {
constexpr std::string_view DefaultPrefix = "Default__";
constexpr size_t MaxInterfaceLookups = 4096;

// Reads `length` bytes at each address, MaxBatchReads per request; nullopt where unreadable.
std::vector<std::optional<std::vector<uint8_t>>> ReadEach(Client& client, std::span<const uint64_t> addresses,
                                                          uint32_t length) {
    std::vector<std::optional<std::vector<uint8_t>>> out(addresses.size());
    for (size_t start = 0; start < addresses.size(); start += MaxBatchReads) {
        std::vector<ReadSpan> reads;
        for (size_t i = start; i < std::min(addresses.size(), start + MaxBatchReads); ++i)
            reads.push_back({addresses[i], length});
        auto batch = client.ReadBatch(reads);
        if (!batch) continue;
        for (size_t i = 0; i < batch->items.size(); ++i)
            if (batch->items[i].status == Status::Ok) out[start + i] = std::move(batch->items[i].bytes);
    }
    return out;
}

uint64_t Qword(const std::optional<std::vector<uint8_t>>& bytes, size_t at = 0) {
    uint64_t value = 0;
    if (bytes && bytes->size() >= at + 8) memcpy(&value, bytes->data() + at, 8);
    return value;
}
}

void RunExportLabels(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    const uint32_t kinds = command.limit;
    const bool wantExec = kinds & label_kind::Exec, wantVtables = kinds & label_kind::Vtable,
        wantInterfaces = kinds & label_kind::Interface;
    if ((wantExec && !client.Supports(ue::op::ExecEntries)) ||
        ((wantVtables || wantInterfaces) && !client.Supports(ue::op::Find)) ||
        (wantInterfaces && (!client.Supports(ue::op::Function) || !client.Supports(ue::op::Functions) ||
                            !client.Supports(ue::op::DescribeMany))))
        return JsonError(session, jsonCommand, "unsupported", "export-labels needs a newer agent for these kinds");
    const auto modules = LoadedModules(session);
    const Module* module = command.target.empty() ? (modules.empty() ? nullptr : &modules.front())
                                                  : text::ModuleNamed(command.target, modules);
    if (!module) return JsonError(session, jsonCommand, "module", "no loaded module with that name");
    const auto timestamp = ImageTimestamp(session, *module);
    if (!timestamp) return JsonError(session, jsonCommand, "module", "the module's PE header is unreadable");
    const auto inside = [&](uint64_t address) { return address >= module->base && address - module->base < module->size; };
    std::vector<LabelRow> rows;

    // Globals the reader confirmed with peek --label, for this build only.
    size_t globals = 0;
    if (std::ifstream in(command.file); in) {
        for (std::string line; std::getline(in, line);)
            if (auto row = ParseLabel(line, *module, *timestamp); row && row->kind == "global") {
                rows.push_back(std::move(*row));
                ++globals;
            }
    }

    // Every native function's exec thunk.
    size_t execCount = 0;
    if (wantExec) {
        auto execs = ue::ExecEntries(client);
        if (!execs) return CommandError(session, jsonCommand, execs.error());
        for (const auto& exec : *execs)
            if (exec.exec && inside(exec.exec) && !exec.owner.empty()) {
                rows.push_back({exec.exec - module->base, exec.owner + "::exec" + exec.name, "exec"});
                ++execCount;
            }
    }

    // Class default objects: one per class, so classes with no live instance count too.
    std::vector<ue::Object> defaults;
    for (uint32_t from = 0; wantVtables || wantInterfaces;) {
        auto page = ue::Find(client, DefaultPrefix, {{}, from});
        if (!page) return CommandError(session, jsonCommand, page.error());
        for (auto& object : page->objects)
            if (object.name.starts_with(DefaultPrefix)) defaults.push_back(std::move(object));
        if (!page->next) break;
        from = page->next;
    }
    std::vector<uint64_t> addresses;
    size_t vtableCount = 0;
    if (wantVtables) {
        for (const auto& object : defaults) addresses.push_back(object.ref.address);
        const auto vtables = ReadEach(client, addresses, 8);
        std::vector<std::pair<uint64_t, std::string>> owners;
        for (size_t i = 0; i < defaults.size(); ++i)
            if (const uint64_t vtable = Qword(vtables[i]); inside(vtable))
                owners.emplace_back(vtable - module->base, defaults[i].name.substr(DefaultPrefix.size()));
        auto vtableRows = VtableLabels(owners);
        vtableCount = vtableRows.size();
        rows.insert(rows.end(), vtableRows.begin(), vtableRows.end());
    }

    // Native interface implementations (best effort): each native class's
    // interfaces array, then `func` on its default object per interface function.
    std::vector<size_t> natives;
    for (size_t i = 0; wantInterfaces && i < defaults.size(); ++i)
        if (!BlueprintClass(defaults[i].name)) natives.push_back(i);
    addresses.clear();
    for (const size_t i : natives) addresses.push_back(defaults[i].ref.address + ue::ObjectIdentityOffset + 4);
    const auto classes = ReadEach(client, addresses, 8);
    addresses.clear();
    for (const auto& type : classes) addresses.push_back(Qword(type) ? Qword(type) + ue::InterfacesOffset : 1);
    const auto headers = ReadEach(client, addresses, 16);
    std::vector<std::pair<size_t, uint64_t>> implemented; // (default object, interface class pointer)
    std::set<uint64_t> interfaceClasses;
    {
        std::vector<uint64_t> arrays;
        std::vector<std::pair<size_t, uint32_t>> owners2;
        for (size_t n = 0; n < natives.size(); ++n) {
            int32_t count = 0;
            if (headers[n] && headers[n]->size() == 16) memcpy(&count, headers[n]->data() + 8, 4);
            const uint64_t data = Qword(headers[n]);
            if (!data || count <= 0 || count > static_cast<int32_t>(ue::MaxInterfaces)) continue;
            arrays.push_back(data);
            owners2.emplace_back(natives[n], static_cast<uint32_t>(count));
        }
        for (size_t start = 0, end = 0; start < arrays.size(); start = end) {
            std::vector<ReadSpan> reads;
            for (end = start; end < arrays.size(); ++end) {
                reads.push_back({arrays[end], owners2[end].second * uint64_t{ue::InterfaceEntrySize}});
                if (!ReadBatchSize(reads)) { reads.pop_back(); break; } // Past the response budget.
            }
            auto batch = client.ReadBatch(reads);
            if (!batch) continue;
            for (size_t k = 0; k < batch->items.size(); ++k) {
                if (batch->items[k].status != Status::Ok) continue;
                const auto& bytes = batch->items[k].bytes;
                for (uint32_t e = 0; e < owners2[start + k].second; ++e) {
                    uint64_t type = 0;
                    memcpy(&type, bytes.data() + e * ue::InterfaceEntrySize, 8);
                    if (!type) continue;
                    implemented.emplace_back(owners2[start + k].first, type);
                    interfaceClasses.insert(type);
                }
            }
        }
    }
    // Each interface's own functions, by name.
    std::map<uint64_t, std::pair<std::string, std::vector<std::string>>> interfaces;
    {
        std::vector<Ref> refs;
        for (const uint64_t type : interfaceClasses) refs.push_back(ue::AddressRef(type));
        auto described = ue::DescribeMany(client, refs);
        if (!described) return CommandError(session, jsonCommand, described.error());
        for (size_t i = 0; i < refs.size(); ++i) {
            if (!(*described)[i]) continue;
            const auto& iface = *(*described)[i];
            std::vector<std::string> functions;
            for (uint32_t from = 0;;) {
                auto list = ue::Functions(client, iface.ref, "", from);
                if (!list) break;
                for (const auto& text : list->names)
                    if (text.starts_with(iface.name + ":")) functions.push_back(text.substr(iface.name.size() + 1));
                if (!list->next) break;
                from = list->next;
            }
            interfaces[refs[i].address] = {iface.name, std::move(functions)};
        }
    }
    size_t interfaceCount = 0, lookups = 0;
    bool capped = false;
    for (const auto& [index, type] : implemented) {
        const auto found = interfaces.find(type);
        if (found == interfaces.end()) continue;
        const auto& [iface, functions] = found->second;
        const auto className = defaults[index].name.substr(DefaultPrefix.size());
        for (const auto& name : functions) {
            if (lookups == MaxInterfaceLookups) { capped = true; break; }
            ++lookups;
            auto function = ue::Function(client, defaults[index].ref, iface + ":" + name);
            if (!function || !function->interface || !inside(function->implementation)) continue;
            rows.push_back({function->implementation - module->base, className + "::" + iface + "_" + name, "interface"});
            ++interfaceCount;
        }
    }

    rows = MergeLabels(std::move(rows));
    const auto temporary = std::filesystem::path(command.file).concat(".tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        for (const auto& row : rows)
            out << ToJson(MakeLabel(*module, *timestamp, module->base + row.rva, row.name, row.kind)).dump() << '\n';
        if (!out) return JsonError(session, jsonCommand, "file", "cannot write " + command.file);
    }
    std::error_code error;
    std::filesystem::rename(temporary, command.file, error);
    if (error) return JsonError(session, jsonCommand, "file", "cannot replace " + command.file + ": " + error.message());
    Emit(session, jsonCommand, shape::ExportLabels{command.file, module->name, *timestamp, rows.size(), globals,
        execCount, vtableCount, interfaceCount, lookups, capped});
}
}
