# 0005. Assemblies: own Levenberg–Marquardt mate solver on Eigen

- **Status:** Accepted (Phase 0 spike passed; full solver in Phase 4)
- **Date:** 2026-11-25
- **Spike:** [`spikes/mate_solver`](../../spikes/mate_solver/main.cpp) — run as CTest `spike_mate_solver`

## Context

Assemblies need a solver that places components from mates (coincident, concentric, distance,
angle, …), moves parts as little as possible, reports each component's remaining degrees of
freedom (under / fully / over-defined) and names conflicting mates (PLAN.md §6.8). Options: our
own solver on Eigen, or OndselSolver (FreeCAD 1.0's assembly solver, LGPL).

## Spike

A fixed block with a hole and a free pin with a head. Mates: **Concentric** (pin axis on hole
axis) and **Coincident** (head underside on block top, normals opposed). Pose = translation +
rotation vector (6 unknowns). Damped least squares (Levenberg–Marquardt), numerical Jacobian,
DOF = 6 − rank(J) via SVD.

| Check | Result |
|---|---|
| 200 random starts (±60 mm, ±3 rad per axis) | **200/200 converged**, mean 12 iterations, 0.06 ms per solve, worst residual 9e-11 |
| Solution | head centre exactly (20, 10, 10), axis +Z |
| Remaining freedom | rank 5 → **DOF 1**; null-space motion = spin about the pin axis (correct) |
| Conflict: add "distance 5 mm" to the coincident faces | does not converge (residual 3.5) → **over-defined** reported |

## Decision

Build our own solver on Eigen (as PLAN.md recommended); keep OndselSolver as a reference, not a
dependency. Details for Phase 4:

- Split the assembly into rigid clusters connected by mates; solve each cluster separately.
- Start from current positions (minimal movement); LM with an analytic Jacobian per mate type
  (numerical one only as a test oracle).
- Rotation: update with a **local increment** (rotation vector applied to the current
  orientation each step) rather than one global rotation vector. Avoids the singularity near
  2π that the spike's global form would hit in long drag sessions.
- DOF per component from the rank of its Jacobian columns; drives the tree's (−)/(+) markers
  and drag-within-DOF.
- **Which mate conflicts:** least squares spreads the error (the spike shows 2.5 mm on *both*
  mates). Identify conflicts with rank-revealing QR on the Jacobian rows plus a drop-one-mate
  re-solve, and report the minimal conflicting set.
- **Redundancy:** raw row counts overstate it (the spike reports 5 "redundant" rows because
  vector residuals have 3 components for 2 real constraints). Count redundancy per mate from
  rank, so a duplicated mate is flagged but a vector residual is not.

## Consequences

- No extra dependency; solver code is small (the spike is ~250 lines) and fully under our
  control, including error messages.
- We own correctness: Phase 4 adds tests per mate type, flip options, gimbal-like singular poses,
  and the three benchmark assemblies.
- Speed is not a concern at this scale (0.06 ms per solve); large assemblies rely on clustering.
