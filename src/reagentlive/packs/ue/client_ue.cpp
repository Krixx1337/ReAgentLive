// reagentlive Unreal pack client calls.
#include <algorithm>
#include <cstring>
#include "reagentlive/packs/ue/client_ue.h"

namespace reagentlive::ue {
namespace {
template<size_t N> std::string Text(const char (&field)[N]) { return std::string(field, strnlen(field, N)); }

Error TooLong(const char* what) { return {Error::Kind::Agent, Status::BadRequest, 0, std::string(what) + " too long"}; }
}

// The object filter (protocol_ue.h): class name, NUL, then the outer text and NUL when set.
static bool SetFilter(Request& request, std::string_view className, const OuterFilter& outer) {
    if (className.size() >= MaxClassName || outer.text.size() >= MaxOuterText) return false;
    request.address = outer.object.address;
    const bool slot = outer.object.address && outer.object.index >= 0;
    if (className.empty() && outer.text.empty() && !slot) return true;
    memcpy(request.data, className.data(), className.size());
    request.data[className.size()] = 0;
    request.length = static_cast<uint32_t>(className.size() + 1);
    if (!outer.text.empty() || slot) {
        memcpy(request.data + request.length, outer.text.data(), outer.text.size());
        request.data[request.length + outer.text.size()] = 0;
        request.length += static_cast<uint32_t>(outer.text.size() + 1);
    }
    if (slot) {
        memcpy(request.data + request.length, &outer.object.index, sizeof(int32_t));
        memcpy(request.data + request.length + sizeof(int32_t), &outer.object.serial, sizeof(int32_t));
        request.length += 2 * sizeof(int32_t);
    }
    return true;
}

Result<FindResult> Find(Client& client, std::string_view text, FindOptions options, std::chrono::milliseconds timeout) {
    if (text.empty() && options.className.empty() && options.outer.Empty())
        return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "find needs a name, a class or an outer"});
    if (text.size() >= MaxText) return std::unexpected(TooLong("search text"));
    Request request{};
    request.op = op::Find;
    CopyText(request.text, text);
    if (!SetFilter(request, options.className, options.outer)) return std::unexpected(TooLong("class name or outer text"));
    request.args[1] = options.from;
    if (options.exact) {
        if (text.empty()) return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "an exact find needs a name"});
        request.flags = FindExact;
    }
    if (options.live) request.flags |= FindLive;
    auto response = client.Send(request, timeout);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
    FindResult result{{}, FindTotal(response->detail), FindNext(response->detail)};
    const size_t count = std::min(response->length / sizeof(FindEntry), MaxFindResults);
    for (size_t i = 0; i < count; ++i) {
        FindEntry entry;
        memcpy(&entry, response->data + i * sizeof(FindEntry), sizeof(entry));
        result.objects.push_back({entry.object, Text(entry.name), Text(entry.className), 0, entry.outer});
    }
    return result;
}

static Result<Signature> ReadFields(Client& client, const Ref& object, uint32_t op, const KnownTable* known = nullptr,
                                    bool* cached = nullptr) {
    Signature result;
    auto& fields = result.parameters;
    Request request{};
    request.op = op;
    SetRef(request, object);
    for (;;) {
        request.length = static_cast<uint32_t>(fields.size());
        auto response = client.Send(request);
        if (!response) return std::unexpected(response.error());
        if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
        const uint32_t total = FindTotal(response->detail);
        const uint32_t size = FindNext(response->detail);
        const size_t before = fields.size();
        const bool read = ReadFieldsPage(response->data, response->length,
            [&](const FieldRecord& r, const Ref& structType, const Ref& valueStructType, const FieldStrings& s) {
                fields.push_back({r.offset, r.size, r.boolMask, std::string(s.type), std::string(s.name),
                    std::string(s.owner), r.elementSize, std::string(s.elementType), structType, std::string(s.structName),
                    op == op::Parameters ? r.flags : uint8_t{0}, r.stride, r.valueOffset, r.valueSize,
                    std::string(s.valueType), valueStructType, std::string(s.valueStructName)});
                // A map's subtype is "Key|Value".
                const auto bar = s.subtype.find('|');
                fields.back().subtype = s.subtype.substr(0, bar);
                if (bar != std::string_view::npos) fields.back().valueSubtype = s.subtype.substr(bar + 1);
            });
        const size_t count = fields.size() - before;
        if (!read || fields.size() > total || (!count && fields.size() < total) ||
            (op == op::Parameters && before && size != result.size))
            return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "invalid fields page"});
        result.size = size;
        // Page 0 names the type: a known table whose first page matches is
        // reused. Not copied: under a hold the next request must follow within
        // the agent's short wait, and a large table takes longer to copy.
        if (known && *known && cached && fields.size() == count && count < total) {
            const auto* table = (*known)(static_cast<int32_t>(size));
            if (table && table->size() == total && std::equal(fields.begin(), fields.end(), table->begin())) {
                *cached = true;
                fields.clear();
                return result;
            }
        }
        if (fields.size() >= total) break;
        if (fields.size() >= MaxFieldStart && op == op::Fields) break;
        if (fields.size() >= MaxFieldStart)
            return std::unexpected(TooLong("field list"));
    }
    return result;
}

