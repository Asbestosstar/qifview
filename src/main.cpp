#include "qif_model.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace {

using qifv::QifMesh;
using qifv::Vec3;

struct Camera {
    Vec3 target{};
    Vec3 homeTarget{};
    double yaw = 0.75;
    double pitch = 0.45;
    double distance = 10.0;
    double homeDistance = 10.0;
};

struct CameraFrame {
    Vec3 pos;
    Vec3 right;
    Vec3 up;
    Vec3 forward;
};

CameraFrame frameFor(const Camera& c) {
    const double cp = std::cos(c.pitch), sp = std::sin(c.pitch);
    const double cy = std::cos(c.yaw), sy = std::sin(c.yaw);
    const Vec3 fromTarget{cp * cy, cp * sy, sp};
    const Vec3 pos = c.target + fromTarget * c.distance;
    const Vec3 forward = qifv::normalized(c.target - pos);
    Vec3 worldUp{0.0, 0.0, 1.0};
    Vec3 right = qifv::normalized(qifv::cross(forward, worldUp));
    if (qifv::length2(right) < 1e-20) right = {1.0, 0.0, 0.0};
    const Vec3 up = qifv::normalized(qifv::cross(right, forward));
    return {pos, right, up, forward};
}

Vec3 toCamera(Vec3 p, const CameraFrame& f) {
    const Vec3 d = p - f.pos;
    return {qifv::dot(d, f.right), qifv::dot(d, f.up), qifv::dot(d, f.forward)};
}

bool project(Vec3 p, int width, int height, double focal, double nearPlane, SDL_FPoint& out) {
    if (p.z <= nearPlane) return false;
    out.x = static_cast<float>(static_cast<double>(width) * 0.5 + focal * p.x / p.z);
    out.y = static_cast<float>(static_cast<double>(height) * 0.5 - focal * p.y / p.z);
    return true;
}

void resetCamera(Camera& c, const QifMesh& mesh) {
    c.homeTarget = mesh.bounds.valid ? mesh.bounds.center() : Vec3{};
    c.target = c.homeTarget;
    const double radius = std::max(mesh.bounds.diagonal() * 0.5, 1.0);
    c.homeDistance = radius * 2.8;
    c.distance = c.homeDistance;
    c.yaw = 0.75;
    c.pitch = 0.45;
}

struct ProjectedTriangle {
    double depth = 0.0;
    SDL_Vertex v[3]{};
};

SDL_FColor shade(qifv::Color base, Vec3 normal) {
    const Vec3 light = qifv::normalized(Vec3{-0.30, 0.45, -1.0});
    const double diffuse = std::abs(qifv::dot(qifv::normalized(normal), light));
    const double k = 0.30 + 0.70 * diffuse;
    return {static_cast<float>((base.r / 255.0) * k),
            static_cast<float>((base.g / 255.0) * k),
            static_cast<float>((base.b / 255.0) * k),
            1.0f};
}

