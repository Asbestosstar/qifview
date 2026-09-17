#include "qif_model.hpp"

#include <mapbox/earcut.hpp>
#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace qifv {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

double sqr(double x) { return x * x; }
double dist2(Vec2 a, Vec2 b) { return sqr(a.x - b.x) + sqr(a.y - b.y); }

std::string localName(const char* raw) {
    if (!raw) return {};
    std::string s(raw);
    const auto pos = s.find(':');
    return pos == std::string::npos ? s : s.substr(pos + 1);
}

pugi::xml_node childLocal(pugi::xml_node n, const char* name) {
    for (auto c : n.children()) {
        if (c.type() == pugi::node_element && localName(c.name()) == name) return c;
    }
    return {};
}

pugi::xml_node firstElementChild(pugi::xml_node n) {
    for (auto c : n.children()) if (c.type() == pugi::node_element) return c;
    return {};
}

pugi::xml_node findFirstLocal(pugi::xml_node n, const char* name) {
    if (n && localName(n.name()) == name) return n;
    for (auto c : n.children()) {
        if (c.type() != pugi::node_element) continue;
        auto r = findFirstLocal(c, name);
        if (r) return r;
    }
    return {};
}

std::vector<double> parseNumbers(const char* text) {
    std::vector<double> out;
    if (!text) return out;
    std::istringstream ss(text);
    double v = 0.0;
    while (ss >> v) out.push_back(v);
    return out;
}

Vec2 parseVec2(const char* text) {
    const auto v = parseNumbers(text);
    return {v.size() > 0 ? v[0] : 0.0, v.size() > 1 ? v[1] : 0.0};
}

Vec3 parseVec3(const char* text) {
    const auto v = parseNumbers(text);
    return {v.size() > 0 ? v[0] : 0.0,
            v.size() > 1 ? v[1] : 0.0,
            v.size() > 2 ? v[2] : 0.0};
}

std::string nodeText(pugi::xml_node n, const char* childName) {
    auto c = childLocal(n, childName);
    return c ? c.child_value() : std::string{};
}

double nodeDouble(pugi::xml_node n, const char* childName, double fallback = 0.0) {
    auto c = childLocal(n, childName);
    return c ? c.text().as_double(fallback) : fallback;
}

int nodeInt(pugi::xml_node n, const char* childName, int fallback = 0) {
    auto c = childLocal(n, childName);
    return c ? c.text().as_int(fallback) : fallback;
}

std::vector<double> basisAll(double t, int order, const std::vector<double>& knots, std::size_t ncp) {
    if (order <= 0 || ncp == 0 || knots.size() < ncp + static_cast<std::size_t>(order)) return {};
    const int degree = order - 1;
    std::vector<double> prev(ncp, 0.0), next(ncp, 0.0);

    const double lo = knots[static_cast<std::size_t>(degree)];
    const double hi = knots[ncp];
    if (t < lo) t = lo;
    if (t > hi) t = hi;

    for (std::size_t i = 0; i < ncp; ++i) {
        const bool inSpan = knots[i] <= t && t < knots[i + 1];
        const bool atEnd = (std::abs(t - hi) <= 1e-12 * std::max(1.0, std::abs(hi))) && i + 1 == ncp;
        prev[i] = (inSpan || atEnd) ? 1.0 : 0.0;
    }

    for (int k = 1; k <= degree; ++k) {
        std::fill(next.begin(), next.end(), 0.0);
        for (std::size_t i = 0; i < ncp; ++i) {
            double a = 0.0, b = 0.0;
            const double d1 = knots[i + static_cast<std::size_t>(k)] - knots[i];
            if (std::abs(d1) > 1e-30) a = (t - knots[i]) / d1 * prev[i];
            if (i + 1 < ncp) {
                const double d2 = knots[i + static_cast<std::size_t>(k) + 1] - knots[i + 1];
                if (std::abs(d2) > 1e-30) b = (knots[i + static_cast<std::size_t>(k) + 1] - t) / d2 * prev[i + 1];
            }
            next[i] = a + b;
        }
        prev.swap(next);
    }
    return prev;
}

