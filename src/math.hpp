#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace qifv {

struct Vec2 { double x = 0.0, y = 0.0; };
struct Vec3 { double x = 0.0, y = 0.0, z = 0.0; };

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, double s) { return {a.x * s, a.y * s}; }
inline Vec2 operator*(double s, Vec2 a) { return a * s; }
inline Vec2 operator/(Vec2 a, double s) { return {a.x / s, a.y / s}; }
inline Vec2& operator+=(Vec2& a, Vec2 b) { a = a + b; return a; }

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
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

struct Quaternion {
    double w = 1.0, x = 0.0, y = 0.0, z = 0.0;
};

inline Quaternion normalized(Quaternion q) {
    const double n = std::sqrt(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
    if (n <= 1e-30) return {};
    return {q.w/n, q.x/n, q.y/n, q.z/n};
}

// Rotate a vector by a unit quaternion q = (scalar, vector). QIF QuaternionType
// stores the scalar component first, followed by the x/y/z vector components.
inline Vec3 rotate(Quaternion q, Vec3 v) {
    q = normalized(q);
    const Vec3 u{q.x, q.y, q.z};
    return v * (q.w*q.w - dot(u,u)) + u * (2.0 * dot(u,v)) + cross(u,v) * (2.0 * q.w);
}

// QIF TransformMatrixType is rotation + translation only (no scale).  These
// members are the three rotation columns and Origin, so applyPoint() directly
// follows the equations in ANSI/DMSC QIF 3.0 section 5.16.
struct Transform3 {
    Vec3 x{1.0, 0.0, 0.0};
    Vec3 y{0.0, 1.0, 0.0};
    Vec3 z{0.0, 0.0, 1.0};
    Vec3 origin{};

    Vec3 applyVector(Vec3 v) const { return x * v.x + y * v.y + z * v.z; }
    Vec3 applyPoint(Vec3 p) const { return origin + applyVector(p); }

    // QIF transforms are rigid rotation + translation matrices.  The inverse
    // rotation is therefore the transpose of the three orthonormal columns.
    Vec3 inverseApplyVector(Vec3 v) const { return {dot(v, x), dot(v, y), dot(v, z)}; }
    Vec3 inverseApplyPoint(Vec3 p) const { return inverseApplyVector(p - origin); }

    static Transform3 identity() { return {}; }
};

// Composition a*b means: apply b first, then a.
inline Transform3 compose(const Transform3& a, const Transform3& b) {
    Transform3 r;
    r.x = a.applyVector(b.x);
    r.y = a.applyVector(b.y);
    r.z = a.applyVector(b.z);
    r.origin = a.applyPoint(b.origin);
    return r;
}

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
