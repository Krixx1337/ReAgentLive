// reagentlive CLI console: Function metadata, parameters and typed calls.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <deque>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include "reagentlive/packs/ue/cli/results/call_text.h"
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/packs/ue/cli/state/cache_ue.h"
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/packs/ue/cli/values/format_ue.h"
#include "reagentlive/packs/ue/cli/values/json_output.h"
#include "reagentlive/packs/ue/cli/values/pack_input.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/core/client.h"
#include "reagentlive/packs/ue/client_ue.h"

#include "reagentlive/packs/ue/cli/console/internal_ue.h"

namespace reagentlive::cli::detail {

shape::Call MakeCall(const Request& request, std::span<const uint8_t> block, const char* mode, bool kept) {
    shape::Call out;
    out.ref = ReusableRef(ue::GetRef(request));
    out.function = request.text;
    out.mode = mode;
    out.blockSize = block.size();
    out.hex = HexBytes(block);
    out.kept = kept;
    return out;
}

shape::Call MakeDryCall(const Request& request, std::span<const uint8_t> block, const ue::FunctionInfo& function) {
    auto out = MakeCall(request, block, "dry", false);
    out.function = function.owner + ":" + function.name;
    out.exec = NonZero(function.exec);
    out.pure = ue::EachFunctionAllowed(function.flags);
    return out;
}

void PrintFunction(Session& session, const Ref& ref, std::string_view name, std::string_view jsonCommand) {
    Client& client = session.client;
    const auto function = ue::Function(client, ref, name);
    if (!function) return CommandError(session, jsonCommand, function.error());
    if (!function->exec) return Emit(session, jsonCommand, MakeFunc(ref, *function, {}));
    const auto modules = client.Modules();
    return Emit(session, jsonCommand, modules ? MakeFunc(ref, *function, *modules) :
        MakeFunc(ref, *function, {}, modules.error().message));
}

void PrintParameters(Session& session, const ue::FunctionInfo& function, const ue::Signature& signature, const Ref& object) {
    Emit(session, "params", MakeParams(object, function, signature));
}

namespace {
// A name input: "#index[:number]" checked against the pool, else the name's
// text (any case), instance 0. `missing` keeps the text the pool lacks.
bool PackName(Session& session, std::string_view value, int32_t& index, uint32_t& number, std::string& missing) {
    if (value.starts_with('#')) {
        value.remove_prefix(1);
        const auto colon = value.find(':');
        uint64_t id = 0, instance = 0;
        if (!text::Number(value.substr(0, colon), id) || id > INT32_MAX ||
            (colon != std::string_view::npos && !text::Number(value.substr(colon + 1), instance)) ||
            instance > INT32_MAX) return false;
        index = static_cast<int32_t>(id);
        auto names = ue::Names(session.client, std::span(&index, 1));
        if (!names || names->front().empty()) { missing = "#" + std::to_string(id); return false; }
        number = static_cast<uint32_t>(instance);
        return true;
    }
    const auto found = NameIndex(session, value);
    if (!found) {
        if (found.error().kind == Error::Kind::Agent && found.error().status == Status::NotFound) missing = value;
        return false;
    }
    index = *found;
    number = 0;
    return true;
}
}

namespace {
// --fields a,b.c as a tree: the members kept at one level, each with the
// paths kept below it (empty: all of it).
struct RowPaths {
    std::vector<std::string> names;
    std::vector<RowPaths> below;
    bool Empty() const { return names.empty(); }
    const RowPaths* Find(const ue::Field& member, const std::string& key) const {
        for (size_t i = 0; i < names.size(); ++i)
            if (text::SameText(names[i], member.name) || text::SameText(names[i], key)) return &below[i];
        return nullptr;
    }
};
RowPaths ParsePaths(std::span<const std::string> paths) {
    RowPaths out;
    for (const auto& path : paths) {
        RowPaths* level = &out;
        std::string_view rest = path;
        while (!rest.empty()) {
            const auto dot = rest.find('.');
            const std::string name(rest.substr(0, dot));
            rest = dot == std::string_view::npos ? std::string_view{} : rest.substr(dot + 1);
            auto at = std::ranges::find_if(level->names, [&](const std::string& n) { return text::SameText(n, name); });
            if (at == level->names.end()) {
                level->names.push_back(name);
                level->below.emplace_back();
                at = std::prev(level->names.end());
            }
            level = &level->below[static_cast<size_t>(at - level->names.begin())];
        }
    }
    return out;
}

// An array's element as a field of its own.
ue::Field Element(const ue::Field& array) {
    ue::Field element{};
    element.name = array.name;
    element.size = array.elementSize;
    element.type = array.elementType;
    element.structType = array.structType;
    element.structName = array.structName;
    return element;
}

// The rows of returned arrays: each element read from the engine's memory
// (the agent keeps the block that owns it) and decoded with its reflected
// struct, nested arrays included. Rendered twice: once collecting names and
// texts, once with them resolved (ResolveTexts between); element reads are
// cached between the two.
class RowReader {
public:
    static constexpr uint32_t MaxNested = 256;    // Elements per nested array.
    static constexpr unsigned MaxDepth = 3;       // Arrays inside arrays.
    static constexpr size_t MaxBytes = 16u << 20; // All element reads of one call.

