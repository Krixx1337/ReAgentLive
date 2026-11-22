# The Unreal pack (id ue), included by the root CMakeLists.txt when
# REAGENTLIVE_PACKS lists it. INTERFACE targets like the core's: the consumer
# compiles them with its own flags.
#
#   reagentlive::ue-base    agent side without an SDK: every handler, reading with the
#                       stock UE5 layout checked on known objects (agent_ue_check.cpp)
#   reagentlive::ue         the same handlers with the host's Dumper-7 SDK as the layout
#                       (SDK/... on the include path); every layout group on
#   reagentlive::ue-draw    opt-in host helper: a game-thread tick through the viewport's Draw
#   reagentlive::ue-client  client calls (Find, Fields, Call); no SDK needed
#
# Its CLI parts are source lists the root adds to reagentlive::cli and
# reagentlive::console: reagentlive_pack_syntax_sources,
# reagentlive_pack_console_sources and reagentlive_pack_console_link.
set(reagentlive_ue_dir "${CMAKE_CURRENT_LIST_DIR}")

# Every agent handler. Without an SDK the object table and name pool are
# found at run time and engine types read with StockUE5 (engine_layout.h),
# each layout group checked on known objects before its ops run;
# reagentlive::ue builds the same files with the host's SDK as the layout
# (REAGENTLIVE_UE_SDK).
add_library(reagentlive-ue-base INTERFACE)
add_library(reagentlive::ue-base ALIAS reagentlive-ue-base)
target_sources(reagentlive-ue-base INTERFACE "${reagentlive_ue_dir}/agent_ue.cpp"
    "${reagentlive_ue_dir}/agent_ue_census.cpp" "${reagentlive_ue_dir}/agent_ue_globals.cpp"
    "${reagentlive_ue_dir}/agent_ue_check.cpp" "${reagentlive_ue_dir}/agent_ue_entries.cpp"
    "${reagentlive_ue_dir}/agent_ue_reflect.cpp" "${reagentlive_ue_dir}/agent_ue_scan.cpp"
    "${reagentlive_ue_dir}/agent_ue_script.cpp" "${reagentlive_ue_dir}/agent_ue_calls.cpp")
target_link_libraries(reagentlive-ue-base INTERFACE reagentlive-core)
set_property(GLOBAL APPEND PROPERTY REAGENTLIVE_AGENT_TARGETS reagentlive-ue-base)

# Opt-in for hosts: a game-thread tick through the game viewport's Draw and
# the GameThread op (host_draw.h). Links ue-base.
add_library(reagentlive-ue-draw INTERFACE)
add_library(reagentlive::ue-draw ALIAS reagentlive-ue-draw)
target_sources(reagentlive-ue-draw INTERFACE "${reagentlive_ue_dir}/host_draw.cpp")
target_link_libraries(reagentlive-ue-draw INTERFACE reagentlive-ue-base)
set_property(GLOBAL APPEND PROPERTY REAGENTLIVE_AGENT_TARGETS reagentlive-ue-draw)

add_library(reagentlive-ue INTERFACE)
add_library(reagentlive::ue ALIAS reagentlive-ue)
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
    "${reagentlive_ue_dir}/cli/console/layout.cpp" "${reagentlive_ue_dir}/cli/console/text.cpp"
    "${reagentlive_ue_dir}/cli/console/rows.cpp")
set(reagentlive_pack_console_link reagentlive-ue-client)
