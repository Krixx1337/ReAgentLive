// reagentlive CLI, Unreal pack: result structs (results/schema.h) of the
// objects: find, name and name-find, enum, owner and route, each, assets-export.
// Built from client results only (no I/O); conventions in results/result_json.h,
// the verb table naming each verb's type in verbs_ue.h.
#pragma once
#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/packs/ue/cli/values/json_output.h"
#include "reagentlive/packs/ue/cli/state/native_names.h"
#include "reagentlive/packs/ue/client_ue.h"

namespace reagentlive::cli {
namespace shape {
struct Find {
    std::vector<Object> rows;
    std::optional<uint64_t> total;      // Only the first page counts.
    uint32_t from = 0;
    std::optional<uint32_t> nextCursor;
    template<class V> static void Describe(V& v) {
        v("rows", &Find::rows);
        v("total", &Find::total);
        v("from", &Find::from);
        v("next_cursor", &Find::nextCursor);
    }
};

struct NameRow {
    static constexpr std::string_view TypeName = "ue.Name";
    int32_t index = 0;
    std::optional<std::string> name;    // Absent: no pooled name at that index.
    template<class V> static void Describe(V& v) {
        v("index", &NameRow::index);
        v("name", &NameRow::name);
    }
};
struct Names {
    std::vector<NameRow> rows;
    template<class V> static void Describe(V& v) { v("rows", &Names::rows); }
};
struct NameFind {
    std::vector<NameRow> rows;
    uint32_t from = 0;
    std::optional<uint32_t> nextCursor;
    template<class V> static void Describe(V& v) {
        v("rows", &NameFind::rows);
        v("from", &NameFind::from);
        v("next_cursor", &NameFind::nextCursor);
    }
};

// enum: the names and values in the enum's order.
struct EnumRow {
    std::string name;
    int64_t value = 0;
    template<class V> static void Describe(V& v) {
        v("name", &EnumRow::name);
        v("value", &EnumRow::value);
    }
};
struct Enum {
    RefId ref;
    std::string name;
    std::vector<EnumRow> rows;
    template<class V> static void Describe(V& v) {
        v("ref", &Enum::ref);
        v("name", &Enum::name);
        v("rows", &Enum::rows);
    }
};
// owner: one row per address, in the order asked. path: the field holding
// it (readable names; raw: the reflected path where it differs), into
// structs and a container's element; outer: the object's Outer pointer;
// container: the address is in a container's elements, and object holds
// the container (offset then absent).
struct OwnerRow {
    Addr address;
    std::optional<Object> object;
    std::optional<HexNum> offset;
    std::optional<std::string> path, raw, type;
    std::optional<bool> outer, container;
    template<class V> static void Describe(V& v) {
        v("address", &OwnerRow::address);
        v("object", &OwnerRow::object);
        v("offset", &OwnerRow::offset);
        v("path", &OwnerRow::path);
        v("raw", &OwnerRow::raw);
        v("type", &OwnerRow::type);
        v("outer", &OwnerRow::outer);
        v("container", &OwnerRow::container);
    }
};
struct Owner {
    std::vector<OwnerRow> rows;
    uint32_t owned = 0;                 // Rows with an object.
    template<class V> static void Describe(V& v) {
        v("rows", &Owner::rows);
        v("owned", &Owner::owned);
    }
};
// route: one hop, the field of the previous object (Name, Name.Member,
// Name[3].Member, or Outer) and the object it reaches.
struct RouteStep {
    std::string field;
    Object object;
    template<class V> static void Describe(V& v) {
        v("field", &RouteStep::field);
        v("object", &RouteStep::object);
    }
};
struct RouteRow {
    std::optional<std::string> root;    // The root it starts at, by name.
    Object source;
    std::string path;                   // The root's name (else the source's ref), then each step's field.
    std::vector<RouteStep> steps;
    template<class V> static void Describe(V& v) {
        v("root", &RouteRow::root);
        v("source", &RouteRow::source);
        v("path", &RouteRow::path);
        v("steps", &RouteRow::steps);
    }
};
// end: found (rows follow), depth (none within depth hops), budget (none
// before budget objects), none (every object the sources reach visited).
struct Route {
    Object object;
    std::vector<RouteRow> rows;
    std::string end;
    uint32_t visited = 0, depth = 0;
    template<class V> static void Describe(V& v) {
        v("object", &Route::object);
        v("rows", &Route::rows);
        v("end", &Route::end, "found|depth|budget|none");
        v("visited", &Route::visited);
        v("depth", &Route::depth);
    }
};

// each: a table, one row per live instance with a value per column, in the
// columns' order. A call's value is its return value, or {return, outputs}.
// A cell that failed is null, with an `errors` entry naming its row and
// column. With `seconds` (one column) also the polls and the changes seen.
struct EachRow {
    RefId ref;
    std::optional<std::string> name; // The instance's object name (absent when it was gone).
    std::vector<Json> values;
    std::optional<HexBytes> hex;    // `meta`, one column read in the instance: its bytes.
    template<class V> static void Describe(V& v) {
        v("ref", &EachRow::ref);
        v("name", &EachRow::name);
        v("values", &EachRow::values);
        v("hex", &EachRow::hex);
    }
};
struct EachError {
    RefId ref;
    uint32_t column = 0;
    std::string message;
    template<class V> static void Describe(V& v) {
        v("ref", &EachError::ref);
        v("column", &EachError::column);
        v("message", &EachError::message);
    }
};
struct EachChange {
    int64_t ms = 0;
    uint64_t tick = 0;
    RefId ref;
    Json before, after;
    template<class V> static void Describe(V& v) {
        v("ms", &EachChange::ms);
        v("tick", &EachChange::tick);
        v("ref", &EachChange::ref);
        v("before", &EachChange::before);
        v("after", &EachChange::after);
    }
};
// each's passes: the [begin, end) spans its columns need inside an instance,
// grouped so each pass reads at most `most` bytes. Sorted by begin.
inline std::vector<std::pair<uint32_t, uint32_t>> EachPasses(std::vector<std::pair<uint32_t, uint32_t>> spans, uint32_t most) {
    std::ranges::sort(spans);
    std::vector<std::pair<uint32_t, uint32_t>> passes;
    for (const auto& [begin, end] : spans) {
        if (!passes.empty() && end <= passes.back().first + most) {
            passes.back().second = std::max(passes.back().second, end);
            continue;
        }
        passes.emplace_back(begin, end);
    }
    return passes;
}

// `same_tick`: every value came from one agent pass (one column window, no
// path through a pointer).
struct Each {
    std::string className;
    RefId classRef;
    std::vector<std::string> columns;
    std::string mode;
    std::vector<EachRow> rows;
    bool sameTick = false, capped = false, truncated = false;
    std::optional<std::vector<EachError>> errors;
    std::optional<uint32_t> seconds;
    std::optional<uint64_t> polls;
    std::optional<std::vector<EachChange>> changes;
    std::optional<std::string> pollError;
    std::optional<uint64_t> matched, omitted; // `where`: instances that matched; past `view.limit`: rows not shown.
    std::optional<uint32_t> unnamed;    // Distinct pointers past the lookup cap, left as addresses.
    template<class V> static void Describe(V& v) {
        v("class", &Each::className);
        v("class_ref", &Each::classRef);
        v("columns", &Each::columns);
        v("mode", &Each::mode, "field|call");
        v("rows", &Each::rows);
        v("same_tick", &Each::sameTick);
        v("capped", &Each::capped);
        v("truncated", &Each::truncated);
        v("errors", &Each::errors);
        v("seconds", &Each::seconds);
        v("polls", &Each::polls);
        v("changes", &Each::changes);
        v("poll_error", &Each::pollError);
        v("matched", &Each::matched);
        v("omitted", &Each::omitted);
        v("unnamed", &Each::unnamed);
    }
};
// assets-export: one file per object; the index lists them all, rows the first.
struct ExportedAsset {
    static constexpr std::string_view TypeName = "ExportedAsset";
    std::string name, className, file;
    RefId ref;
    std::optional<uint64_t> tableRows;
    std::optional<std::string> error;
    template<class V> static void Describe(V& v) {
        v("name", &ExportedAsset::name);
        v("class", &ExportedAsset::className);
        v("ref", &ExportedAsset::ref);
        v("file", &ExportedAsset::file);
        v("table_rows", &ExportedAsset::tableRows);
        v("error", &ExportedAsset::error);
    }
};
struct ExportAssets {
    std::string dir, index;
    uint64_t objects = 0, tables = 0, failed = 0;
    bool capped = false;
    std::vector<ExportedAsset> rows;
    std::optional<uint64_t> omitted;
    template<class V> static void Describe(V& v) {
        v("dir", &ExportAssets::dir);
        v("index", &ExportAssets::index);
        v("exported", &ExportAssets::objects);
        v("tables", &ExportAssets::tables);
        v("failed", &ExportAssets::failed);
        v("capped", &ExportAssets::capped);
        v("rows", &ExportAssets::rows);
        v("omitted", &ExportAssets::omitted);
    }
};
}

inline shape::Object MakeObject(const ue::Object& object) {
    shape::Object row{FullRef(object.ref), object.name, object.className, {}, {}};
    if (object.size) row.classSize = object.size;
    return row;
}

// `outers`: the found objects' outers, described (nullopt where gone).
inline shape::Find MakeFind(const ue::FindResult& found, uint32_t from, std::span<const std::optional<ue::Object>> outers = {}) {
    shape::Find out;
    for (const auto& object : found.objects) {
        auto row = MakeObject(object);
        for (const auto& outer : outers)
            if (outer && outer->ref.address == object.outer.address) {
                row.outer = shape::ObjectOuter{outer->name, FullRef(outer->ref)};
                break;
            }
        out.rows.push_back(std::move(row));
    }
    if (!from) out.total = found.total;
    out.from = from;
    if (found.next) out.nextCursor = found.next;
    return out;
}

inline shape::Names MakeNames(std::span<const int32_t> indices, std::span<const std::string> names) {
    shape::Names out;
    for (size_t i = 0; i < indices.size(); ++i) {
        shape::NameRow row{indices[i], {}};
        if (i < names.size() && !names[i].empty()) row.name = names[i];
        out.rows.push_back(std::move(row));
    }
    return out;
}

inline shape::NameFind MakeNameFind(const ue::NameFindResult& found, uint32_t from) {
    shape::NameFind out;
    for (const auto& name : found.names) out.rows.push_back({name.index, name.name});
    out.from = from;
    if (found.next) out.nextCursor = found.next;
    return out;
}

}
