# 0006. Sketch constraint solver: PlaneGCS

- **Status:** Proposed — spike pending (moved to the start of Phase 2)
- **Date:** 2026-11-25

## Context

The sketcher (Phase 2) needs a 2D geometric constraint solver with fast interactive dragging,
DOF counting for the under/fully/over-defined colours, and conflict reporting. PLAN.md §6.6
recommends **PlaneGCS**, FreeCAD's solver (LGPL-2.1-or-later, Eigen-based; DogLeg, LM and BFGS,
with QR-based redundancy and conflict detection). SolveSpace's solver is GPL and ruled out for the
main executable.

PlaneGCS is not packaged in vcpkg. It lives in FreeCAD's `src/Mod/Sketcher/App/planegcs/` and
depends on Eigen and a few FreeCAD base headers (logging, exceptions).

## Decision (proposed)

Vendor PlaneGCS as `third_party/planegcs/` (a pinned FreeCAD tag, files unmodified except a small
shim for FreeCAD base headers, all changes listed in `third_party/planegcs/PATCHES.md`), build it
as its own shared library (`bl_planegcs.dll`, keeps the LGPL boundary clean), and wrap it behind
a BrokenLiquid `SketchSolver` interface so the solver could be replaced later.

## Spike to run before acceptance

Phase 0 spent its time on the three risks with no fallback (naming, HLR mapping, mate solving).
The PlaneGCS spike moves to the first week of Phase 2:

1. Extract the files and the base-header shim; build on MSVC with our triplet.
2. Solve a fully constrained bracket profile (lines, arcs, tangency, equal, symmetric,
   dimensions); check DOF = 0 and the result against hand-computed values.
3. Drag a point of an under-defined sketch at 60 Hz; budget < 2 ms per solve for 100 entities.
4. Add a conflicting dimension; check that the conflicting constraints are named.

Accept if all four pass. Fallback: write a small LM solver on Eigen, as done for mates in
[ADR 0005](0005-assembly-solver.md), with the PlaneGCS constraint formulations as reference.

## Consequences

- Shared-library boundary adds one DLL but keeps licence obligations obvious.
- We must track FreeCAD upstream fixes manually (pinned tag, periodic review).
