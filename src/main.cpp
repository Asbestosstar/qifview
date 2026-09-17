#include "qif_model.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace {

using qifv::QifMesh;
using qifv::Vec3;

struct CameraFrame {
    Vec3 pos;
    Vec3 right;
    Vec3 up;
    Vec3 forward;
};

struct Camera {
    Vec3 target{};
    Vec3 homeTarget{};
    double yaw = 0.75;
    double pitch = 0.45;
    double distance = 10.0;
    double homeDistance = 10.0;

    bool qifSavedCamera = false;
    bool qifOrthographic = false;
    bool qifPerspective = false;
    CameraFrame qifFrame{};
    double qifHalfHeight = 1.0;
    double qifRatio = 1.0;
    double qifNear = 0.0;
    double qifFar = 0.0;
    int activeSavedView = -1;
    std::string activeSavedViewLabel;
};

CameraFrame frameFor(const Camera& c) {
    if (c.qifSavedCamera) return c.qifFrame;
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

bool projectPoint(Vec3 p, int width, int height, double focal, double nearPlane, const Camera& camera, SDL_FPoint& out) {
    if (camera.qifSavedCamera) {
        const double halfHeight = std::max(std::abs(camera.qifHalfHeight), 1e-12);
        const double ratio = std::max(std::abs(camera.qifRatio), 1e-6);
        const double sx = static_cast<double>(width) / (2.0 * halfHeight * ratio);
        const double sy = static_cast<double>(height) / (2.0 * halfHeight);
        const double scale = std::min(sx, sy);

        // QIF Camera Near/Far are distances along the oriented viewing axis.
        // Orthographic files commonly use signed symmetric values.  Apply the
        // interval whenever it is non-degenerate.
        if (std::abs(camera.qifFar - camera.qifNear) > 1e-12) {
            const double lo = std::min(camera.qifNear, camera.qifFar);
            const double hi = std::max(camera.qifNear, camera.qifFar);
            if (p.z < lo || p.z > hi) return false;
        }

        if (camera.qifPerspective) {
            // QIF Figure 211 defines ViewPlaneOrigin as the projection center;
            // Near locates the 2D/near plane and Height is its half-height.
            double projectionDistance = camera.qifNear;
            if (projectionDistance <= 1e-12) {
                // Be tolerant of producers that omit or zero Near while still
                // avoiding a singular projection.
                projectionDistance = std::max(std::abs(camera.qifFar) * 1e-6, 1e-6);
            }
            if (p.z <= 1e-12) return false;
            const double k = scale * projectionDistance / p.z;
            out.x = static_cast<float>(static_cast<double>(width) * 0.5 + k * p.x);
            out.y = static_cast<float>(static_cast<double>(height) * 0.5 - k * p.y);
            return std::isfinite(out.x) && std::isfinite(out.y);
        }

        out.x = static_cast<float>(static_cast<double>(width) * 0.5 + scale * p.x);
        out.y = static_cast<float>(static_cast<double>(height) * 0.5 - scale * p.y);
        return std::isfinite(out.x) && std::isfinite(out.y);
    }
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
    c.qifSavedCamera = false;
    c.qifOrthographic = false;
    c.qifPerspective = false;
    c.qifNear = 0.0;
    c.qifFar = 0.0;
    c.activeSavedView = -1;
    c.activeSavedViewLabel.clear();
}

const qifv::SavedCamera* findSavedCamera(const QifMesh& mesh, const std::string& id) {
    for (const auto& c : mesh.savedCameras) if (c.id == id) return &c;
    return nullptr;
}

bool activateSavedView(Camera& camera, const QifMesh& mesh, int index, bool announce = true) {
    if (index < 0 || index >= static_cast<int>(mesh.savedViews.size())) return false;
    const auto& view = mesh.savedViews[static_cast<std::size_t>(index)];
    const qifv::SavedCamera* source = nullptr;
    for (const auto& id : view.cameraIds) {
        const auto* candidate = findSavedCamera(mesh, id);
        if (candidate) { source = candidate; break; }
    }
    if (!source) return false;

    qifv::Quaternion q = qifv::normalized(source->orientation);
    Vec3 right = qifv::normalized(qifv::rotate(q, {1.0, 0.0, 0.0}));
    Vec3 upRaw = qifv::rotate(q, {0.0, 1.0, 0.0});
    Vec3 up = qifv::normalized(upRaw - right * qifv::dot(upRaw, right));
    Vec3 forward = qifv::normalized(qifv::cross(right, up));
    const Vec3 declaredNormal = qifv::normalized(qifv::rotate(q, {0.0, 0.0, 1.0}));
    if (qifv::dot(forward, declaredNormal) < 0.0) forward = -forward;
    if (qifv::length2(right) < 1e-20 || qifv::length2(up) < 1e-20 || qifv::length2(forward) < 1e-20) return false;

    camera.qifSavedCamera = true;
    camera.qifPerspective = source->form == "PERSPECTIVE";
    camera.qifOrthographic = !camera.qifPerspective;
    camera.qifFrame = {source->viewPlaneOrigin, right, up, forward};
    camera.qifHalfHeight = std::abs(source->halfHeight) > 1e-12 ? std::abs(source->halfHeight)
                         : std::max(mesh.bounds.diagonal() * 0.5, 1.0);
    camera.qifRatio = source->ratio > 1e-9 ? source->ratio : 1.0;
    camera.qifNear = source->nearDistance;
    camera.qifFar = source->farDistance;
    camera.activeSavedView = index;
    camera.activeSavedViewLabel = view.label.empty() ? view.id : view.label;
    if (announce) std::cerr << "Saved view: " << camera.activeSavedViewLabel << " (camera " << source->id << ")\n";
    return true;
}

void activateInitialSavedView(Camera& camera, const QifMesh& mesh) {
    for (std::size_t i = 0; i < mesh.savedViews.size(); ++i) {
        if (mesh.savedViews[i].active && activateSavedView(camera, mesh, static_cast<int>(i), false)) {
            std::cerr << "Active QIF saved view: " << camera.activeSavedViewLabel << "\n";
            return;
        }
    }
}

void cycleSavedView(Camera& camera, const QifMesh& mesh) {
    if (mesh.savedViews.empty()) { std::cerr << "No QIF saved views in this file.\n"; return; }
    const int n = static_cast<int>(mesh.savedViews.size());
    int start = camera.activeSavedView >= 0 ? camera.activeSavedView : -1;
    for (int step = 1; step <= n; ++step) {
        const int index = (start + step) % n;
        if (activateSavedView(camera, mesh, index)) return;
    }
    std::cerr << "No QIF saved camera is available.\n";
}

void leaveSavedViewForOrbit(Camera& camera, const QifMesh& mesh) {
    if (!camera.qifSavedCamera) return;
    const Vec3 viewNormal = camera.qifFrame.forward;
    resetCamera(camera, mesh);
    const Vec3 fromTarget = -viewNormal;
    camera.yaw = std::atan2(fromTarget.y, fromTarget.x);
    camera.pitch = std::asin(std::clamp(fromTarget.z, -1.0, 1.0));
}

const qifv::SavedView* activeSavedView(const Camera& camera, const QifMesh& mesh) {
    if (camera.activeSavedView < 0 || camera.activeSavedView >= static_cast<int>(mesh.savedViews.size())) return nullptr;
    return &mesh.savedViews[static_cast<std::size_t>(camera.activeSavedView)];
}

bool containsId(const std::vector<std::string>& ids, const std::string& id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

bool componentPathContains(const std::string& path, const std::string& id) {
    return !id.empty() && path.find("|" + id + "|") != std::string::npos;
}

bool primitiveVisibleInSavedView(const std::string& bodyId,
                                 const std::string& componentPath,
                                 const qifv::SavedView* view) {
    if (!view) return true;
    if (!view->bodyIds.empty() && !containsId(view->bodyIds, bodyId)) return false;
    if (!view->componentIds.empty()) {
        bool matched = false;
        for (const auto& id : view->componentIds) {
            if (componentPathContains(componentPath, id)) { matched = true; break; }
        }
        if (!matched) return false;
    }
    return true;
}

bool annotationVisibleInSavedView(const std::string& sourceId, const qifv::SavedView* view) {
    if (!view || sourceId.empty()) return true;
    if (!view->annotationVisibleIds.empty() && !containsId(view->annotationVisibleIds, sourceId)) return false;
    if (containsId(view->annotationHiddenIds, sourceId)) return false;
    return true;
}

struct StrokeSeg { float x1, y1, x2, y2; };

std::vector<StrokeSeg> strokeGlyph(char ch) {
    switch (std::toupper(static_cast<unsigned char>(ch))) {
    case 'A': return {{0,0,0.5f,1},{1,0,0.5f,1},{0.2f,0.5f,0.8f,0.5f}};
    case 'B': return {{0,0,0,1},{0,1,0.7f,1},{0.7f,1,0.8f,0.85f},{0.8f,0.85f,0.8f,0.65f},{0.8f,0.65f,0.7f,0.5f},{0,0.5f,0.7f,0.5f},{0.7f,0.5f,0.8f,0.35f},{0.8f,0.35f,0.8f,0.15f},{0.8f,0.15f,0.7f,0},{0,0,0.7f,0}};
    case 'C': return {{0.85f,0.9f,0.7f,1},{0.7f,1,0.15f,1},{0.15f,1,0,0.8f},{0,0.8f,0,0.2f},{0,0.2f,0.15f,0},{0.15f,0,0.7f,0},{0.7f,0,0.85f,0.1f}};
    case 'D': return {{0,0,0,1},{0,1,0.6f,1},{0.6f,1,0.9f,0.75f},{0.9f,0.75f,0.9f,0.25f},{0.9f,0.25f,0.6f,0},{0.6f,0,0,0}};
    case 'E': return {{0,0,0,1},{0,1,0.9f,1},{0,0.5f,0.7f,0.5f},{0,0,0.9f,0}};
    case 'F': return {{0,0,0,1},{0,1,0.9f,1},{0,0.5f,0.7f,0.5f}};
    case 'G': return {{0.85f,0.9f,0.7f,1},{0.7f,1,0.15f,1},{0.15f,1,0,0.8f},{0,0.8f,0,0.2f},{0,0.2f,0.15f,0},{0.15f,0,0.7f,0},{0.7f,0,0.85f,0.1f},{0.85f,0.1f,0.85f,0.4f},{0.85f,0.4f,0.5f,0.4f}};
    case 'H': return {{0,0,0,1},{1,0,1,1},{0,0.5f,1,0.5f}};
    case 'I': return {{0,1,1,1},{0.5f,1,0.5f,0},{0,0,1,0}};
    case 'J': return {{0,1,1,1},{0.5f,1,0.5f,0.2f},{0.5f,0.2f,0.35f,0},{0.35f,0,0.1f,0}};
    case 'K': return {{0,0,0,1},{0,0.5f,1,1},{0,0.5f,1,0}};
    case 'L': return {{0,1,0,0},{0,0,0.9f,0}};
    case 'M': return {{0,0,0,1},{0,1,0.5f,0.4f},{0.5f,0.4f,1,1},{1,1,1,0}};
    case 'N': return {{0,0,0,1},{0,1,1,0},{1,0,1,1}};
    case 'O': return {{0.15f,0,0.75f,0},{0.75f,0,0.9f,0.2f},{0.9f,0.2f,0.9f,0.8f},{0.9f,0.8f,0.75f,1},{0.75f,1,0.15f,1},{0.15f,1,0,0.8f},{0,0.8f,0,0.2f},{0,0.2f,0.15f,0}};
    case 'P': return {{0,0,0,1},{0,1,0.8f,1},{0.8f,1,0.8f,0.55f},{0.8f,0.55f,0,0.55f}};
    case 'Q': return {{0.15f,0,0.75f,0},{0.75f,0,0.9f,0.2f},{0.9f,0.2f,0.9f,0.8f},{0.9f,0.8f,0.75f,1},{0.75f,1,0.15f,1},{0.15f,1,0,0.8f},{0,0.8f,0,0.2f},{0,0.2f,0.15f,0},{0.55f,0.25f,1,0}};
    case 'R': return {{0,0,0,1},{0,1,0.8f,1},{0.8f,1,0.8f,0.55f},{0.8f,0.55f,0,0.55f},{0,0.55f,0.9f,0}};
    case 'S': return {{0.85f,0.85f,0.7f,1},{0.7f,1,0.15f,1},{0.15f,1,0,0.8f},{0,0.8f,0.8f,0.2f},{0.8f,0.2f,0.7f,0},{0.7f,0,0.1f,0}};
    case 'T': return {{0,1,1,1},{0.5f,1,0.5f,0}};
    case 'U': return {{0,1,0,0.2f},{0,0.2f,0.15f,0},{0.15f,0,0.85f,0},{0.85f,0,1,0.2f},{1,0.2f,1,1}};
    case 'V': return {{0,1,0.5f,0},{0.5f,0,1,1}};
    case 'W': return {{0,1,0.2f,0},{0.2f,0,0.5f,0.6f},{0.5f,0.6f,0.8f,0},{0.8f,0,1,1}};
    case 'X': return {{0,0,1,1},{0,1,1,0}};
    case 'Y': return {{0,1,0.5f,0.55f},{1,1,0.5f,0.55f},{0.5f,0.55f,0.5f,0}};
    case 'Z': return {{0,1,1,1},{1,1,0,0},{0,0,1,0}};
    case '0': return {{0.15f,0,0.75f,0},{0.75f,0,0.9f,0.2f},{0.9f,0.2f,0.9f,0.8f},{0.9f,0.8f,0.75f,1},{0.75f,1,0.15f,1},{0.15f,1,0,0.8f},{0,0.8f,0,0.2f},{0,0.2f,0.15f,0},{0.2f,0.2f,0.7f,0.8f}};
    case '1': return {{0.2f,0.75f,0.5f,1},{0.5f,1,0.5f,0},{0.2f,0,0.8f,0}};
    case '2': return {{0.1f,0.8f,0.25f,1},{0.25f,1,0.75f,1},{0.75f,1,0.9f,0.8f},{0.9f,0.8f,0.1f,0},{0.1f,0,0.9f,0}};
    case '3': return {{0.1f,1,0.85f,1},{0.85f,1,0.55f,0.5f},{0.55f,0.5f,0.85f,0},{0.85f,0,0.1f,0},{0.55f,0.5f,0.2f,0.5f}};
    case '4': return {{0.75f,0,0.75f,1},{0.05f,0.4f,0.95f,0.4f},{0.05f,0.4f,0.6f,1}};
    case '5': return {{0.9f,1,0.15f,1},{0.15f,1,0.15f,0.5f},{0.15f,0.5f,0.7f,0.5f},{0.7f,0.5f,0.9f,0.3f},{0.9f,0.3f,0.75f,0},{0.75f,0,0.15f,0}};
    case '6': return {{0.8f,1,0.2f,0.6f},{0.2f,0.6f,0.1f,0.2f},{0.1f,0.2f,0.25f,0},{0.25f,0,0.75f,0},{0.75f,0,0.9f,0.2f},{0.9f,0.2f,0.75f,0.5f},{0.75f,0.5f,0.2f,0.5f}};
    case '7': return {{0.1f,1,0.9f,1},{0.9f,1,0.3f,0}};
    case '8': return {{0.2f,0.5f,0.75f,0.5f},{0.15f,0,0.75f,0},{0.75f,0,0.9f,0.2f},{0.9f,0.2f,0.75f,0.5f},{0.75f,0.5f,0.9f,0.8f},{0.9f,0.8f,0.75f,1},{0.75f,1,0.15f,1},{0.15f,1,0,0.8f},{0,0.8f,0.15f,0.5f},{0.15f,0.5f,0,0.2f},{0,0.2f,0.15f,0}};
    case '9': return {{0.8f,0.4f,0.25f,0.4f},{0.25f,0.4f,0.1f,0.6f},{0.1f,0.6f,0.25f,1},{0.25f,1,0.75f,1},{0.75f,1,0.9f,0.8f},{0.9f,0.8f,0.8f,0.4f},{0.8f,0.4f,0.75f,0}};
    case '.': return {{0.42f,0,0.58f,0}};
    case '-': return {{0.2f,0.5f,0.8f,0.5f}};
    case '+': return {{0.2f,0.5f,0.8f,0.5f},{0.5f,0.2f,0.5f,0.8f}};
    case '/': return {{0.1f,0,0.9f,1}};
    case '(': return {{0.65f,1,0.35f,0.8f},{0.35f,0.8f,0.25f,0.5f},{0.25f,0.5f,0.35f,0.2f},{0.35f,0.2f,0.65f,0}};
    case ')': return {{0.35f,1,0.65f,0.8f},{0.65f,0.8f,0.75f,0.5f},{0.75f,0.5f,0.65f,0.2f},{0.65f,0.2f,0.35f,0}};
    case ':': return {{0.48f,0.75f,0.52f,0.75f},{0.48f,0.2f,0.52f,0.2f}};
    case ',': return {{0.52f,0.1f,0.45f,-0.08f}};
    case '<': return {{0.8f,0.9f,0.2f,0.5f},{0.2f,0.5f,0.8f,0.1f}};
    case '>': return {{0.2f,0.9f,0.8f,0.5f},{0.8f,0.5f,0.2f,0.1f}};
    case '=': return {{0.2f,0.65f,0.8f,0.65f},{0.2f,0.35f,0.8f,0.35f}};
    case '"': return {{0.35f,1,0.35f,0.7f},{0.65f,1,0.65f,0.7f}};
    default: return ch == ' ' ? std::vector<StrokeSeg>{} : std::vector<StrokeSeg>{{0,0,1,0},{1,0,1,1},{1,1,0,1},{0,1,0,0}};
    }
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
            static_cast<float>(base.a / 255.0)};
}

bool projectWorldPoint(Vec3 p, const CameraFrame& cf, int width, int height, double focal, double nearPlane, const Camera& camera, SDL_FPoint& out) {
    return projectPoint(toCamera(p, cf), width, height, focal, nearPlane, camera, out);
}

void renderProjectedPolyline(SDL_Renderer* renderer,
                             const std::vector<Vec3>& points,
                             qifv::Color color,
                             const CameraFrame& cf,
                             int width,
                             int height,
                             double focal,
                             double nearPlane,
                             const Camera& camera) {
    if (points.size() < 2) return;
    std::vector<SDL_FPoint> projected;
    projected.reserve(points.size());
    for (auto p : points) {
        SDL_FPoint q{};
        if (!projectWorldPoint(p, cf, width, height, focal, nearPlane, camera, q)) return;
        projected.push_back(q);
    }
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderLines(renderer, projected.data(), static_cast<int>(projected.size()));
}

void renderStrokeText(SDL_Renderer* renderer,
                      const qifv::AnnotationText& text,
                      const CameraFrame& cf,
                      int width,
                      int height,
                      double focal,
                      double nearPlane,
                      const Camera& camera) {
    SDL_SetRenderDrawColor(renderer, text.color.r, text.color.g, text.color.b, text.color.a);
    const double heightScale = text.lineHeight;
    const double charAdvance = 0.78 * heightScale;
    const double lineAdvance = 1.35 * heightScale;
    double xCursor = 0.0;
    double yCursor = 0.0;
    for (char ch : text.text) {
        if (ch == '\n') { xCursor = 0.0; yCursor -= lineAdvance; continue; }
        const auto glyph = strokeGlyph(ch);
        for (const auto& seg : glyph) {
            const Vec3 a = text.origin + text.right * ((xCursor + seg.x1 * 0.75) * heightScale) + text.up * ((yCursor + seg.y1) * heightScale);
            const Vec3 b = text.origin + text.right * ((xCursor + seg.x2 * 0.75) * heightScale) + text.up * ((yCursor + seg.y2) * heightScale);
            SDL_FPoint pa{}, pb{};
            if (!projectWorldPoint(a, cf, width, height, focal, nearPlane, camera, pa) ||
                !projectWorldPoint(b, cf, width, height, focal, nearPlane, camera, pb)) continue;
            SDL_FPoint pts[2]{pa, pb};
            SDL_RenderLines(renderer, pts, 2);
        }
        xCursor += charAdvance;
    }
}

void renderModel(SDL_Renderer* renderer,
                 const QifMesh& mesh,
                 const Camera& camera,
                 bool filled,
                 bool wireframe,
                 bool pointsVisible,
                 bool annotationsVisible) {
    int width = 0, height = 0;
    if (!SDL_GetRenderOutputSize(renderer, &width, &height) || width <= 1 || height <= 1) return;

    const CameraFrame cf = frameFor(camera);
    const qifv::SavedView* savedView = activeSavedView(camera, mesh);
    const double focal = 0.5 * static_cast<double>(std::min(width, height)) / std::tan(45.0 * 3.14159265358979323846 / 360.0);
    const double nearPlane = std::max(camera.distance * 0.01, 1e-9);

    if (filled) {
        std::vector<ProjectedTriangle> projected;
        projected.reserve(mesh.triangles.size());
        for (const auto& t : mesh.triangles) {
            if (!primitiveVisibleInSavedView(t.bodyId, t.componentPath, savedView)) continue;
            const Vec3 a = toCamera(t.a, cf);
            const Vec3 b = toCamera(t.b, cf);
            const Vec3 c = toCamera(t.c, cf);
            SDL_FPoint pa{}, pb{}, pc{};
            if (!projectPoint(a, width, height, focal, nearPlane, camera, pa) ||
                !projectPoint(b, width, height, focal, nearPlane, camera, pb) ||
                !projectPoint(c, width, height, focal, nearPlane, camera, pc)) continue;

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
        std::vector<SDL_FPoint> projectedLine;
        for (const auto& line : mesh.edges) {
            if (!primitiveVisibleInSavedView(line.bodyId, line.componentPath, savedView)) continue;
            SDL_SetRenderDrawColor(renderer, line.color.r, line.color.g, line.color.b, line.color.a);
            projectedLine.clear();
            projectedLine.reserve(line.points.size());
            bool visible = true;
            for (auto p : line.points) {
                SDL_FPoint q{};
                if (!projectPoint(toCamera(p, cf), width, height, focal, nearPlane, camera, q)) {
                    visible = false;
                    break;
                }
                projectedLine.push_back(q);
            }
            if (visible && projectedLine.size() >= 2) SDL_RenderLines(renderer, projectedLine.data(), static_cast<int>(projectedLine.size()));
        }
    }

    if (pointsVisible) {
        for (const auto& point : mesh.points) {
            if (!primitiveVisibleInSavedView(point.bodyId, point.componentPath, savedView)) continue;
            SDL_FPoint q{};
            if (!projectPoint(toCamera(point.point, cf), width, height, focal, nearPlane, camera, q)) continue;
            SDL_SetRenderDrawColor(renderer, point.color.r, point.color.g, point.color.b, point.color.a);
            const float r = static_cast<float>(std::clamp(point.size, 1.0, 6.0));
            SDL_FPoint h[2]{{q.x-r,q.y},{q.x+r,q.y}};
            SDL_FPoint v[2]{{q.x,q.y-r},{q.x,q.y+r}};
            SDL_RenderLines(renderer,h,2);
            SDL_RenderLines(renderer,v,2);
        }
    }

    if (annotationsVisible) {
        for (const auto& poly : mesh.annotationPolylines) {
            if (!annotationVisibleInSavedView(poly.sourceId, savedView)) continue;
            renderProjectedPolyline(renderer, poly.points, poly.color, cf, width, height, focal, nearPlane, camera);
        }
        for (const auto& text : mesh.annotationTexts) {
            if (!annotationVisibleInSavedView(text.sourceId, savedView)) continue;
            renderStrokeText(renderer, text, cf, width, height, focal, nearPlane, camera);
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
    if (!mesh.annotationTexts.empty() || !mesh.annotationPolylines.empty()) {
        title += " / " + std::to_string(mesh.annotationTexts.size()) + " PMI text";
        title += " / " + std::to_string(mesh.annotationPolylines.size()) + " PMI line";
    }
    SDL_SetWindowTitle(window, title.c_str());
}

bool loadFile(const std::string& path, QifMesh& mesh, Camera& camera, const qifv::LoadOptions& requested) {
    qifv::LoadOptions opts = requested;
    std::string error;
    QifMesh next;
    std::cerr << "Loading " << path << " ...\n";
    if (!qifv::QifLoader::load(path, next, error, opts)) {
        std::cerr << "QIF load failed: " << error << "\n";
        return false;
    }
    mesh = std::move(next);
    resetCamera(camera, mesh);
    activateInitialSavedView(camera, mesh);
    std::cerr << "QIF " << mesh.qifVersion << ", unit=" << (mesh.linearUnit.empty() ? "unknown" : mesh.linearUnit)
              << ", faces=" << mesh.faceCount
              << ", triangles=" << mesh.triangles.size()
              << ", edge/wire polylines=" << mesh.edges.size()
              << ", points=" << mesh.points.size()
              << ", PMI text=" << mesh.annotationTexts.size()
              << ", PMI linework=" << mesh.annotationPolylines.size()
              << ", transforms=" << mesh.diagnostics.transformCount
              << ", component instances=" << mesh.diagnostics.componentInstances
              << ", tessellation tolerance=" << mesh.tessellationTolerance << "\n";
    if (mesh.skippedFaces) std::cerr << "Warning: skipped " << mesh.skippedFaces << " unsupported/invalid faces.\n";
    return true;
}

void printUsage(const char* exe) {
    std::cout
        << "Usage: " << exe << " [--renderer auto|vulkan|metal|opengl|software] [--tolerance N] [--show-hidden] [--strict] file.qif\n\n"
        << "Controls:\n"
        << "  Left drag        Orbit\n"
        << "  Right drag       Pan\n"
        << "  Mouse wheel      Zoom\n"
        << "  F                Toggle shaded faces\n"
        << "  W                Toggle wireframe edges\n"
        << "  P                Toggle points / point clouds\n"
        << "  R                Reset to fitted orbit view\n"
        << "  V                Cycle QIF saved views/cameras\n"
        << "  1 / 2 / 3        Front / side / top view\n"
        << "  Esc              Quit\n"
        << "  Drag a .qif file onto the window to open another file\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string rendererName;
    std::string path;
    qifv::LoadOptions loadOptions;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--renderer" && i + 1 < argc) {
            rendererName = argv[++i];
            if (rendererName == "auto") rendererName.clear();
        } else if (a == "--tolerance" && i + 1 < argc) {
            loadOptions.tessellationTolerance = std::strtod(argv[++i], nullptr);
        } else if (a == "--show-hidden") {
            loadOptions.includeHidden = true;
        } else if (a == "--strict") {
            loadOptions.strictUnsupported = true;
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
    bool pointsVisible = true;
    bool annotationsVisible = true;
    bool leftDown = false;
    bool rightDown = false;
    bool running = true;

    if (!path.empty()) {
        if (loadFile(path, mesh, camera, loadOptions)) setTitle(window, path, mesh, renderer);
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
                    leaveSavedViewForOrbit(camera, mesh);
                    camera.yaw -= static_cast<double>(ev.motion.xrel) * 0.007;
                    camera.pitch += static_cast<double>(ev.motion.yrel) * 0.007;
                    camera.pitch = std::clamp(camera.pitch, -1.53, 1.53);
                }
                if (rightDown && mesh.bounds.valid) {
                    int w = 1, h = 1;
                    SDL_GetRenderOutputSize(renderer, &w, &h);
                    const CameraFrame cf = frameFor(camera);
                    if (camera.qifSavedCamera) {
                        const double worldPerPixel = (2.0 * camera.qifHalfHeight) / std::max(1, h);
                        camera.qifFrame.pos = camera.qifFrame.pos
                            - cf.right * (static_cast<double>(ev.motion.xrel) * worldPerPixel)
                            + cf.up * (static_cast<double>(ev.motion.yrel) * worldPerPixel);
                    } else {
                        const double worldPerPixel = camera.distance / std::max(1, std::min(w, h));
                        camera.target = camera.target - cf.right * (static_cast<double>(ev.motion.xrel) * worldPerPixel)
                                                     + cf.up * (static_cast<double>(ev.motion.yrel) * worldPerPixel);
                    }
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (camera.qifSavedCamera) {
                    camera.qifHalfHeight *= std::exp(-static_cast<double>(ev.wheel.y) * 0.12);
                    const double base = std::max(mesh.bounds.diagonal(), 1.0);
                    camera.qifHalfHeight = std::clamp(camera.qifHalfHeight, base * 1e-5, base * 1e4);
                } else {
                    camera.distance *= std::exp(-static_cast<double>(ev.wheel.y) * 0.12);
                    camera.distance = std::clamp(camera.distance, camera.homeDistance * 0.015, camera.homeDistance * 100.0);
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (ev.key.repeat) break;
                if (ev.key.key == SDLK_ESCAPE) running = false;
                else if (ev.key.key == SDLK_F) filled = !filled;
                else if (ev.key.key == SDLK_W) wireframe = !wireframe;
                else if (ev.key.key == SDLK_P) pointsVisible = !pointsVisible;
                else if (ev.key.key == SDLK_A) annotationsVisible = !annotationsVisible;
                else if (ev.key.key == SDLK_R) resetCamera(camera, mesh);
                else if (ev.key.key == SDLK_V) cycleSavedView(camera, mesh);
                else if (ev.key.key == SDLK_1) { leaveSavedViewForOrbit(camera, mesh); camera.yaw = 0.0; camera.pitch = 0.0; }
                else if (ev.key.key == SDLK_2) { leaveSavedViewForOrbit(camera, mesh); camera.yaw = 1.5707963267948966; camera.pitch = 0.0; }
                else if (ev.key.key == SDLK_3) { leaveSavedViewForOrbit(camera, mesh); camera.yaw = 0.0; camera.pitch = 1.52; }
                break;
            case SDL_EVENT_DROP_FILE:
                if (ev.drop.data && loadFile(ev.drop.data, mesh, camera, loadOptions)) {
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
        if (mesh.bounds.valid) renderModel(renderer, mesh, camera, filled, wireframe, pointsVisible, annotationsVisible);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
