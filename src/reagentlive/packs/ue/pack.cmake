# The Unreal pack (id ue), included by the root CMakeLists.txt when
# REAGENTLIVE_PACKS lists it. INTERFACE targets like the core's: the consumer
# compiles them with its own flags.
#
#   reagentlive::ue         agent side (find/fields/call); the consumer must
#                       provide its Dumper-7 SDK on the include path as SDK/...
#   reagentlive::ue-client  client calls (Find, Fields, Call); no SDK needed
#
# Its CLI parts are source lists the root adds to reagentlive::cli and
# reagentlive::console: reagentlive_pack_syntax_sources,
# reagentlive_pack_console_sources and reagentlive_pack_console_link.
set(reagentlive_ue_dir "${CMAKE_CURRENT_LIST_DIR}")

add_library(reagentlive-ue INTERFACE)
add_library(reagentlive::ue ALIAS reagentlive-ue)
target_sources(reagentlive-ue INTERFACE "${reagentlive_ue_dir}/agent_ue.cpp"
    "${reagentlive_ue_dir}/agent_ue_reflect.cpp" "${reagentlive_ue_dir}/agent_ue_scan.cpp"
    "${reagentlive_ue_dir}/agent_ue_script.cpp" "${reagentlive_ue_dir}/agent_ue_calls.cpp"
    "${reagentlive_ue_dir}/agent_ue_globals.cpp")
target_link_libraries(reagentlive-ue INTERFACE reagentlive-core)
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
