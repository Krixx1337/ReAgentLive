// reagentlive consumer test: an Unreal host with no SDK (reagentlive::ue-base).
// No SDK/ on the include path: the SDK-free part of the pack must build alone.
#include "reagentlive/core/agent.h"
#include "reagentlive/packs/ue/agent_ue.h"
#include "reagentlive/packs/ue/protocol_ue.h"

#if __has_include("SDK/CoreUObject_classes.hpp")
#error "the consumer must build ue-base without an SDK"
#endif

namespace {
bool Readable(const void*, size_t) { return false; }
bool OnEngineThread() { return true; }
constexpr reagentlive::Command Commands[] = {
    {reagentlive::op::Read, "read", &reagentlive::Read},
    {reagentlive::ue::op::Find, "find", &reagentlive::ue::Find},
    {reagentlive::ue::op::Describe, "describe", &reagentlive::ue::Describe},
    {reagentlive::ue::op::DescribeMany, "describe many", &reagentlive::ue::DescribeMany},
    {reagentlive::ue::op::ReadObject, "read object", &reagentlive::ue::ReadObject},
    {reagentlive::ue::op::Names, "names", &reagentlive::ue::Names},
    {reagentlive::ue::op::Census, "census", &reagentlive::ue::Census},
    {reagentlive::ue::op::NameFind, "name-find", &reagentlive::ue::NameFind},
};
}

extern "C" __declspec(dllexport) void ConsumerUeBaseTick() {
    static reagentlive::ue::Context pack{&reagentlive::ue::ObjectInTable, &OnEngineThread};
    static const reagentlive::Host host{.sectionName = L"Local\\reagentlive-consumer-ue-base-test",
                                        .layout = reagentlive::LayoutCore + reagentlive::ue::LayoutPart,
                                        .readable = &Readable,
                                        .context = &pack,
                                        .thread = reagentlive::HostThread::Engine};
    reagentlive::Tick(host, Commands);
}
