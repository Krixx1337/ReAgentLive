// reagentlive CLI console: layout-export and layout-check, many types' reflected layouts in one command.
#include <algorithm>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "reagentlive/packs/ue/cli/results/layout.h"

#include "reagentlive/packs/ue/cli/console/internal_ue.h"

namespace reagentlive::cli::detail {
namespace {
shape::LayoutField FieldOf(const ue::Field& field) { return {field.offset, field.size, field.type}; }

// The fields a class or struct (by name) lists, or an error.
Result<std::vector<ue::Field>> ClassFields(Session& session, const std::string& name) {
    auto type = ResolveClass(session, name);
    if (!type) return std::unexpected(type.error());
    return ObjectFields(session, *type);
}

bool NotFound(const Error& error) { return error.kind == Error::Kind::Agent && error.status == Status::NotFound; }

// Types (classes and structs) one export writes at most; past it, capped.
constexpr size_t MaxLayoutTypes = 1024;

// One type to export: found by name or through a field's struct ref, and
// how many references away from a named one it is.
struct Pending { std::string name; Ref ref; uint32_t depth; };

// A class's or struct's line: its kind, super and size (UStruct::SuperStruct
// and ::Size, read at the stock offsets), or an error.
Result<Json> TypeLine(Session& session, const Ref& type, const std::string& name, std::string& super) {
    Client& client = session.client;
    auto described = ue::Describe(client, type);
    if (!described) return std::unexpected(described.error());
    constexpr auto& Layout = ue::layout::StockUE5;
    const auto from = static_cast<uint32_t>(std::min(Layout.superStruct, Layout.structSize));
    const auto to = static_cast<uint32_t>(std::max(Layout.superStruct + 8, Layout.structSize + 4));
    auto bytes = ue::ReadObject(client, described->ref, from, to - from);
    if (!bytes) return std::unexpected(bytes.error());
    uint64_t superAddress = 0;
    int32_t size = 0;
    memcpy(&superAddress, bytes->data() + (Layout.superStruct - from), 8);
    memcpy(&size, bytes->data() + (Layout.structSize - from), 4);
    const bool isClass = described->className.ends_with("Class");
    Json line{{"kind", isClass ? "class" : "struct"}, {"name", name}, {"size", LayoutHex(size > 0 ? uint64_t(size) : 0)}};
    super.clear();
    if (superAddress) {
        auto parent = ue::Describe(client, Ref{static_cast<uintptr_t>(superAddress), -1, 0});
        if (parent) {
            super = parent->name;
            line["super"] = super;
        }
    }
    return line;
}

// The field's line with its struct, class or enum and its container parts;
// `refer` gets each type it names: a struct with its ref, a class by name,
// an enum by name with the bytes it takes (enum is true).
using Refer = std::function<void(const std::string& name, const Ref& ref, bool isEnum, uint32_t size)>;
Json LayoutFieldLine(const ue::Field& field, const Refer& refer) {
    Json line = LayoutJson(field.owner, field.name, field.offset, field.size, field.type);
    const bool array = field.type == "ArrayProperty", set = field.type == "SetProperty", map = field.type == "MapProperty";
    if (field.type == "BoolProperty") line["bool_mask"] = field.boolMask;
    if (array || set || map) {
        if (!field.elementType.empty())
            line[map ? "key" : "element"] = LayoutPart(field.elementType, field.elementSize, field.structName, field.subtype);
        if (map && !field.valueType.empty())
            line["value"] = LayoutPart(field.valueType, field.valueSize, field.valueStructName, field.valueSubtype);
        if ((set || map) && field.stride) line["stride"] = field.stride;
        if (map && field.stride) line["value_offset"] = LayoutHex(field.valueOffset);
    } else {
        if (!field.structName.empty()) line["struct"] = field.structName;
        if (!field.subtype.empty()) line[LayoutEnumType(field.type) ? "enum" : "class"] = field.subtype;
    }
    if (!field.structName.empty()) refer(field.structName, field.structType, false, 0);
    if (!field.valueStructName.empty()) refer(field.valueStructName, field.valueStructType, false, 0);
    const auto subtype = [&](const std::string& type, const std::string& name, uint32_t size) {
        if (name.empty()) return;
        if (LayoutEnumType(type)) refer(name, Ref{}, true, type == "ByteProperty" ? 1 : size);
        else refer(name, Ref{}, false, 0);
    };
    if (array || set || map) subtype(field.elementType, field.subtype, field.elementSize);
    else subtype(field.type, field.subtype, field.size);
    if (map) subtype(field.valueType, field.valueSubtype, field.valueSize);
    return line;
}

// An enum's line: its entries, named as the engine stores them.
Result<Json> EnumLine(Session& session, const Ref& object, const std::string& name, std::optional<uint32_t> size) {
    Client& client = session.client;
    auto entries = ue::EnumEntries(client, object);
    if (!entries) return std::unexpected(entries.error());
    std::vector<int32_t> indices;
    for (const auto& entry : *entries) indices.push_back(entry.name);
    std::vector<std::string> names;
    if (!indices.empty()) {
        auto found = ue::Names(client, indices);
        if (!found) return std::unexpected(found.error());
        names = std::move(*found);
    }
    Json values = Json::array();
    for (size_t i = 0; i < entries->size(); ++i) {
        const auto& entry = (*entries)[i];
        std::string text = i < names.size() && !names[i].empty() ? names[i] : "#" + std::to_string(entry.name);
        if (entry.number) text += "_" + std::to_string(entry.number - 1);
        values.push_back({std::move(text), entry.value});
    }
    Json line{{"kind", "enum"}, {"name", name}};
    if (size) line["size"] = *size;
    line["values"] = std::move(values);
    return line;
}

// An enum by its exact name, or NotFound.
Result<Ref> ResolveEnum(Session& session, const std::string& name) {
    auto found = ue::Find(session.client, name, {"Enum", 0, true});
    if (!found) return std::unexpected(found.error());
    for (const auto& object : found->objects)
        if (ue::SameWireName(object.name, name, ue::FindNameBytes)) return object.ref;
    return std::unexpected(Error{Error::Kind::Agent, Status::NotFound, 0, {}, "no enum named " + name});
}

// The named classes, structs and enums; with --closure also what their
// fields reference, to the depth, and each exported type's super chain.
void RunLayoutExport(Session& session, const Command& command, std::string_view jsonCommand) {
    shape::LayoutExport out;
    out.path = command.file;
    const bool closure = command.closure.has_value();
    const uint32_t depth = command.closure.value_or(0);
    std::deque<Pending> queue;
    std::vector<std::string> queued;                          // Type names taken, in order.
    struct EnumWanted { std::string name; std::optional<uint32_t> size; Ref ref; };
    std::vector<EnumWanted> enums;
    const auto wantEnum = [&](const std::string& name, std::optional<uint32_t> size, const Ref& ref) {
        const auto known = std::ranges::find(enums, name, &EnumWanted::name);
        if (known == enums.end()) enums.push_back({name, size, ref});
        else if (!known->size) known->size = size;
    };
    const auto want = [&](const std::string& name, const Ref& ref, uint32_t at) {
        if (std::ranges::find(queued, name) != queued.end()) return;
        if (queued.size() == MaxLayoutTypes) { out.capped = true; return; }
        queued.push_back(name);
        queue.push_back({name, ref, at});
    };
    for (const auto& name : command.fields) {
        auto type = ResolveClass(session, name);
        if (type) { want(name, *type, 0); continue; }
        if (!NotFound(type.error())) return CommandError(session, jsonCommand, type.error());
        auto found = ResolveEnum(session, name);
        if (found) { wantEnum(name, std::nullopt, *found); continue; }
        if (!NotFound(found.error())) return CommandError(session, jsonCommand, found.error());
        if (!out.missing) out.missing.emplace();
        out.missing->push_back(name);
    }
    std::string lines;
    while (!queue.empty()) {
        Pending type = std::move(queue.front());
        queue.pop_front();
        if (!type.ref.address) {
            auto resolved = ResolveClass(session, type.name);
            if (!resolved) {
                if (!NotFound(resolved.error())) return CommandError(session, jsonCommand, resolved.error());
                continue; // Named by a field but not found: the field line still names it.
            }
            type.ref = *resolved;
        }
        std::string super;
        auto head = TypeLine(session, type.ref, type.name, super);
        if (!head) return CommandError(session, jsonCommand, head.error());
        auto fields = ObjectFields(session, type.ref);
        if (!fields) return CommandError(session, jsonCommand, fields.error());
        ++((*head)["kind"] == "class" ? out.classes : out.structs);
        lines += head->dump() + "\n";
        if (closure && !super.empty()) want(super, Ref{}, type.depth);
        std::vector<ue::Field> own;
        for (const auto& field : *fields)
            if (field.owner == type.name) own.push_back(field); // Inherited: its own type's lines.
        // Enums have no references of their own: with --closure, every one named is exported.
        const Refer refer = [&](const std::string& name, const Ref& ref, bool isEnum, uint32_t size) {
            if (!closure) return;
            if (isEnum) wantEnum(name, size ? std::optional(size) : std::nullopt, Ref{});
            else if (type.depth < depth) want(name, ref, type.depth + 1);
        };
        for (const auto& field : SortedByOffset(std::move(own))) {
            lines += LayoutFieldLine(field, refer).dump() + "\n";
            ++out.fields;
        }
    }
    for (const auto& wanted : enums) {
        Ref ref = wanted.ref;
        if (!ref.address) {
            auto found = ResolveEnum(session, wanted.name);
            if (!found) {
                if (!NotFound(found.error())) return CommandError(session, jsonCommand, found.error());
                continue;
            }
            ref = *found;
        }
        auto line = EnumLine(session, ref, wanted.name, wanted.size);
        if (!line) return CommandError(session, jsonCommand, line.error());
        lines += line->dump() + "\n";
        ++out.enums;
    }
    std::ofstream file(command.file, std::ios::binary | std::ios::trunc);
    if (!file || !(file << lines)) return JsonError(session, jsonCommand, ErrorKind::File, "cannot write " + command.file);
    Emit(session, jsonCommand, std::move(out));
}

void RunLayoutCheck(Session& session, const Command& command, std::string_view jsonCommand) {
    std::ifstream file(command.file);
    if (!file) return JsonError(session, jsonCommand, ErrorKind::File, "cannot read " + command.file);
    std::vector<LayoutLine> lines;
    size_t number = 0;
    for (std::string text; std::getline(file, text);) {
        ++number;
        if (text.find_first_not_of(" \t\r") == std::string::npos || LayoutTypeLine(text)) continue;
        auto line = ParseLayoutLine(text);
        if (!line) return JsonError(session, jsonCommand, ErrorKind::Validation,
            command.file + " line " + std::to_string(number) + ": " + line.error());
        if (lines.size() == MaxLayoutLines)
            return JsonError(session, jsonCommand, ErrorKind::Validation, "at most " + std::to_string(MaxLayoutLines) + " lines");
        lines.push_back(std::move(*line));
    }
    shape::LayoutCheck out;
    out.path = command.file;
    std::map<std::string, std::optional<std::vector<ue::Field>>> classes; // nullopt: no such class.
    const auto mismatch = [&](const LayoutLine& line, std::string problem, std::optional<shape::LayoutField> found) {
        ++out.mismatched;
        if (out.rows.size() == MaxLayoutRows) { out.capped = true; return; }
        out.rows.push_back({line.owner, line.field, std::move(problem),
            {line.offset, line.size.value_or(0), line.type.value_or("")}, std::move(found)});
    };
    for (const auto& line : lines) {
        auto known = classes.find(line.owner);
        if (known == classes.end()) {
            auto fields = ClassFields(session, line.owner);
            if (!fields && !NotFound(fields.error())) return CommandError(session, jsonCommand, fields.error());
            known = classes.emplace(line.owner, fields ? std::optional(std::move(*fields)) : std::nullopt).first;
        }
        ++out.checked;
        if (!known->second) { mismatch(line, "no class", std::nullopt); continue; }
        const ue::Field* field = nullptr;
        for (const auto& candidate : *known->second)
            if (candidate.name == line.field && candidate.owner == line.owner) { field = &candidate; break; }
        if (!field) { mismatch(line, "no field", std::nullopt); continue; }
        if (auto problem = LayoutProblem(line, field->offset, field->size, field->type); !problem.empty())
            mismatch(line, std::move(problem), FieldOf(*field));
    }
    for (const auto& [name, fields] : classes) out.classes += fields.has_value();
    out.ok = out.mismatched == 0;
    Emit(session, jsonCommand, std::move(out));
}
}

void RunLayout(Session& session, const Command& command, std::string_view jsonCommand) {
    if (!session.client.Supports(ue::op::Fields))
        return JsonError(session, jsonCommand, ErrorKind::Unsupported, "layout needs an agent with fields");
    if (command.target == "export") return RunLayoutExport(session, command, jsonCommand);
    RunLayoutCheck(session, command, jsonCommand);
}
}
