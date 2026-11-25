// reagentlive CLI results: the typed result structs (results/schema.h) and the
// values their builders give (the verb table naming them: table_tests.cpp). Builders take
// canned client results, so no agent or object table is needed. Every result
// built here is also checked against its own described type.
#include "reagentlive/packs/ue/cli/results/call_text.h"
#include "reagentlive/packs/ue/cli/results/census.h"
#include "reagentlive/packs/ue/cli/results/layout.h"
#include "reagentlive/frontends/cli/results/calls.h"
#include "reagentlive/frontends/cli/results/labels.h"
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/packs/ue/cli/results/result_ue_objects.h"
#include "reagentlive/packs/ue/cli/results/result_ue_fields.h"
#include "reagentlive/packs/ue/cli/results/result_ue_calls.h"
#include "reagentlive/packs/ue/cli/results/result_ue_code.h"
#include "reagentlive/packs/ue/cli/values/pack_input.h"
#include "reagentlive/packs/ue/cli/values/watch_field.h"
#include "doctest.h"
#include "verbs.h"
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace reagentlive;
using namespace reagentlive::cli;

namespace {
const Ref Pawn{0x1000, 7, 3};

ue::Field Field(std::string name, uint32_t offset, uint32_t size, std::string type) {
    ue::Field field{};
    field.name = std::move(name);
    field.owner = "Pawn";
    field.offset = offset;
    field.size = size;
    field.type = std::move(type);
    return field;
}
}

TEST_CASE("find and describe results") {
    const ue::FindResult found{{{Pawn, "Pawn_0", "Pawn"}}, 1, 25};
    const auto first = Checked(MakeFind(found, 0));
    CHECK(first["rows"][0] == Json{{"ref", "0x1000#7:3"}, {"name", "Pawn_0"}, {"class", "Pawn"}});
    CHECK(first["total"] == 1);
    CHECK(first["next_cursor"] == 25);
    const auto later = Checked(MakeFind(found, 25));
    CHECK(later["total"] == 1);   // Every page carries the total.
    CHECK(later["from"] == 25);
    const ue::FindResult last{{{Pawn, "Pawn_0", "Pawn"}}, 1, 0};
    CHECK_FALSE(Checked(MakeFind(last, 0)).contains("next_cursor")); // Done: no cursor.
    const ue::Object described{Pawn, "Pawn_0", "Pawn", 0x4f0}; // A single describe knows the class size.
    CHECK(Checked(MakeObject(described))["class_size"] == 0x4f0);
    CHECK_FALSE(Checked(MakeObject(found.objects[0])).contains("class_size"));
}

TEST_CASE("funcs lists functions with owner and name") {
    const ue::FunctionList list{{"Pawn:GetSpeed", "Actor:K2_GetActorLocation", "Bare", "Owner:GetData"},
        {false, false, false, true}, 0};
    const auto result = Checked(MakeFuncs(Pawn, list, 0));
    REQUIRE(result["rows"].size() == 4);
    CHECK(result["rows"][1] == Json{{"owner", "Actor"}, {"name", "K2_GetActorLocation"}});
    CHECK(result["rows"][2] == Json{{"name", "Bare"}});
    // A native interface's function is marked: only on the interface class.
    CHECK(result["rows"][3] == Json{{"owner", "Owner"}, {"name", "GetData"}, {"via", "interface"}});
    CHECK_FALSE(result.contains("next_cursor"));
}

namespace {
ue::Field Member(std::string name, std::string type, uint32_t offset, uint32_t size) {
    ue::Field f{};
    f.name = std::move(name); f.type = std::move(type); f.offset = offset; f.size = size;
    return f;
}
}
TEST_CASE("typed call inputs pack scalars and plain structs") {
    namespace pi = pack_input;
    std::vector<uint8_t> b(8);
    auto span = [&](size_t n) { std::fill(b.begin(), b.end(), 0xcc); return std::span(b).first(n); };
    CHECK(pi::PackScalar(Member("F", "FloatProperty", 0, 4), "1.5", span(4)));
    float f; memcpy(&f, b.data(), 4); CHECK(f == 1.5f);
    CHECK(pi::PackScalar(Member("D", "DoubleProperty", 0, 8), "-2.25e3", span(8)));
    double d; memcpy(&d, b.data(), 8); CHECK(d == -2250.0);
    CHECK_FALSE(pi::PackScalar(Member("F", "FloatProperty", 0, 4), "1e39", span(4)));
    CHECK_FALSE(pi::PackScalar(Member("F", "FloatProperty", 0, 4), "nan", span(4)));
    CHECK(pi::PackScalar(Member("I", "IntProperty", 0, 4), "-7", span(4)));
    int32_t i; memcpy(&i, b.data(), 4); CHECK(i == -7);
    CHECK_FALSE(pi::PackScalar(Member("I", "Int8Property", 0, 1), "128", span(1)));
    CHECK(pi::PackScalar(Member("I", "Int8Property", 0, 1), "-128", span(1)));
    CHECK(pi::PackScalar(Member("U", "UInt16Property", 0, 2), "0xffff", span(2)));
    CHECK_FALSE(pi::PackScalar(Member("U", "UInt16Property", 0, 2), "65536", span(2)));
    CHECK_FALSE(pi::PackScalar(Member("U", "UInt32Property", 0, 4), "-1", span(4)));
    CHECK(pi::PackScalar(Member("E", "EnumProperty", 0, 1), "3", span(1)));
    CHECK(b[0] == 3);
    // A bitfield bool sets only its mask; a native bool (mask 0xff) the byte.
    auto flag = Member("B", "BoolProperty", 0, 1);
    flag.boolMask = 0x04;
    std::fill(b.begin(), b.end(), 0);
    CHECK(pi::PackScalar(flag, "true", std::span(b).first(1)));
    CHECK(b[0] == 0x04);
    flag.boolMask = 0xff;
    CHECK(pi::PackScalar(flag, "false", std::span(b).first(1)));
    CHECK(b[0] == 0);
    CHECK_FALSE(pi::PackScalar(flag, "yes", std::span(b).first(1)));
    CHECK_FALSE(pi::PackScalar(Member("S", "StrProperty", 0, 16), "x", span(8)));

    // FVector of doubles; a struct holding one nests.
    const Ref vectorType{0x1000, 1, 0}, outerType{0x2000, 2, 0};
    const std::vector<ue::Field> vector{Member("X", "DoubleProperty", 0, 8), Member("Y", "DoubleProperty", 8, 8),
        Member("Z", "DoubleProperty", 16, 8)};
    auto inner = Member("Where", "StructProperty", 8, 24);
    inner.structType = vectorType;
    const std::vector<ue::Field> outer{Member("Scale", "FloatProperty", 0, 4), inner};
    const auto fieldsOf = [&](const Ref& type) -> const std::vector<ue::Field>* {
        return type == vectorType ? &vector : type == outerType ? &outer : nullptr;
    };
    const auto noNames = [](std::string_view, int32_t&, uint32_t&) { return false; };
    auto location = Member("Location", "StructProperty", 0, 24);
    location.structType = vectorType;
    std::vector<uint8_t> block(32, 0);
    double xyz[3];
    CHECK(pi::PackValue(location, "(X=1, Y=-2.5, Z=3)", std::span(block).first(24), fieldsOf, noNames));
    memcpy(xyz, block.data(), 24);
    CHECK((xyz[0] == 1 && xyz[1] == -2.5 && xyz[2] == 3));
    CHECK(pi::PackValue(location, "(4,5,6)", std::span(block).first(24), fieldsOf, noNames));
    memcpy(xyz, block.data(), 24);
    CHECK((xyz[0] == 4 && xyz[1] == 5 && xyz[2] == 6));
    CHECK(pi::PackValue(location, "(Z=9)", std::span(block).first(24), fieldsOf, noNames)); // Others keep their bytes.
    memcpy(xyz, block.data(), 24);
    CHECK((xyz[0] == 4 && xyz[2] == 9));
    CHECK_FALSE(pi::PackValue(location, "(1,2,3,4)", std::span(block).first(24), fieldsOf, noNames));
    CHECK_FALSE(pi::PackValue(location, "(W=1)", std::span(block).first(24), fieldsOf, noNames));
    CHECK_FALSE(pi::PackValue(location, "(X=1, X=2)", std::span(block).first(24), fieldsOf, noNames));
    CHECK_FALSE(pi::PackValue(location, "(1,2", std::span(block).first(24), fieldsOf, noNames));
    CHECK_FALSE(pi::PackValue(location, "1,2,3", std::span(block).first(24), fieldsOf, noNames));
    auto nested = Member("Box", "StructProperty", 0, 32);
    nested.structType = outerType;
    std::fill(block.begin(), block.end(), 0);
    CHECK(pi::PackValue(nested, "(Scale=2, Where=(7, 8, 9))", std::span(block), fieldsOf, noNames));
    float scale; memcpy(&scale, block.data(), 4); memcpy(xyz, block.data() + 8, 24);
    CHECK((scale == 2 && xyz[0] == 7 && xyz[2] == 9));
    CHECK(pi::PackValue(nested, "(3, (1, 1, 1))", std::span(block), fieldsOf, noNames));
    auto unknown = Member("Other", "StructProperty", 0, 8);
    unknown.structType = {0x3000, 3, 0};
    CHECK_FALSE(pi::PackValue(unknown, "(1)", std::span(block).first(8), fieldsOf, noNames));
}

