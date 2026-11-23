// reagentlive CLI value formatting: field values, JSON values and struct expansion.
#include "reagentlive/packs/ue/cli/values/format_ue.h"
#include "reagentlive/packs/ue/cli/values/json_output.h"
#include "reagentlive/frontends/cli/values/format.h"
#include "reagentlive/frontends/cli/values/layout_json.h"
#include "doctest.h"
#include "verbs.h"
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace reagentlive;
using reagentlive::ue::Ref;
using namespace reagentlive::cli;

namespace {
template<class T> std::vector<uint8_t> Bytes(T value) {
    std::vector<uint8_t> bytes(sizeof(T));
    memcpy(bytes.data(), &value, sizeof(T));
    return bytes;
}
}

TEST_CASE("bool values use the bit mask") {
    const std::vector<uint8_t> byte{0x04};
    CHECK(FormatValue("BoolProperty", byte, 0x04) == "true");
    CHECK(FormatValue("BoolProperty", byte, 0x02) == "false");
    CHECK(FormatValue("BoolProperty", byte, 0xff) == "true");
    CHECK(FormatValue("BoolProperty", std::vector<uint8_t>{0}, 0) == "false");
}

TEST_CASE("numbers format by property type") {
    CHECK(FormatValue("FloatProperty", Bytes(1.5f)) == "1.5");
    CHECK(FormatValue("DoubleProperty", Bytes(-2.25)) == "-2.25");
    CHECK(FormatValue("IntProperty", Bytes(int32_t{-7})) == "-7");
    CHECK(FormatValue("UInt32Property", Bytes(uint32_t{4000000000u})) == "4000000000");
    CHECK(FormatValue("Int64Property", Bytes(int64_t{-1})) == "-1");
    CHECK(FormatValue("ByteProperty", Bytes(uint8_t{200})) == "200");
    CHECK(FormatValue("EnumProperty", Bytes(uint32_t{3})) == "3");
}

TEST_CASE("pointers, names and containers show their headers") {
    CHECK(FormatValue("ObjectProperty", Bytes(uint64_t{0x1f3bbcd9d90})) == "0x1f3bbcd9d90");
    CHECK(FormatValue("ObjectProperty", Bytes(uint64_t{0})) == "null");
    struct Name { int32_t index; uint32_t number; };
    CHECK(FormatValue("NameProperty", Bytes(Name{42, 1})) == "FName #42 number 1");
    const auto name = [](int32_t index) -> std::string_view { return index == 42 ? "Tag" : ""; };
    CHECK(FormatValue("NameProperty", Bytes(Name{42, 0}), 0, name) == "Tag");
    CHECK(FormatValue("NameProperty", Bytes(Name{42, 1}), 0, name) == "Tag_0");
    CHECK(FormatValue("NameProperty", Bytes(Name{43, 1}), 0, name) == "FName #43 number 1");
    struct Array { uint64_t data; int32_t num, max; };
    CHECK(FormatValue("ArrayProperty", Bytes(Array{0x1000, 3, 4})) == "TArray num=3 max=4 @0x1000");
    CHECK(FormatValue("StrProperty", Bytes(Array{0, 0, 0})) == "FString num=0 max=0 @null");
}