    RowReader(Session& session, StructCache& structs) : session_(session), structs_(structs) {}

    // Why a path names no member, or empty.
    std::string Check(const ue::Field& element, const RowPaths& paths, unsigned depth = 0) {
        if (paths.Empty()) return {};
        const auto* members = element.type == "StructProperty" ? structs_(element.structType) : nullptr;
        if (!members) return element.name + " has no members to pick (" + element.type + ")";
        for (size_t i = 0; i < paths.names.size(); ++i) {
            // A member by its reflected name or the readable one rows print.
            const ValueStyle style{false, &session_.names};
            const auto found = std::ranges::find_if(*members, [&](const ue::Field& m) {
                return text::SameText(m.name, paths.names[i]) ||
                    text::SameText(MemberKey(m, style, Json::object()), paths.names[i]); });
            if (found == members->end()) {
                std::string known;
                for (const auto& m : *members) known += (known.empty() ? "" : ", ") + MemberKey(m, style, Json::object());
                return "no member " + paths.names[i] + " in " + element.structName + " (members: " + known + ")";
            }
            if (paths.below[i].Empty()) continue;
            if (depth >= MaxDepth) return "--fields goes " + std::to_string(MaxDepth) + " levels deep";
            const ue::Field next = found->type == "ArrayProperty" ? Element(*found) : *found;
            if (auto why = Check(next, paths.below[i], depth + 1); !why.empty()) return why;
        }
        return {};
    }

    template<class Names>
    Json Array(const ue::Field& array, std::span<const uint8_t> header, uint32_t from, uint32_t limit,
               const RowPaths& paths, const Names& names, bool& truncated, unsigned depth = 0) {
        ue::ArrayHeader h{};
        if (header.size() != sizeof(h)) return nullptr;
        memcpy(&h, header.data(), sizeof(h));
        Json out = {{"count", h.count}, {"capacity", h.capacity},
            {"address", h.data ? Json(format_detail::Pointer(h.data)) : Json(nullptr)}};
        const ue::Field element = Element(array);
        if (h.count < 0 || h.capacity < h.count || !element.size || (h.count && !h.data)) {
            out["rows_error"] = "not a valid array header";
            truncated = true;
            return out;
        }
        const auto count = static_cast<uint32_t>(h.count);
        const uint32_t first = std::min(from, count);
        const uint32_t shown = limit ? std::min(limit, count - first) : count - first;
        Json rows = Json::array();
        if (shown) {
            const auto* bytes = Elements(h.data + uint64_t{first} * element.size, size_t{shown} * element.size);
            if (!bytes) {
                out["rows_error"] = error_.empty() ? "elements unreadable" : error_;
                truncated = true;
                return out;
            }
            for (uint32_t i = 0; i < shown; ++i)
                rows.push_back(Row(element, std::span(*bytes).subspan(size_t{i} * element.size, element.size), paths,
                    names, truncated, depth));
        }
        out["rows"] = std::move(rows);
        if (first) out["start"] = first;
        if (first + shown < count) {
            if (depth) { out["truncated"] = true; truncated = true; }
            else out["next_cursor"] = first + shown;
        }
        return out;
    }

    // Text members met so far, converted by the engine (TextBatch).
    void ResolveTexts() { texts_.Resolve(session_); }