struct NurbsCurve2 {
    int order = 0;
    std::vector<double> knots;
    std::vector<Vec2> cps;
    std::vector<double> weights;
    double t0 = 0.0;
    double t1 = 1.0;

    Vec2 eval(double t) const {
        const auto b = basisAll(t, order, knots, cps.size());
        if (b.size() != cps.size()) return {};
        Vec2 num{};
        double den = 0.0;
        for (std::size_t i = 0; i < cps.size(); ++i) {
            const double w = i < weights.size() ? weights[i] : 1.0;
            const double q = b[i] * w;
            num = num + cps[i] * q;
            den += q;
        }
        return std::abs(den) > 1e-30 ? num / den : Vec2{};
    }
};

struct NurbsSurface3 {
    int orderU = 0;
    int orderV = 0;
    std::vector<double> knotsU;
    std::vector<double> knotsV;
    std::size_t countU = 0;
    std::size_t countV = 0;
    std::vector<Vec3> cps;      // U varies fastest.
    std::vector<double> weights;

    Vec3 eval(double u, double v) const {
        const auto bu = basisAll(u, orderU, knotsU, countU);
        const auto bv = basisAll(v, orderV, knotsV, countV);
        if (bu.size() != countU || bv.size() != countV || cps.size() != countU * countV) return {};
        Vec3 num{};
        double den = 0.0;
        for (std::size_t j = 0; j < countV; ++j) {
            for (std::size_t i = 0; i < countU; ++i) {
                const std::size_t k = j * countU + i;
                const double w = k < weights.size() ? weights[k] : 1.0;
                const double q = bu[i] * bv[j] * w;
                num += cps[k] * q;
                den += q;
            }
        }
        return std::abs(den) > 1e-30 ? num / den : Vec3{};
    }
};

enum class SurfaceType { Unsupported, Plane, Cylinder, Cone, Sphere, Torus, Nurbs };

struct Surface {
    SurfaceType type = SurfaceType::Unsupported;
    Vec3 origin{};
    Vec3 dirU{};
    Vec3 dirV{};

    Vec3 axisPoint{};
    Vec3 axisDir{};
    Vec3 dirBeg{};
    Vec3 dirSide{};
    double scaleU = 1.0;
    double scaleV = 1.0;
    double diameter = 0.0;
    double diameterBottom = 0.0;
    double diameterTop = 0.0;
    double length = 0.0;

    Vec3 center{};
    Vec3 meridian{};
    Vec3 north{};
    Vec3 east{};
    double diameterMajor = 0.0;
    double diameterMinor = 0.0;

    NurbsSurface3 nurbs;

    bool curved() const { return type != SurfaceType::Plane; }

    Vec3 eval(Vec2 uv) const {
        const double u = uv.x;
        const double v = uv.y;
        switch (type) {
        case SurfaceType::Plane:
            return origin + dirU * u + dirV * v;
        case SurfaceType::Cylinder: {
            const double r = diameter * 0.5;
            return axisPoint + axisDir * (v * scaleV)
                 + (dirBeg * std::cos(u) + dirSide * std::sin(u)) * r;
        }
        case SurfaceType::Cone: {
            const double z = v * scaleV;
            const double f = std::abs(length) > 1e-30 ? z / length : 0.0;
            const double r = 0.5 * (diameterBottom + (diameterTop - diameterBottom) * f);
            return axisPoint + axisDir * z
                 + (dirBeg * std::cos(u) + dirSide * std::sin(u)) * r;
        }
        case SurfaceType::Sphere: {
            const double lon = u * scaleU;
            const double lat = v * scaleV;
            const double r = diameter * 0.5;
            const Vec3 radial = meridian * std::cos(lon) + east * std::sin(lon);
            return center + (radial * std::cos(lat) + north * std::sin(lat)) * r;
        }
        case SurfaceType::Torus: {
            const double lon = u * scaleU;
            const double lat = v * scaleV;
            const double R = diameterMajor * 0.5;
            const double r = diameterMinor * 0.5;
            const Vec3 radial = meridian * std::cos(lon) + east * std::sin(lon);
            return center + radial * (R + r * std::cos(lat)) + north * (r * std::sin(lat));
        }
        case SurfaceType::Nurbs:
            return nurbs.eval(u, v);
        default:
            return {};
        }
    }
};

