// reagentlive CLI console: native objects without an engine pack. vtable (slots
// read from live bytes, code decided by the module's section table) and
// region (the agent's VirtualQuery, named here).
#include <algorithm>
#include <optional>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/code/code_json.h"
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/code/native.h"
#include "reagentlive/core/client.h"

#include "reagentlive/frontends/cli/console/internal.h"

namespace reagentlive::cli::detail {
std::optional<std::string> RegionKind(Session& session, uint64_t address) {
    if (!session.client.Supports(op::Region)) return std::nullopt;
    auto info = session.client.Region(address);
    if (!info) return std::nullopt;
    return std::string(native::region::Kind(*info, std::nullopt));
}

void RunRegion(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(op::Region)) return JsonError(session, jsonCommand, "unsupported", "agent does not support region");
    const uint64_t address = command.request.address;
    auto info = client.Region(address);
    if (!info) return CommandError(session, jsonCommand, info.error());
    std::optional<std::pair<bool, bool>> flags;
    std::string section;
    if (info->type == native::region::Image)
        if (const Module* module = ModuleAt(address, LoadedModules(session)))
            if (auto layout = LayoutOf(session, *module))
                if (const auto* found = (*layout)->At(address - module->base)) {
                    flags = std::pair(found->Exec(), static_cast<bool>(found->flags & image::Writable));
                    section = found->name;
                }
    Emit(session, jsonCommand, MakeRegion(address, *info, native::region::Kind(*info, flags), section,
        LoadedModules(session)));
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
    Emit(session, jsonCommand, MakeVtable(address, command.offset, table, found, slots, LoadedModules(session)));
}
}
