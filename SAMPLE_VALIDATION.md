# Validation and regression status

QIF Viewer 0.4.0 uses several independent checks because no single test proves complete QIF behavior.

## Package/inventory audit

```text
tools/full_spec_audit.py
  Reachable XSD files: 23/23
  Global declarations: elements=1058 complexTypes=2046 simpleTypes=168
  Reachable official XSLT files: 6
  PASS

tools/schema_coverage.py
  Geometry: 28/28
  Topology: 9/9
  Geometry evaluator core branches: 25/25
  PASS
```

## Full-conformance fixtures

`tests/conformance/minimal.qif` is a valid QIF document with no product geometry. It verifies that QIF conformance is not incorrectly tied to the presence of a 3D model.

`tests/conformance/external-source.qif` references `external-target.qif` using a local external-QIF reference. `qifvalidate --full` validates the target, verifies its QPId and `xId`, and resolves it without network access.

Both fixtures pass the full validation stack.

## Six NIST product samples

The six NIST samples used throughout development remain XSD-valid and retain their existing geometry rendering coverage. Five pass the official DMSC checks with no reported errors. `nist_ftc_08_asme1_ap242-1.qif` is XSD-valid but the official QIF quality stylesheet reports four `G-SH-FR` free-edge findings (edge ids 2884, 2888, 2892 and 2894).

Those four findings are deliberately **not** treated as parser failures. They demonstrate the distinction between:

```text
schema validity
!= official QIF quality findings
!= viewer implementation errors
```

## SDL Solaris Vulkan regression test

`tests/sdl_solaris_vulkan_patch_test.py` constructs the known SDL 3.x Vulkan platform-gate variants, applies `cmake/PatchSDLSolarisVulkan.cmake`, and then runs `tools/verify_sdl_solaris_vulkan.py`. It also tests an already-Solaris-enabled allow-list to ensure the patch is idempotent.

This test validates build-system logic only. It cannot prove a specific Solaris machine has a working Vulkan loader/ICD/WSI stack.

## C++ build validation in this package-generation environment

The source has been syntax-checked as C++17 with warnings enabled using local header stubs for pugixml, SDL3 and earcut. CMake is also configured against synthetic dependency targets to catch project-level CMake errors.

The package-generation environment cannot perform a real SDL/Vulkan/Solaris/SPARC linked build because it does not contain that target graphics stack and cannot fetch/build all third-party dependencies. A real Solaris validation should therefore still run:

```sh
python3 tools/verify_sdl_solaris_vulkan.py /path/to/SDL
cmake -S . -B build ...
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/qifviewer --renderer vulkan sample.qif
```

The runtime test is the final proof that Mesa/loader/ICD/X11 WSI and SDL agree on that machine.

## Edge/wire regression fixtures

`tests/conformance/edge-only.qif` is a schema-valid edge topology file without Body/Part links, and `tests/conformance/wire-body.qif` is a schema-valid `Body form="WIRE"` model. Both use a Curve13 wrapper with `Attributes` before `Segment13Core` to exercise the core-lookup bug fixed in 0.5.1. CTest loads both with the real `qifcore` loader and requires emitted wire polylines and expected bounds.

## STEP-derived sheet regression

`tests/conformance/sheet-circle-no-pcurve.qif` is a schema-valid open SHEET body/open Shell with a planar circular face whose four CoEdges intentionally omit optional `Curve12` p-curves. `qif_sheet_surface_load_test` verifies that the viewer reconstructs the trimming loop from the referenced 3D ArcCircular13 edges and emits a filled face rather than only wire geometry.
