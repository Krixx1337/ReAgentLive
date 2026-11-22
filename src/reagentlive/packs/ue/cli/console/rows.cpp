// reagentlive CLI console, Unreal pack: rows, a data table's RowMap.
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/core/client.h"
#include "reagentlive/packs/ue/client_ue.h"

#include "reagentlive/packs/ue/cli/console/internal_ue.h"
#include "reagentlive/packs/ue/cli/console/rows.h"

namespace reagentlive::cli::detail {
namespace {
Error Refusal(std::string message) { return Error{Error::Kind::Agent, Status::BadRequest, 0, {}, std::move(message)}; }
}

// UDataTable: RowStruct (reflected), then RowMap, a TMap<FName, uint8*>
// (TSetElement: the pair, HashNextId, HashIndex).
void RunRows(Session& session, const Command& command, StructCache& structs, std::string_view jsonCommand) {
    Client& client = session.client;
    constexpr uint32_t Stride = 24;
    constexpr size_t MaxTableBytes = 16u << 20;
    const Ref table = ue::GetRef(command.request);
    const auto fail = [&](std::string message) { CommandError(session, jsonCommand, Refusal(std::move(message))); };
    auto fields = ObjectTable(session, table);
    if (!fields) return CommandError(session, jsonCommand, fields.error());
    // RowStruct by its name, its readable name, or (names obfuscated) as the
    // one object field the DataTable class declares.
    const auto isRowStruct = [&](const ue::Field& f) {
        return f.name == "RowStruct" || session.names.Find(f.owner, f.offset, f.size, f.boolMask) == "RowStruct";
    };
    auto rowStruct = std::ranges::find_if(**fields, isRowStruct);
    if (rowStruct == (*fields)->end() &&
        std::ranges::count_if(**fields, [](const ue::Field& f) { return f.owner == "DataTable" && f.type == "ObjectProperty"; }) == 1)
        rowStruct = std::ranges::find_if(**fields, [](const ue::Field& f) {
            return f.owner == "DataTable" && f.type == "ObjectProperty"; });
    if (rowStruct == (*fields)->end() || rowStruct->size != 8 || rowStruct->type != "ObjectProperty")
        return fail("not a data table: no RowStruct field");
    auto head = ue::ReadObject(client, table, rowStruct->offset, 8 + sizeof(ue::SparseHeader));
    if (!head) return CommandError(session, jsonCommand, head.error());
    uint64_t structAddress = 0;
    ue::SparseHeader map{};
    memcpy(&structAddress, head->data(), 8);
    memcpy(&map, head->data() + 8, sizeof(map));
    if (!structAddress) return fail("RowStruct is null");
    auto described = ue::Describe(client, Ref{static_cast<uintptr_t>(structAddress), -1, 0});
    if (!described) return CommandError(session, jsonCommand, described.error());
    ue::Field type{};
    type.name = "RowStruct";
    type.type = "StructProperty";
    type.structType = described->ref;
    type.structName = described->name;
    const auto* members = structs(described->ref);
    if (!members) return fail("RowStruct " + described->name + " has no readable members");
    const uint32_t rowSize = static_cast<uint32_t>(DumpEnd(*members));
    const std::string layout = "RowMap is not the stock UE5 TMap<FName, uint8*> after RowStruct";
    if (!ue::SparseHeaderValid(map) || size_t(map.num) * Stride > MaxTableBytes) return fail(layout);
    const auto slots = static_cast<uint32_t>(map.num);
    std::vector<uint32_t> bits(map.secondaryBits ? (slots + 31) / 32 : 4);
    if (map.secondaryBits && slots) {
        auto read = client.ReadRange(map.secondaryBits, bits.size() * 4);
        if (!read) return CommandError(session, jsonCommand, read.error());
        memcpy(bits.data(), read->data(), bits.size() * 4);
    } else {
        memcpy(bits.data(), map.inlineBits, sizeof(map.inlineBits));
    }
    uint32_t allocated = 0;
    ue::WalkSlots(bits.data(), slots, 0, UINT32_MAX, [&](uint32_t) { ++allocated; });
    if (allocated != slots - static_cast<uint32_t>(map.numFree)) return fail(layout + " (allocation bits disagree)");
    RowReader reader(session, structs);
    const RowPaths paths = ParsePaths(command.rowPaths);
    if (auto why = reader.Check(type, paths); !why.empty()) return fail(std::move(why));

    const Client::Hold hold(client);
    std::vector<uint8_t> elements;
    if (slots) {
        auto read = client.ReadRange(map.data, size_t{slots} * Stride);
        if (!read) return CommandError(session, jsonCommand, read.error());
        elements = std::move(*read);
    }
    struct Slot { int32_t index; uint32_t number; uint64_t row; };
    std::vector<Slot> rows;
    ue::WalkSlots(bits.data(), slots, 0, UINT32_MAX, [&](uint32_t slot) {
        Slot row{};
        memcpy(&row.index, &elements[size_t{slot} * Stride], 4);
        memcpy(&row.number, &elements[size_t{slot} * Stride + 4], 4);
        memcpy(&row.row, &elements[size_t{slot} * Stride + 8], 8);
        rows.push_back(row);
    });
    // Row names first: --match needs them.
    std::vector<int32_t> rowNames;
    for (const auto& row : rows) if (std::ranges::find(rowNames, row.index) == rowNames.end()) rowNames.push_back(row.index);
    std::vector<std::string> rowText;
    if (!rowNames.empty()) {
        auto names = ue::Names(client, rowNames);
        if (!names) return CommandError(session, jsonCommand, names.error());
        rowText = std::move(*names);
    }
    const auto rowName = [&](const Slot& row) {
        const auto at = static_cast<size_t>(std::ranges::find(rowNames, row.index) - rowNames.begin());
        return NameText(at < rowText.size() ? rowText[at] : "#" + std::to_string(row.index), row.number);
    };
    if (!command.match.empty())
        std::erase_if(rows, [&](const Slot& row) { return !text::ContainsNoCase(rowName(row), command.match); });
    shape::Rows out;
    out.ref = ReusableRef(table);
    out.rowStruct = described->name;
    out.rowStructRef = FullRef(described->ref);
    out.total = rows.size();
    out.from = std::min<uint32_t>(command.from, static_cast<uint32_t>(rows.size()));
    const uint32_t left = static_cast<uint32_t>(rows.size()) - out.from;
    const uint32_t shown = command.limit ? std::min(command.limit, left) : left;
    if (out.from + shown < rows.size()) out.nextCursor = out.from + shown;
    // Each shown row's bytes, then its values: names collected, resolved, then rendered.
    std::vector<std::vector<uint8_t>> bytes(shown);
    std::vector<std::string> errors(shown);
    for (uint32_t i = 0; i < shown; ++i) {
        const auto& row = rows[out.from + i];
        if (!row.row) { errors[i] = "null row"; continue; }
        auto read = client.ReadRange(row.row, rowSize);
        if (!read) { errors[i] = read.error().message; continue; }
        bytes[i] = std::move(*read);
    }
    std::vector<int32_t> nameIndices;
    const auto collect = [&](int32_t index) -> std::string_view {
        if (index >= 0 && std::ranges::find(nameIndices, index) == nameIndices.end()) nameIndices.push_back(index);
        return {};
    };
    bool ignored = false;
    for (uint32_t i = 0; i < shown; ++i)
        if (errors[i].empty()) reader.Value(type, bytes[i], paths, collect, ignored);
    std::vector<std::string> resolved;
    if (!nameIndices.empty())
        if (auto names = ue::Names(client, nameIndices); names) resolved = std::move(*names);
    reader.Resolve();
    const auto named = [&](int32_t index) -> std::string_view {
        const auto at = std::ranges::find(nameIndices, index);
        const auto slot = static_cast<size_t>(at - nameIndices.begin());
        return at == nameIndices.end() || slot >= resolved.size() ? std::string_view{} : std::string_view(resolved[slot]);
    };
    for (uint32_t i = 0; i < shown; ++i) {
        const auto& row = rows[out.from + i];
        shape::TableRow item{rowName(row), Addr(row.row), nullptr};
        if (errors[i].empty()) item.value = reader.Value(type, bytes[i], paths, named, out.truncated);
        else { item.value = {{"error", errors[i]}}; out.truncated = true; }
        if (command.nonzero) KeepNonZero(item.value);
        out.rows.push_back(std::move(item));
    }
    if (reader.Unnamed()) out.unnamed = reader.Unnamed();
    Emit(session, jsonCommand, std::move(out));
}
}
