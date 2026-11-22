// reagentlive console tests: a fake engine. One real agent per process,
// ticking on its own thread, whose Unreal pack ops answer from a small
// synthetic world in this process's memory: types with reflected fields,
// objects with bytes, an FName pool. Core reads are the real ones, so the
// console reads arrays, sets and maps through memory as it does in a game.
// RunLines runs the real console against it and returns its JSON lines.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>
#include "reagentlive/core/agent.h"
#include "reagentlive/core/client.h"
#include "reagentlive/packs/ue/protocol_ue.h"
#include "reagentlive/frontends/cli/console/console.h"
#include "doctest.h"

namespace fake {
using reagentlive::Ref;
using reagentlive::Request;
using reagentlive::Response;
using reagentlive::Status;
using reagentlive::Progress;
using reagentlive::Host;
namespace ue = reagentlive::ue;
using Json = nlohmann::json;

constexpr uint32_t Layout = reagentlive::LayoutCore + ue::LayoutPart;

// A reflected type (a class or a struct) and an object (an instance, or the
// type's own object). Object refs carry the slot and a serial; a slot whose
// serial moved on is a gone object.
struct Type {
    std::string name;
    uint32_t size = 0;
    std::vector<ue::FieldEntry> fields;
    std::string parent;     // A class's parent class name, for class filters.
    int32_t slot = -1;      // Its own object.
};
struct Object {
    Ref ref{};
    std::string name, className;
    int32_t type = -1;      // Index into World::types: an instance's class.
    int32_t ownType = -1;   // A type's own object: which type it is.
    Ref outer{};
};

struct World {
    std::deque<std::vector<uint8_t>> memory;    // Stable addresses.
    std::deque<Type> types;
    std::vector<Object> objects;                // By slot.
    std::vector<std::string> names{"None"};     // The FName pool, by comparison index.

    uint8_t* Alloc(size_t size) {
        memory.emplace_back(std::max<size_t>(size, 16), uint8_t{0});
        return memory.back().data();
    }
    int32_t Name(const std::string& text) {
        if (const auto at = std::ranges::find(names, text); at != names.end()) return static_cast<int32_t>(at - names.begin());
        names.push_back(text);
        return static_cast<int32_t>(names.size() - 1);
    }
    Object& Add(std::string name, std::string className, size_t size, Ref outer = {}) {
        Object object;
        object.ref = {reinterpret_cast<uintptr_t>(Alloc(size)), static_cast<int32_t>(objects.size()), 1};
        object.name = std::move(name);
        object.className = std::move(className);
        object.outer = outer;
        objects.push_back(std::move(object));
        return objects.back();
    }
    // A type, and its own object (a ScriptStruct or a Class).
    Type& AddType(std::string name, uint32_t size, bool isStruct, std::string parent = {}) {
        types.push_back({name, size, {}, std::move(parent), static_cast<int32_t>(objects.size())});
        Object& own = Add(name, isStruct ? "ScriptStruct" : "Class", 64);
        own.ownType = static_cast<int32_t>(types.size() - 1);
        return types.back();
    }
    Object& Instance(std::string name, const Type& type, Ref outer = {}) {
        int32_t index = 0; // A deque is not contiguous: find the type, do not subtract pointers.
        while (&types[static_cast<size_t>(index)] != &type) ++index;
        Object& object = Add(std::move(name), type.name, type.size, outer);
        object.type = index;
        return object;
    }
    uint8_t* Bytes(const Object& object) { return reinterpret_cast<uint8_t*>(object.ref.address); }
    Ref TypeRef(const Type& type) const { return objects[static_cast<size_t>(type.slot)].ref; }

