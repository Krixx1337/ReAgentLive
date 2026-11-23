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

using namespace fake;

namespace {
template<class T> std::span<const uint8_t> AsBytes(const T& value) {
    return {reinterpret_cast<const uint8_t*>(&value), sizeof(value)};
}
struct WeakPtr { int32_t index, serial; };
struct BoxBytes { double center[3]; int32_t name, number; float radius; WeakPtr owner; uint32_t pad; };
static_assert(sizeof(BoxBytes) == 48);
struct ScoreSlot { int32_t name, number; int32_t value, pad; int32_t hashNext, hashIndex; };
static_assert(sizeof(ScoreSlot) == 24);

// The fixture: a Thing whose Info struct holds every kind of value the
// decoder handles, its own top-level containers, and a crowd of pointers.
struct Fixture {
    std::string thing, ghost, package;   // Literal refs.
    std::string stale;                   // A ref to Thing_0 with a serial that moved on.
    uint32_t crowd = 0;
};

std::string Text(const Ref& ref) {
    return std::format("0x{:x}#{}:{}", ref.address, ref.index, ref.serial);
}

const Fixture& TheFixture() {
    static const Fixture fixture = [] {
        auto& world = TheWorld();
        Type& vector = world.AddType("Vector", 24, true);
        vector.fields = {MakeField("X", "DoubleProperty", 0, 8), MakeField("Y", "DoubleProperty", 8, 8), MakeField("Z", "DoubleProperty", 16, 8)};
        Type& tag = world.AddType("GameplayTag", 8, true);
        tag.fields = {MakeField("TagName", "NameProperty", 0, 8)};
        Type& box = world.AddType("Box", 48, true);
        box.fields = {StructField("Center", 0, vector), MakeField("Name", "NameProperty", 24, 8),
            MakeField("Radius", "FloatProperty", 32, 4), MakeField("Owner", "WeakObjectProperty", 36, 8)};
        const auto scoreKey = MakeField("Key", "NameProperty", 0, 8), scoreValue = MakeField("Value", "IntProperty", 0, 4);
        Type& info = world.AddType("Info", 152, true);
        info.fields = {MakeField("Owner", "WeakObjectProperty", 0, 8), MakeField("Stale", "WeakObjectProperty", 8, 8),
            MakeField("Friend", "ObjectProperty", 16, 8), MapField("Scores", 24, scoreKey, scoreValue, 24, 8),
            ArrayField("Boxes", 104, "StructProperty", 48, &box), StructField("Tag", 120, tag),
            StructField("Zero", 128, vector)};
        Type& instanced = world.AddType("InstancedStruct", 16, true);
        Type& thingType = world.AddType("Thing", 0x400, false, "Object");
        thingType.fields = {StructField("Info", 0x30, info), ArrayField("Boxes", 0x100, "StructProperty", 48, &box),
            MapField("Scores", 0x110, scoreKey, scoreValue, 24, 8), ArrayField("Crowd", 0x160, "ObjectProperty", 8),
            MakeField("Health", "FloatProperty", 0x170, 4), StructField("Payload", 0x178, instanced),
            MakeField("Asset", "SoftObjectProperty", 0x188, 40), MakeField("Label", "StrProperty", 0x1b0, 16)};

        Object& package = world.Add("Package_Things", "Package", 64);
        const Ref packageRef = package.ref;
        Object& buddy = world.Instance("Buddy", thingType, packageRef);
        const Ref buddyRef = buddy.ref;
        Object& ghost = world.Instance("Ghost", thingType, packageRef);
        const WeakPtr ghostWeak{ghost.ref.index, ghost.ref.serial};
        ghost.ref.serial = 2; // Freed and its slot reused: the old weak pointer no longer resolves.
        Object& thing = world.Instance("Thing_0", thingType, packageRef);
        const Ref thingRef = thing.ref;

        // Values, written into Thing_0's bytes.
        uint8_t* bytes = world.Bytes(thing);
        const auto boxOf = [&](double x, const char* name, float radius) {
            return BoxBytes{{x, 2.0, 3.0}, world.Name(name), 0, radius, {buddyRef.index, buddyRef.serial}, 0};
        };
        const BoxBytes boxes[2] = {boxOf(1.5, "hand_r_box", 2.35f), boxOf(-4.0, "head_box", 0.5f)};
        const ScoreSlot scores[3] = {{world.Name("Kills"), 0, 7, 0, -1, 0}, {world.Name("Deaths"), 0, 0, 0, -1, 0},
            {world.Name("Assists"), 0, 3, 0, -1, 0}};
        uint8_t* infoAt = bytes + 0x30;
        const WeakPtr owner{buddyRef.index, buddyRef.serial};
        memcpy(infoAt + 0, &owner, 8);
        memcpy(infoAt + 8, &ghostWeak, 8);
        const uint64_t friendAddress = buddyRef.address;
        memcpy(infoAt + 16, &friendAddress, 8);
        SetSparse(infoAt + 24, AsBytes(scores), 3, 24);
        SetArray(infoAt + 104, AsBytes(boxes), 2);
        const int32_t tagName[2] = {world.Name("Item.Weapon.Rifle"), 0};
        memcpy(infoAt + 120, tagName, 8);
        SetArray(bytes + 0x100, AsBytes(boxes), 2);
        SetSparse(bytes + 0x110, AsBytes(scores), 3, 24);
        const float health = 87.5f;
        memcpy(bytes + 0x170, &health, 4);
        // An instanced struct holding a Box, and a soft pointer to Buddy, loaded.
        uint8_t* payload = world.Alloc(sizeof(BoxBytes));
        memcpy(payload, &boxes[0], sizeof(BoxBytes));
        const uint64_t instancedParts[2] = {world.TypeRef(box).address, reinterpret_cast<uint64_t>(payload)};
        memcpy(bytes + 0x178, instancedParts, 16);
        const int32_t softParts[6] = {buddyRef.index, buddyRef.serial, world.Name("/Game/Things/Buddy"), 0,
            world.Name("Buddy"), 0};
        memcpy(bytes + 0x188, softParts, sizeof(softParts));
        const char16_t label[] = u"Rifle \u00e9";
        SetArray(bytes + 0x1b0, {reinterpret_cast<const uint8_t*>(label), sizeof(label)}, static_cast<uint32_t>(std::size(label)));
        // The crowd: more distinct objects than one command names.
        std::vector<uint64_t> crowd;
        for (int i = 0; i < 150; ++i) crowd.push_back(world.Instance("Extra_" + std::to_string(i), vector).ref.address);
        SetArray(bytes + 0x160, {reinterpret_cast<const uint8_t*>(crowd.data()), crowd.size() * 8},
            static_cast<uint32_t>(crowd.size()));

        Fixture out;
        out.thing = Text(thingRef);
        out.package = Text(packageRef);
        out.ghost = Text(Ref{0, ghostWeak.index, ghostWeak.serial});
        out.stale = Text(Ref{thingRef.address, thingRef.index, thingRef.serial + 5});
        out.crowd = static_cast<uint32_t>(crowd.size());
        return out;
    }();
    return fixture;
}

// A field path as calls take it: "Info.Tag" is ["Info", "Tag"].
Json PathOf(std::string_view dotted) {
    Json path = Json::array();
    for (size_t start = 0;;) {
        const size_t dot = dotted.find('.', start);
        path.push_back(std::string(dotted.substr(start, dot == std::string_view::npos ? dot : dot - start)));
        if (dot == std::string_view::npos) return path;
        start = dot + 1;
    }
}
// A list of one path, as get's and each's paths take it.
Json PathsOf(std::string_view dotted) { return Json::array({PathOf(dotted)}); }
// Args with more args (an object, or null for none) merged in.
Json With(Json args, const Json& more) {
    if (more.is_object()) args.update(more);
    return args;
}
// Thing_0's get args: its paths (a list of them), or none for every field.
Json GetArgs(const Json& paths = {}) {
    Json args{{"object", TheFixture().thing}};
    if (paths.is_array()) args["paths"] = paths;
    return args;
}
// each over Thing, Thing_0's row only.
Json EachArgs(const Json& paths) {
    return {{"cls", "Thing"}, {"paths", paths}, {"where", {{"field", Json::array({"Name"})}, {"contains", "Thing_0"}}}};
}
// A get row's value by path; `more` adds args, `keys` the call's own keys (meta).
Json Get(const std::string& path, const Json& more = {}, const Json& keys = {}) {
    const auto result = Result(CallText("get", With(GetArgs(PathsOf(path)), more), keys));
    REQUIRE(result["rows"].size() == 1);
    return result["rows"][0]["value"];
}
// Thing_0's cell for one each column.
Json Each(const std::string& path, const Json& more = {}, const Json& keys = {}) {
    const auto result = Result(CallText("each", With(EachArgs(PathsOf(path)), more), keys));
    REQUIRE(result["rows"].size() == 1);
    return result["rows"][0]["values"][0];
}
// A row of get with no paths, by field name.
Json DumpRow(const std::string& name, const std::string& key = "value", const Json& more = {}, const Json& keys = {}) {
    const auto result = Result(CallText("get", With(GetArgs(), more), keys));
    for (const auto& row : result["rows"])
        if (row["name"] == name) return row.value(key, Json());
    FAIL("no get row " << name);
    return {};
}
}

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
    const auto records = RunLines(CallText("get", With(GetArgs(PathsOf("Crowd")), elements), {{"budget", 1000}}));
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
    const auto stopped = Result(CallText("watch-stop"));      // Another console run: the path comes from the cache file.
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
    CHECK_FALSE(Result(CallText("watch-stop")).contains("file"));
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
// The one record a call printed: its envelope, ok or not.
Json Envelope(const std::string& call) {
    auto records = RunLines(call);
    REQUIRE(records.size() == 1);
    return records.front();
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

namespace {
// Game.exe (fake_engine.h): PE32+ headers, .text at 0x1000 and an exception
// directory of one-byte entries over real code, the decoys a packed image
// lists: a function's start (0x1000) and a chained chunk (0x1200); and a
// switch (0x1400) whose targets lie in its own entry, a chained chunk
// (0x1500) that also holds its table, code no entry lists (0x1600) and
// undecodable bytes (0x1610); a call chain (0x1700) and a function calling
// one more than the callee cap (0x1800).
uint64_t TheCode() {
    static const uint64_t base = [] {
        auto& image = TheImage();
        std::fill(image.bytes + 0x1000, image.bytes + 0x5000, uint8_t{0xcc});
        image.Put32(0x3c, 0x80);
        image.Put32(0x80, 0x00004550);
        image.Put(0x86, {2, 0});            // NumberOfSections.
        image.Put32(0x88, 0x5f5e100);       // TimeDateStamp.
        image.Put(0x94, {0xf0, 0});         // SizeOfOptionalHeader.
        image.Put(0x98, {0x0b, 0x02});
        image.Put32(0x98 + 108, 16);
        image.Put32(0x80 + 160, 0x5000);    // Exception directory.
        image.Put32(0x80 + 164, 11 * 12);
        const uint64_t sections = 0x98 + 0xf0;
        image.Put(sections, {'.', 't', 'e', 'x', 't', 0, 0, 0});
        image.Put32(sections + 8, 0x4000);
        image.Put32(sections + 12, 0x1000);
        image.Put32(sections + 36, 0x60000020);
        image.Put(sections + 40, {'.', 'r', 'd', 'a', 't', 'a', 0, 0});
        image.Put32(sections + 48, 0x3000);
        image.Put32(sections + 52, 0x5000);
        image.Put32(sections + 76, 0x40000040);
        const auto entry = [&](uint32_t index, uint32_t begin, uint32_t end, uint32_t unwind) {
            image.Put32(0x5000 + index * 12, begin);
            image.Put32(0x5000 + index * 12 + 4, end);
            image.Put32(0x5000 + index * 12 + 8, unwind);
        };
        entry(0, 0x1000, 0x1001, 0x6000);             // Decoy over a function's start.
        entry(1, 0x1100, 0x1140, 0x6010);
        entry(2, 0x1200, 0x1201, 0x5000 + 1 * 12 + 1); // A decoy chunk, chained to entry 1.
        entry(3, 0x1400, 0x1480, 0x6020);
        entry(4, 0x1500, 0x1520, 0x5000 + 3 * 12 + 1); // The switch's chained chunk, holding its table.
        uint32_t index = 5;                            // The call chain's functions.
        for (const auto& [begin, end] : {std::pair{0x1700, 0x1706}, {0x1720, 0x172a}, {0x1740, 0x1746}, {0x1760, 0x1766},
                                         {0x1780, 0x1786}, {0x17a0, 0x17a1}})
            entry(index++, begin, end, 0x6000);
        image.Put(0x6020, {0x01, 0x04, 0x00, 0x00});
        // cmp ecx, 4; ja 0x1478; lea rdx, [image base]; movsxd rax, ecx; mov ecx, [rdx + rax*4 + 0x1508];
        // add rcx, rdx; jmp rcx; then two cases (mov eax, N; ret) and the default at 0x1478; the table at
        // 0x1508, after the chained chunk's code.
        image.Put(0x1400, {0x83, 0xf9, 0x04, 0x0f, 0x87, 0x6f, 0, 0, 0, 0x48, 0x8d, 0x15, 0xf0, 0xeb, 0xff, 0xff,
                           0x48, 0x63, 0xc1, 0x8b, 0x8c, 0x82, 0x08, 0x15, 0, 0, 0x48, 0x03, 0xca, 0xff, 0xe1,
                           0xb8, 0, 0, 0, 0, 0xc3, 0xb8, 0x01, 0, 0, 0, 0xc3});
        uint32_t slot = 0x1508;
        for (const uint32_t target : {0x141f, 0x1425, 0x1500, 0x1600, 0x1610}) image.Put32(std::exchange(slot, slot + 4), target);
        image.Put(0x1478, {0x33, 0xc0, 0xc3});
        image.Put(0x1500, {0xb8, 0x02, 0, 0, 0, 0xc3});
        image.Put(0x1600, {0xb8, 0x03, 0, 0, 0, 0xc3});
        image.Put(0x1610, {0x06});
        image.Put(0x6000, {0x01, 0x04, 0x00, 0x00});
        image.Put(0x6010, {0x01, 0x04, 0x00, 0x00});
        // sub rsp, 0x28; mov eax, 1; add rsp, 0x28; ret
        image.Put(0x1000, {0x48, 0x83, 0xec, 0x28, 0xb8, 0x01, 0, 0, 0, 0x48, 0x83, 0xc4, 0x28, 0xc3});
        image.Put(0x1100, {0x48, 0x83, 0xec, 0x28, 0x48, 0x83, 0xc4, 0x28, 0xc3});
        // mov rax, rcx; add rsp, 0x28; ret
        image.Put(0x1200, {0x48, 0x8b, 0xc1, 0x48, 0x83, 0xc4, 0x28, 0xc3});
        image.Put(0x1300, {0x06});          // Undecodable in 64-bit mode.
        // A call chain, each function in its entry: 0x1700 calls 0x1720, which calls 0x1740 and
        // tail-jumps to 0x1760; 0x1740 calls 0x1780, which calls 0x17a0.
        image.Put(0x1700, {0xe8, 0x1b, 0, 0, 0, 0xc3});
        image.Put(0x1720, {0xe8, 0x1b, 0, 0, 0, 0xe9, 0x36, 0, 0, 0});
        image.Put(0x1740, {0xe8, 0x3b, 0, 0, 0, 0xc3});
        image.Put(0x1760, {0xb8, 0x05, 0, 0, 0, 0xc3});
        image.Put(0x1780, {0xe8, 0x1b, 0, 0, 0, 0xc3});
        image.Put(0x17a0, {0xc3});
        // 0x1800 calls 17 one-byte functions (0x1900, 0x1910, ...): one more than the cap.
        for (uint32_t i = 0; i < 17; ++i) {
            const uint32_t at = 0x1800 + i * 5, target = 0x1900 + i * 0x10;
            image.Put(at, {0xe8});
            image.Put32(at + 1, target - (at + 5));
            image.Put(target, {0xc3});
        }
        image.Put(0x1800 + 17 * 5, {0xc3});
        std::fill(image.bytes + 0x3000, image.bytes + 0x3100, uint8_t{0x90}); // Nops: no signature is unique.
        return image.Base();
    }();
    return base;
}
std::string At(uint32_t rva) { return std::format("0x{:x}", TheCode() + rva); }
std::string SaveFile(const char* name) {
    return (std::filesystem::temp_directory_path() / std::format("reagentlive-fake-{}-{}", GetCurrentProcessId(), name)).string();
}
}

TEST_CASE("disasm with save walks past a one-byte decoy entry and refuses an empty listing") {
    // A decoy at the function's start: walked to its return and saved.
    const auto file = SaveFile("decoy.jsonl");
    const auto saved = Result(CallText("disasm", {{"addr", At(0x1000)}, {"function", true}, {"save", {{"file", file}}}}));
    CHECK(saved["saved_rows"] == 4);
    CHECK(saved["stop"] == "walk end");
    CHECK(std::filesystem::file_size(file) > 0);
    // resume in a one-byte chunk: walked too.
    const Json resume{{"function", true}, {"resume", true}, {"save", {{"file", file}}}};
    const auto chunk = Result(CallText("disasm", With({{"addr", At(0x1200)}}, resume)));
    CHECK(chunk["saved_rows"] == 3);
    // Without save the listing is the same walk.
    CHECK(Result(CallText("disasm", {{"addr", At(0x1000)}, {"function", true}}))["rows"].size() == 4);
    // Nothing decodes: a refusal, and the console keeps running.
    const auto records = RunLines(CallsText({CallOf("disasm", With({{"addr", At(0x1300)}}, resume)), CallOf("status")}));
    REQUIRE(records.size() == 2);
    CHECK_FALSE(records[0]["ok"].get<bool>());
    CHECK(records[0]["error"]["kind"] == "start");
    CHECK(records[1]["ok"].get<bool>());
    std::filesystem::remove(file);
}

TEST_CASE("disasm save callees follows calls and tail jumps breadth-first to the depth, inside the caps") {
    const auto file = SaveFile("callees.jsonl");
    const auto save = [&](uint32_t rva, int depth) {
        return Result(CallText("disasm", {{"addr", At(rva)}, {"function", true}, {"save", {{"file", file}, {"callees", depth}}}}));
    };
    const auto starts = [](const Json& saved) {
        std::vector<std::pair<std::string, int>> out;
        for (const auto& one : saved["functions"]) out.push_back({one["start"].get<std::string>(), one["depth"].get<int>()});
        return out;
    };
    using Starts = std::vector<std::pair<std::string, int>>;
    // Depth 1: the call and nothing past it; the next level is listed as left out by depth.
    const auto one = save(0x1700, 1);
    CHECK(starts(one) == Starts{{At(0x1700), 0}, {At(0x1720), 1}});
    CHECK(one["skipped"] == Json::parse(R"([{"address": ")" + At(0x1740) + R"(", "why": "depth"}, {"address": ")" +
                                         At(0x1760) + R"(", "why": "depth"}])"));
    // Depth 3: the call, then its call and tail jump, then the call below those.
    const auto three = save(0x1700, 3);
    CHECK(starts(three) == Starts{{At(0x1700), 0}, {At(0x1720), 1}, {At(0x1740), 2}, {At(0x1760), 2}, {At(0x1780), 3}});
    CHECK(three["skipped"] == Json::parse(R"([{"address": ")" + At(0x17a0) + R"(", "why": "depth"}])"));
    size_t functionRows = 0;
    std::ifstream in(file);
    for (std::string line; std::getline(in, line);) functionRows += Json::parse(line)["row"] == "function";
    in.close();
    CHECK(functionRows == 5);
    // The count cap holds for the whole save: 16 callees, the 17th skipped.
    const auto capped = save(0x1800, 1);
    CHECK(capped["functions"].size() == 17);
    CHECK(capped["skipped"] == Json::parse(R"([{"address": ")" + At(0x1900 + 16 * 0x10) + R"(", "why": "count"}])"));
    std::filesystem::remove(file);
}

