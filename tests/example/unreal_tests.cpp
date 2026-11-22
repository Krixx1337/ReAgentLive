// reagentlive Unreal example, end to end: the real DLL loaded into this
// process (no engine here), its thread ticking, a real client attached by pid.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstring>
#include <string>
#include "reagentlive/core/client.h"
#include "reagentlive/packs/ue/protocol_ue.h"
#include "doctest.h"

using namespace reagentlive;

namespace {
template<class Predicate> bool Until(Predicate done, int milliseconds = 5000) {
    for (int waited = 0; waited < milliseconds; waited += 5) {
        if (done()) return true;
        Sleep(5);
    }
    return done();
}

Client AttachUnreal() {
    static const HMODULE dll = LoadLibraryW(REAGENTLIVE_UNREAL_DLL);
    REQUIRE(dll != nullptr);
    const std::wstring name = L"Local\\reagentlive-ue-" + std::to_wstring(GetCurrentProcessId());
    Result<Client> client = std::unexpected(Error{});
    Until([&] { return (client = Client::Attach(name, LayoutCore + ue::LayoutPart)).has_value(); });
    REQUIRE(client);
    return std::move(*client);
}

ue::GameThreadState GameThread(Client& client, ue::GameThreadAction action) {
    Request request{};
    request.op = ue::op::GameThread;
    request.flags = static_cast<uint32_t>(action);
    auto response = client.Send(request);
    REQUIRE(response);
    REQUIRE(response->status == Status::Ok);
    ue::GameThreadState state{};
    REQUIRE(response->length == sizeof(state));
    memcpy(&state, response->data, sizeof(state));
    return state;
}
}

TEST_CASE("the Unreal example answers in a process with no engine") {
    Client client = AttachUnreal();
    CHECK(client.AgentPid() == GetCurrentProcessId());
    CHECK(client.Supports(ue::op::Fields));
    CHECK(client.Supports(ue::op::Call));
    CHECK(client.Supports(ue::op::GameThread));
    auto modules = client.Modules();
    REQUIRE(modules);
    CHECK(!modules->empty());

    // No object table here: Unreal ops are refused with the locate's reason, never answered.
    Request find{};
    find.op = ue::op::Find;
    strcpy_s(find.text, "Class");
    auto found = client.Send(find, std::chrono::seconds(30));
    REQUIRE(found);
    CHECK(found->status != Status::Ok);
}

TEST_CASE("the Unreal example's game-thread tick waits for a viewport and turns off") {
    Client client = AttachUnreal();
    CHECK(GameThread(client, ue::GameThreadAction::Status).mode == static_cast<uint32_t>(ue::GameThreadMode::Off));
    GameThread(client, ue::GameThreadAction::On);
    CHECK(Until([&] {
        return GameThread(client, ue::GameThreadAction::Status).mode == static_cast<uint32_t>(ue::GameThreadMode::Waiting);
    }));
    const auto state = GameThread(client, ue::GameThreadAction::Off);
    CHECK(state.mode == static_cast<uint32_t>(ue::GameThreadMode::Off));
    CHECK(state.viewport == 0);

    Request bad{};
    bad.op = ue::op::GameThread;
    bad.flags = 9;
    auto refused = client.Send(bad);
    REQUIRE(refused);
    CHECK(refused->status == Status::BadRequest);
}
