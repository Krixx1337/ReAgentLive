// reagentlive console tests: the real console against the fake engine
// (fake_engine.h). One fixture object is read through every verb that decodes
// values, and the same member must print the same JSON in each; status keys
// and stale object arguments must be reported alike. A divergence still
// known is marked should_fail, so the test turns red when it is fixed and
// the mark can go.
#include "fake_engine.h"
#include <chrono>
#include <format>
#include <limits>
#include <map>
#include <string_view>

#include "console_support.h"

using namespace fake;
using namespace console_support;

TEST_CASE("the fake engine answers the console") {
    const auto& fixture = TheFixture();
    const auto described = Result(CallText("describe", {{"object", fixture.thing}}));
    CHECK(described["name"] == "Thing_0");
    CHECK(described["outer"]["name"] == "Package_Things");
    CHECK(Get("Health") == 87.5);
}

TEST_CASE("one member, one JSON: get, each and dump agree") {
    const Json info = Get("Info");
    // Every kind of value, decoded the same at any depth.
    CHECK(info["Scores"] == Json{{"count", 3}, {"rows", Json::array({{{"key", "Kills"}, {"value", 7}},
        {{"key", "Deaths"}, {"value", 0}}, {{"key", "Assists"}, {"value", 3}}})}});
    CHECK(info["Tag"] == "Item.Weapon.Rifle");
    CHECK(info["Owner"]["name"] == "Buddy");
    CHECK(info["Friend"]["name"] == "Buddy");
    CHECK(info["Stale"].value("gone", false));
    CHECK(info["Boxes"]["rows"][0]["Name"] == "hand_r_box");
    CHECK(info["Boxes"]["rows"][0]["Radius"] == 2.35);
    CHECK(info["Boxes"]["rows"][0]["Owner"]["name"] == "Buddy");
    // dump's value of Info is the same struct.
    CHECK(DumpRow("Info") == info);
    // each reads the members one column each.
    for (const char* member : {"Scores", "Boxes", "Tag", "Zero", "Owner", "Stale", "Friend"})
        CHECK_MESSAGE(Each(std::string("Info.") + member) == info[member], member);
}

TEST_CASE("instanced structs and soft pointers decode alike in get, each and dump") {
    const Json payload = Get("Payload");
    CHECK(payload["struct"]["name"] == "Box");
    CHECK(payload["value"]["Name"] == "hand_r_box");
    CHECK(payload["value"]["Owner"]["name"] == "Buddy");
    CHECK(DumpRow("Payload") == payload);
    CHECK(Each("Payload") == payload);
    const Json asset = Get("Asset");
    CHECK(asset["path"] == "/Game/Things/Buddy.Buddy");
    CHECK(asset["object"]["name"] == "Buddy");
    CHECK(DumpRow("Asset") == asset);
    CHECK(Each("Asset") == asset);
    const Json label = Get("Label");
    CHECK(label == "Rifle \xC3\xA9"); // UTF-8 bytes: the test build is not /utf-8.
    CHECK(DumpRow("Label") == label);
    CHECK(Each("Label") == label);
}

TEST_CASE("nonzero agrees across verbs") {
    const Json nonzero{{"view", {{"nonzero", true}}}};
    const Json info = Get("Info", nonzero);
    CHECK_FALSE(info.contains("Zero"));
    CHECK(info["Scores"]["rows"][1] == Json{{"key", "Deaths"}});
    CHECK(DumpRow("Info", "value", nonzero) == info);
    CHECK(Each("Info.Scores", nonzero) == info["Scores"]);
}

TEST_CASE("meta agrees across verbs: the reflected layout from one reader") {
    const Json meta{{"meta", true}};
    const Json tag = Get("Info.Tag", {}, meta);
    REQUIRE(tag.contains("members"));
    CHECK(tag["members"][0]["value"]["name"] == "Item.Weapon.Rifle");
    CHECK(Each("Info.Tag", {}, meta) == tag);
    const Json info = Get("Info", {}, meta);
    CHECK(DumpRow("Info", "value", {}, meta) == info);
}

TEST_CASE("top-level containers: get and each in full, entries and dump previews element by element") {
    const Json boxes = Get("Boxes");
    CHECK(Each("Boxes") == boxes);
    CHECK(Get("Scores") == Each("Scores"));
    const auto entries = Result(CallText("entries", {{"object", TheFixture().thing}, {"field", "Boxes"}}));
    CHECK(entries["rows"][0]["value"] == boxes["rows"][0]);
    const Json preview = DumpRow("Boxes", "preview");
    CHECK(preview["elements"][0]["value"] == boxes["rows"][0]);
    // The header beside a whole preview is not cut: the preview lists every element.
    CHECK(DumpRow("Boxes", "truncated").is_null());
    CHECK_FALSE(DumpRow("Boxes").contains("truncated"));
}