TEST_CASE("find with limit pages until N rows and continues after the last") {
    TheFixture();
    const auto first = Result(CallText("find", {{"text", "Extra_"}, {"view", {{"limit", 30}}}}));
    REQUIRE(first["rows"].size() == 30);
    CHECK(first["total"] == 150);
    CHECK(first["rows"][29]["name"] == "Extra_29");
    const auto rest = Result(CallText("find", {{"text", "Extra_"}, {"view", {{"limit", 200}, {"start", first["next_cursor"]}}}}));
    REQUIRE(rest["rows"].size() == 120);
    CHECK(rest["rows"][0]["name"] == "Extra_30");
    CHECK_FALSE(rest.contains("next_cursor"));
    CHECK(Result(CallText("find", {{"text", "Extra_"}}))["rows"].size() == 24);   // One agent page without it.
}

TEST_CASE("disasm counts past 64 instructions and sig-bytes caps a long length") {
    const auto listed = Result(CallText("disasm", {{"addr", At(0x2000)}, {"count", 200}}));
    CHECK(listed["rows"].size() == 200);
    CHECK(listed["stop"] == "count");
    const auto bytes = Result(CallText("sig-bytes", {{"addr", At(0x2000)}, {"len", 200}}));
    CHECK(bytes["length"] == 96);
    CHECK(bytes["capped"] == true);
    CHECK_FALSE(Result(CallText("sig-bytes", {{"addr", At(0x2000)}, {"len", 16}})).contains("capped"));
}