    // The object a ref names: full, address-only (index -1) or weak (no address); null when gone.
    const Object* Resolve(const Ref& ref) const {
        if (!ref.address) {
            if (ref.index < 0 || static_cast<size_t>(ref.index) >= objects.size()) return nullptr;
            const auto& object = objects[static_cast<size_t>(ref.index)];
            return object.ref.serial == ref.serial ? &object : nullptr;
        }
        for (const auto& object : objects)
            if (object.ref.address == ref.address)
                return ref.index < 0 || (object.ref.index == ref.index && (!ref.serial || object.ref.serial == ref.serial))
                    ? &object : nullptr;
        return nullptr;
    }
    bool ClassMatches(const Object& object, std::string_view wanted) const {
        if (wanted.empty()) return true;
        if (wanted == "Struct") return object.ownType >= 0;
        for (std::string name = object.className; !name.empty();) {
            if (_stricmp(name.c_str(), std::string(wanted).c_str()) == 0) return true;
            const auto type = std::ranges::find(types, name, &Type::name);
            name = type == types.end() ? std::string() : type->parent;
        }
        return false;
    }
    bool OuterMatches(const Object& object, const Request& request) const {
        const auto text = ue::detail::FilterOuter(request);
        if (!request.address && text.empty()) return true;
        const Object* outer = &object;
        for (uint32_t step = 0; step < ue::MaxOuterSteps; ++step) {
            outer = outer->outer.address ? Resolve(outer->outer) : nullptr;
            if (!outer) return false;
            if (request.address && outer->ref.address == request.address) return true;
            if (!text.empty() && ue::NameMatches(outer->name, text)) return true;
        }
        return false;
    }
    ue::FindEntry Entry(const Object& object) const {
        ue::FindEntry entry{};
        entry.object = object.ref;
        entry.outer = object.outer;
        reagentlive::CopyText(entry.name, object.name);
        reagentlive::CopyText(entry.className, object.className);
        return entry;
    }
};
inline World& TheWorld() {
    static World world;
    return world;
}

// A field row, as the agent's reflection answers it.
inline ue::FieldEntry Field(std::string_view name, std::string_view type, uint32_t offset, uint32_t size) {
    ue::FieldEntry field{};
    field.offset = offset;
    field.size = size;
    reagentlive::CopyText(field.name, name);
    reagentlive::CopyText(field.type, type);
    reagentlive::CopyText(field.owner, "Fake");
    if (type == "BoolProperty") field.boolMask = 0xff;
    return field;
}
inline ue::FieldEntry StructField(std::string_view name, uint32_t offset, const Type& type) {
    auto field = Field(name, "StructProperty", offset, type.size);
    field.structType = TheWorld().TypeRef(type);
    reagentlive::CopyText(field.structName, type.name);
    return field;
}
// An array: element type and size; a struct element names its type.
inline ue::FieldEntry ArrayField(std::string_view name, uint32_t offset, std::string_view element, uint32_t elementSize,
                                 const Type* type = nullptr) {
    auto field = Field(name, "ArrayProperty", offset, 16);
    field.elementSize = elementSize;
    reagentlive::CopyText(field.elementType, element);
    if (type) {
        field.structType = TheWorld().TypeRef(*type);
        reagentlive::CopyText(field.structName, type->name);
    }
    return field;
}
// A map: key and value as fields, the slot's stride and the value's offset in it.
inline ue::FieldEntry MapField(std::string_view name, uint32_t offset, const ue::FieldEntry& key, const ue::FieldEntry& value,
                               uint32_t stride, uint32_t valueOffset) {
    auto field = Field(name, "MapProperty", offset, 80);
    field.elementSize = key.size;
    memcpy(field.elementType, key.type, sizeof(field.elementType));
    field.structType = key.structType;
    memcpy(field.structName, key.structName, sizeof(field.structName));
    field.stride = stride;
    field.valueOffset = valueOffset;
    field.valueSize = value.size;
    memcpy(field.valueType, value.type, sizeof(field.valueType));
    field.valueStructType = value.structType;
    memcpy(field.valueStructName, value.structName, sizeof(field.valueStructName));
    return field;
}

// Writes a TArray header and its elements (allocated in the world).
inline void SetArray(uint8_t* at, std::span<const uint8_t> elements, uint32_t count) {
    uint8_t* data = count ? TheWorld().Alloc(elements.size()) : nullptr;
    if (count) memcpy(data, elements.data(), elements.size());
    const uint64_t pointer = reinterpret_cast<uint64_t>(data);
    const int32_t sizes[2] = {static_cast<int32_t>(count), static_cast<int32_t>(count)};
    memcpy(at, &pointer, 8);
    memcpy(at + 8, sizes, 8);
}
// Writes a set or map header over `slots` elements of `stride` bytes, every one allocated.
inline void SetSparse(uint8_t* at, std::span<const uint8_t> elements, uint32_t slots, uint32_t stride) {
    ue::SparseHeader header{};
    uint8_t* data = slots ? TheWorld().Alloc(size_t{slots} * stride) : nullptr;
    if (slots) memcpy(data, elements.data(), elements.size());
    header.data = reinterpret_cast<uint64_t>(data);
    header.num = header.max = header.numBits = header.maxBits = static_cast<int32_t>(slots);
    for (uint32_t slot = 0; slot < slots; ++slot) header.inlineBits[slot / 32] |= 1u << (slot % 32);
    header.firstFree = -1;
    memcpy(at, &header, sizeof(header));
}

// --- The pack ops, from the world ---
inline Progress Find(const Request& request, Response& response, bool, const Host&) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    auto& world = TheWorld();
    if (request.address) {
        Ref outer = ue::detail::FilterRef(request);
        if (!world.Resolve(outer)) { reagentlive::Refuse(response, Status::NotFound, "outer object gone or not a valid object"); return Progress::Done; }
    }
    const std::string_view text(request.text);
    const std::string_view wanted(request.length ? reinterpret_cast<const char*>(request.data) : "");
    const bool exact = request.flags & ue::FindExact;
    auto* entries = reinterpret_cast<ue::FindEntry*>(response.data);
    uint32_t count = 0, total = 0, next = 0;
    for (size_t slot = request.args[1]; slot < world.objects.size(); ++slot) {
        const auto& object = world.objects[slot];
        if (!text.empty() && (exact ? object.name != text : !ue::NameMatches(object.name, text))) continue;
        if (!world.ClassMatches(object, wanted) || !world.OuterMatches(object, request)) continue;
        ++total;
        if (count == ue::MaxFindResults) { if (!next) next = static_cast<uint32_t>(slot); continue; }
        entries[count++] = world.Entry(object);
        if (exact) break;
    }
    response.length = count * static_cast<uint32_t>(sizeof(ue::FindEntry));
    response.detail = ue::FindDetail(exact ? 0 : total, next);
    return Progress::Done;
}