Result<std::vector<Field>> Fields(Client& client, const Ref& object) {
    auto result = ReadFields(client, object, op::Fields);
    if (!result) return std::unexpected(result.error());
    return std::move(result->parameters);
}

Result<FieldTable> Fields(Client& client, const Ref& object, const KnownTable& known) {
    bool cached = false;
    auto result = ReadFields(client, object, op::Fields, &known, &cached);
    if (!result) return std::unexpected(result.error());
    return FieldTable{static_cast<int32_t>(result->size), std::move(result->parameters), cached};
}

Result<Signature> Parameters(Client& client, uint64_t functionAddress) {
    return ReadFields(client, AddressRef(functionAddress), op::Parameters);
}

Result<ArrayPreview> PreviewArray(Client& client, const Ref& ref, const Field& field) {
    Request request{};
    request.op = op::PreviewArray;
    SetRef(request, ref);
    request.args[1] = field.offset;
    request.length = field.elementSize;
    auto response = client.Send(request);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
    if (response->detail > INT32_MAX ||
        response->length != ArrayPreviewCount(response->detail, field.elementSize) * field.elementSize)
        return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "invalid array preview response"});
    return ArrayPreview{static_cast<uint32_t>(response->detail),
        std::vector<uint8_t>(response->data, response->data + response->length)};
}

Result<EntriesPage> Entries(Client& client, const Ref& ref, uint32_t offset, ContainerKind kind, uint32_t stride,
                            uint32_t first) {
    Request request{};
    request.op = op::Entries;
    SetRef(request, ref);
    request.args[1] = EntriesArgs(offset, first);
    request.length = stride;
    request.flags = static_cast<uint32_t>(kind);
    auto response = client.Send(request);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
    const uint32_t total = FindTotal(response->detail), next = FindNext(response->detail);
    if (!stride || response->length % stride || response->length / stride > MaxRead / stride || (next && next <= first))
        return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "invalid entries page"});
    return EntriesPage{total, next, std::vector<uint8_t>(response->data, response->data + response->length)};
}

Result<std::vector<std::string>> Names(Client& client, std::span<const int32_t> indices) {
    std::vector<std::string> names;
    while (names.size() < indices.size()) {
        Request request{};
        request.op = op::Names;
        const size_t count = std::min<size_t>(indices.size() - names.size(), MaxNames);
        memcpy(request.data, indices.data() + names.size(), count * 4);
        request.length = static_cast<uint32_t>(count * 4);
        auto response = client.Send(request);
        if (!response) return std::unexpected(response.error());
        if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
        const size_t before = names.size();
        for (size_t at = 0; at < response->length && names.size() - before < response->detail;) {
            const auto* text = reinterpret_cast<const char*>(response->data + at);
            const size_t length = strnlen(text, response->length - at);
            names.emplace_back(text, length);
            at += length + 1;
        }
        if (names.size() - before != response->detail || !response->detail)
            return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "invalid names response"});
    }
    return names;
}

