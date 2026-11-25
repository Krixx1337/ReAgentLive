# reagentlive: reusable dev agent library (Windows x64, C++23).
#
# Targets are INTERFACE libraries that carry their sources: each consumer
# compiles them with its own flags. Injected images often need special ones
# (static CRT, no thread-safe statics, no static TLS), and a separately built
# archive could disagree with them.
#
#   In the target process:
#   reagentlive::core       protocol, agent (read/write/base, tick, dispatch)
#   reagentlive::ue-base    Unreal pack's agent side, no SDK (src/reagentlive/packs/ue/pack.cmake)
#   reagentlive::ue         the same with the host's SDK as the layout
#   reagentlive::ue-draw    opt-in host helper: a game-thread tick through the viewport's Draw
#   In clients:
#   reagentlive::client     C++ client API (attach, read, write, base, raw Send)
#   reagentlive::ue-client  Unreal pack's client calls
#   reagentlive::cli        a call: the verb table, its args and checks (frontends/cli/call/)
#   reagentlive::console    interactive console over the client API
#                       (frontends/cli/console/ and the pure headers); links the
#                       x64 decoder (Zydis) for disasm and sig. No agent
#                       target may link it: a configure check fails if one does.
#                       It carries the one pack REAGENTLIVE_PACKS names, or none.
#
# Engine packs live in src/reagentlive/packs/<id>/, each with a pack.cmake
# that defines its targets. Headers are included as
# "reagentlive/<layer>/<file>.h" and "reagentlive/packs/<id>/<file>.h".
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

# Third-party sources come through CPM.cmake v0.43.2, kept in cmake/ (never
# fetched at configure). Its source cache shares one download per version
# between projects and works offline after it: CPM_SOURCE_CACHE from the
# environment or a parent wins (OFF: each build tree downloads its own);
# else %LOCALAPPDATA%\reagentlive\cpm, a cache safe to delete.
if(NOT DEFINED CPM_SOURCE_CACHE AND NOT DEFINED ENV{CPM_SOURCE_CACHE} AND DEFINED ENV{LOCALAPPDATA})
    file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}/reagentlive/cpm" reagentlive_cpm_cache)
    set(CPM_SOURCE_CACHE "${reagentlive_cpm_cache}" CACHE PATH "CPM's shared source cache (ReAgentLive's default)")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/cmake/CPM.cmake")
# CPM takes any folder in its cache as downloaded, so an interrupted download
# leaves an empty one that fails later as a missing target: stop at the cause.
function(reagentlive_check_package name)
    file(GLOB entries "${${name}_SOURCE_DIR}/*")
    if(NOT entries)
        message(FATAL_ERROR "reagentlive: ${name}'s source folder ${${name}_SOURCE_DIR} is empty (an interrupted "
                            "download in CPM's cache); delete it and configure again.")
    endif()
endfunction()

option(REAGENTLIVE_BUILD_TESTS "Build reagentlive's tests" ${PROJECT_IS_TOP_LEVEL})
# The console targets need nlohmann/json and the Zydis decoder, fetched at
# configure unless the parent project already provides them. A host that
# links only the agent turns this off and downloads nothing.
option(REAGENTLIVE_CONSOLE "Define the console targets (reagentlive::console, and the JSON ::cli needs)" ON)
# The engine packs to define, by folder name under src/reagentlive/packs/.
# The console and the CLI call layer carry the one listed pack; empty, they
# compile no pack file and the verb table (help, the generated functions, the
# MCP tools) holds core and host rows only. For a target that is not Unreal.
set(REAGENTLIVE_PACKS "ue" CACHE STRING "Engine packs to define (folders under src/reagentlive/packs/); empty for none")
if(REAGENTLIVE_BUILD_TESTS AND NOT REAGENTLIVE_CONSOLE)
    message(FATAL_ERROR "reagentlive: REAGENTLIVE_BUILD_TESTS needs REAGENTLIVE_CONSOLE.")
endif()
if(REAGENTLIVE_BUILD_TESTS AND NOT "ue" IN_LIST REAGENTLIVE_PACKS)
    message(FATAL_ERROR "reagentlive: REAGENTLIVE_BUILD_TESTS needs ue in REAGENTLIVE_PACKS (the tests build both consoles).")
endif()

