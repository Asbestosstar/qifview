#pragma once

#include "math.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace qifv {

struct Triangle {
    Vec3 a, b, c;
    Color color;
    Triangle() = default;
    Triangle(Vec3 aa, Vec3 bb, Vec3 cc, Color col) : a(aa), b(bb), c(cc), color(col) {}
    std::string sourceId;
    std::string bodyId;
    std::string componentPath;
};

struct Polyline3 {
    std::vector<Vec3> points;
    Color color{40, 44, 50, 255};
    Polyline3() = default;
    Polyline3(std::vector<Vec3> pts, Color col) : points(std::move(pts)), color(col) {}
    std::string sourceId;
    std::string bodyId;
    std::string componentPath;
};

struct PointPrimitive {
    Vec3 point;
    Color color{230, 230, 230, 255};
    double size = 1.0;
    PointPrimitive() = default;
    PointPrimitive(Vec3 p, Color col, double s) : point(p), color(col), size(s) {}
    std::string sourceId;
    std::string bodyId;
    std::string componentPath;
};

struct AnnotationPolyline {
    std::vector<Vec3> points;
    Color color{245, 245, 245, 255};
    std::string sourceId;
};

struct AnnotationText {
    std::string text;
    Vec3 origin;
    Vec3 right{1.0, 0.0, 0.0};
    Vec3 up{0.0, 1.0, 0.0};
    double lineHeight = 1.0;
    Color color{245, 245, 245, 255};
    std::string sourceId;
};

struct GenericAttribute {
    std::string name;
    std::string value;
};

// Schema-neutral retention of the complete QIF XML tree.  Viewer-specific
// typed handlers sit alongside this rather than discarding application data
// (Plans, Resources, Rules, Results, Statistics, traceability, signatures,
// user extensions, etc.) that does not affect 3D geometry.
struct GenericElement {
    std::string name;
    std::vector<GenericAttribute> attributes;
    std::string text;
    std::vector<GenericElement> children;
};

struct SavedCamera {
    std::string id;
    std::string label;
    std::string form = "ORTHOGRAPHIC";
    Vec3 viewPlaneOrigin{};
    Quaternion orientation{};
    double ratio = 1.0;
    double nearDistance = 0.0;
    double farDistance = 0.0;
    double halfHeight = 1.0;
};

struct SavedView {
    std::string id;
    std::string label;
    bool active = false;
    std::vector<std::string> cameraIds;
    std::vector<std::string> annotationVisibleIds;
    std::vector<std::string> annotationHiddenIds;
    std::vector<std::string> bodyIds;
    std::vector<std::string> componentIds;
    std::string simplifiedRepresentationId;
    std::string explodedViewId;
    std::string displayStyleId;
    std::string zoneSectionId;
};

struct QifDiagnostics {
    // Schema-complete generic inventory. Every element in a valid QIFDocument
    // is retained by the XML DOM even when it has no viewer-specific semantic
    // handler; these maps make that coverage visible to qifcheck.
    std::map<std::string, std::size_t> elementTypes;
    std::map<std::string, std::size_t> rootSections;
    std::map<std::string, std::size_t> geometryTypes;
    std::map<std::string, std::size_t> unsupportedGeometryTypes;
    std::map<std::string, std::size_t> topologyTypes;
    std::vector<std::string> warnings;
    std::size_t totalElementCount = 0;
    std::size_t idCount = 0;
    std::size_t duplicateIdCount = 0;
    std::size_t transformCount = 0;
    std::size_t componentInstances = 0;
    std::size_t meshFaceCount = 0;
    std::size_t pointCloudCount = 0;
    std::size_t hiddenEntityCount = 0;
    std::size_t binaryArrayCount = 0;
    std::size_t externalQifReferenceCount = 0;
    std::size_t visualizationSetCount = 0;
    std::size_t pmiDisplayCount = 0;
    std::size_t savedViewCount = 0;
    std::size_t annotationTextCount = 0;
    std::size_t annotationPolylineCount = 0;
};

struct QifMesh {
    std::vector<Triangle> triangles;
    std::vector<Polyline3> edges;
    std::vector<PointPrimitive> points;
    std::vector<AnnotationPolyline> annotationPolylines;
    std::vector<AnnotationText> annotationTexts;
    Bounds3 bounds;
    std::size_t faceCount = 0;
    std::size_t skippedFaces = 0;
    std::string qifVersion;
    std::string linearUnit;
    double tessellationTolerance = 0.0;
    std::vector<SavedCamera> savedCameras;
    std::vector<SavedView> savedViews;
    QifDiagnostics diagnostics;
    GenericElement documentTree;
    bool hasDocumentTree = false;
};

struct LoadOptions {
    // <= 0 means derive a tolerance from the QIF model bounds.
    double tessellationTolerance = 0.0;
    int maxRefinementDepth = 7;
    int curveSamplesPerSpan = 10;
    bool includeHidden = false;
    bool includePointClouds = true;
    bool strictUnsupported = false;
    // Viewer mode normally requires displayable Product geometry. Set false
    // for full-document inspection of valid QIF Plan/Rules/Results/Statistics
    // documents that legitimately contain no renderable product shape.
    bool requireDisplayGeometry = true;
    // Preserve every XML element/attribute/value in a schema-neutral tree.
    // Off by default for the GUI to avoid duplicating large documents in RAM.
    bool retainDocumentTree = false;
};

class QifLoader {
public:
    // Loads QIF 3.x Product geometry and creates a portable display mesh.
    // Geometry/topology semantics are based on ANSI/DMSC QIF 3.0-2018.
    static bool load(const std::string& path,
                     QifMesh& out,
                     std::string& error,
                     const LoadOptions& options = {});
};

} // namespace qifv
