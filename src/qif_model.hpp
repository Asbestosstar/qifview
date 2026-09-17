#pragma once

#include "math.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace qifv {

struct Triangle {
    Vec3 a;
    Vec3 b;
    Vec3 c;
    Color color;
};

struct Polyline3 {
    std::vector<Vec3> points;
};

struct QifMesh {
    std::vector<Triangle> triangles;
    std::vector<Polyline3> edges;
    Bounds3 bounds;
    std::size_t faceCount = 0;
    std::size_t skippedFaces = 0;
    std::string qifVersion;
    std::string linearUnit;
    double tessellationTolerance = 0.0;
};

struct LoadOptions {
    // <= 0 means derive a tolerance from the QIF model bounds.
    double tessellationTolerance = 0.0;
    int maxRefinementDepth = 7;
    int nurbsCurveSamplesPerSpan = 8;
};

class QifLoader {
public:
    // Loads and tessellates QIF 3.x B-rep geometry.
    // On failure, returns false and puts a human-readable message in error.
    static bool load(const std::string& path,
                     QifMesh& out,
                     std::string& error,
                     const LoadOptions& options = {});
};

} // namespace qifv
