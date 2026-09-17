# QIF 3.0 schema coverage

## Root-schema/package coverage

QIF Viewer treats `QIFApplications/QIFDocument.xsd` as the validation root. `tools/full_spec_audit.py` follows the complete include/import graph and maps the external XMLDSIG URL import to the bundled local copy.

Current package audit:

```text
Reachable XSD files: 23/23
Global elements: 1058
Complex types: 2046
Simple types: 168
Official check root: Check/Check.xsl
Reachable official XSLT files: 6
Full QIF 3.0 schema/check package audit: PASS
```

The official stylesheets in this QIF 3.0 package declare XSLT 1.0.

## Geometry inventory

`tools/schema_coverage.py` compares the schema inventory with the C++ implementation declaration. Current result: **28/28 concrete Geometry entities**.

2D curves: `Segment12`, `Polyline12`, `ArcCircular12`, `ArcConic12`, `Nurbs12`, `Spline12`, `Aggregate12`.

3D curves: `Segment13`, `Polyline13`, `ArcCircular13`, `ArcConic13`, `Nurbs13`, `Spline13`, `Aggregate13`.

Surfaces: `Plane23`, `Cylinder23`, `Cone23`, `Sphere23`, `Torus23`, `Nurbs23`, `Spline23`, `Extrude23`, `Ruled23`, `Revolution23`, `Offset23`.

Discrete geometry: `Point`, `MeshTriangle`, `PathTriangulation`.

Evaluator core branch audit: **25/25**.

## Topology inventory

Current result: **9/9 concrete Topology entities**: `Vertex`, `Edge`, `Loop`, `LoopMesh`, `Face`, `FaceMesh`, `Shell`, `Body`, `PointCloud`.

The render path handles parametric face trimming, holes, slit/vertex loops, natural outer domains, mesh faces/loops, shells, lower-dimensional bodies, face/shell orientation, point clouds, per-primitive style/visibility data and product transforms.

## All other QIF schema content

The C++ reader can optionally retain the complete XML tree in `GenericElement`. This is the fallback representation for all legal QIF content that has no specialized render-time structure. `qifcheck` is configured not to require displayable geometry and can therefore inspect application-only QIF documents.

That means a schema type is not silently discarded merely because it belongs to Plans, Resources, Rules, Results, Statistics, Features, Characteristics, traceability, XML Signature, or application-specific metadata.

This is **reader retention coverage**, not a claim that all 2,046 complex types have distinct hand-written C++ domain classes or operational semantics.

## Validation coverage

`tools/validate_qif.py --full` combines:

- `QIFDocument.xsd` validation across the full 23-XSD graph;
- package graph audit;
- list `n` checks;
- the official DMSC QIF `Check.xsl` suite;
- local external-QIF URI/QPId/xId validation with cycle/recursion protection.

No network access is performed for validation.

## Presentation boundary

Cameras/saved views and product body/component visibility are implemented. Rich Visualization/PMI is retained and validated but not yet fully rendered; see `FULL_SPEC_ROADMAP.md` for the exact remaining semantic presentation work.

## STEP-derived wire/edge fallback

The loader renders lower-dimensional `Body` topology and also recovers display geometry from standalone `Edge`/`Vertex` topology or standalone Curve13/MeshTriangle/Point geometry when a STEP-derived exporter omits complete Product/Part/Body links. Geometry wrappers with an optional leading `Attributes` element are handled without confusing `Attributes` for the mathematical `*Core`.
