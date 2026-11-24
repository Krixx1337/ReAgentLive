// reagentlive CLI console: native objects without an engine pack. vtable (slots
// read from live bytes, code decided by the module's section table; `rtti`
// the MSVC type information; `parts` the tables inside an object) and the
// region an address lies in (the agent's
// VirtualQuery, named here) for read and peek.
#include <algorithm>
#include <expected>
#include <optional>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/code/code_json.h"
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/code/native.h"
#include "reagentlive/frontends/cli/code/pe_names.h"
#include "reagentlive/core/client.h"

#include "reagentlive/frontends/cli/console/internal.h"

namespace reagentlive::cli::detail {
std::optional<std::string> RegionKind(Session& session, uint64_t address) {
    if (!session.client.Supports(op::Region)) return std::nullopt;
    auto info = session.client.Region(address);
    if (!info) return std::nullopt;
    return std::string(native::region::Kind(*info, std::nullopt));
}

std::optional<shape::Region> RegionOf(Session& session, uint64_t address) {
    Client& client = session.client;
    if (!client.Supports(op::Region)) return std::nullopt;
    auto info = client.Region(address);
    if (!info) return std::nullopt;
    std::optional<std::pair<bool, bool>> flags;
    if (info->type == native::region::Image)
        if (const Module* module = ModuleAt(address, LoadedModules(session)))
            if (auto layout = LayoutOf(session, *module))
                if (const auto* found = (*layout)->At(address - module->base))
                    flags = std::pair(found->Exec(), static_cast<bool>(found->flags & image::Writable));
    return MakeRegion(*info, native::region::Kind(*info, flags));
}

void RunVtable(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(op::ReadBatch)) return JsonError(session, jsonCommand, ErrorKind::Unsupported, "vtable needs read-batch");
    const Client::Hold hold(client);
    const uint64_t address = command.request.address;
    const auto modules = LoadedModules(session);
    const auto code = [&session](uint64_t value) { return InCode(session, value); };
    // Slots read: up to the highest asked, else view.limit, else until one stops it.
    const size_t count = !command.slots.empty() ? command.slots.back() + 1 : command.limit;
    // A table's slots as asked (picked, stubs resolved), or why not.
    const auto readTable = [&](uint64_t table) -> std::expected<native::Slots, std::string> {
        const Module* module = ModuleAt(table, modules);
        const size_t wanted = (count ? count : native::MaxSlots) * 8;
        size_t length = module ? static_cast<size_t>(std::min<uint64_t>(wanted, module->base + module->size - table))
                               : wanted;
        length -= length % 8;
        std::optional<std::vector<uint8_t>> bytes;
        if (length) {
            if (auto read = client.Read(table, static_cast<uint32_t>(length))) bytes = std::move(*read);
            else if (const uint64_t page = 0x1000 - (table & 0xfff); page < length && page >= 8) {
                if (auto part = client.Read(table, static_cast<uint32_t>(page - page % 8))) bytes = std::move(*part);
            }
        }
        if (!bytes) return std::unexpected("the table is unreadable");
        auto slots = native::ReadSlots(*bytes, count, code);
        native::Pick(slots, command.slots);
        if (command.bodies)
            if (auto resolved = native::ResolveBodies(AgentRead(session), slots.slots); !resolved)
                return std::unexpected(resolved.error());
        return slots;
    };
    if (command.partBytes) {
        std::vector<uint8_t> object;
        for (uint32_t at = 0; at < command.partBytes; at += static_cast<uint32_t>(MaxRead)) {
            const uint32_t length = std::min<uint32_t>(static_cast<uint32_t>(MaxRead), command.partBytes - at);
            auto bytes = client.Read(address + at, length);
            if (!bytes) {
                if (at) break;   // Past the object's readable end: scan what was read.
                return CommandError(session, jsonCommand, bytes.error());
            }
            object.insert(object.end(), bytes->begin(), bytes->end());
        }
        const auto image = [&modules](uint64_t value) { return ModuleAt(value, modules) != nullptr; };
        auto out = MakeVtable(address, 0, std::nullopt, std::nullopt, {}, modules);
        out.parts.emplace();
        for (const auto& candidate : native::PartCandidates(object, image, code)) {
            auto first = client.Read(candidate.table, 8);
            if (!first) continue;
            uint64_t entry = 0;
            format_detail::Load(*first, entry);
            if (!code(entry)) continue;
            native::Slots slots;
            if (!command.slots.empty()) {
                auto read = readTable(candidate.table);
                if (!read) return JsonError(session, jsonCommand, ErrorKind::Read, read.error());
                slots = std::move(*read);
            }
            out.parts->push_back(MakeVtablePart(candidate.offsets, candidate.table, slots.slots, modules));
        }
        return Emit(session, jsonCommand, std::move(out));
    }
    std::optional<uint64_t> table, found;
    auto bytes = client.Read(address + command.offset, 8);
    if (!bytes) return CommandError(session, jsonCommand, bytes.error());
    uint64_t value = 0;
    format_detail::Load(*bytes, value);
    const bool self = !command.offset && code(value);   // addr is the table: its first entry is code.
    if (self) table = address;
    else if (ModuleAt(value, modules)) table = value;
    else found = value;
    native::Slots slots;
    if (table) {
        auto read = readTable(*table);
        if (!read) return JsonError(session, jsonCommand, ErrorKind::Read, read.error());
        slots = std::move(*read);
    }
    auto out = MakeVtable(address, command.offset, table, found, slots, modules);
    if (command.rtti) {
        if (!table) out.rttiError = "no table: the qword there points into no module";
        else if (auto rtti = pe::ReadRtti(AgentRead(session), modules, *table))
            out.rtti = MakeVtableRtti(address, self ? 0 : command.offset, *rtti);
        else out.rttiError = rtti.error();
    }
    Emit(session, jsonCommand, std::move(out));
}
}