TEST_CASE("typed call inputs take names, and a tag as its name alone") {
    namespace pi = pack_input;
    std::string asked;
    const auto pool = [&](std::string_view text, int32_t& index, uint32_t& number) {
        asked = text;
        if (text != "Some.Tag.Name") return false;
        index = 0x1234;
        number = 0;
        return true;
    };
    const Ref tagType{0x4000, 4, 0}, holderType{0x5000, 5, 0};
    const std::vector<ue::Field> tag{Member("TagName", "NameProperty", 0, 8)};
    auto tagMember = Member("Tag", "StructProperty", 8, 8);
    tagMember.structType = tagType;
    const std::vector<ue::Field> holder{Member("Count", "IntProperty", 0, 4), tagMember};
    const auto fieldsOf = [&](const Ref& type) -> const std::vector<ue::Field>* {
        return type == tagType ? &tag : type == holderType ? &holder : nullptr;
    };
    std::vector<uint8_t> block(16, 0xcc);
    const auto nameAt = [&](size_t at) { int32_t i; uint32_t n; memcpy(&i, &block[at], 4); memcpy(&n, &block[at + 4], 4);
        return std::pair{i, n}; };
    // A name parameter, a tag parameter by its name, and a tag inside a struct.
    CHECK(pi::PackValue(Member("Name", "NameProperty", 0, 8), "Some.Tag.Name", std::span(block).first(8), fieldsOf, pool));
    CHECK(nameAt(0) == std::pair{0x1234, 0u});
    auto tagParam = Member("ResourceTag", "StructProperty", 0, 8);
    tagParam.structType = tagType;
    std::fill(block.begin(), block.end(), 0xcc);
    CHECK(pi::PackValue(tagParam, " Some.Tag.Name ", std::span(block).first(8), fieldsOf, pool));
    CHECK(nameAt(0) == std::pair{0x1234, 0u});
    CHECK(pi::PackValue(tagParam, "(TagName=Some.Tag.Name)", std::span(block).first(8), fieldsOf, pool));
    auto holderParam = Member("Entry", "StructProperty", 0, 16);
    holderParam.structType = holderType;
    std::fill(block.begin(), block.end(), 0);
    CHECK(pi::PackValue(holderParam, "(Count=2, Tag=Some.Tag.Name)", std::span(block), fieldsOf, pool));
    CHECK(nameAt(8) == std::pair{0x1234, 0u});
    // A name the pool lacks is refused, and the resolver saw its exact text.
    CHECK_FALSE(pi::PackValue(tagParam, "Missing.Tag", std::span(block).first(8), fieldsOf, pool));
    CHECK(asked == "Missing.Tag");
    CHECK_FALSE(pi::PackValue(tagParam, "", std::span(block).first(8), fieldsOf, pool));
    // Only a struct of one name takes a bare value.
    auto vectorish = Member("Holder", "StructProperty", 0, 16);
    vectorish.structType = holderType;
    CHECK_FALSE(pi::PackValue(vectorish, "Some.Tag.Name", std::span(block), fieldsOf, pool));
}

