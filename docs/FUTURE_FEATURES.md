# BrokenLiquid — Future Features (post-v1 parking lot)

> **Status:** Draft 1 · 2026-11-25
> **Companion file:** [PLAN.md](PLAN.md) — the v1.0 scope, architecture and roadmap

Everything here is **out of scope for v1.0**. This file exists so good ideas have a home and
don't creep into v1. The v1 architecture (command registry, persistent naming, document model,
Python API, Qt/OCCT stack) was chosen so each item below can be added **without a rewrite**.

## How to use this file

- **Tier A — v1.x:** small to medium, high value, builds directly on v1 systems.
- **Tier B — v2:** new modelling domains or major subsystems.
- **Tier C — v3+:** large platforms, separate products or heavy R&D.
- **Tier D — research / moonshots:** worth exploring, no commitment.
- **Size:** S (days–2 wks) · M (2–6 wks) · L (1–3 months) · XL (3+ months), for one developer.
- **Promotion rule:** to move an item into a release, write an ADR in `docs/adr/`, add it to the
  roadmap in PLAN.md, and list what it depends on.
- Licences listed are a starting point — **verify before adoption**. GPL tools should run as
  separate processes so they don't affect BrokenLiquid's own licence.

---

## 1. User interface and workflow

| Feature | Tier | Size | Notes |
|---|---|---|---|
| Gesture Wheel (right-drag radial menu) | A | S | Equivalent of SolidWorks mouse gestures; 4/8 slots per document type; reads from the command registry |
| Selection breadcrumbs | A | S | Shows the hierarchy of the current selection (face → feature → body → component) |
| Live Handles (Instant3D-style) | A | M | Drag arrows to change extrude depth, fillet radius, plane offset; rulers with snapping |
| Multiple viewports (1/2/4) | A | M | Each with its own orientation and display style |
| Toolbar and Command Bar customisation | A | M | Drag commands between tabs; save/load UI profiles |
| Macro recorder | A | M | Record UI actions as Python code via the command registry |
| Design Library pane | A | M | Drag-and-drop reusable parts, features and sketches from folders |
| Tags, comments and markups in the tree | B | M | Notes on features; markups exported as images/PDF |
| Display Pane | B | S | Overview of visibility, colour, transparency per item |
| Touch and pen support | C | M | Multi-touch navigation; pen sketching |
| Localisation (translations) | B | M | Qt Linguist; community translations; metric/imperial defaults per locale |
| Screen-reader-friendly tree and panels | B | M | Deeper accessibility pass beyond v1 basics |
| Video capture of the viewport | B | S | Record MP4 of model rotation or motion studies |

## 2. Sketching

| Feature | Tier | Size | Notes |
|---|---|---|---|
| Splines (B-spline/NURBS) with handles | A | M | Strong v1.1 candidate; PlaneGCS already supports B-splines |
| Ellipse, partial ellipse, parabola, conic | A | S | |
| Sketch text (fonts → profiles) | A | M | OCCT `Font_BRepTextBuilder`; single-line fonts for engraving |
| Sketch blocks | B | M | Reusable grouped sketch geometry, for layout and mechanism sketches |
| Sketch pictures (trace over an image) | A | S | Image plane with scale/rotate calibration |
| 3D sketch | B | L | Lines/splines in space; needed for sweeps and weldment frames |
| Curves: helix/spiral, projected, composite, curve through points | A–B | M | Helix needed for springs and modelled threads |
| Equation-driven curves | B | S | `y = f(x)` and parametric curves |
| Sketch Doctor (full) | A | M | Suggests which relations/dimensions to remove for over-defined sketches |

## 3. Part modelling