Result<FunctionInfo> Function(Client& client, const Ref& ref, std::string_view name) {
    if (name.size() >= MaxText) return std::unexpected(TooLong("function name"));
    Request request{};
    request.op = op::Function;
    SetRef(request, ref);
    CopyText(request.text, name);
    auto response = client.Send(request);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
    FunctionEntry entry{};
    if (response->length != sizeof(entry))
        return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "incomplete function description"});
    memcpy(&entry, response->data, sizeof(entry));
    return FunctionInfo{entry.address, entry.exec, entry.flags, entry.executable != 0, Text(entry.owner), Text(entry.name),
        entry.interface != 0, entry.interfaceOffset, entry.slot, entry.implementation};
}

Result<FunctionList> Functions(Client& client, const Ref& ref, std::string_view filter, uint32_t from) {
    if (filter.size() >= MaxText) return std::unexpected(TooLong("function filter"));
    Request request{};
    request.op = op::Functions;
    SetRef(request, ref);
    request.length = from;
    CopyText(request.text, filter);
    auto response = client.Send(request);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
    const size_t count = response->length / sizeof(FunctionNameEntry);
    if (response->length % sizeof(FunctionNameEntry) || count > MaxFunctionsPerPage ||
        (response->detail && (count != MaxFunctionsPerPage || response->detail != from + count)))
        return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "invalid function list"});
    FunctionList result{{}, {}, static_cast<uint32_t>(response->detail)};
    for (size_t i = 0; i < count; ++i) {
        FunctionNameEntry entry;
        memcpy(&entry, response->data + i * sizeof(entry), sizeof(entry));
        result.names.push_back(Text(entry.owner) + ":" + Text(entry.name));
        result.interfaces.push_back(entry.interface != 0);
    }
    return result;
}

Result<Object> Describe(Client& client, const Ref& ref) {
    Request request{};
    request.op = op::Describe;
    SetRef(request, ref);
    auto response = client.Send(request);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
    FindEntry entry{};
    if (response->length != sizeof(entry))
        return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "incomplete object description"});
    memcpy(&entry, response->data, sizeof(entry));
    return Object{entry.object, Text(entry.name), Text(entry.className),
        response->detail <= UINT32_MAX ? static_cast<uint32_t>(response->detail) : 0, entry.outer};
}

Result<std::vector<std::optional<Object>>> DescribeMany(Client& client, std::span<const Ref> refs) {
    std::vector<std::optional<Object>> objects;
    for (size_t start = 0; start < refs.size(); start += MaxDescribeMany) {
        const size_t count = std::min<size_t>(MaxDescribeMany, refs.size() - start);
        Request request{};
        request.op = op::DescribeMany;
        request.length = static_cast<uint32_t>(count * sizeof(Ref));
        memcpy(request.data, refs.data() + start, request.length);
        auto response = client.Send(request);
        if (!response) return std::unexpected(response.error());
        if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
        if (response->length != count * sizeof(FindEntry))
            return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "incomplete object descriptions"});
        for (size_t i = 0; i < count; ++i) {
            FindEntry entry{};
            memcpy(&entry, response->data + i * sizeof(entry), sizeof(entry));
            if (entry.object.address) objects.push_back(Object{entry.object, Text(entry.name), Text(entry.className), 0, entry.outer});
            else objects.push_back(std::nullopt);
        }
    }
    return objects;
}

Result<std::vector<uint8_t>> ReadObject(Client& client, const Ref& object, uint32_t offset, uint32_t length) {
    Request request{};
    request.op = op::ReadObject;
    SetRef(request, object);
    request.args[1] = offset;
    request.length = length;
    auto response = client.Send(request);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
    if (response->length != length)
        return std::unexpected(Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "incomplete object read"});
    return std::vector<uint8_t>(response->data, response->data + length);
}

namespace {
Error BadResponse(const char* what) { return {Error::Kind::Agent, Status::BadRequest, 0, {}, what}; }
}