TEST_CASE("past the pointer cap, every verb counts unnamed alike, digested or not") {
    const auto& fixture = TheFixture();
    const Json elements{{"view", {{"elements", 256}}}};
    const auto get = Result(CallText("get", With(GetArgs(PathsOf("Crowd")), elements)));
    REQUIRE(get.contains("unnamed"));
    const auto unnamed = get["unnamed"].get<uint32_t>();
    CHECK(unnamed == fixture.crowd - 112);
    const auto each = Result(CallText("each", With(EachArgs(PathsOf("Crowd")), elements)));
    CHECK(each["unnamed"] == unnamed);
    // A digest keeps it.
    const auto records = RunLines(CallText("get", With(GetArgs(PathsOf("Crowd")), elements), {{"max_chars", 1000}}));
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].contains("digest"));
    CHECK(records[0]["digest"]["status"]["unnamed"] == unnamed);
}

TEST_CASE("a stale object fails alike as the object and as outer") {
    const auto& fixture = TheFixture();
    for (const std::string line : {CallText("get", {{"object", fixture.stale}, {"paths", PathsOf("Health")}}),
                                   CallText("describe", {{"object", fixture.stale}}),
                                   CallText("find", {{"cls", "Thing"}, {"outer", fixture.stale}}),
                                   CallText("each", {{"cls", "Thing"}, {"paths", PathsOf("Health")}, {"outer", fixture.stale}})}) {
        const auto records = RunLines(line);
        REQUIRE(records.size() == 1);
        INFO(line << " -> " << records[0].dump());
        CHECK_FALSE(records[0]["ok"].get<bool>());
        CHECK(records[0]["error"]["status"] == static_cast<int>(Status::NotFound));
    }
    // An outer named by text that matches nothing is an answer: no rows.
    CHECK(Result(CallText("find", {{"cls", "Thing"}, {"outer", "NoSuchPackage"}}))["rows"].empty());
    CHECK(Result(CallText("find", {{"cls", "Thing"}, {"outer", fixture.package}}))["total"] == 3);
}

TEST_CASE("watch-start with file keeps every sample in a file each later read rewrites") {
    static volatile uint32_t value = 7;
    const std::string address = std::format("0x{:x}", reinterpret_cast<uintptr_t>(&value));
    const auto path = (std::filesystem::temp_directory_path() /
        ("reagentlive-watch-" + std::to_string(GetCurrentProcessId()) + ".jsonl")).generic_string();
    std::filesystem::remove(path);
    const auto started = Result(CallText("watch-start", {{"duration", 0.1},
        {"ranges", Json::array({{{"addr", address}, {"type", "u32"}}})}, {"file", path}}));
    CHECK(started["file"] == path);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const auto stopped = Result(CallText("watch-read", {{"stop", true}}));      // Another console run: the path comes from the cache file.
    CHECK(stopped["file"] == path);
    std::ifstream in(path);
    std::string line;
    REQUIRE(std::getline(in, line));
    CHECK(Json::parse(line)["ranges"][0]["type"] == "u32");
    REQUIRE(std::getline(in, line));
    CHECK(Json::parse(line)["values"][0] == 7);
    in.close();
    // A start without file drops it.
    Result(CallText("watch-start", {{"duration", 0.05}, {"ranges", Json::array({{{"addr", address}, {"len", 4}}})}}));
    CHECK_FALSE(Result(CallText("watch-read", {{"stop", true}})).contains("file"));
    std::filesystem::remove(path);
}

TEST_CASE("a get path that names no field suggests the closest fields") {
    const auto records = RunLines(CallText("get", GetArgs(PathsOf("Scorez"))));
    REQUIRE(records.size() == 1);
    CHECK(records[0].dump().find("no field Scorez; closest: Scores") != std::string::npos);
}

TEST_CASE("get with types and match keeps the matching fields, any case") {
    const auto all = Result(CallText("get", With(GetArgs(), {{"mode", "types"}})));
    const auto some = Result(CallText("get", With(GetArgs(), {{"mode", "types"}, {"view", {{"match", "sCORE"}}}})));
    REQUIRE(some["rows"].size() == 1);
    CHECK(some["rows"][0]["name"] == "Scores");
    CHECK(all["rows"].size() > some["rows"].size());
}