| Feature | Tier | Size | Notes |
|---|---|---|---|
| **Variants (configurations)** | A | L | Multiple dimension/suppression sets per part; table-driven (spreadsheet/CSV design table). High value for families of parts |
| Variable-radius and face fillets, setback corners | A | M | OCCT supports variable radius |
| Rib, dome, wrap, indent, flex, deform | A–B | M each | Rib first (common in plastics) |
| Modelled threads | A | M | Helical sweep cut; cosmetic threads stay the default for speed |
| Full Hole Builder | A | M | More standards (DIN/JIS), slots, tapered pipe threads, hole series across bodies |
| Feature patterns: table-driven, sketch-driven, curve-driven, fill pattern | A | M | |
| Thin features (thin extrude/revolve) | A | S | |
| Split, intersect, delete/keep body, save bodies to parts | B | M | Multibody workflows |
| Library features | B | M | Save a feature set with references; drop onto new parts |
| Direct editing (move/delete/offset faces on imported geometry) | B | L | OCCT `BRepOffsetAPI`/defeaturing; essential for STEP-heavy users |
| Defeature / simplify | B | M | OCCT `BRepAlgoAPI_Defeaturing` |
| Mold tools (parting line, core/cavity, draft analysis) | C | L | |
| Draft, thickness, curvature and zebra-stripe analysis | B | M | Evaluate-tab visual checks |

## 4. Surfacing

| Feature | Tier | Size | Notes |
|---|---|---|---|
| Extruded / revolved / swept / lofted surfaces | B | M | Same machinery as solid features, open shells |
| Boundary surface, filled surface (N-sided patch) | B | L | OCCT `BRepFill_Filling` |
| Offset, ruled, planar surfaces | B | M | |
| Knit, trim, untrim, extend, thicken, replace face | B | L | Turns surface models into solids |

## 5. Sheet metal

| Feature | Tier | Size | Notes |
|---|---|---|---|
| Base flange, edge flange, miter flange, tab, hem, jog, sketched bend | B | L | Bend allowance via K-factor / bend tables |
| Convert solid to sheet metal; insert bends | B | L | |
| Flat pattern + DXF export for laser/waterjet | B | M | Very high value for makers; bend lines and notes on the DXF |
| Forming tools (louvres, dimples, ribs) | C | M | |
| Sheet-metal drawing views with bend tables | B | M | |

## 6. Weldments and structures

| Feature | Tier | Size | Notes |
|---|---|---|---|
| Structural members from a profile library along 3D sketch lines | B | L | ISO/EN/ANSI profiles (box, angle, channel, I-beam, pipe) |
| Trim/extend, end caps, gussets, corner treatments | B | M | |
| Cut list with lengths and angles; cut-list table in drawings | B | M | |
| Weld beads and weld symbols | C | M | |

## 7. Assemblies

| Feature | Tier | Size | Notes |
|---|---|---|---|
| **Exploded views** with explode lines | A | M | First item after v1 — needed for assembly drawings and instructions |
| Advanced mates: width, symmetric, limit (distance/angle), path, linear coupler, profile centre | A | M | Same solver, new residual equations |
| Mechanical mates: gear, rack & pinion, screw, cam, hinge, slot, universal joint | A–B | M | Coupling equations between DOFs |
| Smart mates (Alt-drag to mate) | A | S | Infer mate type from picked geometry |
| Component patterns (linear, circular, pattern-driven) and mirror components | A | M | |
| Edit part in context (top-down design) with external references | B | L | Reference management and "lock references" are the hard parts |
| Layout sketches driving parts (skeleton modelling) | B | M | |
| Collision detection while dragging; physical dynamics | B | M | Uses interference engine incrementally |
| Lightweight / large-assembly mode | B | L | Load display meshes only; resolve on demand; LOD |
| Simplified representations (envelope/defeatured exports) | C | M | Share models without exposing detail |
| Assembly features (cuts/holes across components) | B | M | |
| Pack & Bundle (collect all referenced files into a folder/zip) | A | S | Copy with path rewrite and optional rename |
| Replace component, find references, reload | A | S | |
| Standard Parts Library (parametric ISO/ANSI fasteners, washers, nuts, bearings, pins) | A–B | L | Generated on demand from tables; drag onto holes to auto-mate and auto-size |

## 8. Drawings and documentation

