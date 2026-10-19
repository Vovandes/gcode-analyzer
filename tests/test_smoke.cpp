#include "gca/analyzer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

TEST_CASE("version is not empty", "[smoke]") {
    REQUIRE_FALSE(gca::version().empty());
}

TEST_CASE("sample programs are present", "[smoke][samples]") {
    const std::filesystem::path dir{GCA_SAMPLES_DIR};
    CHECK(std::filesystem::exists(dir / "sample-01-skymaster-vl1100a.nc"));
    CHECK(std::filesystem::exists(dir / "sample-02-bridgeport-1300-480.nc"));
}