Result<CensusResult> Census(Client& client, std::string_view className, OuterFilter outer, std::chrono::milliseconds timeout) {
    if (className.size() >= MaxClassName || outer.text.size() >= MaxOuterText) return std::unexpected(TooLong("class name or outer text"));
    CensusResult result{{}, 0};
    Request request{};
    request.op = op::Census;
    SetFilter(request, className, outer);
    for (;;) {
        request.args[1] = result.rows.size();
        auto response = client.Send(request, timeout);
        if (!response) return std::unexpected(response.error());
        if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
        const size_t count = response->length / sizeof(CensusEntry);
        const uint32_t total = FindTotal(response->detail), next = FindNext(response->detail);
        if (response->length % sizeof(CensusEntry) || count > MaxCensusPerPage || result.rows.size() + count > total ||
            (next && next != result.rows.size() + count) || (!next && result.rows.size() + count != total))
            return std::unexpected(BadResponse("invalid census page"));
        for (size_t i = 0; i < count; ++i) {
            CensusEntry entry;
            memcpy(&entry, response->data + i * sizeof(entry), sizeof(entry));
            CensusRow row{{entry.type, Text(entry.name), {}}, entry.count, {}};
            for (uint32_t s = 0; s < entry.samples && s < MaxCensusSamples; ++s) row.samples.push_back(entry.sample[s]);
            result.instances += entry.count;
            result.rows.push_back(std::move(row));
        }
        if (!next) return result;
    }
}

Result<std::vector<EnumEntry>> EnumEntries(Client& client, const Ref& object) {
    std::vector<EnumEntry> entries;
    Request request{};
    request.op = op::Enum;
    SetRef(request, object);
    for (;;) {
        request.args[1] = entries.size();
        auto response = client.Send(request);
        if (!response) return std::unexpected(response.error());
        if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
        const size_t count = response->length / sizeof(EnumEntry);
        const uint32_t total = FindTotal(response->detail), next = FindNext(response->detail);
        if (response->length % sizeof(EnumEntry) || count > MaxEnumPerPage || entries.size() + count > total ||
            (next && next != entries.size() + count) || (!next && entries.size() + count != total))
            return std::unexpected(BadResponse("invalid enum page"));
        for (size_t i = 0; i < count; ++i) {
            EnumEntry entry;
            memcpy(&entry, response->data + i * sizeof(entry), sizeof(entry));
            entries.push_back(entry);
        }
        if (!next) return entries;
    }
}

Result<ScriptResult> Script(Client& client, const Ref& function) {
    ScriptResult result{0, {}};
    Request request{};
    request.op = op::Script;
    SetRef(request, function);
    for (;;) {
        request.args[1] = result.entries.size();
        auto response = client.Send(request);
        if (!response) return std::unexpected(response.error());
        if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
        const size_t count = response->length / sizeof(ScriptEntry);
        const auto next = static_cast<uint32_t>(response->detail >> 32);
        if (response->length % sizeof(ScriptEntry) || count > MaxScriptEntriesPerPage ||
            (next && next != result.entries.size() + count))
            return std::unexpected(BadResponse("invalid script page"));
        result.size = static_cast<uint32_t>(response->detail);
        for (size_t i = 0; i < count; ++i) {
            ScriptEntry entry;
            memcpy(&entry, response->data + i * sizeof(entry), sizeof(entry));
            entry.text[sizeof(entry.text) - 1] = 0;
            result.entries.push_back(entry);
        }
        if (!next) return result;
    }
}

Result<std::vector<ExecFunction>> ExecEntries(Client& client, std::chrono::milliseconds timeout) {
    std::vector<ExecFunction> rows;
    Request request{};
    request.op = op::ExecEntries;
    for (;;) {
        request.args[1] = rows.size();
        auto response = client.Send(request, timeout);
        if (!response) return std::unexpected(response.error());
        if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
        const size_t count = response->length / sizeof(ExecEntry);
        const uint32_t total = FindTotal(response->detail), next = FindNext(response->detail);
        if (response->length % sizeof(ExecEntry) || count > MaxExecPerPage || rows.size() + count > total ||
            (next && next != rows.size() + count) || (!next && rows.size() + count != total))
            return std::unexpected(BadResponse("invalid exec entries page"));
        for (size_t i = 0; i < count; ++i) {
            ExecEntry entry;
            memcpy(&entry, response->data + i * sizeof(entry), sizeof(entry));
            entry.owner[sizeof(entry.owner) - 1] = entry.name[sizeof(entry.name) - 1] = 0;
            rows.push_back({entry.exec, entry.owner, entry.name});
        }
        if (!next) return rows;
    }
}

