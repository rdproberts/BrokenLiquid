// Spike: is our own sketch solver (ADR 0007) fast enough to drag geometry interactively?
//
// Builds a 100-entity sketch: a chain of 20 cells, each a rectangle (4 lines, coincident corners,
// horizontal/vertical, a width dimension) with a hole (circle, diameter), every cell joined to the
// previous one, the first to the origin. Heights are left free, so the sketch is under-defined and
// dragging the last corner moves geometry along the whole chain.
//
// Output (BL_SPIKE_OUT): sketch_solver_report.txt.
// Exit code 0 when every drag step converges and the mean drag solve takes under 2 ms.

#include "sketch/Sketch.h"
#include "sketch/Solver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace bl::sketch;

namespace {

double ms(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

int main() {
    Sketch s;
    constexpr int kCells = 20;
    Id previousCorner = s.origin();
    Id dragCorner = 0;
    for (int i = 0; i < kCells; ++i) {
        const double x0 = i * 12.0, w = 10.0, h = 8.0 + (i % 3);
        const Id bottom = s.addLine(x0, 0, x0 + w, 0);
        const Id right = s.addLine(x0 + w, 0, x0 + w, h);
        const Id top = s.addLine(x0 + w, h, x0, h);
        const Id left = s.addLine(x0, h, x0, 0);
        const Id hole = s.addCircle(x0 + w / 2, h / 2, 2.0);
        auto corner = [&s](Id a, Id b) { s.addRelation(ConstraintType::Coincident, {s.curve(a)->p2, s.curve(b)->p1}); };
        corner(bottom, right);
        corner(right, top);
        corner(top, left);
        corner(left, bottom);
        s.addRelation(ConstraintType::Horizontal, {bottom});
        s.addRelation(ConstraintType::Horizontal, {top});
        s.addRelation(ConstraintType::Vertical, {right});
        s.addRelation(ConstraintType::Vertical, {left});
        s.addDimension(ConstraintType::Distance, {bottom}, 10.0);
        s.addDimension(ConstraintType::Diameter, {hole}, 4.0);
        if (i == 0) {
            s.addRelation(ConstraintType::Coincident, {previousCorner, s.curve(bottom)->p1});
        } else {
            s.addDimension(ConstraintType::HorizontalDistance, {previousCorner, s.curve(bottom)->p1}, 2.0);
            s.addRelation(ConstraintType::Horizontal, {previousCorner, s.curve(bottom)->p1});
        }
        previousCorner = s.curve(bottom)->p2;
        dragCorner = s.curve(right)->p2;
    }

    std::ostringstream report;
    report << "Sketch solver spike (ADR 0007)\n\n";
    report << "entities: " << s.curves().size() << " curves, " << s.points().size() << " points, "
           << s.constraints().size() << " constraints\n";

    auto t0 = std::chrono::steady_clock::now();
    SolveResult first = solve(s);
    const double firstMs = ms(t0);
    report << "initial solve + analysis: " << (first.converged ? "converged" : "FAILED") << ", "
           << statusName(first.status) << ", DOF " << first.dof << ", " << first.iterations << " iterations, "
           << firstMs << " ms\n";

    // Drag the last cell's top-right corner up and down: the cell heights follow.
    constexpr int kSteps = 200;
    const Point start = *s.point(dragCorner);
    double total = 0.0, worst = 0.0;
    int failed = 0, iterations = 0;
    for (int k = 0; k < kSteps; ++k) {
        SolveOptions drag;
        drag.dragPoint = dragCorner;
        drag.dragX = start.x + 3.0 * std::sin(k * 0.05);
        drag.dragY = start.y + 6.0 * std::sin(k * 0.031);
        drag.analyse = false;
        t0 = std::chrono::steady_clock::now();
        const SolveResult r = solve(s, drag);
        const double t = ms(t0);
        total += t;
        worst = std::max(worst, t);
        iterations += r.iterations;
        failed += r.converged ? 0 : 1;
    }
    const double mean = total / kSteps;
    report << "drag: " << kSteps << " steps, " << failed << " failed, mean " << mean << " ms, worst " << worst
           << " ms, mean iterations " << double(iterations) / kSteps << "\n";

    t0 = std::chrono::steady_clock::now();
    const SolveResult after = solve(s);
    report << "analysis after drag: " << statusName(after.status) << ", DOF " << after.dof << ", " << ms(t0) << " ms\n";

    const bool pass = failed == 0 && mean < 2.0 && first.converged;
    report << "\nresult: " << (pass ? "PASS" : "FAIL") << " (target: every step converges, mean drag < 2 ms)\n";

    const std::filesystem::path out(BL_SPIKE_OUT);
    std::filesystem::create_directories(out);
    std::ofstream(out / "sketch_solver_report.txt") << report.str();
    std::fputs(report.str().c_str(), stdout);
    return pass ? 0 : 1;
}
