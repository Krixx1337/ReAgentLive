// reagentlive CLI console: assets-export, each object of some classes into
// its own file (its get with `view.nonzero`, and a table's rows), for reading offline.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "reagentlive/packs/ue/cli/console/internal_ue.h"

namespace reagentlive::cli::detail {
namespace {
constexpr size_t ShownRows = 100;

// A file name from an object or class name: characters Windows refuses become '_'.
std::string FileName(std::string_view name) {
    std::string out;
    for (const char c : name)
        out.push_back(static_cast<unsigned char>(c) < 0x20 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos ?
            '_' : c);
    if (out.empty() || out == "." || out == "..") out = "_";
    return out;
}

// One line run with its records captured: the result, or {error}.
Json Captured(Session& session, const std::string& line) {
    std::vector<Json> records;
    std::vector<Json>* outer = session.capture;
    session.capture = &records;
    ExecuteLine(session, line);
    session.capture = outer;
    if (records.empty()) return Json{{"error", "no result"}};
    Json& record = records.back();
    if (record.value("ok", false) && record.contains("result")) return std::move(record["result"]);
    if (record.contains("error")) return Json{{"error", record["error"].value("message", record["error"].dump())}};
    return Json{{"error", "no result"}};
}

// A data table shows its RowStruct field; a curve table is known by its
// class or the class asked for (a subclass's own name need not say so).
bool Table(const Json& get, std::string_view className, std::string_view asked) {
    for (const auto name : {className, asked})
        if (name.find("CurveTable") != std::string_view::npos) return true;
    if (!get.contains("rows") || !get["rows"].is_array()) return false;
    return std::ranges::any_of(get["rows"], [](const Json& row) { return row.value("name", "") == "RowStruct"; });
}
}

void RunExportAssets(Session& session, const Command& command, std::string_view jsonCommand) {
    Client& client = session.client;
    if (!client.Supports(ue::op::Find) || !client.Supports(ue::op::Fields) || !client.Supports(ue::op::ReadObject))
        return JsonError(session, jsonCommand, ErrorKind::Unsupported, "assets-export needs an agent with find, fields and object reads");
    namespace fs = std::filesystem;
    const fs::path dir(command.file);
    std::error_code error;
    fs::create_directories(dir, error);
    if (error) return JsonError(session, jsonCommand, ErrorKind::File, "cannot create " + command.file + ": " + error.message());

    // The objects: each class's live instances, once each, in table order.
    std::vector<std::pair<ue::Object, std::string_view>> objects; // With the class asked for.
    std::set<int32_t> seen;
    bool capped = false;
    for (const auto& className : command.fields) {
        ue::FindOptions options;
        options.className = className;
        options.live = true;
        do {
            auto page = ue::Find(client, command.match, options);
            if (!page) return CommandError(session, jsonCommand, page.error());
            for (auto& object : page->objects) {
                if (!seen.insert(object.ref.index).second) continue;
                if (objects.size() == MaxExportObjects) { capped = true; break; }
                objects.emplace_back(std::move(object), className);
            }
            options.from = page->next;
        } while (options.from && !capped);
        if (capped) break;
    }

    shape::ExportAssets result;
    result.dir = fs::absolute(dir, error).string();
    result.capped = capped;
    Json index = Json::array();
    std::map<std::string, uint32_t> used; // Per file: names taken, so a repeated name gets ~N.
    // The lines run as batch items do: no result cap per item, and an
    // item's failure is the file's, not this command's.
    const auto budget = session.input.maxChars;
    const bool failed = session.failed;
    session.input.maxChars = 0;
    for (const auto& [object, asked] : objects) {
        const std::string ref = RefText(object.ref);
        Json file{{"ref", ref}, {"name", object.name}, {"class", object.className}};
        file["get"] = Captured(session, Json{{"verb", "get"}, {"args", {{"object", ref}, {"view", {{"nonzero", true}}}}}}.dump());
        shape::ExportedAsset row{object.name, object.className, {}, FullRef(object.ref), std::nullopt, std::nullopt};
        if (file["get"].contains("error")) row.error = file["get"]["error"].get<std::string>();
        else if (Table(file["get"], object.className, asked)) {
            file["rows"] = Captured(session, Json{{"verb", "rows"}, {"args", {{"object", ref}, {"view", {{"limit", 0}}}}}}.dump());
            if (file["rows"].contains("error")) row.error = file["rows"]["error"].get<std::string>();
            else {
                row.tableRows = file["rows"].contains("rows") ? file["rows"]["rows"].size() : 0;
                ++result.tables;
            }
        }
        const fs::path folder = dir / FileName(object.className);
        std::string stem = FileName(object.name);
        if (const uint32_t n = ++used[(folder / stem).string()]; n > 1) stem += "~" + std::to_string(n);
        const fs::path path = folder / (stem + ".json");
        fs::create_directories(folder, error);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out || !(out << file.dump(1) << '\n')) row.error = "cannot write " + path.string();
        row.file = path.string();
        result.failed += row.error.has_value();
        ++result.objects;
        index.push_back(ToJson(row));
        if (result.rows.size() < ShownRows) result.rows.push_back(std::move(row));
    }
    session.input.maxChars = budget;
    session.failed = failed;
    const fs::path indexPath = dir / "index.json";
    std::ofstream out(indexPath, std::ios::binary | std::ios::trunc);
    if (!out || !(out << index.dump(1) << '\n'))
        return JsonError(session, jsonCommand, ErrorKind::File, "cannot write " + indexPath.string());
    result.index = indexPath.string();
    if (result.objects > result.rows.size()) result.omitted = result.objects - result.rows.size();
    Emit(session, jsonCommand, std::move(result));
}
}