Result<RefsResult> Refs(Client& client, int32_t nameIndex, std::chrono::milliseconds timeout) {
    if (nameIndex <= 0) return std::unexpected(BadResponse("refs needs a name index"));
    RefsResult result{{}, false};
    Request request{};
    request.op = op::Refs;
    request.args[0] = static_cast<uint32_t>(nameIndex);
    for (;;) {
        request.args[1] = result.rows.size();
        auto response = client.Send(request, timeout);
        if (!response) return std::unexpected(response.error());
        if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
        const size_t count = response->length / sizeof(RefsEntry);
        const uint32_t total = FindTotal(response->detail), next = FindNext(response->detail);
        if (response->length % sizeof(RefsEntry) || count > MaxRefsPerPage || result.rows.size() + count > total ||
            (next && next != result.rows.size() + count))
            return std::unexpected(BadResponse("invalid refs page"));
        result.capped = std::string_view(response->reason) == "capped";
        for (size_t i = 0; i < count; ++i) {
            RefsEntry entry;
            memcpy(&entry, response->data + i * sizeof(entry), sizeof(entry));
            entry.owner[sizeof(entry.owner) - 1] = entry.name[sizeof(entry.name) - 1] = 0;
            result.rows.push_back(entry);
        }
        if (!next) return result;
    }
}

Result<NameFindResult> NameFind(Client& client, std::string_view text, uint32_t from, std::chrono::milliseconds timeout) {
    if (text.empty()) return std::unexpected(BadResponse("name-find needs text"));
    if (text.size() >= MaxText) return std::unexpected(TooLong("search text"));
    Request request{};
    request.op = op::NameFind;
    CopyText(request.text, text);
    request.args[1] = from;
    auto response = client.Send(request, timeout);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
    if (response->detail > INT32_MAX) return std::unexpected(BadResponse("invalid name-find cursor"));
    NameFindResult result{{}, static_cast<uint32_t>(response->detail)};
    for (size_t at = 0; at < response->length;) {
        int32_t index;
        if (at + sizeof(index) >= response->length) return std::unexpected(BadResponse("invalid name-find page"));
        memcpy(&index, response->data + at, sizeof(index));
        const auto* name = reinterpret_cast<const char*>(response->data + at + sizeof(index));
        const size_t length = strnlen(name, response->length - at - sizeof(index));
        result.names.push_back({index, std::string(name, length)});
        at += sizeof(index) + length + 1;
    }
    return result;
}

Result<EachResult> Each(Client& client, const EachQuery& query, std::chrono::milliseconds timeout) {
    if (query.className.empty() || query.className.size() >= MaxClassName) return std::unexpected(TooLong("class name"));
    if (query.function.size() >= MaxText) return std::unexpected(TooLong("function name"));
    Request request{};
    request.op = op::Each;
    request.flags = query.reuse ? EachReuse : 0;
    if (!SetFilter(request, query.className, query.outer)) return std::unexpected(TooLong("outer text"));
    CopyText(request.text, query.function);
    request.args[0] = EachField(query.function.empty() ? query.offset : 0, query.size);
    if (ue::Check(request) != Status::Ok) return std::unexpected(BadResponse("invalid each query"));
    const uint32_t stride = EachStride(query.size);
    EachResult result{{}, false};
    for (;;) {
        request.args[1] = result.rows.size();
        auto response = client.Send(request, timeout);
        if (!response) return std::unexpected(response.error());
        if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
        const size_t count = response->length / stride;
        const uint32_t total = FindTotal(response->detail), next = FindNext(response->detail);
        if (response->length % stride || result.rows.size() + count > total ||
            (next && next != result.rows.size() + count) || (!next && result.rows.size() + count != total))
            return std::unexpected(BadResponse("invalid each page"));
        result.capped |= std::string_view(response->reason) == "capped";
        for (size_t i = 0; i < count; ++i) {
            const uint8_t* row = response->data + i * stride;
            EachRow item{};
            memcpy(&item.object, row, sizeof(Ref));
            item.value.assign(row + sizeof(Ref), row + sizeof(Ref) + query.size);
            result.rows.push_back(std::move(item));
        }
        if (!next) return result;
    }
}

