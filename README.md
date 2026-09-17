# QIF Viewer

A small cross-platform C++17 viewer for the 3D product geometry embedded in QIF 3.x files.

This implementation was built against the six NIST QIF samples supplied with the request. Those files contain boundary-representation (B-rep) geometry rather than an STL-style triangle mesh, so the program reads each face's QIF surface plus its 2D trimming curves, triangulates the trimmed UV region, and evaluates the corresponding 3D surface.

## What it supports

- QIF 3.x XML loading.
- Trimmed B-rep faces and inner loops/holes.
- `Plane23`, `Cylinder23`, `Cone23`, `Sphere23`, `Torus23`, and `Nurbs23` surfaces.
- `Segment12`, `Nurbs12`, and `Aggregate12` trimming curves.
- Adaptive tessellation of curved surfaces.
- Shaded faces plus QIF boundary/wireframe display.
- Mouse orbit, pan, zoom, front/side/top views, reset, and drag-and-drop opening.
- SDL3 rendering backends, including **Metal** and **Vulkan**, with OpenGL/software fallbacks.
- A headless `qifcheck` utility that exercises the same parser/tessellator without opening a window.

## Platform strategy

The QIF parser and tessellator are platform-neutral C++17. The window/input/render-output layer uses SDL3.

- **macOS:** use `--renderer metal` for the native Metal renderer.
- **Windows:** use `--renderer vulkan` when a Vulkan runtime/driver is installed; `auto` can choose another native SDL renderer when Vulkan is unavailable.
- **Linux:** use `--renderer vulkan` with a Vulkan driver; OpenGL/software are fallbacks.
- **FreeBSD / NetBSD / OpenBSD:** the source is intended to build with SDL3. Vulkan availability depends on the particular OS, GPU, and driver; otherwise use OpenGL/software.
- **Solaris:** the source is intended to build with SDL3 and keeps the QIF code free of OS-specific APIs. Vulkan is not assumed to exist on a Solaris installation; use an SDL renderer actually supplied by that SDL build, normally OpenGL or software when Vulkan is unavailable. You can enable Vulkan on Solaris/Illumos by compiling Mesa with it enabled as it is proven to work with LavaPipe.

In other words, Metal and Vulkan are supported render paths, but the application does **not** claim that every target OS has a Vulkan driver.

## Dependencies

The default CMake configuration downloads pinned versions of:

- SDL 3.4.16
- pugixml 1.16
- mapbox/earcut.hpp 2.2.4

For an offline/package-managed build, configure with `-DQIFVIEWER_FETCH_DEPS=OFF` after providing SDL3 and pugixml CMake packages and making `mapbox/earcut.hpp` discoverable. You may also set `QIFVIEWER_EARCUT_INCLUDE_DIR` explicitly.

Requirements: CMake 3.20+, a C++17 compiler, and the normal development libraries required by SDL for the selected platform/window system.

## Build

### macOS / Linux / BSD / Solaris-style shells

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Open a file using the default renderer:

```sh
./build/qifviewer part.qif
```

Force Metal on macOS:

```sh
./build/qifviewer --renderer metal part.qif
```

Force Vulkan where the SDL build and installed GPU driver provide it:

```sh
./build/qifviewer --renderer vulkan part.qif
```

If a forced backend is unavailable, the program prints the renderer names compiled into the SDL build.

### Windows (Visual Studio generator)

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\qifviewer.exe --renderer vulkan part.qif
```

## Headless parser check

```sh
./build/qifcheck part.qif
./build/qifcheck --tolerance 0.002 part1.qif part2.qif
```

This reports the QIF version, units, face count, skipped-face count, tessellated triangle count, edge-loop count, tolerance, and 3D bounds.

## Viewer controls

| Control | Action |
|---|---|
| Left drag | Orbit |
| Right drag | Pan |
| Mouse wheel | Zoom |
| F | Toggle shaded faces |
| W | Toggle QIF boundary/wireframe lines |
| R | Reset view |
| 1 | Front view |
| 2 | Side view |
| 3 | Top view |
| Esc | Quit |
| Drop `.qif` file | Open that file |

## Tessellation

QIF analytic and NURBS faces are trimmed in their two-dimensional parameter space. QIF Viewer:

1. resolves the face's `Surface` and `LoopIds` references;
2. samples the referenced Curve12 geometry into UV rings;
3. triangulates the outer loop and holes with earcut;
4. maps UV vertices onto the QIF surface equation;
5. recursively subdivides curved triangles until midpoint/centroid deviation is within the tessellation tolerance.

The default tolerance is `0.00025 * model-bounds diagonal`. Override it with `--tolerance <model-units>`.

## Current scope / limitations

This is a focused geometry viewer, not yet a complete QIF metrology application. It does not currently render PMI/GD&T annotations, measurement results, feature labels, point-cloud inspection data, textures, or every possible QIF geometry subtype. Unsupported/invalid faces are counted and reported instead of aborting the whole file.

The display layer uses SDL's triangle renderer and performs the 3D transform and painter sorting in the application. This keeps the source portable across the requested operating systems and lets SDL select Metal/Vulkan/OpenGL/software, but it is intentionally simpler than a full Vulkan/Metal 3D engine with a hardware depth buffer.

## License

The QIF Viewer source in this repository is The Unlicense licensed. Dependencies retain their own licenses and are fetched separately.

## Additional notes

- `SAMPLE_VALIDATION.md` documents the geometry/topology coverage of the six supplied NIST samples.
