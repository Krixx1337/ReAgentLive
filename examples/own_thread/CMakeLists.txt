# reagentlive own-thread example: a host DLL that works in any x64 process
# (memory commands and the fast lane, no engine pack) and the generic console
# for it. Builds on its own, as any consumer of the library would:
#
#   cmake -S examples/own_thread -B build/own-thread -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/own-thread
#
# reagentlive's own build adds it for its tests.
cmake_minimum_required(VERSION 3.25)
project(reagentlive-own-thread LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
    set(REAGENTLIVE_BUILD_TESTS OFF)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." reagentlive)
endif()

add_library(reagentlive-own-thread SHARED host.cpp)
target_link_libraries(reagentlive-own-thread PRIVATE reagentlive::core)
set_property(GLOBAL APPEND PROPERTY REAGENTLIVE_AGENT_TARGETS reagentlive-own-thread)

add_executable(reagentlive-console-exe console.cpp)
target_link_libraries(reagentlive-console-exe PRIVATE reagentlive::console)
set_target_properties(reagentlive-console-exe PROPERTIES OUTPUT_NAME reagentlive)

foreach(target reagentlive-own-thread reagentlive-console-exe)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
    target_compile_options(${target} PRIVATE /W4 /WX /EHsc)
endforeach()

# The console is a host of the CLI: the layout test checks its includes.
if(NOT PROJECT_IS_TOP_LEVEL)
    set(REAGENTLIVE_HOST_SOURCES ${REAGENTLIVE_HOST_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/console.cpp" PARENT_SCOPE)
endif()