bool parseNurbsCurveCore(pugi::xml_node core, NurbsCurve2& c) {
    c.order = nodeInt(core, "Order", 0);
    c.knots = parseNumbers(nodeText(core, "Knots").c_str());
    const auto cp = parseNumbers(nodeText(core, "CPs").c_str());
    for (std::size_t i = 0; i + 1 < cp.size(); i += 2) c.cps.push_back({cp[i], cp[i + 1]});
    c.weights = parseNumbers(nodeText(core, "Weights").c_str());
    if (c.weights.empty()) c.weights.assign(c.cps.size(), 1.0);
    if (c.order <= 0 || c.cps.empty() || c.knots.size() < c.cps.size() + static_cast<std::size_t>(c.order)) return false;

    const auto domain = parseNumbers(core.attribute("domain").value());
    if (domain.size() >= 2) {
        c.t0 = domain[0]; c.t1 = domain[1];
    } else {
        c.t0 = c.knots[static_cast<std::size_t>(c.order - 1)];
        c.t1 = c.knots[c.cps.size()];
    }
    return true;
}

bool parseNurbsSurfaceCore(pugi::xml_node core, NurbsSurface3& s) {
    s.orderU = nodeInt(core, "OrderU", 0);
    s.orderV = nodeInt(core, "OrderV", 0);
    s.knotsU = parseNumbers(nodeText(core, "KnotsU").c_str());
    s.knotsV = parseNumbers(nodeText(core, "KnotsV").c_str());
    if (s.orderU <= 0 || s.orderV <= 0 ||
        s.knotsU.size() < static_cast<std::size_t>(s.orderU) ||
        s.knotsV.size() < static_cast<std::size_t>(s.orderV)) return false;
    s.countU = s.knotsU.size() - static_cast<std::size_t>(s.orderU);
    s.countV = s.knotsV.size() - static_cast<std::size_t>(s.orderV);
    const auto cp = parseNumbers(nodeText(core, "CPs").c_str());
    for (std::size_t i = 0; i + 2 < cp.size(); i += 3) s.cps.push_back({cp[i], cp[i + 1], cp[i + 2]});
    s.weights = parseNumbers(nodeText(core, "Weights").c_str());
    if (s.weights.empty()) s.weights.assign(s.cps.size(), 1.0);
    return s.countU * s.countV == s.cps.size();
}