TEST_CASE("JSON values preserve types and flag bounded previews") {
    const auto noStruct = [](const Ref&) -> const std::vector<ue::Field>* { return nullptr; };
    const auto names = [](int32_t index) -> std::string_view { return index == 42 ? "a\"b\n" : ""; };
    ue::Field field{};
    bool truncated = false;
    field.type = "UInt64Property";
    field.size = 8;
    auto value = JsonFieldValue(field, Bytes(UINT64_MAX), noStruct, names, truncated);
    CHECK(Json::parse(value.dump()).get<uint64_t>() == UINT64_MAX);
    field.type = "NameProperty";
    struct Name { int32_t index; uint32_t number; };
    // Compact: the text with the instance suffix; unresolved: "#index"; meta: the parts.
    value = JsonFieldValue(field, Bytes(Name{42, 1}), noStruct, names, truncated);
    CHECK(Json::parse(value.dump()) == "a\"b\n_0");
    CHECK(JsonFieldValue(field, Bytes(Name{42, 0}), noStruct, names, truncated) == "a\"b\n");
    CHECK(JsonFieldValue(field, Bytes(Name{7, 0}), noStruct, names, truncated) == "#7");
    const ValueStyle meta{true, nullptr};
    value = JsonFieldValue(field, Bytes(Name{42, 1}), noStruct, names, truncated, meta);
    CHECK(value["name"] == "a\"b\n");
    CHECK(value["number"] == 1);
    CHECK(JsonFieldValue(field, Bytes(Name{7, 0}), noStruct, names, truncated, meta)["name"].is_null());
    // A struct of one name (a gameplay tag) is the name; meta keeps its member.
    const Ref tagType{0x7000, 20, 1};
    ue::Field tagName{};
    tagName.name = "TagName";
    tagName.type = "NameProperty";
    tagName.size = 8;
    const std::vector<ue::Field> tagMembers{tagName};
    const auto tagStruct = [&](const Ref& type) -> const std::vector<ue::Field>* { return type == tagType ? &tagMembers : nullptr; };
    ue::Field tag{};
    tag.type = "StructProperty";
    tag.size = 8;
    tag.structType = tagType;
    CHECK(JsonFieldValue(tag, Bytes(Name{42, 0}), tagStruct, names, truncated) == "a\"b\n");
    CHECK(JsonFieldValue(tag, Bytes(Name{42, 0}), tagStruct, names, truncated, meta)["members"][0]["value"]["index"] == 42);
    CHECK_FALSE(truncated);
    field.type = "MapProperty";
    field.size = 32;
    value = JsonFieldValue(field, std::vector<uint8_t>(32), noStruct, names, truncated);
    CHECK(value["byte_count"] == 32);
    CHECK(value["truncated"] == true);
    CHECK(truncated);
}

TEST_CASE("unknown or mismatched types fall back to hex") {
    const std::vector<uint8_t> bytes{1, 2, 0xab};
    CHECK(FormatValue("StructProperty", bytes) == "01 02 ab");
    CHECK(FormatValue("FloatProperty", bytes) == "01 02 ab"); // Wrong size.
    std::vector<uint8_t> big(20, 0xff);
    CHECK(FormatValue("StructProperty", big).ends_with(" ..."));
    CHECK(FormatValue("IntProperty", std::vector<uint8_t>{}).empty());
}

namespace {
struct TestField {
    uint32_t offset, size;
    uint8_t boolMask;
    std::string type, name;
    Ref structType;
};
const Ref VectorType{0x5000, 10, 1}, OuterType{0x6000, 11, 1};
const std::vector<TestField> VectorMembers{
    {0, 8, 0, "DoubleProperty", "X", {}}, {8, 8, 0, "DoubleProperty", "Y", {}}, {16, 8, 0, "DoubleProperty", "Z", {}}};
const std::vector<TestField> OuterMembers{
    {0, 24, 0, "StructProperty", "Location", VectorType}, {24, 1, 0x02, "BoolProperty", "bValid", {}}};
const std::vector<TestField>* Lookup(const Ref& type) {
    if (type == VectorType) return &VectorMembers;
    if (type == OuterType) return &OuterMembers;
    return nullptr;
}
std::vector<uint8_t> Vector(double x, double y, double z) {
    std::vector<uint8_t> bytes = Bytes(x);
    for (double v : {y, z}) { const auto more = Bytes(v); bytes.insert(bytes.end(), more.begin(), more.end()); }
    return bytes;
}
}

TEST_CASE("struct fields expand through their members") {
    const TestField location{0x120, 24, 0, "StructProperty", "Location", VectorType};
    CHECK(FormatField(location, Vector(1.5, -2, 3), Lookup) == "(X 1.5, Y -2, Z 3)");
    auto outer = Vector(1, 2, 3);
    outer.push_back(0x02);
    const TestField hit{0, 25, 0, "StructProperty", "Hit", OuterType};
    CHECK(FormatField(hit, outer, Lookup) == "(Location (X 1, Y 2, Z 3), bValid true)");
}

