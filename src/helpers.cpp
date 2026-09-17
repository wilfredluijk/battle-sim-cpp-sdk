#include <naval_sdk/helpers.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace naval_sdk {
double distance(Point a, Point b) { return std::hypot(b[0] - a[0], b[1] - a[1]); }
double wrap_bearing(double degrees) {
    auto result = std::fmod(degrees, 360.0);
    return result < 0 ? result + 360 : result;
}
double signed_bearing_delta(double target, double current) {
    return wrap_bearing(target - current + 540) - 180;
}
double clamp(double value, double lo, double hi) { return std::max(lo, std::min(value, hi)); }
double bearing_to(Point from, Point to) {
    return wrap_bearing(std::atan2(to[0] - from[0], from[1] - to[1]) * 180 / std::acos(-1));
}
std::optional<Point> lead_target(Point shooter, Point target, Vec2 velocity, double speed) {
    if (!std::isfinite(speed) || speed <= 0) return std::nullopt;
    double rx = target[0] - shooter[0], ry = target[1] - shooter[1];
    double a = velocity[0]*velocity[0] + velocity[1]*velocity[1] - speed*speed;
    double b = 2*(rx*velocity[0] + ry*velocity[1]), c = rx*rx + ry*ry;
    double t = std::numeric_limits<double>::infinity();
    if (std::abs(a) < 1e-9) {
        if (std::abs(b) < 1e-9) { if (c < 1e-9) t = 0; }
        else if (-c/b >= 0) t = -c/b;
    } else {
        double disc = b*b - 4*a*c;
        if (disc < 0) return std::nullopt;
        double root = std::sqrt(disc);
        for (double candidate : {(-b-root)/(2*a), (-b+root)/(2*a)})
            if (candidate >= 0) t = std::min(t, candidate);
    }
    if (!std::isfinite(t)) return std::nullopt;
    return Point{target[0] + velocity[0]*t, target[1] + velocity[1]*t};
}
}