bool parseSurface(pugi::xml_node n, Surface& s) {
    const std::string type = localName(n.name());
    auto core = firstElementChild(n);
    if (!core) return false;

    if (type == "Plane23") {
        s.type = SurfaceType::Plane;
        s.origin = parseVec3(nodeText(core, "Origin").c_str());
        s.dirU = parseVec3(nodeText(core, "DirU").c_str());
        s.dirV = parseVec3(nodeText(core, "DirV").c_str());
        return true;
    }

    if (type == "Cylinder23" || type == "Cone23") {
        auto axis = childLocal(core, "Axis");
        auto sweep = childLocal(core, "Sweep");
        if (!axis || !sweep) return false;
        s.axisPoint = parseVec3(nodeText(axis, "AxisPoint").c_str());
        s.axisDir = normalized(parseVec3(nodeText(axis, "Direction").c_str()));
        s.dirBeg = normalized(parseVec3(nodeText(sweep, "DirBeg").c_str()));
        s.dirSide = normalized(cross(s.axisDir, s.dirBeg));
        s.scaleV = core.attribute("scaleV").as_double(1.0);
        if (type == "Cylinder23") {
            s.type = SurfaceType::Cylinder;
            s.diameter = nodeDouble(core, "Diameter");
            s.length = nodeDouble(core, "Length");
        } else {
            s.type = SurfaceType::Cone;
            s.diameterBottom = nodeDouble(core, "DiameterBottom");
            s.diameterTop = nodeDouble(core, "DiameterTop");
            s.length = nodeDouble(core, "Length");
        }
        return true;
    }

    if (type == "Sphere23") {
        s.type = SurfaceType::Sphere;
        s.diameter = nodeDouble(core, "Diameter");
        s.center = parseVec3(nodeText(core, "Location").c_str());
        s.scaleU = core.attribute("scaleU").as_double(1.0);
        s.scaleV = core.attribute("scaleV").as_double(1.0);
        auto sweep = childLocal(core, "LatitudeLongitudeSweep");
        if (!sweep) return false;
        s.meridian = normalized(parseVec3(nodeText(sweep, "DirMeridianPrime").c_str()));
        auto np = childLocal(sweep, "DirNorthPole");
        s.north = np ? normalized(parseVec3(np.child_value())) : Vec3{0.0, 0.0, 1.0};
        s.east = normalized(cross(s.north, s.meridian));
        return true;
    }

    if (type == "Torus23") {
        s.type = SurfaceType::Torus;
        s.diameterMinor = nodeDouble(core, "DiameterMinor");
        s.diameterMajor = nodeDouble(core, "DiameterMajor");
        s.scaleU = core.attribute("scaleU").as_double(1.0);
        s.scaleV = core.attribute("scaleV").as_double(1.0);
        auto axis = childLocal(core, "Axis");
        auto sweep = childLocal(core, "LatitudeLongitudeSweep");
        if (!axis || !sweep) return false;
        s.center = parseVec3(nodeText(axis, "AxisPoint").c_str());
        s.north = normalized(parseVec3(nodeText(axis, "Direction").c_str()));
        s.meridian = normalized(parseVec3(nodeText(sweep, "DirMeridianPrime").c_str()));
        s.east = normalized(cross(s.north, s.meridian));
        return true;
    }

    if (type == "Nurbs23") {
        s.type = SurfaceType::Nurbs;
        return parseNurbsSurfaceCore(core, s.nurbs);
    }

    return false;
}

void appendCurveSamples(std::vector<Vec2>& dst, std::vector<Vec2> src) {
    if (src.empty()) return;
    if (!dst.empty()) {
        // CoEdges/SubCurves in a QIF loop are topologically connected. CAD exports
        // can leave tiny numerical gaps between the two p-curve endpoints, so pick
        // the endpoint closest to the previous curve and then snap the joint by
        // retaining the previous endpoint rather than creating a microscopic edge.
        if (dist2(dst.back(), src.back()) < dist2(dst.back(), src.front())) std::reverse(src.begin(), src.end());
        src.erase(src.begin());
    }
    dst.insert(dst.end(), src.begin(), src.end());
}

std::vector<Vec2> sampleCurveCore(pugi::xml_node core, int samplesPerSpan);

std::vector<Vec2> sampleAggregateCore(pugi::xml_node core, int samplesPerSpan) {
    std::vector<Vec2> out;
    auto subs = childLocal(core, "SubCurves");
    if (!subs) return out;
    for (auto sub : subs.children()) {
        if (sub.type() != pugi::node_element || localName(sub.name()) != "SubCurve") continue;
        auto subCore = firstElementChild(sub);
        if (!subCore) continue;
        auto pts = sampleCurveCore(subCore, samplesPerSpan);
        if (sub.attribute("turned").as_bool(false)) std::reverse(pts.begin(), pts.end());
        appendCurveSamples(out, std::move(pts));
    }
    return out;
}

