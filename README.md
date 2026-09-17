# QIF Viewer 0.5.0

Cross-platform C++17 **QIF 3.0 reader, conformance checker, and 3D product-geometry viewer**. The implementation is based on the ANSI/DMSC QIF 3.0-2018 specification and the complete QIF 3.0.0 XSD/XSLT package bundled under `spec/qif3/xsd`.

Version 0.5 separates three different notions of completeness that should not be confused:

1. **QIF document acceptance/conformance:** the complete QIF 3.0 schema package is reachable from `QIFApplications/QIFDocument.xsd`; `qifvalidate --full` performs root-XSD validation, the official DMSC `Check.xsl` checks, list-count checks, package auditing, and local external-QIF reference verification.
2. **Reader retention:** `qifcheck` can inspect QIF documents even when they contain no renderable product shape. With `--tree`, every XML element, attribute, and value is retained in a schema-neutral tree, so Plans, Resources, Rules, Results, Statistics, traceability, signatures, user data, and other legal QIF sections are not discarded.
3. **3D semantic rendering:** the typed renderer covers the complete concrete Geometry/Topology entity inventory from the QIF 3.0 schemas. Rich PMI/display semantics are still a separate presentation layer and are not claimed complete merely because their XML validates and is retained.

## QIF geometry and topology

`tools/schema_coverage.py` reads the official QIF 3.0 schemas and verifies the viewer inventory. Current results are **28/28 concrete Geometry entities**, **9/9 concrete Topology entities**, and **25/25 geometry evaluator core branches**.

Implemented geometry includes all QIF 3.0 2D and 3D curve families (`Segment`, `Polyline`, circular/conic arcs, `Nurbs`, `Spline`, `Aggregate`), all parametric surface families (`Plane`, `Cylinder`, `Cone`, `Sphere`, `Torus`, `Nurbs`, `Spline`, `Extrude`, `Ruled`, `Revolution`, `Offset`), and the discrete geometry types (`Point`, `MeshTriangle`, `PathTriangulation`).

Topology support covers `Vertex`, `Edge`, `Loop`, `LoopMesh`, `Face`, `FaceMesh`, `Shell`, `Body`, and `PointCloud`, including face/shell orientation, `hasOuter=false`, inner/slit/vertex loops, mesh-face subsets, lower-dimensional bodies, tolerant topology as display geometry, point-cloud visibility/color data, and binary arrays.

## Product structure, transforms, and units

The viewer traverses QIF `Part`, `Assembly`, and `Component` relationships from the Product root, composes nested QIF transforms, and supports repeated component instances. QIF transform matrices, linear/angular unit handling used by geometry, explicit little-endian QIF binary-array decoding, and SPARC-safe reconstruction are implemented without assuming host endianness.

## Saved views and visualization

QIF `Camera` and `SavedView` records are parsed. Active saved views are selected automatically and **V** cycles saved cameras. Both orthographic and perspective QIF camera forms are supported, including the QIF quaternion, view-plane origin, ratio, height, and near/far values. Saved-view body/component visibility filtering is applied to rendered product geometry.

The viewer now also renders the practical PMI presentation layer from `Visualization.xsd`: annotation-plane text strings, leaders (including extended leaders and arrowheads), witness lines, common frame outlines, and additional 2D graphics/polyline overlays. Saved-view `AnnotationVisibleIds` / `AnnotationHiddenIds` filtering is applied to these PMI overlays, and **A** toggles PMI visibility in the GUI. The text renderer intentionally uses a built-in stroke font so the viewer remains SDL-only and still builds cleanly on Solaris and other minimal platforms.

`DisplayStyle`, `SimplifiedRepresentation`, `ExplodedView`, and `ZoneSection` records are still retained and inspectable, but they are not yet graphically applied as full style/clipping semantics. See `FULL_SPEC_ROADMAP.md` for the exact boundary.

## Full QIF 3.0 conformance checker

The bundled package contains all **23 QIF 3.0 XSD files** reachable from the root schema, with **1,058 global elements, 2,046 complex types, and 168 simple types** in the package audit. The official DMSC check suite is also bundled.

Install Python `lxml`, then run:

```sh
python3 tools/validate_qif.py --full part.qif
```

or, after installation:

```sh
qifvalidate --full part.qif
```

`--full` performs:

- complete `QIFDocument.xsd` validation with offline resolution of the bundled XMLDSIG schema;
- package dependency audit across all QIF XSDs and official check stylesheets;
- `n` list-cardinality checks;
- the unmodified official DMSC `Check/Check.xsl` suite (the supplied stylesheets declare XSLT 1.0, so Python `lxml`/libxslt is sufficient);
- local/file external-QIF resolution, target schema/check validation, QPId verification, `xId` verification, cycle detection, and recursion limits.

HTTP/HTTPS external references are intentionally not fetched automatically. Manufacturing/QIF validation should not silently perform network access.

The official quality checks are intentionally reported separately from XSD validity. A document can be XSD-valid while still receiving a QIF quality finding such as a free shell edge.

## Full-document inspection

