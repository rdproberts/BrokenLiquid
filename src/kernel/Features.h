#pragma once

// Solid-building blocks behind part features (PLAN.md 6.7): sketch regions turned into planar
// faces, extrusion, and booleans that keep persistent names (ADR 0003).

#include "kernel/Naming.h"
#include "sketch/Operations.h"
#include "sketch/Sketch.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax3.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>

#include <string>
#include <utility>
#include <vector>

namespace bl::kernel {

// A sketch's regions as faces on its plane. Every boundary edge remembers the sketch curve it came
// from, which names the faces a feature sweeps from it.
struct Profile {
    std::vector<TopoDS_Face> faces;
    std::vector<std::pair<TopoDS_Edge, sketch::Id>> edges;

    bool empty() const { return faces.empty(); }
};

// Regions a feature uses when none are picked: the even-odd rule, as for a sketch with nested
// contours (outer contour minus its islands, islands inside islands again, ...).
std::vector<int> defaultRegions(const sketch::Contours& contours);

// Faces on `plane` for the given regions of `contours` (found in `sketch`). `error` says why the
// result is empty.
Profile buildProfile(const sketch::Sketch& sketch, const gp_Ax3& plane, const sketch::Contours& contours,
                     const std::vector<int>& regions, std::string* error = nullptr);

// The solid swept by the profile `length` mm along `direction` (normally the sketch normal, or
// its reverse). Faces are named "<feature>/Side(<sketchKey>:<curve id>)", "<feature>/StartCap"
// (on the profile) and "<feature>/EndCap".
NamedShape extrude(const Profile& profile, const gp_Dir& direction, double length, const std::string& feature,
                   const std::string& sketchKey, std::string* error = nullptr);

// Booleans that carry the names of both inputs into the result. On failure the result is null and
// `error` explains.
// Revolves the profile about `axis` by `angle` radians (2 pi: a full turn, no caps). Faces are
// named like extrude faces: "<feature>/Side(<sketch>:<curve>)", "<feature>/StartCap", "/EndCap".
NamedShape revolve(const Profile& profile, const gp_Ax1& axis, double angle, const std::string& feature,
                   const std::string& sketchKey, std::string* error = nullptr);

// Rounds `edges` of `body` with a constant radius. Edges tangent to a picked edge are rounded too
// (OCCT follows smooth chains). New faces: "<feature>/Blend(<edge name>)", corners
// "<feature>/Corner(...)".
NamedShape fillet(const NamedShape& body, const std::vector<TopoDS_Edge>& edges, double radius,
                  const std::string& feature, std::string* error = nullptr);

// One edge to chamfer; `face` (one of the two faces at the edge) takes `distance1` when the two
// distances differ.
struct ChamferEdge {
    TopoDS_Edge edge;
    TopoDS_Face face;
};
// Bevels `edges` of `body`: equal setback `distance1` when `distance2` <= 0, otherwise distance1 on
// each edge's `face` and distance2 on the other face. New faces: "<feature>/Chamfer(<edge name>)".
NamedShape chamfer(const NamedShape& body, const std::vector<ChamferEdge>& edges, double distance1,
                   double distance2, const std::string& feature, std::string* error = nullptr);

// The faces of `shape` whose names start with `prefix` (e.g. "<feature>/"), as one compound.
TopoDS_Shape facesNamed(const NamedShape& shape, const std::string& prefix);

NamedShape fuse(const NamedShape& a, const NamedShape& b, std::string* error = nullptr);
NamedShape cut(const NamedShape& body, const NamedShape& tool, std::string* error = nullptr);

// Do two shapes touch or overlap?
bool touches(const TopoDS_Shape& a, const TopoDS_Shape& b);
// Does the shape hold any solid?
bool hasSolid(const TopoDS_Shape& shape);

} // namespace bl::kernel