# Only src/ is on the include path: headers are "reagentlive/<layer>/<file>.h",
# whatever this folder is called, and nothing beside it becomes includable.
set(reagentlive_include_root "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(reagentlive-core INTERFACE)
add_library(reagentlive::core ALIAS reagentlive-core)
target_sources(reagentlive-core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/core/agent.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/core/agent_search.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/core/agent_recorder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/core/agent_vtable_copy.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/core/agent_native_calls.cpp")
target_include_directories(reagentlive-core INTERFACE "${reagentlive_include_root}")
# The native call recorder's thunks (x64 MSVC has no inline assembly): MASM
# assembled once here into a static library every agent target links, so a
# host enables no assembler of its own, and an OBJECT library that carries
# the agent passes it on to the image it ends up in.
enable_language(ASM_MASM)
add_library(reagentlive-core-thunks STATIC "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/core/native_thunks.asm")
set_target_properties(reagentlive-core-thunks PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(reagentlive-core INTERFACE reagentlive-core-thunks)
target_compile_features(reagentlive-core INTERFACE cxx_std_23)

# The source tree's commit, for status (reagentlive/commit.h, refreshed on
# every build, rewritten only when the commit changes).
set(reagentlive_generated "${CMAKE_CURRENT_BINARY_DIR}/reagentlive_generated")
add_custom_target(reagentlive-commit
    COMMAND "${CMAKE_COMMAND}" -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}
        -DOUTPUT=${reagentlive_generated}/reagentlive/commit.h -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/commit.cmake"
    BYPRODUCTS "${reagentlive_generated}/reagentlive/commit.h" VERBATIM)

add_library(reagentlive-client INTERFACE)
add_library(reagentlive::client ALIAS reagentlive-client)
target_sources(reagentlive-client INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/core/client.cpp")
target_include_directories(reagentlive-client INTERFACE "${reagentlive_include_root}" "${reagentlive_generated}")
add_dependencies(reagentlive-client reagentlive-commit)
target_compile_features(reagentlive-client INTERFACE cxx_std_23)
target_link_libraries(reagentlive-client INTERFACE winmm) # timeBeginPeriod: 1 ms waits.

# The packs. Each pack.cmake defines its targets and sets its CLI parts:
# reagentlive_pack_call_sources, reagentlive_pack_console_sources and
# reagentlive_pack_console_link. One console carries one pack until
# console/pack.h becomes a table of packs (docs/design/architecture.md#source-layout).
set(reagentlive_packs_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/packs")
set(reagentlive_packs_seen)
foreach(pack IN LISTS REAGENTLIVE_PACKS)
    if(pack IN_LIST reagentlive_packs_seen)
        message(FATAL_ERROR "reagentlive: REAGENTLIVE_PACKS lists ${pack} twice.")
    endif()
    if(NOT EXISTS "${reagentlive_packs_dir}/${pack}/pack.cmake")
        message(FATAL_ERROR "reagentlive: no engine pack ${pack} (no src/reagentlive/packs/${pack}/pack.cmake).")
    endif()
    list(APPEND reagentlive_packs_seen ${pack})
    include("${reagentlive_packs_dir}/${pack}/pack.cmake")
endforeach()
list(LENGTH REAGENTLIVE_PACKS reagentlive_pack_count)
if(reagentlive_pack_count GREATER 1)
    message(FATAL_ERROR "reagentlive: REAGENTLIVE_PACKS names ${reagentlive_pack_count} packs; one console carries one pack until console/pack.h becomes a table (docs/design/architecture.md#source-layout).")
endif()

add_library(reagentlive-cli INTERFACE)
add_library(reagentlive::cli ALIAS reagentlive-cli)
target_include_directories(reagentlive-cli INTERFACE "${reagentlive_include_root}")
target_compile_features(reagentlive-cli INTERFACE cxx_std_23)
# Each part of the CLI as a core list; the pack's lists come from its pack.cmake.
set(reagentlive_cli_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/reagentlive/frontends/cli")
set(reagentlive_call_sources "${reagentlive_cli_dir}/call/build.cpp"
    "${reagentlive_cli_dir}/call/build_memory.cpp" "${reagentlive_cli_dir}/call/build_code.cpp")
set(reagentlive_console_sources "${reagentlive_cli_dir}/console/console.cpp" "${reagentlive_cli_dir}/console/output.cpp"
    "${reagentlive_cli_dir}/console/memory.cpp" "${reagentlive_cli_dir}/console/sig.cpp" "${reagentlive_cli_dir}/console/label_sigs.cpp"
    "${reagentlive_cli_dir}/console/code.cpp" "${reagentlive_cli_dir}/console/native.cpp"
    "${reagentlive_cli_dir}/console/recorder.cpp"
    "${reagentlive_cli_dir}/console/snapshot.cpp" "${reagentlive_cli_dir}/console/batch.cpp"
    "${reagentlive_cli_dir}/console/offline.cpp" "${reagentlive_cli_dir}/console/session_files.cpp")
set(reagentlive_console_no_pack_sources "${reagentlive_cli_dir}/console/no_pack.cpp")
target_sources(reagentlive-cli INTERFACE ${reagentlive_call_sources})
if(reagentlive_pack_count EQUAL 1)
    target_sources(reagentlive-cli INTERFACE ${reagentlive_pack_call_sources})
endif()

if(REAGENTLIVE_CONSOLE)
    add_library(reagentlive-console INTERFACE)
    add_library(reagentlive::console ALIAS reagentlive-console)
    # A parent's own target or an installed package wins; otherwise the pinned
    # release is fetched.
    if(NOT TARGET nlohmann_json::nlohmann_json)
        CPMFindPackage(NAME nlohmann_json VERSION 3.12.0
            URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.12.0
            URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
            FIND_PACKAGE_ARGUMENTS CONFIG)
        if(nlohmann_json_ADDED)
            reagentlive_check_package(nlohmann_json)
        endif()
    endif()
    # A call's arguments are JSON: the call layer reads them.
    target_link_libraries(reagentlive-cli INTERFACE nlohmann_json::nlohmann_json)
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
        CPMAddPackage(NAME zydis
            GIT_REPOSITORY https://github.com/zyantific/zydis.git
            GIT_TAG a2278f1d254e492f6a6b39f6cb5d1f5d515659dc) # v4.1.1
        reagentlive_check_package(zydis)
    endif()
    if(reagentlive_pack_count EQUAL 1)
        target_sources(reagentlive-console INTERFACE ${reagentlive_console_sources} ${reagentlive_pack_console_sources})
        target_link_libraries(reagentlive-console INTERFACE ${reagentlive_pack_console_link})
    else()
        target_sources(reagentlive-console INTERFACE ${reagentlive_console_sources} ${reagentlive_console_no_pack_sources})
        target_link_libraries(reagentlive-console INTERFACE reagentlive-client)
    endif()
    target_link_libraries(reagentlive-console INTERFACE reagentlive-cli nlohmann_json::nlohmann_json Zydis)
endif()

# The agent never decodes: fail the configure when Zydis reaches an agent
# target. Hosts add their agent-carrying targets to REAGENTLIVE_AGENT_TARGETS.
# Each pack's pack.cmake adds its own agent target.
set_property(GLOBAL APPEND PROPERTY REAGENTLIVE_AGENT_TARGETS reagentlive-core)
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
# own-thread host and, with the Unreal pack, the Unreal example, so they join here.
if(REAGENTLIVE_BUILD_TESTS)
    add_subdirectory(examples/own_thread)
    if(TARGET reagentlive-ue-draw)
        add_subdirectory(examples/unreal)
    endif()
endif()

# Tests: built when reagentlive is the top-level project, or when the host sets
# REAGENTLIVE_BUILD_TESTS. They need no game: request bounds and CLI calls, and
# the real agent and client end to end in one process.
if(REAGENTLIVE_BUILD_TESTS)
    enable_testing()
    # doctest v2.5.3: the header and its test discovery, fetched at configure.
    CPMAddPackage(NAME doctest
        URL https://codeload.github.com/doctest/doctest/tar.gz/refs/tags/v2.5.3
        URL_HASH SHA256=174ebc4e769928959614789c5b4e9c3d0a0f81a62bb608756b127bfebfb21331
        DOWNLOAD_ONLY YES) # The header and its CMake script only, none of doctest's own targets.
    reagentlive_check_package(doctest)
    add_library(reagentlive-doctest INTERFACE)
    target_include_directories(reagentlive-doctest SYSTEM INTERFACE "${doctest_SOURCE_DIR}/doctest")
    include("${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")

    # One folder per suite: tests/cli, tests/ue, tests/loopback, tests/python (tests/table is a tool they use).
    add_executable(reagentlive-cli-tests tests/cli/main.cpp tests/cli/check_tests.cpp tests/cli/call_tests.cpp
        tests/cli/call_ue_tests.cpp tests/cli/format_tests.cpp tests/cli/state_tests.cpp tests/cli/result_tests.cpp
        tests/cli/table_tests.cpp tests/cli/sig_tests.cpp tests/cli/code_tests.cpp tests/cli/code_save_tests.cpp
        tests/cli/code_scan_tests.cpp tests/cli/budget_tests.cpp tests/cli/kismet_tests.cpp tests/cli/usage_tests.cpp)
    target_link_libraries(reagentlive-cli-tests PRIVATE reagentlive-cli reagentlive-doctest nlohmann_json::nlohmann_json Zydis)
    # Args::At throws on a key the verb's row lacks (call/verb.h): in the test
    # builds only, so a builder reading a renamed or misspelt key fails a test.
    target_compile_definitions(reagentlive-cli-tests PRIVATE REAGENTLIVE_ARGS_CHECK)
    # The verb table without a console: what `help --json` prints, and the docs page generated from it.
    add_executable(reagentlive-table tests/table/main.cpp)
    target_link_libraries(reagentlive-table PRIVATE reagentlive-cli nlohmann_json::nlohmann_json Zydis)
    target_compile_definitions(reagentlive-table PRIVATE REAGENTLIVE_ARGS_CHECK) # The docs' calls too.
    set_target_properties(reagentlive-table PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
    target_compile_options(reagentlive-table PRIVATE /W4 /WX /EHsc)
    add_test(NAME reagentlive.results-doc
        COMMAND reagentlive-table --check "${CMAKE_CURRENT_SOURCE_DIR}/docs/commands/results.md")
    set_tests_properties(reagentlive.results-doc PROPERTIES LABELS "reagentlive" TIMEOUT 30)
    add_test(NAME reagentlive.reference-doc
        COMMAND reagentlive-table --check-reference "${CMAKE_CURRENT_SOURCE_DIR}/docs/commands/reference.md")
    set_tests_properties(reagentlive.reference-doc PROPERTIES LABELS "reagentlive" TIMEOUT 30)
    # The console without the pack, built beside the default one: the proof
    # that the core console does not lean on the pack (the example's main).
    add_executable(reagentlive-core-console examples/own_thread/console.cpp
        ${reagentlive_call_sources} ${reagentlive_console_sources} ${reagentlive_console_no_pack_sources})
    target_link_libraries(reagentlive-core-console PRIVATE reagentlive-client nlohmann_json::nlohmann_json Zydis)
    set_target_properties(reagentlive-core-console PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
    target_compile_options(reagentlive-core-console PRIVATE /W4 /WX /EHsc)
    add_executable(reagentlive-ue-tests tests/ue/main.cpp tests/ue/protocol_tests.cpp tests/ue/globals_tests.cpp
        tests/ue/layout_tests.cpp tests/ue/index_tests.cpp)
    target_link_libraries(reagentlive-ue-tests PRIVATE reagentlive-cli reagentlive-doctest)
    add_executable(reagentlive-loopback-tests tests/loopback/main.cpp tests/loopback/memory_tests.cpp
        tests/loopback/services_tests.cpp tests/loopback/channel_tests.cpp tests/loopback/recorder_tests.cpp
        tests/loopback/vtable_copy_tests.cpp tests/loopback/native_calls_tests.cpp)
    target_link_libraries(reagentlive-loopback-tests PRIVATE reagentlive-core reagentlive-client reagentlive-ue-client reagentlive-cli reagentlive-doctest)
    # The real console against a fake engine: an in-process agent whose pack
    # ops answer from a synthetic world (tests/console/fake_engine.h).
    if(TARGET reagentlive-console AND reagentlive_pack_count EQUAL 1)
        add_executable(reagentlive-console-tests tests/console/main.cpp tests/console/console_tests.cpp
            tests/console/console_files_tests.cpp)
        target_link_libraries(reagentlive-console-tests PRIVATE reagentlive-console reagentlive-core reagentlive-doctest)
        target_compile_definitions(reagentlive-console-tests PRIVATE REAGENTLIVE_ARGS_CHECK)
        set_target_properties(reagentlive-console-tests PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
        target_compile_options(reagentlive-console-tests PRIVATE /W4 /WX /EHsc)
        doctest_discover_tests(reagentlive-console-tests TEST_PREFIX "reagentlive." PROPERTIES LABELS "reagentlive" TIMEOUT 60)
    endif()
    # The own-thread host end to end: the real DLL loaded into the test process.
    add_executable(reagentlive-example-tests tests/example/main.cpp)
    target_link_libraries(reagentlive-example-tests PRIVATE reagentlive-client reagentlive-doctest)
    target_compile_definitions(reagentlive-example-tests PRIVATE
        "REAGENTLIVE_OWN_THREAD_DLL=L\"$<TARGET_FILE:reagentlive-own-thread>\"")
    add_dependencies(reagentlive-example-tests reagentlive-own-thread)
    # The Unreal example the same way, when the pack is built.
    if(TARGET reagentlive-unreal)
        target_sources(reagentlive-example-tests PRIVATE tests/example/unreal_tests.cpp)
        target_compile_definitions(reagentlive-example-tests PRIVATE
            "REAGENTLIVE_UNREAL_DLL=L\"$<TARGET_FILE:reagentlive-unreal>\"")
        add_dependencies(reagentlive-example-tests reagentlive-unreal)
    endif()
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
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_session.py"
                --table "$<TARGET_FILE:reagentlive-table>")
        set_tests_properties(reagentlive.python-session PROPERTIES LABELS "reagentlive" TIMEOUT 60)
        # The MCP adapter over a generated module (skips itself without the MCP Python SDK).
        add_test(NAME reagentlive.python-mcp
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_mcp.py"
                --table "$<TARGET_FILE:reagentlive-table>")
        set_tests_properties(reagentlive.python-mcp PROPERTIES LABELS "reagentlive" TIMEOUT 60)
        # The pack-less console's table: no pack row, no row that needs one;
        # the generated module and the MCP tools follow from it.
        add_test(NAME reagentlive.core-console
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_core_console.py"
                --console "$<TARGET_FILE:reagentlive-core-console>" --table "$<TARGET_FILE:reagentlive-table>")
        set_tests_properties(reagentlive.core-console PROPERTIES LABELS "reagentlive" TIMEOUT 60)
        # Every verb and arg the docs and the skill name is in the table (and the checker's own tests).
        add_test(NAME reagentlive.verb-names
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/check_verb_names.py"
                --table "$<TARGET_FILE:reagentlive-table>")
        set_tests_properties(reagentlive.verb-names PROPERTIES LABELS "reagentlive" TIMEOUT 60)
        add_test(NAME reagentlive.verb-names-check
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_verb_names.py")
        set_tests_properties(reagentlive.verb-names-check PROPERTIES LABELS "reagentlive" TIMEOUT 60)
        # Every JSON call the docs and the skill show passes the console's own checks (and the checker's tests).
        add_test(NAME reagentlive.json-examples
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/check_json_examples.py"
                --table "$<TARGET_FILE:reagentlive-table>")
        set_tests_properties(reagentlive.json-examples PROPERTIES LABELS "reagentlive" TIMEOUT 60)
        add_test(NAME reagentlive.json-examples-check
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_json_examples.py"
                --table "$<TARGET_FILE:reagentlive-table>")
        set_tests_properties(reagentlive.json-examples-check PROPERTIES LABELS "reagentlive" TIMEOUT 60)
        # Engine offsets have one home, engine_layout.h.
        add_test(NAME reagentlive.engine-layout
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_engine_layout.py")
        set_tests_properties(reagentlive.engine-layout PROPERTIES LABELS "reagentlive" TIMEOUT 60)
        add_test(NAME reagentlive.ida-labels
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_ida_labels.py")
        set_tests_properties(reagentlive.ida-labels PROPERTIES LABELS "reagentlive" TIMEOUT 60)
        add_test(NAME reagentlive.ida-function
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_ida_function.py")
        set_tests_properties(reagentlive.ida-function PROPERTIES LABELS "reagentlive" TIMEOUT 60)
        add_test(NAME reagentlive.ida-types
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_ida_types.py")
        set_tests_properties(reagentlive.ida-types PROPERTIES LABELS "reagentlive" TIMEOUT 60)
        add_test(NAME reagentlive.ida-fetch
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_ida_fetch.py")
        set_tests_properties(reagentlive.ida-fetch PROPERTIES LABELS "reagentlive" TIMEOUT 60)
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
        # tools/agent_link.py: wiring an agent to a host project, new or existing, and again.
        add_test(NAME reagentlive.agent-link
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_agent_link.py")
        set_tests_properties(reagentlive.agent-link PROPERTIES LABELS "reagentlive" TIMEOUT 60)
        # tools/check.ps1: the read-only machine and project check, under Windows PowerShell.
        add_test(NAME reagentlive.check
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/python/test_check.py")
        set_tests_properties(reagentlive.check PROPERTIES LABELS "reagentlive" TIMEOUT 180)
    endif()
endif()
