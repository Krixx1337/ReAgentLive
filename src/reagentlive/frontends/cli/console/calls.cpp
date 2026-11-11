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
#include "reagentlive/frontends/cli/results/call_text.h"
#include "reagentlive/frontends/cli/console/console.h"
#include "reagentlive/frontends/cli/state/cache.h"
#include "reagentlive/frontends/cli/syntax/parse.h"
#include "reagentlive/frontends/cli/values/format.h"
#include "reagentlive/frontends/cli/values/json_output.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/core/client.h"
#include "reagentlive/ue/client_ue.h"

#include "reagentlive/frontends/cli/console/internal.h"

namespace reagentlive::cli::detail {

Json CallJson(const Request& request, std::span<const uint8_t> block, const char* mode, bool kept) {
    return {{"ref", ReusableRef(ue::GetRef(request))}, {"function", request.text}, {"mode", mode},
        {"block_size", block.size()}, {"hex", format_detail::Hex(block, block.size())}, {"kept", kept}};
}

Json ModuleJson(const Module& module) {
    return {{"name", module.name}, {"base", format_detail::Pointer(module.base)}, {"size", module.size}};
}

void PrintFunction(Session& session, const Ref& ref, std::string_view name, std::string_view jsonCommand) {
    Client& client = session.client;
    const auto function = ue::Function(client, ref, name);
    if (!function) return CommandError(session, jsonCommand, function.error());
    if (!function->exec) return JsonResult(session, jsonCommand, FunctionJson(ref, *function, {}));
    const auto modules = client.Modules();
    return JsonResult(session, jsonCommand, modules ? FunctionJson(ref, *function, *modules) :
        FunctionJson(ref, *function, {}, modules.error().message));
}

void PrintParameters(Session& session, const ue::FunctionInfo& function, const ue::Signature& signature, const Ref& object) {
    JsonResult(session, "params", ParamsJson(object, function, signature));
}

namespace {
bool PackInput(Session& session, const ue::Field& parameter, std::string_view value, std::span<uint8_t> bytes) {
    Client& client = session.client;
    if (parameter.type == "NameProperty" && bytes.size() == 8 && value.starts_with('#')) {
        value.remove_prefix(1);
        const auto colon = value.find(':');
        uint64_t index = 0, number = 0;
        if (!text::Number(value.substr(0, colon), index) || index > INT32_MAX ||
            (colon != std::string_view::npos && !text::Number(value.substr(colon + 1), number)) ||
            number > INT32_MAX) return false;
        const auto id = static_cast<int32_t>(index);
        auto names = ue::Names(client, std::span(&id, 1));
        if (!names || names->front().empty()) return false;
        const auto instance = static_cast<uint32_t>(number);
        memcpy(bytes.data(), &id, sizeof(id));
        memcpy(bytes.data() + sizeof(id), &instance, sizeof(instance));
        return true;
    }
    if (parameter.type == "ByteProperty" || parameter.type == "EnumProperty") {
        uint64_t number = 0;
        if ((bytes.size() != 1 && bytes.size() != 2 && bytes.size() != 4 && bytes.size() != 8) ||
            !text::Number(value, number) || (bytes.size() < 8 && number >> (bytes.size() * 8)))
            return false;
        memcpy(bytes.data(), &number, bytes.size());
        return true;
    }
    return false;
}
}

void RunTypedCall(Session& session, const Command& command, uint64_t functionAddress,
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
        constexpr uint8_t required = ue::Param | ue::Plain | ue::Scalar;
        if ((flags & required) != required || (flags & ue::Reference) ||
            !parameter.size || size_t{parameter.offset} + parameter.size > bytes.size())
            return fail("unsupported typed parameter: " + parameter.name);
        if (flags & (ue::Output | ue::Return)) continue;
        ++inputs;
        const auto key = parameter.name + "=";
        const auto matches = [&](const std::string& argument) { return argument.starts_with(key); };
        const auto argument = std::ranges::find_if(arguments, matches);
        if (argument == arguments.end()) return fail("missing parameter: " + parameter.name);
        if (std::count_if(argument, arguments.end(), matches) != 1) return fail("duplicate parameter: " + parameter.name);
        if (!PackInput(session, parameter, std::string_view(*argument).substr(key.size()),
            std::span(bytes).subspan(parameter.offset, parameter.size)))
            return fail("invalid or unsupported input: " + parameter.name);
    }
    if (inputs != arguments.size()) return fail("unexpected typed argument");
    bool kept = false;
    auto result = ue::Call(client, ue::GetRef(command.request), command.request.text, bytes, functionAddress, 0, &kept);
    if (!result) return CommandError(session, jsonCommand, result.error());
    if (result->size() != bytes.size())
        return CommandError(session, jsonCommand, Error{Error::Kind::Agent, Status::BadRequest, 0, {}, "incomplete typed call result"});
    Json outputs = Json::array();
    bool truncated = false;
    for (const auto& parameter : signature.parameters) {
        if (!(parameter.parameterFlags & (ue::Output | ue::Return))) continue;
        Json row = session.meta ? FieldMetadata(parameter) : Json{{"name", parameter.name}};
        row["direction"] = parameter.parameterFlags & ue::Return ? "return" : "out";
        bool partial = false;
        row["value"] = JsonFieldValue(parameter, FieldBytes(*result, parameter), structs,
            format_detail::UnresolvedName{}, partial, session.Style());
        row["truncated"] = partial;
        truncated |= partial;
        outputs.push_back(std::move(row));
    }
    auto record = CallJson(command.request, *result, "typed", kept);
    if (kept) record["kept_reason"] = calls::KeptReason(signature, structs);
    record["outputs"] = std::move(outputs);
    record["truncated"] = truncated;
    return JsonResult(session, jsonCommand, std::move(record));
}
}
