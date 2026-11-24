// reagentlive Unreal pack: the bounds of each request the pack's ops take
// (Check), and the text helpers requests use. Pure; shared with the client,
// the console and tests. The wire layout is protocol_ue.h.
#pragma once
#include <stdint.h>
#include <string.h>
#include <string_view>
#include "reagentlive/packs/ue/protocol_ue.h"

namespace reagentlive::ue {
namespace detail {
// The object filter's data: a class name (may be empty) and NUL, then
// optionally an outer text and NUL, each under its cap.
inline bool FilterValid(const Request& request) {
    if (!request.length) return true;
    const auto* data = reinterpret_cast<const char*>(request.data);
    const auto* end = static_cast<const char*>(memchr(data, 0, request.length));
    if (!end || static_cast<uint32_t>(end - data) >= MaxClassName) return false;
    const uint32_t rest = request.length - static_cast<uint32_t>(end - data) - 1;
    if (!rest) return true;
    const auto* outerEnd = static_cast<const char*>(memchr(end + 1, 0, rest));
    if (!outerEnd || static_cast<uint32_t>(outerEnd - end - 1) >= MaxOuterText) return false;
    const auto tail = static_cast<uint32_t>(end + rest - outerEnd);  // After the outer text's NUL.
    return tail == 0 || (tail == 2 * sizeof(int32_t) && request.address);
}
// The outer object's ref: address, then slot and serial when the request
// carries them (index -1: an address alone, identified by the agent).
inline Ref FilterRef(const Request& request) {
    Ref ref{static_cast<uintptr_t>(request.address), -1, 0};
    if (!request.address || request.length < 2 * sizeof(int32_t) + 2) return ref;
    const auto* data = reinterpret_cast<const char*>(request.data);
    const size_t classLength = strnlen(data, request.length);
    if (classLength + 1 >= request.length) return ref;
    const size_t outerLength = strnlen(data + classLength + 1, request.length - classLength - 1);
    const size_t at = classLength + 1 + outerLength + 1;
    if (at + 2 * sizeof(int32_t) != request.length) return ref;
    memcpy(&ref.index, request.data + at, sizeof(int32_t));
    memcpy(&ref.serial, request.data + at + sizeof(int32_t), sizeof(int32_t));
    return ref;
}
inline std::string_view FilterOuter(const Request& request) {
    if (!request.length) return {};
    const auto* data = reinterpret_cast<const char*>(request.data);
    const size_t classLength = strnlen(data, request.length);
    return classLength + 1 < request.length ? std::string_view(data + classLength + 1) : std::string_view{};
}
// A class name in data: NUL-terminated, non-empty, under MaxClassName (an outer text may follow).
inline bool HasClass(const Request& request) {
    return request.length > 1 && request.data[0] && FilterValid(request);
}
inline bool HasFilter(const Request& request) { return HasClass(request) || request.address || !FilterOuter(request).empty(); }
inline bool FunctionText(std::string_view name) {
    const auto colon = name.find(':');
    return !name.empty() && colon != 0 && (colon == std::string_view::npos || colon + 1 < name.size());
}
}

// Bounds of the pack's ops; other ops are bad requests. Pure; shared with tests.
inline Status Check(const Request& request) {
    switch (request.op) {
    case op::Find: {
        if (!Terminated(request.text) || request.args[1] > INT32_MAX || !detail::FilterValid(request) ||
            (request.flags & ~(FindExact | FindLive)) || ((request.flags & FindExact) && !request.text[0])) return Status::BadRequest;
        return request.text[0] || detail::HasFilter(request) ? Status::Ok : Status::BadRequest;
    }
    case op::Census:
        if (!Terminated(request.text) || request.text[0] || request.args[1] > INT32_MAX || !detail::FilterValid(request))
            return Status::BadRequest;
        return Status::Ok;
    case op::NameFind:
        return Terminated(request.text) && request.text[0] && !request.length && request.args[1] <= INT32_MAX
            ? Status::Ok : Status::BadRequest;
    case op::Each: {
        if (!Terminated(request.text) || !detail::HasClass(request) || request.args[1] > MaxEachInstances ||
            (request.flags & ~EachReuse)) return Status::BadRequest;
        const uint32_t offset = EachOffset(request.args[0]), size = EachSize(request.args[0]);
        if (request.text[0])
            return detail::FunctionText(request.text) && !offset && size && size <= MaxEachValue ? Status::Ok : Status::BadRequest;
        return size && size <= MaxEachValue && offset <= MaxObjectBytes - size ? Status::Ok : Status::BadRequest;
    }
    case op::Call:
        return request.address && Terminated(request.text) && request.text[0] &&
            request.length <= MaxParams && !(request.flags & ~(CallRelease | CallPure)) ? Status::Ok : Status::BadRequest;
    case op::Script:
        return request.address && !request.length && request.args[1] <= MaxScriptEntries ? Status::Ok : Status::BadRequest;
    case op::Enum:
        return request.address && !request.length && request.args[1] <= MaxEnumEntries ? Status::Ok : Status::BadRequest;
    case op::GameThread:
        return !request.length && request.flags <= static_cast<uint32_t>(GameThreadAction::Off) ? Status::Ok
                                                                                                 : Status::BadRequest;
    case op::Entries: {
        const uint32_t offset = EntriesOffset(request.args[1]);
        const auto kind = static_cast<ContainerKind>(request.flags);
        const bool known = kind == ContainerKind::Array || kind == ContainerKind::Set || kind == ContainerKind::Map;
        const uint32_t header = kind == ContainerKind::Array ? sizeof(ArrayHeader) : sizeof(SparseHeader);
        return request.address && known && request.length && request.length <= MaxArrayElementBytes &&
            offset <= MaxObjectBytes - header && request.address <= UINT64_MAX - offset - header ? Status::Ok : Status::BadRequest;
    }
    case op::CallsStart:
        return request.address && Terminated(request.text) && detail::FunctionText(request.text) &&
            (!request.length || request.length == sizeof(uint64_t)) && request.args[1] &&
            request.args[1] <= MaxCallsMilliseconds && !(request.flags & ~CallsDry)
            ? Status::Ok : Status::BadRequest;
    case op::CallsRead:
    case op::CallsStop:
        return !request.address && !request.length && request.args[0] <= CallsLatest ? Status::Ok : Status::BadRequest;
    case op::ExecEntries:
        return !request.address && !request.length && request.args[1] <= MaxExecEntries ? Status::Ok : Status::BadRequest;
    case op::Refs:
        return request.args[0] && request.args[0] <= INT32_MAX && !request.length && request.args[1] <= MaxRefs
            ? Status::Ok : Status::BadRequest;
    case op::Function: {
        if (!request.address || request.length || !Terminated(request.text)) return Status::BadRequest;
        return detail::FunctionText(request.text) ? Status::Ok : Status::BadRequest;
    }
    case op::Names:
        return request.length && request.length % 4 == 0 && request.length <= MaxNames * 4 ? Status::Ok : Status::BadRequest;
    case op::Fields:
    case op::Parameters:
        return request.address && request.length <= MaxFieldStart ? Status::Ok : Status::BadRequest;
    case op::Functions:
        return request.address && request.length <= MaxFieldStart && Terminated(request.text)
            ? Status::Ok : Status::BadRequest;
    case op::ReadObject: {
        const uint64_t offset = request.args[1];
        return request.address && GetRef(request).index >= 0 && request.length &&
            request.length <= MaxRead && offset <= MaxObjectBytes - request.length &&
            request.address <= UINT64_MAX - offset - request.length ? Status::Ok : Status::BadRequest;
    }
    case op::Owner:
        return !request.address && request.length && request.length % sizeof(uint64_t) == 0 &&
            request.length / sizeof(uint64_t) <= MaxOwnerAddresses ? Status::Ok : Status::BadRequest;
    case op::Route: {
        if (!request.address || request.length != sizeof(RouteSpec)) return Status::BadRequest;
        RouteSpec spec;
        memcpy(&spec, request.data, sizeof(spec));
        return RouteSpecValid(spec) ? Status::Ok : Status::BadRequest;
    }
    case op::DescribeMany:
        return request.length && request.length % sizeof(Ref) == 0 && request.length / sizeof(Ref) <= MaxDescribeMany
            ? Status::Ok : Status::BadRequest;
    case op::Describe: {
        const Ref ref = GetRef(request);
        // Address-only lookup, full ref, or a weak slot with a positive serial.
        return ref.address || (ref.index >= 0 && ref.serial > 0) ? Status::Ok : Status::BadRequest;
    }
    case op::PreviewArray: {
        const auto offset = request.args[1];
        return request.address && request.length && request.length <= MaxArrayElementBytes &&
            offset <= MaxObjectBytes - sizeof(ArrayHeader) &&
            request.address <= UINT64_MAX - offset - sizeof(ArrayHeader) ? Status::Ok : Status::BadRequest;
    }
    }
    return Status::BadRequest;
}

// Case-insensitive ASCII substring.
inline bool NameMatches(std::string_view name, std::string_view query) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    if (query.empty() || query.size() > name.size()) return false;
    for (size_t start = 0; start + query.size() <= name.size(); ++start) {
        size_t i = 0;
        while (i < query.size() && lower(name[start + i]) == lower(query[i])) ++i;
        if (i == query.size()) return true;
    }
    return false;
}

// "Class:Function" → owner "Class"; "Function" → empty owner (first match on the chain).
struct FunctionName { std::string_view owner, name; };
inline FunctionName SplitFunction(std::string_view text) {
    const size_t colon = text.find(':');
    if (colon == std::string_view::npos) return {{}, text};
    return {text.substr(0, colon), text.substr(colon + 1)};
}
}
