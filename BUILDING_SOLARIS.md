# Building QIF Viewer on Solaris / SPARC

QIF Viewer is C++17 and keeps the geometry/parser code independent of CPU endianness. QIF binary arrays are defined as little-endian and are decoded byte-by-byte, so a big-endian SPARC host does not reinterpret them as native structs.

## Vulkan status on Solaris

Solaris is not inherently excluded from Vulkan. The practical requirements are a working Vulkan user-space stack on Solaris — a Vulkan loader plus a usable ICD/driver — and X11 WSI support. A Mesa build configured with an appropriate Vulkan driver is one way to provide that stack.

There is, however, an SDL3 build-system gate to account for. As of September 17, 2026, current SDL `main` still:

- sets `SDL_VULKAN_DEFAULT` to `OFF` when `SOLARIS` is true; and
- omits `SOLARIS` from the `SDL_VULKAN` dependency allow-list.

The same platform allow-list issue exists in the SDL 3.4.x line used by the default QIF Viewer fetch. QIF Viewer therefore applies `cmake/PatchSDLSolarisVulkan.cmake` to a **fetched** SDL source tree on Solaris. The patch adds only `SOLARIS` to SDL's Vulkan platform condition and then QIF Viewer explicitly sets:

```text
SDL_X11=ON
SDL_VULKAN=ON
SDL_RENDER_VULKAN=ON
```

If an SDL version already includes Solaris in that allow-list, the patch is a no-op. If SDL's CMake expression changes in a way the patch does not recognize, configuration stops with an error rather than silently disabling Vulkan.

This is compatible with an upstream Solaris Vulkan change: once upstream SDL contains the equivalent platform support, no source modification is made.

## Verify SDL before building

For a separate SDL source tree:

```sh
python3 tools/verify_sdl_solaris_vulkan.py /path/to/SDL
```

A PASS verifies SDL's **CMake configuration path**, not the driver. Runtime still requires the Vulkan loader/ICD and X11 WSI to work on the machine.

Useful system-level checks depend on your Vulkan packaging. At minimum, confirm that a Vulkan loader is resolvable and that the Mesa/driver ICD JSON and driver library are installed in the paths expected by your loader. If your stack supplies `vulkaninfo`, use it before testing SDL.

## Recommended fetched-dependency build

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DQIFVIEWER_FETCH_DEPS=ON \
  -DQIFVIEWER_SOLARIS_SDL_VULKAN_PATCH=ON
cmake --build build -j
```

On Solaris this path fetches SDL, patches the Vulkan allow-list if necessary, explicitly requests X11/Vulkan/Vulkan-renderer support, and fails configuration if SDL does not leave `SDL_VULKAN` and `SDL_RENDER_VULKAN` enabled.

Run:

```sh
./build/qifviewer --renderer vulkan part.qif
```

If SDL reports that the Vulkan renderer is unavailable, inspect the actual SDL configure summary and the system Vulkan loader/ICD. Do not assume the QIF Viewer flag itself creates Vulkan support.

## Package-managed / offline SDL

With:

```sh
cmake -S . -B build \
  -DQIFVIEWER_FETCH_DEPS=OFF \
  -DQIFVIEWER_EARCUT_INCLUDE_DIR=/path/to/earcut/include
```

QIF Viewer calls `find_package(SDL3)`. In this mode the project does **not** patch SDL source because it is already built. Build/install SDL3 first with Solaris Vulkan/X11 support enabled, then point CMake at that SDL package.

The remaining dependencies are pugixml and mapbox/earcut.hpp. They can also be supplied locally to avoid network access.

## Non-Vulkan fallback

OpenGL/software remain useful fallback renderers:

```sh
./build/qifviewer --renderer opengl part.qif
./build/qifviewer --renderer software part.qif
```

The QIF parser, tessellator, conformance tools, and `qifcheck` do not depend on Vulkan.

## SPARC notes

- C++ code does not assume little-endian host order for QIF binary arrays.
- Avoid replacing the explicit binary decoder with pointer casts or packed structs.
- `qifcheck` can be built/used without a graphical session by configuring `-DQIFVIEWER_BUILD_GUI=OFF`.
- For a first Solaris/SPARC bring-up, build `qifcheck` first, run the conformance fixtures, then enable SDL/GUI.

Example headless bring-up:

```sh
cmake -S . -B build-headless \
  -DQIFVIEWER_BUILD_GUI=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-headless -j
ctest --test-dir build-headless --output-on-failure
```
