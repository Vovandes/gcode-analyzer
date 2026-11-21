include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(gca_tests
    test_smoke.cpp
    test_lexer.cpp
    test_modal.cpp
    test_samples_stage1.cpp
    test_geometry.cpp
    test_time_bbox.cpp
    test_stage3.cpp
    test_stage4.cpp
    test_stage5.cpp
    test_stage6.cpp
    test_stage9.cpp
    test_stage11.cpp
    test_stage11b.cpp
    test_stage12.cpp
    test_stage13.cpp
)
target_link_libraries(gca_tests PRIVATE gca_core gca_warnings Catch2::Catch2WithMain)
# Tests can locate sample programs via this define
target_compile_definitions(gca_tests PRIVATE
    GCA_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples"
    GCA_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden"
    GCA_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
target_compile_definitions(gca_tests PRIVATE GCA_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
target_compile_definitions(gca_tests PRIVATE GCA_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/stage9-output")

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(gca_tests)
