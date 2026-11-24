// reagentlive CLI, Unreal pack: result structs (results/schema.h) of the
// fields: a field's layout, get, entries, rows, label, layout-export and layout-check.
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

// A reflected field's layout, as get with `mode` `types`, get with `meta` and func's params list it.
struct FieldInfo {
    static constexpr std::string_view TypeName = "ue.FieldInfo";
    std::string name, owner, type;
    HexNum offset;
    uint32_t size = 0;
    std::optional<std::string> structName;
    std::optional<uint32_t> boolMask;
    std::optional<std::string> elementType;
    std::optional<uint32_t> elementSize;
    std::optional<std::string> valueType;
    std::optional<uint32_t> valueSize;
    std::optional<HexNum> valueOffset;
    std::optional<std::string> valueStruct;
    std::optional<uint32_t> stride;
    // The class an object-like property points to or an enum or byte
    // property's enum (a container's element or key); a map value's.
    std::optional<std::string> subtype, valueSubtype;
    template<class V> static void Describe(V& v) {
        v("name", &FieldInfo::name);
        v("owner", &FieldInfo::owner);
        v("type", &FieldInfo::type);
        v("offset", &FieldInfo::offset);
        v("size", &FieldInfo::size);
        v("struct", &FieldInfo::structName);
        v("bool_mask", &FieldInfo::boolMask);
        v("element_type", &FieldInfo::elementType);
        v("element_size", &FieldInfo::elementSize);
        v("value_type", &FieldInfo::valueType);
        v("value_size", &FieldInfo::valueSize);
        v("value_offset", &FieldInfo::valueOffset);
        v("value_struct", &FieldInfo::valueStruct);
        v("stride", &FieldInfo::stride);
        v("subtype", &FieldInfo::subtype);
        v("value_subtype", &FieldInfo::valueSubtype);
    }
};
struct NativeRefused {
    uint64_t line = 0;
    std::string name, owner;
    HexNum offset;
    std::string reason;
    template<class V> static void Describe(V& v) {
        v("line", &NativeRefused::line);
        v("name", &NativeRefused::name);
        v("owner", &NativeRefused::owner);
        v("offset", &NativeRefused::offset);
        v("reason", &NativeRefused::reason);
    }
};
// One previewed element: an array's or set's `value`, a map's `key` and
// `value`, each with the object a pointer names.
struct PreviewElement {
    std::optional<Json> key;
    Json value;
    std::optional<Object> object, keyObject, valueObject;
    template<class V> static void Describe(V& v) {
        v("key", &PreviewElement::key);
        v("value", &PreviewElement::value);
        v("object", &PreviewElement::object);
        v("key_object", &PreviewElement::keyObject);
        v("value_object", &PreviewElement::valueObject);
    }
};
struct Preview {
    uint64_t total = 0;
    std::vector<PreviewElement> elements;
    template<class V> static void Describe(V& v) {
        v("total", &Preview::total);
        v("elements", &Preview::elements);
    }
};
// get's row, one type for its three modes:
// - every field (no paths): name (the readable one where recovered, `raw`
//   the reflected one beside it), offset, value; `before` with `mode` `changed`,
//   `object` for a pointer, `preview` for a container; the layout is `meta`
//   with `meta`.
// - paths: `path` as asked, value, `address` (with module and rva), `holder`
//   when a pointer led there; `type` and `hex` with `meta`; a failed path
//   keeps its place with `error`.
// - `mode` `types`: name, raw, owner, type, offset, size and the container's parts; no value.
// `native` marks a member from the native file (`stale`: labelled under
// another build); `truncated` only when true.
struct GetRow {
    std::optional<std::string> path, name, raw, owner, type;
    std::optional<HexNum> offset;
    std::optional<uint32_t> size;
    std::optional<std::string> structName;
    std::optional<uint32_t> boolMask;
    std::optional<std::string> elementType;
    std::optional<uint32_t> elementSize;
    std::optional<std::string> valueType;
    std::optional<uint32_t> valueSize;
    std::optional<HexNum> valueOffset;
    std::optional<std::string> valueStruct;
    std::optional<uint32_t> stride;
    std::optional<std::string> subtype, valueSubtype;   // As FieldInfo's.
    std::optional<bool> native, stale;
    std::optional<Json> value, before;
    std::optional<bool> truncated;
    std::optional<Addr> address;
    Location where;
    std::optional<RefId> holder;
    Location valueWhere;
    std::optional<Object> object;
    std::optional<Preview> preview;
    std::optional<std::string> previewError;
    std::optional<FieldInfo> meta;
    std::optional<HexBytes> hex;
    std::optional<std::string> error;
    template<class V> static void Describe(V& v) {
        v("path", &GetRow::path);
        v("name", &GetRow::name);
        v("raw", &GetRow::raw);
        v("owner", &GetRow::owner);
        v("type", &GetRow::type);
        v("offset", &GetRow::offset);
        v("size", &GetRow::size);
        v("struct", &GetRow::structName);
        v("bool_mask", &GetRow::boolMask);
        v("element_type", &GetRow::elementType);
        v("element_size", &GetRow::elementSize);
        v("value_type", &GetRow::valueType);
        v("value_size", &GetRow::valueSize);
        v("value_offset", &GetRow::valueOffset);
        v("value_struct", &GetRow::valueStruct);
        v("subtype", &GetRow::subtype);
        v("value_subtype", &GetRow::valueSubtype);
        v("stride", &GetRow::stride);
        v("native", &GetRow::native);
        v("stale", &GetRow::stale);
        v("value", &GetRow::value);
        v("before", &GetRow::before);
        v("truncated", &GetRow::truncated);
        v("address", &GetRow::address);
        v.flat("", &GetRow::where);
        v("holder", &GetRow::holder);
        v.flat("value_", &GetRow::valueWhere);
        v("object", &GetRow::object);
        v("preview", &GetRow::preview);
        v("preview_error", &GetRow::previewError);
        v("meta", &GetRow::meta);
        v("hex", &GetRow::hex);
        v("error", &GetRow::error);
    }
};
struct Filtered {
    uint64_t shown = 0, total = 0;
    template<class V> static void Describe(V& v) {
        v("shown", &Filtered::shown);
        v("total", &Filtered::total);
    }
};
struct SnapInfo {
    uint64_t size = 0;
    std::optional<std::string> path;
    template<class V> static void Describe(V& v) {
        v("size", &SnapInfo::size);
        v("path", &SnapInfo::path);
    }
};
// get: `same_tick` (paths) covers the value reads, not the lookups before
// them; `truncated` and `capped` (every field, `mode` `types`) say whether the rows
// are complete; `snap` answers `mode` `snap`.
struct Get {
    RefId ref;
    std::vector<GetRow> rows;
    std::optional<bool> sameTick, truncated, capped;
    std::optional<Filtered> filtered;
    std::optional<std::vector<NativeRefused>> nativeRefused;
    std::optional<SnapInfo> snap;
    std::optional<bool> tickAccurate;
    std::optional<uint32_t> unnamed;    // Distinct pointers past the lookup cap, left as addresses.
    template<class V> static void Describe(V& v) {
        v("ref", &Get::ref);
        v("rows", &Get::rows);
        v("same_tick", &Get::sameTick);
        v("truncated", &Get::truncated);
        v("capped", &Get::capped);
        v("filtered", &Get::filtered);
        v("native_refused", &Get::nativeRefused);
        v("snap", &Get::snap);
        v("tick_accurate", &Get::tickAccurate);
        v("unnamed", &Get::unnamed);
    }
};

