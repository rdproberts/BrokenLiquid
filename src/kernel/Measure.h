#pragma once

#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <optional>
#include <string>
#include <vector>

namespace bl::kernel {

enum class EntityKind { Vertex, Edge, Face, Solid, Other };

// What the Measure tool says about one picked entity. Lengths in mm, areas in mm^2, angles in rad.
struct EntityInfo {
    EntityKind kind = EntityKind::Other;
    std::string geometry;          // "Line", "Circle", "Arc", "Plane", "Cylinder", "Sphere", ...
    double length = 0.0;           // edges
    double area = 0.0;             // faces
    double perimeter = 0.0;        // faces: total length of boundary edges
    double volume = 0.0;           // solids
    std::optional<double> radius;  // circles/arcs, cylinders, spheres, cones (base)
    std::optional<gp_Pnt> center;  // circles/arcs, spheres; cylinders: a point on the axis
    std::optional<gp_Ax1> axis;    // lines (direction), circles (normal), planes (normal), cylinders/cones
    std::optional<gp_Pnt> point;   // vertices
};

EntityInfo describe(const TopoDS_Shape& shape);

// Result for two picked entities.
struct PairMeasure {
    double distance = 0.0;               // minimum distance between the two shapes
    gp_Pnt p1, p2;                       // closest points
    gp_Vec delta;                        // p2 - p1 (dX, dY, dZ)
    std::optional<double> centerDistance; // between centres/axes of circular geometry
    gp_Pnt c1, c2;                        // centres used for centerDistance
    std::optional<double> angle;          // between lines/planes/axes (0..pi/2 for planes and lines)
    bool parallel = false;
    bool valid = false;
};

PairMeasure measurePair(const TopoDS_Shape& a, const TopoDS_Shape& b);

// Totals for several picks of the same kind (sum of edge lengths / face areas).
struct Totals {
    int edges = 0, faces = 0, vertices = 0, solids = 0;
    double length = 0.0, area = 0.0, volume = 0.0;
};
Totals totals(const std::vector<TopoDS_Shape>& shapes);

} // namespace bl::kernel
