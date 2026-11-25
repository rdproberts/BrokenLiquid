#pragma once

#include <TopoDS_Shape.hxx>

namespace bl::kernel {

struct ShapeStats {
    int solids = 0;
    int shells = 0;
    int faces = 0;
    int edges = 0;
    int vertices = 0;
    double volume = 0.0;    // model units^3
    double area = 0.0;      // model units^2
    double bbox[6] = {};    // xmin, ymin, zmin, xmax, ymax, zmax
    bool valid = false;     // BRepCheck_Analyzer result
};

// Topology counts (unique sub-shapes), volume, area, bounding box and validity.
ShapeStats computeStats(const TopoDS_Shape& shape);

} // namespace bl::kernel