TEST_CASE("a watched field takes its element type from reflection") {
    using watch_field::FieldType;
    CHECK(WatchTypeName(FieldType(Member("Health", "DoubleProperty", 0, 8), nullptr)) == "f64"); // A Blueprint float.
    CHECK(WatchTypeName(FieldType(Member("Speed", "FloatProperty", 0, 4), nullptr)) == "f32");
    CHECK(WatchTypeName(FieldType(Member("Count", "IntProperty", 0, 4), nullptr)) == "i32");
    CHECK(WatchTypeName(FieldType(Member("Mode", "EnumProperty", 0, 1), nullptr)) == "u8");
    CHECK(WatchTypeName(FieldType(Member("bOn", "BoolProperty", 0, 1), nullptr)) == "u8");
    CHECK(WatchTypeName(FieldType(Member("Owner", "ObjectProperty", 0, 8), nullptr)) == "ptr");
    CHECK(FieldType(Member("Name", "NameProperty", 0, 8), nullptr) == 0);     // Raw bytes.
    CHECK(FieldType(Member("Odd", "FloatProperty", 0, 8), nullptr) == 0);     // Size disagrees with the type.
    const std::vector<ue::Field> vector{Member("X", "DoubleProperty", 0, 8), Member("Y", "DoubleProperty", 8, 8),
        Member("Z", "DoubleProperty", 16, 8)};
    CHECK(WatchTypeName(FieldType(Member("Location", "StructProperty", 0, 24), &vector)) == "f64");
    const std::vector<ue::Field> mixed{Member("A", "FloatProperty", 0, 4), Member("B", "IntProperty", 4, 4)};
    CHECK(FieldType(Member("Pair", "StructProperty", 0, 8), &mixed) == 0);
    const std::vector<ue::Field> gapped{Member("A", "FloatProperty", 0, 4), Member("B", "FloatProperty", 8, 4)};
    CHECK(FieldType(Member("Gap", "StructProperty", 0, 12), &gapped) == 0);
    CHECK(FieldType(Member("Unknown", "StructProperty", 0, 24), nullptr) == 0);
}

TEST_CASE("a const reference parameter is an input") {
    CHECK(ue::InputParameter(ue::Param));
    CHECK_FALSE(ue::InputParameter(ue::Param | ue::Output));
    CHECK_FALSE(ue::InputParameter(ue::Param | ue::Output | ue::Reference)); // in/out ref: filled by the call.
    CHECK(ue::InputParameter(ue::Param | ue::Output | ue::Reference | ue::Const)); // const FVector&
    CHECK_FALSE(ue::InputParameter(ue::Param | ue::Output | ue::Return | ue::Reference | ue::Const));
}

TEST_CASE("a typed watch range is summarised per value") {
    std::vector<shape::WatchRange> ranges(3);
    ranges[0].type = "f64";
    ranges[1].type = "i32[2]";      // ranges[2] is raw: no summary.
    std::vector<shape::WatchSample> rows(3);
    rows[0].atMs = 0;
    rows[0].values = std::vector<Json>{41.9, Json::array({30452, 30549}), "01 02"};
    rows[1].atMs = 500;
    rows[1].values = std::vector<Json>{nullptr, Json::array({30452, 30549}), "01 02"};  // Unreadable once.
    rows[2].atMs = 2000;
    rows[2].values = std::vector<Json>{41.8, Json::array({30549, 30650}), "01 03"};
    const auto summary = shape::SummariseWatch(ranges, rows);
    REQUIRE(summary.size() == 3);
    CHECK(summary[0].range == 0);
    CHECK_FALSE(summary[0].element);
    CHECK(summary[0].samples == 2);
    CHECK(summary[0].first == doctest::Approx(41.9));
    CHECK(summary[0].least == doctest::Approx(41.8));
    CHECK(*summary[0].perSecond == doctest::Approx(-0.05));   // -0.1 over 2 s.
    CHECK(summary[1].element == 0u);
    CHECK(summary[2].element == 1u);
    CHECK(summary[2].greatest == 30650);
    CHECK(*summary[2].perSecond == doctest::Approx(50.5));
    CHECK(summary[0].changes == 1);  // 41.9 -> 41.8 (the unreadable sample does not count).
    CHECK(summary[1].changes == 1);
    CHECK(Checked(shape::WatchCapture{false, std::nullopt, 9, 2000, 1000, std::nullopt, ranges, rows, std::nullopt,
        std::nullopt, std::nullopt, summary})["summary"][0]["per_second"].is_number());
}

TEST_CASE("watch rows keep the first sample, each change with its time and the last") {
    const auto row = [](double ms, Json value) {
        shape::WatchSample sample;
        sample.atMs = ms;
        sample.values = std::vector<Json>{std::move(value)};
        return sample;
    };
    // A bullet list: 0, then 1 for 63 ms, then 0 again, sampled every 10 ms.
    std::vector<shape::WatchSample> rows;
    for (uint64_t t = 0; t < 1000; t += 10) rows.push_back(row(static_cast<double>(t), t >= 300 && t < 363 ? 1 : 0));
    uint64_t dropped = 9;
    const auto kept = shape::WatchChanges(rows, shape::MaxWatchChangeRows, dropped);
    CHECK(dropped == 0);
    REQUIRE(kept.size() == 4);
    CHECK(kept[0].atMs == 0);
    CHECK((kept[1].atMs == 300 && (*kept[1].values)[0] == 1));
    CHECK((kept[2].atMs == 370 && (*kept[2].values)[0] == 0));
    CHECK(kept[3].atMs == 990);  // The last sample, unchanged, ends the capture.
    // A gap's lost and resumed rows always stay, and so does the row after a gap.
    std::vector<shape::WatchSample> gap{row(0, 5), row(10, 5), shape::WatchSample{}, row(30, 5), row(40, 5)};
    gap[2].atMs = 20;
    gap[2].lost = true;
    gap[3].resumed = true;
    CHECK(shape::WatchChanges(gap, 200, dropped).size() == 4);
    // A value that changes every sample is cut to the limit, half from each end.
    std::vector<shape::WatchSample> moving;
    for (uint64_t i = 0; i < 1000; ++i) moving.push_back(row(static_cast<double>(i) / 1000.0, static_cast<double>(i)));
    const auto cut = shape::WatchChanges(moving, 200, dropped);
    CHECK(cut.size() == 200);
    CHECK(dropped == 800);
    CHECK(cut[99].atMs == doctest::Approx(0.099));
    CHECK(cut[100].atMs == doctest::Approx(0.9));
    CHECK(cut.back().atMs == doctest::Approx(0.999));
    CHECK(shape::WatchChanges({}, 200, dropped).empty());
}