    // One struct value as a row (rows: a data table row).
    template<class Names>
    Json Struct(const ue::Field& type, std::span<const uint8_t> bytes, const RowPaths& paths, const Names& names,
                bool& truncated) {
        return Row(type, bytes, paths, names, truncated, 0);
    }

private:
    template<class Names>
    Json Row(const ue::Field& element, std::span<const uint8_t> bytes, const RowPaths& paths, const Names& names,
             bool& truncated, unsigned depth) {
        const ValueStyle style{false, &session_.names};
        const auto* members = element.type == "StructProperty" ? structs_(element.structType) : nullptr;
        if (!members || members->empty()) return JsonFieldValue(element, bytes, structs_, names, truncated, style, 1);
        Json row = Json::object();
        for (const auto& member : *members) {
            const auto key = MemberKey(member, style, row);
            const RowPaths* below = paths.Empty() ? &paths : paths.Find(member, key);
            if (!below) continue;
            if (!member.size || size_t{member.offset} + member.size > bytes.size()) { row[key] = nullptr; continue; }
            const auto data = bytes.subspan(member.offset, member.size);
            if (member.type == "ArrayProperty" && depth < MaxDepth)
                row[key] = Array(member, data, 0, MaxNested, *below, names, truncated, depth + 1);
            else if (!below->Empty() && member.type == "StructProperty")
                row[key] = Row(member, data, *below, names, truncated, depth);
            else {
                texts_.Add(member, data);
                row[key] = texts_.Value(member, data, JsonFieldValue(member, data, structs_, names, truncated, style, 1));
            }
        }
        return row;
    }

    const std::vector<uint8_t>* Elements(uint64_t address, size_t size) {
        const auto key = std::pair{address, size};
        if (const auto found = std::ranges::find(reads_, key, &Read::key); found != reads_.end()) return &found->bytes;
        if (spent_ + size > MaxBytes) {
            error_ = "the call's arrays pass " + std::to_string(MaxBytes >> 20) + " MiB of reads";
            return nullptr;
        }
        auto bytes = session_.client.ReadRange(address, size);
        if (!bytes) { error_ = bytes.error().message; return nullptr; }
        spent_ += size;
        reads_.push_back({key, std::move(*bytes)});
        return &reads_.back().bytes;
    }

