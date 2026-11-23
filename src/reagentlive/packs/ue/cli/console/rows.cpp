// reagentlive CLI console, Unreal pack: rows, a data table's RowMap or a
// curve table's curves.
#include <algorithm>
#include <cstring>
#include <iterator>
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

// A TMap<FName, T*> (TSetElement: the pair, HashNextId, HashIndex): 24-byte
// slots. Its allocated slots as {name index, number, pointer}, the sparse
// header checked before any is trusted; `layout` words the refusal.
constexpr uint32_t Stride = 24;
constexpr size_t MaxTableBytes = 16u << 20;
struct MapRow { int32_t index; uint32_t number; uint64_t pointer; };
Result<std::vector<MapRow>> ReadNameMap(Client& client, const ue::SparseHeader& map, const std::string& layout) {
    if (!ue::SparseHeaderValid(map) || size_t(map.num) * Stride > MaxTableBytes) return std::unexpected(Refusal(layout));
    const auto slots = static_cast<uint32_t>(map.num);
    std::vector<uint32_t> bits(map.secondaryBits ? (slots + 31) / 32 : 4);
    if (map.secondaryBits && slots) {
        auto read = client.ReadRange(map.secondaryBits, bits.size() * 4);
        if (!read) return std::unexpected(read.error());
        memcpy(bits.data(), read->data(), bits.size() * 4);
    } else {
        memcpy(bits.data(), map.inlineBits, sizeof(map.inlineBits));
    }
    uint32_t allocated = 0;
    ue::WalkSlots(bits.data(), slots, 0, UINT32_MAX, [&](uint32_t) { ++allocated; });
    if (allocated != slots - static_cast<uint32_t>(map.numFree))
        return std::unexpected(Refusal(layout + " (allocation bits disagree)"));
    std::vector<uint8_t> elements;
    if (slots) {
        auto read = client.ReadRange(map.data, size_t{slots} * Stride);
        if (!read) return std::unexpected(read.error());
        elements = std::move(*read);
    }
    std::vector<MapRow> rows;
    ue::WalkSlots(bits.data(), slots, 0, UINT32_MAX, [&](uint32_t slot) {
        MapRow row{};
        memcpy(&row.index, &elements[size_t{slot} * Stride], 4);
        memcpy(&row.number, &elements[size_t{slot} * Stride + 4], 4);
        memcpy(&row.pointer, &elements[size_t{slot} * Stride + 8], 8);
        rows.push_back(row);
    });
    return rows;
}

// Each row's name with its instance suffix (Row_2), in row order.
Result<std::vector<std::string>> RowNames(Client& client, std::span<const MapRow> rows) {
    std::vector<int32_t> indices;
    for (const auto& row : rows) if (std::ranges::find(indices, row.index) == indices.end()) indices.push_back(row.index);
    std::vector<std::string> text;
    if (!indices.empty()) {
        auto names = ue::Names(client, indices);
        if (!names) return std::unexpected(names.error());
        text = std::move(*names);
    }
    std::vector<std::string> out;
    for (const auto& row : rows) {
        const auto at = static_cast<size_t>(std::ranges::find(indices, row.index) - indices.begin());
        out.push_back(NameText(at < text.size() ? text[at] : "#" + std::to_string(row.index), row.number));
    }
    return out;
}

// A curve table: its class is CurveTable or CompositeCurveTable, or the agent
// finds it by its exact name as an instance of a CurveTable subclass.
Result<bool> IsCurveTable(Client& client, const Ref& table) {
    auto described = ue::Describe(client, table);
    if (!described) return std::unexpected(described.error());
    if (described->className == "CurveTable" || described->className == "CompositeCurveTable") return true;
    ue::FindOptions options;
    options.className = "CurveTable";
    options.exact = true;
    auto found = ue::Find(client, described->name, options);
    if (!found) return std::unexpected(found.error());
    return std::ranges::any_of(found->objects, [&](const ue::Object& o) { return o.ref.address == table.address; });
}

// ERichCurveInterpMode, and one curve evaluated as the engine does
// (FRichCurve::Eval, FSimpleCurve::Eval, constant extrapolation): outside the
// keys the end key's value; between two, the earlier key's mode decides:
// constant (and none) keeps its value, linear blends, cubic is the Bezier of
// the two values and the leave and arrive tangents (unweighted). A simple
// curve has one mode for all keys and is linear unless constant.
constexpr const char* InterpNames[] = {"linear", "constant", "cubic", "none"};
const char* InterpName(uint8_t mode) { return mode < std::size(InterpNames) ? InterpNames[mode] : "unknown"; }
struct CurveKey { float time = 0, value = 0, arrive = 0, leave = 0; uint8_t interp = 0; };
float Evaluate(std::span<const CurveKey> keys, float t, bool rich) {
    if (t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time) return keys.back().value;
    size_t i = 0;
    while (i + 2 < keys.size() && keys[i + 1].time <= t) ++i;
    const auto& a = keys[i];
    const auto& b = keys[i + 1];
    const float span = b.time - a.time;
    if (span <= 0 || a.interp == 1 || a.interp == 3) return a.value;
    const float alpha = (t - a.time) / span;
    if (!rich || a.interp != 2) return a.value + (b.value - a.value) * alpha;
    const float p0 = a.value, p3 = b.value, p1 = p0 + a.leave * span / 3, p2 = p3 - b.arrive * span / 3;
    const float u = 1 - alpha;
    return u * u * u * p0 + 3 * u * u * alpha * p1 + 3 * u * alpha * alpha * p2 + alpha * alpha * alpha * p3;
}

