# reagentlive consumer test: a host project that links only the agent, the
# way a mod would. Built by ctest (reagentlive.consumer) in its own build
# tree: it must configure with no downloads and see only src/ on the include
# path.
cmake_minimum_required(VERSION 3.25)
project(reagentlive-consumer LANGUAGES CXX)

set(REAGENTLIVE_CONSOLE OFF)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." reagentlive)

if(EXISTS "${CMAKE_BINARY_DIR}/_deps")
    message(FATAL_ERROR "reagentlive consumer: an agent-only configure downloaded dependencies.")
endif()
if(TARGET reagentlive::console)
    message(FATAL_ERROR "reagentlive consumer: REAGENTLIVE_CONSOLE OFF still defined the console.")
endif()

add_library(consumer-host SHARED host.cpp)
target_link_libraries(consumer-host PRIVATE reagentlive::core)
