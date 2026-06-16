cmake_minimum_required(VERSION 3.20)
project(volcano_join_reorder_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- volcano_core library (shared infrastructure) ---
add_library(volcano_core STATIC
  src/cost_model.cpp
  src/exporter.cpp
  src/stats.cpp
  src/types.cpp
)

target_include_directories(volcano_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# --- Strategy implementations ---
add_library(volcano_strategies STATIC
  src/bottom_up_dp.cpp
  src/top_down_partitioning.cpp
  src/transformational.cpp
)

target_include_directories(volcano_strategies
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(volcano_strategies
  PUBLIC
    volcano_core
)

# --- Test cases library ---
add_library(volcano_test_cases STATIC
  examples/test_cases.cpp
)

target_include_directories(volcano_test_cases
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/examples"
)

target_link_libraries(volcano_test_cases
  PUBLIC
    volcano_core
)

# --- Main demo binary ---
add_executable(volcano_join_demo
  src/main.cpp
)

target_link_libraries(volcano_join_demo
  PRIVATE
    volcano_strategies
    volcano_test_cases
)

# --- Tests ---
enable_testing()

add_executable(volcano_tests
  tests/test_join_search.cpp
)

target_link_libraries(volcano_tests
  PRIVATE
    volcano_strategies
    volcano_test_cases
)

add_test(NAME volcano_tests COMMAND volcano_tests)