TEST_CASE("each groups its columns' spans into passes of at most 64 bytes") {
    using Spans = std::vector<std::pair<uint32_t, uint32_t>>;
    // Near fields share a pass; a far one starts its own; overlaps merge.
    CHECK(shape::EachPasses({{0x130, 0x134}, {0x100, 0x108}, {0x134, 0x138}, {0x400, 0x404}}, 64) ==
        Spans{{0x100, 0x138}, {0x400, 0x404}});
    CHECK(shape::EachPasses({{0, 8}, {4, 12}}, 64) == Spans{{0, 12}});
    // Exactly 64 bytes fits; one more starts a pass.
    CHECK(shape::EachPasses({{0, 4}, {60, 64}}, 64) == Spans{{0, 64}});
    CHECK(shape::EachPasses({{0, 4}, {61, 65}}, 64) == Spans{{0, 4}, {61, 65}});

    shape::Each each;
    each.className = "Pawn";
    each.classRef = RefId{"0x1#1:0"};
    each.columns = {"Health", "Controller.PlayerState"};
    each.mode = "field";
    each.rows.push_back({RefId{"0x10#2:1"}, std::string("BP_Pawn_C_0"), {100.0, nullptr}, {}});
    each.errors = std::vector<shape::EachError>{{RefId{"0x10#2:1"}, 1, "null pointer at Controller"}};
    const auto json = Checked(each);
    CHECK(json["rows"][0] == Json{{"ref", "0x10#2:1"}, {"name", "BP_Pawn_C_0"}, {"values", Json::array({100.0, nullptr})}});
    CHECK(json["errors"][0]["column"] == 1);
    CHECK(json["same_tick"] == false);
}

TEST_CASE("get rows: every field is name, offset and value; a path is path, value and address") {
    shape::Get dump;
    shape::GetRow row;
    row.name = "MaxHealth";
    row.raw = "_aaabcd";
    row.offset = 308;
    row.value = 100;
    dump.rows.push_back(row);
    row.raw.reset();
    row.name = "Flags";
    row.truncated = true;
    row.meta = MakeFieldInfo(Field("Flags", 0x60, 1, "ByteProperty"));
    dump.rows.push_back(row);
    const auto json = Checked(dump);
    CHECK(json["rows"][0] == Json{{"name", "MaxHealth"}, {"raw", "_aaabcd"}, {"offset", "0x134"}, {"value", 100}});
    CHECK(json["rows"][1]["truncated"] == true);    // Only when true, and the layout only with meta.
    CHECK(json["rows"][1]["meta"]["type"] == "ByteProperty");

    shape::Get get;
    get.ref = RefId{"0x10#1:1"};
    shape::GetRow value;
    value.path = "A.B";
    value.value = 1.5;
    value.address = Addr(0x18);
    get.rows.push_back(value);
    shape::GetRow failed;
    failed.path = "C";
    failed.error = "no field C";
    get.rows.push_back(failed);
    const auto got = Checked(get);
    CHECK(got["rows"][0] == Json{{"path", "A.B"}, {"value", 1.5}, {"address", "0x18"}});
    CHECK(got["rows"][1] == Json{{"path", "C"}, {"error", "no field C"}});    // A failed path keeps its place.
}

TEST_CASE("get types, func and its signature") {
    const std::vector<ue::Field> fields{Field("_aaabcd", 0x100, 4, "FloatProperty")};
    const auto table = Checked(MakeTypes(Pawn, fields, [](const ue::Field&) { return std::string_view("Health"); }));
    CHECK(table["rows"][0] == Json{{"name", "Health"}, {"raw", "_aaabcd"}, {"owner", "Pawn"}, {"type", "FloatProperty"},
        {"offset", "0x100"}, {"size", 4}});
    CHECK(table["capped"] == false);

    const ue::FunctionInfo function{0x2000, 0x7ff600001000, 0x400, true, "Pawn", "GetSpeed"};
    const std::vector<Module> modules{{"game.exe", 0x7ff600000000, 0x100000}};
    const auto located = Checked(MakeFunc(Pawn, function, modules));
    CHECK(located["exec"] == "0x7ff600001000");
    CHECK(located["rva"] == "0x1000");
    CHECK(Checked(MakeFunc(Pawn, function, {}, "modules unavailable"))["module_error"] == "modules unavailable");
    auto declared = function;
    declared.interface = true;
    declared.interfaceOffset = 0xF0;
    declared.slot = 0xA8;
    declared.implementation = 0x7ff600004000;
    const auto viaInterface = Checked(MakeFunc(Pawn, declared, modules));
    CHECK(viaInterface["interface"] == Json{{"offset", "0xf0"}, {"slot", 21}, {"implementation", "0x7ff600004000"},
        {"implementation_module", "game.exe"}, {"implementation_rva", "0x4000"}});
    declared.implementation = 0;
    CHECK(Checked(MakeFunc(Pawn, declared, modules))["interface"] == Json{{"offset", "0xf0"}, {"slot", 21}});
    auto native = function;
    native.exec = 0;
    const auto unlocated = Checked(MakeFunc(Pawn, native, modules));
    CHECK_FALSE(unlocated.contains("exec")); // No exec entry: absent, with nothing to locate.
    CHECK_FALSE(unlocated.contains("module"));

    ue::Signature signature{8, {Field("ReturnValue", 0, 4, "FloatProperty")}};
    signature.parameters[0].parameterFlags = ue::Return;
    auto withSignature = MakeFunc(Pawn, function, modules);
    AddSignature(withSignature, signature);
    const auto params = Checked(std::move(withSignature));
    CHECK(params["block_size"] == 8);
    CHECK(params["params"][0]["direction"] == "return");
    CHECK(params["params"][0]["name"] == "ReturnValue");
    CHECK_FALSE(params.contains("params_error"));
}

TEST_CASE("addresses inside a module carry module and rva, others nothing") {
    const std::vector<Module> modules{{"game.exe", 0x7ff600000000, 0x100000}, {"engine.dll", 0x7ffa00000000, 0x2000}};
    CHECK(ToJson(Locate(0x7ffa00001ff8, modules)) == Json{{"module", "engine.dll"}, {"rva", "0x1ff8"}});
    CHECK(ToJson(Locate(0x7ff600000000, modules)) == Json{{"module", "game.exe"}, {"rva", "0x0"}});
    CHECK(ToJson(Locate(0x7ffa00002000, modules)).empty()); // One past the image.
    CHECK(ToJson(Locate(0x1aac568d980, modules)).empty());  // Heap.
    CHECK(ToJson(Locate(0, modules)).empty());
    CHECK(ToJson(Locate(0x7ff600000010, {})).empty());      // List unknown.
}