std::vector<Vec2> sampleCurveCore(pugi::xml_node core, int samplesPerSpan) {
    const std::string t = localName(core.name());
    if (t == "Segment12Core") {
        return {parseVec2(nodeText(core, "StartPoint").c_str()),
                parseVec2(nodeText(core, "EndPoint").c_str())};
    }
    if (t == "Nurbs12Core") {
        NurbsCurve2 c;
        if (!parseNurbsCurveCore(core, c)) return {};
        const int count = std::clamp(static_cast<int>(std::max<std::size_t>(2, c.cps.size()) - 1) * std::max(2, samplesPerSpan), 8, 160);
        std::vector<Vec2> pts;
        pts.reserve(static_cast<std::size_t>(count + 1));
        for (int i = 0; i <= count; ++i) {
            const double f = static_cast<double>(i) / static_cast<double>(count);
            pts.push_back(c.eval(c.t0 + (c.t1 - c.t0) * f));
        }
        return pts;
    }
    if (t == "Aggregate12Core") return sampleAggregateCore(core, samplesPerSpan);
    return {};
}

std::vector<Vec2> sampleCurveNode(pugi::xml_node curve, int samplesPerSpan) {
    auto core = firstElementChild(curve);
    return core ? sampleCurveCore(core, samplesPerSpan) : std::vector<Vec2>{};
}

Color parseColor(pugi::xml_node face) {
    const auto v = parseNumbers(face.attribute("color").value());
    Color c;
    if (v.size() >= 3) {
        c.r = static_cast<std::uint8_t>(std::clamp(v[0], 0.0, 255.0));
        c.g = static_cast<std::uint8_t>(std::clamp(v[1], 0.0, 255.0));
        c.b = static_cast<std::uint8_t>(std::clamp(v[2], 0.0, 255.0));
    }
    return c;
}

using EarPoint = std::array<double, 2>;
using EarRing = std::vector<EarPoint>;
using EarPolygon = std::vector<EarRing>;

struct RingData {
    bool outer = false;
    std::vector<Vec2> uv;
};

void cleanRing(std::vector<Vec2>& ring) {
    if (ring.empty()) return;

    double minX = ring.front().x, maxX = ring.front().x;
    double minY = ring.front().y, maxY = ring.front().y;
    for (auto p : ring) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    const double scale = std::max({maxX - minX, maxY - minY, 1.0});
    const double duplicateTol2 = sqr(scale * 1e-12);
    const double closureTol2 = sqr(scale * 1e-6);

    // A Loop is closed by definition. Real QIF exporters commonly leave p-curve
    // closure errors around 1e-8..1e-6 model-parametric units. Snapping these is
    // important because otherwise the tiny closing segment can self-intersect at
    // a sphere pole or periodic-surface seam and confuse polygon triangulation.
    if (ring.size() > 1 && dist2(ring.front(), ring.back()) <= closureTol2) ring.pop_back();

    std::vector<Vec2> clean;
    clean.reserve(ring.size());
    for (auto p : ring) {
        if (clean.empty() || dist2(clean.back(), p) > duplicateTol2) clean.push_back(p);
    }
    if (clean.size() > 1 && dist2(clean.front(), clean.back()) <= closureTol2) clean.pop_back();
    ring.swap(clean);
}