namespace {
std::string LabelLine(uint32_t rva, const char* name, std::string extra = {}) {
    return std::format(R"({{"module":"Game.exe","module_size":{},"timestamp":100000000,"rva":"0x{:x}","name":"{}","kind":"user"{}}})",
        Image::Size, rva, name, extra);
}
std::vector<Json> FileRows(const std::string& path) {
    std::vector<Json> rows;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) rows.push_back(Json::parse(line));
    return rows;
}
}

TEST_CASE("labels-sign marks a row it cannot sign and skips it on this build unless again") {
    TheCode();
    const auto file = SaveFile("labels.jsonl");
    std::ofstream(file, std::ios::binary) << LabelLine(0x1100, "Signable") << '\n' << LabelLine(0x3010, "Twin") << '\n';
    const auto first = Result(CallText("labels-sign", {{"file", file}}));
    CHECK(first["signed"] == 1);
    CHECK(first["unsigned"] == 1);
    CHECK(first["failed_before"] == 0);
    auto rows = FileRows(file);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].contains("sig"));
    CHECK_FALSE(rows[0].contains("sig_failed"));
    REQUIRE(rows[1].contains("sig_failed"));
    CHECK(rows[1]["sig_failed"]["timestamp"] == 100000000);
    CHECK(rows[1]["sig_failed"]["reason"].get<std::string>().starts_with("not unique"));
    // The same build again: nothing tried, nothing searched.
    const auto second = Result(CallText("labels-sign", {{"file", file}}));
    CHECK(second["failed_before"] == 1);
    CHECK(second["skipped"] == 1);
    CHECK(second["searches"] == 0);
    CHECK(second["rows"].empty());
    // again tries every row.
    const auto again = Result(CallText("labels-sign", {{"file", file}, {"again", true}}));
    CHECK(again["rows"].size() == 2);
    CHECK(again["unsigned"] == 1);
    // A mark from another build is tried again and marked for this one.
    std::ofstream(file, std::ios::binary) << LabelLine(0x3010, "Twin", R"(,"sig_failed":{"timestamp":1,"reason":"old"})") << '\n';
    const auto other = Result(CallText("labels-sign", {{"file", file}}));
    CHECK(other["rows"].size() == 1);
    CHECK(other["failed_before"] == 0);
    CHECK(FileRows(file)[0]["sig_failed"]["timestamp"] == 100000000);
    // A row signed later loses its mark.
    std::ofstream(file, std::ios::binary) << LabelLine(0x1100, "Signable", R"(,"sig_failed":{"timestamp":1,"reason":"old"})") << '\n';
    CHECK(Result(CallText("labels-sign", {{"file", file}}))["signed"] == 1);
    rows = FileRows(file);
    CHECK(rows[0].contains("sig"));
    CHECK_FALSE(rows[0].contains("sig_failed"));
    std::filesystem::remove(file);
}