Result<std::vector<uint8_t>> Call(Client& client, const Ref& object, std::string_view function,
                                  std::span<const uint8_t> params, uint64_t expectedFunction,
                                  uint32_t flags, bool* kept) {
    if (function.empty() || function.size() >= MaxText) return std::unexpected(TooLong("function name"));
    if (params.size() > MaxParams) return std::unexpected(TooLong("parameter block"));
    if (kept) *kept = false;
    Request request{};
    request.op = op::Call;
    request.flags = flags;
    SetRef(request, object);
    request.args[1] = expectedFunction;
    CopyText(request.text, function);
    request.length = static_cast<uint32_t>(params.size());
    if (!params.empty()) memcpy(request.data, params.data(), params.size());
    auto response = client.Send(request);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
    if (kept) *kept = (response->detail & CallKept) != 0;
    return std::vector<uint8_t>(response->data, response->data + response->length);
}

Result<CallsStarted> CallsStart(Client& client, const Ref& object, std::string_view function, uint64_t milliseconds,
                                uint64_t filter, bool dry) {
    if (function.empty() || function.size() >= MaxText) return std::unexpected(TooLong("function name"));
    Request request{};
    request.op = op::CallsStart;
    request.flags = dry ? CallsDry : 0;
    SetRef(request, object);
    CopyText(request.text, function);
    request.args[1] = milliseconds;
    if (filter) {
        memcpy(request.data, &filter, sizeof(filter));
        request.length = sizeof(filter);
    }
    auto response = client.Send(request);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
    CallsStarted started{};
    if (response->length != sizeof(started)) return std::unexpected(BadResponse("invalid calls-start reply"));
    memcpy(&started, response->data, sizeof(started));
    return started;
}

namespace {
Result<CallsState> StateFrom(const Response& response) {
    CallsState state{};
    if (response.length < sizeof(state)) return std::unexpected(BadResponse("invalid calls reply"));
    memcpy(&state, response.data, sizeof(state));
    if (state.slot >= MaxCallsSlots || state.blockSize > MaxCallsBlock) return std::unexpected(BadResponse("invalid calls state"));
    return state;
}
}

Result<CallsResult> CallsRead(Client& client, uint64_t slot, uint64_t after) {
    CallsResult result{{}, {}, after};
    Request request{};
    request.op = op::CallsRead;
    request.args[0] = slot;
    for (;;) {
        request.args[1] = result.last;
        auto response = client.Send(request);
        if (!response) return std::unexpected(response.error());
        if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
        auto state = StateFrom(*response);
        if (!state) return std::unexpected(state.error());
        result.state = *state;
        request.args[0] = state->slot; // Later pages read the same recording.
        size_t at = sizeof(CallsState);
        uint64_t previous = result.last;
        while (at < response->length) {
            CallsCall call{};
            if (response->length - at < sizeof(CallsRow)) return std::unexpected(BadResponse("truncated calls row"));
            memcpy(&call.row, response->data + at, sizeof(CallsRow));
            at += sizeof(CallsRow);
            if (call.row.size > MaxCallsBlock || response->length - at < call.row.size || call.row.sequence <= previous)
                return std::unexpected(BadResponse("invalid calls row"));
            call.block.assign(response->data + at, response->data + at + call.row.size);
            at += call.row.size;
            previous = call.row.sequence;
            result.calls.push_back(std::move(call));
        }
        if (response->detail != previous) return std::unexpected(BadResponse("calls cursor mismatch"));
        const bool more = response->detail != result.last && response->detail < state->written;
        result.last = response->detail;
        if (!more) return result;
    }
}

Result<CallsState> CallsStop(Client& client, uint64_t slot) {
    Request request{};
    request.op = op::CallsStop;
    request.args[0] = slot;
    auto response = client.Send(request);
    if (!response) return std::unexpected(response.error());
    if (response->status != Status::Ok) return std::unexpected(AgentError(*response));
    return StateFrom(*response);
}
}