| Feature | Tier | Size | Notes |
|---|---|---|---|
| Auxiliary, broken, broken-out, crop, half-section, aligned-section views | A | M each | |
| GD&T: feature control frames, datums, datum targets (ISO 1101 / ASME Y14.5) | A–B | M | |
| Surface-finish, weld and edge symbols | A | S | |
| Revision tables and revision clouds | A | S | |
| Hole tables, general tolerance tables | A | S | |
| Exploded-view drawings with BOM | A | S | After exploded views |
| Drawing layers, line styles editor | A | S | |
| Better auto-dimension placement (optimisation-based) | B | L | Learn from user corrections on benchmark drawings |
| DWG import/export | B | M | Options: ODA Drawings SDK (commercial membership) or LibreDWG (GPL — external process only) |
| Multi-sheet PDF sets and batch printing | A | S | Through `blcli` |
| Model-based definition (3D annotations, STEP AP242 PMI export) | C | L | OCCT XDE supports PMI data |
| Auto-tolerancing assistant (DimXpert-like) | C | L | |
| Tolerance stack-up analysis (TolAnalyst-like) | C | L | |
| Assembly instruction documents (step-by-step exploded images) | C | M | |

## 9. Analysis and simulation

| Feature | Tier | Size | Notes |
|---|---|---|---|
| Linear static stress (SimulationXpress-like) | B | L | Mesh with **Netgen** (LGPL) or **Gmsh** (GPL, external process); solve with **CalculiX** (GPL, external process) or **Elmer** (ElmerSolver LGPL); results plotted on the model |
| Modal (natural frequency) analysis | B | M | Same pipeline |
| Thermal steady-state | C | M | |
| Motion study (kinematics) | B | L | Drive mates with motors over time using the assembly solver; plots; animation export |
| Rigid-body dynamics (contacts, gravity, springs) | C | XL | Candidate engine: Project Chrono (BSD-3) |
| Topology optimisation / generative design | D | XL | Voxel or level-set methods; OpenVDB (MPL-2.0) for volumes |
| CFD hand-off | D | L | Export clean geometry + setup to OpenFOAM (GPL, external) |
| Design checks (DFM: min wall, draft, hole depth/diameter) | B | M | Rule engine over B-rep |
| Cost estimation (material + machining time heuristics) | C | M | |
| Sustainability / embodied-carbon estimate | D | M | Material database × mass |

## 10. Manufacturing

| Feature | Tier | Size | Notes |
|---|---|---|---|
| 3MF export with colours/materials | A | S | lib3mf (BSD-2) |
| Direct "send to slicer" (PrusaSlicer, Cura, Bambu Studio) | A | S | Export + launch external app |
| 2.5D CAM: pocket, profile, drill, face | C | XL | OpenCAMLib (LGPL) for toolpaths; post-processors for common controllers |
| 3-axis surface CAM | D | XL | |
| Laser/plasma nesting from flat patterns | C | L | |
| G-code simulation / stock removal preview | C | L | |

## 11. Visualisation and rendering

| Feature | Tier | Size | Notes |
|---|---|---|---|
| PBR materials and environment lighting in the viewport | A | M | OCCT already supports PBR shading |
| Appearance library, decals, scenes | A–B | M | |
| Photoreal rendering (path tracing) | B | L | Options: OCCT ray tracing, Embree (Apache-2.0) / OSPRay (Apache-2.0), or Cycles standalone (Apache-2.0); or one-click export to Blender |
| Turntable and exploded-view animations | B | M | |
| Vulkan or Direct3D 12 renderer | C | XL | Behind the render abstraction; for very large assemblies |
| VR/AR design review | D | L | OpenXR (Apache-2.0) |

## 12. Data management and collaboration

| Feature | Tier | Size | Notes |
|---|---|---|---|
| Local version history per file (snapshots, compare, restore) | A | M | Built on the ZIP container + content hashes |
| Visual diff between two versions of a part | B | M | Highlight added/removed/changed faces using persistent names |
| Git-backed project vaults (with LFS), check-in/out and file locking | B | L | Local-first PDM; works with any git host |
| Where-used and reference graph browser | A | S | |
| Revision schemes, release states, approvals | C | L | Light PDM |
| Real-time multi-user collaboration | D | XL | CRDT on the document model; contradicts "local first" unless self-hosted |
| Optional self-hosted sync server | D | XL | |