TEST_CASE("run: a plan's steps as NDJSON lines, under the packs ceiling, with expects, repeats and a report") {
    const Json health = CallOf("get", GetArgs(PathsOf("Health")));
    const auto planFile = SaveFile("plan.json");
    const Json steps = Json::array({
        {{"verb", "get"}, {"args", health["args"]}, {"expect", {{"ok", true}}}},
        {{"verb", "write"}, {"args", {{"addr", "0x1000"}, {"data", "00"}}}},          // modify: outside the ceiling.
        {{"verb", "status"}, {"expect", {{"result", {{"tool_packs", Json{"explore"}}}}}}},
        {{"verb", "status"}, {"expect", {{"result", {{"tool_packs", Json{"modify"}}}}}}},
        {{"verb", "modules"}, {"repeat", {{"every", 0.05}, {"for", 0.1}}}},
        // A batch step: its envelope, every item in it, is the step's line.
        {{"verb", "batch"}, {"args", {{"calls", Json::array({health, CallOf("modules")})}}}, {"expect", {{"ok", true}}}},
    });
    std::ofstream(planFile, std::ios::binary) << steps.dump();
    // No out: the lines come back in the result.
    const auto records = RunLines(CallText("run", {{"plan", planFile}}), "explore");
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].value("ok", false));
    const Json& result = records[0]["result"];
    const Json& rows = result["rows"];
    REQUIRE(rows.size() == 7);
    CHECK(result["lines"] == 7);
    CHECK(rows[0]["step"] == 1);
    CHECK(rows[0]["command"] == "get");
    CHECK(rows[0]["expect"] == "met");
    CHECK(rows[0].contains("at_ms"));
    CHECK(rows[1]["error"]["kind"] == "pack");                                    // Refused, and the plan goes on.
    CHECK(rows[2]["expect"] == "met");
    CHECK(rows[3]["expect"] == "failed");
    CHECK(rows[3]["expect_failed"].get<std::string>().starts_with("result.tool_packs[0]: wanted \"modify\""));
    CHECK(rows[4]["run"] == 1);
    CHECK(rows[5]["run"] == 2);                                                   // every 0.05 for 0.1: two runs.
    CHECK(rows[6]["command"] == "batch");
    CHECK(rows[6]["ok"] == true);
    CHECK(rows[6]["expect"] == "met");
    REQUIRE(rows[6]["result"]["rows"].size() == 2);
    CHECK(rows[6]["result"]["rows"][0]["command"] == "get");
    CHECK(rows[6]["result"]["rows"][1]["command"] == "modules");
    const Json& report = result["report"];
    CHECK(report["steps"] == 6);
    CHECK(report["calls"] == 7);
    CHECK(report["ok"] == 6);
    CHECK(report["failed"] == 1);
    CHECK(report["refused"] == Json{{"pack", 1}});
    CHECK(report["expect_met"] == 3);
    CHECK(report["expect_failed"] == 1);
    REQUIRE(report["timings"].size() == 6);
    CHECK(report["timings"][4]["calls"] == 2);
    CHECK(report["timings"][4]["ms"].get<double>() >= 0);
    CHECK_FALSE(report["console"].get<std::string>().empty());
    CHECK_FALSE(report["table_hash"].get<std::string>().empty());
    // out: the lines and a last report line go to the file; the result keeps the report.
    const auto outFile = SaveFile("plan-out.jsonl");
    const auto written = Result(CallText("run", {{"plan", planFile}, {"out", outFile}}));
    CHECK(written["file"] == outFile);
    CHECK_FALSE(written.contains("rows"));
    std::vector<Json> lines;
    {
        std::ifstream in(outFile, std::ios::binary);
        for (std::string line; std::getline(in, line);) lines.push_back(Json::parse(line));
    }
    REQUIRE(lines.size() == 8);
    CHECK(lines[1].value("error", Json::object()).value("kind", "") != "pack");    // Every pack: the write is tried.
    CHECK(lines[7]["report"]["calls"] == 7);
    CHECK(lines[7]["report"]["steps"] == 6);
    CHECK(lines[6]["command"] == "batch");
    CHECK(lines[6]["ok"] == true);
    // A bad plan is refused before any step runs; run is not allowed in a plan.
    std::ofstream(planFile, std::ios::binary) << R"([{"verb": "status"}, {"verb": "run", "args": {"plan": "x"}}])";
    const auto refused = Envelope(CallText("run", {{"plan", planFile}}));
    CHECK(refused["error"]["kind"] == "schema");
    CHECK(refused["error"]["message"] == "plan step 2: run is not allowed in a plan");
    CHECK(Envelope(CallText("run", {{"plan", SaveFile("missing.json")}}))["error"]["kind"] == "file");
    std::filesystem::remove(planFile);
    std::filesystem::remove(outFile);
}

