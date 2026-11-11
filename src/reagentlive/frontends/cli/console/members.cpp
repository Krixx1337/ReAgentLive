// reagentlive CLI console: native members named in the native file (label),
// checked against the object they are shown on.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h min/max macros break std::min/max.
#endif
#include <windows.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/state/native_names.h"
#include "reagentlive/frontends/cli/results/result_json.h"

#include "reagentlive/frontends/cli/console/internal.h"

namespace reagentlive::cli::detail {

uint32_t ExeTimestamp(Session& session) {
    if (!session.exeTimestamp) {
        const auto modules = LoadedModules(session);
        if (modules.empty()) return 0;
        const auto stamp = ImageTimestamp(session, modules.front());
        if (!stamp) return 0;
        session.exeTimestamp = *stamp;
    }
    return *session.exeTimestamp;
}

NativeShown NativeMembers(Session& session, const Ref& object, std::span<const ue::Field> reflected, bool instance) {
    NativeShown out;
    const auto members = session.native.For(reflected);
    if (members.empty()) return out;
    // The class size reaches past the last reflected field; a class or struct
    // object's own size says nothing about the layout it describes.
    uint32_t classSize = 0;
    if (instance && session.client.Supports(ue::op::Describe))
        if (auto described = ue::Describe(session.client, object);
            described && !described->className.ends_with("Class") && !described->className.ends_with("Struct"))
            classSize = described->size;
    const uint32_t timestamp = ExeTimestamp(session);
    for (const auto* member : members) {
        auto check = CheckNative(*member, reflected, classSize, timestamp);
        (check.problem.empty() ? out.shown : out.refused).push_back(std::move(check));
    }
    return out;
}

Json NativeRefusedJson(std::span<const NativeCheck> refused) {
    Json rows = Json::array();
    for (const auto& check : refused)
        rows.push_back({{"line", check.member->line}, {"name", check.member->field.name}, {"owner", check.member->owner},
            {"offset", check.member->field.offset}, {"reason", check.problem}});
    return rows;
}

// label <object|Class> <offset> <type>[n] <Name>: appends one member to the
// native file, checked against the reflected layout first.
void RunLabel(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    const auto fail = [&](std::string message) { JsonError(session, jsonCommand, "validation", message); };
    if (session.nativeFile.empty()) return fail("no native file: the console's path is unknown");
    if (!client.Supports(ue::op::Fields)) return JsonError(session, jsonCommand, "unsupported", "label needs an agent with fields");
    Ref object = ue::GetRef(command.request);
    if (!command.className.empty()) {
        auto type = ResolveClass(session, command.className);
        if (!type) return CommandError(session, jsonCommand, type.error());
        object = *type;
    }
    auto fields = ObjectFields(session, object);
    if (!fields) return CommandError(session, jsonCommand, fields.error());
    auto field = layout::ParseField(command.target + " " + command.match, true);
    if (!field || field->type.empty()) return fail(field ? "a member needs a type" : field.error());
    field->offset = static_cast<uint32_t>(command.offset);
    NativeMember member{NativeOwner(*fields, field->offset, command.className), *field, ExeTimestamp(session), 0};
    if (member.owner.empty()) return fail("no reflected field to file the member under");
    if (!member.timestamp) return fail("the main image's PE timestamp is unreadable");
    uint32_t classSize = 0;
    if (command.className.empty())
        if (auto described = ue::Describe(client, object); described) classSize = described->size;
    if (auto check = CheckNative(member, *fields, classSize, member.timestamp); !check.problem.empty())
        return fail(member.field.name + " " + check.problem);
    const auto line = NativeLine(member.owner, member.field, member.timestamp);
    {
        std::ofstream file(session.nativeFile, std::ios::app);
        if (!(file << line << '\n')) return fail("cannot write " + session.nativeFile.string());
    }
    // Read the whole file again, so line numbers and replacements match it.
    std::ifstream in(session.nativeFile);
    NativeNames reloaded;
    std::string why;
    if (const size_t bad = reloaded.Load(in, &why)) return fail("native file line " + std::to_string(bad) + ": " + why);
    size_t number = 0;
    for (const auto* found : reloaded.For(*fields))
        if (found->owner == member.owner && found->field.offset == member.field.offset) number = found->line;
    session.native = std::move(reloaded);
    JsonResult(session, jsonCommand, {{"path", session.nativeFile.string()}, {"line", number}, {"owner", member.owner},
        {"offset", member.field.offset}, {"type", NativeType(member.field)}, {"name", member.field.name},
        {"size", member.field.Size()}, {"timestamp", member.timestamp}});
}
}
