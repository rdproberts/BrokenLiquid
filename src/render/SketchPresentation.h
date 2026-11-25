#pragma once

// Draws 2D line work, point markers and text on a plane in the 3D view. The sketch editor (and
// the display of finished sketches) builds a Drawing2d each time something changes; this object
// only turns it into OCCT primitives. It is never selectable: the sketch editor picks in sketch
// coordinates itself.

#include <AIS_InteractiveObject.hxx>
#include <Aspect_TypeOfLine.hxx>
#include <Aspect_TypeOfMarker.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Quantity_Color.hxx>
#include <TCollection_ExtendedString.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt2d.hxx>

#include <optional>
#include <vector>

namespace bl::render {

struct Drawing2d {
    struct Stroke {
        std::vector<gp_Pnt2d> points; // polyline
        Quantity_Color color;
        float width = 1.5f;
        Aspect_TypeOfLine type = Aspect_TOL_SOLID;
    };
    struct Marker {
        gp_Pnt2d at;
        Quantity_Color color;
        Aspect_TypeOfMarker type = Aspect_TOM_BALL;
        float scale = 2.0f;
    };
    struct Label {
        gp_Pnt2d at; // centre of the text
        TCollection_ExtendedString text;
        Quantity_Color color;
        std::optional<Quantity_Color> box; // background box
        float height = 14.0f;              // pixels
    };
    struct Fill {
        std::vector<std::vector<gp_Pnt2d>> loops; // outer outline first, then holes; closed polygons
        Quantity_Color color;
        float transparency = 0.75f;
    };
    std::vector<Fill> fills; // drawn under everything else
    std::vector<Stroke> strokes;
    std::vector<Marker> markers;
    std::vector<Label> labels;

    void line(gp_Pnt2d a, gp_Pnt2d b, const Quantity_Color& color, float width = 1.5f,
              Aspect_TypeOfLine type = Aspect_TOL_SOLID) {
        strokes.push_back({{a, b}, color, width, type});
    }
};

class SketchPresentation : public AIS_InteractiveObject {
    DEFINE_STANDARD_RTTI_INLINE(SketchPresentation, AIS_InteractiveObject)
public:
    explicit SketchPresentation(const gp_Ax3& plane) : m_plane(plane) {}

    const gp_Ax3& plane() const { return m_plane; }
    // Replaces the content; redisplay the object afterwards.
    void setDrawing(Drawing2d drawing) {
        m_drawing = std::move(drawing);
        SetToUpdate();
    }

    bool AcceptDisplayMode(const int mode) const override { return mode == 0; }

protected:
    void Compute(const Handle(PrsMgr_PresentationManager)& manager, const Handle(Prs3d_Presentation)& prs,
                 const int mode) override;
    void ComputeSelection(const Handle(SelectMgr_Selection)&, const int) override {}

private:
    // Triangles of a fill, kept while its outline is unchanged (meshing on every redraw is wasteful).
    struct FillMesh {
        std::vector<std::vector<gp_Pnt2d>> loops;
        Handle(Graphic3d_ArrayOfTriangles) triangles;
    };
    Handle(Graphic3d_ArrayOfTriangles) meshFill(const Drawing2d::Fill& fill);
    std::vector<FillMesh> m_fillCache;

    gp_Pnt to3d(const gp_Pnt2d& p) const;

    gp_Ax3 m_plane;
    Drawing2d m_drawing;
};

} // namespace bl::render
