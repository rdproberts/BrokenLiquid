# 0008. Native files: a ZIP of JSON plus BRep, written through Qt's private zip classes

- **Status:** Accepted (Phase 3; parts first, assemblies and drawings follow the same container)
- **Date:** 2026-11-25

## Context

Phase 3 makes parts editable solids, so they need a native file that keeps everything the user
built: the feature history in order (with suppression and the rollback position), every sketch with
its constraints and dimensions, face references of sketches on faces (ADR 0003), body names,
visibility and colours, and imported geometry that has no source file to read again. The file has
to survive app updates (older files must keep opening), must not be lost to a crash halfway through
a save, and should be easy to inspect when a user reports a problem.

Options looked at:

- **OCAF (`BinOcaf`/`XmlOcaf`)**: the document would have to live in the OCAF label tree, which the
  part model deliberately does not use (ADR 0003 chose our own naming over `TNaming`). Migrations
  would be written against attribute GUIDs.
- **One JSON file** with BRep data base64-encoded inside: simple, but large imports bloat it by a
  third and every save re-encodes everything.
- **SQLite**: transactional and fast for partial writes, but opaque to inspect, and parts are
  small enough to write whole.
- **A ZIP container** with JSON for the model and native BRep for geometry, as `.3mf`, `.docx` and
  several open-source CAD formats do.

## Decision

1. Native files are ZIP archives with BrokenLiquid's own extensions: **`.blpart`** now,
   **`.blasm`** and **`.bldraw`** later with the same layout.
2. Entries of a part:
   - `manifest.json`: `format` (`"BrokenLiquid"`), `type` (`"part"`), `schema` (integer), `app`
     version, `build` id, `saved` (UTC ISO time).
   - `model.json`: units, name counters, rollback index, the features in history order (type, name,
     suppressed, body id, extrude parameters, import body name and source path), the sketches
     (plane origin, normal and x axis, face reference, visibility, solve status, and the sketch
     itself: points, curves and constraints with their stable ids) and the bodies' display
     properties keyed by body id.
   - `shapes/<featureId>.brep`: the shape of each **Import** feature in OCCT's binary format
     (`BinTools`). Geometry built by features is **not stored**: opening a part rebuilds it from
     the history, which keeps files small and guarantees the file and the model never disagree.
3. Zip reading and writing use Qt's private `QZipReader`/`QZipWriter` (`Qt6::CorePrivate`): no new
   dependency, deflate support, and the code using them is confined to `src/doc/PartFile.cpp`.
4. Saving never overwrites in place: the archive is written to `<file>.saving`, the previous file
   becomes `<file>.bak`, then the new file is renamed into place. If that last rename fails the
   previous version is put back.
5. `schema` starts at 1. A file with a newer schema is refused with a message naming the app
   version that wrote it; older files pass through `migrate()` one schema step at a time before
   loading. Unknown JSON keys are ignored, so additive changes need no new schema.

## Consequences

- Opening a part costs one full rebuild (milliseconds for typical parts, dominated by booleans).
  A cached result shape may be added later as an optional entry; the history stays the truth.
- Qt's private zip API has no compatibility promise between minor Qt versions. If it breaks, the
  fallback is minizip-ng from vcpkg behind the same two functions; nothing outside `PartFile.cpp`
  changes. The vcpkg Qt version is pinned, so this cannot happen unnoticed.
- Users and support can unzip a part and read `model.json`. Hand edits are possible but not
  supported; a broken file reports the JSON error instead of guessing.
- Every model change that adds persistent data must extend `model.json` and, when the meaning of an
  existing key changes, bump `schema` and add a migration step with a test file.