TEST_CASE("--packs is a ceiling: a verb of another pack is refused, inside a batch too") {
    const Json health = CallOf("get", GetArgs(PathsOf("Health")));
    // explore and native: a write and a capture are refused by name, reads run.
    const auto records = RunLines(CallsText({CallOf("write", {{"addr", "0x1000"}, {"data", "00"}}), CallOf("watch-read"),
        health, CallOf("status")}), "native");
    REQUIRE(records.size() == 4);
    CHECK(records[0]["error"]["kind"] == "pack");
    CHECK(records[0]["error"]["message"] ==
          "write is in the modify pack, outside this console's --packs explore,native: start it with modify in --packs");
    CHECK(records[1]["error"]["kind"] == "pack");
    CHECK(records[2].value("ok", false));
    CHECK(records[3]["result"]["tool_packs"] == Json{"explore", "native"});
    // A batch is explore; its items obey the ceiling one by one.
    const auto batch = RunLines(CallText("batch", {{"calls", Json::array({health,
        CallOf("call-each", {{"cls", "Thing"}, {"function", "GetScore"}})})}}), "explore");
    REQUIRE(batch.size() == 1);
    const auto& rows = batch[0]["result"]["rows"];
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].value("ok", false));
    CHECK(rows[1]["error"]["kind"] == "pack");
    // Default: every pack; one call alone is checked the same way.
    CHECK(Result(CallText("status"))["tool_packs"] == Json{"explore", "native", "observe", "modify"});
    const auto json = RunLines(CallText("watch-read"), "explore");
    REQUIRE(json.size() == 1);
    CHECK(json[0]["error"]["kind"] == "pack");
    // An unknown pack stops the console at startup.
    const auto bad = RunLines(CallText("status"), "explore,writes");
    REQUIRE(bad.size() == 1);
    CHECK(bad[0]["error"]["kind"] == "startup");
}

namespace {
// Curve tables: one simple (linear), one rich (constant, linear and cubic keys).
struct RichKey { uint8_t interp, tangentMode, weightMode, pad; float time, value, arrive, arriveWeight, leave, leaveWeight; };
static_assert(sizeof(RichKey) == ue::layout::RichCurveKeySize);
struct CurveRow { int32_t name, number; uint64_t curve; int32_t hashNext, hashIndex; };
static_assert(sizeof(CurveRow) == 24);
struct CurveTables { std::string simple, rich, derived, thing; };
const CurveTables& TheCurveTables() {
    static const CurveTables tables = [] {
        const auto& layout = ue::layout::StockUE5;
        auto& world = TheWorld();
        Type& curveTable = world.AddType("CurveTable", 0xA0, false, "Object");
        Type& derivedType = world.AddType("GameCurves", 0xA0, false, "CurveTable");
        const auto table = [&](const Type& type, const char* name, uint8_t mode, std::vector<CurveRow> rows) {
            Object& object = world.Instance(name, type);
            uint8_t* bytes = world.Bytes(object);
            SetSparse(bytes + layout.curveRowMap, {reinterpret_cast<const uint8_t*>(rows.data()), rows.size() * sizeof(CurveRow)},
                      static_cast<uint32_t>(rows.size()), 24);
            bytes[layout.curveTableMode] = mode;
            return Text(object.ref);
        };
        const auto curve = [&](std::span<const uint8_t> keys, uint32_t count, int32_t keysAt, int interp) {
            uint8_t* bytes = world.Alloc(0x90);
            if (interp >= 0) bytes[layout.simpleCurveInterp] = static_cast<uint8_t>(interp);
            SetArray(bytes + keysAt, keys, count);
            return reinterpret_cast<uint64_t>(bytes);
        };
        const float sway[] = {1, 1, 3, 1, 4, 1.3f};
        const float flat[] = {0, 5};
        const uint64_t swayCurve = curve(AsBytes(sway), 3, layout.simpleCurveKeys, 0);
        const uint64_t flatCurve = curve(AsBytes(flat), 1, layout.simpleCurveKeys, 1);
        const RichKey rich[] = {{1, 0, 0, 0, 0, 10, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 20, 0, 0, 0, 0},
                                {2, 0, 0, 0, 2, 40, 0, 0, 4, 0}, {0, 0, 0, 0, 4, 0, -8, 0, 0, 0}};
        const uint64_t richCurve = curve(AsBytes(rich), 4, layout.richCurveKeys, -1);
        CurveTables out;
        out.simple = table(curveTable, "CT_Sway", 1, {{world.Name("Pitch"), 0, swayCurve, -1, 0},
                                                       {world.Name("Flat"), 0, flatCurve, -1, 0}});
        out.rich = table(curveTable, "CT_Skills", 2, {{world.Name("Level"), 0, richCurve, -1, 0}});
        out.derived = table(derivedType, "CT_Derived", 1, {{world.Name("Pitch"), 0, swayCurve, -1, 0}});
        out.thing = TheFixture().thing;
        return out;
    }();
    return tables;
}
}