// entries: one page of an array, set or map.
struct EntryRow {
    std::optional<Json> key;
    Json value;
    std::optional<Object> keyObject, valueObject;
    template<class V> static void Describe(V& v) {
        v("key", &EntryRow::key);
        v("value", &EntryRow::value);
        v("key_object", &EntryRow::keyObject);
        v("value_object", &EntryRow::valueObject);
    }
};
struct Entries {
    RefId ref;
    std::string field, kind;
    uint64_t total = 0;
    std::vector<EntryRow> rows;
    uint32_t from = 0;
    std::optional<uint32_t> nextCursor;
    bool truncated = false;
    std::optional<uint32_t> unnamed;    // Distinct pointers past the lookup cap, left as addresses.
    std::optional<bool> tickAccurate;
    template<class V> static void Describe(V& v) {
        v("ref", &Entries::ref);
        v("field", &Entries::field);
        v("kind", &Entries::kind, "array|set|map");
        v("total", &Entries::total);
        v("rows", &Entries::rows);
        v("from", &Entries::from);
        v("next_cursor", &Entries::nextCursor);
        v("truncated", &Entries::truncated);
        v("unnamed", &Entries::unnamed);
        v("tick_accurate", &Entries::tickAccurate);
    }
};

// rows: a data table's rows, decoded with its RowStruct.
// A data table's row (value: its RowStruct decoded), or a curve table's
// curve (keys; value only with `at`, the curve evaluated there).
struct TableRow {
    std::string name;
    Addr address;
    std::optional<Json> value;
    std::optional<std::string> interp;  // A simple curve's interp mode: linear, constant, cubic, none.
    std::optional<Json> keys;           // [t, v] (rich: [t, v, interp, arrive, leave]) per key.
    std::optional<bool> weighted;       // Rich keys with weighted tangents; value evaluates them unweighted.
    template<class V> static void Describe(V& v) {
        v("name", &TableRow::name);
        v("address", &TableRow::address);
        v("value", &TableRow::value);
        v("interp", &TableRow::interp, "linear|constant|cubic|none");
        v("keys", &TableRow::keys);
        v("weighted", &TableRow::weighted);
    }
};
struct Rows {
    RefId ref;
    std::optional<std::string> rowStruct;   // A data table.
    std::optional<RefId> rowStructRef;
    std::optional<std::string> curve;       // A curve table: simple or rich.
    std::optional<double> at;               // `at`: the time each value is for.
    uint64_t total = 0;
    std::vector<TableRow> rows;
    uint32_t from = 0;
    std::optional<uint32_t> nextCursor;
    bool truncated = false;
    std::optional<uint32_t> unnamed;    // Distinct pointers past the lookup cap, left as addresses.
    template<class V> static void Describe(V& v) {
        v("ref", &Rows::ref);
        v("row_struct", &Rows::rowStruct);
        v("row_struct_ref", &Rows::rowStructRef);
        v("curve", &Rows::curve, "simple|rich");
        v("at", &Rows::at);
        v("total", &Rows::total);
        v("rows", &Rows::rows);
        v("from", &Rows::from);
        v("next_cursor", &Rows::nextCursor);
        v("truncated", &Rows::truncated);
        v("unnamed", &Rows::unnamed);
    }
};