TEST_CASE("peek labels each qword by the first that holds: object, module, heap, value") {
    const std::vector<Module> modules{{"game.exe", 0x7ff600000000, 0x100000}};
    PeekValue object{0x7ff600000010, 0x1aac568d980, true};
    object.object = MakeObject(ue::Object{Pawn, "Pawn_0", "Pawn"});
    object.readable = true;
    object.name = "Ignored";
    auto row = Checked(MakePeekRow(object, modules));
    CHECK(row["label"] == "object");
    CHECK(row["object"]["name"] == "Pawn_0");
    CHECK(row["rva"] == "0x10");
    CHECK_FALSE(row.contains("name_hint"));

    PeekValue vtable{0x1aac568d980, 0x7ff600004000, true};
    row = Checked(MakePeekRow(vtable, modules));
    CHECK(row == Json{{"address", "0x1aac568d980"}, {"value", "0x7ff600004000"}, {"label", "module"},
        {"value_module", "game.exe"}, {"value_rva", "0x4000"}});

    // A value that decodes as a name and is also a readable pointer is a pointer, with no hint.
    PeekValue heap{0x1000, 0x1aac568d980, true};
    heap.readable = true;
    heap.name = "SomeName";
    row = Checked(MakePeekRow(heap, modules));
    CHECK(row == Json{{"address", "0x1000"}, {"value", "0x1aac568d980"}, {"label", "heap"}});

    PeekValue tag{0x1008, 0x23a0f, true};
    tag.name = "Attribute.Gameplay.Weapon.SpreadMultiplier";
    row = Checked(MakePeekRow(tag, modules));
    CHECK(row["label"] == "value");
    CHECK(row["f32"].size() == 2);
    CHECK(row.contains("f64"));
    CHECK(row["name_hint"] == "Attribute.Gameplay.Weapon.SpreadMultiplier");

    PeekValue unreadablePointer{0x1010, 0x1aac568d980, true}; // Pointer-shaped but not readable: a value.
    CHECK(Checked(MakePeekRow(unreadablePointer, modules))["label"] == "value");
    CHECK(Checked(MakePeekRow(PeekValue{0x1018}, modules)) == Json{{"address", "0x1018"}, {"label", "unreadable"}});
    PeekValue zero{0x1020, 0, true};
    CHECK(Checked(MakePeekRow(zero, modules))["value"] == "0x0");

    const auto label = Checked(MakeLabel(modules[0], 0x6543210f, 0x7ff600001234, "FName_Tag", "global"));
    CHECK(label == Json{{"module", "game.exe"}, {"module_size", 0x100000}, {"timestamp", 0x6543210f}, {"rva", "0x1234"},
        {"name", "FName_Tag"}, {"kind", "global"}});
}

TEST_CASE("labels keep one row per RVA, confirmed names first, and one name per vtable") {
    const auto merged = MergeLabels({{0x30, "vtbl_A", "vtable"}, {0x10, "A::execB", "exec"}, {0x10, "FName_Tag", "global"},
        {0x20, "A::I_F", "interface"}, {0x20, "A::execF", "exec"}});
    CHECK(merged == std::vector<LabelRow>{{0x10, "FName_Tag", "global"}, {0x20, "A::execF", "exec"}, {0x30, "vtbl_A", "vtable"}});
    const std::vector<std::pair<uint64_t, std::string>> owners{{0x100, "BP_Pawn_C"}, {0x100, "Pawn"}, {0x100, "Character"},
        {0x200, "BP_Only_C"}};
    CHECK(VtableLabels(owners) == std::vector<LabelRow>{{0x100, "vtbl_Pawn", "vtable"}, {0x200, "vtbl_BP_Only_C", "vtable"}});
}

TEST_CASE("a labels line is read back only for the same build") {
    const Module module{"Game.exe", 0x7ff600000000, 0x100000};
    const auto line = Checked(MakeLabel(module, 7, 0x7ff600001234, "FName_Tag", "global")).dump();
    const auto row = ParseLabel(line, module, 7);
    REQUIRE(row.has_value());
    CHECK(*row == LabelRow{0x1234, "FName_Tag", "global"});
    CHECK_FALSE(ParseLabel(line, module, 8).has_value());                                   // Another timestamp.
    CHECK_FALSE(ParseLabel(line, Module{"Game.exe", 0x7ff600000000, 0x200000}, 7).has_value()); // Another size.
    CHECK(ParseLabel(line, Module{"GAME.EXE", 0x7ff700000000, 0x100000}, 7).has_value());   // Base moves; RVA stays.
    CHECK_FALSE(ParseLabel("not json", module, 7).has_value());
}

TEST_CASE("a name given in the static tool outranks every derived one, and an export keeps what it does not derive") {
    CHECK(MergeLabels({{0x10, "A::execB", "exec"}, {0x10, "Ricochet_Resolve", "user"}, {0x10, "GTag", "global"}}) ==
          std::vector<LabelRow>{{0x10, "Ricochet_Resolve", "user"}});
    const Module module{"Game.exe", 0x7ff600000000, 0x100000};
    const std::vector<std::string> lines{
        Checked(MakeLabel(module, 7, module.base + 0x10, "A::execB", "exec")).dump(),
        Checked(MakeLabel(module, 7, module.base + 0x20, "Ricochet_Resolve", "user")).dump(),
        Checked(MakeLabel(module, 7, module.base + 0x30, "GTag", "global")).dump(),
        Checked(MakeLabel(module, 7, module.base + 0x40, "vtbl_A", "vtable")).dump(),
        Checked(MakeLabel(module, 8, module.base + 0x50, "Old", "user")).dump(),   // Another build.
        "not json"};
    const auto kept = KeepLabels(lines, module, 7, [](std::string_view kind) { return kind == "exec"; });
    CHECK(kept.rows == std::vector<LabelRow>{{0x20, "Ricochet_Resolve", "user"}, {0x30, "GTag", "global"},
                                             {0x40, "vtbl_A", "vtable"}});   // exec is derived again; vtable was not asked for.
    REQUIRE(kept.otherBuilds.size() == 1);
    CHECK(kept.otherBuilds[0] == lines[4]);
}

TEST_CASE("name and name-find results") {
    const std::vector<int32_t> indices{5, 6};
    const std::vector<std::string> names{"Health", ""};
    const auto decoded = Checked(MakeNames(indices, names));
    CHECK(decoded["rows"][0] == Json{{"index", 5}, {"name", "Health"}});
    CHECK(decoded["rows"][1] == Json{{"index", 6}}); // No pooled name at that index.

    const ue::NameFindResult found{{{5, "Health"}}, 64};
    const auto page = Checked(MakeNameFind(found, 0));
    CHECK(page["rows"][0]["name"] == "Health");
    CHECK(page["next_cursor"] == 64);
}