    struct Read { std::pair<uint64_t, size_t> key; std::vector<uint8_t> bytes; };
    Session& session_;
    StructCache& structs_;
    TextBatch texts_;
    std::deque<Read> reads_;
    size_t spent_ = 0;
    std::string error_;
};
}

void RunTypedCall(Session& session, const Command& command, const ue::FunctionInfo& function,
                  const ue::Signature& signature, StructCache& structs, std::string_view jsonCommand) {
    Client& client = session.client;
    const auto& arguments = command.fields;
    const auto fail = [&](std::string message) {
        JsonError(session, jsonCommand, "validation", message);
    };
    if (signature.size > ue::MaxParams) return fail("typed call parameter block too large");
    std::vector<uint8_t> bytes(signature.size);
    size_t inputs = 0;
    for (const auto& parameter : signature.parameters) {
        const auto flags = parameter.parameterFlags;
        // Inputs are plain single values. Outputs start zeroed, as a raw call
        // with no block sends them, and may own memory (an array, a string):
        // the agent keeps that block.
        // A const reference is an input whose value travels in the block, and
        // a returned reference (const FVector) is the return value's own slot.
        // Only a non-const in/out reference is refused.
        const bool input = ue::InputParameter(flags);
        const uint8_t required = input ? ue::Param | ue::Plain | ue::Scalar : ue::Param | ue::Scalar;
        if ((flags & required) != required || ((flags & ue::Reference) && !input && !(flags & ue::Return)) ||
            !parameter.size || size_t{parameter.offset} + parameter.size > bytes.size())
            return fail("unsupported typed parameter: " + parameter.name);
        if (!input) continue;
        ++inputs;
        const auto key = parameter.name + "=";
        const auto matches = [&](const std::string& argument) { return argument.starts_with(key); };
        const auto argument = std::ranges::find_if(arguments, matches);
        if (argument == arguments.end()) return fail("missing parameter: " + parameter.name);
        if (std::count_if(argument, arguments.end(), matches) != 1) return fail("duplicate parameter: " + parameter.name);
        // Bools, integers, floats, enums, names, tags and plain structs
        // ("(X=1,Y=2,Z=3)" or "(1,2,3)").
        std::string missing;
        const auto nameOf = [&](std::string_view text, int32_t& index, uint32_t& number) {
            return PackName(session, text, index, number, missing);
        };
        if (!pack_input::PackValue(parameter, std::string_view(*argument).substr(key.size()),
            std::span(bytes).subspan(parameter.offset, parameter.size),
            [&](const Ref& type) { return structs(type); }, nameOf))
            return fail(missing.empty() ? "invalid or unsupported input: " + parameter.name :
                "no pooled name is '" + missing + "' (parameter " + parameter.name + ")");
    }
    if (inputs != arguments.size()) return fail("unexpected typed argument");
    if (command.dry) return Emit(session, jsonCommand, MakeDryCall(command.request, bytes, function));
    bool kept = false;
    auto result = ue::Call(client, ue::GetRef(command.request), command.request.text, bytes, function.address,
        command.request.flags, &kept);
    if (!result) return CommandError(session, jsonCommand, result.error());
    if (result->size() != bytes.size())
        return CommandError(session, jsonCommand, Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "incomplete typed call result"});
    // Returned arrays as rows (--fields, --start, --limit pick from them).
    RowReader rows(session, structs);
    const RowPaths paths = ParsePaths(command.rowPaths);
    const auto arrayOutput = [](const ue::Field& parameter) {
        return !ue::InputParameter(parameter.parameterFlags) && parameter.type == "ArrayProperty";
    };
    if (!command.rowPaths.empty()) {
        if (std::ranges::none_of(signature.parameters, arrayOutput)) return fail("--fields picks from an array output; none");
        for (const auto& parameter : signature.parameters)
            if (arrayOutput(parameter))
                if (auto why = rows.Check(Element(parameter), paths); !why.empty()) return fail(std::move(why));
    }
    std::vector<int32_t> nameIndices;
    const auto collect = [&](int32_t index) -> std::string_view {
        if (index >= 0 && std::ranges::find(nameIndices, index) == nameIndices.end()) nameIndices.push_back(index);
        return {};
    };
    for (const auto& parameter : signature.parameters) {
        if (ue::InputParameter(parameter.parameterFlags)) continue;
        bool ignored = false;
        if (arrayOutput(parameter))
            rows.Array(parameter, FieldBytes(*result, parameter), command.from, command.limit, paths, collect, ignored);
        else
            JsonFieldValue(parameter, FieldBytes(*result, parameter), structs, collect, ignored, Style(session));
    }
    std::vector<std::string> resolved;
    if (!nameIndices.empty() && client.Supports(ue::op::Names))
        if (auto names = ue::Names(client, nameIndices); names) resolved = std::move(*names);
    rows.ResolveTexts();
    const auto named = [&](int32_t index) -> std::string_view {
        const auto at = std::ranges::find(nameIndices, index);
        const auto slot = static_cast<size_t>(at - nameIndices.begin());
        return at == nameIndices.end() || slot >= resolved.size() ? std::string_view{} : std::string_view(resolved[slot]);
    };
    std::vector<shape::CallOutput> outputs;
    bool truncated = false;
    for (const auto& parameter : signature.parameters) {
        if (ue::InputParameter(parameter.parameterFlags)) continue;
        shape::CallOutput row;
        row.name = parameter.name;
        row.direction = parameter.parameterFlags & ue::Return ? "return" : "out";
        if (session.meta) row.meta = MakeFieldInfo(parameter);
        bool partial = false;
        row.value = arrayOutput(parameter) ?
            rows.Array(parameter, FieldBytes(*result, parameter), command.from, command.limit, paths, named, partial) :
            JsonFieldValue(parameter, FieldBytes(*result, parameter), structs, named, partial, Style(session));
        row.truncated = partial;
        truncated |= partial;
        outputs.push_back(std::move(row));
    }
    auto record = MakeCall(command.request, *result, "typed", kept);
    if (kept) record.keptReason = calls::KeptReason(signature, structs);
    record.outputs = std::move(outputs);
    record.truncated = truncated;
    return Emit(session, jsonCommand, std::move(record));
}

namespace {
Error Refusal(std::string message) { return Error{Error::Kind::Agent, Status::BadRequest, 0, {}, std::move(message)}; }
// A pooled name as the engine prints it: Name, or Name_N for instance N+1.
std::string NameText(std::string_view name, uint32_t number) {
    return number ? std::string(name) + "_" + std::to_string(number - 1) : std::string(name);
}
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
        if (errors[i].empty()) reader.Struct(type, bytes[i], paths, collect, ignored);
    std::vector<std::string> resolved;
    if (!nameIndices.empty())
        if (auto names = ue::Names(client, nameIndices); names) resolved = std::move(*names);
    reader.ResolveTexts();
    const auto named = [&](int32_t index) -> std::string_view {
        const auto at = std::ranges::find(nameIndices, index);
        const auto slot = static_cast<size_t>(at - nameIndices.begin());
        return at == nameIndices.end() || slot >= resolved.size() ? std::string_view{} : std::string_view(resolved[slot]);
    };
    for (uint32_t i = 0; i < shown; ++i) {
        const auto& row = rows[out.from + i];
        shape::TableRow item{rowName(row), Addr(row.row), nullptr};
        if (errors[i].empty()) item.value = reader.Struct(type, bytes[i], paths, named, out.truncated);
        else { item.value = {{"error", errors[i]}}; out.truncated = true; }
        out.rows.push_back(std::move(item));
    }
    Emit(session, jsonCommand, std::move(out));
}
}