void renderModel(SDL_Renderer* renderer,
                 const QifMesh& mesh,
                 const Camera& camera,
                 bool filled,
                 bool wireframe) {
    int width = 0, height = 0;
    if (!SDL_GetRenderOutputSize(renderer, &width, &height) || width <= 1 || height <= 1) return;

    const CameraFrame cf = frameFor(camera);
    const double focal = 0.5 * static_cast<double>(std::min(width, height)) / std::tan(45.0 * 3.14159265358979323846 / 360.0);
    const double nearPlane = std::max(camera.distance * 0.01, 1e-9);

    if (filled) {
        std::vector<ProjectedTriangle> projected;
        projected.reserve(mesh.triangles.size());
        for (const auto& t : mesh.triangles) {
            const Vec3 a = toCamera(t.a, cf);
            const Vec3 b = toCamera(t.b, cf);
            const Vec3 c = toCamera(t.c, cf);
            SDL_FPoint pa{}, pb{}, pc{};
            if (!project(a, width, height, focal, nearPlane, pa) ||
                !project(b, width, height, focal, nearPlane, pb) ||
                !project(c, width, height, focal, nearPlane, pc)) continue;

            const SDL_FColor color = shade(t.color, qifv::cross(b - a, c - a));
            ProjectedTriangle d;
            d.depth = (a.z + b.z + c.z) / 3.0;
            d.v[0].position = pa; d.v[0].color = color; d.v[0].tex_coord = {0.0f, 0.0f};
            d.v[1].position = pb; d.v[1].color = color; d.v[1].tex_coord = {0.0f, 0.0f};
            d.v[2].position = pc; d.v[2].color = color; d.v[2].tex_coord = {0.0f, 0.0f};
            projected.push_back(d);
        }

        std::sort(projected.begin(), projected.end(), [](const ProjectedTriangle& a, const ProjectedTriangle& b) {
            return a.depth > b.depth; // Painter's algorithm: far to near.
        });

        std::vector<SDL_Vertex> vertices;
        vertices.reserve(projected.size() * 3);
        for (const auto& t : projected) {
            vertices.push_back(t.v[0]);
            vertices.push_back(t.v[1]);
            vertices.push_back(t.v[2]);
        }
        if (!vertices.empty()) {
            SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()), nullptr, 0);
        }
    }

    if (wireframe) {
        SDL_SetRenderDrawColor(renderer, 24, 27, 31, 255);
        std::vector<SDL_FPoint> points;
        for (const auto& line : mesh.edges) {
            points.clear();
            points.reserve(line.points.size());
            bool visible = true;
            for (auto p : line.points) {
                SDL_FPoint q{};
                if (!project(toCamera(p, cf), width, height, focal, nearPlane, q)) {
                    visible = false;
                    break;
                }
                points.push_back(q);
            }
            if (visible && points.size() >= 2) SDL_RenderLines(renderer, points.data(), static_cast<int>(points.size()));
        }
    }
}

