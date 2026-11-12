// reagentlive CLI result shapes: each lookup command's result keys, pinned against
// the "Result shapes" table in docs/commands/results.md. Builders take canned client
// results, so no agent or object table is needed.
#include "reagentlive/frontends/cli/results/ue/call_text.h"
#include "reagentlive/frontends/cli/results/ue/census.h"
#include "reagentlive/frontends/cli/results/labels.h"
#include "reagentlive/frontends/cli/results/result_json.h"
#include "reagentlive/frontends/cli/results/ue/result_json_ue.h"
#include "doctest.h"
#include <set>
#include <string>
#include <vector>

using namespace reagentlive;
using namespace reagentlive::cli;

namespace {
std::set<std::string> Keys(const Json& object) {
    std::set<std::string> keys;
    for (const auto& item : object.items()) keys.insert(item.key());
    return keys;
}
using KeySet = std::set<std::string>;
const Ref Pawn{0x1000, 7, 3};
const KeySet FieldKeys{"name", "owner", "type", "offset", "size"};

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

TEST_CASE("find and describe results have their documented keys") {
    const ue::FindResult found{{{Pawn, "Pawn_0", "Pawn"}}, 1, 25};
    const auto first = FindJson(found, 0);
    CHECK(Keys(first) == KeySet{"objects", "total", "from", "next_cursor", "truncated"});
    CHECK(Keys(first["objects"][0]) == KeySet{"ref", "name", "class"});
    CHECK(first["objects"][0]["ref"] == "0x1000#7:3");
    CHECK(first["next_cursor"] == 25);
    CHECK(FindJson(found, 25)["total"].is_null()); // Only the first page counts.
    CHECK(Keys(ObjectJson(found.objects[0])) == KeySet{"ref", "name", "class"});
    const ue::Object described{Pawn, "Pawn_0", "Pawn", 0x4f0}; // A single describe knows the class size.
    CHECK(Keys(ObjectJson(described)) == KeySet{"ref", "name", "class", "class_size"});
    CHECK(ObjectJson(described)["class_size"] == 0x4f0);
}

TEST_CASE("funcs lists objects with owner and name") {
    const ue::FunctionList list{{"Pawn:GetSpeed", "Actor:K2_GetActorLocation"}, 0};
    const auto result = FunctionsJson(Pawn, list, 0);
    CHECK(Keys(result) == KeySet{"ref", "functions", "from", "next_cursor", "truncated"});
    REQUIRE(result["functions"].size() == 2);
    CHECK(Keys(result["functions"][0]) == KeySet{"owner", "name"});
    CHECK(result["functions"][1]["owner"] == "Actor");
    CHECK(result["functions"][1]["name"] == "K2_GetActorLocation");
    CHECK(result["next_cursor"].is_null());
    CHECK_FALSE(result["truncated"].get<bool>());
}

TEST_CASE("fields, func and params results have their documented keys") {
    const std::vector<ue::Field> fields{Field("Health", 0x100, 4, "FloatProperty")};
    const auto table = FieldsJson(Pawn, fields, [](const ue::Field&) { return std::string("Health"); });
    CHECK(Keys(table) == KeySet{"ref", "fields", "truncated"});
    auto rowKeys = FieldKeys;
    rowKeys.insert("label");
    CHECK(Keys(table["fields"][0]) == rowKeys);

    const ue::FunctionInfo function{0x2000, 0x7ff600001000, 0x400, true, "Pawn", "GetSpeed"};
    const std::vector<Module> modules{{"game.exe", 0x7ff600000000, 0x100000}};
    const auto located = FunctionJson(Pawn, function, modules);
    CHECK(Keys(located) == KeySet{"ref", "function", "address", "flags", "exec", "executable", "module", "rva"});
    CHECK(located["rva"] == "0x1000");
    CHECK(Keys(FunctionJson(Pawn, function, {}, "modules unavailable")) ==
        KeySet{"ref", "function", "address", "flags", "exec", "executable", "module_error"});
    auto declared = function;
    declared.interface = true;
    declared.interfaceOffset = 0xF0;
    declared.slot = 0xA8;
    declared.implementation = 0x7ff600004000;
    const auto viaInterface = FunctionJson(Pawn, declared, modules);
    CHECK(Keys(viaInterface["interface"]) ==
        KeySet{"offset", "slot", "implementation", "implementation_module", "implementation_rva"});
    CHECK(viaInterface["interface"]["slot"] == 21);
    CHECK(viaInterface["interface"]["implementation_module"] == "game.exe");
    CHECK(viaInterface["interface"]["implementation_rva"] == "0x4000");
    declared.implementation = 0;
    CHECK(Keys(FunctionJson(Pawn, declared, modules)["interface"]) == KeySet{"offset", "slot", "implementation"});
    auto native = function;
    native.exec = 0;
    CHECK(Keys(FunctionJson(Pawn, native, modules)) == KeySet{"ref", "function", "address", "flags", "exec", "executable"});

    ue::Signature signature{8, {Field("ReturnValue", 0, 4, "FloatProperty")}};
    signature.parameters[0].parameterFlags = ue::Return;
    const auto params = ParamsJson(Pawn, function, signature);
    CHECK(Keys(params) == KeySet{"ref", "function", "address", "block_size", "parameters"});
    auto parameterKeys = FieldKeys;
    parameterKeys.insert({"direction", "flags"});
    CHECK(Keys(params["parameters"][0]) == parameterKeys);
    CHECK(params["parameters"][0]["direction"] == "return");
}

TEST_CASE("addresses inside a module carry module and rva, others nothing") {
    const std::vector<Module> modules{{"game.exe", 0x7ff600000000, 0x100000}, {"engine.dll", 0x7ffa00000000, 0x2000}};
    Json row = Json::object();
    AddLocation(row, 0x7ffa00001ff8, modules);
    CHECK(row == Json{{"module", "engine.dll"}, {"rva", "0x1ff8"}});
    Json view = Json::object();
    AddLocation(view, 0x7ff600000000, modules, "ptr_");
    CHECK(view == Json{{"ptr_module", "game.exe"}, {"ptr_rva", "0x0"}});
    Json outside = Json::object();
    AddLocation(outside, 0x7ffa00002000, modules); // One past the image.
    AddLocation(outside, 0x1aac568d980, modules);  // Heap.
    AddLocation(outside, 0, modules);
    AddLocation(outside, 0x7ff600000010, {});      // List unknown.
    CHECK(outside.empty());
}

TEST_CASE("peek labels each qword by the first that holds: object, module, heap, value") {
    const std::vector<Module> modules{{"game.exe", 0x7ff600000000, 0x100000}};
    PeekValue object{0x7ff600000010, 0x1aac568d980, true};
    object.object = ObjectJson(ue::Object{Pawn, "Pawn_0", "Pawn"});
    object.readable = true;
    object.name = "Ignored";
    auto row = PeekRowJson(object, modules);
    CHECK(Keys(row) == KeySet{"address", "module", "rva", "value", "label", "object"});
    CHECK(row["label"] == "object");
    CHECK(row["rva"] == "0x10");

    PeekValue vtable{0x1aac568d980, 0x7ff600004000, true};
    row = PeekRowJson(vtable, modules);
    CHECK(Keys(row) == KeySet{"address", "value", "label", "value_module", "value_rva"});
    CHECK(row["label"] == "module");
    CHECK(row["value_rva"] == "0x4000");

    // A value that decodes as a name and is also a readable pointer is a pointer, with no hint.
    PeekValue heap{0x1000, 0x1aac568d980, true};
    heap.readable = true;
    heap.name = "SomeName";
    row = PeekRowJson(heap, modules);
    CHECK(Keys(row) == KeySet{"address", "value", "label"});
    CHECK(row["label"] == "heap");

    PeekValue tag{0x1008, 0x23a0f, true};
    tag.name = "Attribute.Gameplay.Weapon.SpreadMultiplier";
    row = PeekRowJson(tag, modules);
    CHECK(Keys(row) == KeySet{"address", "value", "label", "f32", "f64", "name_hint"});
    CHECK(row["label"] == "value");
    CHECK(row["f32"].size() == 2);

    PeekValue unreadablePointer{0x1010, 0x1aac568d980, true}; // Pointer-shaped but not readable: a value.
    CHECK(PeekRowJson(unreadablePointer, modules)["label"] == "value");
    CHECK(PeekRowJson(PeekValue{0x1018}, modules) == Json{{"address", "0x1018"}, {"label", "unreadable"}});
    PeekValue zero{0x1020, 0, true};
    CHECK(PeekRowJson(zero, modules)["value"] == "0x0");

    const auto label = LabelJson(modules[0], 0x6543210f, 0x7ff600001234, "FName_Tag", "global");
    CHECK(Keys(label) == KeySet{"module", "module_size", "timestamp", "rva", "name", "kind"});
    CHECK(label["rva"] == "0x1234");
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
    const auto line = LabelJson(module, 7, 0x7ff600001234, "FName_Tag", "global").dump();
    const auto row = ParseLabel(line, module, 7);
    REQUIRE(row.has_value());
    CHECK(*row == LabelRow{0x1234, "FName_Tag", "global"});
    CHECK_FALSE(ParseLabel(line, module, 8).has_value());                                   // Another timestamp.
    CHECK_FALSE(ParseLabel(line, Module{"Game.exe", 0x7ff600000000, 0x200000}, 7).has_value()); // Another size.
    CHECK(ParseLabel(line, Module{"GAME.EXE", 0x7ff700000000, 0x100000}, 7).has_value());   // Base moves; RVA stays.
    CHECK_FALSE(ParseLabel("not json", module, 7).has_value());
}

TEST_CASE("name, name-find and dump results have their documented keys") {
    const std::vector<int32_t> indices{5, 6};
    const std::vector<std::string> names{"Health", ""};
    const auto decoded = NamesJson(indices, names);
    CHECK(Keys(decoded) == KeySet{"names"});
    CHECK(Keys(decoded["names"][0]) == KeySet{"index", "name"});
    CHECK(decoded["names"][1]["name"].is_null());

    const ue::NameFindResult found{{{5, "Health"}}, 0};
    const auto page = NameFindJson(found, 0);
    CHECK(Keys(page) == KeySet{"names", "from", "next_cursor"});

    CHECK(Keys(DumpJson(Pawn, Json::array(), false, false, std::nullopt)) ==
        KeySet{"ref", "fields", "truncated", "object_lookup_limited"});
    const auto filtered = DumpJson(Pawn, Json::array(), false, false, std::pair<size_t, size_t>(1, 9));
    CHECK(Keys(filtered["filtered"]) == KeySet{"shown", "total"});
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
    const auto result = CensusJson("Actor", census, rows, {0, 1}, 5);
    CHECK(Keys(result) == KeySet{"filter", "classes", "instances", "ms", "rows", "from", "shown", "total", "next_cursor",
        "truncated", "hint"});
    CHECK(Keys(result["rows"][0]) == KeySet{"class", "ref", "count", "samples"});
    CHECK(result["classes"] == 3);
    CHECK(result["instances"] == 14);
    CHECK(result["rows"].size() == 1); // --limit after the filter.
    CHECK(result["truncated"].get<bool>());
    CHECK(result["shown"] == 1);
    CHECK(result["total"] == 2);
    CHECK(result["hint"] == "--from 1 for more, --limit 0 for all");
    const auto plain = CensusJson("", census, census.rows, {}, 5);
    CHECK_FALSE(plain.contains("hint"));
    CHECK(plain["next_cursor"].is_null());
    CHECK(plain["filter"].is_null());
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
    Json cursor = 0;
    for (int page = 0; page < 5 && !cursor.is_null(); ++page) {
        const auto result = CensusJson("Actor", stored, rows, {cursor.get<uint32_t>(), 2}, 0);
        for (const auto& row : result["rows"]) seen.push_back(row["class"]);
        CHECK(result["total"] == 3);
        cursor = result["next_cursor"];
        CHECK(result.contains("hint") == !cursor.is_null());
    }
    CHECK(seen == std::vector<std::string>{"Zone1", "Zone3", "Zone5"});
    // Past the end: an empty page, no cursor.
    const auto past = CensusJson("Actor", stored, rows, {9, 2}, 0);
    CHECK(past["rows"].empty());
    CHECK(past["next_cursor"].is_null());
    CHECK(CensusPageProblem(MakeCensusBaseline("Actor", census), std::nullopt, "Actor", false).empty());
    CHECK(CensusPageProblem(MakeCensusBaseline("Actor", census), std::nullopt, "Actor", true) ==
        "no stored census to page: run census --class Actor --changed without --from first");
    CHECK_FALSE(CensusPageProblem(MakeCensusBaseline("Pawn", census), std::nullopt, "Actor", false).empty());
    CHECK_FALSE(CensusPageProblem(std::nullopt, std::nullopt, "", false).empty());
}

TEST_CASE("census --changed keeps grown, shrunk, new and vanished classes, largest change first") {
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
    const auto result = CensusChangedJson(before, after, changes, {}, 5);
    CHECK(Keys(result) == KeySet{"filter", "classes", "instances", "ms", "rows", "from", "shown", "total", "next_cursor",
        "truncated"});
    const auto paged = CensusChangedJson(before, after, changes, {2, 2}, 5);
    CHECK(paged["rows"][0]["class"] == "New");
    CHECK(paged["next_cursor"] == 4);
    CHECK(Keys(result["rows"][0]) == KeySet{"class", "ref", "before", "after", "samples"});
    CHECK(Keys(result["classes"]) == KeySet{"before", "after"});
    CHECK(result["classes"]["before"] == 6);
    CHECK(result["instances"]["before"] == 17);
    CHECK(result["instances"]["after"] == 23);
    CHECK(result["filter"] == "Actor");
    CHECK(CensusChanges(before, {{}, 0}).size() == 6);
    CHECK(CensusChanges(MakeCensusBaseline("Actor", after), after).empty());
}

TEST_CASE("census --changed needs a baseline under the same filter") {
    CHECK(CensusBaselineProblem(std::nullopt, "Actor").starts_with("no census baseline"));
    const CensusBaseline all{"", {}};
    CHECK(CensusBaselineProblem(all, "Actor") == "the baseline is from census: run census --class Actor first");
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
    const auto result = ScriptJson(Pawn, "BP_Action_Reload_C:GetReloadSpeedMultiplier", script);
    CHECK(Keys(result) == KeySet{"ref", "function", "size", "names", "functions", "properties"});
    CHECK(Keys(result["names"][0]) == KeySet{"offset", "count", "index", "name"});
    CHECK(Keys(result["functions"][0]) == KeySet{"offset", "count", "ref", "function"});
    CHECK(Keys(result["properties"][0]) == KeySet{"offset", "count", "address", "property"});
    CHECK(result["names"][0]["offset"] == 350);

    ue::RefsResult refs{{}, false};
    ue::RefsEntry row{};
    row.function = Ref{0x2000, 3, 1};
    strcpy_s(row.owner, "BP_Action_Reload_C");
    strcpy_s(row.name, "GetReloadSpeedMultiplier");
    refs.rows = {row};
    const auto found = RefsJson(145935, "Attribute.Gameplay.Character.ReloadSpeedMultiplier", refs);
    CHECK(Keys(found) == KeySet{"index", "name", "functions", "capped"});
    CHECK(Keys(found["functions"][0]) == KeySet{"ref", "owner", "name", "offset", "count"});
}

TEST_CASE("every line is a result or an error envelope") {
    CHECK(Keys(ResultEnvelope("find", Json::object())) == KeySet{"command", "ok", "result"});
    const auto validation = ErrorEnvelope("find", "validation", "bad");
    CHECK(Keys(validation) == KeySet{"command", "ok", "error"});
    CHECK(Keys(validation["error"]) == KeySet{"kind", "message"});
    const Error refused{Error::Kind::Agent, Status::NotFound, 0, "gone", "not found: gone"};
    const auto agent = ErrorEnvelope("describe", refused);
    CHECK(Keys(agent["error"]) == KeySet{"kind", "message", "status", "detail", "reason"});
    CHECK(agent["error"]["kind"] == "agent");
}

TEST_CASE("batch results give each command its tick and name the ones that moved it") {
    const auto envelope = [](const char* command) { return Json{{"command", command}, {"ok", true}, {"result", Json::object()}}; };
    // Three single-tick reads.
    std::vector<BatchItemTicks> same{{0, 40, 40}, {40, 40, 40}, {40, 40, 40}};
    auto result = BatchJson({envelope("read"), envelope("read"), envelope("get")}, same, 40, 40);
    CHECK(Keys(result) == KeySet{"count", "all_ok", "same_tick", "first_tick", "last_tick", "spans_ticks", "results"});
    CHECK(result["same_tick"] == true);
    CHECK(result["spans_ticks"].empty());
    CHECK(result["results"][1]["tick"] == 40);
    CHECK_FALSE(result["results"][1].contains("last_tick"));
    // A read, a scan over many ticks, then a read in the tick the scan ended.
    std::vector<BatchItemTicks> skewed{{0, 40, 40}, {40, 41, 355}, {355, 355, 355}};
    result = BatchJson({envelope("read"), envelope("each"), envelope("read")}, skewed, 40, 355);
    CHECK(result["same_tick"] == false);
    REQUIRE(result["spans_ticks"].size() == 1);
    CHECK(result["spans_ticks"][0] == Json{{"index", 1}, {"command", "each"}});
    CHECK(result["results"][1]["tick"] == 41);
    CHECK(result["results"][1]["last_tick"] == 355);
    CHECK_FALSE(result["results"][2].contains("last_tick"));
    // A command that sent nothing (a syntax error) has no tick.
    std::vector<BatchItemTicks> none{{40, 0, 0}};
    result = BatchJson({envelope("bad")}, none, 40, 40);
    CHECK_FALSE(result["results"][0].contains("tick"));
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
    CHECK(calls::EachRefusal(input, Structs) == "Index is an input; each calls only functions with no inputs: use call");
    const ue::Signature plain{24, {Parameter("ReturnValue", ue::Param | ue::Output | ue::Return | ue::Plain, "StructProperty", "Vector")}};
    CHECK(calls::EachRefusal(plain, Structs).empty());
    CHECK(calls::KeptReason(plain, Structs).empty());
}
