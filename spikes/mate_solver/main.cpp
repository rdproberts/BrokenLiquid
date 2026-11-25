// Spike: two-part mate solver. A fixed block with a hole and a free pin with a head, joined by
// Concentric (pin axis / hole axis) and Coincident (head underside / block top) mates.
// Answers PLAN.md §6.8 (assemblies): "Can a small damped least-squares solver place parts reliably and tell
// the user how many degrees of freedom are left (and which mates conflict)?"
//
// Pose = translation t + rotation vector r (6 unknowns). Each mate adds residual equations;
// the solver is Levenberg-Marquardt with a numerical Jacobian. Remaining DOF = 6 - rank(J).
//
// Output (BL_SPIKE_OUT): mate_solver_report.txt.
// Exit code 0 when: >= 95% of random starts converge, DOF == 1 (spin about the pin axis),
// and a conflicting third mate is reported as unsatisfiable.

#include <Eigen/Dense>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using Vec3 = Eigen::Vector3d;
using Mat3 = Eigen::Matrix3d;
using Pose = Eigen::Matrix<double, 6, 1>;

namespace {

Mat3 rotation(const Vec3& r) {
    const double angle = r.norm();
    if (angle < 1e-12) {
        return Mat3::Identity();
    }
    return Eigen::AngleAxisd(angle, r / angle).toRotationMatrix();
}

struct Placed {
    Mat3 R;
    Vec3 t;
    Vec3 point(const Vec3& p) const { return R * p + t; }
    Vec3 dir(const Vec3& d) const { return R * d; }
};

Placed place(const Pose& x) { return {rotation(x.tail<3>()), x.head<3>()}; }

// Geometry of the two parts (mm). Block: 40 x 20 x 10, hole axis at (20, 10) along Z.
// Pin: shaft along local +Z from z = -10, head underside at local z = 0, normal -Z.
struct Axis {
    Vec3 p, d;
};
struct PlaneRef {
    Vec3 p, n;
};
const Axis kHoleAxis{{20, 10, 0}, {0, 0, 1}};
const PlaneRef kBlockTop{{0, 0, 10}, {0, 0, 1}};
const Axis kPinAxis{{0, 0, 0}, {0, 0, 1}};
const PlaneRef kHeadUnderside{{0, 0, 0}, {0, 0, -1}};

using Mate = std::function<void(const Placed&, std::vector<double>&)>;

// Concentric: axes collinear (direction may flip, as in the usual "aligned / anti-aligned" option).
void concentric(const Placed& pin, std::vector<double>& r) {
    const Vec3 d = pin.dir(kPinAxis.d);
    const Vec3 c1 = d.cross(kHoleAxis.d);                       // parallel
    const Vec3 c2 = (pin.point(kPinAxis.p) - kHoleAxis.p).cross(kHoleAxis.d); // on the line
    r.insert(r.end(), {c1.x(), c1.y(), c1.z(), c2.x(), c2.y(), c2.z()});
}

// Coincident plane/plane, anti-aligned normals (faces touch).
void coincident(const Placed& pin, std::vector<double>& r) {
    const Vec3 n = pin.dir(kHeadUnderside.n);
    const Vec3 a = n + kBlockTop.n;
    const double dist = (pin.point(kHeadUnderside.p) - kBlockTop.p).dot(kBlockTop.n);
    r.insert(r.end(), {a.x(), a.y(), a.z(), dist});
}

// Distance mate that conflicts with `coincident`: head 5 mm above the top face.
void distance5(const Placed& pin, std::vector<double>& r) {
    r.push_back((pin.point(kHeadUnderside.p) - kBlockTop.p).dot(kBlockTop.n) - 5.0);
}

Eigen::VectorXd residuals(const Pose& x, const std::vector<Mate>& mates) {
    std::vector<double> r;
    const Placed pin = place(x);
    for (const Mate& m : mates) {
        m(pin, r);
    }
    return Eigen::Map<Eigen::VectorXd>(r.data(), static_cast<Eigen::Index>(r.size()));
}

Eigen::MatrixXd jacobian(const Pose& x, const std::vector<Mate>& mates) {
    const Eigen::VectorXd r0 = residuals(x, mates);
    Eigen::MatrixXd J(r0.size(), 6);
    for (int k = 0; k < 6; ++k) {
        const double h = 1e-7 * std::max(1.0, std::abs(x[k]));
        Pose xp = x, xm = x;
        xp[k] += h;
        xm[k] -= h;
        J.col(k) = (residuals(xp, mates) - residuals(xm, mates)) / (2 * h);
    }
    return J;
}

struct Solve {
    Pose x;
    double residual = 0;
    int iterations = 0;
    bool converged = false;
};

Solve levenbergMarquardt(Pose x, const std::vector<Mate>& mates, int maxIter = 200) {
    double lambda = 1e-3;
    Eigen::VectorXd r = residuals(x, mates);
    double cost = r.squaredNorm();
    Solve s;
    bool stalled = false;
    for (s.iterations = 0; s.iterations < maxIter && cost > 1e-20 && !stalled; ++s.iterations) {
        const Eigen::MatrixXd J = jacobian(x, mates);
        const Eigen::MatrixXd A = J.transpose() * J;
        const Eigen::VectorXd g = J.transpose() * r;
        bool improved = false;
        for (int tries = 0; tries < 12 && !improved; ++tries) {
            Eigen::MatrixXd damped = A;
            damped.diagonal().array() += lambda * (A.diagonal().array() + 1e-9);
            const Pose step = damped.ldlt().solve(-g);
            const Pose xn = x + step;
            const Eigen::VectorXd rn = residuals(xn, mates);
            const double cn = rn.squaredNorm();
            if (cn < cost) {
                stalled = step.norm() < 1e-14 * (1 + x.norm()) || cost - cn < 1e-15 * cost;
                x = xn, r = rn, cost = cn;
                lambda = std::max(lambda / 3, 1e-12);
                improved = true;
            } else {
                lambda *= 4;
            }
        }
        stalled = stalled || !improved; // a minimum that may not be a solution (over-defined)
    }
    s.x = x;
    s.residual = std::sqrt(cost);
    s.converged = s.residual < 1e-8;
    return s;
}

struct Freedom {
    int equations = 0, rank = 0, dof = 0, redundant = 0;
    Eigen::VectorXd nullDirection; // the motion the mates leave free (in pose space)
};

Freedom analyse(const Pose& x, const std::vector<Mate>& mates) {
    const Eigen::MatrixXd J = jacobian(x, mates);
    Eigen::JacobiSVD<Eigen::MatrixXd> svd(J, Eigen::ComputeFullV);
    const auto& sv = svd.singularValues();
    const double tol = 1e-6 * std::max(1.0, sv.size() ? sv[0] : 0.0);
    Freedom f;
    f.equations = static_cast<int>(J.rows());
    for (Eigen::Index i = 0; i < sv.size(); ++i) {
        f.rank += sv[i] > tol ? 1 : 0;
    }
    f.dof = 6 - f.rank;
    f.redundant = f.equations - f.rank;
    if (f.dof > 0) {
        f.nullDirection = svd.matrixV().col(5);
    }
    return f;
}

} // namespace

