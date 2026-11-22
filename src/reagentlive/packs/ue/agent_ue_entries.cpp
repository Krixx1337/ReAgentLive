// reagentlive Unreal pack: container handlers over the host's SDK: array
// previews and one page of an array, set or map field's elements.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include "reagentlive/packs/ue/agent_ue.h"
#include "reagentlive/packs/ue/agent_ue_internal.h"

namespace reagentlive::ue {
namespace {
Progress PreviewArrayBody(const Request& request, Response& response, bool first, const Host& host) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response, Access::Read) || !LayoutReady(GroupClass, host, response))
        return Progress::Done;
    const auto* object = ResolveRequestObject(request, pack);
    if (!object || IsA(object, cast::Struct)) {
        Refuse(response, Status::NotFound, "array preview requires a live instance");
        return Progress::Done;
    }
    const Ref before = Identify(object, pack);
    Request read{};
    read.op = reagentlive::op::Read;
    read.address = request.address + request.args[1];
    read.length = sizeof(ArrayHeader);
    reagentlive::Read(read, response, first, host);
    if (response.status != Status::Ok) return Progress::Done;
    ArrayHeader header{};
    memcpy(&header, response.data, sizeof(header));
    response.length = 0;
    if (!header.count && header.capacity >= 0) return Progress::Done;
    read.address = header.data;
    read.length = ArrayPreviewBytes(header, request.length);
    if (!read.length) {
        Refuse(response, Status::Refused, "invalid array header");
        return Progress::Done;
    }
    reagentlive::Read(read, response, first, host);
    if (response.status == Status::Ok && StillSame(before, pack, response)) response.detail = static_cast<uint32_t>(header.count);
    return Progress::Done;
}

Progress EntriesBody(const Request& request, Response& response, bool, const Host& host) {
    if ((response.status = ue::Check(request)) != Status::Ok) return Progress::Done;
    const Context& pack = Pack(host);
    if (!PackReady(pack, host, response, Access::Read) || !LayoutReady(GroupClass, host, response))
        return Progress::Done;
    const auto* object = ResolveRequestObject(request, pack);
    if (!object || IsA(object, cast::Struct)) {
        Refuse(response, Status::NotFound, "entries require a live instance");
        return Progress::Done;
    }
    const Ref before = Identify(object, pack);
    const uint32_t stride = request.length, first = EntriesFirst(request.args[1]);
    const uint32_t fit = static_cast<uint32_t>(MaxRead / stride);
    const auto* at = reinterpret_cast<const uint8_t*>(request.address) + EntriesOffset(request.args[1]);
    const auto kind = static_cast<ContainerKind>(request.flags);
    response.length = 0;
    if (kind == ContainerKind::Array) {
        ArrayHeader header{};
        if (!host.readable(at, sizeof(header)) || !GuardedCopy(&header, at, sizeof(header))) {
            Refuse(response, Status::NotReadable, "container header unreadable");
            return Progress::Done;
        }
        if (header.count < 0 || header.capacity < header.count || (header.count && !header.data) ||
            static_cast<uint64_t>(header.count) * stride > UINT32_MAX) {
            Refuse(response, Status::Refused, "invalid array header");
            return Progress::Done;
        }
        const uint32_t count = static_cast<uint32_t>(header.count);
        const uint32_t shown = first < count ? std::min(count - first, fit) : 0;
        const auto* data = reinterpret_cast<const uint8_t*>(header.data) + static_cast<uint64_t>(first) * stride;
        // Checked, then copied under the fault guard: another thread can free
        // the storage between the two.
        if (shown && (!host.readable(data, static_cast<size_t>(shown) * stride) ||
                      !GuardedCopy(response.data, data, static_cast<size_t>(shown) * stride))) {
            Refuse(response, Status::NotReadable, "array elements unreadable");
            return Progress::Done;
        }
        response.length = shown * stride;
        response.detail = FindDetail(count, first + shown < count ? first + shown : 0);
        StillSame(before, pack, response);
        return Progress::Done;
    }
    SparseHeader header{};
    if (!host.readable(at, sizeof(header)) || !GuardedCopy(&header, at, sizeof(header))) {
        Refuse(response, Status::NotReadable, "container header unreadable");
        return Progress::Done;
    }
    if (!SparseHeaderValid(header)) { Refuse(response, Status::Refused, "invalid set header"); return Progress::Done; }
    const uint32_t num = static_cast<uint32_t>(header.num), words = (num + 31) / 32;
    // The bits are walked from a guarded copy (the header's own for inline
    // bits), never from live memory. A failed allocation is answered by the
    // agent's dispatch as a refusal.
    std::vector<uint32_t> bitsCopy;
    const uint32_t* bits = header.inlineBits;
    if (header.secondaryBits) {
        const auto* live = reinterpret_cast<const uint32_t*>(header.secondaryBits);
        bitsCopy.resize(words);
        if (words && (!host.readable(live, words * sizeof(uint32_t)) ||
                      !GuardedCopy(bitsCopy.data(), live, words * sizeof(uint32_t)))) {
            Refuse(response, Status::NotReadable, "allocation bits unreadable");
            return Progress::Done;
        }
        bits = bitsCopy.data();
    }
    uint32_t occupied = 0;
    for (uint32_t slot = 0; slot < num; ++slot) occupied += SlotAllocated(bits, slot);
    if (occupied != num - static_cast<uint32_t>(header.numFree)) {
        Refuse(response, Status::Refused, "allocation bits disagree with the free count");
        return Progress::Done;
    }
    const auto* data = reinterpret_cast<const uint8_t*>(header.data);
    bool unreadable = false;
    const uint32_t next = WalkSlots(bits, num, first, fit, [&](uint32_t slot) {
        const auto* element = data + static_cast<uint64_t>(slot) * stride;
        if (unreadable || !host.readable(element, stride) ||
            !GuardedCopy(response.data + response.length, element, stride)) { unreadable = true; return; }
        response.length += stride;
    });
    if (unreadable) {
        response.length = 0;
        Refuse(response, Status::NotReadable, "set elements unreadable");
        return Progress::Done;
    }
    response.detail = FindDetail(occupied, next);
    StillSame(before, pack, response);
    return Progress::Done;
}

}

// Off the engine's thread (Context::offThreadReads) these run under a fault guard.
Progress PreviewArray(const Request& request, Response& response, bool first, const Host& host) {
    return GuardedRead(&PreviewArrayBody, request, response, first, host);
}
Progress Entries(const Request& request, Response& response, bool first, const Host& host) {
    return GuardedRead(&EntriesBody, request, response, first, host);
}
}
