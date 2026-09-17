#pragma once
#include <array>
#include <optional>

namespace naval_sdk {
using Vec2 = std::array<double, 2>;
using Point = Vec2;
double distance(Point a, Point b);
double wrap_bearing(double degrees);
// Shortest clockwise turn, in [-180, 180), matching the Python implementation.
double signed_bearing_delta(double target, double current);
double clamp(double value, double lo, double hi);
double bearing_to(Point from, Point to);
std::optional<Point> lead_target(Point shooter, Point target, Vec2 velocity, double shell_speed);
}
