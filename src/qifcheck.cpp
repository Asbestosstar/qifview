#include "qif_model.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <algorithm>

namespace {

void usage(const char* exe) {
    std::cout << "Usage: " << exe
              << " [--tolerance N] [--show-hidden] [--no-points] [--strict] [--all-elements]"
                 " [--tree [--tree-depth N]] file.qif [file2.qif ...]\n";
}

void printMap(const char* label, const std::map<std::string,std::size_t>& values) {
    if (values.empty()) return;
    std::cout << "  " << label << ":";
    for (const auto& kv : values) std::cout << " " << kv.first << "=" << kv.second;
    std::cout << "\n";
}

std::string compactText(std::string value) {
    for (char& c : value) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    constexpr std::size_t limit = 120;
    if (value.size() > limit) value = value.substr(0, limit) + "...";
    return value;
}

void printTree(const qifv::GenericElement& e, int depth, int maxDepth) {
    if (maxDepth >= 0 && depth > maxDepth) return;
    const std::string pad(static_cast<std::size_t>(depth * 2), ' ');
    std::cout << pad << '<' << e.name;
    for (const auto& a : e.attributes) std::cout << ' ' << a.name << "=\"" << compactText(a.value) << "\"";
    std::cout << '>';
    if (!e.text.empty()) std::cout << ' ' << compactText(e.text);
    std::cout << "\n";
    if (maxDepth < 0 || depth < maxDepth) {
        for (const auto& c : e.children) printTree(c, depth + 1, maxDepth);
    } else if (!e.children.empty()) {
        std::cout << pad << "  ... " << e.children.size() << " child element(s)\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    qifv::LoadOptions opts;
    // qifcheck is a full QIF document inspector, not just a geometry viewer.
    // Allow application-only documents with no displayable Product shape.
    opts.requireDisplayGeometry = false;
    bool allElements = false;
    bool printDocumentTree = false;
    int treeDepth = -1;
    int firstFile = 1;
    if (argc < 2) { usage(argv[0]); return 2; }

    while (firstFile < argc) {
        const std::string a = argv[firstFile];
        if (a == "--tolerance" && firstFile + 1 < argc) {
            opts.tessellationTolerance = std::strtod(argv[firstFile + 1], nullptr);
            firstFile += 2;
        } else if (a == "--show-hidden") {
            opts.includeHidden = true; ++firstFile;
        } else if (a == "--no-points") {
            opts.includePointClouds = false; ++firstFile;
        } else if (a == "--strict") {
            opts.strictUnsupported = true; ++firstFile;
        } else if (a == "--all-elements") {
            allElements = true; ++firstFile;
        } else if (a == "--tree") {
            printDocumentTree = true; opts.retainDocumentTree = true; ++firstFile;
        } else if (a == "--tree-depth" && firstFile + 1 < argc) {
            printDocumentTree = true; opts.retainDocumentTree = true;
            treeDepth = std::max(0, std::atoi(argv[firstFile + 1]));
            firstFile += 2;
        } else if (a == "--help" || a == "-h") {
            usage(argv[0]); return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "Unknown option: " << a << "\n"; usage(argv[0]); return 2;
        } else break;
    }
    if (firstFile >= argc) { usage(argv[0]); return 2; }

    bool failed = false;
    for (int i = firstFile; i < argc; ++i) {
        qifv::QifMesh mesh;
        std::string error;
        if (!qifv::QifLoader::load(argv[i], mesh, error, opts)) {
            std::cerr << argv[i] << ": ERROR: " << error << "\n";
            failed = true;
            continue;
        }

        std::cout << argv[i] << "\n"
                  << "  QIF version: " << (mesh.qifVersion.empty() ? "unknown" : mesh.qifVersion) << "\n"
                  << "  XML elements retained: " << mesh.diagnostics.totalElementCount << "\n"
                  << "  QIF ids: " << mesh.diagnostics.idCount << " (duplicates=" << mesh.diagnostics.duplicateIdCount << ")\n"
                  << "  linear unit: " << (mesh.linearUnit.empty() ? "unknown" : mesh.linearUnit) << "\n"
                  << "  rendered faces: " << mesh.faceCount << "\n"
                  << "  skipped faces: " << mesh.skippedFaces << "\n"
                  << "  triangles: " << mesh.triangles.size() << "\n"
                  << "  wire polylines: " << mesh.edges.size() << "\n"
                  << "  points: " << mesh.points.size() << "\n"
                  << "  mesh faces: " << mesh.diagnostics.meshFaceCount << "\n"
                  << "  point clouds: " << mesh.diagnostics.pointCloudCount << "\n"
                  << "  transforms: " << mesh.diagnostics.transformCount << "\n"
                  << "  component instances: " << mesh.diagnostics.componentInstances << "\n"
                  << "  binary arrays decoded: " << mesh.diagnostics.binaryArrayCount << "\n"
                  << "  hidden entities skipped: " << mesh.diagnostics.hiddenEntityCount << "\n"
                  << "  external QIF references: " << mesh.diagnostics.externalQifReferenceCount << "\n"
                  << "  visualization sets: " << mesh.diagnostics.visualizationSetCount << "\n"
                  << "  PMI display records: " << mesh.diagnostics.pmiDisplayCount << "\n"
                  << "  saved cameras: " << mesh.savedCameras.size() << "\n"
                  << "  saved views: " << mesh.savedViews.size() << "\n"
                  << std::setprecision(12)
                  << "  tessellation tolerance: " << mesh.tessellationTolerance << "\n";
        if (mesh.bounds.valid) {
            std::cout << "  bounds min: " << mesh.bounds.min.x << " " << mesh.bounds.min.y << " " << mesh.bounds.min.z << "\n"
                      << "  bounds max: " << mesh.bounds.max.x << " " << mesh.bounds.max.y << " " << mesh.bounds.max.z << "\n";
        }
        for (const auto& view : mesh.savedViews) {
            std::cout << "  saved view: " << (view.label.empty() ? view.id : view.label)
                      << (view.active ? " [active]" : "")
                      << " cameras=" << view.cameraIds.size()
                      << " visibleBodies=" << view.bodyIds.size()
                      << " visibleComponents=" << view.componentIds.size()
                      << " visibleAnnotations=" << view.annotationVisibleIds.size()
                      << " hiddenAnnotations=" << view.annotationHiddenIds.size() << "\n";
        }
        printMap("root sections", mesh.diagnostics.rootSections);
        printMap("geometry", mesh.diagnostics.geometryTypes);
        printMap("topology", mesh.diagnostics.topologyTypes);
        printMap("unsupported geometry", mesh.diagnostics.unsupportedGeometryTypes);
        if (allElements) printMap("all element names", mesh.diagnostics.elementTypes);
        if (printDocumentTree && mesh.hasDocumentTree) {
            std::cout << "  retained QIF document tree:\n";
            printTree(mesh.documentTree, 2, treeDepth < 0 ? -1 : treeDepth + 2);
        }
        for (const auto& w : mesh.diagnostics.warnings) std::cout << "  warning: " << w << "\n";
    }
    return failed ? 1 : 0;
}