namespace {
ue::CensusRow CensusRow(std::string name, Ref ref, uint32_t count) {
    return {{ref, std::move(name), ""}, count, {Ref{0x50000 + ref.address, 900, 1}}};
}
}

TEST_CASE("census rows filter by class name and keep the whole census's totals") {
    const ue::CensusResult census{{CensusRow("BP_Projectile_C", {0x100, 1, 1}, 9),
        CensusRow("Pawn", {0x200, 2, 1}, 3), CensusRow("ProjectileMovementComponent", {0x300, 3, 1}, 2)}, 14};
    auto rows = census.rows;
    KeepMatching(rows, "PROJECTILE", [](const ue::CensusRow& r) -> const std::string& { return r.type.name; });
    REQUIRE(rows.size() == 2);
    const auto result = Checked(MakeCensus("Actor", census, rows, {0, 1}, 5));
    CHECK(result["classes"] == 3);
    CHECK(result["instances"] == 14);
    CHECK(result["rows"].size() == 1); // limit after the filter.
    CHECK(result["rows"][0] == Json{{"class", "BP_Projectile_C"}, {"ref", "0x100#1:1"}, {"count", 9},
        {"samples", {"0x50100#900:1"}}});
    CHECK(result["next_cursor"] == 1);
    CHECK(result["total"] == 2);
    CHECK(result["filter"] == "Actor");
    CHECK(result["hint"] == "start 1 for more, limit 0 for all");
    const auto plain = Checked(MakeCensus("", census, census.rows, {}, 5));
    CHECK_FALSE(plain.contains("hint"));
    CHECK_FALSE(plain.contains("next_cursor"));
    CHECK_FALSE(plain.contains("filter"));
    CHECK(plain["rows"].size() == 3);
}

TEST_CASE("census pages join to every row of a filter, and the last page has no cursor") {
    std::vector<ue::CensusRow> all;
    for (uint32_t i = 0; i < 7; ++i)
        all.push_back(CensusRow((i % 2 ? "Zone" : "Other") + std::to_string(i), {0x100 * (i + 1), static_cast<int32_t>(i), 1}, 10 - i));
    const ue::CensusResult census{all, 49};
    // Stored and read back as a later page would be: the same rows, samples included.
    const auto stored = StoredCensus(MakeCensusBaseline("Actor", census));
    REQUIRE(stored.rows.size() == census.rows.size());
    CHECK(stored.rows[3].samples == census.rows[3].samples);
    CHECK(stored.instances == 49);
    auto rows = stored.rows;
    KeepMatching(rows, "zone", [](const ue::CensusRow& r) -> const std::string& { return r.type.name; });
    REQUIRE(rows.size() == 3);
    std::vector<std::string> seen;
    std::optional<uint32_t> cursor = 0;
    for (int page = 0; page < 5 && cursor; ++page) {
        const auto result = Checked(MakeCensus("Actor", stored, rows, {*cursor, 2}, 0));
        for (const auto& row : result["rows"]) seen.push_back(row["class"]);
        CHECK(result["total"] == 3);
        cursor = result.contains("next_cursor") ? std::optional(result["next_cursor"].get<uint32_t>()) : std::nullopt;
        CHECK(result.contains("hint") == cursor.has_value());
    }
    CHECK(seen == std::vector<std::string>{"Zone1", "Zone3", "Zone5"});
    // Past the end: an empty page, no cursor.
    const auto past = Checked(MakeCensus("Actor", stored, rows, {9, 2}, 0));
    CHECK(past["rows"].empty());
    CHECK_FALSE(past.contains("next_cursor"));
    CHECK(CensusPageProblem(MakeCensusBaseline("Actor", census), std::nullopt, "Actor", false).empty());
    CHECK(CensusPageProblem(MakeCensusBaseline("Actor", census), std::nullopt, "Actor", true) ==
        "no stored census to page: run census with diff for Actor without start first");
    CHECK_FALSE(CensusPageProblem(MakeCensusBaseline("Pawn", census), std::nullopt, "Actor", false).empty());
    CHECK_FALSE(CensusPageProblem(std::nullopt, std::nullopt, "", false).empty());
}

TEST_CASE("census diff keeps grown, shrunk, new and vanished classes, largest change first") {
    const CensusBaseline before{"Actor", {{"Grown", {0x100, 1, 1}, 2}, {"Shrunk", {0x200, 2, 1}, 5},
        {"Same", {0x300, 3, 1}, 4}, {"Vanished", {0x400, 4, 1}, 3}, {"Reused", {0x500, 5, 1}, 1},
        {"Serial", {0x600, 6, 0}, 2}}};
    const ue::CensusResult after{{CensusRow("Grown", {0x100, 1, 1}, 10), CensusRow("Shrunk", {0x200, 2, 1}, 4),
        CensusRow("Same", {0x300, 3, 1}, 4), CensusRow("New", {0x700, 7, 1}, 2),
        CensusRow("Other", {0x800, 5, 2}, 1), CensusRow("Serial", {0x600, 6, 4}, 2)}, 23};
    const auto changes = CensusChanges(before, after);
    std::vector<std::string> names;
    for (const auto& change : changes) names.push_back(change.name);
    // Same and Serial (serial assigned since) are unchanged; slot 5 now holds another class.
    CHECK(names == std::vector<std::string>{"Grown", "Vanished", "New", "Other", "Reused", "Shrunk"});
    CHECK(changes[0].before == 2);
    CHECK(changes[0].after == 10);
    CHECK(changes[0].samples.size() == 1);
    CHECK(changes[1].after == 0);
    CHECK(changes[1].samples.empty()); // Gone: no instances to sample.
    CHECK(changes[2].before == 0);
    const auto result = Checked(MakeCensusDiff(before, after, changes, {}, 5));
    const auto paged = Checked(MakeCensusDiff(before, after, changes, {2, 2}, 5));
    CHECK(paged["rows"][0]["class"] == "New");
    CHECK(paged["rows"][0]["before"] == 0);
    CHECK(paged["rows"][0]["count"] == 2);
    CHECK(paged["next_cursor"] == 4);
    CHECK(paged["total"] == 6);
    CHECK(result["before"]["classes"] == 6);
    CHECK(result["before"]["instances"] == 17);
    CHECK(result["instances"] == 23);
    CHECK(result["filter"] == "Actor");
    CHECK(CensusChanges(before, {{}, 0}).size() == 6);
    CHECK(CensusChanges(MakeCensusBaseline("Actor", after), after).empty());
}

