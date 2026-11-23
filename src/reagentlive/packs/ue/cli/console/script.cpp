// reagentlive CLI console: script --decode, a Blueprint function's bytecode as
// statements. The bytes are read through plain memory reads and decoded here
// (values/kismet.h); the operands are named with describe, name and FField
// reads, none of which needs the game thread.
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "reagentlive/packs/ue/cli/values/kismet.h"
#include "reagentlive/packs/ue/engine_layout.h"

#include "reagentlive/packs/ue/cli/console/internal_ue.h"

namespace reagentlive::cli::detail {
namespace {
constexpr auto& Layout = ue::layout::StockUE5;

template<class T> T At(const std::vector<uint8_t>& bytes, int32_t offset) {
    T value{};
    if (offset >= 0 && static_cast<size_t>(offset) + sizeof(T) <= bytes.size()) memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

// A function's bytecode (UStruct::Script, a TArray<uint8>).
Result<std::vector<uint8_t>> ScriptBytes(Client& client, uint64_t function) {
    auto header = client.Read(function + Layout.script, 16);
    if (!header) return std::unexpected(header.error());
    const auto data = At<uint64_t>(*header, 0);
    const auto count = At<int32_t>(*header, 8);
    if (count <= 0 || !data || static_cast<uint32_t>(count) > ue::MaxScriptBytes)
        return std::unexpected(Error{Error::Kind::Agent, Status::NotFound, 0, {}, "no bytecode (a native function, or an unreadable script)"});
    return client.ReadRange(data, static_cast<size_t>(count));
}

// Many reads, MaxBatchReads per request; a failed read is left empty.
Result<std::vector<std::vector<uint8_t>>> ReadMany(Client& client, const std::vector<uint64_t>& addresses, uint64_t length) {
    std::vector<std::vector<uint8_t>> out;
    for (size_t start = 0; start < addresses.size(); start += MaxBatchReads) {
        std::vector<ReadSpan> spans;
        for (size_t i = start; i < std::min(addresses.size(), start + MaxBatchReads); ++i) spans.push_back({addresses[i], length});
        auto batch = client.ReadBatch(spans);
        if (!batch) return std::unexpected(batch.error());
        for (auto& item : batch->items) out.push_back(item.status == Status::Ok ? std::move(item.bytes) : std::vector<uint8_t>{});
    }
    return out;
}

// A property as the decode needs it: its reflected and readable names, its type.
struct PropertyInfo { std::string name, readable, type; };

// Names every operand: objects by describe (functions with their outer),
// properties from their FField (name, class, owner, offset and size; the
// readable name from the session's names file), script names from the pool.
// Everything the statements and lists name.
struct Resolved {
    kismet::Symbols symbols;
    std::map<uint64_t, PropertyInfo> properties;
    std::map<uint64_t, Ref> refs; // Objects by address: their full refs.
};

Result<Resolved> Resolve(Session& session, const kismet::Operands& operands) {
    Resolved resolved;
    auto& symbols = resolved.symbols;
    auto& properties = resolved.properties;
    Client& client = session.client;
    std::vector<int32_t> nameIndices(operands.names.begin(), operands.names.end());

    // Objects, then their outers.
    std::vector<Ref> refs;
    for (const uint64_t address : operands.objects) refs.push_back(Ref{static_cast<uintptr_t>(address), -1, 0});
    std::map<uint64_t, uint64_t> outerOf;
    std::map<uint64_t, std::string> objectNames;
    if (!refs.empty()) {
        auto described = ue::DescribeMany(client, refs);
        if (!described) return std::unexpected(described.error());
        std::vector<Ref> outers;
        for (size_t i = 0; i < refs.size(); ++i) {
            if (!(*described)[i]) continue;
            objectNames[refs[i].address] = (*described)[i]->name;
            resolved.refs[refs[i].address] = (*described)[i]->ref;
            if (const auto outer = (*described)[i]->outer; outer.address) {
                outerOf[refs[i].address] = outer.address;
                outers.push_back(outer);
            }
        }
        std::map<uint64_t, std::string> outerNames;
        if (!outers.empty()) {
            auto named = ue::DescribeMany(client, outers);
            if (!named) return std::unexpected(named.error());
            for (size_t i = 0; i < outers.size(); ++i) if ((*named)[i]) outerNames[outers[i].address] = (*named)[i]->name;
        }
        for (const auto& [address, name] : objectNames) {
            kismet::Object object{name, {}};
            if (const auto outer = outerOf.find(address); outer != outerOf.end()) object.owner = outerNames[outer->second];
            symbols.objects[address] = object;
        }
    }

    // Properties: the FField, then its class's name and its owner's name.
    const std::vector<uint64_t> fields(operands.properties.begin(), operands.properties.end());
    const auto fieldBytes = static_cast<uint64_t>(std::max({Layout.propertyOffset + 4, Layout.boolBase + 4, Layout.fieldName + 8}));
    auto read = ReadMany(client, fields, fieldBytes);
    if (!read) return std::unexpected(read.error());
    std::vector<uint64_t> classes, owners;
    for (const auto& bytes : *read) {
        if (bytes.empty()) continue;
        classes.push_back(At<uint64_t>(bytes, Layout.fieldClass));
        if (const auto owner = At<uint64_t>(bytes, Layout.fieldOwner); owner & 1) owners.push_back(owner & ~uint64_t{1});
        nameIndices.push_back(At<int32_t>(bytes, Layout.fieldName));
    }
    std::ranges::sort(classes);
    classes.erase(std::unique(classes.begin(), classes.end()), classes.end());
    std::vector<uint64_t> classNameAt;
    for (const uint64_t type : classes) classNameAt.push_back(type + Layout.fieldClassName);
    auto classRead = ReadMany(client, classNameAt, 8);
    if (!classRead) return std::unexpected(classRead.error());
    std::map<uint64_t, int32_t> classIndex;
    for (size_t i = 0; i < classes.size(); ++i) {
        if ((*classRead)[i].empty()) continue;
        classIndex[classes[i]] = At<int32_t>((*classRead)[i], 0);
        nameIndices.push_back(classIndex[classes[i]]);
    }
    std::ranges::sort(owners);
    owners.erase(std::unique(owners.begin(), owners.end()), owners.end());
    std::vector<Ref> ownerRefs;
    for (const uint64_t owner : owners) if (!objectNames.contains(owner)) ownerRefs.push_back(Ref{static_cast<uintptr_t>(owner), -1, 0});
    if (!ownerRefs.empty()) {
        auto named = ue::DescribeMany(client, ownerRefs);
        if (!named) return std::unexpected(named.error());
        for (size_t i = 0; i < ownerRefs.size(); ++i) if ((*named)[i]) objectNames[ownerRefs[i].address] = (*named)[i]->name;
    }

    // Every name in one pass.
    std::ranges::sort(nameIndices);
    nameIndices.erase(std::unique(nameIndices.begin(), nameIndices.end()), nameIndices.end());
    std::erase_if(nameIndices, [](int32_t index) { return index <= 0; });
    std::map<int32_t, std::string> names;
    if (!nameIndices.empty()) {
        auto decoded = ue::Names(client, nameIndices);
        if (!decoded) return std::unexpected(decoded.error());
        for (size_t i = 0; i < nameIndices.size(); ++i) names[nameIndices[i]] = (*decoded)[i];
    }
    for (const int32_t index : operands.names) if (names.contains(index)) symbols.names[index] = names[index];

    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& bytes = (*read)[i];
        if (bytes.empty()) continue;
        const auto nameIndex = At<int32_t>(bytes, Layout.fieldName);
        const auto number = At<uint32_t>(bytes, Layout.fieldName + Layout.nameNumber);
        if (!names.contains(nameIndex) || names[nameIndex].empty()) continue;
        PropertyInfo info;
        info.name = NameText(names[nameIndex], number);
        const auto type = classIndex.find(At<uint64_t>(bytes, Layout.fieldClass));
        if (type != classIndex.end()) info.type = names[type->second];
        const auto owner = At<uint64_t>(bytes, Layout.fieldOwner);
        const std::string ownerName = owner & 1 ? objectNames[owner & ~uint64_t{1}] : std::string{};
        const auto size = static_cast<uint32_t>(std::max(0, At<int32_t>(bytes, Layout.elementSize) * At<int32_t>(bytes, Layout.arrayDim)));
        const uint8_t mask = info.type == "BoolProperty" ? At<uint8_t>(bytes, Layout.boolBase + 3) : 0;
        const auto readable = session.names.Find(ownerName, static_cast<uint32_t>(At<int32_t>(bytes, Layout.propertyOffset)), size, mask);
        info.readable = readable.empty() ? info.name : std::string(readable);
        symbols.properties[fields[i]] = {info.readable, info.type};
        properties[fields[i]] = std::move(info);
    }
    return resolved;
}

// The names, functions and properties lists from the decoded operands, in
// order of first use.
void Lists(const kismet::Expr& e, const Resolved& resolved, shape::Script& out) {
    const auto& symbols = resolved.symbols;
    const auto& properties = resolved.properties;
    using namespace kismet::ex;
    const auto counted = [](auto& list, auto same) {
        for (auto& row : list) if (same(row)) { ++row.count; return true; }
        return false;
    };
    const auto property = [&](uint64_t pointer) {
        if (!pointer || counted(out.properties, [&](const shape::ScriptProperty& row) { return row.address.value == pointer; })) return;
        shape::ScriptProperty row{e.offset, 1, pointer, "", std::nullopt};
        if (const auto found = properties.find(pointer); found != properties.end()) {
            row.property = (found->second.type.empty() ? "" : found->second.type + " ") + found->second.readable;
            if (found->second.readable != found->second.name) row.raw = found->second.name;
        } else {
            row.property = std::format("prop@0x{:x}", pointer);
        }
        out.properties.push_back(std::move(row));
    };
    const auto name = [&](const kismet::ScriptName& n) {
        if (counted(out.names, [&](const shape::ScriptName& row) { return row.index == n.index; })) return;
        const auto found = symbols.names.find(n.index);
        out.names.push_back({e.offset, 1, n.index, found != symbols.names.end() ? found->second : std::string{}});
    };
    switch (e.op) {
    case LocalVariable: case InstanceVariable: case DefaultVariable: case LocalOutVariable: case ClassSparseDataVariable:
    case PropertyConst: case LetValueOnPersistentFrame: case StructMemberContext: case ArrayConst: case SetConst:
    case BitFieldConst:
    case Let: case Context: case ContextFailSilent: case ClassContext:
        property(e.pointer); break;
    case MapConst: property(e.pointer); property(e.pointer2); break;
    case FinalFunction: case LocalFinalFunction: case CallMath: case CallMulticastDelegate:
        if (const auto ref = resolved.refs.find(e.pointer); ref != resolved.refs.end()) {
            const RefId id = FullRef(ref->second);
            if (counted(out.functions, [&](const shape::ScriptFunction& row) { return row.ref.text == id.text; })) break;
            const auto& found = symbols.objects.at(e.pointer);
            out.functions.push_back({e.offset, 1, id, found.owner.empty() ? found.name : found.owner + ":" + found.name});
        }
        break;
    case NameConst: case VirtualFunction: case LocalVirtualFunction: case InstanceDelegate: case BindDelegate:
        name(e.name); break;
    default: break;
    }
    for (const auto& child : e.children) Lists(child, resolved, out);
}

// Where each event enters the uber graph: the class's other functions whose
// bytecode calls it with an integer constant.
Result<std::vector<shape::ScriptEvent>> UbergraphEntries(Session& session, const Ref& object, const ue::FunctionInfo& graph) {
    Client& client = session.client;
    std::vector<shape::ScriptEvent> events;
    const std::string prefix = graph.owner + ":";
    for (uint32_t from = 0;;) {
        auto page = ue::Functions(client, object, "", from);
        if (!page) return std::unexpected(page.error());
        for (const auto& name : page->names) {
            if (!name.starts_with(prefix) || name == prefix + graph.name) continue;
            auto function = ue::Function(client, object, name);
            if (!function) continue;
            auto bytes = ScriptBytes(client, function->address);
            if (!bytes) continue;
            const auto decoded = kismet::Decode(*bytes);
            std::vector<uint32_t> offsets;
            for (const auto& statement : decoded.statements) kismet::Entries(statement, graph.address, offsets);
            for (const uint32_t offset : offsets) events.push_back({offset, function->name});
        }
        if (!page->next) break;
        from = page->next;
    }
    std::ranges::sort(events, {}, [](const shape::ScriptEvent& e) { return e.offset.value; });
    return events;
}
}

void RunScriptDecode(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    const Ref object = ue::GetRef(command.request);
    auto function = ue::Function(client, object, command.request.text);
    if (!function) return CommandError(session, jsonCommand, function.error());
    auto bytes = ScriptBytes(client, function->address);
    if (!bytes) return CommandError(session, jsonCommand, bytes.error());
    const auto decoded = kismet::Decode(*bytes, command.entry.value_or(0), command.entry.has_value());

    kismet::Operands operands;
    for (const auto& statement : decoded.statements) kismet::Collect(statement, operands);
    auto resolved = Resolve(session, operands);
    if (!resolved) return CommandError(session, jsonCommand, resolved.error());

    shape::Script out;
    out.ref = ReusableRef(object);
    out.function = function->owner + ":" + function->name;
    out.size = static_cast<uint32_t>(bytes->size());
    out.statements.emplace();
    for (const auto& statement : decoded.statements) {
        out.statements->push_back({statement.offset, std::string(kismet::OpName(statement.op)), kismet::Render(statement, resolved->symbols)});
        Lists(statement, *resolved, out);
    }
    if (!decoded.stopped.empty()) out.stopped = decoded.stopped;
    if (function->name.starts_with("ExecuteUbergraph")) {
        auto entries = UbergraphEntries(session, object, *function);
        if (!entries) return CommandError(session, jsonCommand, entries.error());
        out.events = std::move(*entries);
    }
    Emit(session, jsonCommand, std::move(out));
}
}
