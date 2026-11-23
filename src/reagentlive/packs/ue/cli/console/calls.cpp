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
#include "reagentlive/frontends/cli/call/build.h"
#include "reagentlive/packs/ue/cli/values/format_ue.h"
#include "reagentlive/packs/ue/cli/values/json_output.h"
#include "reagentlive/packs/ue/cli/values/pack_input.h"
#include "reagentlive/frontends/cli/state/names.h"
#include "reagentlive/core/client.h"
#include "reagentlive/packs/ue/client_ue.h"

#include "reagentlive/packs/ue/cli/console/internal_ue.h"
#include "reagentlive/packs/ue/cli/console/rows.h"

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

// func: the metadata, then the signature (game thread only: else why not).
void PrintFunction(Session& session, const Ref& ref, std::string_view name, std::string_view jsonCommand) {
    Client& client = session.client;
    const auto function = ue::Function(client, ref, name);
    if (!function) return CommandError(session, jsonCommand, function.error());
    shape::Func out;
    if (!function->exec) {
        out = MakeFunc(ref, *function, {});
    } else {
        const auto modules = client.Modules();
        out = modules ? MakeFunc(ref, *function, *modules) : MakeFunc(ref, *function, {}, modules.error().message);
    }
    if (!client.Supports(ue::op::Parameters)) out.paramsError = "the agent does not read parameters";
    else if (auto signature = ue::Parameters(client, function->address)) AddSignature(out, *signature);
    else out.paramsError = signature.error().message;
    Emit(session, jsonCommand, std::move(out));
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
// An object or class pointer input: a literal ref, an address, or None
// (null). The object is described first, so a gone one is refused. Empty,
// or why the input is refused.
std::string PackObject(Session& session, std::string_view text, std::span<uint8_t> out) {
    std::fill(out.begin(), out.end(), uint8_t{0});
    if (text == "None" || text == "null" || text == "0") return {};
    Ref ref{};
    uint64_t address = 0;
    if (!text::LiteralRef(text, ref)) {
        if (!text::Number(text, address) || !address) return "takes an object (a literal ref or \"0x...\") or null";
        ref = ue::AddressRef(address);
    }
    auto described = ue::Describe(session.client, ref);
    if (!described) return described.error().message;
    const uint64_t pointer = described->ref.address;
    memcpy(out.data(), &pointer, 8);
    return {};
}
}

void RunTypedCall(Session& session, const Command& command, const ue::FunctionInfo& function,
                  const ue::Signature& signature, StructCache& structs,
                  std::string_view jsonCommand) {
    Client& client = session.client;
    // A const or pure function runs again to narrow a large result; any
    // other keeps it in a file (output budgets).
    session.input.reads = ue::EachFunctionAllowed(function.flags);
    const auto& arguments = command.fields;
    const auto fail = [&](std::string message) {
        JsonError(session, jsonCommand, ErrorKind::Validation, message);
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
        if (argument == arguments.end()) return fail("missing parameter: " + parameter.name + " (params gives the inputs by name)");
        if (std::count_if(argument, arguments.end(), matches) != 1) return fail("duplicate parameter: " + parameter.name);
        const auto value = std::string_view(*argument).substr(key.size());
        if ((parameter.type == "ObjectProperty" || parameter.type == "ClassProperty") && parameter.size == 8) {
            if (auto why = PackObject(session, value, std::span(bytes).subspan(parameter.offset, 8));
                !why.empty())
                return fail(parameter.name + ": " + why);
            continue;
        }
        // Bools, integers, floats, enums, names, tags and plain structs
        // ("(X=1,Y=2,Z=3)" or "(1,2,3)").
        std::string missing;
        const auto nameOf = [&](std::string_view text, int32_t& index, uint32_t& number) {
            return PackName(session, text, index, number, missing);
        };
        if (!pack_input::PackValue(parameter, value,
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
    // Returned arrays as rows (`view.fields`, `view.start`, `view.limit` pick from them).
    RowReader rows(session, structs);
    const RowPaths paths = ParsePaths(command.rowPaths);
    const auto arrayOutput = [](const ue::Field& parameter) {
        return !ue::InputParameter(parameter.parameterFlags) && parameter.type == "ArrayProperty";
    };
    if (!command.rowPaths.empty()) {
        if (std::ranges::none_of(signature.parameters, arrayOutput)) return fail("fields picks from an array output; none");
        for (const auto& parameter : signature.parameters)
            if (arrayOutput(parameter))
                if (auto why = rows.Check(Element(parameter), paths); !why.empty()) return fail(std::move(why));
    }
    rows.Meta(session.meta);
    for (const auto& parameter : signature.parameters) {
        if (ue::InputParameter(parameter.parameterFlags)) continue;
        bool ignored = false;
        if (arrayOutput(parameter))
            rows.Array(parameter, FieldBytes(*result, parameter), command.from, command.limit, paths, ignored);
        else
            rows.Value(parameter, FieldBytes(*result, parameter), ignored);
    }
    rows.Resolve();
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
            rows.Array(parameter, FieldBytes(*result, parameter), command.from, command.limit, paths, partial) :
            rows.Value(parameter, FieldBytes(*result, parameter), partial);
        row.truncated = partial;
        truncated |= partial;
        if (command.nonzero) KeepNonZero(row.value);
        outputs.push_back(std::move(row));
    }
    auto record = MakeCall(command.request, *result, "typed", kept);
    record.function = function.owner + ":" + function.name; // As dry prints it.
    if (kept) record.keptReason = calls::KeptReason(signature, structs);
    record.outputs = std::move(outputs);
    record.truncated = truncated;
    if (rows.Unnamed()) record.unnamed = rows.Unnamed();
    return Emit(session, jsonCommand, std::move(record));
}

}
