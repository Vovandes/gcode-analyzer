cmake_minimum_required(VERSION 3.21)

project(gcode_analyzer
    VERSION 0.1.0
    DESCRIPTION "G-code analyzer for postprocessor development (Fanuc milling)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GCA_BUILD_TESTS "Build unit tests (Catch2 via FetchContent)" ON)

# ---------------------------------------------------------------------------
# Warnings
# ---------------------------------------------------------------------------
add_library(gca_warnings INTERFACE)
if(MSVC)
    target_compile_options(gca_warnings INTERFACE /W4 /permissive- /utf-8 /EHsc)
else()
    target_compile_options(gca_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

# ---------------------------------------------------------------------------
# Core library: all analysis logic lives here (CLI is a thin wrapper)
# ---------------------------------------------------------------------------
add_library(gca_core STATIC
    src/analyzer.cpp
    src/lexer.cpp
    src/dialect.cpp
    src/knowledge.cpp
    src/modal_state.cpp
    src/motion.cpp
    src/report.cpp
    src/analyzers/code_stats.cpp
    src/analyzers/tools.cpp
    src/analyzers/time.cpp
    src/analyzers/bbox.cpp
)
target_include_directories(gca_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(gca_core PUBLIC GCA_VERSION="${PROJECT_VERSION}")
target_link_libraries(gca_core PRIVATE gca_warnings)

# ---------------------------------------------------------------------------
# CLI executable
# ---------------------------------------------------------------------------
add_executable(gcode-analyzer src/main.cpp)
target_link_libraries(gcode-analyzer PRIVATE gca_core gca_warnings)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(GCA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