## 13. Data exchange and interoperability

| Feature | Tier | Size | Notes |
|---|---|---|---|
| Opening native SolidWorks files (`.sldprt`/`.sldasm`) | D | XL | Proprietary formats; realistic only through a licensed commercial SDK. STEP remains the supported route |
| Parasolid `.x_t` import | C | M | Requires a commercial translator licence |
| JT, Creo, NX, CATIA, Inventor import | D | L | Commercial translators (e.g. via OCCT's commercial components or third parties) |
| IFC (BIM) export | C | M | |
| Mesh-to-solid / reverse engineering (STL/point cloud → B-rep) | D | XL | Segmentation + surface fitting |
| STEP AP242 with PMI and full colours/layers | B | M | |
| Python-based import/export plugin SDK | A | S | Community formats without core changes |

## 14. Platform, extensibility and distribution

| Feature | Tier | Size | Notes |
|---|---|---|---|
| Linux build (AppImage/Flatpak) | B | M | Qt + OCCT are cross-platform; mostly packaging and testing |
| macOS build (Apple Silicon) | B | L | OpenGL is deprecated on macOS (still works); long-term needs a Metal path or MoltenVK |
| Stable C ABI for C++ plugins | B | L | Versioned, so plugins survive app updates |
| Plugin manager / marketplace (local index, signed plugins) | C | L | |
| In-app updater with delta patches | A | M | WinSparkle (MIT) or custom |
| Microsoft Store / winget distribution | A | S | winget is easy; Store needs an LGPL compatibility check |
| Web viewer for shared models (opencascade.js / WASM) | C | L | Read-only viewing of `.blpart` and drawings in a browser |
| Mobile viewer | D | L | |
| Headless server mode for batch jobs and CI pipelines | B | M | Extend `blcli` (drawings, exports, checks in CI) |

## 15. AI and automation

| Feature | Tier | Size | Notes |
|---|---|---|---|
| Natural-language command bar ("add 4 M6 counterbored holes 10 mm from the edges") | C | L | An LLM writes Python calls against the BL API; the user previews and accepts like any feature. Opt-in and clearly marked when data leaves the machine; local-model option |
| AI-assisted auto-dimension placement | C | L | Train/score placement on corrected benchmark drawings |
| Drawing checker (missing dimensions, over-dimensioning, standard violations) | B | M | Rule-based first, AI second |
| Sketch from photo/scan | D | L | Edge detection → sketch entities → user cleans up |
| Design-intent suggestions (relations, symmetric features) | D | L | |
| Script assistant inside the Python console | C | M | Explains and completes BL API code |
| Feature recognition on imported STEP (holes, fillets, pockets → editable features) | D | XL | Makes dumb solids parametric again |

---

## Explicitly not planned

| Idea | Why not |
|---|---|
| Mandatory accounts, logins or cloud licensing | Contradicts BrokenLiquid's local-first principle |
| Telemetry on by default | Privacy; any data collection stays opt-in |
| Copying SolidWorks icons, names, artwork or help content | Legal risk; BL has its own identity |
| Claiming SolidWorks compatibility or affiliation | Trademark risk; describe BL as "a familiar workflow" only |

---

## Suggested order after v1.0 (first cut)

1. **v1.1 — "Comfort":** exploded views, splines, Live Handles, Gesture Wheel, breadcrumbs,
   advanced mates, Pack & Bundle, Explorer thumbnails (if not in 1.0), macro recorder.
2. **v1.2 — "Families":** Variants (configurations) + design tables, full Hole Builder, Standard
   Parts Library, GD&T and revision tables, sheet set PDF export.
3. **v1.3 — "Makers":** sheet metal with flat-pattern DXF, 3MF export, send-to-slicer, local
   version history.
4. **v2.0 — "Domains":** surfacing, weldments, direct editing of imported geometry, motion study,
   Linux build.
5. **v2.x+:** linear static FEA, photoreal rendering, PDM vaults, macOS, CAM, AI command bar.
