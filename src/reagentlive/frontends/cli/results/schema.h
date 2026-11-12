// reagentlive CLI: results as typed structs. A result struct lists its fields
// once (key and member) in Describe; that one list writes the JSON
// (ToJson) and describes it (Of<T>), so a builder cannot emit a key its
// description lacks. The vocabulary is in syntax/verb.h. Pure; tests cover it.
//
//   struct Row {
//       Addr address; std::optional<std::string> name; std::string kind;
//       template<class V> static void Describe(V& v) {
//           v("address", &Row::address);
//           v("name", &Row::name);                  // std::optional: omitted when empty.
//           v("kind", &Row::kind, "code|data");     // A string enum.
//       }
//   };
#pragma once
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include "reagentlive/frontends/cli/syntax/verb.h"
#include "reagentlive/frontends/cli/values/json.h"

namespace reagentlive::cli {
// Hex with 0x0 for zero.
inline std::string HexNumber(uint64_t value) {
    char text[24];
    snprintf(text, sizeof(text), "0x%" PRIx64, value);
    return text;
}

// Values that carry their notation.
struct Addr {       // addr: an address, "0x7ff6...".
    uint64_t value = 0;
    Addr() = default;
    Addr(uint64_t address) : value(address) {}
};
struct HexNum {     // hex: a number shown in hex (rva, offset, size).
    uint64_t value = 0;
    HexNum() = default;
    HexNum(uint64_t number) : value(number) {}
};
struct HexBytes {   // bytes: "01 02 ff", in memory order.
    std::string text;
    HexBytes() = default;
    HexBytes(std::span<const uint8_t> bytes) : text(format_detail::Hex(bytes, bytes.size())) {}
};
struct RefId {      // ref: a literal ref 0x<address>#<index>:<serial>, or an address where the object is unidentified.
    std::string text;
};
inline RefId FullRef(const Ref& object) { return {RefText(object)}; }
// A literal ref when the object is identified, else its address.
inline RefId ReusableRef(const Ref& object) {
    return {object.index >= 0 ? RefText(object) : HexNumber(object.address)};
}
// An address that is absent when zero.
inline std::optional<Addr> NonZero(uint64_t address) { return address ? std::optional<Addr>(address) : std::nullopt; }

namespace schema {
using Kind = Type::Kind;
template<class T> struct Codec;
template<class T> struct IsOptional : std::false_type {};
template<class T> struct IsOptional<std::optional<T>> : std::true_type {};

inline Type Scalar(Kind kind) { Type type; type.kind = kind; return type; }

// A string (or a list of them) that is one of `values`.
inline void SetEnum(Type& type, std::string_view values) {
    if (type.kind == Kind::List && !type.element.empty()) return SetEnum(type.element.front(), values);
    type.kind = Kind::Enum;
    type.values = values;
}

struct Describer {
    Type type = Scalar(Kind::Struct);
    std::string prefix;
    template<class T, class M> void operator()(std::string_view key, M T::*, std::string_view values = {}) {
        Field field{prefix + std::string(key), IsOptional<M>::value, Codec<M>::Describe()};
        if (!values.empty()) SetEnum(field.type, values);
        type.fields.push_back(std::move(field));
    }
    // A nested struct's fields as this struct's own, their keys prefixed.
    template<class T, class M> void flat(std::string_view keyPrefix, M T::*) {
        Describer inner;
        inner.prefix = prefix + std::string(keyPrefix);
        M::Describe(inner);
        for (auto& field : inner.type.fields) type.fields.push_back(std::move(field));
    }
};

template<class T> struct Writer {
    const T& object;
    Json& out;
    std::string prefix;
    template<class M> void operator()(std::string_view key, M T::* member, std::string_view = {}) {
        Codec<M>::Put(out, prefix + std::string(key), object.*member);
    }
    template<class M> void flat(std::string_view keyPrefix, M T::* member) {
        Writer<M> inner{object.*member, out, prefix + std::string(keyPrefix)};
        M::Describe(inner);
    }
};

// Scalars.
template<class T, Kind K> struct ScalarCodec {
    static Type Describe() { return Scalar(K); }
    static Json Write(const T& value) { return value; }
    static void Put(Json& out, const std::string& key, const T& value) { out[key] = value; }
};
template<> struct Codec<bool> : ScalarCodec<bool, Kind::Bool> {};
template<> struct Codec<uint32_t> : ScalarCodec<uint32_t, Kind::U32> {};
template<> struct Codec<uint64_t> : ScalarCodec<uint64_t, Kind::U64> {};
template<> struct Codec<int32_t> : ScalarCodec<int32_t, Kind::I32> {};
template<> struct Codec<int64_t> : ScalarCodec<int64_t, Kind::I64> {};
template<> struct Codec<double> : ScalarCodec<double, Kind::F64> {};
template<> struct Codec<std::string> : ScalarCodec<std::string, Kind::Str> {};
template<> struct Codec<Json> : ScalarCodec<Json, Kind::Value> {};
template<> struct Codec<Addr> {
    static Type Describe() { return Scalar(Kind::Addr); }
    static Json Write(const Addr& value) { return HexNumber(value.value); }
    static void Put(Json& out, const std::string& key, const Addr& value) { out[key] = Write(value); }
};
template<> struct Codec<HexNum> {
    static Type Describe() { return Scalar(Kind::Hex); }
    static Json Write(const HexNum& value) { return HexNumber(value.value); }
    static void Put(Json& out, const std::string& key, const HexNum& value) { out[key] = Write(value); }
};
template<> struct Codec<HexBytes> {
    static Type Describe() { return Scalar(Kind::Bytes); }
    static Json Write(const HexBytes& value) { return value.text; }
    static void Put(Json& out, const std::string& key, const HexBytes& value) { out[key] = value.text; }
};
template<> struct Codec<RefId> {
    static Type Describe() { return Scalar(Kind::Ref); }
    static Json Write(const RefId& value) { return value.text; }
    static void Put(Json& out, const std::string& key, const RefId& value) { out[key] = value.text; }
};

// An optional member: its key is omitted when empty. As a list element it is null.
template<class T> struct Codec<std::optional<T>> {
    static Type Describe() { return Codec<T>::Describe(); }
    static Json Write(const std::optional<T>& value) { return value ? Codec<T>::Write(*value) : Json(nullptr); }
    static void Put(Json& out, const std::string& key, const std::optional<T>& value) {
        if (value) out[key] = Codec<T>::Write(*value);
    }
};
template<class T> struct Codec<std::vector<T>> {
    static Type Describe() {
        Type type = Scalar(Kind::List);
        type.element.push_back(Codec<T>::Describe());
        type.nullable = IsOptional<T>::value;
        return type;
    }
    static Json Write(const std::vector<T>& values) {
        Json list = Json::array();
        for (const auto& value : values) list.push_back(Codec<T>::Write(value));
        return list;
    }
    static void Put(Json& out, const std::string& key, const std::vector<T>& values) { out[key] = Write(values); }
};
template<class T> struct Codec<std::map<std::string, T>> {
    static Type Describe() {
        Type type = Scalar(Kind::Map);
        type.element.push_back(Codec<T>::Describe());
        return type;
    }
    static Json Write(const std::map<std::string, T>& values) {
        Json object = Json::object();
        for (const auto& [key, value] : values) object[key] = Codec<T>::Write(value);
        return object;
    }
    static void Put(Json& out, const std::string& key, const std::map<std::string, T>& values) { out[key] = Write(values); }
};
// A struct with Describe.
template<class T> struct Codec {
    static Type Describe() {
        Describer describer;
        T::Describe(describer);
        return std::move(describer.type);
    }
    static Json Write(const T& value) {
        Json object = Json::object();
        Writer<T> writer{value, object, {}};
        T::Describe(writer);
        return object;
    }
    static void Put(Json& out, const std::string& key, const T& value) { out[key] = Write(value); }
};

// A verb row's `result`: schema::Of<shape::Region>.
template<class T> Type Of() { return Codec<T>::Describe(); }

// An empty object (quit's and other results with no keys).
struct Nothing {
    template<class V> static void Describe(V&) {}
};

// The type for programs (`help --json`): scalars by name, {"enum": [...]},
// {"list": T} (with "nullable": true), {"map": T},
// {"struct": {key: {"type": T, "optional": true}}}.
inline Json TypeJson(const Type& type) {
    switch (type.kind) {
    case Kind::Enum: {
        Json words = Json::array();
        std::string_view rest = type.values;
        while (!rest.empty()) {
            const size_t bar = rest.find('|');
            words.push_back(rest.substr(0, bar));
            rest = bar == std::string_view::npos ? std::string_view{} : rest.substr(bar + 1);
        }
        return {{"enum", std::move(words)}};
    }
    case Kind::List: {
        Json json = {{"list", type.element.empty() ? Json("value") : TypeJson(type.element.front())}};
        if (type.nullable) json["nullable"] = true;
        return json;
    }
    case Kind::Map: return {{"map", type.element.empty() ? Json("value") : TypeJson(type.element.front())}};
    case Kind::Struct: {
        Json fields = Json::object();
        for (const auto& field : type.fields) {
            Json entry = {{"type", TypeJson(field.type)}};
            if (field.optional) entry["optional"] = true;
            fields[field.key] = std::move(entry);
        }
        return {{"struct", std::move(fields)}};
    }
    default: return std::string(ScalarName(type.kind));
    }
}

// Why `json` is not a `type`; empty when it is. `path` names the place.
inline std::string Problem(const Type& type, const Json& json, const std::string& path = "result") {
    const auto wrong = [&](std::string_view wanted) { return path + ": not " + std::string(wanted); };
    switch (type.kind) {
    case Kind::Bool: return json.is_boolean() ? "" : wrong("a bool");
    case Kind::U32: case Kind::U64:
        return json.is_number_unsigned() || (json.is_number_integer() && json.get<int64_t>() >= 0) ? "" : wrong("an unsigned number");
    case Kind::I32: case Kind::I64: return json.is_number_integer() ? "" : wrong("an integer");
    case Kind::F64: return json.is_number() ? "" : wrong("a number");
    case Kind::Str: case Kind::Bytes: case Kind::Ref: return json.is_string() ? "" : wrong("a string");
    case Kind::Addr: case Kind::Hex:
        return json.is_string() && json.get_ref<const std::string&>().starts_with("0x") ? "" : wrong("0x text");
    case Kind::Value: return "";
    case Kind::Enum: {
        if (!json.is_string()) return wrong("a string");
        const auto& word = json.get_ref<const std::string&>();
        std::string_view rest = type.values;
        while (!rest.empty()) {
            const size_t bar = rest.find('|');
            const auto item = rest.substr(0, bar);
            if (item == word || item == "...") return "";
            rest = bar == std::string_view::npos ? std::string_view{} : rest.substr(bar + 1);
        }
        return path + ": '" + word + "' is not one of " + type.values;
    }
    case Kind::List: {
        if (!json.is_array()) return wrong("a list");
        for (size_t i = 0; i < json.size() && !type.element.empty(); ++i) {
            if (json[i].is_null() && type.nullable) continue;
            if (auto problem = Problem(type.element.front(), json[i], path + "[" + std::to_string(i) + "]"); !problem.empty())
                return problem;
        }
        return "";
    }
    case Kind::Map: {
        if (!json.is_object()) return wrong("an object");
        for (const auto& [key, value] : json.items())
            if (auto problem = type.element.empty() ? std::string() : Problem(type.element.front(), value, path + "." + key);
                !problem.empty()) return problem;
        return "";
    }
    case Kind::Struct: {
        if (!json.is_object()) return wrong("an object");
        for (const auto& [key, value] : json.items()) {
            const auto field = std::ranges::find(type.fields, key, &Field::key);
            if (field == type.fields.end()) return path + ": unknown key '" + key + "'";
            if (auto problem = Problem(field->type, value, path + "." + key); !problem.empty()) return problem;
        }
        for (const auto& field : type.fields)
            if (!field.optional && !json.contains(field.key)) return path + ": missing key '" + field.key + "'";
        return "";
    }
    }
    return "";
}
}

// The result object of a typed result.
template<class T> Json ToJson(const T& value) { return schema::Codec<T>::Write(value); }
}