TEST_CASE("struct expansion falls back to the plain value") {
    const auto bytes = Vector(1, 2, 3);
    const TestField unknown{0, 24, 0, "StructProperty", "Unknown", {0x7000, 12, 1}};
    CHECK(FormatField(unknown, bytes, Lookup) == FormatValue("StructProperty", bytes));
    const TestField noType{0, 24, 0, "StructProperty", "NoType", {}};
    CHECK(FormatField(noType, bytes, Lookup) == FormatValue("StructProperty", bytes));
    const TestField truncated{0, 16, 0, "StructProperty", "Short", VectorType}; // Z outside the bytes.
    CHECK(FormatField(truncated, std::span(bytes).first(16), Lookup) == FormatValue("StructProperty", std::span(bytes).first(16)));
    const TestField scalar{0, 4, 0, "FloatProperty", "Speed", {}};
    CHECK(FormatField(scalar, Bytes(1.5f), Lookup) == "1.5");
}

TEST_CASE("struct expansion bounds nesting and member count") {
    const Ref selfType{0x8000, 13, 1};
    const std::vector<TestField> nested{{0, 8, 0, "StructProperty", "Inner", selfType}};
    const auto selfLookup = [&](const Ref&) { return &nested; };
    const TestField root{0, 8, 0, "StructProperty", "Root", selfType};
    const auto text = FormatField(root, Bytes(uint64_t{0}), selfLookup);
    CHECK(text == "(Inner (Inner (Inner 00 00 00 00 00 00 00 00)))");
    std::vector<TestField> many;
    for (uint32_t i = 0; i < MaxStructMembers + 2; ++i) many.push_back({i, 1, 0, "ByteProperty", "B", {}});
    const auto manyLookup = [&](const Ref&) { return &many; };
    const TestField wide{0, static_cast<uint32_t>(many.size()), 0, "StructProperty", "Wide", selfType};
    CHECK(FormatField(wide, std::vector<uint8_t>(many.size(), 1), manyLookup).ends_with(", ...)"));
}

namespace {
ue::Field Member(uint32_t offset, uint32_t size, std::string type, std::string name, std::string owner, Ref structType = {}) {
    ue::Field field{};
    field.offset = offset;
    field.size = size;
    field.type = std::move(type);
    field.name = std::move(name);
    field.owner = std::move(owner);
    field.structType = structType;
    return field;
}
const std::vector<ue::Field> JsonVector{Member(0, 8, "DoubleProperty", "X", "Vector"),
    Member(8, 8, "DoubleProperty", "Y", "Vector"), Member(16, 8, "DoubleProperty", "Z", "Vector")};
const std::vector<ue::Field> JsonOuter{Member(0, 24, "StructProperty", "Location", "Hit", VectorType),
    Member(24, 4, "FloatProperty", "_aaabq", "Hit")};
const std::vector<ue::Field>* JsonLookup(const Ref& type) {
    if (type == VectorType) return &JsonVector;
    if (type == OuterType) return &JsonOuter;
    return nullptr;
}
std::vector<uint8_t> OuterBytes() {
    auto bytes = Vector(1, 2, 3);
    const auto more = Bytes(0.5f);
    bytes.insert(bytes.end(), more.begin(), more.end());
    return bytes;
}
}

TEST_CASE("struct values are compact by default and full with meta") {
    const format_detail::UnresolvedName names;
    bool truncated = false;
    const auto vector = Member(0x120, 24, "StructProperty", "Location", "Actor", VectorType);
    auto value = JsonFieldValue(vector, Vector(1.5, -2, 3), JsonLookup, names, truncated);
    CHECK(value == Json{{"X", 1.5}, {"Y", -2.0}, {"Z", 3.0}});
    CHECK(value.dump().size() < 40);
    const auto meta = JsonFieldValue(vector, Vector(1.5, -2, 3), JsonLookup, names, truncated, ValueStyle{true, nullptr});
    REQUIRE(meta["members"].size() == 3);
    CHECK(meta["members"][0]["owner"] == "Vector");
    CHECK(meta["members"][2]["offset"] == 16);
    CHECK(meta["members"][2]["value"] == 3.0);
    CHECK_FALSE(truncated);
}

