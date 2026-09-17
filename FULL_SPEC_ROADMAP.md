# QIF 3.0 full-spec status and remaining semantic work

This file is deliberately strict about the word **complete**. QIF has several layers: XML/schema validity, the official DMSC non-XSD checks, full document retention/navigation, domain-specific semantic interpretation, and finally graphical presentation. Passing one layer does not imply the next.

## Complete in 0.4.0: schema/conformance package

The bundled QIF 3.0 package audit starts at `QIFApplications/QIFDocument.xsd` and reaches **23/23 packaged QIF XSD files**. The audit currently sees **1,058 global elements, 2,046 complex types, and 168 simple types**. The official `Check/Check.xsl` import graph is also complete.

`tools/validate_qif.py --full` performs the complete implemented conformance stack:

```text
XML parse
  -> QIFDocument.xsd (all imported/included QIF schemas)
  -> package dependency audit
  -> list n/cardinality checks
  -> official DMSC Check.xsl suite
  -> local external-QIF URI/QPId/xId resolution and validation
```

The supplied official QIF 3.0 stylesheets declare **XSLT 1.0**. They run through Python `lxml`/libxslt; Saxon/XSLT 2.0 is not required for this unmodified package.

This means the project can test a document against the full supplied QIF 3.0 schema/check package. It does **not** mean an arbitrary document will receive no quality warnings; the official quality checks may legitimately find issues in an otherwise XSD-valid file.

## Complete in 0.4.0: schema-neutral full-document reader

The C++ loader can retain every XML element, attribute, and value in a `GenericElement` document tree. `qifcheck` sets geometry as optional, so a legal QIF document containing only Plans, Measurement Resources, Rules, Results, Statistics, traceability, signatures, or other non-renderable information can still be read and inspected.

This provides a loss-avoiding fallback representation for all legal QIF content. It is intentionally separate from specialized convenience classes: a QIF construct need not have a dedicated C++ struct to remain accessible.

Use:

```sh
qifcheck --all-elements file.qif
qifcheck --tree --tree-depth 6 file.qif
```

## Complete inventory coverage: Geometry and Topology

The renderer has explicit branches for all concrete Geometry/Topology entity names discovered from the QIF 3.0 schemas: **28/28 Geometry**, **9/9 Topology**, with **25/25 evaluator core branches** covered by the inventory test.

This is type-family coverage, not a mathematical proof for every legal parameter combination. Dedicated conformance fixtures should continue to grow for unusual domains, periodic seams, poles, degeneracies, `turned` combinations, binary data, nested aggregates, tolerant topology, and every unit/transform combination.

## Implemented presentation semantics

The viewer currently implements product colors/hidden state, mesh/point visibility/color data, assemblies and transforms, plus QIF cameras and saved views. Orthographic and perspective saved-camera projection is implemented; saved-view body/component filters are applied.

## Not yet semantically complete: rich Visualization/PMI

All Visualization/PMI XML is validatable and retainable, but these presentation semantics still need dedicated rendering logic before the GUI can claim complete QIF MBD presentation:

- full `PMIDisplay` text layout and semantic PMI association;
- complete QIF special-symbol/GD&T glyph rendering and font rules;
- all leader forms, leader heads, witness/extension lines and frame rules;
- 2D `Graphics` areas/polylines/triangulations placed on annotation planes;
- annotation visible/hidden interaction for every PMI primitive;
- complete `DisplayStyle` group overrides;
- `SimplifiedRepresentation` forms;
- `ExplodedView` operation sequences;
- `ZoneSection` clipping, logical section-plane operations and hatching;
- exact presentation behavior for every saved-view combination.

These are presentation semantics, not XSD acceptance gaps.

## Not yet domain-typed: the non-geometry application models

The full generic document tree retains these sections, and the full validator checks their XML, but the project does not yet provide thousands of hand-written C++ domain classes and convenience APIs for every specialized QIF type in:

- Features and Characteristics / datum/PMI semantics;
- QIF Plans;
- Measurement Resources;
- QIF Rules and expression evaluation;
- QIF Results;
- QIF Statistics;
- all traceability/standards/software/algorithm metadata.

For a viewer/inspector, the generic tree is sufficient to avoid data loss. For a metrology application that must *execute* plans/rules or *calculate* statistics, domain-specific semantic APIs still have to be implemented above that tree.

## External documents

`qifvalidate --full` resolves and validates locally addressable external QIF documents, verifies target QPIds and `xId` entity IDs, detects cycles, and limits recursion. URI-less QPId references and HTTP/HTTPS references are retained/reported but are not automatically fetched.

The **GUI renderer** still renders geometry from the opened document rather than recursively importing external product geometry. That is separate from validator/reference correctness and remains a rendering/navigation enhancement.

## XML Digital Signature

`xmldsig-core-schema.xsd` is bundled and validated offline, so XML Signature syntax participates in QIF XSD validation. Cryptographic signature verification is intentionally not claimed: that requires certificate/key trust policy and a cryptographic XMLDSIG implementation (for example xmlsec) in addition to schema validity.

## Reader versus writer

0.4.0 is a reader/viewer/checker. Full write/edit conformance would additionally require serialization of all constructs, namespace/schema-location handling, unknown-extension preservation, id/reference preservation, digital-signature policy, and read-write-validate-semantic round-trip tests.

## Native GPU renderer

The current final drawing path is SDL Renderer: QIF geometry is tessellated and CPU-projected, then submitted with `SDL_RenderGeometry`. SDL can use Metal or Vulkan underneath. This is portable and useful, but it is not a native CAD renderer with GPU depth buffering.

A future native GPU path should add hardware depth testing, indexed buffers, picking, clipping planes, large-model batching and robust transparency ordering. On Solaris, this can use Vulkan when SDL/system Mesa/Vulkan support is present; the fetched SDL build path in this project patches SDL's current Solaris Vulkan CMake gate.

## Definition of completion used by this project

The project may now say **full QIF 3.0 schema/check conformance coverage** and **full schema-neutral document retention**. It should not yet say **all QIF semantics are graphically/operationally implemented**. That latter claim requires the rich PMI/display work above and, for a general metrology processor, domain behavior for Plans/Rules/Results/Statistics rather than merely retaining and validating them.
