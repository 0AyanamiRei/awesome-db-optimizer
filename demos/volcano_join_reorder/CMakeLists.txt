cmake_minimum_required(VERSION 3.20)
project(volcano_join_reorder_demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(DUCKDB_ROOT "/home/refrain/proj/duckdb-adl" CACHE PATH "Path to a DuckDB source checkout")

if(NOT EXISTS "${DUCKDB_ROOT}/CMakeLists.txt")
  message(FATAL_ERROR "DUCKDB_ROOT must point to a DuckDB source checkout")
endif()

add_subdirectory("${DUCKDB_ROOT}" "${CMAKE_BINARY_DIR}/duckdb" EXCLUDE_FROM_ALL)

add_library(volcano_core STATIC
  src/exporter.cpp
  src/optimizer.cpp
  src/stats.cpp
  src/types.cpp
)

target_include_directories(volcano_core
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(volcano_duckdb_frontend STATIC
  src/duckdb_sql_frontend.cpp
)

target_include_directories(volcano_duckdb_frontend
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${DUCKDB_ROOT}/src/include"
)

target_link_libraries(volcano_duckdb_frontend
  PUBLIC
    volcano_core
    duckdb_static
    dummy_static_extension_loader
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
  add_executable(volcano_join_demo
    src/main.cpp
  )

  target_link_libraries(volcano_join_demo
    PRIVATE
      volcano_duckdb_frontend
  )
endif()

enable_testing()

add_executable(volcano_tests
  tests/test_volcano.cpp
)

target_link_libraries(volcano_tests
  PRIVATE
    volcano_duckdb_frontend
)

add_test(NAME volcano_tests COMMAND volcano_tests)