TEST_CASE("rows reads a curve table's keys and evaluates them with at") {
    const auto& tables = TheCurveTables();
    const auto rows = [](const std::string& table, const Json& more = {}) { return CallOf("rows", With({{"object", table}}, more)); };
    const auto simple = Result(rows(tables.simple).dump());
    CHECK(simple["curve"] == "simple");
    CHECK_FALSE(simple.contains("row_struct"));
    REQUIRE(simple["rows"].size() == 2);
    CHECK(simple["rows"][0]["name"] == "Pitch");
    CHECK(simple["rows"][0]["interp"] == "linear");
    CHECK(simple["rows"][0]["keys"] == Json::parse("[[1, 1], [3, 1], [4, 1.3]]"));
    CHECK_FALSE(simple["rows"][0].contains("value"));
    // An address alone names the table too.
    CHECK(Result(rows(tables.simple.substr(0, tables.simple.find('#'))).dump())["rows"] == simple["rows"]);
    // Linear between keys, the end key's value outside them, constant keeps its value.
    const auto at = Result(rows(tables.simple, {{"at", 3.5}}).dump());
    CHECK(at["at"] == 3.5);
    CHECK(at["rows"][0]["value"] == 1.15);
    CHECK(Result(rows(tables.simple, {{"at", 9}}).dump())["rows"][0]["value"] == 1.3);
    const auto flat = Result(rows(tables.simple, {{"at", -1}, {"view", {{"match", "flat"}}}}).dump());
    REQUIRE(flat["rows"].size() == 1);
    CHECK(flat["total"] == 1);
    CHECK(flat["rows"][0]["interp"] == "constant");
    CHECK(flat["rows"][0]["keys"] == Json::parse("[[0, 5]]"));
    CHECK(flat["rows"][0]["value"] == 5);

    const auto rich = Result(rows(tables.rich).dump());
    CHECK(rich["curve"] == "rich");
    CHECK(rich["rows"][0]["keys"][2] == Json::parse(R"([2, 40, "cubic", 0, 4])"));
    const auto value = [&](double t) { return Result(rows(tables.rich, {{"at", t}}).dump())["rows"][0]["value"].get<double>(); };
    CHECK(value(0.5) == 10);                // constant
    CHECK(value(1.5) == 30);                // linear
    // Cubic from (2, 40) leaving at 4 to (4, 0) arriving at -8: P1 = 40 + 4*2/3, P2 = 0 + 8*2/3, at alpha 0.5.
    CHECK(value(3) == doctest::Approx((40 + 3 * (40 + 8.0 / 3) + 3 * (16.0 / 3) + 0) / 8).epsilon(1e-5));

    // A subclass of CurveTable is read too; refusals name both table kinds and the wrong args.
    CHECK(Result(rows(tables.derived).dump())["rows"][0]["name"] == "Pitch");
    const auto refused = RunLines(CallsText({rows(tables.thing), rows(tables.simple, {{"view", {{"fields", PathsOf("A")}}}}),
        rows(tables.simple, {{"at", "x"}})}));
    REQUIRE(refused.size() == 3);
    CHECK(refused[0]["error"]["message"] == "neither a data table (no RowStruct field) nor a curve table (no CurveTable class)");
    CHECK(refused[1]["error"]["message"].get<std::string>().find("read data tables; a curve table's rows are keys") !=
          std::string::npos);
    CHECK(refused[2]["error"]["kind"] == "schema");
}

TEST_CASE("export-assets writes each object's fields and a table's rows, one file each, and an index") {
    const auto& fixture = TheFixture();
    TheCurveTables();
    const auto dir = std::filesystem::path(SaveFile("export"));
    std::filesystem::remove_all(dir);
    const auto result = Result(CallText("export-assets", {{"dir", dir.string()}, {"cls", Json::array({"Thing", "CurveTable"})}}));
    CHECK(result["failed"] == 0);
    CHECK(result["capped"] == false);
    CHECK(result["exported"] == result["rows"].size());
    const auto index = Json::parse(std::ifstream(result["index"].get<std::string>()));
    CHECK(index.size() == result["exported"]);
    bool thing = false;
    uint64_t tables = 0;
    for (const auto& row : index) {
        const auto file = Json::parse(std::ifstream(row["file"].get<std::string>()));
        CHECK(file["ref"] == row["ref"]);
        CHECK(file["get"].contains("rows"));
        if (row["name"] == "Thing_0") {
            thing = true;
            CHECK(file["ref"] == fixture.thing);
            CHECK_FALSE(file.contains("rows"));
        }
        if (file.contains("rows")) {
            ++tables;
            CHECK(row["table_rows"] == file["rows"]["rows"].size());
            CHECK(file["rows"].contains("curve"));
        }
    }
    CHECK(thing);
    CHECK(tables == result["tables"]);
    CHECK(tables == 3); // CT_Derived too: a subclass of the class asked for.
    const auto sway = Result(CallText("export-assets", {{"dir", dir.string()}, {"cls", Json::array({"CurveTable"})}, {"view", {{"match", "sway"}}}}));
    CHECK(sway["exported"] == 1);
    CHECK(sway["rows"][0]["table_rows"] == 2);
    // A missing cls is refused before running.
    CHECK(RunLines(CallText("export-assets", {{"dir", "x"}}))[0]["error"]["kind"] == "schema");
}

