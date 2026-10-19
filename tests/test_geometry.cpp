#include "gca/motion.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace gca;
using Catch::Approx;

TEST_CASE("quarter circle R10 has length 15.708", "[geometry]") {
    // CCW from (10,0) to (0,10), center at the origin (I=-10, J=0)
    const ArcResult a = computeArc(10, 0, 0, 10, 0, 0, -10.0, 0.0, std::nullopt, false);
    REQUIRE(a.ok);
    CHECK(a.radius == Approx(10.0));
    CHECK(a.sweep == Approx(kPi / 2.0));
    CHECK(a.length == Approx(15.708).margin(1e-3));
    CHECK_FALSE(a.fullCircle);
}

TEST_CASE("clockwise arc takes the long way when needed", "[geometry]") {
    // same points, CW: 270 degrees
    const ArcResult a = computeArc(10, 0, 0, 10, 0, 0, -10.0, 0.0, std::nullopt, true);
    REQUIRE(a.ok);
    CHECK(a.sweep == Approx(3.0 * kPi / 2.0));
    CHECK(a.length == Approx(47.124).margin(1e-3));
}

TEST_CASE("full circle: end point equals start point, only I/J", "[geometry]") {
    const ArcResult a = computeArc(5, 0, 5, 0, 0, 0, -5.0, 0.0, std::nullopt, false);
    REQUIRE(a.ok);
    CHECK(a.fullCircle);
    CHECK(a.sweep == Approx(2.0 * kPi));
    CHECK(a.length == Approx(2.0 * kPi * 5.0).margin(1e-6));
    const ArcResult cw = computeArc(5, 0, 5, 0, 0, 0, -5.0, 0.0, std::nullopt, true);
    CHECK(cw.length == Approx(2.0 * kPi * 5.0).margin(1e-6));
}

TEST_CASE("helix: full circle with Z, like thread milling", "[geometry]") {
    // G2 Z-0.5 I0.2 J0. : radius 0.2, one turn, pitch 0.5
    const ArcResult a = computeArc(1.0, 1.0, 1.0, 1.0, 0.0, -0.5, 0.2, 0.0, std::nullopt, true);
    REQUIRE(a.ok);
    const double planar = 2.0 * kPi * 0.2;
    CHECK(a.planarLength == Approx(planar));
    CHECK(a.helixHeight == Approx(0.5));
    CHECK(a.length == Approx(std::sqrt(planar * planar + 0.25)).margin(1e-9));
}

TEST_CASE("helical arc: quarter turn with rise", "[geometry]") {
    const ArcResult a = computeArc(10, 0, 0, 10, 0, 4, -10.0, 0.0, std::nullopt, false);
    REQUIRE(a.ok);
    const double planar = 10.0 * kPi / 2.0;
    CHECK(a.length == Approx(std::hypot(planar, 4.0)));
}

TEST_CASE("R form: positive R is the short arc, negative R the long one", "[geometry]") {
    const ArcResult shortArc = computeArc(10, 0, 0, 10, 0, 0, std::nullopt, std::nullopt, 10.0, false);
    REQUIRE(shortArc.ok);
    CHECK(shortArc.sweep == Approx(kPi / 2.0));
    CHECK(shortArc.centerU == Approx(0.0).margin(1e-9));
    CHECK(shortArc.centerV == Approx(0.0).margin(1e-9));
    const ArcResult longArc = computeArc(10, 0, 0, 10, 0, 0, std::nullopt, std::nullopt, -10.0, false);
    REQUIRE(longArc.ok);
    CHECK(longArc.sweep == Approx(3.0 * kPi / 2.0));
    const ArcResult cw = computeArc(0, 10, 10, 0, 0, 0, std::nullopt, std::nullopt, 10.0, true);
    REQUIRE(cw.ok);
    CHECK(cw.sweep == Approx(kPi / 2.0));
}

TEST_CASE("arc errors do not throw", "[geometry]") {
    CHECK_FALSE(computeArc(0, 0, 100, 0, 0, 0, std::nullopt, std::nullopt, 10.0, false).ok);   // R < chord/2
    CHECK_FALSE(computeArc(0, 0, 10, 0, 0, 0, std::nullopt, std::nullopt, std::nullopt, false).ok);
    CHECK_FALSE(computeArc(5, 5, 5, 5, 0, 0, std::nullopt, std::nullopt, 10.0, false).ok);      // R full circle
}

TEST_CASE("arc extents include quadrant points", "[geometry]") {
    // CCW half circle from (10,0) to (-10,0) around the origin passes through (0,10)
    const ArcResult a = computeArc(10, 0, -10, 0, 0, 0, -10.0, 0.0, std::nullopt, false);
    REQUIRE(a.ok);
    const Extents e = arcExtents(a, 10, 0, -10, 0);
    CHECK(e.uMin == Approx(-10.0));
    CHECK(e.uMax == Approx(10.0));
    CHECK(e.vMax == Approx(10.0));
    CHECK(e.vMin == Approx(0.0).margin(1e-9));
    // full circle
    const ArcResult c = computeArc(5, 0, 5, 0, 0, 0, -5.0, 0.0, std::nullopt, true);
    const Extents ce = arcExtents(c, 5, 0, 5, 0);
    CHECK(ce.uMin == Approx(-5.0));
    CHECK(ce.vMin == Approx(-5.0));
    CHECK(ce.vMax == Approx(5.0));
}

TEST_CASE("plane axes mapping", "[geometry]") {
    CHECK(planeAxes(Plane::XY).h == kAxisZ);
    CHECK(planeAxes(Plane::ZX).u == kAxisZ);
    CHECK(planeAxes(Plane::ZX).uOffset == 'K');
    CHECK(planeAxes(Plane::YZ).vOffset == 'K');
}

TEST_CASE("feed conversion: G94 and G95", "[geometry][feed]") {
    Segment s;
    s.hasF = true;
    s.feed = 600.0;
    CHECK(feedMmPerMin(s).value() == Approx(600.0));
    s.feedMode = FeedMode::PerRev;
    s.feed = 0.1;
    CHECK_FALSE(feedMmPerMin(s).has_value());         // no spindle speed
    s.hasS = true;
    s.s = 1000.0;
    s.spindle = SpindleDir::Cw;
    CHECK(feedMmPerMin(s).value() == Approx(100.0));
    s.spindle = SpindleDir::Off;
    CHECK_FALSE(feedMmPerMin(s).has_value());
}
