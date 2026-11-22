// reagentlive engine layout (engine_layout.h): every member has one profile
// entry, and the profile text is what the profile tools read.
#include "reagentlive/packs/ue/engine_layout.h"
#include "doctest.h"
#include <algorithm>
#include <iterator>
#include <ostream>
#include <set>
#include <string>

using namespace reagentlive::ue::layout;

TEST_CASE("every layout member has exactly one profile entry") {
    std::set<std::string> names;
    for (const auto& member : Members) {
        CHECK(names.insert(std::string(member.group) + "." + std::string(member.name)).second);
        CHECK(std::ranges::count(Members, member.field, &Member::field) == 1);
    }
    CHECK(std::size(Members) * sizeof(int32_t) == sizeof(EngineLayout));
}

TEST_CASE("stock UE5 knows every member") {
    for (const auto& member : Members) CHECK_MESSAGE(StockUE5.*member.field >= 0, member.name);
}

TEST_CASE("a profile groups members and leaves unknown ones out") {
    EngineLayout layout{};
    for (const auto& member : Members) layout.*member.field = -1;
    layout.superStruct = 0x40;
    layout.structSize = 0x58;
    layout.rowMap = 0x30;
    CHECK(ProfileJson(layout) ==
          "{\n  \"groups\": {\n    \"struct\": {\"SuperStruct\": \"0x40\", \"Size\": \"0x58\"},\n"
          "    \"datatable\": {\"RowMap\": \"0x30\"}\n  }\n}\n");
    for (const auto& member : Members) layout.*member.field = -1;
    CHECK(ProfileJson(layout) == "{\n  \"groups\": {\n  }\n}\n");
}
