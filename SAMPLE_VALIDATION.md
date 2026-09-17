# Validation against the supplied NIST QIF samples

The loader design was checked against all six QIF files supplied with the request. Together they contain **1,025 B-rep faces**.

| Sample | Faces | Plane23 | Cylinder23 | Cone23 | Sphere23 | Torus23 | Nurbs23 |
|---|---:|---:|---:|---:|---:|---:|---:|
| nist_ftc_09_asme1_ap242.qif | 161 | 62 | 91 | 0 | 0 | 0 | 8 |
| nist_ftc_08_asme1_ap242-1.qif | 248 | 82 | 134 | 0 | 20 | 12 | 0 |
| nist_ftc_06_asme1_ap242.qif | 187 | 71 | 88 | 8 | 8 | 12 | 0 |
| nist_ctc_05_asme1_ap242.qif | 156 | 62 | 67 | 13 | 4 | 6 | 4 |
| nist_ctc_03_asme1_ap242.qif | 156 | 86 | 70 | 0 | 0 | 0 | 0 |
| nist_ctc_01_asme1_ap242.qif | 117 | 56 | 57 | 4 | 0 | 0 | 0 |
| **Total** | **1,025** | **419** | **507** | **25** | **32** | **30** | **12** |

Observed Curve12 types in these samples are `Segment12`, `Nurbs12`, and `Aggregate12`; all are implemented by the loader.

## Geometry checks performed

- Surface parameter equations were compared with the supplied QIF topology by evaluating the Curve12 trimming endpoints and checking that they map onto their corresponding 3D edge endpoints.
- The check covered planes, cylinders, cones, spheres, tori, and NURBS surfaces present in the files.
- All 1,025 face trimming polygons become valid UV polygons after snapping the very small numerical endpoint/closure gaps that occur in the exported p-curves.
- Several sphere/cylinder loops contain closure mismatches on the order of floating-point/CAD export tolerance; without snapping those endpoints, a triangulator can interpret the microscopic closure segment as a self-intersection at a periodic seam or sphere pole. `qif_model.cpp` explicitly handles this case.

## Build validation performed in the development sandbox

The C++ sources were passed through C++17 compiler syntax checks with strict warnings enabled. The sandbox did not contain SDL3/pugixml/earcut development packages and did not allow dependency fetching, so a linked graphical executable was not produced there. The included CMake build fetches pinned dependency versions on a normal development machine, or can use preinstalled packages for offline builds.
