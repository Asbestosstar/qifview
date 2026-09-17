#include "qif_model.hpp"

#include <mapbox/earcut.hpp>
#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace qifv {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kTiny = 1e-30;

double sqr(double x) { return x * x; }
double dist2(Vec2 a, Vec2 b) { return sqr(a.x - b.x) + sqr(a.y - b.y); }

std::string localName(const char* raw) {
    if (!raw) return {};
    std::string s(raw);
    const auto pos = s.find(':');
    return pos == std::string::npos ? s : s.substr(pos + 1);
}

pugi::xml_node childLocal(pugi::xml_node n, const char* name) {
    for (auto c : n.children())
        if (c.type() == pugi::node_element && localName(c.name()) == name) return c;
    return {};
}

pugi::xml_node firstElementChild(pugi::xml_node n) {
    for (auto c : n.children()) if (c.type() == pugi::node_element) return c;
    return {};
}

pugi::xml_node firstCoreChild(pugi::xml_node n) {
    // QIF geometry wrappers inherit NodeWithIdBaseType, so an optional
    // <Attributes> element can legally appear before the geometry core.
    // Never assume the first element child is the Core.
    for (auto c : n.children()) {
        if (c.type() != pugi::node_element) continue;
        const std::string name = localName(c.name());
        if (name.size() >= 4 && name.compare(name.size() - 4, 4, "Core") == 0) return c;
    }
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

std::vector<std::int64_t> parseIntegers(const char* text) {
    std::vector<std::int64_t> out;
    if (!text) return out;
    std::istringstream ss(text);
    std::int64_t v = 0;
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

bool attrBool(pugi::xml_node n, const char* name, bool fallback = false) {
    auto a = n.attribute(name);
    return a ? a.as_bool(fallback) : fallback;
}

std::pair<double,double> domainOf(pugi::xml_node core, double d0 = 0.0, double d1 = 1.0) {
    const auto d = parseNumbers(core.attribute("domain").value());
    return d.size() >= 2 ? std::make_pair(d[0], d[1]) : std::make_pair(d0, d1);
}

std::string refId(pugi::xml_node parent, const char* childName) {
    auto r = childLocal(parent, childName);
    auto i = childLocal(r, "Id");
    return i ? std::string(i.child_value()) : std::string{};
}

std::vector<std::string> refIds(pugi::xml_node parent, const char* childName) {
    std::vector<std::string> out;
    auto a = childLocal(parent, childName);
    if (!a) return out;
    for (auto i : a.children()) {
        if (i.type() == pugi::node_element && localName(i.name()) == "Id") out.emplace_back(i.child_value());
    }
    return out;
}

std::string trimWhitespace(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

GenericElement retainGenericElement(pugi::xml_node n) {
    GenericElement out;
    out.name = localName(n.name());
    for (auto a : n.attributes()) {
        out.attributes.push_back({a.name(), a.value()});
    }
    out.text = trimWhitespace(n.child_value());
    for (auto c : n.children()) {
        if (c.type() == pugi::node_element) out.children.push_back(retainGenericElement(c));
    }
    return out;
}

Color parseColorText(const char* text, Color fallback = {}) {
    const auto v = parseNumbers(text);
    if (v.size() < 3) return fallback;
    auto clampByte = [](double x) -> std::uint8_t {
        return static_cast<std::uint8_t>(std::clamp(std::lround(x), 0L, 255L));
    };
    return {clampByte(v[0]), clampByte(v[1]), clampByte(v[2]), fallback.a};
}

struct Style {
    Color color{};
    bool hasColor = false;
    bool hidden = false;
    double transparency = 0.0;
};

Style applyStyle(Style parent, pugi::xml_node n) {
    if (auto a = n.attribute("color")) {
        parent.color = parseColorText(a.value(), parent.color);
        parent.hasColor = true;
    }
    if (auto a = n.attribute("transparency")) parent.transparency = std::clamp(a.as_double(0.0), 0.0, 1.0);
    if (attrBool(n, "hidden", false)) parent.hidden = true;
    parent.color.a = static_cast<std::uint8_t>(std::clamp(std::lround((1.0 - parent.transparency) * 255.0), 0L, 255L));
    return parent;
}

// RFC 2045 base64 decoder. Whitespace is ignored, as permitted in XML base64Binary.
std::vector<std::uint8_t> decodeBase64(const char* text) {
    static std::array<int,256> table = [] {
        std::array<int,256> t{};
        t.fill(-1);
        const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; alphabet[i]; ++i) t[static_cast<unsigned char>(alphabet[i])] = i;
        return t;
    }();
    std::vector<std::uint8_t> out;
    int val = 0, bits = -8;
    for (const unsigned char c : std::string(text ? text : "")) {
        if (c == '=') break;
        const int d = table[c];
        if (d < 0) continue;
        val = (val << 6) | d;
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<std::uint8_t>((val >> bits) & 0xff));
            bits -= 8;
        }
    }
    return out;
}