namespace {
// Names past the old wire caps, a class with many fields, and a recorded call.
struct Extras {
    std::string longClass, owner, armor;
    Type* wide = nullptr;
};
const Extras& TheExtras() {
    static const Extras extras = [] {
        TheFixture();
        auto& world = TheWorld();
        Extras out;
        out.longClass = "WDDamageModificationOperation_PlayerArmorDurability_WithAVeryLongTail"; // 70 characters.
        out.owner = "WDDamageModificationOperation_PlayerArmorDurability";                     // 51.
        Type& longType = world.AddType(out.longClass, 8, false, "Object");
        auto longField = MakeField(std::string(120, 'L'), "IntProperty", 0, 4);
        longField.owner = out.longClass;
        longType.fields = {longField};
        // Armor_0: one field its own class declares, one its parent does.
        Type& armorType = world.AddType(out.owner, 16, false, "ArmorBase");
        auto own = MakeField("Durability", "FloatProperty", 8, 4);
        own.owner = out.owner;
        auto inherited = MakeField("Base", "IntProperty", 0, 4);
        inherited.owner = "ArmorBase";
        armorType.fields = {own, inherited};
        out.armor = Text(world.Instance("Armor_0", armorType).ref);
        Type& wide = world.AddType("Wide", 4 * 120, false, "Object");
        for (uint32_t i = 0; i < 120; ++i) {
            auto field = MakeField("Field_" + std::to_string(i), "IntProperty", i * 4, 4);
            field.owner = "Wide";
            wide.fields.push_back(field);
        }
        out.wide = &wide;
        // An enum past one page, and a default object.
        Object& response = world.Add("EBallisticResponse", "Enum", 64);
        std::vector<std::pair<std::string, int64_t>> values;
        for (int64_t i = 0; i < 300; ++i) values.push_back({"EBallisticResponse::Value" + std::to_string(i), i * 2});
        world.enums.push_back({response.ref.index, std::move(values)});
        world.Instance("Default__Wide", wide);
        // Thing:TakeDamage(Instigator, Bone, Label, Amount), called once by Thing_0.
        Object& function = world.Add("TakeDamage", "Function", 64);
        Function recorded;
        recorded.slot = function.ref.index;
        recorded.blockSize = 40;
        const auto parameter = [](std::string name, std::string type, uint32_t offset, uint32_t size) {
            auto field = MakeField(name, type, offset, size);
            field.owner = "TakeDamage";
            field.numbers.flags = ue::Param;
            return field;
        };
        recorded.parameters = {parameter("Instigator", "ObjectProperty", 0, 8), parameter("Bone", "NameProperty", 8, 8),
            parameter("Label", "StrProperty", 16, 16), parameter("Amount", "FloatProperty", 32, 4)};
        Call call;
        call.row.sequence = 1;
        call.row.via = ue::CallsEvent;
        call.block.assign(40, 0);
        const auto buddy = std::ranges::find(world.objects, std::string("Buddy"), &Object::name);
        const auto caller = std::ranges::find(world.objects, std::string("Thing_0"), &Object::name);
        call.row.object = caller->ref.address;
        const uint64_t instigator = buddy->ref.address;
        memcpy(call.block.data(), &instigator, 8);
        const int32_t bone[2] = {world.Name("head"), 0};
        memcpy(call.block.data() + 8, bone, 8);
        const char16_t label[] = u"Rifle";
        SetArray(call.block.data() + 16, {reinterpret_cast<const uint8_t*>(label), sizeof(label)}, 6);
        const float amount = 12.5f;
        memcpy(call.block.data() + 32, &amount, 4);
        call.row.size = static_cast<uint32_t>(call.block.size());
        recorded.calls.push_back(std::move(call));
        world.functions.push_back(std::move(recorded));
        return out;
    }();
    return extras;
}
}

TEST_CASE("a class named past 63 characters resolves, and names travel whole") {
    const auto& extras = TheExtras();
    const auto fields = Result(CallText("get", {{"object", extras.longClass}, {"mode", "types"}}));
    REQUIRE(fields["rows"].size() == 1);
    CHECK(fields["rows"][0]["name"] == std::string(120, 'L'));
    CHECK(fields["rows"][0]["owner"] == extras.longClass);
}

TEST_CASE("get with own keeps the fields a 51-character class declares") {
    const auto dumped = Result(CallText("get", {{"object", TheExtras().armor}, {"own", true}}));
    REQUIRE(dumped["rows"].size() == 1);
    CHECK(dumped["rows"][0]["name"] == "Durability");
}