TEST_CASE("disasm with save follows a switch: its table is data, each target a chunk, the rest jumpouts_left") {
    const auto file = SaveFile("switch.jsonl");
    const auto saved = Result(CallText("disasm", {{"addr", At(0x1400)}, {"function", true}, {"save", {{"file", file}}}}));
    REQUIRE(saved.contains("tables"));
    CHECK(saved["tables"] == Json::parse(R"([{"jump": ")" + At(0x141d) + R"(", "table": ")" + At(0x1508) +
                                         R"(", "targets": 5}])"));
    std::vector<std::string> chunks;
    for (const auto& chunk : saved["chunks"]) chunks.push_back(chunk["start"].get<std::string>());
    CHECK(chunks == std::vector<std::string>{At(0x1500), At(0x1600)});
    CHECK(saved["jumpouts_left"] == Json::array({At(0x1610)}));
    CHECK_FALSE(saved.contains("capped"));
    // The file: the table as one data row, never as code (the chunk that runs through it stops there).
    std::ifstream in(file);
    bool table = false;
    for (std::string line; std::getline(in, line);) {
        const auto row = Json::parse(line);
        if (row["row"] == "data" && row["address"] == At(0x1508)) table = row["bytes"].get<std::string>().size() == 40;
        if (row["row"] == "code") {
            const uint64_t at = std::stoull(row["address"].get<std::string>(), nullptr, 16);
            const uint64_t end = at + row["bytes"].get<std::string>().size() / 2;
            CHECK_FALSE((at < TheCode() + 0x151c && end > TheCode() + 0x1508));
        }
    }
    CHECK(table);
}

namespace {
// Layout types: a class with a parent, a struct, two referenced classes (one
// a step further) and an enum, every field kind the export describes.
struct LayoutTypes { std::string file; };
const LayoutTypes& TheLayoutTypes() {
    static const LayoutTypes types = [] {
        auto& world = TheWorld();
        const auto own = [](Field field, const char* owner, std::string subtype = {}) {
            field.owner = owner;
            field.subtype = std::move(subtype);
            return field;
        };
        Type& base = world.AddType("LayoutBase", 0x40, false);
        base.fields = {own(MakeField("Health", "FloatProperty", 0x28, 4), "LayoutBase")};
        Type& spot = world.AddType("LayoutSpot", 0x10, true);
        spot.fields = {own(MakeField("X", "FloatProperty", 0, 4), "LayoutSpot"), own(MakeField("Y", "FloatProperty", 4, 4), "LayoutSpot")};
        Type& deep = world.AddType("LayoutDeep", 0x30, false);
        Type& other = world.AddType("LayoutOther", 0x30, false);
        other.fields = {own(MakeField("Deep", "ObjectProperty", 0x28, 8), "LayoutOther", "LayoutDeep")};
        Type& thing = world.AddType("LayoutThing", 0x100, false, "LayoutBase");
        Field flag = own(MakeField("bFlag", "BoolProperty", 0xc0, 1), "LayoutThing");
        flag.numbers.boolMask = 4;
        Field others = own(ArrayField("Others", 0x60, "ObjectProperty", 8), "LayoutThing", "LayoutOther");
        Field value = MakeField("", "ByteProperty", 0, 1);
        value.subtype = "ELayoutRole";
        thing.fields = {own(MakeField("Owner", "ObjectProperty", 0x40, 8), "LayoutThing", "LayoutBase"),
            own(MakeField("Role", "EnumProperty", 0x48, 1), "LayoutThing", "ELayoutRole"),
            own(StructField("Spot", 0x50, spot), "LayoutThing"), others,
            own(MapField("Roles", 0x70, MakeField("", "NameProperty", 0, 8), value, 16, 8), "LayoutThing"), flag,
            // Inherited rows come with the class's table; the export writes them on their own type's lines.
            own(MakeField("Health", "FloatProperty", 0x28, 4), "LayoutBase")};
        (void)deep;
        Object& role = world.Add("ELayoutRole", "Enum", 64);
        world.enums.push_back({role.ref.index, {{"ELayoutRole::A", 0}, {"ELayoutRole::B", 1}}});
        return LayoutTypes{SaveFile("layout.jsonl")};
    }();
    return types;
}
std::vector<Json> FileLines(const std::string& path) {
    std::vector<Json> lines;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) lines.push_back(Json::parse(line));
    return lines;
}
}

TEST_CASE("layout-export writes type lines and full field lines; closure adds what they reference") {
    const auto& types = TheLayoutTypes();
    const auto layout = [&](const Json& classes, const Json& more = {}) {
        return Result(CallText("layout-export", With({{"file", types.file}, {"classes", classes}}, more)));
    };
    const auto alone = layout(Json::array({"LayoutThing"}));
    CHECK(alone["classes"] == 1);
    CHECK(alone["fields"] == 6);
    auto lines = FileLines(types.file);
    REQUIRE(lines.size() == 7);
    CHECK(lines[0] == Json::parse(R"({"kind": "class", "name": "LayoutThing", "super": "LayoutBase", "size": "0x100"})"));
    CHECK(lines[1] == Json::parse(R"({"kind": "field", "owner": "LayoutThing", "field": "Owner", "offset": "0x40",
        "size": 8, "type": "ObjectProperty", "class": "LayoutBase"})"));
    CHECK(lines[2]["enum"] == "ELayoutRole");
    CHECK(lines[3]["struct"] == "LayoutSpot");
    CHECK(lines[4]["element"] == Json::parse(R"({"type": "ObjectProperty", "size": 8, "class": "LayoutOther"})"));
    CHECK(lines[5]["key"] == Json::parse(R"({"type": "NameProperty", "size": 8})"));
    CHECK(lines[5]["value"] == Json::parse(R"({"type": "ByteProperty", "size": 1, "enum": "ELayoutRole"})"));
    CHECK(lines[5]["stride"] == 16);
    CHECK(lines[5]["value_offset"] == "0x8");
    CHECK(lines[6]["bool_mask"] == 4);
    // layout-check reads the same file: the type lines are skipped, every field holds.
    const auto check = Result(CallText("layout-check", {{"file", types.file}}));
    CHECK(check["checked"] == 6);
    CHECK(check["ok"] == true);

    // Depth 1: the super chain and the types one step away, and the enum with its size.
    const auto closure = layout(Json::array({"LayoutThing"}), {{"closure", 1}});
    CHECK(closure["classes"] == 3);
    CHECK(closure["structs"] == 1);
    CHECK(closure["enums"] == 1);
    lines = FileLines(types.file);
    std::vector<std::string> named;
    for (const auto& line : lines)
        if (line["kind"] != "field") named.push_back(line["name"].get<std::string>());
    CHECK(named == std::vector<std::string>{"LayoutThing", "LayoutBase", "LayoutSpot", "LayoutOther", "ELayoutRole"});
    CHECK(lines.back() == Json::parse(R"({"kind": "enum", "name": "ELayoutRole", "size": 1,
        "values": [["ELayoutRole::A", 0], ["ELayoutRole::B", 1]]})"));
    CHECK(std::ranges::count_if(lines, [](const Json& l) { return l.value("field", "") == "Health"; }) == 1);
    // Depth 2 reaches the class LayoutOther points to; an enum and an unknown name by themselves.
    CHECK(layout(Json::array({"LayoutThing"}), {{"closure", 2}})["classes"] == 4);
    const auto enumOnly = layout(Json::array({"ELayoutRole", "NoSuchType"}));
    CHECK(enumOnly["enums"] == 1);
    CHECK(enumOnly["missing"] == Json::array({"NoSuchType"}));
    CHECK_FALSE(FileLines(types.file)[0].contains("size"));
}

