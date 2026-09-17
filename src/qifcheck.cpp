#include "qif_model.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

void usage(const char* exe) {
    std::cout << "Usage: " << exe << " [--tolerance N] file.qif [file2.qif ...]\n";
}

} // namespace

int main(int argc, char** argv) {
    double tolerance = 0.0;
    int firstFile = 1;

    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    if (firstFile < argc && std::string(argv[firstFile]) == "--tolerance") {
        if (firstFile + 2 >= argc) {
            usage(argv[0]);
            return 2;
        }
        tolerance = std::strtod(argv[firstFile + 1], nullptr);
        firstFile += 2;
    }

    bool failed = false;
    for (int i = firstFile; i < argc; ++i) {
        qifv::LoadOptions opts;
        opts.tessellationTolerance = tolerance;

        qifv::QifMesh mesh;
        std::string error;
        const bool ok = qifv::QifLoader::load(argv[i], mesh, error, opts);
        if (!ok) {
            std::cerr << argv[i] << ": ERROR: " << error << "\n";
            failed = true;
            continue;
        }

        std::cout << argv[i] << "\n"
                  << "  QIF version: " << (mesh.qifVersion.empty() ? "unknown" : mesh.qifVersion) << "\n"
                  << "  linear unit: " << (mesh.linearUnit.empty() ? "unknown" : mesh.linearUnit) << "\n"
                  << "  faces: " << mesh.faceCount << "\n"
                  << "  skipped faces: " << mesh.skippedFaces << "\n"
                  << "  triangles: " << mesh.triangles.size() << "\n"
                  << "  edge loops: " << mesh.edges.size() << "\n"
                  << std::setprecision(12)
                  << "  tessellation tolerance: " << mesh.tessellationTolerance << "\n";

        if (mesh.bounds.valid) {
            std::cout << "  bounds min: " << mesh.bounds.min.x << " " << mesh.bounds.min.y << " " << mesh.bounds.min.z << "\n"
                      << "  bounds max: " << mesh.bounds.max.x << " " << mesh.bounds.max.y << " " << mesh.bounds.max.z << "\n";
        }
    }

    return failed ? 1 : 0;
}
