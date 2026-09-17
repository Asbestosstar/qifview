#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace qifv {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, double s) { return {a.x * s, a.y * s}; }
inline Vec2 operator/(Vec2 a, double s) { return {a.x / s, a.y / s}; }
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(double s, Vec3 a) { return a * s; }
inline Vec3 operator/(Vec3 a, double s) { return {a.x / s, a.y / s, a.z / s}; }
inline Vec3& operator+=(Vec3& a, Vec3 b) { a = a + b; return a; }

inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}
inline double length2(Vec3 a) { return dot(a, a); }
inline double length(Vec3 a) { return std::sqrt(length2(a)); }
inline double distance(Vec3 a, Vec3 b) { return length(a - b); }
inline Vec3 normalized(Vec3 a) {
    const double n = length(a);
    return n > 1e-30 ? a / n : Vec3{};
}
inline Vec2 midpoint(Vec2 a, Vec2 b) { return (a + b) * 0.5; }
inline Vec3 midpoint(Vec3 a, Vec3 b) { return (a + b) * 0.5; }

struct Color {
    std::uint8_t r = 205;
    std::uint8_t g = 211;
    std::uint8_t b = 220;
    std::uint8_t a = 255;
};

struct Bounds3 {
    Vec3 min{ std::numeric_limits<double>::infinity(),
              std::numeric_limits<double>::infinity(),
              std::numeric_limits<double>::infinity() };
    Vec3 max{ -std::numeric_limits<double>::infinity(),
              -std::numeric_limits<double>::infinity(),
              -std::numeric_limits<double>::infinity() };
    bool valid = false;

    void add(Vec3 p) {
        min.x = std::min(min.x, p.x); min.y = std::min(min.y, p.y); min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x); max.y = std::max(max.y, p.y); max.z = std::max(max.z, p.z);
        valid = true;
    }
    Vec3 center() const { return (min + max) * 0.5; }
    double diagonal() const { return valid ? length(max - min) : 0.0; }
};

} // namespace qifv