// UCurveTable: RowMap, a TMap<FName, FRealCurve*>, and CurveTableMode (1
// simple, 2 rich), both unreflected (engine_layout.h, curvetable group).
void RunCurveRows(Session& session, const Command& command, const Ref& table, std::string_view jsonCommand) {
    Client& client = session.client;
    constexpr auto& Layout = ue::layout::StockUE5;
    constexpr uint32_t MaxCurveKeys = 4096;
    const auto fail = [&](std::string message) { CommandError(session, jsonCommand, Refusal(std::move(message))); };
    if (!command.rowPaths.empty() || command.nonzero)
        return fail("fields and nonzero read data tables; a curve table's rows are keys");
    auto head = ue::ReadObject(client, table, static_cast<uint32_t>(Layout.curveRowMap), sizeof(ue::SparseHeader));
    if (!head) return CommandError(session, jsonCommand, head.error());
    auto modeByte = ue::ReadObject(client, table, static_cast<uint32_t>(Layout.curveTableMode), 1);
    if (!modeByte) return CommandError(session, jsonCommand, modeByte.error());
    ue::SparseHeader map{};
    memcpy(&map, head->data(), sizeof(map));
    const uint8_t mode = (*modeByte)[0];
    const Client::Hold hold(client);
    auto rows = ReadNameMap(client, map, "RowMap is not the stock UE5 curve table TMap<FName, FRealCurve*>");
    if (!rows) return CommandError(session, jsonCommand, rows.error());
    if (!rows->empty() && mode != 1 && mode != 2)
        return fail("CurveTableMode reads " + std::to_string(mode) +
                    ", not 1 (simple) or 2 (rich): not the stock UE5 curve table");
    const bool rich = mode == 2;
    auto names = RowNames(client, *rows);
    if (!names) return CommandError(session, jsonCommand, names.error());
    std::vector<size_t> kept;
    for (size_t i = 0; i < rows->size(); ++i)
        if (command.match.empty() || text::ContainsNoCase((*names)[i], command.match)) kept.push_back(i);
    shape::Rows out;
    out.ref = ReusableRef(table);
    out.curve = rich ? "rich" : "simple";
    if (command.curveAt) out.at = ShortFloat(*command.curveAt);
    out.total = kept.size();
    out.from = std::min<uint32_t>(command.from, static_cast<uint32_t>(kept.size()));
    const uint32_t left = static_cast<uint32_t>(kept.size()) - out.from;
    const uint32_t shown = command.limit ? std::min(command.limit, left) : left;
    if (out.from + shown < kept.size()) out.nextCursor = out.from + shown;
    // Each curve is read from its first member used to the end of its keys' TArray header.
    const auto first = static_cast<uint32_t>(rich ? Layout.richCurveKeys : std::min(Layout.simpleCurveInterp, Layout.simpleCurveKeys));
    const uint32_t keysAt = static_cast<uint32_t>(rich ? Layout.richCurveKeys : Layout.simpleCurveKeys) - first;
    const uint32_t stride = rich ? ue::layout::RichCurveKeySize : ue::layout::SimpleCurveKeySize;
    for (uint32_t n = 0; n < shown; ++n) {
        const size_t i = kept[out.from + n];
        const auto& row = (*rows)[i];
        shape::TableRow item{(*names)[i], Addr(row.pointer)};
        std::string error;
        std::vector<uint8_t> curve, raw;
        int32_t count = 0;
        if (!row.pointer) error = "null curve";
        else if (auto read = client.ReadRange(row.pointer + first, keysAt + 16); !read) error = read.error().message;
        else curve = std::move(*read);
        if (error.empty()) {
            uint64_t data = 0;
            memcpy(&data, curve.data() + keysAt, 8);
            memcpy(&count, curve.data() + keysAt + 8, 4);
            if (count < 0 || static_cast<uint32_t>(count) > MaxCurveKeys || (count && !data))
                error = "keys are not a TArray of up to " + std::to_string(MaxCurveKeys) + " keys";
            else if (count) {
                if (auto read = client.ReadRange(data, size_t(count) * stride); !read) error = read.error().message;
                else raw = std::move(*read);
            }
        }
        if (!error.empty()) {
            item.value = Json{{"error", error}};
            out.truncated = true;
            out.rows.push_back(std::move(item));
            continue;
        }
        const uint8_t simpleInterp = rich ? 0 : curve[static_cast<uint32_t>(Layout.simpleCurveInterp) - first];
        if (!rich) item.interp = InterpName(simpleInterp);
        std::vector<CurveKey> keys(static_cast<size_t>(count));
        Json list = Json::array();
        bool weighted = false;
        for (size_t k = 0; k < keys.size(); ++k) {
            const uint8_t* at = raw.data() + k * stride;
            auto& key = keys[k];
            if (rich) {
                // FRichCurveKey: InterpMode, TangentMode, TangentWeightMode, pad, Time, Value,
                // ArriveTangent, ArriveTangentWeight, LeaveTangent, LeaveTangentWeight.
                key.interp = at[0];
                weighted |= at[2] != 0;
                memcpy(&key.time, at + 4, 4);
                memcpy(&key.value, at + 8, 4);
                memcpy(&key.arrive, at + 12, 4);
                memcpy(&key.leave, at + 20, 4);
                list.push_back({ShortFloat(key.time), ShortFloat(key.value), InterpName(key.interp),
                                ShortFloat(key.arrive), ShortFloat(key.leave)});
            } else {
                key.interp = simpleInterp;
                memcpy(&key.time, at, 4);
                memcpy(&key.value, at + 4, 4);
                list.push_back({ShortFloat(key.time), ShortFloat(key.value)});
            }
        }
        item.keys = std::move(list);
        if (weighted) item.weighted = true;
        if (command.curveAt && !keys.empty()) item.value = ShortFloat(Evaluate(keys, *command.curveAt, rich));
        out.rows.push_back(std::move(item));
    }
    Emit(session, jsonCommand, std::move(out));
}
}

