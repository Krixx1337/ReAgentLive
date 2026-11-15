// reagentlive CLI console: layout-export and layout-check, many classes' reflected layouts in one command.
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "reagentlive/frontends/cli/results/ue/layout.h"

#include "reagentlive/frontends/cli/console/ue/internal_ue.h"

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

void RunLayoutExport(Session& session, const Command& command, std::string_view jsonCommand) {
    shape::LayoutExport out;
    out.path = command.file;
    std::string lines;
    for (const auto& name : command.fields) {
        auto fields = ClassFields(session, name);
        if (!fields) {
            if (!NotFound(fields.error())) return CommandError(session, jsonCommand, fields.error());
            if (!out.missing) out.missing.emplace();
            out.missing->push_back(name);
            continue;
        }
        ++out.classes;
        for (const auto& field : *fields) {
            if (field.owner != name) continue; // Inherited: its own class's line.
            lines += LayoutJson(field.owner, field.name, field.offset, field.size, field.type).dump() + "\n";
            ++out.fields;
        }
    }
    std::ofstream file(command.file, std::ios::binary | std::ios::trunc);
    if (!file || !(file << lines)) return JsonError(session, jsonCommand, "file", "cannot write " + command.file);
    Emit(session, jsonCommand, std::move(out));
}

void RunLayoutCheck(Session& session, const Command& command, std::string_view jsonCommand) {
    std::ifstream file(command.file);
    if (!file) return JsonError(session, jsonCommand, "file", "cannot read " + command.file);
    std::vector<LayoutLine> lines;
    size_t number = 0;
    for (std::string text; std::getline(file, text);) {
        ++number;
        if (text.find_first_not_of(" \t\r") == std::string::npos) continue;
        auto line = ParseLayoutLine(text);
        if (!line) return JsonError(session, jsonCommand, "validation",
            command.file + " line " + std::to_string(number) + ": " + line.error());
        if (lines.size() == MaxLayoutLines)
            return JsonError(session, jsonCommand, "validation", "at most " + std::to_string(MaxLayoutLines) + " lines");
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
        return JsonError(session, jsonCommand, "unsupported", "layout needs an agent with fields");
    if (command.target == "export") return RunLayoutExport(session, command, jsonCommand);
    RunLayoutCheck(session, command, jsonCommand);
}
}
