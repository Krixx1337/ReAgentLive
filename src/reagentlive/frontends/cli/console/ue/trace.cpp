// reagentlive CLI console, Unreal pack: trace, a read-only line trace through
// the engine's own query (KismetSystemLibrary.LineTraceSingle), its hit decoded
// by GameplayStatics.BreakHitResult. Both are reached through call, by their
// reflected parameters, so the agent needs nothing new.
#include <algorithm>
#include <cstring>
#include <iterator>
#include <span>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/syntax/text.h"
#include "reagentlive/core/client.h"
#include "reagentlive/ue/client_ue.h"

#include "reagentlive/frontends/cli/console/ue/internal_ue.h"

namespace reagentlive::cli::detail {
namespace {
Error Invalid(std::string message) { return Error{Error::Kind::Agent, Status::BadRequest, 0, {}, std::move(message)}; }

// A default object by its exact name, kept for the session.
Result<Ref> DefaultObject(Session& session, const std::string& name) {
    auto& known = Ue(session).defaultObjects;
    if (const auto found = std::ranges::find(known, name, &ClassRef::name); found != known.end()) return found->ref;
    auto found = ue::Find(session.client, name, {"", 0, true});
    if (!found) return std::unexpected(found.error());
    for (const auto& object : found->objects)
        if (object.name == name) {
            known.push_back({name, object.ref, true});
            return object.ref;
        }
    return std::unexpected(Error{Error::Kind::Agent, Status::NotFound, 0, {}, "no object named " + name});
}

// A function, its parameters and a zeroed block of their size.
struct Prepared {
    std::string name;
    ue::FunctionInfo function;
    ue::Signature signature;
    std::vector<uint8_t> block;
};
Result<Prepared> Prepare(Client& client, const Ref& object, std::string name) {
    auto function = ue::Function(client, object, name);
    if (!function) return std::unexpected(function.error());
    auto signature = ue::Parameters(client, function->address);
    if (!signature) return std::unexpected(signature.error());
    if (signature->size > ue::MaxParams) return std::unexpected(Invalid(name + ": parameter block too large"));
    const uint32_t size = signature->size;
    return Prepared{std::move(name), std::move(*function), std::move(*signature), std::vector<uint8_t>(size)};
}
Result<std::vector<uint8_t>> Run(Client& client, const Ref& object, const Prepared& prepared, uint32_t flags) {
    auto result = ue::Call(client, object, prepared.name, prepared.block, prepared.function.address, flags);
    if (result && result->size() != prepared.block.size())
        return std::unexpected(Invalid(prepared.name + ": incomplete call result"));
    return result;
}

// Parameters in declaration order: the non-return ones, then the return value.
std::vector<const ue::Field*> Ordered(const ue::Signature& signature, bool returnValue) {
    std::vector<const ue::Field*> out;
    for (const auto& parameter : signature.parameters)
        if (((parameter.parameterFlags & ue::Return) != 0) == returnValue) out.push_back(&parameter);
    return out;
}
bool Fits(const ue::Field& field, size_t blockSize) { return field.size && size_t{field.offset} + field.size <= blockSize; }
bool IsVector(const ue::Field& field) { return field.type == "StructProperty" && (field.size == 24 || field.size == 12); }

void PutVector(std::span<uint8_t> bytes, const double (&value)[3]) {
    if (bytes.size() == 24) { memcpy(bytes.data(), value, 24); return; }
    const float narrow[3]{static_cast<float>(value[0]), static_cast<float>(value[1]), static_cast<float>(value[2])};
    memcpy(bytes.data(), narrow, 12);
}
std::vector<double> GetVector(std::span<const uint8_t> bytes) {
    std::vector<double> out(3);
    if (bytes.size() == 24) { memcpy(out.data(), bytes.data(), 24); return out; }
    float narrow[3]{};
    memcpy(narrow, bytes.data(), 12);
    for (int i = 0; i < 3; ++i) out[i] = narrow[i];
    return out;
}
uint64_t GetPointer(std::span<const uint8_t> bytes) {
    uint64_t value = 0;
    memcpy(&value, bytes.data(), std::min<size_t>(bytes.size(), sizeof(value)));
    return value;
}

// An object's world location: a camera manager's view point (it is an actor
// too, whose own location is elsewhere), a scene component's, or an actor's.
Result<std::vector<double>> Location(Client& client, const Ref& object) {
    for (const char* name : {"GetCameraLocation", "K2_GetComponentLocation", "K2_GetActorLocation"}) {
        auto prepared = Prepare(client, object, name);
        if (!prepared) continue;
        const auto result = Ordered(prepared->signature, true);
        if (result.size() != 1 || !IsVector(*result.front()) || !Fits(*result.front(), prepared->block.size()) ||
            !Ordered(prepared->signature, false).empty()) continue;
        auto block = Run(client, object, *prepared, ue::CallPure);
        if (!block) return std::unexpected(block.error());
        return GetVector(std::span(*block).subspan(result.front()->offset, result.front()->size));
    }
    return std::unexpected(Invalid("no location: not an actor, a scene component or a camera manager"));
}
Result<std::vector<double>> SocketLocation(Session& session, const Ref& object, const std::string& socket) {
    Client& client = session.client;
    auto prepared = Prepare(client, object, "SceneComponent:GetSocketLocation");
    if (!prepared) return std::unexpected(Invalid("a socket needs a scene component: " + prepared.error().message));
    const auto inputs = Ordered(prepared->signature, false);
    const auto result = Ordered(prepared->signature, true);
    if (inputs.size() != 1 || inputs.front()->type != "NameProperty" || inputs.front()->size != 8 ||
        !Fits(*inputs.front(), prepared->block.size()) || result.size() != 1 || !IsVector(*result.front()) ||
        !Fits(*result.front(), prepared->block.size()))
        return std::unexpected(Invalid("unexpected GetSocketLocation signature"));
    auto index = NameIndex(session, socket);
    if (!index) return std::unexpected(index.error());
    memcpy(prepared->block.data() + inputs.front()->offset, &*index, sizeof(int32_t));
    auto block = Run(client, object, *prepared, ue::CallPure);
    if (!block) return std::unexpected(block.error());
    return GetVector(std::span(*block).subspan(result.front()->offset, result.front()->size));
}
Result<std::vector<double>> EndPoint(Session& session, const Command::TracePoint& end) {
    if (end.literal) return std::vector<double>{end.point[0], end.point[1], end.point[2]};
    if (!end.socket.empty()) return SocketLocation(session, end.object, end.socket);
    return Location(session.client, end.object);
}

std::string ChannelText(uint8_t channel) {
    return channel == 0 ? "visibility" : channel == 1 ? "camera" : "query " + std::to_string(channel);
}

// LineTraceSingle(WorldContextObject, Start, End, TraceChannel, bTraceComplex,
// ActorsToIgnore, DrawDebugType, OutHit, bIgnoreSelf, TraceColor, TraceHitColor,
// DrawTime) -> bool, checked by type before anything is written.
Result<std::vector<uint8_t>> LineTrace(Session& session, const Ref& world, bool ignoreSelf, const std::vector<double>& from,
                                       const std::vector<double>& to, const Command& command, bool& blocked) {
    Client& client = session.client;
    auto library = DefaultObject(session, "Default__KismetSystemLibrary");
    if (!library) return std::unexpected(library.error());
    auto prepared = Prepare(client, *library, "KismetSystemLibrary:LineTraceSingle");
    if (!prepared) return std::unexpected(prepared.error());
    const auto p = Ordered(prepared->signature, false);
    const auto r = Ordered(prepared->signature, true);
    const auto byteLike = [](const ue::Field* f) { return (f->type == "ByteProperty" || f->type == "EnumProperty") && f->size == 1; };
    if (p.size() != 12 || r.size() != 1 || r[0]->type != "BoolProperty" || p[0]->type != "ObjectProperty" ||
        !IsVector(*p[1]) || !IsVector(*p[2]) || !byteLike(p[3]) || p[4]->type != "BoolProperty" ||
        p[5]->type != "ArrayProperty" || !byteLike(p[6]) || p[7]->type != "StructProperty" || p[8]->type != "BoolProperty" ||
        !std::ranges::all_of(p, [&](const ue::Field* f) { return Fits(*f, prepared->block.size()); }))
        return std::unexpected(Invalid("unexpected LineTraceSingle signature"));
    auto& block = prepared->block;
    memcpy(block.data() + p[0]->offset, &world.address, sizeof(uint64_t));
    PutVector(std::span(block).subspan(p[1]->offset, p[1]->size), {from[0], from[1], from[2]});
    PutVector(std::span(block).subspan(p[2]->offset, p[2]->size), {to[0], to[1], to[2]});
    block[p[3]->offset] = command.traceChannel;
    block[p[4]->offset] = command.traceSimple ? 0 : 1;
    block[p[6]->offset] = 0; // EDrawDebugTrace::None.
    block[p[8]->offset] = ignoreSelf ? 1 : 0;
    auto result = Run(client, *library, *prepared, 0);
    if (!result) return std::unexpected(result.error());
    blocked = (*result)[r[0]->offset] != 0;
    return std::vector<uint8_t>(result->begin() + p[7]->offset, result->begin() + p[7]->offset + p[7]->size);
}

// BreakHitResult(Hit) outputs, by position. UE5: bBlockingHit, bInitialOverlap,
// Time, Distance, Location, ImpactPoint, Normal, ImpactNormal, PhysMat,
// HitActor, HitComponent, HitBoneName, BoneName, HitItem, ElementIndex,
// FaceIndex, TraceStart, TraceEnd. UE4 lacks BoneName and ElementIndex.
struct HitLayout { size_t outputs, overlap, distance, impact, normal, material, actor, component, bone, item, face; };
constexpr HitLayout HitLayouts[]{
    {18, 1, 3, 5, 7, 8, 9, 10, 11, 13, 15},
    {16, 1, 3, 5, 7, 8, 9, 10, 11, 12, 13},
};
Result<void> Decode(Session& session, std::span<const uint8_t> hit, shape::Trace& out) {
    Client& client = session.client;
    auto statics = DefaultObject(session, "Default__GameplayStatics");
    if (!statics) return std::unexpected(statics.error());
    auto prepared = Prepare(client, *statics, "GameplayStatics:BreakHitResult");
    if (!prepared) return std::unexpected(prepared.error());
    std::vector<const ue::Field*> inputs, outputs;
    for (const auto& parameter : prepared->signature.parameters)
        (ue::InputParameter(parameter.parameterFlags) ? inputs : outputs).push_back(&parameter);
    const auto layout = std::ranges::find(HitLayouts, outputs.size(), &HitLayout::outputs);
    if (layout == std::end(HitLayouts) || inputs.size() != 1 || inputs[0]->size != hit.size() ||
        !std::ranges::all_of(outputs, [&](const ue::Field* f) { return Fits(*f, prepared->block.size()); }) ||
        !Fits(*inputs[0], prepared->block.size()) || !IsVector(*outputs[layout->impact]) ||
        !IsVector(*outputs[layout->normal]) || outputs[layout->material]->size != 8 || outputs[layout->actor]->size != 8 ||
        outputs[layout->component]->size != 8 || outputs[layout->bone]->type != "NameProperty" ||
        outputs[layout->item]->size != 4 || outputs[layout->face]->size != 4)
        return std::unexpected(Invalid("unexpected BreakHitResult signature"));
    const HitLayout& at = *layout;
    memcpy(prepared->block.data() + inputs[0]->offset, hit.data(), hit.size());
    auto result = Run(client, *statics, *prepared, ue::CallPure);
    if (!result) return std::unexpected(result.error());
    const auto bytes = [&](size_t i) { return std::span<const uint8_t>(*result).subspan(outputs[i]->offset, outputs[i]->size); };
    const auto number = [&](size_t i) -> double {
        const auto value = bytes(i);
        if (value.size() == 8) { double d; memcpy(&d, value.data(), 8); return d; }
        float f = 0;
        memcpy(&f, value.data(), std::min<size_t>(value.size(), 4));
        return f;
    };
    const auto integer = [&](size_t i) { int32_t v = 0; memcpy(&v, bytes(i).data(), std::min<size_t>(bytes(i).size(), 4)); return v; };
    out.initialOverlap = bytes(at.overlap)[0] != 0;
    out.distance = number(at.distance);
    out.impact = GetVector(bytes(at.impact));
    out.normal = GetVector(bytes(at.normal));
    out.item = integer(at.item);
    out.face = integer(at.face);
    // The material, actor and component, named in one round trip.
    const size_t objects[]{at.material, at.actor, at.component};
    std::vector<Ref> refs;
    for (size_t i : objects) refs.push_back(Ref{static_cast<uintptr_t>(GetPointer(bytes(i))), -1, 0});
    std::vector<Ref> present;
    for (const auto& ref : refs) if (ref.address) present.push_back(ref);
    if (!present.empty()) {
        auto described = ue::DescribeMany(client, present);
        if (!described) return std::unexpected(described.error());
        size_t next = 0;
        std::optional<shape::Object>* slots[]{&out.material, &out.actor, &out.component};
        for (size_t k = 0; k < 3; ++k) {
            if (!refs[k].address) continue;
            const auto& object = (*described)[next++];
            if (object) *slots[k] = MakeObject(*object);
        }
    }
    int32_t bone = 0;
    memcpy(&bone, bytes(at.bone).data(), sizeof(bone));
    if (bone) {
        auto names = ue::Names(client, std::span(&bone, 1));
        if (names && !names->front().empty() && names->front() != "None") out.bone = names->front();
    }
    return {};
}
}

Result<int32_t> NameIndex(Session& session, std::string_view text) {
    auto& known = Ue(session).nameIndices;
    const std::string key(text);
    if (const auto found = known.find(key); found != known.end()) return found->second;
    for (uint32_t from = 0;;) {
        auto found = ue::NameFind(session.client, text, from);
        if (!found) return std::unexpected(found.error());
        // FNames compare without case.
        for (const auto& match : found->names)
            if (text::SameText(match.name, text)) {
                known.emplace(key, match.index);
                return match.index;
            }
        if (!found->next) break;
        from = found->next;
    }
    return std::unexpected(Error{Error::Kind::Agent, Status::NotFound, 0, {}, "no pooled name is '" + key + "'"});
}

void RunTrace(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(ue::op::Call) || !client.Supports(ue::op::Parameters) || !client.Supports(ue::op::DescribeMany))
        return JsonError(session, jsonCommand, "unsupported", "trace needs an agent with call, params and describe");
    shape::Trace out;
    std::vector<double> ends[2];
    for (int i = 0; i < 2; ++i) {
        auto point = EndPoint(session, command.traceEnds[i]);
        if (!point) return CommandError(session, jsonCommand, point.error());
        ends[i] = std::move(*point);
    }
    // The world: the ignored actor (bIgnoreSelf skips it), else an end's object.
    Ref world = command.traceIgnore;
    const bool ignoreSelf = world.address != 0;
    for (const auto& end : command.traceEnds)
        if (!world.address && !end.literal) world = end.object;
    if (!world.address)
        return JsonError(session, jsonCommand, "validation", "two points need --ignore <actor>: the trace runs in its world");
    out.start = ends[0];
    out.end = ends[1];
    out.channel = ChannelText(command.traceChannel);
    out.complex = !command.traceSimple;
    if (ignoreSelf) out.ignored = FullRef(command.traceIgnore);
    bool blocked = false;
    auto hit = LineTrace(session, world, ignoreSelf, ends[0], ends[1], command, blocked);
    if (!hit) return CommandError(session, jsonCommand, hit.error());
    out.hit = blocked;
    if (blocked)
        if (auto decoded = Decode(session, *hit, out); !decoded) return CommandError(session, jsonCommand, decoded.error());
    return Emit(session, jsonCommand, std::move(out));
}
}
