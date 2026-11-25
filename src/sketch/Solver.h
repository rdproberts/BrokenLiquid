#pragma once

// Sketch constraint solver (ADR 0007): damped, weighted minimum-norm Gauss-Newton on Eigen.
//
// Every relation and driving dimension becomes one or two residual equations over the point
// coordinates (and circle radii). Each iteration takes the smallest weighted step that zeroes the
// linearised residuals, so geometry moves as little as possible: an edited dimension nudges the
// sketch instead of rebuilding it, and a dragged point (given a large weight) stays under the
// pointer while the rest of the sketch follows.
//
// After a solve, the rank of the Jacobian gives the remaining degrees of freedom and, per
// coordinate, whether it can still move - that drives the under/fully/over-defined colours.
// Constraints whose equations add nothing to the rank are reported as redundant; when the
// equations cannot be satisfied at all, drop-one tests name the conflicting constraints.

#include "sketch/Sketch.h"

#include <vector>

namespace bl::sketch {

enum class SketchStatus { UnderDefined, FullyDefined, OverDefined };

const char* statusName(SketchStatus status); // "Under Defined", ...

struct DragTarget {
    Id point = 0;
    double x = 0.0, y = 0.0;
};

struct SolveOptions {
    Id dragPoint = 0;            // keep this point as close as possible to (dragX, dragY)
    double dragX = 0.0, dragY = 0.0;
    std::vector<DragTarget> moreDrags; // further dragged points (a whole line moved at once)
    bool analyse = true;         // DOF, per-entity status, redundancy, conflicts (skip while dragging)
    int maxIterations = 60;
    double tolerance = 1e-9;     // max residual (mm, or rad for angle equations)
};

struct SolveResult {
    bool converged = false;
    SketchStatus status = SketchStatus::UnderDefined;
    int dof = 0;
    int iterations = 0;
    double residual = 0.0;
    std::vector<Id> conflicting; // cannot all hold together (only when not converged)
    std::vector<Id> redundant;   // implied by earlier constraints (converged but over-defined)
    std::vector<Id> defined;     // points and curves that can no longer move

    bool isDefined(Id id) const;
};

// Solves the sketch in place. If the constraints cannot be met, the geometry is left as it was.
SolveResult solve(Sketch& sketch, const SolveOptions& options = {});

} // namespace bl::sketch
