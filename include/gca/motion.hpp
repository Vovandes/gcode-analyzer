// Pure geometry and timing helpers for motion segments (no state, easy to test).
#pragma once

#include "gca/modal_state.hpp"

#include <optional>
#include <string>

namespace gca {

constexpr double kPi = 3.14159265358979323846;

/// Result of an arc computation in a plane (u, v) with helix axis h.
struct ArcResult {
    bool ok = false;
    std::string error;
    bool fullCircle = false;
    double centerU = 0.0;
    double centerV = 0.0;
    double radius = 0.0;
    double radiusMismatch = 0.0;    ///< |R(start) - R(end)|
    double startAngle = 0.0;        ///< radians
    double sweep = 0.0;             ///< radians, always positive (0 < sweep <= 2*pi)
    double planarLength = 0.0;      ///< radius * sweep
    double helixHeight = 0.0;       ///< |h1 - h0|
    double length = 0.0;            ///< sqrt(planarLength^2 + helixHeight^2)
    bool cw = false;
};

/// Euclidean length between two points given by arrays of coordinates.
[[nodiscard]] double euclid(const double* a, const double* b, int n);

/// Arc from (u0,v0) to (u1,v1). Center is given either by offsets (i = along u, j = along v from the
/// start point) or by radius r (negative r = arc longer than 180 degrees). Coincident end points with
/// offsets give a full circle. cw: clockwise seen from the positive side of the plane normal.
[[nodiscard]] ArcResult computeArc(double u0, double v0, double u1, double v1, double h0, double h1,
                                   std::optional<double> iOff, std::optional<double> jOff,
                                   std::optional<double> r, bool cw);

struct Extents {
    double uMin, uMax, vMin, vMax;
};
/// Exact bounding rectangle of the arc (end points and quadrant extremes).
[[nodiscard]] Extents arcExtents(const ArcResult& arc, double u0, double v0, double u1, double v1);

/// Plane -> indices of the axes (u, v, helix axis) and the offset letters for u and v.
struct PlaneAxes {
    int u, v, h;
    char uOffset, vOffset;
};
[[nodiscard]] PlaneAxes planeAxes(Plane plane);

/// Arc geometry of an Arc segment; error "unknown start point" when the plane axes are not known.
[[nodiscard]] ArcResult computeSegmentArc(const Segment& seg);

/// Feed in mm/min for a segment (G94: F, G95: F * S). nullopt if it cannot be determined.
[[nodiscard]] std::optional<double> feedMmPerMin(const Segment& seg);

/// Lengths and times of one segment.
struct SegmentMetrics {
    double rapidMm = 0.0;
    double feedMm = 0.0;
    double dwellSec = 0.0;
    double rapidSec = 0.0;
    double feedSec = 0.0;
    bool partial = false;           ///< start point (some axis) unknown, length not fully counted
    bool noFeed = false;            ///< cutting length but feed unknown/zero: feed time not counted
    bool arcError = false;
    std::string arcMessage;
};
[[nodiscard]] SegmentMetrics measureSegment(const Segment& seg, double rapidMmPerMin);

} // namespace gca
