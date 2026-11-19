# The Unreal pack (id ue), included by the root CMakeLists.txt when
# REAGENTLIVE_PACKS lists it. INTERFACE targets like the core's: the consumer
# compiles them with its own flags.
#
#   reagentlive::ue-base    agent side without an SDK (find, describe, names, census)
#   reagentlive::ue         agent side (adds fields, calls, containers, script); the
#                       consumer must provide its Dumper-7 SDK on the include path as SDK/...
#   reagentlive::ue-client  client calls (Find, Fields, Call); no SDK needed
#
# Its CLI parts are source lists the root adds to reagentlive::cli and
# reagentlive::console: reagentlive_pack_syntax_sources,
# reagentlive_pack_console_sources and reagentlive_pack_console_link.
set(reagentlive_ue_dir "${CMAKE_CURRENT_LIST_DIR}")

# The SDK-free part: the object table and name pool, the object layout
# (stock UE5 shapes checked at run time, UStruct offsets probed from the
# engine's class tree), find, describe, raw object reads, names, census and
# name-find. Needs no SDK; reagentlive::ue builds the same files with the
# host's SDK as the layout's source (REAGENTLIVE_UE_SDK).
add_library(reagentlive-ue-base INTERFACE)
add_library(reagentlive::ue-base ALIAS reagentlive-ue-base)
target_sources(reagentlive-ue-base INTERFACE "${reagentlive_ue_dir}/agent_ue.cpp"
    "${reagentlive_ue_dir}/agent_ue_census.cpp" "${reagentlive_ue_dir}/agent_ue_globals.cpp")
target_link_libraries(reagentlive-ue-base INTERFACE reagentlive-core)
set_property(GLOBAL APPEND PROPERTY REAGENTLIVE_AGENT_TARGETS reagentlive-ue-base)

add_library(reagentlive-ue INTERFACE)
add_library(reagentlive::ue ALIAS reagentlive-ue)
target_sources(reagentlive-ue INTERFACE "${reagentlive_ue_dir}/agent_ue_entries.cpp"
    "${reagentlive_ue_dir}/agent_ue_reflect.cpp" "${reagentlive_ue_dir}/agent_ue_scan.cpp"
    "${reagentlive_ue_dir}/agent_ue_script.cpp" "${reagentlive_ue_dir}/agent_ue_calls.cpp")
target_compile_definitions(reagentlive-ue INTERFACE REAGENTLIVE_UE_SDK=1)
target_link_libraries(reagentlive-ue INTERFACE reagentlive-ue-base)
set_property(GLOBAL APPEND PROPERTY REAGENTLIVE_AGENT_TARGETS reagentlive-ue)

add_library(reagentlive-ue-client INTERFACE)
add_library(reagentlive::ue-client ALIAS reagentlive-ue-client)
target_sources(reagentlive-ue-client INTERFACE "${reagentlive_ue_dir}/client_ue.cpp")
target_link_libraries(reagentlive-ue-client INTERFACE reagentlive-client)

set(reagentlive_pack_syntax_sources "${reagentlive_ue_dir}/cli/syntax/parse_unreal.cpp")
set(reagentlive_pack_console_sources "${reagentlive_ue_dir}/cli/console/pack.cpp"
    "${reagentlive_ue_dir}/cli/console/dump.cpp" "${reagentlive_ue_dir}/cli/console/capture.cpp"
    "${reagentlive_ue_dir}/cli/console/calls.cpp" "${reagentlive_ue_dir}/cli/console/trace.cpp"
    "${reagentlive_ue_dir}/cli/console/recorder.cpp" "${reagentlive_ue_dir}/cli/console/query.cpp"
    "${reagentlive_ue_dir}/cli/console/labels.cpp" "${reagentlive_ue_dir}/cli/console/members.cpp"
    "${reagentlive_ue_dir}/cli/console/layout.cpp")
set(reagentlive_pack_console_link reagentlive-ue-client)