void emitRefined(const Surface& surface,
                 Vec2 ua, Vec2 ub, Vec2 uc,
                 Color color,
                 double tolerance,
                 int depth,
                 int maxDepth,
                 std::vector<Triangle>& triangles,
                 Bounds3& bounds) {
    const Vec3 a = surface.eval(ua);
    const Vec3 b = surface.eval(ub);
    const Vec3 c = surface.eval(uc);

    bool split = false;
    if (surface.curved() && depth < maxDepth) {
        const Vec2 uab = midpoint(ua, ub);
        const Vec2 ubc = midpoint(ub, uc);
        const Vec2 uca = midpoint(uc, ua);
        const Vec2 ucenter{(ua.x + ub.x + uc.x) / 3.0, (ua.y + ub.y + uc.y) / 3.0};
        const double e0 = distance(surface.eval(uab), midpoint(a, b));
        const double e1 = distance(surface.eval(ubc), midpoint(b, c));
        const double e2 = distance(surface.eval(uca), midpoint(c, a));
        const double e3 = distance(surface.eval(ucenter), (a + b + c) / 3.0);
        split = std::max(std::max(e0, e1), std::max(e2, e3)) > tolerance;
    }

    if (split) {
        const Vec2 ab = midpoint(ua, ub);
        const Vec2 bc = midpoint(ub, uc);
        const Vec2 ca = midpoint(uc, ua);
        emitRefined(surface, ua, ab, ca, color, tolerance, depth + 1, maxDepth, triangles, bounds);
        emitRefined(surface, ab, ub, bc, color, tolerance, depth + 1, maxDepth, triangles, bounds);
        emitRefined(surface, ca, bc, uc, color, tolerance, depth + 1, maxDepth, triangles, bounds);
        emitRefined(surface, ab, bc, ca, color, tolerance, depth + 1, maxDepth, triangles, bounds);
        return;
    }

    if (length2(cross(b - a, c - a)) < 1e-28) return;
    triangles.push_back({a, b, c, color});
    bounds.add(a); bounds.add(b); bounds.add(c);
}

void indexIds(pugi::xml_node n, std::unordered_map<std::string, pugi::xml_node>& ids, std::vector<pugi::xml_node>& faces) {
    if (n.type() == pugi::node_element) {
        if (auto a = n.attribute("id")) ids[a.value()] = n;
        if (localName(n.name()) == "Face") faces.push_back(n);
    }
    for (auto c : n.children()) indexIds(c, ids, faces);
}

bool parseLoopRing(pugi::xml_node loop,
                   const std::unordered_map<std::string, pugi::xml_node>& ids,
                   int samplesPerSpan,
                   RingData& out) {
    out.outer = std::string(loop.attribute("form").value()) != "INNER";
    auto coedges = childLocal(loop, "CoEdges");
    if (!coedges) return false;

    for (auto ce : coedges.children()) {
        if (ce.type() != pugi::node_element || localName(ce.name()) != "CoEdge") continue;
        auto curveRef = childLocal(ce, "Curve12");
        auto idNode = childLocal(curveRef, "Id");
        if (!idNode) continue;
        const auto it = ids.find(idNode.child_value());
        if (it == ids.end()) continue;
        auto pts = sampleCurveNode(it->second, samplesPerSpan);
        appendCurveSamples(out.uv, std::move(pts));
    }
    cleanRing(out.uv);
    return out.uv.size() >= 3;
}

} // namespace