std::string fileNameOnly(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

void setTitle(SDL_Window* window, const std::string& path, const QifMesh& mesh, SDL_Renderer* renderer) {
    const char* rendererName = SDL_GetRendererName(renderer);
    std::string title = "QIF Viewer — " + fileNameOnly(path)
        + " — " + (rendererName ? rendererName : "unknown renderer")
        + " — " + std::to_string(mesh.faceCount) + " faces / "
        + std::to_string(mesh.triangles.size()) + " triangles";
    if (mesh.skippedFaces) title += " / " + std::to_string(mesh.skippedFaces) + " skipped";
    SDL_SetWindowTitle(window, title.c_str());
}

bool loadFile(const std::string& path, QifMesh& mesh, Camera& camera, double tolerance) {
    qifv::LoadOptions opts;
    opts.tessellationTolerance = tolerance;
    std::string error;
    QifMesh next;
    std::cerr << "Loading " << path << " ...\n";
    if (!qifv::QifLoader::load(path, next, error, opts)) {
        std::cerr << "QIF load failed: " << error << "\n";
        return false;
    }
    mesh = std::move(next);
    resetCamera(camera, mesh);
    std::cerr << "QIF " << mesh.qifVersion << ", unit=" << (mesh.linearUnit.empty() ? "unknown" : mesh.linearUnit)
              << ", faces=" << mesh.faceCount
              << ", triangles=" << mesh.triangles.size()
              << ", edge loops=" << mesh.edges.size()
              << ", tessellation tolerance=" << mesh.tessellationTolerance << "\n";
    if (mesh.skippedFaces) std::cerr << "Warning: skipped " << mesh.skippedFaces << " unsupported/invalid faces.\n";
    return true;
}

void printUsage(const char* exe) {
    std::cout
        << "Usage: " << exe << " [--renderer auto|vulkan|metal|opengl|software] [--tolerance N] file.qif\n\n"
        << "Controls:\n"
        << "  Left drag        Orbit\n"
        << "  Right drag       Pan\n"
        << "  Mouse wheel      Zoom\n"
        << "  F                Toggle shaded faces\n"
        << "  W                Toggle wireframe edges\n"
        << "  R                Reset view\n"
        << "  1 / 2 / 3        Front / side / top view\n"
        << "  Esc              Quit\n"
        << "  Drag a .qif file onto the window to open another file\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string rendererName;
    std::string path;
    double tolerance = 0.0;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--renderer" && i + 1 < argc) {
            rendererName = argv[++i];
            if (rendererName == "auto") rendererName.clear();
        } else if (a == "--tolerance" && i + 1 < argc) {
            tolerance = std::strtod(argv[++i], nullptr);
        } else if (a == "--help" || a == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "Unknown option: " << a << "\n";
            printUsage(argv[0]);
            return 2;
        } else {
            path = a;
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("QIF Viewer", 1280, 820,
                                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, rendererName.empty() ? nullptr : rendererName.c_str());
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer failed";
        if (!rendererName.empty()) std::cerr << " for '" << rendererName << "'";
        std::cerr << ": " << SDL_GetError() << "\nAvailable renderers:";
        for (int i = 0; i < SDL_GetNumRenderDrivers(); ++i) std::cerr << " " << SDL_GetRenderDriver(i);
        std::cerr << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    printUsage(argv[0]);
    std::cerr << "Renderer: " << (SDL_GetRendererName(renderer) ? SDL_GetRendererName(renderer) : "unknown") << "\n";

    QifMesh mesh;
    Camera camera;
    bool filled = true;
    bool wireframe = true;
    bool leftDown = false;
    bool rightDown = false;
    bool running = true;

    if (!path.empty()) {
        if (loadFile(path, mesh, camera, tolerance)) setTitle(window, path, mesh, renderer);
    } else {
        std::cerr << "No file supplied. Drag a .qif file onto the window.\n";
    }

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) leftDown = true;
                if (ev.button.button == SDL_BUTTON_RIGHT) rightDown = true;
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (ev.button.button == SDL_BUTTON_LEFT) leftDown = false;
                if (ev.button.button == SDL_BUTTON_RIGHT) rightDown = false;
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (leftDown) {
                    camera.yaw -= static_cast<double>(ev.motion.xrel) * 0.007;
                    camera.pitch += static_cast<double>(ev.motion.yrel) * 0.007;
                    camera.pitch = std::clamp(camera.pitch, -1.53, 1.53);
                }
                if (rightDown && mesh.bounds.valid) {
                    int w = 1, h = 1;
                    SDL_GetRenderOutputSize(renderer, &w, &h);
                    const double worldPerPixel = camera.distance / std::max(1, std::min(w, h));
                    const CameraFrame cf = frameFor(camera);
                    camera.target = camera.target - cf.right * (static_cast<double>(ev.motion.xrel) * worldPerPixel)
                                                 + cf.up * (static_cast<double>(ev.motion.yrel) * worldPerPixel);
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                camera.distance *= std::exp(-static_cast<double>(ev.wheel.y) * 0.12);
                camera.distance = std::clamp(camera.distance, camera.homeDistance * 0.015, camera.homeDistance * 100.0);
                break;
            case SDL_EVENT_KEY_DOWN:
                if (ev.key.repeat) break;
                if (ev.key.key == SDLK_ESCAPE) running = false;
                else if (ev.key.key == SDLK_F) filled = !filled;
                else if (ev.key.key == SDLK_W) wireframe = !wireframe;
                else if (ev.key.key == SDLK_R) resetCamera(camera, mesh);
                else if (ev.key.key == SDLK_1) { camera.yaw = 0.0; camera.pitch = 0.0; }
                else if (ev.key.key == SDLK_2) { camera.yaw = 1.5707963267948966; camera.pitch = 0.0; }
                else if (ev.key.key == SDLK_3) { camera.yaw = 0.0; camera.pitch = 1.52; }
                break;
            case SDL_EVENT_DROP_FILE:
                if (ev.drop.data && loadFile(ev.drop.data, mesh, camera, tolerance)) {
                    path = ev.drop.data;
                    setTitle(window, path, mesh, renderer);
                }
                break;
            default:
                break;
            }
        }

        SDL_SetRenderDrawColor(renderer, 238, 241, 245, 255);
        SDL_RenderClear(renderer);
        if (mesh.bounds.valid) renderModel(renderer, mesh, camera, filled, wireframe);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
