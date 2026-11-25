# reagentlive Unreal example: a host DLL for any UE5 game with no dump (the
# whole Unreal pack on its own thread, and a game-thread tick through the
# game viewport's Draw on request) and its console. Builds on its own, as any
# consumer of the library would, and works as a
# starting point copied into a project of its own:
#
#   cmake --preset x64-release              (in this folder; CMakePresets.json)
#   cmake --build --preset x64-release
#
# reagentlive's own build adds it for its tests.
cmake_minimum_required(VERSION 3.25)
project(reagentlive-unreal LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
    set(REAGENTLIVE_BUILD_TESTS OFF)
    # Where ReAgentLive is: REAGENTLIVE_DIR when set; else, in a copy of this
    # example, its reagentlive/ submodule; in ReAgentLive's own tree, two
    # folders up.
    set(REAGENTLIVE_DIR "" CACHE PATH "The ReAgentLive checkout (default: reagentlive/ beside this file)")
    if(REAGENTLIVE_DIR)
        add_subdirectory("${REAGENTLIVE_DIR}" reagentlive)
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/reagentlive/CMakeLists.txt")
        add_subdirectory(reagentlive)
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../src/reagentlive/version.h")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." reagentlive)
    else()
        message(FATAL_ERROR "ReAgentLive not found: git submodule add https://github.com/Krixx1337/ReAgentLive.git reagentlive (or git submodule update --init), or set REAGENTLIVE_DIR to its checkout")
    endif()
endif()

add_library(reagentlive-unreal SHARED host.cpp)
target_link_libraries(reagentlive-unreal PRIVATE reagentlive::ue-draw)
set_property(GLOBAL APPEND PROPERTY REAGENTLIVE_AGENT_TARGETS reagentlive-unreal)

add_executable(reagentlive-ue-console-exe console.cpp)
target_link_libraries(reagentlive-ue-console-exe PRIVATE reagentlive::console)
set_target_properties(reagentlive-ue-console-exe PROPERTIES OUTPUT_NAME reagentlive-ue)

foreach(target reagentlive-unreal reagentlive-ue-console-exe)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
    target_compile_options(${target} PRIVATE /W4 /WX /EHsc)
endforeach()

# The console is a host of the CLI: the layout test checks its includes.
if(NOT PROJECT_IS_TOP_LEVEL)
    set(REAGENTLIVE_HOST_SOURCES ${REAGENTLIVE_HOST_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/console.cpp" PARENT_SCOPE)
endif()
