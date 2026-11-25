# Architecture Decision Records

One file per decision that is expensive to reverse. Format: Context, Decision, Consequences,
Status. New ADRs copy [`0000-template.md`](0000-template.md) and take the next number.

| # | Decision | Status |
|---|---|---|
| [0001](0001-tech-stack.md) | C++20 + Qt 6 Widgets + Open CASCADE 8 + embedded Python | Accepted |
| [0002](0002-dependencies-via-vcpkg.md) | Dependencies from vcpkg (manifest, release-only dynamic triplet) | Accepted |
| [0003](0003-persistent-naming.md) | Persistent naming: generative names from OCCT history | Accepted (spike passed) |
| [0004](0004-drawing-hlr-edge-mapping.md) | Drawings: OCCT HLR + geometric 2D↔3D edge mapping | Accepted (spike passed) |
| [0005](0005-assembly-solver.md) | Assemblies: own Levenberg–Marquardt mate solver on Eigen | Accepted (spike passed) |
| [0006](0006-sketch-solver-planegcs.md) | Sketch solver: PlaneGCS | Proposed (spike pending) |
