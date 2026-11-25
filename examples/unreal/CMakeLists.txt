# reagentlive Unreal example: a host DLL for any UE5 game with no dump (the
# whole Unreal pack on its own thread, and a game-thread tick through the
# game viewport's Draw on request) and its console. Builds on its own, as any
# consumer of the library would, and is the kit
# tools/project.py copies into a new project:
#
#   cmake -S examples/unreal -B build/unreal -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/unreal
#
# reagentlive's own build adds it for its tests.
cmake_minimum_required(VERSION 3.25)
project(reagentlive-unreal LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
    set(REAGENTLIVE_BUILD_TESTS OFF)
    # A project made from this kit carries ReAgentLive as its reagentlive/
    # submodule; in ReAgentLive's own tree the library is two folders up.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/reagentlive/CMakeLists.txt")
        add_subdirectory(reagentlive)
    else()
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." reagentlive)
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
