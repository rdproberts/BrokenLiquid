#pragma once

#include "render/OcctViewer.h"
#include "render/SketchPresentation.h"

#include <AIS_InteractiveObject.hxx>
#include <Bnd_Box.hxx>
#include <gp_Pln.hxx>

#include <QList>
#include <QUuid>
#include <QWidget>

#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace bl::doc {
class Document;
struct Body;
}

namespace bl::ui {

class CommandRegistry;
class ConfirmationCorner;
class ViewHud;

// A section view as the user set it up (PLAN.md 6.10). The cut plane is a reference plane or a
// planar face, moved `offset` mm along its normal; `flip` keeps the other side.
struct SectionSpec {
    enum Reference { Front, Top, Right, Face };
    Reference reference = Front;
    gp_Pln face;          // used when reference == Face
    double offset = 0.0;  // mm along the reference normal
    bool flip = false;
    bool cap = true;

    gp_Pln basePlane() const;
    gp_Pln plane() const; // the actual cut plane
};

// One document's graphics area: the 3D viewer, its View HUD and the Confirmation Corner.
// Keeps the displayed objects in step with the document model (one object per body).
class DocumentView : public QWidget {
    Q_OBJECT
public:
    DocumentView(doc::Document* document, CommandRegistry& registry, QWidget* parent = nullptr);

    doc::Document* document() const { return m_document; }
    render::OcctViewer* viewer() const { return m_viewer; }
    ConfirmationCorner* corner() const { return m_corner; }
    ViewHud* hud() const { return m_hud; }

    // Body that owns a displayed object (null if it is not a body, e.g. a measure line).
    QUuid bodyOf(const Handle(AIS_InteractiveObject)& object) const;
    Handle(AIS_InteractiveObject) objectOf(const QUuid& body) const;

    // Bounding box of the visible bodies in mm; false when nothing is shown.
    bool bounds(Bnd_Box& box) const;

    QList<QUuid> selectedBodies() const;
    void selectBodies(const QList<QUuid>& ids);

    // Section view. previewSection() only changes the display; setSection() also remembers it so a
    // cancelled command can go back with restoreSection().
    const std::optional<SectionSpec>& section() const { return m_section; }
    void setSection(const std::optional<SectionSpec>& spec);
    void previewSection(const std::optional<SectionSpec>& spec);
    void restoreSection() { previewSection(m_section); }

    // The sketch open in the sketch editor is drawn by the editor, not as a finished sketch.
    void setEditingSketch(const QUuid& id);
    // Redraws finished sketches (after a theme change).
    void refreshSketches() { sync(); }
    // Shows these bodies instead of the model's (a command editing a feature shows the model as it
    // was before that feature); nullopt goes back to the model.
    void setBodiesOverride(std::optional<std::vector<doc::Body>> bodies);

private:
    void sync();

    struct Entry {
        Handle(AIS_InteractiveObject) object;
        TopoDS_Shape shape;
        std::optional<QColor> color;
        bool visible = true;
    };

    doc::Document* m_document;
    render::OcctViewer* m_viewer;
    ViewHud* m_hud;
    ConfirmationCorner* m_corner;
    std::map<QUuid, Entry> m_objects;
    std::map<QUuid, Handle(render::SketchPresentation)> m_sketches;
    QUuid m_editingSketch;
    std::optional<SectionSpec> m_section;
    std::shared_ptr<const std::vector<doc::Body>> m_override;
    bool m_fitted = false;
};

} // namespace bl::ui