TEST_CASE("census diff needs a baseline under the same filter") {
    CHECK(CensusBaselineProblem(std::nullopt, "Actor").starts_with("no census baseline"));
    const CensusBaseline all{"", {}};
    CHECK(CensusBaselineProblem(all, "Actor") == "the baseline is from census: run census for Actor first");
    CHECK(CensusBaselineProblem(all, "").empty());
    CHECK(CensusBaselineProblem(CensusBaseline{"Actor", {}}, "Actor").empty());
}

TEST_CASE("script and refs results group entries by kind") {
    ue::ScriptResult script{513, {}};
    ue::ScriptEntry name{};
    name.offset = 350;
    name.count = 1;
    name.kind = ue::ScriptKind::Name;
    name.name = 145935;
    strcpy_s(name.text, "Attribute.Gameplay.Character.ReloadSpeedMultiplier");
    ue::ScriptEntry call{};
    call.kind = ue::ScriptKind::Function;
    call.object = Ref{0x7ff4d87a0378, 9, 0};
    strcpy_s(call.text, "BHGameplayEffectLibrary:GetValues");
    ue::ScriptEntry local{};
    local.kind = ue::ScriptKind::Property;
    local.pointer = 0x1aac568d980;
    strcpy_s(local.text, "DoubleProperty PlayRate");
    script.entries = {name, call, local};
    const auto result = Checked(MakeScript(Pawn, "BP_Action_Reload_C:GetReloadSpeedMultiplier", script));
    CHECK(result["names"][0] == Json{{"offset", "0x15e"}, {"count", 1}, {"index", 145935},
        {"name", "Attribute.Gameplay.Character.ReloadSpeedMultiplier"}});
    CHECK(result["functions"][0]["ref"] == "0x7ff4d87a0378#9:0");
    CHECK(result["properties"][0]["address"] == "0x1aac568d980");

    ue::RefsResult refs{{}, false};
    ue::RefsEntry row{};
    row.function = Ref{0x2000, 3, 1};
    strcpy_s(row.owner, "BP_Action_Reload_C");
    strcpy_s(row.name, "GetReloadSpeedMultiplier");
    refs.rows = {row};
    const auto found = Checked(MakeRefs(145935, "Attribute.Gameplay.Character.ReloadSpeedMultiplier", refs));
    CHECK(found["rows"][0] == Json{{"ref", "0x2000#3:1"}, {"owner", "BP_Action_Reload_C"},
        {"name", "GetReloadSpeedMultiplier"}, {"offset", "0x0"}, {"count", 0}});
    CHECK(found["capped"] == false);
    CHECK_FALSE(Checked(MakeRefs(7, "", refs)).contains("name"));
}

TEST_CASE("every line is a result or an error envelope") {
    CHECK(ResultEnvelope("find", Json::object()) == Json{{"command", "find"}, {"ok", true}, {"result", Json::object()}});
    const auto validation = ErrorEnvelope("find", ErrorKind::Validation, "bad");
    CHECK(validation == Json{{"command", "find"}, {"ok", false}, {"error", {{"kind", "validation"}, {"message", "bad"}}}});
    const Error refused{Error::Kind::Agent, Status::NotFound, 0, "gone", "not found: gone"};
    const auto agent = ErrorEnvelope("describe", refused);
    CHECK(agent["error"]["kind"] == "agent");
    CHECK(agent["error"]["reason"] == "gone");
    CHECK(agent["error"].contains("status"));
    CHECK(agent["error"].contains("detail"));
}

TEST_CASE("batch results give each command its tick and name the ones that moved it") {
    const auto envelope = [](const char* command) { return Json{{"command", command}, {"ok", true}, {"result", Json::object()}}; };
    // Three single-tick reads.
    std::vector<BatchItemTicks> same{{0, 40, 40}, {40, 40, 40}, {40, 40, 40}};
    auto result = Checked(MakeBatch({envelope("read"), envelope("read"), envelope("get")}, same, 40, 40));
    CHECK(result["same_tick"] == true);
    CHECK(result["all_ok"] == true);
    CHECK(result["spans_ticks"].empty());
    CHECK(result["rows"][1]["tick"] == 40);
    CHECK_FALSE(result["rows"][1].contains("last_tick"));
    // A read, a scan over many ticks, then a read in the tick the scan ended.
    std::vector<BatchItemTicks> skewed{{0, 40, 40}, {40, 41, 355}, {355, 355, 355}};
    result = Checked(MakeBatch({envelope("read"), envelope("each"), envelope("read")}, skewed, 40, 355));
    CHECK(result["same_tick"] == false);
    CHECK(result["first_tick"] == 40);
    CHECK(result["last_tick"] == 355);
    REQUIRE(result["spans_ticks"].size() == 1);
    CHECK(result["spans_ticks"][0] == Json{{"index", 1}, {"command", "each"}});
    CHECK(result["rows"][1]["tick"] == 41);
    CHECK(result["rows"][1]["last_tick"] == 355);
    CHECK_FALSE(result["rows"][2].contains("last_tick"));
    // A command that sent nothing (a schema refusal) has no tick.
    std::vector<BatchItemTicks> none{{40, 0, 0}};
    result = Checked(MakeBatch({envelope("bad")}, none, 40, 40));
    CHECK_FALSE(result["rows"][0].contains("tick"));
}

namespace {
ue::Field Parameter(std::string name, uint8_t flags, std::string type, std::string structName = {}, Ref structType = {}) {
    auto field = Field(std::move(name), 0, 8, std::move(type));
    field.parameterFlags = flags;
    field.structName = std::move(structName);
    field.structType = structType;
    return field;
}
const Ref TagType{0x7000, 20, 1}, ContainerType{0x7100, 21, 1};
const std::vector<ue::Field> TagMembers{Field("TagName", 0, 8, "NameProperty")};
const std::vector<ue::Field> ContainerMembers{Field("GameplayTags", 0, 16, "ArrayProperty"), Field("ParentTags", 16, 16, "ArrayProperty")};
const std::vector<ue::Field>* Structs(const Ref& type) {
    if (type == TagType) return &TagMembers;
    if (type == ContainerType) return &ContainerMembers;
    return nullptr;
}
}

