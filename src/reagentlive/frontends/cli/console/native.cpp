// reagentlive CLI console: native objects without an engine pack. vtable (slots
// read from live bytes, code decided by the module's section table; --rtti
// the MSVC type information) and the region an address lies in (the agent's
// VirtualQuery, named here) for read and peek.
#include <algorithm>
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
    if (!client.Supports(op::ReadBatch)) return JsonError(session, jsonCommand, "unsupported", "vtable needs read-batch");
    const Client::Hold hold(client);
    const uint64_t address = command.request.address;
    const auto modules = LoadedModules(session);
    std::optional<uint64_t> table, found;
    if (command.table) {
        table = address;
    } else {
        auto bytes = client.Read(address + command.offset, 8);
        if (!bytes) return CommandError(session, jsonCommand, bytes.error());
        uint64_t value = 0;
        format_detail::Load(*bytes, value);
        if (ModuleAt(value, modules)) table = value;
        else found = value;
    }
    native::Slots slots;
    if (table) {
        const Module* module = ModuleAt(*table, modules);
        const size_t wanted = (command.limit ? command.limit : native::MaxSlots) * 8;
        size_t length = module ? static_cast<size_t>(std::min<uint64_t>(wanted, module->base + module->size - *table))
                               : wanted;
        length -= length % 8;
        std::optional<std::vector<uint8_t>> bytes;
        if (length) {
            if (auto read = client.Read(*table, static_cast<uint32_t>(length))) bytes = std::move(*read);
            else if (const uint64_t page = 0x1000 - (*table & 0xfff); page < length && page >= 8) {
                if (auto part = client.Read(*table, static_cast<uint32_t>(page - page % 8))) bytes = std::move(*part);
            }
        }
        if (!bytes) return JsonError(session, jsonCommand, "read", "the table is unreadable");
        slots = native::ReadSlots(*bytes, command.limit, [&session](uint64_t value) { return InCode(session, value); });
        if (command.bodies)
            if (auto resolved = native::ResolveBodies(AgentRead(session), slots.slots); !resolved)
                return JsonError(session, jsonCommand, "read", resolved.error());
    }
    auto out = MakeVtable(address, command.offset, table, found, slots, LoadedModules(session));
    if (command.rtti) {
        if (!table) out.rttiError = "no table: the qword there points into no module";
        else if (auto rtti = pe::ReadRtti(AgentRead(session), LoadedModules(session), *table))
            out.rtti = MakeVtableRtti(address, command.table ? 0 : command.offset, *rtti);
        else out.rttiError = rtti.error();
    }
    Emit(session, jsonCommand, std::move(out));
}
}