inline Progress Fields(const Request& request, Response& response, bool, const Host&) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    auto& world = TheWorld();
    const Object* object = world.Resolve(ue::GetRef(request));
    if (!object) { reagentlive::Refuse(response, Status::NotFound, "object gone or not a valid object"); return Progress::Done; }
    const int32_t index = object->ownType >= 0 ? object->ownType : object->type;
    if (index < 0) { reagentlive::Refuse(response, Status::NotFound, "no type"); return Progress::Done; }
    const auto& fields = world.types[static_cast<size_t>(index)].fields;
    const size_t first = request.length;
    const size_t count = first < fields.size() ? std::min(fields.size() - first, ue::MaxFieldsPerPage) : 0;
    if (count) memcpy(response.data, fields.data() + first, count * sizeof(ue::FieldEntry));
    response.length = static_cast<uint32_t>(count * sizeof(ue::FieldEntry));
    response.detail = ue::FindDetail(static_cast<uint32_t>(fields.size()), static_cast<uint32_t>(world.types[static_cast<size_t>(index)].slot));
    return Progress::Done;
}

inline Progress ReadObject(const Request& request, Response& response, bool, const Host&) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    auto& world = TheWorld();
    const Object* object = world.Resolve(ue::GetRef(request));
    if (!object) { reagentlive::Refuse(response, Status::NotFound, "object gone or not a valid object"); return Progress::Done; }
    memcpy(response.data, reinterpret_cast<const uint8_t*>(object->ref.address) + request.args[1], request.length);
    response.length = request.length;
    return Progress::Done;
}

inline Progress Names(const Request& request, Response& response, bool, const Host&) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const auto& names = TheWorld().names;
    uint32_t at = 0, count = 0;
    for (uint32_t i = 0; i < request.length / 4; ++i) {
        int32_t index = 0;
        memcpy(&index, request.data + i * 4, 4);
        const std::string name = index >= 0 && static_cast<size_t>(index) < names.size() ? names[static_cast<size_t>(index)] : "";
        if (at + name.size() + 1 > reagentlive::MaxRead) break;
        memcpy(response.data + at, name.c_str(), name.size() + 1);
        at += static_cast<uint32_t>(name.size() + 1);
        ++count;
    }
    response.length = at;
    response.detail = count;
    return Progress::Done;
}

inline Progress Describe(const Request& request, Response& response, bool, const Host&) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    auto& world = TheWorld();
    const Object* object = world.Resolve(ue::GetRef(request));
    if (!object) { reagentlive::Refuse(response, Status::NotFound, "object gone or not a valid object"); return Progress::Done; }
    const auto entry = world.Entry(*object);
    memcpy(response.data, &entry, sizeof(entry));
    response.length = sizeof(entry);
    response.detail = object->type >= 0 ? world.types[static_cast<size_t>(object->type)].size : 0;
    return Progress::Done;
}