TEST_CASE("disasm save adds layout rows for the classes its labelled functions name") {
    TheLayoutTypes();
    const auto labels = SaveFile("save-labels.jsonl");
    {
        std::ofstream out(labels, std::ios::binary | std::ios::trunc);
        out << LabelLine(0x1700, "NoClass_Run") << "\n" << LabelLine(0x1720, "LayoutThing_Do_Tick") << "\n"
            << LabelLine(0x1740, "LayoutSpot::Get") << "\n";
    }
    const auto file = SaveFile("save-layout.jsonl");
    const auto saved = Result(CallText("disasm", {{"addr", At(0x1700)}, {"function", true},
        {"save", {{"file", file}, {"callees", 2}, {"labels", labels}}}}));
    // The longest prefix that is a class (LayoutThing, not LayoutThing_Do); `::` names it whole.
    REQUIRE(saved.contains("layouts"));
    CHECK(saved["layouts"]["classes"] == Json::array({"LayoutThing", "LayoutSpot"}));
    CHECK(saved["layouts"]["types"] == 5);   // The closure of one step: its super, struct, class and enum.
    std::vector<std::string> types;
    for (const auto& row : FileLines(file))
        if (row["row"] == "layout" && row["kind"] != "field") types.push_back(row["name"].get<std::string>());
    CHECK(types == std::vector<std::string>{"LayoutThing", "LayoutSpot", "LayoutBase", "LayoutOther", "ELayoutRole"});
    // No label names a class: no layout rows.
    const auto plain = Result(CallText("disasm", {{"addr", At(0x1760)}, {"function", true}, {"save", {{"file", file}, {"labels", labels}}}}));
    CHECK_FALSE(plain.contains("layouts"));
    std::filesystem::remove(file);
    std::filesystem::remove(labels);
}

TEST_CASE("a map of structs past 512 bytes decodes in get; one with no stride names its element types") {
    TheFixture();
    static const std::string seater = [] {
        World& world = TheWorld();
        Type& camera = world.AddType("SeatCamera", 0x300, true);
        camera.fields = {MakeField("Fov", "FloatProperty", 0, 4)};
        const auto key = MakeField("Key", "IntProperty", 0, 4);
        const auto value = StructField("Value", 0, camera);
        // A curve at its engine defaults: DefaultValue FLT_MAX (unset), an empty key-handle map.
        Type& handles = world.AddType("KeyHandleMap", 0x60, true);
        Type& curve = world.AddType("SeatCurve", 0x70, true);
        curve.fields = {MakeField("DefaultValue", "FloatProperty", 0, 4), StructField("KeyHandlesToIndices", 8, handles),
            MakeField("Gain", "FloatProperty", 0x68, 4)};
        Type& type = world.AddType("Seater", 0x200, false, "Object");
        type.fields = {MapField("Seats", 0x30, key, value, 0x310, 8), MapField("Refused", 0x80, key, value, 0, 0),
            StructField("Curve", 0xd0, curve)};
        const Object& object = world.Instance("Seater_0", type);
        const Ref ref = object.ref;
        uint8_t* curveAt = TheWorld().Bytes(world.objects[static_cast<size_t>(ref.index)]) + 0xd0;
        const float unset = std::numeric_limits<float>::max(), gain = 1.5f;
        memcpy(curveAt, &unset, 4);
        SetSparse(curveAt + 8, {}, 0, 8);
        memcpy(curveAt + 0x68, &gain, 4);
        // Two seats: key 0 and 2, Fov 90 and 70, hash links after the value.
        std::vector<uint8_t> slots(2 * 0x310);
        for (int i = 0; i < 2; ++i) {
            const int32_t seat = i * 2;
            const float fov = i ? 70.0f : 90.0f;
            const int32_t links[2] = {-1, 0};
            memcpy(slots.data() + i * 0x310, &seat, 4);
            memcpy(slots.data() + i * 0x310 + 8, &fov, 4);
            memcpy(slots.data() + i * 0x310 + 0x308, links, 8);
        }
        SetSparse(TheWorld().Bytes(world.objects[static_cast<size_t>(ref.index)]) + 0x30, slots, 2, 0x310);
        return Text(ref);
    }();
    const auto get = [](const char* path) {
        const auto result = Result(CallText("get", {{"object", seater}, {"paths", PathsOf(path)}}));
        REQUIRE(result["rows"].size() == 1);
        return result["rows"][0]["value"];
    };
    const Json seats = get("Seats");
    CHECK(seats["count"] == 2);
    REQUIRE(seats["rows"].size() == 2);
    CHECK(seats["rows"][1]["key"] == 2);
    CHECK(seats["rows"][1]["value"]["Fov"] == 70.0);
    const std::string why = get("Refused")["container_error"].get<std::string>();
    CHECK(why.starts_with("the element layout of Refused (map IntProperty (4 bytes) -> SeatCamera (768 bytes)) is unknown"));
    CHECK(why.find("re-inject") != std::string::npos);                     // A slot past 512 bytes: an older agent's refusal.
    // nonzero: a curve at its defaults shows only what was set.
    const Json curve = get("Curve");
    CHECK(curve["KeyHandlesToIndices"] == Json{{"count", 0}});
    CHECK(curve["DefaultValue"].get<double>() > 3e38);
    const auto set = Result(CallText("get", {{"object", seater}, {"paths", PathsOf("Curve")}, {"view", {{"nonzero", true}}}}));
    CHECK(set["rows"][0]["value"] == Json{{"Gain", 1.5}});
    // entries carries 512-byte slots at most: refused with what to use instead.
    const auto entries = Envelope(CallText("entries", {{"object", seater}, {"field", "Seats"}}));
    CHECK(entries["error"]["kind"] == "validation");
    CHECK(entries["error"]["message"].get<std::string>().find("get the field") != std::string::npos);
}