// label: the member as the native file now holds it.
struct NativeLabel {
    std::string path;
    uint64_t line = 0;
    std::string owner;
    HexNum offset;
    std::string type, name;
    uint32_t size = 0, timestamp = 0;
    template<class V> static void Describe(V& v) {
        v("path", &NativeLabel::path);
        v("line", &NativeLabel::line);
        v("owner", &NativeLabel::owner);
        v("offset", &NativeLabel::offset);
        v("type", &NativeLabel::type);
        v("name", &NativeLabel::name);
        v("size", &NativeLabel::size);
        v("timestamp", &NativeLabel::timestamp);
    }
};
// layout-export: the reflected fields each named class or struct declares,
// one JSON line each in the file; names that are no class or struct.
struct LayoutExport {
    std::string path;
    uint64_t classes = 0, structs = 0, enums = 0, fields = 0;
    std::optional<std::vector<std::string>> missing;
    std::optional<bool> capped;     // `closure` stopped at the type cap.
    template<class V> static void Describe(V& v) {
        v("path", &LayoutExport::path);
        v("classes", &LayoutExport::classes);
        v("structs", &LayoutExport::structs);
        v("enums", &LayoutExport::enums);
        v("fields", &LayoutExport::fields);
        v("missing", &LayoutExport::missing);
        v("capped", &LayoutExport::capped);
    }
};
// layout-check: only the lines the live layout does not hold.
struct LayoutField {
    HexNum offset;
    uint32_t size = 0;
    std::string type;
    template<class V> static void Describe(V& v) {
        v("offset", &LayoutField::offset);
        v("size", &LayoutField::size);
        v("type", &LayoutField::type);
    }
};
struct LayoutMismatch {
    std::string owner, field, problem;
    LayoutField expected;
    std::optional<LayoutField> found;
    template<class V> static void Describe(V& v) {
        v("owner", &LayoutMismatch::owner);
        v("field", &LayoutMismatch::field);
        v("problem", &LayoutMismatch::problem, "no class|no field|offset|size|type");
        v("expected", &LayoutMismatch::expected);
        v("found", &LayoutMismatch::found);
    }
};
struct LayoutCheck {
    std::string path;
    uint64_t classes = 0, checked = 0, mismatched = 0;
    bool ok = false;
    std::vector<LayoutMismatch> rows;
    bool capped = false;
    template<class V> static void Describe(V& v) {
        v("path", &LayoutCheck::path);
        v("classes", &LayoutCheck::classes);
        v("checked", &LayoutCheck::checked);
        v("mismatched", &LayoutCheck::mismatched);
        v("ok", &LayoutCheck::ok);
        v("rows", &LayoutCheck::rows);
        v("capped", &LayoutCheck::capped);
    }
};
}

