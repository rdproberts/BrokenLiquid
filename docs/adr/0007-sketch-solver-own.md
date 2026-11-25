# 0007. Sketch solver: own minimum-norm Gauss–Newton on Eigen

- **Status:** Accepted (spike passed)
- **Date:** 2026-11-25
- **Supersedes:** [0006](0006-sketch-solver-planegcs.md) (PlaneGCS)
- **Code:** [`src/sketch/Solver.cpp`](../../src/sketch/Solver.cpp), tests in
  [`tests/test_sketch_solver.cpp`](../../tests/test_sketch_solver.cpp), spike
  [`spikes/sketch_solver`](../../spikes/sketch_solver/main.cpp) (CTest `spike_sketch_solver`)

## Context

The sketcher needs a 2D constraint solver with fast interactive dragging, DOF counting for the
under/fully/over-defined colours, and names for redundant and conflicting constraints.
[ADR 0006](0006-sketch-solver-planegcs.md) proposed vendoring FreeCAD's PlaneGCS (LGPL). At the
start of Phase 2 we chose its documented fallback instead: a small solver of our own on Eigen,
like the mate solver of [ADR 0005](0005-assembly-solver.md). Reasons: no vendored third-party
code to patch, shim and track; no extra DLL or licence boundary; error messages and behaviour
fully under our control; and v1's entity set (points, lines, circles, arcs) is small.

## Decision

- **Model** ([`Sketch.h`](../../src/sketch/Sketch.h)): point-based. A line owns two points, a
  circle a centre plus a radius value, an arc its centre, start and end points (radius = start to
  centre; an internal equation keeps the end on the same circle). Relations and dimensions refer
  to points and curves by stable integer ids.
- **Equations:** every relation or driving dimension becomes one or two scalar residuals over
  the point coordinates and circle radii. Angles are normalised (cross/dot of unit vectors), so
  residuals are in mm or radians. Fixed points (the origin, later converted model edges) are
  constants, not unknowns.
- **Tangency at a shared end point** (fillets, tangent arcs) is written as *radius ⟂ line* (or
  *centres and point collinear*), not *distance − radius*: the latter has a zero gradient at a
  point already on both curves, which the solver and the DOF count would read as a missing
  equation. The solver detects the shared point from the coincident / on-curve relations.
- **Iteration:** damped, weighted minimum-norm Gauss–Newton,
  `dx = W⁻¹Jᵀ (J W⁻¹ Jᵀ + μI)⁻¹ (−F)`, with a sparse `SimplicialLDLT`, Jacobian by local central
  differences, and μ adapted per step (Levenberg–Marquardt style). The minimum-norm step moves
  geometry as little as possible, so a changed dimension nudges the sketch. A dragged point is
  placed at the pointer and given a large weight, so it stays there unless the constraints forbid
  it.
- **Analysis** (after a converged solve, skipped while dragging): rank of the dense Jacobian by
  column-pivoted QR → DOF = unknowns − rank. An unknown is still free if it has a component
  outside the Jacobian's row space; a point or curve is *defined* when none of its unknowns is
  free (drives the black/blue colours). Redundant constraints come from a greedy Gram–Schmidt
  pass over the rows in creation order, so the newest constraint gets the blame.
- **Failure:** if the equations cannot be met, the geometry is left unchanged and drop-one
  re-solves name the conflicting constraints (each one whose removal makes the sketch solvable).

## Spike results

| Check | Result |
|---|---|
| L-bracket with fillet and two holes, drawn off by 0.5–2 mm, 36 unknowns, 36 equations | converges, **DOF 0**, Fully Defined, all points within 1e-6 mm of hand values |
| Remove the width dimension | DOF 1; right-hand edges reported not defined, rest defined |
| Add a conflicting length (50 where 45 is implied) | not converged, geometry unchanged, the new dimension and the dimensions it fights are named; unrelated hole diameters are not |
| Add Parallel to two lines already horizontal | converged, Over Defined, exactly that relation reported redundant |
| Drag in a 100-curve, 239-constraint sketch (200 steps) | **0 failures, mean 0.57 ms, worst 0.98 ms** per solve (target < 2 ms), ~4 iterations |
| Full analysis on the same sketch | ~15 ms (only after a drag ends or an edit) |

## Consequences

- No new dependency (Eigen is already used); about 600 lines of our own code with unit tests per
  relation type.
- We own correctness and robustness. Watch: abs-based dimensions near zero, tangency side
  flips, large sketches (analysis is dense and O(n³); split into independent clusters if sketches
  grow past a few hundred entities), and drop-one cost on big conflicts.
- Splines (a v1.1 candidate) need new equation types; PlaneGCS's formulations stay a reference.