int main() {
    std::ostringstream report;
    report << "BrokenLiquid spike: two-part mate solver (LM, numerical Jacobian)\n\n";

    const std::vector<Mate> mates = {concentric, coincident};
    std::mt19937 rng(1729);
    std::uniform_real_distribution<double> pos(-60, 60), rot(-3.0, 3.0);

    const int runs = 200;
    int converged = 0, totalIter = 0;
    double worst = 0;
    Solve sample;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < runs; ++i) {
        Pose x0;
        x0 << pos(rng), pos(rng), pos(rng), rot(rng), rot(rng), rot(rng);
        const Solve s = levenbergMarquardt(x0, mates);
        converged += s.converged ? 1 : 0;
        totalIter += s.iterations;
        worst = std::max(worst, s.residual);
        if (i == 0) {
            sample = s;
        }
    }
    const double perSolveMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / runs;

    const Placed pin = place(sample.x);
    const Vec3 axisPoint = pin.point(kPinAxis.p), axisDir = pin.dir(kPinAxis.d);
    const Freedom f = analyse(sample.x, mates);

    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "random starts: %d, converged: %d (%.1f%%), mean iterations %.1f, %.3f ms per solve, worst residual %.2e\n",
                  runs, converged, 100.0 * converged / runs, double(totalIter) / runs, perSolveMs, worst);
    report << buf;
    std::snprintf(buf, sizeof buf, "sample solution: head-underside centre (%.6f, %.6f, %.6f), axis (%.6f, %.6f, %.6f)\n",
                  axisPoint.x(), axisPoint.y(), axisPoint.z(), axisDir.x(), axisDir.y(), axisDir.z());
    report << buf;
    std::snprintf(buf, sizeof buf, "freedom: %d equations, rank %d, redundant %d, remaining DOF %d\n", f.equations, f.rank,
                  f.redundant, f.dof);
    report << buf;

    // The free motion should be a pure spin about the hole axis: translation part ~ the rotation
    // of the pin's origin about that axis, rotation part parallel to Z.
    bool spinAboutAxis = false;
    if (f.dof == 1) {
        const Vec3 w = f.nullDirection.tail<3>();
        spinAboutAxis = w.norm() > 1e-6 && w.normalized().cross(Vec3::UnitZ()).norm() < 1e-4;
        std::snprintf(buf, sizeof buf, "free motion: rotation part (%.4f, %.4f, %.4f) -> %s\n", w.x(), w.y(), w.z(),
                      spinAboutAxis ? "spin about the pin axis (expected)" : "unexpected");
        report << buf;
    }

    // Over-defined case: add a 5 mm distance that contradicts the coincident mate.
    std::vector<Mate> conflicting = mates;
    conflicting.push_back(distance5);
    const Solve bad = levenbergMarquardt(sample.x, conflicting);
    std::snprintf(buf, sizeof buf, "conflict test (coincident + distance 5 mm): converged=%s residual %.3f -> %s\n",
                  bad.converged ? "yes" : "no", bad.residual,
                  bad.converged ? "NOT detected" : "reported as over-defined");
    report << buf;

    // Per-mate residuals tell the UI which mates to flag.
    {
        std::vector<double> rc, rd;
        const Placed p = place(bad.x);
        coincident(p, rc);
        distance5(p, rd);
        std::snprintf(buf, sizeof buf, "  per-mate error: coincident %.3f mm, distance %.3f mm (both flagged)\n",
                      std::abs(rc.back()), std::abs(rd.back()));
        report << buf;
    }

    const bool pass = converged >= runs * 95 / 100 && f.dof == 1 && spinAboutAxis && !bad.converged;
    report << "\nresult: " << (pass ? "PASS" : "FAIL") << "\n";

    const std::filesystem::path out = BL_SPIKE_OUT;
    std::filesystem::create_directories(out);
    std::ofstream(out / "mate_solver_report.txt") << report.str();
    std::fputs(report.str().c_str(), stdout);
    return pass ? 0 : 1;
}
