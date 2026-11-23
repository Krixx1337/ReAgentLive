// reagentlive console tests: the real console against the fake engine
// (fake_engine.h). One fixture object is read through every verb that decodes
// values, and the same member must print the same JSON in each; status keys
// and stale object arguments must be reported alike. A divergence still
// known is marked should_fail, so the test turns red when it is fixed and
// the mark can go.
#include "fake_engine.h"
#include <map>

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

// A get row's value by path.
Json Get(const std::string& path, const std::string& flags = {}) {
    const auto result = Result("get " + TheFixture().thing + " " + path + (flags.empty() ? "" : " " + flags));
    REQUIRE(result["rows"].size() == 1);
    return result["rows"][0]["value"];
}
// Thing_0's cell for one each column.
Json Each(const std::string& path, const std::string& flags = {}) {
    const auto result = Result("each Thing " + path + " --where Name~Thing_0" + (flags.empty() ? "" : " " + flags));
    REQUIRE(result["rows"].size() == 1);
    return result["rows"][0]["values"][0];
}
// A dump row's value by field name.
Json DumpRow(const std::string& name, const std::string& key = "value", const std::string& flags = {}) {
    const auto result = Result("dump " + TheFixture().thing + (flags.empty() ? "" : " " + flags));
    for (const auto& row : result["rows"])
        if (row["name"] == name) return row.value(key, Json());
    FAIL("no dump row " << name);
    return {};
}
}

TEST_CASE("the fake engine answers the console") {
    const auto& fixture = TheFixture();
    const auto described = Result("describe " + fixture.thing);
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

TEST_CASE("--nonzero agrees across verbs") {
    const Json info = Get("Info", "--nonzero");
    CHECK_FALSE(info.contains("Zero"));
    CHECK(info["Scores"]["rows"][1] == Json{{"key", "Deaths"}});
    CHECK(DumpRow("Info", "value", "--nonzero") == info);
    CHECK(Each("Info.Scores", "--nonzero") == info["Scores"]);
}

TEST_CASE("--meta agrees across verbs: the reflected layout from one reader") {
    const Json tag = Get("Info.Tag", "--meta");
    REQUIRE(tag.contains("members"));
    CHECK(tag["members"][0]["value"]["name"] == "Item.Weapon.Rifle");
    CHECK(Each("Info.Tag", "--meta") == tag);
    const Json info = Get("Info", "--meta");
    CHECK(DumpRow("Info", "value", "--meta") == info);
}

TEST_CASE("top-level containers: get and each in full, entries and dump previews element by element") {
    const Json boxes = Get("Boxes");
    CHECK(Each("Boxes") == boxes);
    CHECK(Get("Scores") == Each("Scores"));
    const auto entries = Result("entries " + TheFixture().thing + " Boxes");
    CHECK(entries["rows"][0]["value"] == boxes["rows"][0]);
    const Json preview = DumpRow("Boxes", "preview");
    CHECK(preview["elements"][0]["value"] == boxes["rows"][0]);
    // The header beside a whole preview is not cut: the preview lists every element.
    CHECK(DumpRow("Boxes", "truncated").is_null());
    CHECK_FALSE(DumpRow("Boxes").contains("truncated"));
}

TEST_CASE("past the pointer cap, every verb counts unnamed alike, digested or not") {
    const auto& fixture = TheFixture();
    const auto get = Result("get " + fixture.thing + " Crowd --elements 256");
    REQUIRE(get.contains("unnamed"));
    const auto unnamed = get["unnamed"].get<uint32_t>();
    CHECK(unnamed == fixture.crowd - 112);
    const auto each = Result("each Thing Crowd --where Name~Thing_0 --elements 256");
    CHECK(each["unnamed"] == unnamed);
    // A digest keeps it.
    const auto records = RunLines("get " + fixture.thing + " Crowd --elements 256 --budget 1000");
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].contains("digest"));
    CHECK(records[0]["digest"]["status"]["unnamed"] == unnamed);
}