// UDataTable: RowStruct (reflected), then RowMap, a TMap<FName, uint8*>. An
// object with no RowStruct is read as a curve table when it is one.
void RunRows(Session& session, const Command& command, StructCache& structs, std::string_view jsonCommand) {
    Client& client = session.client;
    Ref table = ue::GetRef(command.request);
    const auto fail = [&](std::string message) { CommandError(session, jsonCommand, Refusal(std::move(message))); };
    if (table.index < 0) { // An address alone: object reads need the full ref.
        auto described = ue::Describe(client, table);
        if (!described) return CommandError(session, jsonCommand, described.error());
        table = described->ref;
    }
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
    if (rowStruct == (*fields)->end() || rowStruct->size != 8 || rowStruct->type != "ObjectProperty") {
        auto curve = IsCurveTable(client, table);
        if (!curve) return CommandError(session, jsonCommand, curve.error());
        if (*curve) return RunCurveRows(session, command, table, jsonCommand);
        return fail("neither a data table (no RowStruct field) nor a curve table (no CurveTable class)");
    }
    if (command.curveAt) return fail("at evaluates a curve table's rows; this is a data table");
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
    RowReader reader(session, structs);
    const RowPaths paths = ParsePaths(command.rowPaths);
    if (auto why = reader.Check(type, paths); !why.empty()) return fail(std::move(why));

    const Client::Hold hold(client);
    auto read = ReadNameMap(client, map, "RowMap is not the stock UE5 TMap<FName, uint8*> after RowStruct");
    if (!read) return CommandError(session, jsonCommand, read.error());
    auto names = RowNames(client, *read);
    if (!names) return CommandError(session, jsonCommand, names.error());
    // `view.match` on the names, before paging.
    std::vector<MapRow> rows;
    std::vector<std::string> rowNames;
    for (size_t i = 0; i < read->size(); ++i)
        if (command.match.empty() || text::ContainsNoCase((*names)[i], command.match)) {
            rows.push_back((*read)[i]);
            rowNames.push_back(std::move((*names)[i]));
        }
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
        if (!row.pointer) { errors[i] = "null row"; continue; }
        auto rowBytes = client.ReadRange(row.pointer, rowSize);
        if (!rowBytes) { errors[i] = rowBytes.error().message; continue; }
        bytes[i] = std::move(*rowBytes);
    }
    bool ignored = false;
    for (uint32_t i = 0; i < shown; ++i)
        if (errors[i].empty()) reader.Value(type, bytes[i], paths, ignored);
    reader.Resolve();
    for (uint32_t i = 0; i < shown; ++i) {
        const auto& row = rows[out.from + i];
        shape::TableRow item{rowNames[out.from + i], Addr(row.pointer)};
        Json value = errors[i].empty() ? reader.Value(type, bytes[i], paths, out.truncated) : Json{{"error", errors[i]}};
        if (!errors[i].empty()) out.truncated = true;
        if (command.nonzero) KeepNonZero(value);
        item.value = std::move(value);
        out.rows.push_back(std::move(item));
    }
    if (reader.Unnamed()) out.unnamed = reader.Unnamed();
    Emit(session, jsonCommand, std::move(out));
}
}