TEST_CASE("nested structs, struct elements and map entries use the compact form") {
    const format_detail::UnresolvedName names;
    bool truncated = false;
    const auto hit = Member(0, 28, "StructProperty", "Hit", "Actor", OuterType);
    const auto value = JsonFieldValue(hit, OuterBytes(), JsonLookup, names, truncated);
    CHECK(value == Json{{"Location", {{"X", 1.0}, {"Y", 2.0}, {"Z", 3.0}}}, {"_aaabq", 0.5}});
    // An array element or a map's value is decoded one level down, as dump previews and entries do.
    auto element = Member(0, 24, "StructProperty", "", "", VectorType);
    CHECK(JsonFieldValue(element, Vector(4, 5, 6), JsonLookup, names, truncated, {}, 1) ==
        Json{{"X", 4.0}, {"Y", 5.0}, {"Z", 6.0}});
    // Readable names from the names file replace obfuscated member names.
    Names labels;
    std::istringstream file("Hit 24 4 0 Spread\n");
    REQUIRE(labels.Load(file) == 0);
    const auto named = JsonFieldValue(hit, OuterBytes(), JsonLookup, names, truncated, ValueStyle{false, &labels});
    CHECK(named["Spread"] == 0.5);
    CHECK_FALSE(named.contains("_aaabq"));
    CHECK_FALSE(truncated);
}

TEST_CASE("compact structs count members past the bound and keep duplicate names apart") {
    const format_detail::UnresolvedName names;
    const Ref wideType{0x9000, 14, 1};
    std::vector<ue::Field> many;
    for (uint32_t i = 0; i < MaxStructMembers + 2; ++i) many.push_back(Member(i, 1, "ByteProperty", "B" + std::to_string(i), "Wide"));
    many[1].name = "B0";
    const auto lookup = [&](const Ref&) { return &many; };
    bool truncated = false;
    const auto wide = Member(0, static_cast<uint32_t>(many.size()), "StructProperty", "W", "Actor", wideType);
    const auto value = JsonFieldValue(wide, std::vector<uint8_t>(many.size(), 1), lookup, names, truncated);
    CHECK(value["..."] == 2);
    CHECK(value.contains("B0@1"));
    CHECK(value.size() == MaxStructMembers + 1);
    CHECK(truncated);
}

TEST_CASE("layouts decode mixed widths, padding, arrays and unnamed fields") {
    auto parsed = layout::Parse("u8 phase, pad 3, f32 duration, f32, f64[2] start, ptr owner");
    REQUIRE(parsed.has_value());
    CHECK(parsed->size == 36);
    CHECK(parsed->fields[2].offset == 4);
    CHECK(parsed->fields[3].offset == 8);
    CHECK(parsed->fields[4].offset == 12);
    std::vector<uint8_t> bytes(36);
    bytes[0] = 2;
    const float duration = 410.85f, elapsed = 12.5f;
    const double start[2] = {-659959.0, -622404.0};
    memcpy(bytes.data() + 4, &duration, 4);
    memcpy(bytes.data() + 8, &elapsed, 4);
    memcpy(bytes.data() + 12, start, 16);
    const auto values = layout::Decode(*parsed, bytes);
    CHECK(values == Json{{"phase", 2}, {"duration", 410.85f}, {"+0x8", 12.5f}, {"start", {-659959.0, -622404.0}},
        {"owner", nullptr}});
    // One element written with [1] is still a list.
    CHECK(layout::Decode(*layout::Parse("u16[1] x"), std::vector<uint8_t>{7, 0}) == Json{{"x", {7}}});
}

TEST_CASE("layouts refuse bad types, counts, names and sizes") {
    CHECK(layout::Parse("u32 a, f16 b").error() == "field 2: unknown type 'f16' (i8 u8 i16 u16 i32 u32 i64 u64 f32 f64 ptr)");
    CHECK_FALSE(layout::Parse("u32[0] a").has_value());
    CHECK_FALSE(layout::Parse("u32[x] a").has_value());
    CHECK_FALSE(layout::Parse("pad").has_value());
    CHECK_FALSE(layout::Parse("u32 a b").has_value());
    CHECK_FALSE(layout::Parse("u32 1a").has_value());
    CHECK(layout::Parse("u32 a, u32 a").error() == "name 'a' twice");
    CHECK_FALSE(layout::Parse("u8[4097] big").has_value());
    CHECK(layout::Parse("f32 Speed?").has_value());
    CHECK_FALSE(layout::ParseField("f32", true).has_value());
}