TEST_CASE("a stale object fails alike as a positional and as --outer") {
    const auto& fixture = TheFixture();
    for (const std::string line : {"get " + fixture.stale + " Health", "describe " + fixture.stale,
                                   "find --cls Thing --outer " + fixture.stale,
                                   "each Thing Health --outer " + fixture.stale}) {
        const auto records = RunLines(line);
        REQUIRE(records.size() == 1);
        INFO(line << " -> " << records[0].dump());
        CHECK_FALSE(records[0]["ok"].get<bool>());
        CHECK(records[0]["error"]["status"] == static_cast<int>(Status::NotFound));
    }
    // An outer named by text that matches nothing is an answer: no rows.
    CHECK(Result("find --cls Thing --outer NoSuchPackage")["rows"].empty());
    CHECK(Result("find --cls Thing --outer " + fixture.package)["total"] == 3);
}

TEST_CASE("fields --match keeps the matching fields, any case") {
    const auto all = Result("fields " + TheFixture().thing);
    const auto some = Result("fields " + TheFixture().thing + " --match sCORE");
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
    const auto fields = Result("fields " + extras.longClass);
    REQUIRE(fields["rows"].size() == 1);
    CHECK(fields["rows"][0]["name"] == std::string(120, 'L'));
    CHECK(fields["rows"][0]["owner"] == extras.longClass);
}

TEST_CASE("dump --own keeps the fields a 51-character class declares") {
    const auto dumped = Result("dump " + TheExtras().armor + " --own");
    REQUIRE(dumped["rows"].size() == 1);
    CHECK(dumped["rows"][0]["name"] == "Durability");
}

TEST_CASE("enum names an enum's values, past one page") {
    TheExtras();
    const auto named = Result("enum EBallisticResponse");
    CHECK(named["name"] == "EBallisticResponse");
    REQUIRE(named["rows"].size() == 300);
    CHECK(named["rows"][0] == Json{{"name", "EBallisticResponse::Value0"}, {"value", 0}});
    CHECK(named["rows"][299] == Json{{"name", "EBallisticResponse::Value299"}, {"value", 598}});
    const auto records = RunLines("enum Wide");
    REQUIRE(records.size() == 1);
    CHECK_FALSE(records[0]["ok"].get<bool>()); // A class is no enum.
}

TEST_CASE("a bare Default__ name is the default object, not a class") {
    TheExtras();
    const auto fields = Result("fields Default__Wide");
    CHECK(fields["rows"].size() == 120);
    const auto dumped = Result("dump Default__Wide --match Field_7");
    CHECK(dumped["rows"].size() > 0);
}

TEST_CASE("a class's fields come in few pages") {
    const auto& extras = TheExtras();
    const uint32_t before = extras.wide->pages;
    const auto fields = Result("fields Wide");
    CHECK(fields["rows"].size() == 120);
    const uint32_t pages = extras.wide->pages - before;
    CHECK(pages * 4 <= (120 + 10) / 11); // At least 4x fewer than 11 entries a page.
}

TEST_CASE("dump --elements lifts a top-level array's preview past 8") {
    const auto& fixture = TheFixture();
    const auto dumped = Result("dump " + fixture.thing + " --match Crowd --elements 12");
    REQUIRE(dumped["rows"].size() == 1);
    const auto& preview = dumped["rows"][0]["preview"];
    CHECK(preview["total"] == fixture.crowd);
    REQUIRE(preview["elements"].size() == 12);
    CHECK(preview["elements"][11]["object"]["name"] == "Extra_11");
    CHECK(Result("dump " + fixture.thing + " --match Crowd")["rows"][0]["preview"]["elements"].size() == 8);
}