`qifcheck` no longer requires a document to contain renderable geometry. This is important because legal QIF documents can contain application data such as Plans, Rules, Results, or Statistics without a Product shape.

```sh
./build/qifcheck --all-elements file.qif
./build/qifcheck --tree --tree-depth 5 file.qif
```

`--tree` retains and prints the complete QIF document tree. This is the schema-complete fallback object model: application data not yet represented by a specialized C++ convenience type remains available instead of being lost.

## SDL3, Vulkan, Metal, and Solaris

The QIF parser/tessellator is platform-neutral C++17. SDL3 provides windowing/input and the final portable renderer layer.

- macOS: `--renderer metal`
- Windows/Linux: `--renderer vulkan` when the installed SDL/driver supports it
- FreeBSD/OpenBSD and other supported BSD configurations: Vulkan where the graphics stack/SDL build provides it, otherwise OpenGL/software
- Solaris: Vulkan is supported when the Solaris graphics stack supplies a working Vulkan loader/ICD with X11 WSI; a Mesa build providing Vulkan is one way to supply that stack

There is an SDL-specific complication. As of September 17, 2026, upstream SDL `main` still sets the Vulkan default off for Solaris and omits `SOLARIS` from the `SDL_VULKAN` CMake platform allow-list. QIF Viewer therefore patches **only that CMake platform gate** when it fetches SDL on Solaris, then explicitly requests `SDL_X11=ON`, `SDL_VULKAN=ON`, and `SDL_RENDER_VULKAN=ON`. If a future SDL release (including a Solaris Vulkan upstream change) already includes `SOLARIS`, the patch detects it and becomes a no-op. If SDL changes the gate in an unrecognized way, configuration fails instead of silently producing a non-Vulkan build.

To verify an SDL source tree:

```sh
python3 tools/verify_sdl_solaris_vulkan.py /path/to/SDL
```

See `BUILDING_SOLARIS.md` for the exact Solaris workflow.

**Renderer architecture note:** QIF Viewer currently CPU-projects its tessellated geometry and sends triangles to `SDL_RenderGeometry`. Selecting SDL's Vulkan or Metal renderer therefore uses those real SDL backends, but it is not yet a native Vulkan/Metal CAD renderer with a hardware depth buffer. A later native GPU path can improve depth testing, large-model batching, picking, clipping, and transparency without changing the QIF reader.

## Dependencies

The default CMake build fetches pinned versions of:

- SDL 3.4.16 (configurable with `QIFVIEWER_SDL_GIT_TAG`)
- pugixml 1.16
- mapbox/earcut.hpp 2.2.4

CMake 3.20+ and a C++17 compiler are required. `qifvalidate --full` additionally needs Python 3 and `lxml`.

For an offline/package-managed build, use `-DQIFVIEWER_FETCH_DEPS=OFF` after supplying SDL3, pugixml, and earcut. On Solaris, a package-managed SDL3 must itself have been configured with its Vulkan renderer enabled; QIF Viewer cannot retroactively patch an already-built SDL library.

## Build

Convenience wrappers are included for all primary platforms:

```sh
./build.sh
./build/qifviewer part.qif
```

On Windows:

```bat
build.bat
build\qifviewer.exe part.qif
```

Both scripts forward extra arguments to the CMake configure step, so options such as `-DQIFVIEWER_FETCH_DEPS=OFF` can be appended directly. `BUILD_DIR`, `BUILD_TYPE`, `CMAKE_GENERATOR`, and `JOBS` can also be set in the environment.

The equivalent manual CMake build remains:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/qifviewer part.qif
```

Metal on macOS:

```sh
./build/qifviewer --renderer metal part.qif
```

Vulkan where available:

```sh
./build/qifviewer --renderer vulkan part.qif
```

Headless reader/checker:

```sh
./build/qifcheck part.qif
./build/qifcheck --strict --all-elements part.qif
./build/qifcheck --tree --tree-depth 4 part.qif
```

Viewer controls: left drag orbit, right drag pan, wheel zoom, **F** filled, **W** wireframe, **P** points, **A** PMI/annotations, **R** reset, **V** next QIF saved view, **1/2/3** standard views, **Esc** quit. QIF files can also be dropped onto the window.

## Testing

The `tests/` directory is optional. CMake will configure and build normally if it is omitted from a release/source package, even when `BUILD_TESTING` is left enabled. When the directory is present, available test files are registered individually.

```sh
ctest --test-dir build --output-on-failure
python3 tools/full_spec_audit.py
python3 tools/schema_coverage.py
python3 tests/sdl_solaris_vulkan_patch_test.py
```

The full test suite includes schema-package reachability, full-conformance fixtures, local external-QIF resolution, camera/quaternion math, geometry/topology inventory checks, and regression tests for the Solaris SDL Vulkan CMake patch.

## License

Original QIF Viewer source in this project is released under **The Unlicense** (`SPDX-License-Identifier: Unlicense`). `UNLICENSE` and `LICENSE` contain the project license text.

Third-party dependencies and the bundled QIF standard/schema/check resources are **not relicensed** by this project. Their original copyright/license/permission notices remain applicable.