inline Progress DescribeMany(const Request& request, Response& response, bool, const Host&) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    auto& world = TheWorld();
    const size_t count = request.length / sizeof(Ref);
    uint64_t found = 0;
    for (size_t i = 0; i < count; ++i) {
        Ref ref{};
        memcpy(&ref, request.data + i * sizeof(ref), sizeof(ref));
        ue::FindEntry entry{};
        if (const Object* object = world.Resolve(ref)) { entry = world.Entry(*object); ++found; }
        memcpy(response.data + i * sizeof(entry), &entry, sizeof(entry));
    }
    response.length = static_cast<uint32_t>(count * sizeof(ue::FindEntry));
    response.detail = found;
    return Progress::Done;
}

inline Progress PreviewArray(const Request& request, Response& response, bool, const Host&) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const Object* object = TheWorld().Resolve(ue::GetRef(request));
    if (!object) { reagentlive::Refuse(response, Status::NotFound, "object gone"); return Progress::Done; }
    ue::ArrayHeader header{};
    memcpy(&header, reinterpret_cast<const uint8_t*>(object->ref.address) + request.args[1], sizeof(header));
    const uint32_t bytes = ue::ArrayPreviewBytes(header, request.length);
    if (bytes) memcpy(response.data, reinterpret_cast<const void*>(header.data), bytes);
    response.length = bytes;
    response.detail = static_cast<uint32_t>(header.count);
    return Progress::Done;
}

inline Progress Entries(const Request& request, Response& response, bool, const Host&) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const Object* object = TheWorld().Resolve(ue::GetRef(request));
    if (!object) { reagentlive::Refuse(response, Status::NotFound, "object gone"); return Progress::Done; }
    const uint32_t stride = request.length, first = ue::EntriesFirst(request.args[1]);
    const auto* at = reinterpret_cast<const uint8_t*>(object->ref.address) + ue::EntriesOffset(request.args[1]);
    const uint32_t fit = static_cast<uint32_t>(reagentlive::MaxRead / stride);
    response.length = 0;
    if (static_cast<ue::ContainerKind>(request.flags) == ue::ContainerKind::Array) {
        ue::ArrayHeader header{};
        memcpy(&header, at, sizeof(header));
        const auto count = static_cast<uint32_t>(header.count);
        const uint32_t shown = first < count ? std::min(count - first, fit) : 0;
        if (shown) memcpy(response.data, reinterpret_cast<const uint8_t*>(header.data) + size_t{first} * stride, size_t{shown} * stride);
        response.length = shown * stride;
        response.detail = ue::FindDetail(count, first + shown < count ? first + shown : 0);
        return Progress::Done;
    }
    ue::SparseHeader header{};
    memcpy(&header, at, sizeof(header));
    const auto num = static_cast<uint32_t>(header.num);
    const auto* data = reinterpret_cast<const uint8_t*>(header.data);
    uint32_t occupied = 0;
    for (uint32_t slot = 0; slot < num; ++slot) occupied += ue::SlotAllocated(header.inlineBits, slot);
    const uint32_t next = ue::WalkSlots(header.inlineBits, num, first, fit, [&](uint32_t slot) {
        memcpy(response.data + response.length, data + size_t{slot} * stride, stride);
        response.length += stride;
    });
    response.detail = ue::FindDetail(occupied, next);
    return Progress::Done;
}

inline Progress Each(const Request& request, Response& response, bool, const Host&) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    auto& world = TheWorld();
    if (request.text[0]) { reagentlive::Refuse(response, Status::Refused, "the fake engine calls nothing"); return Progress::Done; }
    if (request.address && !world.Resolve(ue::detail::FilterRef(request))) {
        reagentlive::Refuse(response, Status::NotFound, "outer object gone or not a valid object");
        return Progress::Done;
    }
    const std::string_view wanted(reinterpret_cast<const char*>(request.data));
    const uint32_t offset = ue::EachOffset(request.args[0]), size = ue::EachSize(request.args[0]);
    const uint32_t stride = ue::EachStride(size);
    std::vector<const Object*> rows;
    for (const auto& object : world.objects)
        if (object.type >= 0 && world.ClassMatches(object, wanted) && world.OuterMatches(object, request)) rows.push_back(&object);
    const auto from = static_cast<uint32_t>(request.args[1]);
    const uint32_t perPage = static_cast<uint32_t>(reagentlive::MaxRead / stride);
    const uint32_t total = static_cast<uint32_t>(rows.size());
    const uint32_t count = from < total ? std::min(perPage, total - from) : 0;
    memset(response.data, 0, size_t{count} * stride);
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t* row = response.data + size_t{i} * stride;
        memcpy(row, &rows[from + i]->ref, sizeof(Ref));
        memcpy(row + sizeof(Ref), reinterpret_cast<const uint8_t*>(rows[from + i]->ref.address) + offset, size);
    }
    response.length = count * stride;
    response.detail = ue::FindDetail(total, from + count < total ? from + count : 0);
    return Progress::Done;
}