std::uint32_t readU32LE(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

std::int32_t readI32LE(const std::uint8_t* p) {
    return static_cast<std::int32_t>(readU32LE(p));
}

double readF64LE(const std::uint8_t* p) {
    std::uint64_t u = 0;
    for (int i = 0; i < 8; ++i) u |= std::uint64_t(p[i]) << (8 * i);
    double d = 0.0;
    static_assert(sizeof(double) == sizeof(std::uint64_t), "QIF binary64 requires an 8-byte double");
    std::memcpy(&d, &u, sizeof(d));
    return d;
}

struct Context;

std::vector<double> arrayDoubles(pugi::xml_node parent, const char* textName, const char* binaryName, Context* ctx = nullptr);
std::vector<std::int64_t> arrayInts(pugi::xml_node parent, const char* textName, const char* binaryName, Context* ctx = nullptr);
std::vector<std::uint8_t> arrayBytes(pugi::xml_node parent, const char* textName, const char* binaryName, Context* ctx = nullptr);

std::vector<double> basisAll(double t, int order, const std::vector<double>& knots, std::size_t ncp) {
    if (order <= 0 || ncp == 0 || knots.size() < ncp + static_cast<std::size_t>(order)) return {};
    const int degree = order - 1;
    std::vector<double> prev(ncp, 0.0), next(ncp, 0.0);
    const double lo = knots[static_cast<std::size_t>(degree)];
    const double hi = knots[ncp];
    t = std::clamp(t, lo, hi);
    for (std::size_t i = 0; i < ncp; ++i) {
        const bool inSpan = knots[i] <= t && t < knots[i + 1];
        const bool atEnd = std::abs(t - hi) <= 1e-12 * std::max(1.0, std::abs(hi)) && i + 1 == ncp;
        prev[i] = (inSpan || atEnd) ? 1.0 : 0.0;
    }
    for (int k = 1; k <= degree; ++k) {
        std::fill(next.begin(), next.end(), 0.0);
        for (std::size_t i = 0; i < ncp; ++i) {
            double a = 0.0, b = 0.0;
            const double d1 = knots[i + static_cast<std::size_t>(k)] - knots[i];
            if (std::abs(d1) > kTiny) a = (t - knots[i]) / d1 * prev[i];
            if (i + 1 < ncp) {
                const double d2 = knots[i + static_cast<std::size_t>(k) + 1] - knots[i + 1];
                if (std::abs(d2) > kTiny) b = (knots[i + static_cast<std::size_t>(k) + 1] - t) / d2 * prev[i + 1];
            }
            next[i] = a + b;
        }
        prev.swap(next);
    }
    return prev;
}

struct Curve2 {
    std::string type;
    double t0 = 0.0, t1 = 1.0;
    std::function<Vec2(double)> fn;
    bool valid() const { return static_cast<bool>(fn); }
    Vec2 eval(double t) const { return fn ? fn(t) : Vec2{}; }
};

struct Curve3 {
    std::string type;
    double t0 = 0.0, t1 = 1.0;
    std::function<Vec3(double)> fn;
    bool valid() const { return static_cast<bool>(fn); }
    Vec3 eval(double t) const { return fn ? fn(t) : Vec3{}; }
};

int findPolynomialSpan(double t, const std::vector<double>& knots) {
    if (knots.size() < 2) return -1;
    if (t <= knots.front()) return 0;
    if (t >= knots.back()) return static_cast<int>(knots.size()) - 2;
    auto it = std::upper_bound(knots.begin(), knots.end(), t);
    int i = static_cast<int>(std::distance(knots.begin(), it)) - 1;
    return std::clamp(i, 0, static_cast<int>(knots.size()) - 2);
}

Vec2 evalConic2(const std::string& form, double A, double B, double t, Vec2 center, Vec2 dirX, Vec2 dirY) {
    double x = 0.0, y = 0.0;
    if (form == "ELLIPSE") { x = A * std::cos(t); y = B * std::sin(t); }
    else if (form == "PARABOLA") { x = A * t; y = B * t * t; }
    else if (form == "HYPERBOLA") { x = A * std::sqrt(1.0 + (t * t) / std::max(B * B, kTiny)); y = t; }
    return center + dirX * x + dirY * y;
}

Vec3 evalConic3(const std::string& form, double A, double B, double t, Vec3 center, Vec3 dirX, Vec3 dirY) {
    double x = 0.0, y = 0.0;
    if (form == "ELLIPSE") { x = A * std::cos(t); y = B * std::sin(t); }
    else if (form == "PARABOLA") { x = A * t; y = B * t * t; }
    else if (form == "HYPERBOLA") { x = A * std::sqrt(1.0 + (t * t) / std::max(B * B, kTiny)); y = t; }
    return center + dirX * x + dirY * y;
}

Curve2 parseCurve2Core(pugi::xml_node core, Context& ctx);
Curve3 parseCurve3Core(pugi::xml_node core, Context& ctx);

struct AnnotationViewFrame {
    Vec3 normal{0.0, 0.0, 1.0};
    Vec3 direction{1.0, 0.0, 0.0};
};

struct Context {
    const LoadOptions* options = nullptr;
    QifMesh* out = nullptr;
    std::unordered_map<std::string, pugi::xml_node> ids;
    std::unordered_map<std::string, Transform3> transforms;
    std::unordered_map<std::string, AnnotationViewFrame> annotationViews;
    // QIF angle values may name any unit declared in FileUnits. Values here
    // convert the declared unit to radians using S=(X+offset)*factor.
    std::unordered_map<std::string, std::pair<double,double>> angularUnits;
    std::string primaryAngularUnit;

    void warn(const std::string& w) {
        if (out && out->diagnostics.warnings.size() < 256) out->diagnostics.warnings.push_back(w);
    }
};

void countBinary(Context* ctx) {
    if (ctx && ctx->out) ++ctx->out->diagnostics.binaryArrayCount;
}

std::vector<double> arrayDoubles(pugi::xml_node parent, const char* textName, const char* binaryName, Context* ctx) {
    if (auto n = childLocal(parent, textName)) return parseNumbers(n.child_value());
    auto b = childLocal(parent, binaryName);
    if (!b) return {};
    countBinary(ctx);
    const auto bytes = decodeBase64(b.child_value());
    std::vector<double> out;
    if (bytes.size() % 8 != 0) {
        if (ctx) ctx->warn(std::string(binaryName) + " byte count is not a multiple of 8.");
        return out;
    }
    out.reserve(bytes.size() / 8);
    for (std::size_t i = 0; i + 7 < bytes.size(); i += 8) out.push_back(readF64LE(bytes.data() + i));
    return out;
}

std::vector<std::int64_t> arrayInts(pugi::xml_node parent, const char* textName, const char* binaryName, Context* ctx) {
    if (auto n = childLocal(parent, textName)) return parseIntegers(n.child_value());
    auto b = childLocal(parent, binaryName);
    if (!b) return {};
    countBinary(ctx);
    const auto bytes = decodeBase64(b.child_value());
    std::vector<std::int64_t> out;
    if (bytes.size() % 4 != 0) {
        if (ctx) ctx->warn(std::string(binaryName) + " byte count is not a multiple of 4.");
        return out;
    }
    out.reserve(bytes.size() / 4);
    for (std::size_t i = 0; i + 3 < bytes.size(); i += 4) out.push_back(readI32LE(bytes.data() + i));
    return out;
}

std::vector<std::uint8_t> arrayBytes(pugi::xml_node parent, const char* textName, const char* binaryName, Context* ctx) {
    if (auto n = childLocal(parent, textName)) {
        const auto v = parseIntegers(n.child_value());
        std::vector<std::uint8_t> out;
        out.reserve(v.size());
        for (auto x : v) out.push_back(static_cast<std::uint8_t>(std::clamp<std::int64_t>(x, 0, 255)));
        return out;
    }
    auto b = childLocal(parent, binaryName);
    if (!b) return {};
    countBinary(ctx);
    return decodeBase64(b.child_value());
}

std::vector<Vec2> arrayVec2s(pugi::xml_node parent, const char* textName, const char* binaryName, Context* ctx) {
    const auto raw = arrayDoubles(parent, textName, binaryName, ctx);
    std::vector<Vec2> out;
    out.reserve(raw.size() / 2);
    for (std::size_t i = 0; i + 1 < raw.size(); i += 2) out.push_back({raw[i], raw[i + 1]});
    return out;
}

Color pmiDefaultColor() { return {245, 245, 245, 255}; }

Color parseColorNode(pugi::xml_node parent, const char* childName, Color fallback = pmiDefaultColor()) {
    if (auto c = childLocal(parent, childName)) return parseColorText(c.child_value(), fallback);
    return fallback;
}

Vec3 orthonormalPerp(Vec3 n) {
    n = normalized(n);
    return std::abs(n.z) < 0.9 ? normalized(cross({0.0, 0.0, 1.0}, n))
                               : normalized(cross({0.0, 1.0, 0.0}, n));
}

struct PMIPlane {
    Vec3 origin{};
    Vec3 right{1.0, 0.0, 0.0};
    Vec3 up{0.0, 1.0, 0.0};
    Vec3 normal{0.0, 0.0, 1.0};
    bool valid = false;
};

Vec3 planePoint(const PMIPlane& plane, Vec2 xy, double z = 0.0) {
    return plane.origin + plane.right * xy.x + plane.up * xy.y + plane.normal * z;
}

void updateBounds(QifMesh& out, const Vec3& p) {
    out.bounds.add(p);
}

void addAnnotationPolyline(Context& ctx, const std::vector<Vec3>& pts, Color color, const std::string& sourceId) {
    if (!ctx.out || pts.size() < 2) return;
    AnnotationPolyline poly;
    poly.points = pts;
    poly.color = color;
    poly.sourceId = sourceId;
    for (const auto& p : poly.points) updateBounds(*ctx.out, p);
    ctx.out->annotationPolylines.push_back(std::move(poly));
    ++ctx.out->diagnostics.annotationPolylineCount;
}

void addAnnotationText(Context& ctx,
                       const std::string& text,
                       Vec3 origin,
                       Vec3 right,
                       Vec3 up,
                       double lineHeight,
                       Color color,
                       const std::string& sourceId) {
    if (!ctx.out || text.empty()) return;
    AnnotationText t;
    t.text = text;
    t.origin = origin;
    t.right = normalized(right);
    t.up = normalized(up);
    t.lineHeight = std::max(lineHeight, 1e-6);
    t.color = color;
    t.sourceId = sourceId;
    updateBounds(*ctx.out, origin);
    ctx.out->annotationTexts.push_back(std::move(t));
    ++ctx.out->diagnostics.annotationTextCount;
}

void addArrowHead(Context& ctx,
                  const PMIPlane& plane,
                  Vec2 from,
                  Vec2 tip,
                  double headHeight,
                  const std::string& headForm,
                  Color color,
                  const std::string& sourceId) {
    const Vec2 dir2 = tip - from;
    const double len = std::hypot(dir2.x, dir2.y);
    if (len <= 1e-12) return;
    const Vec2 dir = dir2 / len;
    const Vec2 left{-dir.y, dir.x};
    const double size = std::max(std::abs(headHeight), 1e-6);
    const Vec2 base = tip - dir * size;
    const Vec2 p1 = base + left * (size * 0.45);
    const Vec2 p2 = base - left * (size * 0.45);
    const std::string form = headForm.empty() ? "ARROW_OPEN" : headForm;
    if (form.find("CIRCLE") != std::string::npos || form.find("DOT") != std::string::npos) {
        std::vector<Vec3> ring;
        const int steps = 20;
        ring.reserve(static_cast<std::size_t>(steps) + 1);
        const double radius = size * 0.35;
        for (int i = 0; i <= steps; ++i) {
            const double a = (2.0 * kPi * i) / steps;
            ring.push_back(planePoint(plane, tip + Vec2{std::cos(a) * radius, std::sin(a) * radius}));
        }
        addAnnotationPolyline(ctx, ring, color, sourceId);
        return;
    }
    addAnnotationPolyline(ctx, {planePoint(plane, p1), planePoint(plane, tip), planePoint(plane, p2)}, color, sourceId);
    if (form.find("FILLED") != std::string::npos) {
        addAnnotationPolyline(ctx, {planePoint(plane, p1), planePoint(plane, p2)}, color, sourceId);
    }
}

std::string replaceAll(std::string text, const std::string& from, const std::string& to) {
    std::size_t pos = 0;
    while (!from.empty() && (pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::string decodePmiText(std::string text) {
    static const std::pair<const char*, const char*> replacements[] = {
        {"{DIAMETER}", "DIA "},
        {"{RADIUS}", "R "},
        {"{PLUS_MINUS}", "+/-"},
        {"{DEGREE}", " DEG"},
        {"{POSITION}", "POSITION "},
        {"{PERPENDICULARITY}", "PERP "},
        {"{PARALLELISM}", "PARALLEL "},
        {"{FLATNESS}", "FLATNESS "},
        {"{STRAIGHTNESS}", "STRAIGHT "},
        {"{PROFILE_OF_A_LINE}", "PROFILE LINE "},
        {"{PROFILE_OF_A_SURFACE}", "PROFILE SURF "},
        {"{ANGULARITY}", "ANGULARITY "},
        {"{CONCENTRICITY}", "CONCENTRIC "},
        {"{SYMMETRY}", "SYMMETRY "},
        {"{DATUM}", "DATUM "},
        {"{MAXIMUM_MATERIAL_CONDITION}", "MMC"},
        {"{LEAST_MATERIAL_CONDITION}", "LMC"},
        {"{REGARDLESS_OF_FEATURE_SIZE}", "RFS"},
        {"{COUNTERSINK}", "CSK"},
        {"{COUNTERBORE}", "CBORE"},
        {"{DEPTH}", "DEPTH "},
        {"{UNEF}", "UNEF"},
        {"{LEFT_DOUBLE_QUOTE}", "\""},
        {"{RIGHT_DOUBLE_QUOTE}", "\""},
        {"{NEW_LINE}", "\n"},
    };
    for (const auto& r : replacements) text = replaceAll(text, r.first, r.second);
    return text;
}

PMIPlane resolvePMIPlane(pugi::xml_node planeNode, Context& ctx) {
    PMIPlane plane;
    if (!planeNode) return plane;
    const std::string annotationViewId = refId(planeNode, "AnnotationViewId");
    Vec3 normal{0.0, 0.0, 1.0};
    Vec3 right{1.0, 0.0, 0.0};
    if (!annotationViewId.empty()) {
        auto it = ctx.annotationViews.find(annotationViewId);
        if (it != ctx.annotationViews.end()) {
            normal = normalized(it->second.normal);
            right = normalized(it->second.direction);
        }
    }
    if (auto d = childLocal(planeNode, "Direction")) right = normalized(parseVec3(d.child_value()));
    if (length2(normal) < 1e-20) normal = {0.0, 0.0, 1.0};
    if (length2(right) < 1e-20) right = orthonormalPerp(normal);
    right = normalized(right - normal * dot(right, normal));
    if (length2(right) < 1e-20) right = orthonormalPerp(normal);
    Vec3 up = normalized(cross(normal, right));
    if (length2(up) < 1e-20) up = orthonormalPerp(right);
    plane.origin = parseVec3(nodeText(planeNode, "Origin").c_str());
    plane.right = right;
    plane.up = up;
    plane.normal = normal;
    plane.valid = true;
    return plane;
}

void add2DPolyline(Context& ctx,
                   const PMIPlane& plane,
                   const std::vector<Vec2>& pts,
                   Color color,
                   const std::string& sourceId,
                   bool closed = false) {
    if (!plane.valid || pts.size() < 2) return;
    std::vector<Vec3> poly;
    poly.reserve(pts.size() + (closed ? 1 : 0));
    for (auto p : pts) poly.push_back(planePoint(plane, p));
    if (closed) poly.push_back(poly.front());
    addAnnotationPolyline(ctx, poly, color, sourceId);
}

bool parseLineSegment2d(pugi::xml_node n, Vec2& a, Vec2& b) {
    if (auto s = childLocal(n, "StartPoint")) a = parseVec2(s.child_value()); else return false;
    if (auto e = childLocal(n, "EndPoint")) b = parseVec2(e.child_value()); else return false;
    return true;
}

void parseWitnessLines(Context& ctx, pugi::xml_node witnessNode, const PMIPlane& plane, Color color, const std::string& sourceId) {
    if (!witnessNode || !plane.valid) return;
    if (auto s1 = childLocal(witnessNode, "Segment1")) {
        Vec2 a{}, b{};
        if (parseLineSegment2d(s1, a, b)) add2DPolyline(ctx, plane, {a, b}, color, sourceId, false);
        if (auto s2 = childLocal(witnessNode, "Segment2")) {
            if (parseLineSegment2d(s2, a, b)) add2DPolyline(ctx, plane, {a, b}, color, sourceId, false);
        }
        return;
    }
    if (auto bp = childLocal(witnessNode, "BeginPoint")) {
        const Vec2 begin = parseVec2(bp.child_value());
        const Vec2 end = parseVec2(nodeText(witnessNode, "EndPoint").c_str());
        const Vec2 center = parseVec2(nodeText(witnessNode, "CircleCenter").c_str());
        const double radius = nodeDouble(witnessNode, "CircleRadius", 0.0);
        if (radius <= 0.0) return;
        double a0 = std::atan2(begin.y - center.y, begin.x - center.x);
        double a1 = std::atan2(end.y - center.y, end.x - center.x);
        double da = a1 - a0;
        while (da <= -kPi) da += 2.0 * kPi;
        while (da > kPi) da -= 2.0 * kPi;
        const int steps = std::max(8, static_cast<int>(std::ceil(std::abs(da) * 12.0 / kPi)));
        std::vector<Vec2> pts;
        pts.reserve(static_cast<std::size_t>(steps) + 1);
        for (int i = 0; i <= steps; ++i) {
            const double a = a0 + da * (static_cast<double>(i) / steps);
            pts.push_back({center.x + radius * std::cos(a), center.y + radius * std::sin(a)});
        }
        add2DPolyline(ctx, plane, pts, color, sourceId, false);
    }
}

void parseFrame(Context& ctx, pugi::xml_node frame, const PMIPlane& plane, Color color, const std::string& sourceId) {
    const std::string type = localName(frame.name());
    if (type == "FrameRectangular" || type == "FrameFlag") {
        const Vec2 xy = parseVec2(nodeText(frame, "XY").c_str());
        const double w = nodeDouble(frame, "Width", 0.0);
        const double h = nodeDouble(frame, "Height", 0.0);
        std::vector<Vec2> pts{{xy.x,xy.y},{xy.x+w,xy.y},{xy.x+w,xy.y+h},{xy.x,xy.y+h}};
        add2DPolyline(ctx, plane, pts, color, sourceId, true);
        if (type == "FrameFlag") {
            const bool right = !frame.attribute("right") || frame.attribute("right").as_bool(true);
            if (right) add2DPolyline(ctx, plane, {{xy.x+w,xy.y},{xy.x+w+h*0.6,xy.y+h*0.5},{xy.x+w,xy.y+h}}, color, sourceId, false);
            else add2DPolyline(ctx, plane, {{xy.x,xy.y},{xy.x-h*0.6,xy.y+h*0.5},{xy.x,xy.y+h}}, color, sourceId, false);
        }
    } else if (type == "FrameCircular") {
        const Vec2 c = parseVec2(nodeText(frame, "XY").c_str());
        const double r = nodeDouble(frame, "Radius", 0.0);
        if (r <= 0.0) return;
        std::vector<Vec2> pts;
        const int steps = 32;
        for (int i=0;i<=steps;++i){const double a=(2.0*kPi*i)/steps;pts.push_back({c.x+r*std::cos(a), c.y+r*std::sin(a)});}        
        add2DPolyline(ctx, plane, pts, color, sourceId, false);
        const bool crossed = !frame.attribute("crossed") || frame.attribute("crossed").as_bool(true);
        if (crossed) add2DPolyline(ctx, plane, {{c.x-r,c.y},{c.x+r,c.y}}, color, sourceId, false);
    } else if (type == "FrameIrregularForm") {
        add2DPolyline(ctx, plane, arrayVec2s(frame, "Points", "PointsBinary", &ctx), color, sourceId, true);
    } else if (type == "FrameTriangle" || type == "FramePentagonal" || type == "FrameHexagonal" || type == "FrameOctagonal") {
        std::vector<Vec2> pts;
        for (auto p : frame.children()) if (p.type()==pugi::node_element && localName(p.name())=="Point") pts.push_back(parseVec2(p.child_value()));
        add2DPolyline(ctx, plane, pts, color, sourceId, true);
    } else if (type == "FrameWeldSymbol") {
        Vec2 a = parseVec2(nodeText(frame, "ReferenceLineBeginPoint").c_str());
        Vec2 b = parseVec2(nodeText(frame, "ReferenceLineEndPoint").c_str());
        add2DPolyline(ctx, plane, {a,b}, color, sourceId, false);
        if (auto tail = childLocal(frame, "Tail")) {
            const Vec2 u = parseVec2(nodeText(tail, "UpperPoint").c_str());
            const Vec2 l = parseVec2(nodeText(tail, "LowerPoint").c_str());
            add2DPolyline(ctx, plane, {b,u}, color, sourceId, false);
            add2DPolyline(ctx, plane, {b,l}, color, sourceId, false);
        }
    }
}

void parseGraphics(Context& ctx, pugi::xml_node graphicsNode, const PMIPlane& plane, const std::string& sourceId) {
    if (!graphicsNode || !plane.valid) return;
    if (auto polylines = childLocal(graphicsNode, "Polylines")) {
        for (auto pl : polylines.children()) {
            if (pl.type() != pugi::node_element || localName(pl.name()) != "Polyline") continue;
            const Color c = pl.attribute("color") ? parseColorText(pl.attribute("color").value(), pmiDefaultColor()) : pmiDefaultColor();
            add2DPolyline(ctx, plane, arrayVec2s(pl, "Points", "PointsBinary", &ctx), c, sourceId, false);
        }
    }
    if (auto areas = childLocal(graphicsNode, "Areas")) {
        for (auto area : areas.children()) {
            if (area.type() != pugi::node_element || localName(area.name()) != "Area") continue;
            if (auto loops = childLocal(area, "Loops")) {
                for (auto loop : loops.children()) {
                    if (loop.type() != pugi::node_element || localName(loop.name()) != "Loop") continue;
                    add2DPolyline(ctx, plane, arrayVec2s(loop, "Points", "PointsBinary", &ctx), pmiDefaultColor(), sourceId, true);
                }
            } else if (auto tri = childLocal(area, "Triangulation")) {
                const auto verts = arrayVec2s(tri, "Vertices", "VerticesBinary", &ctx);
                const auto idx = arrayInts(tri, "VertexIndices", "VertexIndicesBinary", &ctx);
                for (std::size_t i = 0; i + 2 < idx.size(); i += 3) {
                    const auto ia = static_cast<std::size_t>(idx[i]);
                    const auto ib = static_cast<std::size_t>(idx[i+1]);
                    const auto ic = static_cast<std::size_t>(idx[i+2]);
                    if (ia < verts.size() && ib < verts.size() && ic < verts.size()) {
                        add2DPolyline(ctx, plane, {verts[ia], verts[ib], verts[ic]}, pmiDefaultColor(), sourceId, true);
                    }
                }
            }
        }
    }
}

void parsePMIDisplay(Context& ctx, pugi::xml_node n) {
    const PMIPlane plane = resolvePMIPlane(childLocal(n, "Plane"), ctx);
    const std::string sourceId = refId(n, "Reference");
    const Color color = parseColorNode(n, "Color", pmiDefaultColor());

    if (auto texts = childLocal(n, "Texts")) {
        const double lineHeight = texts.attribute("lineHeight") ? texts.attribute("lineHeight").as_double(1.0) : 1.0;
        for (auto t : texts.children()) {
            if (t.type() != pugi::node_element || localName(t.name()) != "Text") continue;
            if (!plane.valid) continue;
            const std::string data = decodePmiText(nodeText(t, "Data"));
            const Vec2 xy = parseVec2(nodeText(t, "XY").c_str());
            addAnnotationText(ctx, data, planePoint(plane, xy), plane.right, plane.up, lineHeight, color, sourceId);
        }
    }

    for (auto c : n.children()) {
        if (c.type() != pugi::node_element) continue;
        const std::string name = localName(c.name());
        if (name == "Leader" || name == "LeaderExtend") {
            if (!plane.valid) continue;
            Vec2 start{}, end{};
            if (!parseLineSegment2d(c, start, end)) continue;
            std::vector<Vec2> pts{start, end};
            if (name == "LeaderExtend") pts.insert(pts.begin() + 1, parseVec2(nodeText(c, "PointExtension").c_str()));
            add2DPolyline(ctx, plane, pts, color, sourceId, false);
            const Vec2 from = pts.size() >= 2 ? pts[pts.size() - 2] : start;
            addArrowHead(ctx, plane, from, pts.back(), nodeDouble(c, "HeadHeight", 1.0), nodeText(c, "HeadForm"), color, sourceId);
        }
    }

    parseWitnessLines(ctx, childLocal(n, "WitnessLines"), plane, color, sourceId);
    if (auto frames = childLocal(n, "Frames")) {
        for (auto frame : frames.children()) {
            if (frame.type() != pugi::node_element) continue;
            parseFrame(ctx, frame, plane, color, sourceId);
        }
    }
    parseGraphics(ctx, childLocal(n, "Graphics"), plane, sourceId);
}

void collectVisualization(pugi::xml_node n, Context& ctx) {
    if (n.type() == pugi::node_element) {
        const std::string name = localName(n.name());
        if (name == "PMIDisplay") parsePMIDisplay(ctx, n);
    }
    for (auto c : n.children()) collectVisualization(c, ctx);
}

Transform3 parseTransformInstance(pugi::xml_node n) {
    Transform3 t;
    if (auto r = childLocal(n, "Rotation")) {
        if (auto x = childLocal(r, "XDirection")) t.x = parseVec3(x.child_value());
        if (auto y = childLocal(r, "YDirection")) t.y = parseVec3(y.child_value());
        if (auto z = childLocal(r, "ZDirection")) t.z = parseVec3(z.child_value());
    }
    if (auto o = childLocal(n, "Origin")) t.origin = parseVec3(o.child_value());
    return t;
}

Transform3 referencedTransform(pugi::xml_node n, const Context& ctx) {
    const std::string id = refId(n, "Transform");
    if (id.empty()) return Transform3::identity();
    const auto it = ctx.transforms.find(id);
    return it == ctx.transforms.end() ? Transform3::identity() : it->second;
}

Curve2 parseCurve2Core(pugi::xml_node core, Context& ctx) {
    Curve2 c;
    c.type = localName(core.name());
    std::tie(c.t0, c.t1) = domainOf(core);

    if (c.type == "Segment12Core") {
        const Vec2 a = parseVec2(nodeText(core, "StartPoint").c_str());
        const Vec2 b = parseVec2(nodeText(core, "EndPoint").c_str());
        c.fn = [a,b](double t) { return a + (b-a) * t; };
        return c;
    }

    if (c.type == "Polyline12Core") {
        const auto raw = arrayDoubles(core, "Points", "PointsBinary", &ctx);
        std::vector<Vec2> pts;
        for (std::size_t i = 0; i + 1 < raw.size(); i += 2) pts.push_back({raw[i], raw[i+1]});
        if (pts.size() < 2) return {};
        c.fn = [pts = std::move(pts)](double t) {
            t = std::clamp(t, 0.0, static_cast<double>(pts.size()-1));
            std::size_t i = static_cast<std::size_t>(std::floor(t));
            if (i + 1 >= pts.size()) return pts.back();
            const double f = t - static_cast<double>(i);
            return pts[i] + (pts[i+1]-pts[i]) * f;
        };
        return c;
    }

    if (c.type == "ArcCircular12Core") {
        const double r = nodeDouble(core, "Radius");
        const Vec2 center = parseVec2(nodeText(core, "Center").c_str());
        Vec2 x = parseVec2(nodeText(core, "DirBeg").c_str());
        const double n = std::hypot(x.x, x.y);
        if (n > kTiny) x = x / n;
        const bool turned = attrBool(core, "turned", false);
        Vec2 y = turned ? Vec2{x.y, -x.x} : Vec2{-x.y, x.x};
        c.fn = [r,center,x,y](double t) { return center + x*(r*std::cos(t)) + y*(r*std::sin(t)); };
        return c;
    }

    if (c.type == "ArcConic12Core") {
        const double A = nodeDouble(core, "A"), B = nodeDouble(core, "B");
        const std::string form = core.attribute("form").value();
        const Vec2 center = parseVec2(nodeText(core, "Center").c_str());
        Vec2 x = parseVec2(nodeText(core, "DirBeg").c_str());
        const double n = std::hypot(x.x, x.y);
        if (n > kTiny) x = x / n;
        const bool turned = attrBool(core, "turned", false);
        Vec2 y = turned ? Vec2{x.y, -x.x} : Vec2{-x.y, x.x};
        c.fn = [form,A,B,center,x,y](double t) { return evalConic2(form,A,B,t,center,x,y); };
        return c;
    }

    if (c.type == "Nurbs12Core") {
        const int order = nodeInt(core, "Order");
        const auto knots = arrayDoubles(core, "Knots", "KnotsBinary", &ctx);
        const auto raw = arrayDoubles(core, "CPs", "CPsBinary", &ctx);
        std::vector<Vec2> cps;
        for (std::size_t i = 0; i + 1 < raw.size(); i += 2) cps.push_back({raw[i], raw[i+1]});
        auto weights = arrayDoubles(core, "Weights", "WeightsBinary", &ctx);
        if (weights.empty()) weights.assign(cps.size(), 1.0);
        if (order <= 0 || cps.empty() || knots.size() < cps.size() + static_cast<std::size_t>(order)) return {};
        c.fn = [order,knots,cps,weights](double t) {
            const auto b = basisAll(t, order, knots, cps.size());
            Vec2 num{}; double den = 0.0;
            for (std::size_t i=0;i<cps.size() && i<b.size();++i) {
                const double q = b[i] * (i < weights.size() ? weights[i] : 1.0);
                num += cps[i] * q; den += q;
            }
            return std::abs(den)>kTiny ? num/den : Vec2{};
        };
        return c;
    }

    if (c.type == "Spline12Core") {
        const auto knots = arrayDoubles(core, "Knots", "KnotsBinary", &ctx);
        const auto ordersRaw = arrayInts(core, "Orders", "OrdersBinary", &ctx);
        const auto raw = arrayDoubles(core, "Coefficients", "CoefficientsBinary", &ctx);
        std::vector<int> orders; for (auto v : ordersRaw) orders.push_back(static_cast<int>(v));
        std::vector<Vec2> coeff;
        for (std::size_t i=0;i+1<raw.size();i+=2) coeff.push_back({raw[i],raw[i+1]});
        const bool normalizedFlag = attrBool(core, "normalized", false);
        if (knots.size()<2 || orders.size()+1!=knots.size()) return {};
        std::vector<std::size_t> offsets(orders.size()+1,0);
        for (std::size_t i=0;i<orders.size();++i) offsets[i+1]=offsets[i]+static_cast<std::size_t>(std::max(0,orders[i]));
        if (offsets.back()>coeff.size()) return {};
        c.fn = [knots,orders,coeff,offsets,normalizedFlag](double t) {
            const int p = findPolynomialSpan(t,knots);
            if (p<0 || static_cast<std::size_t>(p)>=orders.size()) return Vec2{};
            const double span = knots[p+1]-knots[p];
            const double u = normalizedFlag && std::abs(span)>kTiny ? (t-knots[p])/span : (t-knots[p]);
            Vec2 r{}; double powu=1.0;
            for (int i=0;i<orders[p];++i) { r += coeff[offsets[p]+static_cast<std::size_t>(i)]*powu; powu*=u; }
            return r;
        };
        return c;
    }

    if (c.type == "Aggregate12Core") {
        auto subs = childLocal(core, "SubCurves");
        if (!subs) return {};
        struct S { Curve2 c; bool turned=false; double len=0; };
        std::vector<S> sv;
        double total=0.0;
        for (auto s : subs.children()) {
            if (s.type()!=pugi::node_element || localName(s.name())!="SubCurve") continue;
            auto sc = firstElementChild(s);
            Curve2 sub = parseCurve2Core(sc,ctx);
            if (!sub.valid()) continue;
            const double len = std::abs(sub.t1-sub.t0);
            sv.push_back({sub,attrBool(s,"turned",false),len}); total += len;
        }
        if (sv.empty()) return {};
        const double aggregateStart = c.t0;
        c.fn = [sv=std::move(sv),total,aggregateStart](double t) {
            double q = std::clamp(t-aggregateStart,0.0,total);
            for (const auto& s : sv) {
                if (q <= s.len || &s == &sv.back()) {
                    const double lt = s.turned ? s.c.t1-q : s.c.t0+q;
                    return s.c.eval(lt);
                }
                q -= s.len;
            }
            return sv.back().c.eval(sv.back().c.t1);
        };
        return c;
    }

    return {};
}

Curve3 parseCurve3Core(pugi::xml_node core, Context& ctx) {
    Curve3 c;
    c.type = localName(core.name());
    std::tie(c.t0,c.t1) = domainOf(core);

    if (c.type == "Segment13Core") {
        const Vec3 a=parseVec3(nodeText(core,"StartPoint").c_str()), b=parseVec3(nodeText(core,"EndPoint").c_str());
        c.fn=[a,b](double t){return a+(b-a)*t;}; return c;
    }
    if (c.type == "Polyline13Core") {
        const auto raw=arrayDoubles(core,"Points","PointsBinary",&ctx);
        std::vector<Vec3> pts; for(std::size_t i=0;i+2<raw.size();i+=3) pts.push_back({raw[i],raw[i+1],raw[i+2]});
        if(pts.size()<2)return {};
        c.fn=[pts=std::move(pts)](double t){
            t=std::clamp(t,0.0,static_cast<double>(pts.size()-1));
            std::size_t i=static_cast<std::size_t>(std::floor(t)); if(i+1>=pts.size())return pts.back();
            double f=t-static_cast<double>(i); return pts[i]+(pts[i+1]-pts[i])*f;
        }; return c;
    }
    if (c.type == "ArcCircular13Core") {
        const double r=nodeDouble(core,"Radius"); const Vec3 center=parseVec3(nodeText(core,"Center").c_str());
        const Vec3 x=normalized(parseVec3(nodeText(core,"DirBeg").c_str()));
        const Vec3 normal=normalized(parseVec3(nodeText(core,"Normal").c_str())); const Vec3 y=normalized(cross(normal,x));
        c.fn=[r,center,x,y](double t){return center+x*(r*std::cos(t))+y*(r*std::sin(t));}; return c;
    }
    if (c.type == "ArcConic13Core") {
        const double A=nodeDouble(core,"A"),B=nodeDouble(core,"B"); const std::string form=core.attribute("form").value();
        const Vec3 center=parseVec3(nodeText(core,"Center").c_str()); const Vec3 x=normalized(parseVec3(nodeText(core,"DirBeg").c_str()));
        const Vec3 normal=normalized(parseVec3(nodeText(core,"Normal").c_str())); const Vec3 y=normalized(cross(normal,x));
        c.fn=[form,A,B,center,x,y](double t){return evalConic3(form,A,B,t,center,x,y);}; return c;
    }
    if (c.type == "Nurbs13Core") {
        const int order=nodeInt(core,"Order"); const auto knots=arrayDoubles(core,"Knots","KnotsBinary",&ctx);
        const auto raw=arrayDoubles(core,"CPs","CPsBinary",&ctx); std::vector<Vec3> cps;
        for(std::size_t i=0;i+2<raw.size();i+=3)cps.push_back({raw[i],raw[i+1],raw[i+2]});
        auto weights=arrayDoubles(core,"Weights","WeightsBinary",&ctx); if(weights.empty())weights.assign(cps.size(),1.0);
        if(order<=0||cps.empty()||knots.size()<cps.size()+static_cast<std::size_t>(order))return {};
        c.fn=[order,knots,cps,weights](double t){
            const auto b=basisAll(t,order,knots,cps.size()); Vec3 num{}; double den=0;
            for(std::size_t i=0;i<cps.size()&&i<b.size();++i){double q=b[i]*(i<weights.size()?weights[i]:1.0);num+=cps[i]*q;den+=q;}
            return std::abs(den)>kTiny?num/den:Vec3{};
        }; return c;
    }
    if (c.type == "Spline13Core") {
        const auto knots=arrayDoubles(core,"Knots","KnotsBinary",&ctx); const auto ordersRaw=arrayInts(core,"Orders","OrdersBinary",&ctx);
        const auto raw=arrayDoubles(core,"Coefficients","CoefficientsBinary",&ctx); std::vector<int> orders;
        for (auto v : ordersRaw) orders.push_back(static_cast<int>(v));
        std::vector<Vec3> coeff;
        for(std::size_t i=0;i+2<raw.size();i+=3)coeff.push_back({raw[i],raw[i+1],raw[i+2]});
        const bool normalizedFlag=attrBool(core,"normalized",false); if(knots.size()<2||orders.size()+1!=knots.size())return {};
        std::vector<std::size_t> offsets(orders.size()+1,0);for(std::size_t i=0;i<orders.size();++i)offsets[i+1]=offsets[i]+static_cast<std::size_t>(std::max(0,orders[i]));
        if(offsets.back()>coeff.size())return {};
        c.fn=[knots,orders,coeff,offsets,normalizedFlag](double t){
            int p=findPolynomialSpan(t,knots);if(p<0||static_cast<std::size_t>(p)>=orders.size())return Vec3{};
            double span=knots[p+1]-knots[p],u=normalizedFlag&&std::abs(span)>kTiny?(t-knots[p])/span:(t-knots[p]);
            Vec3 r{};double powu=1;for(int i=0;i<orders[p];++i){r+=coeff[offsets[p]+static_cast<std::size_t>(i)]*powu;powu*=u;}return r;
        };return c;
    }
    if (c.type == "Aggregate13Core") {
        auto subs=childLocal(core,"SubCurves");if(!subs)return {};
        struct S{Curve3 c;bool turned=false;double len=0;};std::vector<S> sv;double total=0;
        for(auto s:subs.children()){
            if (s.type()!=pugi::node_element || localName(s.name())!="SubCurve") continue;
            Curve3 sub=parseCurve3Core(firstElementChild(s),ctx);
            if(!sub.valid()) continue;
            double len=std::abs(sub.t1-sub.t0);sv.push_back({sub,attrBool(s,"turned",false),len});total+=len;
        }if(sv.empty())return {};
        double aggregateStart=c.t0;c.fn=[sv=std::move(sv),total,aggregateStart](double t){double q=std::clamp(t-aggregateStart,0.0,total);
            for(std::size_t k=0;k<sv.size();++k){const auto&s=sv[k];if(q<=s.len||k+1==sv.size()){double lt=s.turned?s.c.t1-q:s.c.t0+q;return s.c.eval(lt);}q-=s.len;}return sv.back().c.eval(sv.back().c.t1);};return c;
    }
    return {};
}

Curve2 parseCurve2Node(pugi::xml_node n, Context& ctx) {
    auto core = localName(n.name()).find("Core") != std::string::npos ? n : firstCoreChild(n);
    return core ? parseCurve2Core(core,ctx) : Curve2{};
}

Curve3 parseCurve3Node(pugi::xml_node n, Context& ctx) {
    const bool wrapper = localName(n.name()).find("Core") == std::string::npos;
    auto core = wrapper ? firstCoreChild(n) : n;
    Curve3 c = core ? parseCurve3Core(core,ctx) : Curve3{};
    if (wrapper && c.valid()) {
        const Transform3 tr = referencedTransform(n,ctx);
        const auto f = c.fn;
        c.fn = [f,tr](double t){return tr.applyPoint(f(t));};
    }
    return c;
}

std::vector<Vec2> sampleCurve2(const Curve2& c, int samplesPerSpan) {
    if(!c.valid())return {};
    int count=std::clamp(std::max(8,samplesPerSpan*std::max(1,static_cast<int>(std::ceil(std::abs(c.t1-c.t0))))),8,512);
    if(c.type=="Segment12Core")count=1;
    std::vector<Vec2> out;out.reserve(static_cast<std::size_t>(count+1));
    for(int i=0;i<=count;++i){double f=static_cast<double>(i)/count;out.push_back(c.eval(c.t0+(c.t1-c.t0)*f));}return out;
}

std::vector<Vec3> sampleCurve3(const Curve3& c, int samplesPerSpan) {
    if(!c.valid())return {};
    int count=std::clamp(std::max(8,samplesPerSpan*std::max(1,static_cast<int>(std::ceil(std::abs(c.t1-c.t0))))),8,512);
    if(c.type=="Segment13Core")count=1;
    std::vector<Vec3> out;out.reserve(static_cast<std::size_t>(count+1));
    for(int i=0;i<=count;++i){double f=static_cast<double>(i)/count;out.push_back(c.eval(c.t0+(c.t1-c.t0)*f));}return out;
}

struct Surface {
    std::string type;
    double u0=0.0,u1=1.0,v0=0.0,v1=1.0;
    bool curved=true;
    bool periodicU=false, periodicV=false;
    double periodU=0.0, periodV=0.0;
    std::function<Vec3(Vec2)> fn;
    std::function<bool(Vec3,Vec2&)> inverse;
    bool valid() const {return static_cast<bool>(fn);}
    Vec3 eval(Vec2 uv) const {return fn?fn(uv):Vec3{};}
};

inline double clampParam(double x, double a, double b) {
    if (a > b) std::swap(a,b);
    return std::clamp(x,a,b);
}

Vec2 clampSurfaceUV(const Surface& s, Vec2 q) {
    q.x = clampParam(q.x,s.u0,s.u1);
    q.y = clampParam(q.y,s.v0,s.v1);
    return q;
}

bool numericalSurfaceInverse(const Surface& s, Vec3 target, Vec2& uv, const Vec2* seed = nullptr) {
    if (!s.valid()) return false;
    Vec2 q{};
    if (seed) {
        q = clampSurfaceUV(s,*seed);
    } else {
        // Coarse global search for the first point of an edge. Subsequent
        // edge samples use the previous UV as a seed, so this cost is paid
        // only once per co-edge.
        constexpr int grid = 8;
        double best = std::numeric_limits<double>::infinity();
        for (int j=0;j<=grid;++j) {
            const double fv=static_cast<double>(j)/grid;
            const double v=s.v0+(s.v1-s.v0)*fv;
            for (int i=0;i<=grid;++i) {
                const double fu=static_cast<double>(i)/grid;
                const double u=s.u0+(s.u1-s.u0)*fu;
                const double d=length2(s.eval({u,v})-target);
                if (d<best) { best=d; q={u,v}; }
            }
        }
    }

    const double ur=std::max(std::abs(s.u1-s.u0),1.0);
    const double vr=std::max(std::abs(s.v1-s.v0),1.0);
    for (int iter=0;iter<16;++iter) {
        q=clampSurfaceUV(s,q);
        const Vec3 p=s.eval(q);
        const Vec3 r=target-p;
        const double hu=std::max(ur*1e-6,1e-8);
        const double hv=std::max(vr*1e-6,1e-8);
        const double ua=clampParam(q.x-hu,s.u0,s.u1), ub=clampParam(q.x+hu,s.u0,s.u1);
        const double va=clampParam(q.y-hv,s.v0,s.v1), vb=clampParam(q.y+hv,s.v0,s.v1);
        Vec3 du{},dv{};
        if (std::abs(ub-ua)>kTiny) du=(s.eval({ub,q.y})-s.eval({ua,q.y}))/(ub-ua);
        if (std::abs(vb-va)>kTiny) dv=(s.eval({q.x,vb})-s.eval({q.x,va}))/(vb-va);
        const double a=dot(du,du), b=dot(du,dv), c=dot(dv,dv);
        const double rhsU=dot(du,r), rhsV=dot(dv,r);
        const double det=a*c-b*b;
        if (std::abs(det)<1e-24) break;
        const double dU=(rhsU*c-rhsV*b)/det;
        const double dV=(rhsV*a-rhsU*b)/det;
        q.x+=dU; q.y+=dV;
        if (dU*dU+dV*dV<1e-22) break;
    }
    q=clampSurfaceUV(s,q);
    const double scale=std::max({length(s.eval({s.u0,s.v0})-s.eval({s.u1,s.v1})),1.0});
    const bool ok=distance(s.eval(q),target)<=std::max(scale*5e-5,1e-6);
    uv=q;
    return ok;
}

bool surfaceInverse(const Surface& s, Vec3 p, Vec2& uv, const Vec2* seed = nullptr) {
    if (s.inverse && s.inverse(p,uv)) return true;
    return numericalSurfaceInverse(s,p,uv,seed);
}

std::pair<double,double> angleRangeRadians(pugi::xml_node n, const Context& ctx, double a=0.0, double b=1.0) {
    if (!n) return {a,b};
    const auto v = parseNumbers(n.child_value());
    if (v.size() < 2) return {a,b};
    std::string unit = n.attribute("angularUnit").value();
    if (unit.empty()) unit = ctx.primaryAngularUnit;
    double factor = 1.0, offset = 0.0;
    if (!unit.empty()) {
        auto it = ctx.angularUnits.find(unit);
        if (it != ctx.angularUnits.end()) { factor = it->second.first; offset = it->second.second; }
        else if (unit == "degree" || unit == "degrees" || unit == "deg") factor = kPi / 180.0;
        else if (unit == "revolution" || unit == "turn") factor = 2.0 * kPi;
        // "radian" and unknown names default to factor 1. Unknown names are
        // reported by the optional schema/XSLT validation tools.
    }
    return {(v[0] + offset) * factor, (v[1] + offset) * factor};
}

Surface parseSurfaceCore(pugi::xml_node core, Context& ctx);

Surface parseSurfaceCore(pugi::xml_node core, Context& ctx) {
    Surface s; s.type=localName(core.name());

    if(s.type=="Plane23Core") {
        const Vec3 o=parseVec3(nodeText(core,"Origin").c_str()),du=parseVec3(nodeText(core,"DirU").c_str()),dv=parseVec3(nodeText(core,"DirV").c_str());
        auto ur=parseNumbers(core.attribute("domainU").value()), vr=parseNumbers(core.attribute("domainV").value());
        if(ur.size()>=2){s.u0=ur[0];s.u1=ur[1];} if(vr.size()>=2){s.v0=vr[0];s.v1=vr[1];}
        s.curved=false;s.fn=[o,du,dv](Vec2 q){return o+du*q.x+dv*q.y;};
        s.inverse=[o,du,dv](Vec3 p,Vec2& q){
            const Vec3 d=p-o;const double a=dot(du,du),b=dot(du,dv),c=dot(dv,dv),det=a*c-b*b;
            if(std::abs(det)<1e-24)return false;
            const double r0=dot(d,du),r1=dot(d,dv);
            q={(r0*c-r1*b)/det,(r1*a-r0*b)/det};return true;
        };return s;
    }

    if(s.type=="Cylinder23Core" || s.type=="Cone23Core") {
        auto axis=childLocal(core,"Axis"),sweep=childLocal(core,"Sweep");if(!axis||!sweep)return {};
        const Vec3 ap=parseVec3(nodeText(axis,"AxisPoint").c_str()),az=normalized(parseVec3(nodeText(axis,"Direction").c_str()));
        const Vec3 dx=normalized(parseVec3(nodeText(sweep,"DirBeg").c_str())),dy=normalized(cross(az,dx));
        const double su=core.attribute("scaleU").as_double(1.0),sv=core.attribute("scaleV").as_double(1.0);const bool turned=attrBool(core,"turnedV",false);
        auto ar=angleRangeRadians(childLocal(sweep,"DomainAngle"),ctx,0.0,2*kPi);s.u0=ar.first/std::max(su,kTiny);s.u1=ar.second/std::max(su,kTiny);
        const double L=nodeDouble(core,"Length");s.v0=0;s.v1=L/std::max(sv,kTiny);
        s.periodicU=true;s.periodU=(2*kPi)/std::max(std::abs(su),kTiny);
        if(s.type=="Cylinder23Core") {
            const double r=nodeDouble(core,"Diameter")*0.5;
            s.fn=[ap,az,dx,dy,su,sv,turned,L,r](Vec2 q){double u=q.x*su;double z=q.y*sv;if(turned)z=L-z;return ap+az*z+(dx*std::cos(u)+dy*std::sin(u))*r;};
            s.inverse=[ap,az,dx,dy,su,sv,turned,L](Vec3 p,Vec2& q){Vec3 d=p-ap;double z=dot(d,az);Vec3 rr=d-az*z;double a=std::atan2(dot(rr,dy),dot(rr,dx));double u=a/std::max(std::abs(su),kTiny);double v=(turned?(L-z):z)/std::max(std::abs(sv),kTiny);q={u,v};return true;};
        } else {
            const double rb=nodeDouble(core,"DiameterBottom")*0.5,rt=nodeDouble(core,"DiameterTop")*0.5;
            s.fn=[ap,az,dx,dy,su,sv,turned,L,rb,rt](Vec2 q){double u=q.x*su;double z=q.y*sv;if(turned)z=L-z;double f=std::abs(L)>kTiny?z/L:0;double r=rb+(rt-rb)*f;return ap+az*z+(dx*std::cos(u)+dy*std::sin(u))*r;};
            s.inverse=[ap,az,dx,dy,su,sv,turned,L](Vec3 p,Vec2& q){Vec3 d=p-ap;double z=dot(d,az);Vec3 rr=d-az*z;double a=std::atan2(dot(rr,dy),dot(rr,dx));double u=a/std::max(std::abs(su),kTiny);double v=(turned?(L-z):z)/std::max(std::abs(sv),kTiny);q={u,v};return true;};
        }
        return s;
    }

    if(s.type=="Sphere23Core") {
        auto sw=childLocal(core,"LatitudeLongitudeSweep");if(!sw)return {};
        const double r=nodeDouble(core,"Diameter")*0.5,su=core.attribute("scaleU").as_double(1.0),sv=core.attribute("scaleV").as_double(1.0);const bool turned=attrBool(core,"turnedV",false);
        const Vec3 center=parseVec3(nodeText(core,"Location").c_str()),mer=normalized(parseVec3(nodeText(sw,"DirMeridianPrime").c_str())),north=normalized(parseVec3(nodeText(sw,"DirNorthPole").c_str())),east=normalized(cross(north,mer));
        auto lat=angleRangeRadians(childLocal(sw,"DomainLatitude"),ctx,-kPi/2,kPi/2),lon=angleRangeRadians(childLocal(sw,"DomainLongitude"),ctx,0,2*kPi);
        s.u0=lon.first/std::max(su,kTiny);s.u1=lon.second/std::max(su,kTiny);s.v0=lat.first/std::max(sv,kTiny);s.v1=lat.second/std::max(sv,kTiny);s.periodicU=true;s.periodU=(2*kPi)/std::max(std::abs(su),kTiny);
        s.fn=[r,su,sv,turned,center,mer,north,east](Vec2 q){double lon=q.x*su,lat=q.y*sv;if(turned)lat=-lat;Vec3 radial=mer*std::cos(lon)+east*std::sin(lon);return center+(radial*std::cos(lat)+north*std::sin(lat))*r;};
        s.inverse=[su,sv,turned,center,mer,north,east](Vec3 p,Vec2& q){Vec3 d=normalized(p-center);double lat=std::asin(std::clamp(dot(d,north),-1.0,1.0));double lon=std::atan2(dot(d,east),dot(d,mer));if(turned)lat=-lat;q={lon/std::max(std::abs(su),kTiny),lat/std::max(std::abs(sv),kTiny)};return true;};return s;
    }

    if(s.type=="Torus23Core") {
        auto axis=childLocal(core,"Axis"),sw=childLocal(core,"LatitudeLongitudeSweep");if(!axis||!sw)return {};
        const double R=nodeDouble(core,"DiameterMajor")*0.5,r=nodeDouble(core,"DiameterMinor")*0.5,su=core.attribute("scaleU").as_double(1.0),sv=core.attribute("scaleV").as_double(1.0),off=core.attribute("offsetV").as_double(0.0);const bool turned=attrBool(core,"turnedV",false);
        const Vec3 center=parseVec3(nodeText(axis,"AxisPoint").c_str()),north=normalized(parseVec3(nodeText(axis,"Direction").c_str())),mer=normalized(parseVec3(nodeText(sw,"DirMeridianPrime").c_str())),east=normalized(cross(north,mer));
        auto lat=angleRangeRadians(childLocal(sw,"DomainLatitude"),ctx,-kPi,kPi),lon=angleRangeRadians(childLocal(sw,"DomainLongitude"),ctx,0,2*kPi);s.u0=lon.first/std::max(su,kTiny);s.u1=lon.second/std::max(su,kTiny);s.v0=lat.first/std::max(sv,kTiny);s.v1=lat.second/std::max(sv,kTiny);s.periodicU=true;s.periodU=(2*kPi)/std::max(std::abs(su),kTiny);s.periodicV=true;s.periodV=(2*kPi)/std::max(std::abs(sv),kTiny);
        s.fn=[R,r,su,sv,off,turned,center,north,mer,east](Vec2 q){double lon=q.x*su,lat=off+q.y*sv;if(turned)lat=off-q.y*sv;Vec3 radial=mer*std::cos(lon)+east*std::sin(lon);return center+radial*(R+r*std::cos(lat))+north*(r*std::sin(lat));};
        s.inverse=[R,su,sv,off,turned,center,north,mer,east](Vec3 p,Vec2& q){Vec3 d=p-center;double z=dot(d,north);Vec3 rp=d-north*z;double rho=length(rp);double lon=std::atan2(dot(rp,east),dot(rp,mer));double lat=std::atan2(z,rho-R);double v=(turned?(off-lat):(lat-off))/std::max(std::abs(sv),kTiny);q={lon/std::max(std::abs(su),kTiny),v};return true;};return s;
    }

    if(s.type=="Nurbs23Core") {
        const int ou=nodeInt(core,"OrderU"),ov=nodeInt(core,"OrderV");const auto ku=arrayDoubles(core,"KnotsU","KnotsUBinary",&ctx),kv=arrayDoubles(core,"KnotsV","KnotsVBinary",&ctx);
        if(ou<=0||ov<=0||ku.size()<static_cast<std::size_t>(ou)||kv.size()<static_cast<std::size_t>(ov))return {};
        const std::size_t nu=ku.size()-static_cast<std::size_t>(ou),nv=kv.size()-static_cast<std::size_t>(ov);const auto raw=arrayDoubles(core,"CPs","CPsBinary",&ctx);std::vector<Vec3> cps;
        for(std::size_t i=0;i+2<raw.size();i+=3) cps.push_back({raw[i],raw[i+1],raw[i+2]});
        auto w=arrayDoubles(core,"Weights","WeightsBinary",&ctx);
        if(w.empty()) w.assign(cps.size(),1.0);
        if(cps.size()!=nu*nv) return {};
        s.u0=ku[static_cast<std::size_t>(ou-1)];s.u1=ku[nu];s.v0=kv[static_cast<std::size_t>(ov-1)];s.v1=kv[nv];
        s.fn=[ou,ov,ku,kv,nu,nv,cps,w](Vec2 q){auto bu=basisAll(q.x,ou,ku,nu),bv=basisAll(q.y,ov,kv,nv);Vec3 num{};double den=0;for(std::size_t j=0;j<nv;++j)for(std::size_t i=0;i<nu;++i){std::size_t k=j*nu+i;double a=bu[i]*bv[j]*(k<w.size()?w[k]:1.0);num+=cps[k]*a;den+=a;}return std::abs(den)>kTiny?num/den:Vec3{};};return s;
    }

    if(s.type=="Spline23Core") {
        const auto ku=arrayDoubles(core,"KnotsU","KnotsUBinary",&ctx);
        const auto kv=arrayDoubles(core,"KnotsV","KnotsVBinary",&ctx);
        const auto ouraw=arrayInts(core,"OrdersU","OrdersUBinary",&ctx);
        const auto ovraw=arrayInts(core,"OrdersV","OrdersVBinary",&ctx);
        const auto raw=arrayDoubles(core,"Coefficients","CoefficientsBinary",&ctx);
        std::vector<int> ou,ov;for(auto x:ouraw)ou.push_back(static_cast<int>(x));for(auto x:ovraw)ov.push_back(static_cast<int>(x));std::vector<Vec3> coeff;for(std::size_t i=0;i+2<raw.size();i+=3)coeff.push_back({raw[i],raw[i+1],raw[i+2]});
        if(ku.size()<2||kv.size()<2||ou.size()+1!=ku.size()||ov.size()+1!=kv.size()) return {};
        const bool norm=attrBool(core,"normalized",false);const std::size_t pu=ou.size(),pv=ov.size();std::vector<std::size_t> offs(pu*pv+1,0);std::size_t k=0;
        for(std::size_t j=0;j<pv;++j) for(std::size_t i=0;i<pu;++i){offs[j*pu+i]=k;k+=static_cast<std::size_t>(std::max(0,ou[i])*std::max(0,ov[j]));}
        offs.back()=k;
        if(k>coeff.size()) return {};
        s.u0=ku.front();s.u1=ku.back();s.v0=kv.front();s.v1=kv.back();
        s.fn=[ku,kv,ou,ov,coeff,offs,pu,norm](Vec2 q){int ip=findPolynomialSpan(q.x,ku),jp=findPolynomialSpan(q.y,kv);if(ip<0||jp<0)return Vec3{};double du=ku[ip+1]-ku[ip],dv=kv[jp+1]-kv[jp];double u=norm&&std::abs(du)>kTiny?(q.x-ku[ip])/du:q.x-ku[ip],v=norm&&std::abs(dv)>kTiny?(q.y-kv[jp])/dv:q.y-kv[jp];Vec3 r{};std::size_t base=offs[static_cast<std::size_t>(jp)*pu+static_cast<std::size_t>(ip)];double pvpow=1;for(int j=0;j<ov[jp];++j){double pupow=1;for(int i=0;i<ou[ip];++i){r+=coeff[base+static_cast<std::size_t>(j*ou[ip]+i)]*(pupow*pvpow);pupow*=u;}pvpow*=v;}return r;};return s;
    }

    if(s.type=="Extrude23Core") {
        auto curveNode=childLocal(core,"Curve");auto ccore=firstElementChild(curveNode);Curve3 c=parseCurve3Core(ccore,ctx);if(!c.valid())return {};
        const Vec3 term=parseVec3(nodeText(core,"TerminationPoint").c_str()),start=c.eval(c.t0),d=term-start;s.u0=c.t0;s.u1=c.t1;s.v0=0;s.v1=1;
        s.fn=[c,d](Vec2 q){return c.eval(q.x)+d*q.y;};return s;
    }

    if(s.type=="Ruled23Core") {
        std::vector<Curve3> cs;for(auto n:core.children()){if(n.type()==pugi::node_element&&localName(n.name())=="Curve"){auto cc=firstElementChild(n);Curve3 c=parseCurve3Core(cc,ctx);if(c.valid())cs.push_back(c);}}
        if(cs.size()!=2) return {};
        const bool turn2=attrBool(core,"turnedSecondCurve",false);s.u0=0;s.u1=1;s.v0=0;s.v1=1;Curve3 a=cs[0],b=cs[1];
        s.fn=[a,b,turn2](Vec2 q){double ta=a.t0+(a.t1-a.t0)*q.x;double tb=turn2?b.t1-(b.t1-b.t0)*q.x:b.t0+(b.t1-b.t0)*q.x;return a.eval(ta)*(1-q.y)+b.eval(tb)*q.y;};return s;
    }

    if(s.type=="Revolution23Core") {
        auto axis=childLocal(core,"Axis"),g=childLocal(core,"Generatrix");auto gc=firstElementChild(g);Curve3 c=parseCurve3Core(gc,ctx);if(!axis||!c.valid())return {};
        const Vec3 ap=parseVec3(nodeText(axis,"AxisPoint").c_str()),az=normalized(parseVec3(nodeText(axis,"Direction").c_str()));auto av=parseNumbers(core.attribute("angle").value());auto ar=av.size()>=2?std::make_pair(av[0],av[1]):std::make_pair(0.0,2*kPi);s.u0=c.t0;s.u1=c.t1;s.v0=ar.first;s.v1=ar.second;
        s.fn=[c,ap,az](Vec2 q){Vec3 p=c.eval(q.x),proj=ap+az*dot(p-ap,az),rvec=p-proj;double rr=length(rvec);if(rr<kTiny)return proj;Vec3 x=rvec/rr,y=normalized(cross(az,x));return proj+x*(rr*std::cos(q.y))+y*(rr*std::sin(q.y));};return s;
    }

    if(s.type=="Offset23Core") {
        auto holder=childLocal(core,"Surface");auto sc=firstElementChild(holder);Surface base=parseSurfaceCore(sc,ctx);if(!base.valid())return {};const double d=nodeDouble(core,"Distance");s.u0=base.u0;s.u1=base.u1;s.v0=base.v0;s.v1=base.v1;
        s.fn=[base,d](Vec2 q){double eu=std::max(std::abs(base.u1-base.u0)*1e-6,1e-8),ev=std::max(std::abs(base.v1-base.v0)*1e-6,1e-8);Vec3 pu=base.eval({q.x+eu,q.y})-base.eval({q.x-eu,q.y}),pv=base.eval({q.x,q.y+ev})-base.eval({q.x,q.y-ev});Vec3 n=normalized(cross(pu,pv));return base.eval(q)+n*d;};return s;
    }

    return {};
}

Surface parseSurfaceNode(pugi::xml_node n, Context& ctx) {
    const bool wrapper=localName(n.name()).find("Core")==std::string::npos;auto core=wrapper?firstCoreChild(n):n;Surface s=core?parseSurfaceCore(core,ctx):Surface{};
    if(wrapper&&s.valid()){
        Transform3 tr=referencedTransform(n,ctx);auto f=s.fn;auto inv=s.inverse;
        s.fn=[f,tr](Vec2 q){return tr.applyPoint(f(q));};
        if(inv)s.inverse=[inv,tr](Vec3 p,Vec2& q){return inv(tr.inverseApplyPoint(p),q);};
    }
    return s;
}

struct MeshData {
    std::vector<Vec3> vertices;
    std::vector<std::array<std::uint32_t,3>> triangles;
    bool valid() const {return !vertices.empty()&&!triangles.empty();}
};

MeshData parseMesh(pugi::xml_node mesh, Context& ctx) {
    MeshData m;auto core=childLocal(mesh,"MeshTriangleCore");if(!core)core=firstCoreChild(mesh);if(!core)return m;
    const auto vr=arrayDoubles(core,"Vertices","VerticesBinary",&ctx);for(std::size_t i=0;i+2<vr.size();i+=3)m.vertices.push_back({vr[i],vr[i+1],vr[i+2]});
    const auto ti=arrayInts(core,"Triangles","TrianglesBinary",&ctx);for(std::size_t i=0;i+2<ti.size();i+=3){if(ti[i]<0||ti[i+1]<0||ti[i+2]<0)continue;std::uint32_t a=static_cast<std::uint32_t>(ti[i]),b=static_cast<std::uint32_t>(ti[i+1]),c=static_cast<std::uint32_t>(ti[i+2]);if(a<m.vertices.size()&&b<m.vertices.size()&&c<m.vertices.size())m.triangles.push_back({a,b,c});}
    return m;
}

void appendCurveSamples(std::vector<Vec2>& dst,std::vector<Vec2> src) {
    if(src.empty()) return;
    if(!dst.empty()){
        if(dist2(dst.back(),src.back())<dist2(dst.back(),src.front()))std::reverse(src.begin(),src.end());
        if(!src.empty())src.erase(src.begin());
    }dst.insert(dst.end(),src.begin(),src.end());
}

void cleanRing(std::vector<Vec2>& ring) {
    if(ring.size()<2) return;
    double scale=1;
    for(auto p:ring) scale=std::max(scale,std::max(std::abs(p.x),std::abs(p.y)));
    double dup2=sqr(scale*1e-11),close2=sqr(scale*1e-7);
    if(ring.size()>1&&dist2(ring.front(),ring.back())<=close2) ring.pop_back();
    std::vector<Vec2> clean;clean.reserve(ring.size());
    for(auto p:ring) if(clean.empty()||dist2(clean.back(),p)>dup2) clean.push_back(p);
    if(clean.size()>1&&dist2(clean.front(),clean.back())<=close2) clean.pop_back();
    ring.swap(clean);
}

struct RingData { std::vector<Vec2> uv; std::string form; };

void unwrapOne(double& value, double previous, bool periodic, double period) {
    if (!periodic || period <= kTiny) return;
    while (value - previous > 0.5 * period) value -= period;
    while (value - previous < -0.5 * period) value += period;
}

void appendSurfaceCurveSamples(std::vector<Vec2>& dst, std::vector<Vec2> src, const Surface& surface) {
    if (src.empty()) return;
    // Preserve QIF co-edge orientation.  Only move periodic coordinates by
    // whole periods so neighboring samples form a continuous UV path across
    // cylinder/sphere/torus seams.
    if (!dst.empty()) {
        unwrapOne(src.front().x, dst.back().x, surface.periodicU, surface.periodU);
        unwrapOne(src.front().y, dst.back().y, surface.periodicV, surface.periodV);
    }
    for (std::size_t i=1;i<src.size();++i) {
        unwrapOne(src[i].x, src[i-1].x, surface.periodicU, surface.periodU);
        unwrapOne(src[i].y, src[i-1].y, surface.periodicV, surface.periodV);
    }
    if (!dst.empty() && !src.empty()) {
        const double scale=std::max({1.0,std::abs(dst.back().x),std::abs(dst.back().y),std::abs(src.front().x),std::abs(src.front().y)});
        if (dist2(dst.back(),src.front()) <= sqr(scale*1e-9)) src.erase(src.begin());
    }
    dst.insert(dst.end(),src.begin(),src.end());
}

bool projectEdgeToSurfaceUV(pugi::xml_node coedge,const Surface& surface,Context& ctx,std::vector<Vec2>& uv) {
    auto eo=childLocal(coedge,"EdgeOriented");
    auto edgeIdNode=childLocal(eo,"Id");
    if(!edgeIdNode)return false;
    auto eit=ctx.ids.find(edgeIdNode.child_value());
    if(eit==ctx.ids.end()||localName(eit->second.name())!="Edge")return false;
    const std::string curveId=refId(eit->second,"Curve");
    auto cit=ctx.ids.find(curveId);
    if(curveId.empty()||cit==ctx.ids.end())return false;
    Curve3 c=parseCurve3Node(cit->second,ctx);
    if(!c.valid())return false;
    auto samples=sampleCurve3(c,std::max(6,ctx.options->curveSamplesPerSpan));
    if(attrBool(eo,"turned",false))std::reverse(samples.begin(),samples.end());
    if(samples.empty())return false;
    std::vector<Vec2> projected;projected.reserve(samples.size());
    Vec2 previous{};bool havePrevious=false;
    for(const auto&p:samples){
        Vec2 q{};const Vec2* seed=havePrevious?&previous:nullptr;
        if(!surfaceInverse(surface,p,q,seed))return false;
        if(havePrevious){unwrapOne(q.x,previous.x,surface.periodicU,surface.periodU);unwrapOne(q.y,previous.y,surface.periodicV,surface.periodV);}
        projected.push_back(q);previous=q;havePrevious=true;
    }
    uv=std::move(projected);return uv.size()>=2;
}

bool parseLoopRing(pugi::xml_node loop,const Surface& surface,Context& ctx,RingData& out) {
    out.form=loop.attribute("form").value();auto coedges=childLocal(loop,"CoEdges");if(!coedges)return false;
    for (auto ce : coedges.children()) {
        if (ce.type() != pugi::node_element || localName(ce.name()) != "CoEdge") continue;
        std::vector<Vec2> samples;
        const std::string cid = refId(ce, "Curve12");
        if (!cid.empty()) {
            auto it = ctx.ids.find(cid);
            if (it != ctx.ids.end()) {
                Curve2 c = parseCurve2Node(it->second, ctx);
                if (c.valid()) samples=sampleCurve2(c, ctx.options->curveSamplesPerSpan);
            }
        }
        // Curve12 is optional in QIF.  STEP-derived files often preserve only
        // the topological Edge/Curve13. Reconstruct the missing p-curve by
        // projecting that oriented 3D edge onto the Face surface.
        if(samples.empty())projectEdgeToSurfaceUV(ce,surface,ctx,samples);
        appendSurfaceCurveSamples(out.uv,std::move(samples),surface);
    }
    cleanRing(out.uv);return out.uv.size()>=3;
}

void addPolyline(QifMesh& out,std::vector<Vec3> pts,Color c={40,44,50,255}) {
    if(pts.size()<2) return;
    for(auto p:pts) out.bounds.add(p);
    out.edges.push_back({std::move(pts),c});
}

struct PrimitiveCursor {
    std::size_t triangles = 0, edges = 0, points = 0;
};

PrimitiveCursor primitiveCursor(const QifMesh& out) {
    return {out.triangles.size(), out.edges.size(), out.points.size()};
}

void tagSource(QifMesh& out, PrimitiveCursor before, const std::string& id) {
    if (id.empty()) return;
    for (std::size_t i=before.triangles;i<out.triangles.size();++i) out.triangles[i].sourceId=id;
    for (std::size_t i=before.edges;i<out.edges.size();++i) out.edges[i].sourceId=id;
    for (std::size_t i=before.points;i<out.points.size();++i) out.points[i].sourceId=id;
}

void tagBody(QifMesh& out, PrimitiveCursor before, const std::string& id) {
    if (id.empty()) return;
    for (std::size_t i=before.triangles;i<out.triangles.size();++i) out.triangles[i].bodyId=id;
    for (std::size_t i=before.edges;i<out.edges.size();++i) out.edges[i].bodyId=id;
    for (std::size_t i=before.points;i<out.points.size();++i) out.points[i].bodyId=id;
}

void tagComponent(QifMesh& out, PrimitiveCursor before, const std::string& id) {
    if (id.empty()) return;
    const std::string token = "|" + id + "|";
    auto append=[&](std::string& p){ if(p.find(token)==std::string::npos) p += token; };
    for (std::size_t i=before.triangles;i<out.triangles.size();++i) append(out.triangles[i].componentPath);
    for (std::size_t i=before.edges;i<out.edges.size();++i) append(out.edges[i].componentPath);
    for (std::size_t i=before.points;i<out.points.size();++i) append(out.points[i].componentPath);
}

void emitRefined(const Surface& surface,const Transform3& world,Vec2 ua,Vec2 ub,Vec2 uc,Color color,double tolerance,int depth,int maxDepth,bool turned,std::vector<Triangle>& triangles,Bounds3& bounds) {
    const Vec3 a=world.applyPoint(surface.eval(ua)),b=world.applyPoint(surface.eval(ub)),c=world.applyPoint(surface.eval(uc));bool split=false;
    if(surface.curved&&depth<maxDepth){Vec2 ab=midpoint(ua,ub),bc=midpoint(ub,uc),ca=midpoint(uc,ua),center{(ua.x+ub.x+uc.x)/3.0,(ua.y+ub.y+uc.y)/3.0};double e0=distance(world.applyPoint(surface.eval(ab)),midpoint(a,b)),e1=distance(world.applyPoint(surface.eval(bc)),midpoint(b,c)),e2=distance(world.applyPoint(surface.eval(ca)),midpoint(c,a)),e3=distance(world.applyPoint(surface.eval(center)),(a+b+c)/3.0);split=std::max(std::max(e0,e1),std::max(e2,e3))>tolerance;}
    if(split){Vec2 ab=midpoint(ua,ub),bc=midpoint(ub,uc),ca=midpoint(uc,ua);emitRefined(surface,world,ua,ab,ca,color,tolerance,depth+1,maxDepth,turned,triangles,bounds);emitRefined(surface,world,ab,ub,bc,color,tolerance,depth+1,maxDepth,turned,triangles,bounds);emitRefined(surface,world,ca,bc,uc,color,tolerance,depth+1,maxDepth,turned,triangles,bounds);emitRefined(surface,world,ab,bc,ca,color,tolerance,depth+1,maxDepth,turned,triangles,bounds);return;}
    if(length2(cross(b-a,c-a))<1e-28) return;
    if(turned) triangles.push_back({a,c,b,color}); else triangles.push_back({a,b,c,color});
    bounds.add(a);bounds.add(b);bounds.add(c);
}

using EarPoint=std::array<double,2>;using EarRing=std::vector<EarPoint>;using EarPolygon=std::vector<EarRing>;

bool periodicRectangleBounds(const RingData& ring,const Surface& surface,Vec2& lo,Vec2& hi) {
    if(ring.uv.size()<4 || (!surface.periodicU && !surface.periodicV)) return false;
    lo={std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity()};
    hi={-std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()};
    for(auto p:ring.uv){lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);}
    const double du=hi.x-lo.x,dv=hi.y-lo.y;
    const bool coversU=surface.periodicU && surface.periodU>kTiny && du>=surface.periodU*0.95;
    const bool coversV=surface.periodicV && surface.periodV>kTiny && dv>=surface.periodV*0.95;
    if(!coversU && !coversV)return false;
    const double tol=std::max({std::abs(du),std::abs(dv),1.0})*2e-5;
    for(auto p:ring.uv){
        const bool onSide=std::abs(p.x-lo.x)<=tol||std::abs(p.x-hi.x)<=tol||std::abs(p.y-lo.y)<=tol||std::abs(p.y-hi.y)<=tol;
        if(!onSide)return false;
    }
    return du>tol&&dv>tol;
}

void emitParamRectangle(const Surface& surface,const Transform3& world,Vec2 lo,Vec2 hi,Color color,double tolerance,int maxDepth,bool turned,std::vector<Triangle>& triangles,Bounds3& bounds){
    const Vec2 a{lo.x,lo.y},b{hi.x,lo.y},c{hi.x,hi.y},d{lo.x,hi.y};
    emitRefined(surface,world,a,b,c,color,tolerance,0,maxDepth,turned,triangles,bounds);
    emitRefined(surface,world,a,c,d,color,tolerance,0,maxDepth,turned,triangles,bounds);
}

bool renderParamFace(pugi::xml_node face,const Transform3& world,Style inherited,Context& ctx) {
    const PrimitiveCursor emittedFrom = primitiveCursor(*ctx.out);
    Style style=applyStyle(inherited,face);if(style.hidden&&!ctx.options->includeHidden){++ctx.out->diagnostics.hiddenEntityCount;return true;}
    const std::string sid=refId(face,"Surface");auto sit=ctx.ids.find(sid);if(sid.empty()||sit==ctx.ids.end())return false;Surface surface=parseSurfaceNode(sit->second,ctx);if(!surface.valid()){++ctx.out->diagnostics.unsupportedGeometryTypes[localName(sit->second.name())];return false;}
    const bool hasOuter=attrBool(face,"hasOuter",true);std::vector<RingData> rings;const auto lids=refIds(face,"LoopIds");
    for(const auto&id:lids){auto it=ctx.ids.find(id);if(it==ctx.ids.end()||localName(it->second.name())!="Loop")continue;RingData r;if(parseLoopRing(it->second,surface,ctx,r))rings.push_back(std::move(r));}
    if(!hasOuter){RingData natural;natural.form="OUTER";natural.uv={{surface.u0,surface.v0},{surface.u1,surface.v0},{surface.u1,surface.v1},{surface.u0,surface.v1}};rings.insert(rings.begin(),std::move(natural));}
    if(rings.empty())return false;
    // Slit/vertex loops are non-area trimming entities. Keep their wireframe but do not feed them to earcut as holes.
    std::vector<RingData> areaRings;for(std::size_t ri=0;ri<rings.size();++ri){const auto&r=rings[ri];std::vector<Vec3> line;line.reserve(r.uv.size()+1);for(auto uv:r.uv)line.push_back(world.applyPoint(surface.eval(uv)));if(!line.empty())line.push_back(line.front());addPolyline(*ctx.out,std::move(line));if(r.form!="SLIT"&&r.form!="VERTEX")areaRings.push_back(r);}
    if(areaRings.empty())return false;
    // QIF says the first LoopId is the outer loop when hasOuter=true. Some exports also label it OUTER; move one explicitly-labelled outer loop first when present.
    auto oi=std::find_if(areaRings.begin(),areaRings.end(),[](const RingData&r){return r.form=="OUTER";});if(oi!=areaRings.end()&&oi!=areaRings.begin())std::iter_swap(areaRings.begin(),oi);
    const bool turned=attrBool(face,"turned",false);
    if(areaRings.size()==1){
        Vec2 lo{},hi{};
        if(periodicRectangleBounds(areaRings.front(),surface,lo,hi)){
            emitParamRectangle(surface,world,lo,hi,style.color,ctx.out->tessellationTolerance,std::max(0,ctx.options->maxRefinementDepth),turned,ctx.out->triangles,ctx.out->bounds);
            tagSource(*ctx.out,emittedFrom,face.attribute("id").value());
            return ctx.out->triangles.size()>emittedFrom.triangles;
        }
    }
    EarPolygon poly;std::vector<Vec2> flat;for(const auto&r:areaRings){EarRing er;for(auto p:r.uv){er.push_back({p.x,p.y});flat.push_back(p);}poly.push_back(std::move(er));}
    std::vector<std::uint32_t> indices;try{indices=mapbox::earcut<std::uint32_t>(poly);}catch(...){return false;}if(indices.size()<3)return false;
    for(std::size_t i=0;i+2<indices.size();i+=3){if(indices[i]>=flat.size()||indices[i+1]>=flat.size()||indices[i+2]>=flat.size())continue;emitRefined(surface,world,flat[indices[i]],flat[indices[i+1]],flat[indices[i+2]],style.color,ctx.out->tessellationTolerance,0,std::max(0,ctx.options->maxRefinementDepth),turned,ctx.out->triangles,ctx.out->bounds);}
    tagSource(*ctx.out,emittedFrom,face.attribute("id").value());return true;
}

bool visibleIndex(std::size_t localIndex,const std::vector<std::int64_t>& visible,const std::vector<std::int64_t>& hidden) {
    if(!visible.empty()&&std::find(visible.begin(),visible.end(),static_cast<std::int64_t>(localIndex))==visible.end())return false;
    return std::find(hidden.begin(),hidden.end(),static_cast<std::int64_t>(localIndex))==hidden.end();
}

bool renderMeshFace(pugi::xml_node face,const Transform3& world,Style inherited,Context& ctx) {
    const PrimitiveCursor emittedFrom = primitiveCursor(*ctx.out);
    Style style=applyStyle(inherited,face);if(style.hidden&&!ctx.options->includeHidden){++ctx.out->diagnostics.hiddenEntityCount;return true;}std::string mid=refId(face,"Mesh");auto mit=ctx.ids.find(mid);if(mid.empty()||mit==ctx.ids.end())return false;MeshData m=parseMesh(mit->second,ctx);if(!m.valid())return false;
    std::vector<std::int64_t> subset=arrayInts(face,"Triangles","TrianglesBinary",&ctx);if(subset.empty()){subset.resize(m.triangles.size());for(std::size_t i=0;i<subset.size();++i)subset[i]=static_cast<std::int64_t>(i);}
    auto vis=arrayInts(face,"TrianglesVisible","TrianglesVisibleBinary",&ctx),hid=arrayInts(face,"TrianglesHidden","TrianglesHiddenBinary",&ctx);auto colors=arrayBytes(face,"TrianglesColor","TrianglesColorBinary",&ctx);bool turned=attrBool(face,"turned",false);
    for(std::size_t li=0;li<subset.size();++li){if(!visibleIndex(li,vis,hid))continue;auto ti=subset[li];if(ti<0||static_cast<std::size_t>(ti)>=m.triangles.size())continue;auto t=m.triangles[static_cast<std::size_t>(ti)];Color c=style.color;if(colors.size()>li*3+2)c={colors[li*3],colors[li*3+1],colors[li*3+2],style.color.a};Vec3 a=world.applyPoint(m.vertices[t[0]]),b=world.applyPoint(m.vertices[t[1]]),d=world.applyPoint(m.vertices[t[2]]);if(turned)std::swap(b,d);ctx.out->triangles.push_back({a,b,d,c});ctx.out->bounds.add(a);ctx.out->bounds.add(b);ctx.out->bounds.add(d);}
    // Mesh loops reference PathTriangulation objects. Render each path edge from its triangle/edge pair.
    for(const auto&lid:refIds(face,"LoopIds")){auto lit=ctx.ids.find(lid);if(lit==ctx.ids.end())continue;auto ces=childLocal(lit->second,"CoEdgesMesh");for(auto ce:ces.children()){if(ce.type()!=pugi::node_element||localName(ce.name())!="CoEdgeMesh")continue;std::string pid=refId(ce,"CurveMesh");auto pit=ctx.ids.find(pid);if(pit==ctx.ids.end())continue;auto pathCore=childLocal(pit->second,"PathTriangulationCore");auto pairs=arrayInts(pathCore,"Edges","EdgesBinary",&ctx);std::string pmid=refId(pit->second,"MeshTriangle");auto pmit=ctx.ids.find(pmid);MeshData pm=(pmit!=ctx.ids.end())?parseMesh(pmit->second,ctx):m;for(std::size_t j=0;j+1<pairs.size();j+=2){auto ti=pairs[j],opp=pairs[j+1];if(ti<0||opp<0||opp>2||static_cast<std::size_t>(ti)>=pm.triangles.size())continue;auto tr=pm.triangles[static_cast<std::size_t>(ti)];std::uint32_t i0=tr[(static_cast<int>(opp)+1)%3],i1=tr[(static_cast<int>(opp)+2)%3];if(i0>=pm.vertices.size()||i1>=pm.vertices.size())continue;addPolyline(*ctx.out,{world.applyPoint(pm.vertices[i0]),world.applyPoint(pm.vertices[i1])});}}}
    tagSource(*ctx.out,emittedFrom,face.attribute("id").value());
    return true;
}

bool renderFaceById(const std::string& id,const Transform3& world,Style style,Context&ctx){auto it=ctx.ids.find(id);if(it==ctx.ids.end())return false;std::string n=localName(it->second.name());if(n=="Face")return renderParamFace(it->second,world,style,ctx);if(n=="FaceMesh"){++ctx.out->diagnostics.meshFaceCount;return renderMeshFace(it->second,world,style,ctx);}return false;}


bool renderEdgeById(const std::string& id,const Transform3& world,Style inherited,Context& ctx,bool reverse=false) {
    const PrimitiveCursor emittedFrom = primitiveCursor(*ctx.out);
    auto it=ctx.ids.find(id);if(it==ctx.ids.end()||localName(it->second.name())!="Edge")return false;
    pugi::xml_node edge=it->second;Style style=applyStyle(inherited,edge);if(style.hidden&&!ctx.options->includeHidden){++ctx.out->diagnostics.hiddenEntityCount;return true;}
    const std::string cid=refId(edge,"Curve");auto cit=ctx.ids.find(cid);if(cid.empty()||cit==ctx.ids.end())return false;
    Curve3 c=parseCurve3Node(cit->second,ctx);if(!c.valid()){++ctx.out->diagnostics.unsupportedGeometryTypes[localName(cit->second.name())];return false;}
    auto pts=sampleCurve3(c,ctx.options->curveSamplesPerSpan);if(reverse)std::reverse(pts.begin(),pts.end());for(auto& p:pts)p=world.applyPoint(p);addPolyline(*ctx.out,std::move(pts),style.color);tagSource(*ctx.out,emittedFrom,id);return true;
}

bool renderLoopById(const std::string& id,const Transform3& world,Style inherited,Context& ctx) {
    auto it = ctx.ids.find(id);
    if (it == ctx.ids.end()) return false;
    const std::string type = localName(it->second.name());
    if (type == "Loop") {
        bool any = false;
        auto coedges = childLocal(it->second, "CoEdges");
        for (auto ce : coedges.children()) {
            if (ce.type() != pugi::node_element || localName(ce.name()) != "CoEdge") continue;
            auto eo = childLocal(ce, "EdgeOriented");
            auto idNode = childLocal(eo, "Id");
            if (!idNode) continue;
            any = renderEdgeById(idNode.child_value(), world, inherited, ctx, attrBool(eo, "turned", false)) || any;
        }
        return any;
    }
    if (type == "LoopMesh") {
        bool any = false;
        auto coedges = childLocal(it->second, "CoEdgesMesh");
        for (auto ce : coedges.children()) {
            if (ce.type() != pugi::node_element || localName(ce.name()) != "CoEdgeMesh") continue;
            auto eo = childLocal(ce, "EdgeOriented");
            auto idNode = childLocal(eo, "Id");
            if (!idNode) continue;
            any = renderEdgeById(idNode.child_value(), world, inherited, ctx, attrBool(eo, "turned", false)) || any;
        }
        return any;
    }
    return false;
}

bool renderVertexById(const std::string& id,const Transform3& world,Style inherited,Context& ctx) {
    const PrimitiveCursor emittedFrom = primitiveCursor(*ctx.out);
    auto it=ctx.ids.find(id);if(it==ctx.ids.end()||localName(it->second.name())!="Vertex")return false;
    pugi::xml_node vertex=it->second;Style style=applyStyle(inherited,vertex);if(style.hidden&&!ctx.options->includeHidden){++ctx.out->diagnostics.hiddenEntityCount;return true;}
    const std::string pid=refId(vertex,"Point");auto pit=ctx.ids.find(pid);if(pid.empty()||pit==ctx.ids.end())return false;
    auto xyz=childLocal(pit->second,"XYZ");if(!xyz)return false;Vec3 p=world.applyPoint(parseVec3(xyz.child_value()));ctx.out->points.push_back({p,style.color,2.0});ctx.out->bounds.add(p);tagSource(*ctx.out,emittedFrom,id);return true;
}

bool renderPointCloud(pugi::xml_node cloud,const Transform3& world,Style inherited,Context& ctx) {
    const PrimitiveCursor emittedFrom = primitiveCursor(*ctx.out);
    if(!ctx.options->includePointClouds) return true;
    Style style=applyStyle(inherited,cloud);
    if(style.hidden&&!ctx.options->includeHidden){++ctx.out->diagnostics.hiddenEntityCount;return true;}
    auto raw=arrayDoubles(cloud,"Points","PointsBinary",&ctx);if(raw.size()<3)return false;
    auto vis=arrayInts(cloud,"PointsVisible","PointsVisibleBinary",&ctx);
    auto hid=arrayInts(cloud,"PointsHidden","PointsHiddenBinary",&ctx);
    auto colors=arrayBytes(cloud,"PointsColor","PointsColorBinary",&ctx);
    const std::size_t n=raw.size()/3;for(std::size_t i=0;i<n;++i){if(!visibleIndex(i,vis,hid))continue;Color c=style.color;if(colors.size()>=3*(i+1))c={colors[3*i],colors[3*i+1],colors[3*i+2],style.color.a};Vec3 p=world.applyPoint({raw[3*i],raw[3*i+1],raw[3*i+2]});ctx.out->points.push_back({p,c,2.0});ctx.out->bounds.add(p);}tagSource(*ctx.out,emittedFrom,cloud.attribute("id").value());++ctx.out->diagnostics.pointCloudCount;return true;
}

std::vector<std::string> shellFaceIds(pugi::xml_node body,Context& ctx,std::vector<std::pair<std::string,bool>>& faces) {
    std::unordered_set<std::string> seen;
    for(const auto& id:refIds(body,"FaceIds"))if(seen.insert(id).second)faces.push_back({id,false});
    for(const auto& sid:refIds(body,"ShellIds")){
        auto it=ctx.ids.find(sid);if(it==ctx.ids.end()||localName(it->second.name())!="Shell")continue;const bool st=attrBool(it->second,"turned",false);
        for(const auto& fid:refIds(it->second,"FaceIds"))if(seen.insert(fid).second)faces.push_back({fid,st});
    }
    std::vector<std::string> out;out.reserve(faces.size());for(auto&f:faces)out.push_back(f.first);return out;
}

bool renderFaceByIdTurned(const std::string& id,const Transform3& world,Style style,bool extraTurn,Context&ctx){
    auto it=ctx.ids.find(id);if(it==ctx.ids.end())return false;std::string n=localName(it->second.name());
    if(!extraTurn)return renderFaceById(id,world,style,ctx);
    // Shell orientation is topological. Instead of mutating the DOM, render and reverse the triangles just emitted.
    const std::size_t before=ctx.out->triangles.size();bool ok=false;if(n=="Face")ok=renderParamFace(it->second,world,style,ctx);else if(n=="FaceMesh"){++ctx.out->diagnostics.meshFaceCount;ok=renderMeshFace(it->second,world,style,ctx);}if(ok)for(std::size_t i=before;i<ctx.out->triangles.size();++i)std::swap(ctx.out->triangles[i].b,ctx.out->triangles[i].c);return ok;
}

bool renderBody(pugi::xml_node body,const Transform3& parentWorld,Style inherited,Context&ctx) {
    const PrimitiveCursor emittedFrom = primitiveCursor(*ctx.out);
    Style style=applyStyle(inherited,body);if(style.hidden&&!ctx.options->includeHidden){++ctx.out->diagnostics.hiddenEntityCount;return true;}
    Transform3 world=compose(parentWorld,referencedTransform(body,ctx));std::vector<std::pair<std::string,bool>> faces;shellFaceIds(body,ctx,faces);bool any=false;
    for(const auto& f:faces){++ctx.out->faceCount;if(renderFaceByIdTurned(f.first,world,style,f.second,ctx))any=true;else ++ctx.out->skippedFaces;}

    // QIF explicitly permits lower-dimensional bodies (loops, edges and
    // vertices).  STEP-derived wire/edge models frequently use these forms.
    // Render the explicit lower-dimensional topology regardless of whether a
    // face list is also present: it is useful as a recovery path when a face
    // has malformed or producer-specific trimming data.
    const std::size_t edgesBefore = ctx.out->edges.size();
    for(const auto&l:refIds(body,"LoopIds"))any=renderLoopById(l,world,style,ctx)||any;
    if(ctx.out->edges.size()==edgesBefore){
        for(const auto&e:refIds(body,"EdgeIds"))any=renderEdgeById(e,world,style,ctx)||any;
    }
    for(const auto&v:refIds(body,"VertexIds"))any=renderVertexById(v,world,style,ctx)||any;

    tagBody(*ctx.out,emittedFrom,body.attribute("id").value());
    return any;
}

bool renderPartAssemblyCommon(pugi::xml_node n,const Transform3& world,Style style,Context&ctx) {
    bool any=false;for(const auto& bid:refIds(n,"BodyIds")){auto it=ctx.ids.find(bid);if(it!=ctx.ids.end()&&localName(it->second.name())=="Body")any=renderBody(it->second,world,style,ctx)||any;}
    for(const auto& pid:refIds(n,"PointCloudIds")){auto it=ctx.ids.find(pid);if(it!=ctx.ids.end()&&localName(it->second.name())=="PointCloud")any=renderPointCloud(it->second,world,style,ctx)||any;}
    return any;
}

bool renderPart(pugi::xml_node part,const Transform3& world,Style inherited,Context&ctx) {
    Style style=applyStyle(inherited,part);if(style.hidden&&!ctx.options->includeHidden){++ctx.out->diagnostics.hiddenEntityCount;return true;}return renderPartAssemblyCommon(part,world,style,ctx);
}

bool renderNodeById(const std::string& id,const Transform3& world,Style style,Context&ctx,std::unordered_set<std::string>& stack);

bool renderComponent(pugi::xml_node comp,const Transform3& parentWorld,Style inherited,Context&ctx,std::unordered_set<std::string>& stack) {
    const PrimitiveCursor emittedFrom = primitiveCursor(*ctx.out);
    Style style=applyStyle(inherited,comp);if(style.hidden&&!ctx.options->includeHidden){++ctx.out->diagnostics.hiddenEntityCount;return true;}Transform3 world=compose(parentWorld,referencedTransform(comp,ctx));++ctx.out->diagnostics.componentInstances;
    std::string target=refId(comp,"Part");if(target.empty())target=refId(comp,"Assembly");if(target.empty())return false;
    const bool ok=renderNodeById(target,world,style,ctx,stack);tagComponent(*ctx.out,emittedFrom,comp.attribute("id").value());return ok;
}

bool renderAssembly(pugi::xml_node assembly,const Transform3& world,Style inherited,Context&ctx,std::unordered_set<std::string>& stack) {
    Style style=applyStyle(inherited,assembly);if(style.hidden&&!ctx.options->includeHidden){++ctx.out->diagnostics.hiddenEntityCount;return true;}bool any=renderPartAssemblyCommon(assembly,world,style,ctx);
    for(const auto& cid:refIds(assembly,"ComponentIds")){auto it=ctx.ids.find(cid);if(it!=ctx.ids.end()&&localName(it->second.name())=="Component")any=renderComponent(it->second,world,style,ctx,stack)||any;}return any;
}

bool renderNodeById(const std::string& id,const Transform3& world,Style style,Context&ctx,std::unordered_set<std::string>& stack) {
    if(!stack.insert(id).second){ctx.warn("Assembly/component cycle detected at QIF id "+id+".");return false;}auto it=ctx.ids.find(id);if(it==ctx.ids.end()){stack.erase(id);return false;}const std::string n=localName(it->second.name());bool ok=false;
    if(n=="Part")ok=renderPart(it->second,world,style,ctx);else if(n=="Assembly")ok=renderAssembly(it->second,world,style,ctx,stack);else if(n=="Component")ok=renderComponent(it->second,world,style,ctx,stack);else if(n=="Body")ok=renderBody(it->second,world,style,ctx);stack.erase(id);return ok;
}

SavedCamera parseSavedCamera(pugi::xml_node n) {
    SavedCamera c;
    c.id = n.attribute("id").value();
    c.label = n.attribute("label").value();
    if (auto form = n.attribute("form")) c.form = form.value();
    c.viewPlaneOrigin = parseVec3(nodeText(n,"ViewPlaneOrigin").c_str());
    if (auto orientation = childLocal(n,"Orientation")) {
        auto q = parseNumbers(nodeText(orientation,"Value").c_str());
        if (q.size() >= 4) c.orientation = {q[0],q[1],q[2],q[3]};
    }
    c.ratio = nodeDouble(n,"Ratio",1.0);
    c.nearDistance = nodeDouble(n,"Near",0.0);
    c.farDistance = nodeDouble(n,"Far",0.0);
    c.halfHeight = nodeDouble(n,"Height",1.0);
    return c;
}

SavedView parseSavedView(pugi::xml_node n) {
    SavedView v;
    v.id = n.attribute("id").value();
    v.label = n.attribute("label").value();
    const std::string active = nodeText(n,"ActiveView");
    v.active = active == "1" || active == "true" || active == "TRUE" || active == "True";
    v.cameraIds = refIds(n,"CameraIds");
    v.annotationVisibleIds = refIds(n,"AnnotationVisibleIds");
    v.annotationHiddenIds = refIds(n,"AnnotationHiddenIds");
    v.bodyIds = refIds(n,"BodyIds");
    v.componentIds = refIds(n,"ComponentIds");
    v.simplifiedRepresentationId = nodeText(n,"SimplifiedRepresentationId");
    v.explodedViewId = nodeText(n,"ExplodedViewId");
    v.displayStyleId = nodeText(n,"DisplayStyleId");
    v.zoneSectionId = nodeText(n,"ZoneSectionId");
    return v;
}

void indexModel(pugi::xml_node n,Context&ctx,Bounds3& rawBounds) {
    static const std::unordered_set<std::string> geometryNames={"Point","Segment12","Polyline12","ArcCircular12","ArcConic12","Nurbs12","Spline12","Aggregate12","Segment13","Polyline13","ArcCircular13","ArcConic13","Nurbs13","Spline13","Aggregate13","Plane23","Cylinder23","Cone23","Sphere23","Torus23","Nurbs23","Spline23","Extrude23","Ruled23","Revolution23","Offset23","PathTriangulation","MeshTriangle"};
    static const std::unordered_set<std::string> topologyNames={"Vertex","Edge","Loop","LoopMesh","Face","FaceMesh","Shell","Body","PointCloud"};
    if(n.type()==pugi::node_element){const std::string name=localName(n.name());++ctx.out->diagnostics.totalElementCount;++ctx.out->diagnostics.elementTypes[name];if(auto a=n.attribute("id")){++ctx.out->diagnostics.idCount;auto inserted=ctx.ids.emplace(a.value(),n);if(!inserted.second){++ctx.out->diagnostics.duplicateIdCount;ctx.warn("Duplicate QIF id "+std::string(a.value())+" encountered; keeping the first entity.");}}if(geometryNames.count(name))++ctx.out->diagnostics.geometryTypes[name];if(topologyNames.count(name))++ctx.out->diagnostics.topologyTypes[name];
        if(name=="Point"){auto xyz=childLocal(n,"XYZ");if(xyz)rawBounds.add(parseVec3(xyz.child_value()));}
        else if(name=="MeshTriangle"){auto core=childLocal(n,"MeshTriangleCore");auto v=arrayDoubles(core,"Vertices","VerticesBinary",nullptr);for(std::size_t i=0;i+2<v.size();i+=3)rawBounds.add({v[i],v[i+1],v[i+2]});}
        else if(name=="PointCloud"){auto v=arrayDoubles(n,"Points","PointsBinary",nullptr);for(std::size_t i=0;i+2<v.size();i+=3)rawBounds.add({v[i],v[i+1],v[i+2]});}
        else if(name=="ExternalQIFDocument")++ctx.out->diagnostics.externalQifReferenceCount;
        else if(name=="VisualizationSet")++ctx.out->diagnostics.visualizationSetCount;
        else if(name=="PMIDisplay")++ctx.out->diagnostics.pmiDisplayCount;
        else if(name=="AnnotationView" && n.attribute("id")) {
            AnnotationViewFrame av;
            av.normal = parseVec3(nodeText(n, "Normal").c_str());
            av.direction = parseVec3(nodeText(n, "Direction").c_str());
            ctx.annotationViews[n.attribute("id").value()] = av;
        }
        else if(name=="Camera")ctx.out->savedCameras.push_back(parseSavedCamera(n));
        else if(name=="SavedView"){++ctx.out->diagnostics.savedViewCount;ctx.out->savedViews.push_back(parseSavedView(n));}
        if(name=="Transform"&&n.attribute("id")){ctx.transforms[n.attribute("id").value()]=parseTransformInstance(n);++ctx.out->diagnostics.transformCount;}
    }
    for(auto c:n.children())indexModel(c,ctx,rawBounds);
}

void collectAngularUnits(pugi::xml_node n, Context& ctx) {
    if (n.type() == pugi::node_element && localName(n.name()) == "AngularUnit") {
        const std::string name = nodeText(n,"UnitName");
        if (!name.empty()) {
            double factor = 1.0, offset = 0.0;
            if (auto conv = childLocal(n,"UnitConversion")) {
                factor = nodeDouble(conv,"Factor",1.0);
                offset = nodeDouble(conv,"Offset",0.0);
            }
            ctx.angularUnits[name] = {factor,offset};
        }
    }
    for (auto c:n.children()) collectAngularUnits(c,ctx);
}

void parseUnitContext(pugi::xml_node root, Context& ctx) {
    auto fileUnits = findFirstLocal(root,"FileUnits");
    if (!fileUnits) return;
    collectAngularUnits(fileUnits,ctx);
    if (auto primary=childLocal(fileUnits,"PrimaryUnits")) {
        if (auto au=childLocal(primary,"AngularUnit")) {
            ctx.primaryAngularUnit=nodeText(au,"UnitName");
            if (!ctx.primaryAngularUnit.empty() && !ctx.angularUnits.count(ctx.primaryAngularUnit)) {
                double factor=1.0,offset=0.0;
                if (auto conv=childLocal(au,"UnitConversion")) { factor=nodeDouble(conv,"Factor",1.0); offset=nodeDouble(conv,"Offset",0.0); }
                ctx.angularUnits[ctx.primaryAngularUnit]={factor,offset};
            }
        }
    }
}

std::string primaryLinearUnit(pugi::xml_node root) {
    if(auto fileUnits=findFirstLocal(root,"FileUnits")){if(auto primary=childLocal(fileUnits,"PrimaryUnits")){if(auto linear=childLocal(primary,"LinearUnit")){if(auto unit=childLocal(linear,"UnitName"))return unit.child_value();}}}
    return {};
}

bool renderProduct(pugi::xml_node product,Context&ctx) {
    Transform3 world=Transform3::identity();Style style;std::unordered_set<std::string> stack;std::string rootId=refId(product,"RootPart");if(rootId.empty())rootId=refId(product,"RootAssembly");if(rootId.empty())rootId=refId(product,"RootComponent");
    if(!rootId.empty())return renderNodeById(rootId,world,style,ctx,stack);
    // Root is optional in the schema. If absent, render all declared parts; if there are none, render all bodies.
    bool any=false;if(auto set=childLocal(product,"PartSet")){for(auto n:set.children())if(n.type()==pugi::node_element&&localName(n.name())=="Part")any=renderPart(n,world,style,ctx)||any;}
    if(!any){auto topo=childLocal(product,"TopologySet");auto bodies=childLocal(topo,"BodySet");for(auto b:bodies.children())if(b.type()==pugi::node_element&&localName(b.name())=="Body")any=renderBody(b,world,style,ctx)||any;}
    return any;
}

bool renderStandaloneCurve(pugi::xml_node curve, Context& ctx) {
    const PrimitiveCursor emittedFrom = primitiveCursor(*ctx.out);
    Style style = applyStyle({}, curve);
    if (style.hidden && !ctx.options->includeHidden) { ++ctx.out->diagnostics.hiddenEntityCount; return true; }
    Curve3 c = parseCurve3Node(curve, ctx);
    if (!c.valid()) {
        ++ctx.out->diagnostics.unsupportedGeometryTypes[localName(curve.name())];
        return false;
    }
    auto pts = sampleCurve3(c, ctx.options->curveSamplesPerSpan);
    if (pts.size() < 2) return false;
    addPolyline(*ctx.out, std::move(pts), style.color);
    tagSource(*ctx.out, emittedFrom, curve.attribute("id").value());
    return true;
}

bool renderStandaloneMesh(pugi::xml_node mesh, Context& ctx) {
    const PrimitiveCursor emittedFrom = primitiveCursor(*ctx.out);
    Style style = applyStyle({}, mesh);
    if (style.hidden && !ctx.options->includeHidden) { ++ctx.out->diagnostics.hiddenEntityCount; return true; }
    MeshData m = parseMesh(mesh, ctx);
    if (!m.valid()) return false;
    for (const auto& t : m.triangles) {
        const Vec3 a = m.vertices[t[0]], b = m.vertices[t[1]], c = m.vertices[t[2]];
        ctx.out->triangles.push_back({a,b,c,style.color});
        ctx.out->bounds.add(a);ctx.out->bounds.add(b);ctx.out->bounds.add(c);
    }
    tagSource(*ctx.out, emittedFrom, mesh.attribute("id").value());
    return true;
}

bool renderStandalonePoint(pugi::xml_node point, Context& ctx) {
    auto xyz = childLocal(point, "XYZ");
    if (!xyz) return false;
    Style style = applyStyle({}, point);
    if (style.hidden && !ctx.options->includeHidden) { ++ctx.out->diagnostics.hiddenEntityCount; return true; }
    const Vec3 p = parseVec3(xyz.child_value());
    ctx.out->points.push_back({p, style.color, 2.0});
    ctx.out->bounds.add(p);
    if (auto id = point.attribute("id")) ctx.out->points.back().sourceId = id.value();
    return true;
}

bool renderFallbackGeometry(Context& ctx) {
    Style style;
    bool any = false;

    // First prefer explicit topology. This is the important path for STEP
    // wireframe/edge-derived QIF files whose Product root or Body references
    // are incomplete or omitted by the exporter.
    if (ctx.out->faceCount == 0) {
        for (const auto& kv : ctx.ids) {
            const std::string n = localName(kv.second.name());
            if (n == "Face" || n == "FaceMesh") {
                ++ctx.out->faceCount;
                if (renderFaceById(kv.first, Transform3::identity(), style, ctx)) any = true;
                else ++ctx.out->skippedFaces;
            }
        }
    }
    const std::size_t edgeCountAfterFaces = ctx.out->edges.size();
    for (const auto& kv : ctx.ids) {
        if (localName(kv.second.name()) == "Edge") any = renderEdgeById(kv.first, Transform3::identity(), style, ctx) || any;
    }
    for (const auto& kv : ctx.ids) {
        if (localName(kv.second.name()) == "Vertex") any = renderVertexById(kv.first, Transform3::identity(), style, ctx) || any;
    }
    for (const auto& kv : ctx.ids) {
        if (localName(kv.second.name()) == "PointCloud" && ctx.options->includePointClouds)
            any = renderPointCloud(kv.second, Transform3::identity(), style, ctx) || any;
    }

    // If there is no usable topology, display legal standalone GeometrySet
    // entities. STEP converters often emit Curve13 geometry even when they do
    // not build Body/Edge topology.
    static const std::unordered_set<std::string> curve13Names = {
        "Segment13","Polyline13","ArcCircular13","ArcConic13","Nurbs13","Spline13","Aggregate13"
    };
    if (ctx.out->edges.size() == edgeCountAfterFaces) {
        for (const auto& kv : ctx.ids) {
            if (curve13Names.count(localName(kv.second.name()))) any = renderStandaloneCurve(kv.second, ctx) || any;
        }
    }
    if (ctx.out->triangles.empty()) {
        for (const auto& kv : ctx.ids) {
            if (localName(kv.second.name()) == "MeshTriangle") any = renderStandaloneMesh(kv.second, ctx) || any;
        }
    }
    if (ctx.out->points.empty() && ctx.out->edges.empty() && ctx.out->triangles.empty()) {
        for (const auto& kv : ctx.ids) {
            if (localName(kv.second.name()) == "Point") any = renderStandalonePoint(kv.second, ctx) || any;
        }
    }
    return any;
}

} // namespace

bool QifLoader::load(const std::string& path,QifMesh& out,std::string& error,const LoadOptions& options) {
    out={};error.clear();pugi::xml_document doc;const auto result=doc.load_file(path.c_str(),pugi::parse_default|pugi::parse_ws_pcdata);if(!result){error=std::string("XML parse failed: ")+result.description();return false;}
    auto root=doc.document_element();if(!root||localName(root.name())!="QIFDocument"){error="Not a QIFDocument XML file.";return false;}out.qifVersion=root.attribute("versionQIF").value();out.linearUnit=primaryLinearUnit(root);
    if(options.retainDocumentTree){out.documentTree=retainGenericElement(root);out.hasDocumentTree=true;}
    for(auto c:root.children())if(c.type()==pugi::node_element)++out.diagnostics.rootSections[localName(c.name())];
    Context ctx;ctx.options=&options;ctx.out=&out;parseUnitContext(root,ctx);Bounds3 rawBounds;ctx.ids.reserve(static_cast<std::size_t>(root.attribute("idMax").as_ullong(4096))+128);indexModel(root,ctx,rawBounds);collectVisualization(root, ctx);
    if(out.diagnostics.externalQifReferenceCount>0)ctx.warn("External QIF document references are present. This viewer currently resolves only entities stored in the opened QIFDocument.");
    if(out.diagnostics.pmiDisplayCount>0 && out.annotationTexts.empty() && out.annotationPolylines.empty())ctx.warn("PMI display records are present, but none could be projected because required annotation-plane data were missing.");
    if(out.diagnostics.savedViewCount>0 && (!out.annotationTexts.empty() || !out.annotationPolylines.empty()))ctx.warn("Saved-view camera/body/component visibility and annotation visible/hidden filtering are supported. Display-style, simplified-representation, exploded-view and zone-section semantics are retained, but not yet graphically applied.");
    else if(out.diagnostics.savedViewCount>0)ctx.warn("Saved-view camera and body/component visibility are supported. Display-style, simplified-representation, exploded-view and zone-section semantics are retained, but not yet graphically applied.");
    if(!out.qifVersion.empty() && out.qifVersion.rfind("3.",0)!=0)ctx.warn("This build targets QIF 3.x; the document reports versionQIF="+out.qifVersion+".");
    const double diag=std::max(rawBounds.diagonal(),1.0);out.tessellationTolerance=options.tessellationTolerance>0.0?options.tessellationTolerance:diag*2.5e-4;
    auto product=findFirstLocal(root,"Product");bool rendered=product&&renderProduct(product,ctx);
    if(!rendered || (out.triangles.empty() && out.edges.empty() && out.points.empty())){
        // Geometry-only and STEP-derived edge/wire QIF files may omit the
        // Product root, Part/Body links, or even topology completely. Fall
        // back through Face -> Edge -> Vertex -> standalone Curve13/Mesh/Point
        // so legal display geometry is not rejected just because assembly
        // structure is incomplete.
        rendered = renderFallbackGeometry(ctx) || rendered;
    } else if (out.triangles.empty() && out.skippedFaces > 0) {
        // If every face failed to triangulate, keep the model useful by
        // drawing its underlying 3D edges instead of presenting an empty view.
        Style s;
        for (const auto& kv : ctx.ids) if (localName(kv.second.name()) == "Edge") renderEdgeById(kv.first, Transform3::identity(), s, ctx);
    }
    if(!out.bounds.valid&&rawBounds.valid)out.bounds=rawBounds;
    if(options.strictUnsupported&&!out.diagnostics.unsupportedGeometryTypes.empty()){std::ostringstream ss;ss<<"Unsupported QIF geometry types:";for(const auto&kv:out.diagnostics.unsupportedGeometryTypes)ss<<" "<<kv.first<<"("<<kv.second<<")";error=ss.str();return false;}
    if(options.requireDisplayGeometry && out.triangles.empty()&&out.edges.empty()&&out.points.empty()){
        std::ostringstream ss;
        ss << "The QIF file was parsed, but no displayable geometry/topology was emitted. Indexed geometry:";
        for (const auto& kv : out.diagnostics.geometryTypes) ss << " " << kv.first << "=" << kv.second;
        ss << "; topology:";
        for (const auto& kv : out.diagnostics.topologyTypes) ss << " " << kv.first << "=" << kv.second;
        if (!out.diagnostics.unsupportedGeometryTypes.empty()) {
            ss << "; unsupported/invalid geometry:";
            for (const auto& kv : out.diagnostics.unsupportedGeometryTypes) ss << " " << kv.first << "=" << kv.second;
        }
        error=ss.str();return false;
    }
    return true;
}

} // namespace qifv