TEST_CASE("an outputs-only call takes a zeroed block; one with inputs names the input") {
    const ue::Signature getter{8, {Parameter("ReturnValue", ue::Param | ue::Output | ue::Return | ue::Scalar, "StructProperty", "Vector")}};
    CHECK(calls::ZeroBlockRefusal(getter).empty());
    const ue::Signature out{24, {Parameter("Location", ue::Param | ue::Output | ue::Plain, "StructProperty", "Vector")}};
    CHECK(calls::ZeroBlockRefusal(out).empty());
    const ue::Signature input{16, {Parameter("Socket", ue::Param | ue::Plain, "NameProperty"),
        Parameter("ReturnValue", ue::Param | ue::Output | ue::Return | ue::Plain, "FloatProperty")}};
    CHECK(calls::ZeroBlockRefusal(input) == "Socket is an input: pass the block (params shows the layout) or Name=value");
    const ue::Signature constRef{8, {Parameter("Tag", ue::Param | ue::Output | ue::Reference, "StructProperty", "GameplayTag", TagType)}};
    CHECK(calls::ZeroBlockRefusal(constRef).starts_with("Tag is an input"));
}

TEST_CASE("each refusals and kept reasons speak of the parameter they name") {
    // A gameplay tag the agent's plain bit misses (not plain old data, yet it owns nothing).
    const ue::Signature tag{8, {Parameter("ReturnValue", ue::Param | ue::Output | ue::Return, "StructProperty", "GameplayTag", TagType)}};
    CHECK(calls::EachRefusal(tag, Structs) ==
        "ReturnValue is a GameplayTag, which the engine does not mark plain data (it may need destroying); "
        "each reads only plain values: use call");
    const ue::Signature container{24, {Parameter("ReturnValue", ue::Param | ue::Output | ue::Return, "StructProperty",
        "GameplayTagContainer", ContainerType)}};
    CHECK(calls::EachRefusal(container, Structs) ==
        "ReturnValue is a GameplayTagContainer, which owns GameplayTags (an array); each reads only plain values: use call");
    CHECK(calls::KeptReason(container, Structs).starts_with("ReturnValue is a GameplayTagContainer, which owns GameplayTags"));
    const ue::Signature text{16, {Parameter("ReturnValue", ue::Param | ue::Output | ue::Return, "StrProperty")}};
    CHECK(calls::KeptReason(text, Structs).starts_with("ReturnValue is a String, which owns memory; the agent keeps"));
    const ue::Signature input{8, {Parameter("Index", ue::Param | ue::Plain, "IntProperty")}};
    CHECK(calls::EachRefusal(input, Structs) == "Index is an input; each calls only functions with no inputs: call-pure with params calls a const or "
        "pure one on one object (read-only), else call");
    const ue::Signature plain{24, {Parameter("ReturnValue", ue::Param | ue::Output | ue::Return | ue::Plain, "StructProperty", "Vector")}};
    CHECK(calls::EachRefusal(plain, Structs).empty());
    CHECK(calls::KeptReason(plain, Structs).empty());
}

TEST_CASE("a layout line round-trips and names the first thing that differs") {
    const auto written = LayoutJson("Pawn", "Controller", 0x2e8, 8, "ObjectProperty").dump();
    const auto line = ParseLayoutLine(written);
    REQUIRE(line.has_value());
    CHECK(line->owner == "Pawn");
    CHECK(line->offset == 0x2e8);
    CHECK(line->size == 8u);
    CHECK(LayoutProblem(*line, 0x2e8, 8, "ObjectProperty").empty());
    CHECK(LayoutProblem(*line, 0x2f0, 4, "IntProperty") == "offset");
    CHECK(LayoutProblem(*line, 0x2e8, 4, "IntProperty") == "size");
    CHECK(LayoutProblem(*line, 0x2e8, 8, "IntProperty") == "type");
    const auto bare = ParseLayoutLine(R"({"owner": "Pawn", "field": "Controller", "offset": 744})");
    REQUIRE(bare.has_value());                                  // Size and type are optional, offset a plain number.
    CHECK(LayoutProblem(*bare, 744, 4, "IntProperty").empty());
    CHECK_FALSE(ParseLayoutLine(R"({"owner": "Pawn", "field": "X"})").has_value());
    CHECK_FALSE(ParseLayoutLine(R"({"owner": "Pawn", "field": "X", "offset": "nope"})").has_value());
    CHECK_FALSE(ParseLayoutLine("not json").has_value());
}

TEST_CASE("a native recording's rows show the registers, where each call returns and the copy") {
    const std::vector<Module> modules{{"game.exe", 0x7ff600000000, 0x100000}};
    RecorderStatus status{0x2000, 0x7ff600001000, 3, 2, static_cast<uint32_t>(RecorderState::Recording), NativeCallsKind,
        static_cast<uint32_t>(sizeof(NativeCall)) + 4, {1, 1}};
    NativeCall call{{0x3000, 2, 3, 4}, {0x3ff8000000000000, 0, 0, 0}, 0x7ff600002010};
    std::vector<RecorderCall> calls(2);
    for (auto& each : calls) {
        each.payload.resize(sizeof(call));
        memcpy(each.payload.data(), &call, sizeof(call));
    }
    calls[0].row = {2, 0x3000, 1500, 7, sizeof(call) + 4, NativeCopied, {}};
    calls[0].payload.insert(calls[0].payload.end(), {0xde, 0xad, 0xbe, 0xef});
    calls[1].row = {3, 0x3000, 2500, 7, sizeof(call), 0, {}};
    const auto names = [](uint64_t address) { return address == 0x7ff600002010 ? std::string("Owner::Fire+0x10") : std::string(); };
    const auto json = Checked(MakeNativeCalls(status, calls, 0, 3, modules, names));
    CHECK(json["recorder"] == "native-calls-start");
    CHECK(json["target"] == "0x2000");
    CHECK(json["original_module"] == "game.exe");
    CHECK(json["original_rva"] == "0x1000");
    CHECK(json["dropped"] == 1);              // Row 1 was gone when read.
    CHECK((json["copied"] == 1 && json["faulted"] == 1));
    REQUIRE(json["rows"].size() == 2);
    const auto& first = json["rows"][0];
    CHECK(first["args"] == Json{"0x3000", "0x2", "0x3", "0x4"});
    CHECK(first["xmm"][0] == "0x3ff8000000000000");
    CHECK(first["ret_rva"] == "0x2010");
    CHECK(first["ret_name"] == "Owner::Fire+0x10");
    CHECK(first["bytes"] == "de ad be ef");
    CHECK(first["at_ms"] == 1.5);
    CHECK(json["rows"][1]["copy_faulted"] == true);
    CHECK_FALSE(json.contains("next_cursor"));
}