TEST_CASE("calls-read decodes a recorded block through the shared reader, as a copy") {
    TheExtras();
    const auto calls = Result("calls-read");
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
// lists: a function's start (0x1000) and a chained chunk (0x1200).
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
        image.Put32(0x80 + 160, 0x5000);    // Exception directory: three entries.
        image.Put32(0x80 + 164, 3 * 12);
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
        image.Put(0x6000, {0x01, 0x04, 0x00, 0x00});
        image.Put(0x6010, {0x01, 0x04, 0x00, 0x00});
        // sub rsp, 0x28; mov eax, 1; add rsp, 0x28; ret
        image.Put(0x1000, {0x48, 0x83, 0xec, 0x28, 0xb8, 0x01, 0, 0, 0, 0x48, 0x83, 0xc4, 0x28, 0xc3});
        image.Put(0x1100, {0x48, 0x83, 0xec, 0x28, 0x48, 0x83, 0xc4, 0x28, 0xc3});
        // mov rax, rcx; add rsp, 0x28; ret
        image.Put(0x1200, {0x48, 0x8b, 0xc1, 0x48, 0x83, 0xc4, 0x28, 0xc3});
        image.Put(0x1300, {0x06});          // Undecodable in 64-bit mode.
        return image.Base();
    }();
    return base;
}
std::string At(uint32_t rva) { return std::format("0x{:x}", TheCode() + rva); }
std::string SaveFile(const char* name) {
    return (std::filesystem::temp_directory_path() / std::format("reagentlive-fake-{}-{}", GetCurrentProcessId(), name)).string();
}
}

TEST_CASE("disasm --save walks past a one-byte decoy entry and refuses an empty listing") {
    // A decoy at the function's start: walked to its return and saved.
    const auto file = SaveFile("decoy.jsonl");
    const auto saved = Result("disasm " + At(0x1000) + " --function --save " + file);
    CHECK(saved["saved_rows"] == 4);
    CHECK(saved["stop"] == "walk end");
    CHECK(std::filesystem::file_size(file) > 0);
    // --resume in a one-byte chunk: walked too.
    const auto chunk = Result("disasm " + At(0x1200) + " --function --resume --save " + file);
    CHECK(chunk["saved_rows"] == 3);
    // Without --save the listing is the same walk.
    CHECK(Result("disasm " + At(0x1000) + " --function")["rows"].size() == 4);
    // Nothing decodes: a refusal, and the console keeps running.
    const auto records = RunLines("disasm " + At(0x1300) + " --function --resume --save " + file + "; status");
    REQUIRE(records.size() == 2);
    CHECK_FALSE(records[0]["ok"].get<bool>());
    CHECK(records[0]["error"]["kind"] == "start");
    CHECK(records[1]["ok"].get<bool>());
    std::filesystem::remove(file);
}

TEST_CASE("find --limit pages until N rows and continues after the last") {
    TheFixture();
    const auto first = Result("find Extra_ --limit 30");
    REQUIRE(first["rows"].size() == 30);
    CHECK(first["total"] == 150);
    CHECK(first["rows"][29]["name"] == "Extra_29");
    const auto rest = Result("find Extra_ --limit 200 --start " + std::to_string(first["next_cursor"].get<uint32_t>()));
    REQUIRE(rest["rows"].size() == 120);
    CHECK(rest["rows"][0]["name"] == "Extra_30");
    CHECK_FALSE(rest.contains("next_cursor"));
    CHECK(Result("find Extra_")["rows"].size() == 24);   // One agent page without it.
}

TEST_CASE("disasm counts past 64 instructions and sig-bytes caps a long length") {
    const auto listed = Result("disasm " + At(0x2000) + " 200");
    CHECK(listed["rows"].size() == 200);
    CHECK(listed["stop"] == "count");
    const auto bytes = Result("sig-bytes " + At(0x2000) + " 200");
    CHECK(bytes["length"] == 96);
    CHECK(bytes["capped"] == true);
    CHECK_FALSE(Result("sig-bytes " + At(0x2000) + " 16").contains("capped"));
}
