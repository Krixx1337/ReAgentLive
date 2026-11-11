// reagentlive consumer test: compiles against the library as a host would.
#include "reagentlive/core/agent.h"
#include "reagentlive/version.h"

// Only src/ is on the include path: the library folder's own tests and
// examples, and anything beside the library, stay out of reach.
#if __has_include("reagentlive/tests/consumer/host.cpp") ||     __has_include("reagentlive/examples/own_thread/host.cpp") || __has_include("reagentlive/CMakeLists.txt")
#error "the include path reaches past src/"
#endif
#ifndef REAGENTLIVE_VERSION_MAJOR
#error "reagentlive/version.h defines the version"
#endif

namespace {
bool Readable(const void*, size_t) { return false; }
constexpr reagentlive::Command Commands[] = {{reagentlive::op::Read, "read", &reagentlive::Read}};
}

extern "C" __declspec(dllexport) void ConsumerTick() {
    static const reagentlive::Host host{.sectionName = L"Local\\reagentlive-consumer-test",
                                        .layout = reagentlive::LayoutCore,
                                        .readable = &Readable,
                                        .thread = reagentlive::HostThread::Own};
    reagentlive::Tick(host, Commands);
}
