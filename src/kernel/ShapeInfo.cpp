#include "kernel/ShapeInfo.h"

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>

namespace bl::kernel {

namespace {
int countOf(const TopoDS_Shape& shape, TopAbs_ShapeEnum type) {
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> map;
    TopExp::MapShapes(shape, type, map);
    return map.Extent();
}
} // namespace

ShapeStats computeStats(const TopoDS_Shape& shape) {
    ShapeStats s;
    if (shape.IsNull()) {
        return s;
    }
    s.solids = countOf(shape, TopAbs_SOLID);
    s.shells = countOf(shape, TopAbs_SHELL);
    s.faces = countOf(shape, TopAbs_FACE);
    s.edges = countOf(shape, TopAbs_EDGE);
    s.vertices = countOf(shape, TopAbs_VERTEX);

    GProp_GProps vprops;
    BRepGProp::VolumeProperties(shape, vprops);
    s.volume = vprops.Mass();

    GProp_GProps sprops;
    BRepGProp::SurfaceProperties(shape, sprops);
    s.area = sprops.Mass();

    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    if (!box.IsVoid()) {
        box.Get(s.bbox[0], s.bbox[1], s.bbox[2], s.bbox[3], s.bbox[4], s.bbox[5]);
    }

    BRepCheck_Analyzer analyzer(shape);
    s.valid = analyzer.IsValid();
    return s;
}

} // namespace bl::kernel