// No functions in the fake world: each tries a lone column as one first.
inline Progress Function(const Request& request, Response& response, bool, const Host&) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    reagentlive::Refuse(response, Status::NotFound, "no such function");
    return Progress::Done;
}

inline bool Readable(const void* pointer, size_t bytes) {
    auto cursor = reinterpret_cast<uintptr_t>(pointer);
    const uintptr_t end = cursor + bytes;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        cursor = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    }
    return true;
}

constexpr reagentlive::Command Commands[] = {
    {reagentlive::op::Read, "read", &reagentlive::Read},
    {reagentlive::op::ReadBatch, "read-batch", &reagentlive::ReadBatch},
    {ue::op::Find, "find", &Find},
    {ue::op::Fields, "fields", &Fields},
    {ue::op::ReadObject, "read object", &ReadObject},
    {ue::op::Describe, "describe", &Describe},
    {ue::op::PreviewArray, "preview array", &PreviewArray},
    {ue::op::Names, "names", &Names},
    {ue::op::Each, "each", &Each},
    {ue::op::DescribeMany, "describe many", &DescribeMany},
    {ue::op::Entries, "entries", &Entries},
    {ue::op::Function, "function", &Function},
};

inline std::wstring SectionPrefix() { return L"Local\\reagentlive-fake-"; }

// The agent, ticking every millisecond on its own thread.
struct Agent {
    std::wstring name = SectionPrefix() + std::to_wstring(GetCurrentProcessId());
    Host host{name.c_str(), Layout, &Readable, nullptr, nullptr, reagentlive::HostThread::Engine};
    std::atomic<bool> stop{false};
    std::thread thread;
    Agent() {
        thread = std::thread([this] {
            while (!stop) {
                reagentlive::Tick(host, Commands);
                Sleep(1);
            }
        });
    }
    ~Agent() { stop = true; thread.join(); }
};
// The agent, once its section is open (its first tick opens it).
inline Agent& TheAgent() {
    static Agent agent;
    static const bool open = [] {
        for (int attempt = 0; attempt < 2000; ++attempt) {
            if (reagentlive::Client::Attach(agent.name, Layout)) return true;
            Sleep(1);
        }
        return false;
    }();
    REQUIRE(open);
    return agent;
}

// One console run over `lines` ("get ...; each ..."): its JSON lines.
inline std::vector<Json> RunLines(const std::string& lines) {
    TheAgent();
    static const std::wstring prefix = SectionPrefix(); // The config keeps a view of it.
    static const reagentlive::cli::ConsoleConfig config{prefix, L"", Layout};
    const auto out = std::filesystem::temp_directory_path() /
        (L"reagentlive-fake-" + std::to_wstring(GetCurrentProcessId()) + L".out");
    std::wstring wide(lines.begin(), lines.end());
    std::wstring pid = std::to_wstring(GetCurrentProcessId());
    std::wstring program = L"console", noUsage = L"--no-usage", dashC = L"-c";
    wchar_t* argv[] = {program.data(), pid.data(), noUsage.data(), dashC.data(), wide.data()};
    std::fflush(stdout);
    const int saved = _dup(_fileno(stdout));
    FILE* file = nullptr;
    _wfopen_s(&file, out.c_str(), L"wb");
    REQUIRE(file);
    _dup2(_fileno(file), _fileno(stdout));
    reagentlive::cli::RunConsole(config, 5, argv);
    std::fflush(stdout);
    _dup2(saved, _fileno(stdout));
    _close(saved);
    std::fclose(file);
    std::vector<Json> records;
    std::ifstream in(out);
    for (std::string line; std::getline(in, line);)
        if (!line.empty() && line.front() == '{') records.push_back(Json::parse(line));
    return records;
}
inline Json Result(const std::string& line) {
    auto records = RunLines(line);
    REQUIRE(records.size() == 1);
    INFO(records.front().dump());
    REQUIRE(records.front().value("ok", false));
    return records.front()["result"];
}
}