TEST_CASE("enum names an enum's values, past one page") {
    TheExtras();
    const auto named = Result(CallText("enum", {{"enum", "EBallisticResponse"}}));
    CHECK(named["name"] == "EBallisticResponse");
    REQUIRE(named["rows"].size() == 300);
    CHECK(named["rows"][0] == Json{{"name", "EBallisticResponse::Value0"}, {"value", 0}});
    CHECK(named["rows"][299] == Json{{"name", "EBallisticResponse::Value299"}, {"value", 598}});
    const auto records = RunLines(CallText("enum", {{"enum", "Wide"}}));
    REQUIRE(records.size() == 1);
    CHECK_FALSE(records[0]["ok"].get<bool>()); // A class is no enum.
}

TEST_CASE("a bare Default__ name is the default object, not a class") {
    TheExtras();
    const auto fields = Result(CallText("get", {{"object", "Default__Wide"}, {"mode", "types"}}));
    CHECK(fields["rows"].size() == 120);
    const auto dumped = Result(CallText("get", {{"object", "Default__Wide"}, {"view", {{"match", "Field_7"}}}}));
    CHECK(dumped["rows"].size() > 0);
}

TEST_CASE("a class's fields come in few pages") {
    const auto& extras = TheExtras();
    const uint32_t before = extras.wide->pages;
    const auto fields = Result(CallText("get", {{"object", "Wide"}, {"mode", "types"}}));
    CHECK(fields["rows"].size() == 120);
    const uint32_t pages = extras.wide->pages - before;
    CHECK(pages * 4 <= (120 + 10) / 11); // At least 4x fewer than 11 entries a page.
}

TEST_CASE("get with elements lifts a top-level array's preview past 8") {
    const auto& fixture = TheFixture();
    const auto dumped = Result(CallText("get", With(GetArgs(), {{"view", {{"match", "Crowd"}, {"elements", 12}}}})));
    REQUIRE(dumped["rows"].size() == 1);
    const auto& preview = dumped["rows"][0]["preview"];
    CHECK(preview["total"] == fixture.crowd);
    REQUIRE(preview["elements"].size() == 12);
    CHECK(preview["elements"][11]["object"]["name"] == "Extra_11");
    CHECK(Result(CallText("get", With(GetArgs(), {{"view", {{"match", "Crowd"}}}})))["rows"][0]["preview"]["elements"].size() == 8);
}

namespace {
// Thing:GetScore, pure, returning an int: what call-pure with each calls.
void TheScoreFunction() {
    static const bool added = [] {
        TheFixture();
        auto& world = TheWorld();
        Object& object = world.Add("GetScore", "Function", 64);
        Function function;
        function.slot = object.ref.index;
        function.blockSize = 4;
        auto value = MakeField("ReturnValue", "IntProperty", 0, 4);
        value.owner = "GetScore";
        value.numbers.flags = ue::Param | ue::Output | ue::Return | ue::Plain;
        function.parameters = {value};
        world.functions.push_front(std::move(function)); // calls-read reads the last one.
        return true;
    }();
    (void)added;
}
}

TEST_CASE("each never calls: a function goes to call-each, which does") {
    TheScoreFunction();
    const auto each = Envelope(CallText("each", {{"cls", "Thing"}, {"paths", PathsOf("GetScore")}}));
    REQUIRE_FALSE(each.value("ok", true));
    CHECK(each["error"]["message"].get<std::string>().starts_with("GetScore is a function of Thing: call-each"));
    // The fake engine refuses any call: reaching it is the point.
    const auto eachCall = [](const char* function) {
        return Envelope(CallText("call-each", {{"cls", "Thing"}, {"function", function}}));
    };
    const auto called = eachCall("GetScore");
    REQUIRE_FALSE(called.value("ok", true));
    CHECK(called["error"]["message"].get<std::string>().find("the fake engine calls nothing") != std::string::npos);
    const auto missing = eachCall("Health");
    REQUIRE_FALSE(missing.value("ok", true));
    CHECK(missing["error"]["message"] == "no function Health on Thing (each reads fields)");
}

TEST_CASE("calls-read decodes a recorded block through the shared reader, as a copy") {
    TheExtras();
    const auto calls = Result(CallText("calls-read"));
    CHECK(calls["function"] == "TakeDamage");
    REQUIRE(calls["rows"].size() == 1);
    const auto& row = calls["rows"][0];
    CHECK(row["object_name"] == "Thing_0");
    std::map<std::string, Json> values;
    for (const auto& value : row["values"]) values[value["name"].get<std::string>()] = value["value"];
    CHECK(values["Instigator"]["name"] == "Buddy");   // Named, as typed call outputs are.
    CHECK(values["Bone"] == "head");                  // FNames resolved.
    CHECK(values["Amount"] == 12.5);
    CHECK(values["Label"] != "Rifle");                // The caller's string memory is not read.
}
