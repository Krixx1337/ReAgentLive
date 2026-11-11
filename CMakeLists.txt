# reagentlive: reusable dev agent library (Windows x64, C++23).
#
# Targets are INTERFACE libraries that carry their sources: each consumer
# compiles them with its own flags. Injected images often need special ones
# (static CRT, no thread-safe statics, no static TLS), and a separately built
# archive could disagree with them.
#
#   In the target process:
#   reagentlive::core       protocol, agent (read/write/base, tick, dispatch)
#   reagentlive::ue         Unreal pack (find/fields/call); the consumer must provide
#                       its Dumper-7 SDK on the include path as SDK/...
#   In clients:
#   reagentlive::client     C++ client API (attach, read, write, base, raw Send)
#   reagentlive::ue-client  Unreal client calls (Find, Fields, Call); no SDK needed
#   reagentlive::cli        console syntax (frontends/cli/syntax/), no JSON
#   reagentlive::console    interactive console over the client API
#                       (frontends/cli/console/ and the pure headers); links the
#                       x64 decoder (Zydis) for disasm and sig. No agent
#                       target may link it: a configure check fails if one does.
#
# Headers are included as "reagentlive/<layer>/<file>.h".
cmake_minimum_required(VERSION 3.25)
# The version is written once, in src/reagentlive/version.h.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/src/reagentlive/version.h" reagentlive_version_lines
    REGEX "^#define REAGENTLIVE_VERSION_(MAJOR|MINOR|PATCH) [0-9]+$")
foreach(part MAJOR MINOR PATCH)
    string(REGEX MATCH "REAGENTLIVE_VERSION_${part} ([0-9]+)" reagentlive_version_match "${reagentlive_version_lines}")
    set(reagentlive_version_${part} ${CMAKE_MATCH_1})
endforeach()
project(reagentlive
    VERSION ${reagentlive_version_MAJOR}.${reagentlive_version_MINOR}.${reagentlive_version_PATCH}
    LANGUAGES CXX)

# Windows x64 with MSVC or clang-cl only (the agent uses structured exception
# handling). Elsewhere a parent project still configures: it just gets no
# reagentlive targets.
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT MSVC)
    message(WARNING "reagentlive: Windows x64 with MSVC or clang-cl only; no reagentlive targets on this platform.")
    return()
endif()

option(REAGENTLIVE_BUILD_TESTS "Build reagentlive's tests" ${PROJECT_IS_TOP_LEVEL})
# The console targets need nlohmann/json and the Zydis decoder, fetched at
# configure unless the parent project already provides them. A host that
# links only the agent turns this off and downloads nothing.
option(REAGENTLIVE_CONSOLE "Define the console targets (reagentlive::console, ::cli needs nothing)" ON)
if(REAGENTLIVE_BUILD_TESTS AND NOT REAGENTLIVE_CONSOLE)
    message(FATAL_ERROR "reagentlive: REAGENTLIVE_BUILD_TESTS needs REAGENTLIVE_CONSOLE.")
endif()

