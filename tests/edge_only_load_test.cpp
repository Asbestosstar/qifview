#include "qif_model.hpp"

#include <cmath>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: edge_only_load_test edge-only.qif\n";
        return 2;
    }
    qifv::QifMesh mesh;
    std::string error;
    qifv::LoadOptions opts;
    if (!qifv::QifLoader::load(argv[1], mesh, error, opts)) {
        std::cerr << error << "\n";
        return 1;
    }
    if (mesh.edges.empty()) {
        std::cerr << "edge-only QIF loaded but emitted no wire polylines\n";
        return 1;
    }
    if (!mesh.bounds.valid || std::abs(mesh.bounds.min.x - 0.0) > 1e-9 ||
        std::abs(mesh.bounds.max.x - 25.0) > 1e-9) {
        std::cerr << "unexpected edge-only bounds\n";
        return 1;
    }
    std::cout << "edge-only STEP-style QIF fallback: PASS (" << mesh.edges.size() << " polyline(s))\n";
    return 0;
}