inline shape::FieldInfo MakeFieldInfo(const ue::Field& field) {
    shape::FieldInfo info;
    info.name = field.name;
    info.owner = field.owner;
    info.type = field.type;
    info.offset = field.offset;
    info.size = field.size;
    if (!field.structName.empty()) info.structName = field.structName;
    if (field.boolMask) info.boolMask = field.boolMask;
    if (field.elementSize) {
        info.elementType = field.elementType;
        info.elementSize = field.elementSize;
    }
    if (field.valueSize) {
        info.valueType = field.valueType;
        info.valueSize = field.valueSize;
        info.valueOffset = field.valueOffset;
        if (!field.valueStructName.empty()) info.valueStruct = field.valueStructName;
    }
    if (field.stride) info.stride = field.stride;
    if (!field.subtype.empty()) info.subtype = field.subtype;
    if (!field.valueSubtype.empty()) info.valueSubtype = field.valueSubtype;
    return info;
}

// A member from the native file as a field's layout.
inline shape::FieldInfo MakeFieldInfo(const NativeMember& member) {
    shape::FieldInfo info;
    info.name = member.field.name;
    info.owner = member.owner;
    info.type = NativeType(member.field);
    info.offset = member.field.offset;
    info.size = static_cast<uint32_t>(member.field.Size());
    return info;
}

// get with `mode` `types`: a row from a field's layout; `readable` is the recovered
// name, shown as name with the reflected one as raw.
inline shape::GetRow MakeTypeRow(const shape::FieldInfo& info, std::string_view readable) {
    shape::GetRow row;
    row.name = readable.empty() ? info.name : std::string(readable);
    if (!readable.empty() && readable != info.name) row.raw = info.name;
    row.owner = info.owner;
    row.type = info.type;
    row.offset = info.offset;
    row.size = info.size;
    row.structName = info.structName;
    row.boolMask = info.boolMask;
    row.elementType = info.elementType;
    row.elementSize = info.elementSize;
    row.valueType = info.valueType;
    row.valueSize = info.valueSize;
    row.valueOffset = info.valueOffset;
    row.valueStruct = info.valueStruct;
    row.stride = info.stride;
    row.subtype = info.subtype;
    row.valueSubtype = info.valueSubtype;
    return row;
}

inline shape::GetRow MakeNativeTypeRow(const NativeMember& member, bool stale) {
    auto row = MakeTypeRow(MakeFieldInfo(member), {});
    row.native = true;
    if (stale) row.stale = true;
    return row;
}

// get with `mode` `types`: `label(field)` is the readable name shown beside an obfuscated one.
template<class LabelOf>
shape::Get MakeTypes(const Ref& ref, std::span<const ue::Field> sorted, const LabelOf& label) {
    shape::Get out;
    out.ref = ReusableRef(ref);
    for (const auto& field : sorted) out.rows.push_back(MakeTypeRow(MakeFieldInfo(field), label(field)));
    out.capped = sorted.size() >= ue::MaxFieldStart;
    return out;
}

// Field rows (reflected and native) in offset order.
inline void SortRowsByOffset(std::vector<shape::GetRow>& rows) {
    const auto offset = [](const shape::GetRow& row) { return row.offset ? row.offset->value : 0; };
    std::stable_sort(rows.begin(), rows.end(), [&](const auto& a, const auto& b) { return offset(a) < offset(b); });
}

inline std::vector<shape::NativeRefused> MakeNativeRefused(std::span<const NativeCheck> refused) {
    std::vector<shape::NativeRefused> rows;
    for (const auto& check : refused)
        rows.push_back({check.member->line, check.member->field.name, check.member->owner, check.member->field.offset,
            check.problem});
    return rows;
}

}