# Only src/ is on the include path: headers are "reagentlive/<layer>/<file>.h",
# whatever this folder is called, and nothing beside it becomes includable.
set(reagentlive_include_root "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(reagentlive-core INTERFACE)
add_library(reagentlive::core ALIAS reagentlive-core)
target_sources(reagentlive-core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/core/agent.cpp")
target_include_directories(reagentlive-core INTERFACE "${reagentlive_include_root}")
target_compile_features(reagentlive-core INTERFACE cxx_std_23)

add_library(reagentlive-ue INTERFACE)
add_library(reagentlive::ue ALIAS reagentlive-ue)
target_sources(reagentlive-ue INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/ue/agent_ue.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/ue/agent_ue_reflect.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/ue/agent_ue_scan.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/ue/agent_ue_script.cpp")
target_link_libraries(reagentlive-ue INTERFACE reagentlive-core)

add_library(reagentlive-client INTERFACE)
add_library(reagentlive::client ALIAS reagentlive-client)
target_sources(reagentlive-client INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/core/client.cpp")
target_include_directories(reagentlive-client INTERFACE "${reagentlive_include_root}")
target_compile_features(reagentlive-client INTERFACE cxx_std_23)
target_link_libraries(reagentlive-client INTERFACE winmm) # timeBeginPeriod: 1 ms waits.

add_library(reagentlive-ue-client INTERFACE)
add_library(reagentlive::ue-client ALIAS reagentlive-ue-client)
target_sources(reagentlive-ue-client INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/ue/client_ue.cpp")
target_link_libraries(reagentlive-ue-client INTERFACE reagentlive-client)

add_library(reagentlive-cli INTERFACE)
add_library(reagentlive::cli ALIAS reagentlive-cli)
target_include_directories(reagentlive-cli INTERFACE "${reagentlive_include_root}")
target_compile_features(reagentlive-cli INTERFACE cxx_std_23)
target_sources(reagentlive-cli INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/frontends/cli/syntax/parse.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/frontends/cli/syntax/parse_memory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/frontends/cli/syntax/parse_unreal.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/frontends/cli/syntax/parse_code.cpp")

if(REAGENTLIVE_CONSOLE)
    add_library(reagentlive-console INTERFACE)
    add_library(reagentlive::console ALIAS reagentlive-console)
    include(FetchContent)
    # A parent's own target or an installed package wins; otherwise the pinned
    # release is fetched.
    if(NOT TARGET nlohmann_json::nlohmann_json)
        FetchContent_Declare(nlohmann_json
            URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.12.0
            URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
            FIND_PACKAGE_ARGS 3.12 CONFIG)
        FetchContent_MakeAvailable(nlohmann_json)
    endif()
    # The decoder: Zydis v4.1.1 with its formatter, static, built once for the
    # whole tree; a parent that already has a Zydis target keeps its own. Its
    # options are plain variables (CMP0077), so a parent's cache is never
    # overwritten. Console and tests only.
    if(NOT TARGET Zydis)
        set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
        set(ZYDIS_BUILD_SHARED_LIB OFF)
        set(ZYDIS_BUILD_TOOLS OFF)
        set(ZYDIS_BUILD_EXAMPLES OFF)
        set(ZYDIS_BUILD_DOXYGEN OFF)
        set(ZYDIS_FEATURE_ENCODER OFF)
        set(ZYDIS_FEATURE_FORMATTER ON)
        FetchContent_Declare(zydis
            GIT_REPOSITORY https://github.com/zyantific/zydis.git
            GIT_TAG a2278f1d254e492f6a6b39f6cb5d1f5d515659dc) # v4.1.1
        FetchContent_MakeAvailable(zydis)
    endif()
    target_sources(reagentlive-console INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/frontends/cli/console/console.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/frontends/cli/console/output.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/frontends/cli/console/memory.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/frontends/cli/console/dump.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/frontends/cli/console/capture.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/frontends/cli/console/calls.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/frontends/cli/console/query.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/frontends/cli/console/labels.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/frontends/cli/console/sig.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/frontends/cli/console/code.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/frontends/cli/console/native.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/frontends/cli/console/members.cpp")
    target_link_libraries(reagentlive-console INTERFACE reagentlive-ue-client reagentlive-cli nlohmann_json::nlohmann_json Zydis)
endif()

# The agent never decodes: fail the configure when Zydis reaches an agent
# target. Hosts add their agent-carrying targets to REAGENTLIVE_AGENT_TARGETS.
set_property(GLOBAL APPEND PROPERTY REAGENTLIVE_AGENT_TARGETS reagentlive-core reagentlive-ue)
function(reagentlive_check_no_decoder)
    get_property(targets GLOBAL PROPERTY REAGENTLIVE_AGENT_TARGETS)
    foreach(target IN LISTS targets)
        set(pending ${target})
        set(seen)
        while(pending)
            list(POP_FRONT pending current)
            if(current IN_LIST seen OR NOT TARGET ${current})
                continue()
            endif()
            list(APPEND seen ${current})
            get_target_property(aliased ${current} ALIASED_TARGET)
            if(aliased)
                list(APPEND pending ${aliased})
                continue()
            endif()
            if(current STREQUAL "Zydis")
                message(FATAL_ERROR "reagentlive: agent target ${target} links the decoder (Zydis); it belongs in the console only.")
            endif()
            get_target_property(type ${current} TYPE)
            set(links)
            if(NOT type STREQUAL "INTERFACE_LIBRARY")
                get_target_property(direct ${current} LINK_LIBRARIES)
                if(direct)
                    list(APPEND links ${direct})
                endif()
            endif()
            get_target_property(interface ${current} INTERFACE_LINK_LIBRARIES)
            if(interface)
                list(APPEND links ${interface})
            endif()
            foreach(link IN LISTS links)
                if(link MATCHES "^\\$<LINK_ONLY:(.+)>$")
                    set(link ${CMAKE_MATCH_1})
                endif()
                list(APPEND pending ${link})
            endforeach()
        endwhile()
    endforeach()
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL reagentlive_check_no_decoder)

# Examples build on their own (examples/*/CMakeLists.txt); the tests need the
# own-thread host, so it joins them here.
if(REAGENTLIVE_BUILD_TESTS)
    add_subdirectory(examples/own_thread)
endif()

# Tests: built when reagentlive is the top-level project, or when the host sets
# REAGENTLIVE_BUILD_TESTS. They need no game: request bounds and CLI syntax, and
# the real agent and client end to end in one process.
if(REAGENTLIVE_BUILD_TESTS)
    enable_testing()
    add_library(reagentlive-doctest INTERFACE)
    target_include_directories(reagentlive-doctest SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest")
    include("${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest/cmake/doctest.cmake")

    # One folder per suite: tests/cli, tests/ue, tests/loopback, tests/python.
    add_executable(reagentlive-cli-tests tests/cli/main.cpp tests/cli/parse_tests.cpp tests/cli/format_tests.cpp
        tests/cli/state_tests.cpp tests/cli/result_tests.cpp tests/cli/sig_tests.cpp tests/cli/code_tests.cpp)
    target_link_libraries(reagentlive-cli-tests PRIVATE reagentlive-cli reagentlive-doctest nlohmann_json::nlohmann_json Zydis)
    add_executable(reagentlive-ue-tests tests/ue/main.cpp tests/ue/protocol_tests.cpp)
    target_link_libraries(reagentlive-ue-tests PRIVATE reagentlive-cli reagentlive-doctest)
    add_executable(reagentlive-loopback-tests tests/loopback/main.cpp tests/loopback/memory_tests.cpp
        tests/loopback/services_tests.cpp tests/loopback/channel_tests.cpp)
    target_link_libraries(reagentlive-loopback-tests PRIVATE reagentlive-core reagentlive-client reagentlive-ue-client reagentlive-cli reagentlive-doctest)
    # The own-thread host end to end: the real DLL loaded into the test process.
    add_executable(reagentlive-example-tests tests/example/main.cpp)
    target_link_libraries(reagentlive-example-tests PRIVATE reagentlive-client reagentlive-doctest)
    target_compile_definitions(reagentlive-example-tests PRIVATE
        "REAGENTLIVE_OWN_THREAD_DLL=L\"$<TARGET_FILE:reagentlive-own-thread>\"")
    add_dependencies(reagentlive-example-tests reagentlive-own-thread)
    foreach(test reagentlive-cli-tests reagentlive-ue-tests reagentlive-loopback-tests reagentlive-example-tests)
        set_target_properties(${test} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
        target_compile_options(${test} PRIVATE /W4 /WX /EHsc)
        doctest_discover_tests(${test} TEST_PREFIX "reagentlive." PROPERTIES LABELS "reagentlive" TIMEOUT 30)
    endforeach()
    # A host project that links only the agent, configured and built in its
    # own tree: no downloads, and only src/ on the include path.
    add_test(NAME reagentlive.consumer
        COMMAND "${CMAKE_CTEST_COMMAND}" --build-and-test
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/consumer" "${CMAKE_CURRENT_BINARY_DIR}/consumer"
            --build-generator "${CMAKE_GENERATOR}" --build-makeprogram "${CMAKE_MAKE_PROGRAM}"
            --build-options "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}" "-DCMAKE_BUILD_TYPE=Release")
    set_tests_properties(reagentlive.consumer PROPERTIES LABELS "reagentlive" TIMEOUT 300)
    # The Python session client, against a scripted fake console.
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME reagentlive.python-session
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_session.py")
        set_tests_properties(reagentlive.python-session PROPERTIES LABELS "reagentlive" TIMEOUT 60)
        add_test(NAME reagentlive.ida-labels
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_ida_labels.py")
        set_tests_properties(reagentlive.ida-labels PROPERTIES LABELS "reagentlive" TIMEOUT 60)
        # frontends/cli layering; hosts list their sources that include the
        # console in REAGENTLIVE_HOST_SOURCES, so the test checks them too.
        set(host_args)
        foreach(host IN LISTS REAGENTLIVE_HOST_SOURCES)
            list(APPEND host_args --host "${host}")
        endforeach()
        add_test(NAME reagentlive.cli-layout
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_cli_layout.py" ${host_args})
        set_tests_properties(reagentlive.cli-layout PROPERTIES LABELS "reagentlive" TIMEOUT 60)
        # Docs: links, anchors and the index; a host lists its own and its
        # game's names in REAGENTLIVE_FORBIDDEN_NAMES so the library never names them.
        set(forbid_args)
        foreach(word IN LISTS REAGENTLIVE_FORBIDDEN_NAMES)
            list(APPEND forbid_args --forbid "${word}")
        endforeach()
        add_test(NAME reagentlive.docs
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_docs.py" ${forbid_args})
        set_tests_properties(reagentlive.docs PROPERTIES LABELS "reagentlive" TIMEOUT 60)
    endif()
endif()