bool QifLoader::load(const std::string& path,
                     QifMesh& out,
                     std::string& error,
                     const LoadOptions& options) {
    out = {};
    error.clear();

    pugi::xml_document doc;
    const auto result = doc.load_file(path.c_str(), pugi::parse_default | pugi::parse_ws_pcdata);
    if (!result) {
        error = std::string("XML parse failed: ") + result.description();
        return false;
    }

    auto root = doc.document_element();
    if (!root || localName(root.name()) != "QIFDocument") {
        error = "Not a QIFDocument XML file.";
        return false;
    }
    out.qifVersion = root.attribute("versionQIF").value();

    if (auto fileUnits = findFirstLocal(root, "FileUnits")) {
        if (auto primary = childLocal(fileUnits, "PrimaryUnits")) {
            if (auto linear = childLocal(primary, "LinearUnit")) {
                if (auto unitName = childLocal(linear, "UnitName")) out.linearUnit = unitName.child_value();
            }
        }
    }

    Bounds3 rawBounds;
    std::function<void(pugi::xml_node)> scanPoints = [&](pugi::xml_node n) {
        if (n.type() == pugi::node_element && localName(n.name()) == "Point") {
            if (auto xyz = childLocal(n, "XYZ")) rawBounds.add(parseVec3(xyz.child_value()));
        }
        for (auto c : n.children()) scanPoints(c);
    };
    scanPoints(root);

    const double diag = std::max(rawBounds.diagonal(), 1.0);
    const double tolerance = options.tessellationTolerance > 0.0
                           ? options.tessellationTolerance
                           : diag * 2.5e-4;
    out.tessellationTolerance = tolerance;

    std::unordered_map<std::string, pugi::xml_node> ids;
    std::vector<pugi::xml_node> faces;
    ids.reserve(static_cast<std::size_t>(root.attribute("idMax").as_ullong(4096)) + 128);
    indexIds(root, ids, faces);
    out.faceCount = faces.size();

    for (auto face : faces) {
        auto surfaceRef = childLocal(face, "Surface");
        auto surfaceId = childLocal(surfaceRef, "Id");
        if (!surfaceId) { ++out.skippedFaces; continue; }
        auto sit = ids.find(surfaceId.child_value());
        if (sit == ids.end()) { ++out.skippedFaces; continue; }

        Surface surface;
        if (!parseSurface(sit->second, surface)) { ++out.skippedFaces; continue; }

        std::vector<RingData> rings;
        auto loopIds = childLocal(face, "LoopIds");
        if (!loopIds) { ++out.skippedFaces; continue; }
        for (auto idNode : loopIds.children()) {
            if (idNode.type() != pugi::node_element || localName(idNode.name()) != "Id") continue;
            auto lit = ids.find(idNode.child_value());
            if (lit == ids.end() || localName(lit->second.name()) != "Loop") continue;
            RingData r;
            if (parseLoopRing(lit->second, ids, options.nurbsCurveSamplesPerSpan, r)) rings.push_back(std::move(r));
        }
        if (rings.empty()) { ++out.skippedFaces; continue; }

        auto outerIt = std::find_if(rings.begin(), rings.end(), [](const RingData& r) { return r.outer; });
        if (outerIt != rings.end() && outerIt != rings.begin()) std::iter_swap(rings.begin(), outerIt);
        if (!rings.front().outer) { ++out.skippedFaces; continue; }

        EarPolygon polygon;
        std::vector<Vec2> flatUV;
        polygon.reserve(rings.size());
        for (const auto& ring : rings) {
            EarRing er;
            er.reserve(ring.uv.size());
            for (auto uv : ring.uv) {
                er.push_back({uv.x, uv.y});
                flatUV.push_back(uv);
            }
            polygon.push_back(std::move(er));

            Polyline3 line;
            line.points.reserve(ring.uv.size() + 1);
            for (auto uv : ring.uv) line.points.push_back(surface.eval(uv));
            if (!line.points.empty()) line.points.push_back(line.points.front());
            out.edges.push_back(std::move(line));
        }

        std::vector<std::uint32_t> indices;
        try {
            indices = mapbox::earcut<std::uint32_t>(polygon);
        } catch (...) {
            ++out.skippedFaces;
            continue;
        }
        if (indices.size() < 3) { ++out.skippedFaces; continue; }

        const Color color = parseColor(face);
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
            const std::size_t ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
            if (ia >= flatUV.size() || ib >= flatUV.size() || ic >= flatUV.size()) continue;
            emitRefined(surface, flatUV[ia], flatUV[ib], flatUV[ic], color,
                        tolerance, 0, std::max(0, options.maxRefinementDepth),
                        out.triangles, out.bounds);
        }
    }

    if (!out.bounds.valid && rawBounds.valid) out.bounds = rawBounds;
    if (out.triangles.empty() && out.edges.empty()) {
        error = "The QIF file was parsed, but no supported QIF 3D face geometry could be tessellated.";
        return false;
    }
    return true;
}

} // namespace qifv