namespace {
// A wide row as a vendor list holds one: a transform, a tag, a name, a price
// and an array, compact. Its printed size is held below, so a formatting
// change that bloats every value fails here before it floods a reader.
const Ref QuatType{0x8000, 30, 1}, TransformType{0x8001, 31, 1}, TagType{0x8002, 32, 1}, WideType{0x8003, 33, 1};
const std::vector<ue::Field> WideQuat{Member(0, 8, "DoubleProperty", "X", "Quat"), Member(8, 8, "DoubleProperty", "Y", "Quat"),
    Member(16, 8, "DoubleProperty", "Z", "Quat"), Member(24, 8, "DoubleProperty", "W", "Quat")};
const std::vector<ue::Field> WideTransform{Member(0, 32, "StructProperty", "Rotation", "Transform", QuatType),
    Member(32, 24, "StructProperty", "Translation", "Transform", VectorType),
    Member(64, 24, "StructProperty", "Scale3D", "Transform", VectorType)};
const std::vector<ue::Field> WideTag{Member(0, 8, "NameProperty", "TagName", "GameplayTag")};
const std::vector<ue::Field> WideRow{Member(0, 96, "StructProperty", "SpawnTransform", "Row", TransformType),
    Member(96, 8, "StructProperty", "Tag", "Row", TagType), Member(104, 8, "NameProperty", "RowName", "Row"),
    Member(112, 4, "IntProperty", "Price", "Row"), Member(120, 16, "ArrayProperty", "Variations", "Row")};
const std::vector<ue::Field>* WideLookup(const Ref& type) {
    if (type == VectorType) return &JsonVector;
    if (type == QuatType) return &WideQuat;
    if (type == TransformType) return &WideTransform;
    if (type == TagType) return &WideTag;
    if (type == WideType) return &WideRow;
    return nullptr;
}
std::vector<uint8_t> WideBytes() {
    std::vector<uint8_t> bytes(136);
    const double values[] = {0, 0, 0.7071, 0.7071, 1250.5, -310.25, 42, 1, 1, 1};
    memcpy(bytes.data(), values, 32);           // Rotation.
    memcpy(bytes.data() + 32, values + 4, 24);  // Translation.
    memcpy(bytes.data() + 64, values + 7, 24);  // Scale3D.
    const int32_t tag[2] = {42, 0}, name[2] = {43, 3}, price = 1500;
    memcpy(bytes.data() + 96, tag, 8);
    memcpy(bytes.data() + 104, name, 8);
    memcpy(bytes.data() + 112, &price, 4);
    const uint64_t data = 0x7ff612340000;
    const int32_t counts[2] = {2, 4};
    memcpy(bytes.data() + 120, &data, 8);
    memcpy(bytes.data() + 128, counts, 8);
    return bytes;
}
}

TEST_CASE("a wide row prints compact: its size is held") {
    const auto names = [](int32_t index) -> std::string_view {
        return index == 42 ? "Item.Weapon.Rifle.Standard" : index == 43 ? "Row" : "";
    };
    bool truncated = false;
    const auto row = Member(0, 136, "StructProperty", "Item", "Vendor", WideType);
    const auto value = JsonFieldValue(row, WideBytes(), WideLookup, names, truncated);
    CHECK(value["Tag"] == "Item.Weapon.Rifle.Standard");
    CHECK(value["RowName"] == "Row_2");
    CHECK(value["SpawnTransform"]["Scale3D"] == Json{{"X", 1.0}, {"Y", 1.0}, {"Z", 1.0}});
    // Measured at 303 characters; the bound leaves room for a key, not for a
    // new wrapper on every value.
    const auto size = value.dump(-1, ' ', true).size();
    MESSAGE("wide row: " << size << " characters");
    CHECK(size <= 320);
    // meta is the layout, many times larger: the compact form is the default for a reason.
    const auto meta = JsonFieldValue(row, WideBytes(), WideLookup, names, truncated, ValueStyle{true, nullptr});
    CHECK(meta.dump().size() > 3 * size);

    // nonzero leaves out the zero members: the rotation's X and Y here.
    Json pruned = value;
    CHECK_FALSE(DropZero(pruned));
    CHECK(pruned["SpawnTransform"]["Rotation"] == Json{{"Z", 0.7071}, {"W", 0.7071}});
    CHECK(pruned.dump().size() < value.dump().size());
}

