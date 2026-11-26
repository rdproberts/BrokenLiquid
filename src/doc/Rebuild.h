#pragma once

#include "doc/PartModel.h"

namespace bl::doc {

// Regenerates the part (PLAN.md 6.4): runs the history in order, honouring suppression and the
// rollback bar, and leaves the result in `model.bodies` (keeping the user's body names, visibility
// and colours) and each feature's state. Sketches on faces follow their face. Features before the
// first one that differs from `previous` reuse their cached results, so most edits only rebuild
// the tail of the history.
void rebuild(PartModel& model, const PartModel* previous = nullptr);

// The key a feature's faces are named with (its id, stable across renames).
std::string namingKey(const QUuid& id);

// The solid an extrude sweeps before it is merged or cut (also the command's preview). `before`:
// the bodies before the feature (Through All goes through them). Null with `error` on failure.
// The solid a revolve sweeps from its sketch (before it is merged or cut).
kernel::NamedShape revolveTool(const SketchFeature& sketch, const RevolveParams& params, const std::string& featureKey,
                               QString* error = nullptr);
// The line of `sketch` a revolve turns about: `axis` if set, else the first centerline (0: none).
sketch::Id revolveAxis(const sketch::Sketch& sketch, sketch::Id axis);

kernel::NamedShape extrudeTool(const SketchFeature& sketch, const ExtrudeParams& params, const BuildState& before,
                               const std::string& featureKey, QString* error = nullptr);

} // namespace bl::doc