TEST_CASE("a float32 prints at float precision") {
    CHECK(JsonNumber<float>(Bytes(2.35f)).dump() == "2.35");
    CHECK(JsonNumber<float>(Bytes(0.1f)).dump() == "0.1");
    CHECK(JsonNumber<float>(Bytes(100000.0f)).dump() == "100000.0");
    CHECK(JsonNumber<double>(Bytes(0.1)).dump() == "0.1");                 // Doubles as they are.
    CHECK(static_cast<float>(JsonNumber<float>(Bytes(1.0f / 3.0f)).get<double>()) == 1.0f / 3.0f);
}

TEST_CASE("nonzero: zero members go, elements and non-zero values stay") {
    Json value = {{"A", 0}, {"B", false}, {"C", ""}, {"D", "None"}, {"E", nullptr}, {"F", Json::array()},
        {"G", {{"X", 0.0}}}, {"H", 1}, {"I", Json::array({0, 2})}, {"J", {{"count", 0}, {"rows", Json::array()}}},
        {"K", {{"count", 1}, {"rows", Json::array({{{"P", 0}, {"Q", 3}}})}}}, {"L", "x"}};
    CHECK_FALSE(DropZero(value));
    CHECK(value == Json{{"H", 1}, {"I", Json::array({0, 2})}, {"K", {{"count", 1}, {"rows", Json::array({{{"Q", 3}}})}}},
        {"L", "x"}});
    Json zero = {{"A", 0}, {"B", {{"C", false}}}};
    CHECK(DropZero(zero));
    CHECK(zero.empty());
    // An element or a whole value the filter emptied says so; one empty to begin with stays empty.
    Json list = Json::array({{{"Radius", 0}, {"Center", {{"X", 0}}}}, {{"Radius", 4}}, Json::object()});
    CHECK_FALSE(DropZero(list));
    CHECK(list == Json::array({{{"zeroed", 2}}, {{"Radius", 4}}, Json::object()}));
    Json whole = {{"A", 0}, {"B", false}};
    KeepNonZero(whole);
    CHECK(whole == Json{{"zeroed", 2}});
    Json scalar = 0;
    KeepNonZero(scalar);
    CHECK(scalar == 0);
}

TEST_CASE("search text shows the printable string around a hit") {
    const std::string ascii = std::string("\x01\x02") + "Projectile.Impact.PenetrationEnter" + std::string(1, 0) + "Next";
    const std::vector<uint8_t> bytes(ascii.begin(), ascii.end());
    const size_t hit = ascii.find("enetrat");
    CHECK(TextAround(bytes, hit, 7, false) == "Projectile.Impact.PenetrationEnter");
    // At most 4 characters each side, the cut ends marked.
    CHECK(TextAround(bytes, hit, 7, false, 4) == "...ct.PenetrationE...");
    CHECK(TextAround(bytes, hit, 7, false, 4).starts_with("..."));
    CHECK(TextAround(bytes, hit, 7, false, 4).ends_with("..."));
    CHECK(TextAround(bytes, 0, 2, false).empty());            // Not inside text.
    std::vector<uint8_t> wide;
    for (const char c : std::string("Hit ImpactVelocity: %f")) { wide.push_back(static_cast<uint8_t>(c)); wide.push_back(0); }
    wide.push_back(0); wide.push_back(0);
    CHECK(TextAround(wide, 8, 12, true) == "Hit ImpactVelocity: %f");
    CHECK(TextAround(wide, 8, 12, false).empty());           // UTF-16 read as ASCII is not text.
}
