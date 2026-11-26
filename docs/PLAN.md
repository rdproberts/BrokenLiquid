# BrokenLiquid: research and build plan for v1.0

*Draft, November 2026. BrokenLiquid (BL) is free, local, history-based parametric 3D CAD for
Windows. Everything left out of v1 is in [FUTURE_FEATURES.md](FUTURE_FEATURES.md).*

---

## Contents

1. [Vision and principles](#1-vision-and-principles)
2. [v1 scope at a glance](#2-v1-scope-at-a-glance)
3. [Research: the classic desktop CAD layout](#3-research-the-classic-desktop-cad-layout)
4. [Research: modelling, assembly and drawing logic](#4-research-modelling-assembly-and-drawing-logic)
5. [Technology decision](#5-technology-decision)
6. [Architecture](#6-architecture)
7. [UI specification](#7-ui-specification)
8. [Splash screen and startup](#8-splash-screen-and-startup)
9. [File formats and data exchange](#9-file-formats-and-data-exchange)
10. [Windows packaging and shipping](#10-windows-packaging-and-shipping)
11. [Quality: testing and performance budgets](#11-quality-testing-and-performance-budgets)
12. [Roadmap](#12-roadmap)
13. [Risks and mitigations](#13-risks-and-mitigations)
14. [Open decisions](#14-open-decisions)
15. [Sources](#15-sources)

---

## 1. Vision and principles

BrokenLiquid is a free, offline, parametric 3D CAD application. Anyone who has learned
history-based 3D modelling should feel at home within minutes. It follows the standard workflow
(**sketch → feature → part → assembly → drawing**), a conventional screen layout and the usual
mouse and keyboard habits, with its own terminology, icons, look and file formats.

**Principles**

1. **Familiar by design.** Follow the established conventions of history-based modelling for
   layout and workflow, so nothing has to be learned from scratch. Names, icons, artwork and
   file formats are designed for BrokenLiquid (terminology in §3.4).
2. **Local first.** No account, no login and no telemetry by default. Works fully offline.
3. **Small v1, room to grow.** v1 covers parts, assemblies and drawings only. Architecture
   choices leave room for the items in FUTURE_FEATURES.md without a rewrite.
4. **Reliable before clever.** A smaller feature set that never corrupts a model or silently
   breaks a reference is better than a large, fragile one.
5. **Scriptable.** Every UI command is a registered command with a Python entry point. Anything
   a user can click, a script can do.

---

## 2. v1 scope at a glance

| Area | In v1 | Deferred (see FUTURE_FEATURES.md) |
|---|---|---|
| **App shell** | Splash screen, Home screen, classic CAD layout, command search, Light and Dark themes, undo/redo, autosave and crash recovery | Mouse gestures, selection breadcrumbs, multiple viewports, toolbar customisation UI |
| **Sketching** | 2D sketches on planes and planar faces, core entities, geometric relations, dimensions, constraint solver, status colours, trim/extend/offset/mirror, projecting model edges | 3D sketch, sketch text, blocks, sketch pictures (splines are a v1.1 candidate) |
| **Part features** | Extrude, Revolve, Sweep, Loft (add and cut), Fillet, Chamfer, Shell, Draft, Hole (simple + "Hole Builder Lite"), Linear/Circular Pattern, Mirror, reference planes/axes/points/coordinate systems, Combine, Move/Copy Body, equations and global variables, materials, mass properties, measure, section view, appearances | Sheet metal, weldments, surfaces, variable fillet, rib, dome, wrap, configurations, modelled threads |
| **Assemblies** | Insert components, fixed/floating, move/rotate with remaining freedom, standard mates, mate error reporting, interference check, BOM data, hide/suppress/transparency | Advanced and mechanical mates, exploded views, in-context editing, component patterns, motion |
| **Drawings** | Sheets and templates (ISO and ANSI), three-view layout, model/projected/section/detail/isometric views, hidden-line projection, model-dimension import, auto-dimension, centre marks, hole callouts, notes, BOM table and auto balloons, **one-click Auto Drawing**, PDF/DXF/SVG export | GD&T, surface-finish and weld symbols, broken/broken-out/auxiliary views, revision tables, DWG |
| **Data exchange** | Native `.blpart` / `.blasm` / `.bldraw`, STEP AP214/AP242 and IGES import/export, STL/OBJ/glTF export | 3MF, DWG, JT, proprietary kernel and native CAD formats |
| **Extensibility** | Embedded Python API, headless command line, Python plugins | C++ plugin ABI, plugin marketplace |
| **Shipping** | Windows 10/11 x64 installer and portable zip, file associations, signed binaries, update check, local crash dumps | Linux, macOS, Microsoft Store, delta updates |

---

## 3. Research: the classic desktop CAD layout

Most history-based desktop modellers have shared the same basic screen arrangement for many
years, and people carry its habits from one tool to the next. BrokenLiquid uses that classic
arrangement.

### 3.1 Screen layout

The main elements of the classic layout:

| # | Element | # | Element |
|---|---|---|---|
| 1 | Menu bar | 6 | Selection breadcrumbs |
| 2 | Toolbars | 7 | Model tree filter |
| 3 | Tabbed command toolbar | 8 | Model tree |
| 4 | Configurations panel | 9 | Status bar |
| 5 | Command property panel | | |

Around the graphics area: a transparent view toolbar, search, a help menu and a resource
sidebar on the right.

### 3.2 Component findings and BrokenLiquid decisions

| Classic component | What it does | BrokenLiquid v1 decision |
|---|---|---|
| **Menu bar** | Menus, quick-access tools, search, help | **Adopt.** Logo menu flyout, quick-access buttons (New, Open, Save, Print, Undo, Redo, Rebuild, Options), command search box, Help |
| **Tabbed command toolbar** | Context-sensitive tabbed toolbar; toolbars embedded according to document type | **Adopt** as the *Command Bar*: tabs per document type, large icon + label buttons, flyout groups |
| **Model tree** | Outline of the active part, assembly or drawing; rollback bar; filter; arrow-key navigation; folders; history folder; list of rebuild errors and warnings; feature dependency view | **Adopt** as the *Feature Tree*: rollback bar, filter, folders, History, rebuild issues, parent/child view. **Skip:** attached-documents folder, favourites, comments, markups, tags |
| **Pop-out tree** | Shows the tree over the graphics area while a command panel is open | **Adopt** (cheap and useful) |
| **Command property panel** | Title bar; OK, Cancel, detailed preview, help, keep visible (pin); message box; collapsible group boxes; selection boxes that highlight when active; Tab-key navigation; split bar; floatable and dockable | **Adopt** as the *Property Panel* with all listed behaviours. Active selection box uses the BL accent colour |
| **Configurations panel** | Create, select and view multiple configurations | **Defer** |
| **Selection breadcrumbs** | Context view of the current selection's hierarchy | **v1.1** |
| **In-viewport view toolbar** | Transparent toolbar in each viewport with the common view tools; custom views appear in the view orientation flyout | **Adopt** as the *View HUD*: Zoom to Fit, Zoom to Area, Previous View, Section View, View Orientation, Display Style, Hide/Show Items, Edit Appearance, View Settings |
| **Graphics area** | Reference triad, triad manipulator, origin, view toolbar, OK/Cancel corner, right-click advance selection, callouts and handles, shaded and dynamic previews, system colours, 1/2/4 viewports, image capture, video recording | **Adopt:** triad, origin, Confirm Corner, callouts/handles, previews, colour options, image capture. **Defer:** multiple viewports, video |
| **Resource sidebar** | Resources, a reusable parts library, file explorer, view palette, appearances/scenes/decals, custom properties | **Adopt** as the *Resource Pane*: Home/Resources, File Explorer, View Palette, Appearances, Properties. The parts library is deferred |
| **Status bar** | Tool descriptions, rebuild indicator, sketch status and pointer coordinates, measurements of the selection, "editing part in assembly" message, unit-system switcher | **Adopt** all of these |
| **Start-up dialog** | Home, recent files, learning material, alerts; online sign-in | **Adopt** as the *Home screen*: Home, Recent, Learn. No login, no alerts feed |
| **Commands, menus, toolbars** | Menu bar, mouse gestures, toolbars, repeat last command, recent commands (last ten unique), customisation, multi-touch | **Adopt:** repeat last command, recent commands, keyboard customisation. **v1.1:** mouse gestures. **Later:** toolbar customisation, touch |
| **Drag handles** | Create and modify geometry with drag handles and rulers | **v1.1** (extrude-depth arrows first) |
| **Message bars** | Document-specific information bar at the top of the window | **Adopt** (read-only, imported geometry, rebuild needed, recovered file) |
| **Display overview** | Display settings of every item in one list | **Defer** |

### 3.3 Interaction conventions

These are the habits people bring with them from other modellers. BL supports all of them in v1
unless marked.

- **Accepting a feature:** Enter or OK; Esc or Cancel; right-click → OK; or the **Confirm
  Corner** (OK/Cancel icons, top-right of the graphics area; Exit Sketch/Cancel Sketch while
  sketching). Pressing **D** moves the OK/Cancel buttons to the pointer and back.
- **Property panel selection boxes:** one box is active at a time; picks in the graphics area or
  tree go into the active box; the right-click advance pointer moves to the next box.
- **Live preview:** a translucent preview of the feature updates as values change, before OK.
- **Sketch status colours:** under-defined entities are blue, fully defined are black, over-defined
  are red. The status bar shows the sketch state. A sketch does not have to be fully defined
  before it is used in a feature. BL adds a colour-blind-safe alternative palette.
- **Over-defined sketches:** BL offers "make this dimension driven" and a repair assistant
  (*Sketch Doctor*).
- **Rollback bar:** drag the bar in the tree to temporarily show the model at an earlier step;
  new features insert at the bar.
- **Repeat last command** with Enter; **recent commands** list (last ten unique).
- **Quick Bar on the S key** and a small **context toolbar** next to the pointer after a selection.
- **Box selection:** drag left→right selects items fully inside; right→left selects items crossed.
- **Mouse navigation:** see §7.6.

### 3.4 BrokenLiquid terminology

BL gives each part of its interface a clear, consistent name. Generic CAD terms (extrude, fillet,
mate, sketch, view palette) keep their usual meaning.

| Idea | BrokenLiquid term |
|---|---|
| Tabbed, context-sensitive command toolbar | Command Bar |
| History tree of the document | Feature Tree |
| Options panel of the running command | Property Panel |
| Sets of dimension and suppression values per part | Variants (future) |
| Transparent view toolbar in the viewport | View HUD |
| Resource sidebar on the right | Resource Pane |
| OK/Cancel icons in the graphics area | Confirm Corner |
| Drag handles and rulers on features | Live Handles |
| Shortcut toolbar on a key | Quick Bar |
| Radial mouse-gesture menu | Gesture Wheel (future) |
| Sketch repair assistant | Sketch Doctor |
| Guided hole creation | Hole Builder |
| Library of standard fasteners and parts | Standard Parts Library (future) |
| List of rebuild errors and warnings | Rebuild Issues |
| Collect a design and its references for sharing | Pack & Bundle (future) |
| Automatic tolerancing | Auto-Tolerance (future) |
| Start-up screen | Home screen |
| Native part / assembly / drawing files | `.blpart` / `.blasm` / `.bldraw` |

### 3.5 Small extras for v1

The classic layout has hardly changed in years, so BL can aim at it without chasing a moving
target. A few small touches are worth having from the start:

- One-click **Reset to defaults** in Options (and per-section reset).
- **File-name sanitising** on save (leading and trailing spaces stripped).
- **Coordinate systems** as first-class reference geometry.

---

## 4. Research: modelling, assembly and drawing logic

### 4.1 Part building logic

1. A new part opens from a template with the **origin** and three default planes
   (**Front, Top, Right**).
2. The user selects a plane or planar face and starts a **sketch**.
3. Sketch entities are constrained with **relations** (horizontal, coincident, tangent…) and
   **dimensions**. Status colours show what is still free.
4. The sketch feeds a **feature** (extrude, revolve…), which adds or removes material.
5. Features stack in **history order** in the tree. Each feature has parents (the sketch, the faces
   or edges it references) and may have children.
6. Editing any feature or dimension **regenerates** everything downstream.
7. The **rollback bar** lets the user insert features earlier in history.

BL follows this history-based, parent/child model throughout.

### 4.2 Assembly logic

- The **first component** inserted is fixed at the assembly origin. Later components **float**
  with six degrees of freedom until mates remove them.
- **Standard mates:** angle, coincident, concentric, distance, lock, parallel,
  perpendicular, tangent. Each has an alignment option (aligned / anti-aligned) and a flip.
- **Advanced mates:** limit, linear/linear coupler, path, profile centre, symmetric,
  width. These are deferred to v1.x.
- Conflicting mates are reported as errors and the component is flagged in the tree.
- Dragging a component moves it only within its remaining degrees of freedom.

### 4.3 Drawing logic

- A **three-view** command creates three related orthographic views. The top and side views are
  aligned to the front view.
- The **View Palette** offers images of the eight standard orientations (Front, Right, Top, Back,
  Left, Bottom, Current, Isometric) plus custom views, to drag onto a sheet. A placed view starts
  projected-view placement automatically.
- **Model dimensions** brings in the dimensions and annotations that already exist in the model.
- **Auto-dimensioning** adds horizontal and vertical reference dimensions as baseline, chain or
  ordinate.
- **Auto-ballooning** places balloons outside the view border with leaders that don't cross.
- A document setting can add balloons, and dimensions marked for drawings, to new views
  automatically.

These are usually separate commands. BL's **Auto Drawing** chains them into one command that
produces a usable first-draft drawing (§6.9).

---

## 5. Technology decision

### 5.1 Requirements for the stack

1. A real **B-rep solid-modelling kernel** (booleans, fillets, shelling, STEP), free to ship.
2. **Native performance** for regeneration, hidden-line projection and large assemblies.
3. A mature **desktop UI toolkit** with dockable panels, trees, dense forms and HiDPI support.
4. **Long-term reach:** scripting for users, plugins, AI and automation, and a path to Linux and
   macOS.
5. Licences that allow free distribution and keep BrokenLiquid's own licence choice open.

### 5.2 Options considered

| Option | Kernel | Verdict |
|---|---|---|
| **C++ + Qt 6 + Open CASCADE (OCCT) + embedded Python** | OCCT, native | **Chosen.** Native speed, the kernel's own language, proven CAD UI stack, Python for reach |
| C# / .NET + WPF or WinUI + OCCT via C++/CLI wrapper | OCCT through interop | Good Windows UI, but every kernel call crosses a hand-maintained interop layer; Windows-only UI; two-language complexity in the hottest code |
| Rust + egui/Slint + truck or Fornjot | Pure-Rust kernels | Excellent language, but the Rust B-rep kernels are early-stage and OCCT bindings are partial. Revisit for isolated modules later |
| Electron/Tauri + opencascade.js (WebAssembly) | OCCT compiled to WASM | Slower kernel, memory limits, heavy runtime. Good fit for a future **web viewer**, not the main app |
| Python + PySide6 + pythonOCC | OCCT via Python | Fastest to prototype, but slow at scale and harder to package. Python is used as the *scripting layer* instead |
| Fork an existing open-source CAD application (LGPL) | OCCT | Large head start, but brings a different UI paradigm, a different scene graph and a large legacy codebase. Making it feel like the classic layout means fighting it. Use open-source CAD code as a **reference** and reuse individual LGPL components |

### 5.3 Decision

| Layer | Choice | Why |
|---|---|---|
| **Core language** | **C++20** (MSVC on Windows; Clang/GCC later) | Same language as the kernel and UI toolkit; zero-overhead access to OCCT; OCCT 8 requires at least C++17 |
| **Geometry kernel** | **Open CASCADE Technology 8.0.x** | The only mature, free B-rep kernel. Booleans, fillets, offsets, sweeps, lofts, STEP/IGES, mass properties and hidden-line removal for drawings. 8.0.0 shipped May 2026 with 500+ changes since 7.9; 8.0.1 maintenance release July 2026 |
| **UI toolkit** | **Qt 6 Widgets** (6.8 LTS or newest 6.x at project start) | Industry standard for desktop CAD; dockable panels, trees, property forms, HiDPI, accessibility support, cross-platform. Widgets suit dense tool UIs better than QML |
| **3D viewport** | **OCCT visualisation (AIS/V3d, OpenGL)** inside a Qt `QOpenGLWidget` | Built-in shaded/HLR display, sub-shape selection, highlighting, clipping planes, view cube, instancing. OCCT ships a Qt viewer sample to start from |
| **Scripting & plugins** | **Embedded CPython 3.13+** via **pybind11** (or nanobind) | Python is the common language of engineering, data and AI. Gives users macros, automation and plugins without a C++ toolchain |
| **Build** | **CMake + Ninja + vcpkg** (manifest mode), **CMakePresets** | Reproducible dependency builds; CI-friendly |
| **Installer** | **Inno Setup 6** for v1 (WiX MSI later if needed) | Simple, free, per-user installs, file associations, signing hooks |

**Why this holds up in the long run:**

- *Optimisation:* everything performance-critical (kernel, regeneration, projection, rendering)
  runs as native C++ with no interop boundary. Regeneration and projection run on worker threads.
- *Accessibility for future features:* the Python API makes BL scriptable by non-C++ developers
  and is the natural home for AI features, CAM, FEA hand-off and batch tools. Qt and OCCT are
  cross-platform, so Linux and macOS ports later need no rewrite. Qt also provides UI accessibility
  (screen readers, keyboard navigation, scaling).

### 5.4 Third-party libraries and licences

| Library | Purpose | Licence (verify at adoption) |
|---|---|---|
| Open CASCADE Technology 8 | B-rep kernel, visualisation, STEP/IGES, HLR | LGPL-2.1 with OCCT exception |
| Qt 6 (Base, Gui, Widgets, OpenGLWidgets, Svg, PrintSupport) | UI, PDF/SVG output, printing | LGPL-3 (dynamic linking) |
| Eigen 3 | Linear algebra for the sketch and mate solvers (own code, ADR 0005/0007) | MPL-2.0 |
| pybind11 / nanobind | Python bindings | BSD-style |
| CPython 3.13+ (embeddable) | Scripting runtime | PSF |
| nlohmann/json | Document model serialisation | MIT |
| minizip-ng or libzip | Native file container | zlib / BSD-3 |
| spdlog + fmt | Logging | MIT |
| muparser or exprtk | Equations and global variables | permissive (BSD/MIT) |
| GoogleTest or Catch2 | Tests | BSD-3 / BSL-1.0 |
| oneTBB (optional, via OCCT) | Parallelism | Apache-2.0 |
| FreeType (via OCCT) | Text rendering | FTL (BSD-style) |
| WinSparkle (later) | Update UI | MIT |
| SARibbon (optional) | Ribbon widget, if the custom Command Bar is not enough | MIT |
| OndselSolver (reference only) | Open-source assembly solver, used as a reference for our own (ADR 0005) | Reported LGPL-2.1; confirm the repo's LICENSE |

**Avoid** GPL-only libraries in the main executable (for example libdxfrw and some open-source
sketch solvers) so BrokenLiquid's own licence stays a free choice. GPL tools can still run as separate
processes later (FEA solvers, mesh tools).

### 5.5 Licensing implications

- Link Qt and OCCT **dynamically** and ship their DLLs unmodified (or publish modifications).
  Users must be able to swap the LGPL libraries.
- Ship all licence texts in `licenses/` and show them in **Help → About → Third-party licences**.
- Qt's guidance warns that some app stores have rules that conflict with the LGPL. Check this
  before any Microsoft Store release.
- BrokenLiquid's own licence is MIT (decided 2026-11-26, §14).

---

## 6. Architecture

### 6.1 Layers

```
┌──────────────────────────────────────────────────────────────────────────┐
│  APP SHELL (Qt Widgets)                                                  │
│  Splash · Main window · Command Bar · Feature Tree · Property Panel ·    │
│  Resource Pane · View HUD · Status bar · Home screen · Options           │
├──────────────────────────────────────────────────────────────────────────┤
│  COMMAND LAYER   Command registry · transactions · undo/redo · selection │
├──────────────────────────────────────────────────────────────────────────┤
│  DOCUMENTS   PartDocument · AssemblyDocument · DrawingDocument           │
│              feature graph · parameters & equations · regeneration       │
├───────────────┬────────────────┬─────────────────┬───────────────────────┤
│  SKETCH       │  FEATURES      │  ASSEMBLY       │  DRAWING              │
│  entities,    │  extrude,      │  components,    │  sheets, views,       │
│  constraints, │  fillet, ...   │  mates, solver, │  HLR projection,      │
│  own solver   │  (OCCT ops)    │  interference   │  annotations, BOM     │
├───────────────┴────────────────┴─────────────────┴───────────────────────┤
│  KERNEL SERVICES   OCCT wrappers · persistent naming · shape cache ·     │
│                    healing · tolerance policy                            │
├──────────────────────────────────────────────────────────────────────────┤
│  RENDER   OCCT AIS/V3d in QOpenGLWidget · selection · highlight · clip   │
├──────────────────────────────────────────────────────────────────────────┤
│  I/O   native zip container · STEP/IGES/STL/OBJ/glTF · PDF/DXF/SVG       │
├──────────────────────────────────────────────────────────────────────────┤
│  PYTHON API (pybind11) · plugin host · headless CLI                      │
└──────────────────────────────────────────────────────────────────────────┘
```

Rule: lower layers never include headers from upper layers. Documents and features compile and
test **without Qt Widgets**, so the headless CLI and Python API reuse them unchanged.

### 6.2 Repository layout

The planned layout. So far the part document and native files live in `src/doc` and the features
in `src/kernel`; the README lists what exists today.

```
broken-liquid/
├─ CMakeLists.txt · CMakePresets.json · vcpkg.json
├─ src/
│  ├─ core/        ids (UUID), units, math, settings, logging, error types
│  ├─ kernel/      OCCT wrappers, persistent naming, shape cache, healing
│  ├─ sketch/      entities, constraints, solver (ADR 0007), profile/region finder
│  ├─ features/    one file per feature (Extrude, Revolve, Fillet, ...)
│  ├─ part/        PartDocument, feature graph, regeneration, equations
│  ├─ assembly/    AssemblyDocument, components, mates, mate solver
│  ├─ drawing/     sheets, views, HLR engine, annotations, BOM, Auto Drawing
│  ├─ io/          native format + migrations, STEP/IGES/STL/OBJ/glTF, PDF/DXF/SVG
│  ├─ render/      viewer widget, presentations, selection, highlight
│  ├─ ui/          main window, Command Bar, panels, Property Panel pages, dialogs
│  ├─ app/         main.cpp, splash, startup pipeline, single-instance
│  └─ python/      bindings module `brokenliquid`, plugin loader, console
├─ resources/      SVG icons, themes (QSS), templates, sheet formats, splash art
├─ tests/          unit, regression models, golden drawings, fuzz
├─ installer/      Inno Setup script, file-association assets
└─ docs/           PLAN.md, FUTURE_FEATURES.md, adr/ (architecture decision records)
```

### 6.3 Core patterns

- **Command registry.** Every action (Extrude, Mate, Zoom to Fit…) is a `Command` with id, label,
  tooltip, SVG icon, default shortcut, an `isEnabled(context)` predicate and a handler. The
  Command Bar, menus, Quick Bar, context toolbar, command search, keyboard map and Python API all
  read from this one registry. Plugins add commands the same way.
- **Transactions and undo.** Every document change happens inside a named transaction
  ("Edit Extrude1"). Undo/redo restores the document model (not geometry), then regenerates.
  Unlimited undo per document, bounded by memory.
- **Selection manager.** Holds typed selections (face, edge, vertex, feature, component, view,
  annotation), each stored as a *persistent reference* (§6.5), never as a raw OCCT pointer.
- **Threading.** Regeneration, HLR projection, STEP import and mass properties run on worker
  threads with progress and cancel. The UI thread only updates presentations. OCCT shapes are
  immutable once produced, so sharing between threads is safe.
- **Error model.** Feature failures never throw to the UI. Each feature stores a status
  (OK / warning / error + message). Failing features show in the tree and in *Rebuild Issues*;
  downstream features are skipped, not deleted.
- **Architecture decision records.** Each major choice gets a short ADR in `docs/adr/`.

### 6.4 Part document and regeneration

```
PartDocument
├─ Origin, Front/Top/Right planes (fixed features)
├─ Features[]          ordered history (the Feature Tree)
│   └─ Feature         id (UUID), name, type, parameters, references[], suppressed,
│                      status, cachedResult {shape, namingMap}
├─ Parameters          global variables + equations (dependency-sorted)
├─ Material, Appearances, Custom properties, Units
└─ RollbackIndex
```

- **Regeneration:** when feature *k* changes, re-run features *k..n* in order. Each feature takes
  the previous body and produces a new body plus a naming map. Results are cached per feature, so
  rollback and undo are instant until something actually changes.
- **Dependency graph:** built from references (sketch → feature, face → sketch plane,
  edge → fillet). Used for parent/child display, delete warnings ("Fillet2 depends on this") and
  to skip unaffected features later (v1 regenerates linearly from the first changed feature).
- **Multibody:** a part can contain several solid bodies. Features choose "merge result" or
  "new body", as usual in CAD.
- **Equations:** `Sketch1.D1 = Width / 2` style. Parsed with muparser/exprtk; dependency
  order resolved by topological sort; cycles reported as errors.

### 6.5 Persistent naming (the biggest architectural risk)

The **topological naming problem**: after an upstream edit, OCCT may renumber faces and edges, so
a fillet that referenced "edge 12" now hits the wrong edge. Open-source CAD tools lived with this
for years, and recent element-naming algorithms only *mitigate* it: they detect broken references
and only auto-repair when confident. BL has to design for this from the start.

**Approach: generative names + geometric fallback**

1. **Never store indices.** References store an `ElementName` plus a small geometric signature.
2. **Name by origin.** Each feature records how every output face/edge/vertex was created, using
   OCCT's `BRepBuilderAPI_MakeShape::Generated()`, `Modified()` and `IsDeleted()` history:
   - Extrude side face → `Extrude1/Side(Sketch1/Line3)` (sketch entity ids are stable UUIDs)
   - Caps → `Extrude1/StartCap`, `Extrude1/EndCap`
   - Edges → the sorted pair of adjacent face names: `E[Extrude1/Side(Sketch1/Line3) | Extrude1/EndCap]`
   - Vertices → the sorted triple of face names
   - Boolean results → modified faces inherit their source names; new faces get tool names
   - Fillet face → `Fillet2/Blend(<name of the edge it replaced>)`
3. **Resolve after regeneration:** exact name match → if several (face split), pick by
   signature and raise a warning → if none, geometric match (surface type, normal, centroid,
   area) above a confidence threshold → otherwise mark the reference **broken** in the tree and
   ask the user. Never guess silently.
4. **Guide users toward stable models:** default sketch placement on reference planes, easy
   "sketch on plane offset from face", and a warning icon when a reference was repaired.
5. **Spike in Phase 0:** compare our own naming map against OCCT's OCAF `TNaming` framework and
   published element-map approaches. Record the decision as an ADR.

Assemblies (mates) and drawings (dimensions) use the same persistent references, so this one
system protects the whole product.

### 6.6 Sketch subsystem

| Group | v1 contents |
|---|---|
| **Entities** | Point, Line, Centreline, Midpoint line, Rectangle (corner, centre, 3-point corner, 3-point centre, parallelogram), Circle (centre, perimeter), Arc (centre-point, tangent, 3-point), Slot (straight, centre-point), Polygon, construction toggle |
| **Tools** | Sketch fillet, sketch chamfer, Offset, Project Edges (copy model edges into the sketch), Trim, Extend, Mirror, Move/Copy/Rotate/Scale, linear and circular sketch patterns |
| **Relations** | Horizontal, Vertical, Coincident, Midpoint, Collinear, Parallel, Perpendicular, Tangent, Concentric, Equal, Symmetric, Fix, Merge |
| **Dimensions** | One Dimension command, the type follows the picks: linear (horizontal/vertical/aligned), angular, radius, diameter, arc length; driving vs driven; double-click to edit; values accept equations and units ("25.4mm", "1in") |
| **Inference** | Snaps (endpoint, midpoint, centre, quadrant, intersection, on-curve), inference lines, automatic relations while drawing (horizontal/vertical/coincident/tangent) |
| **Solver** | Own minimum-norm Gauss–Newton on Eigen ([ADR 0007](adr/0007-sketch-solver-own.md)). Degrees-of-freedom analysis drives status colours and the status-bar text: *Under Defined / Fully Defined / Over Defined* |
| **Profiles** | Region finder builds closed loops and faces (with holes) from the sketch, so the user can pick regions for features (contour selection) |

### 6.7 Part features in v1

| Feature | Key options | OCCT basis |
|---|---|---|
| Extrude and Extrude Cut | Blind, Through All, Up To Vertex, Up To Surface, Mid Plane; Direction 2; draft angle; flip side to cut; merge result | `BRepPrimAPI_MakePrism`, `BRepFeat_MakeDPrism`, booleans |
| Revolve and Revolve Cut | Axis from centreline or edge; angle; two directions | `BRepPrimAPI_MakeRevol` |
| Sweep and Sweep Cut | Profile + path; keep normal constant / follow path | `BRepOffsetAPI_MakePipeShell` |
| Loft and Loft Cut | Ordered profiles; optional guide curves (v1.1) | `BRepOffsetAPI_ThruSections` |
| Fillet | Constant radius; edges, faces, features; tangent propagation | `BRepFilletAPI_MakeFillet` |
| Chamfer | Distance-distance, angle-distance | `BRepFilletAPI_MakeChamfer` |
| Shell | Faces to remove, thickness, outward option | `BRepOffsetAPI_MakeThickSolid` |
| Draft | Neutral plane, faces, angle | `BRepOffsetAPI_DraftAngle` |
| Hole / Hole Builder Lite | Simple, counterbore, countersink, tapped (cosmetic thread); ISO metric and ANSI inch size tables; positions from sketch points | Revolved profile + boolean cut; thread metadata stored for drawing callouts |
| Linear / Circular Pattern | Features or bodies; spacing, count, two directions; skip instances | `gp_Trsf` copies + boolean fuse/cut |
| Mirror | Features, faces or bodies about a plane | `BRepBuilderAPI_Transform` + boolean |
| Reference geometry | Plane (offset, angle, 3 points, parallel at point, normal to curve, mid-plane), Axis, Point, Coordinate System | `gp_Pln`, `gp_Ax1`, `gp_Ax3` |
| Combine | Add / subtract / common between bodies | `BRepAlgoAPI_Fuse/Cut/Common` |
| Move/Copy Body | Translate/rotate with dimensions | `BRepBuilderAPI_Transform` |
| Imported body | Dumb solid from STEP/IGES as a base feature; can be built on | `STEPCAFControl_Reader` |

**Evaluate tools:** Measure (distance, angle, radius, area, perimeter between any picks),
Mass Properties (volume, mass, centre of mass, inertia via `BRepGProp`), Section View (live clip
plane), Check Geometry (`BRepCheck_Analyzer`), Rebuild Issues list.

**Materials:** small built-in library (steels, aluminium alloys, ABS, PLA, nylon, brass, wood)
with density, plus user materials in JSON.

**Robustness policy:** fuzzy booleans (`SetFuzzyValue`), `ShapeFix` healing on import, tolerance
checks after each feature, and a clear error when OCCT fails. Never a half-built body.

### 6.8 Assembly subsystem

- **Components:** references to `.blpart` / `.blasm` files (relative path + file UUID) plus a
  transform. Same part used many times = **shared geometry, instanced display**
  (`AIS_ConnectedInteractive`), so memory grows with unique parts, not instances.
- **States:** fixed / floating; hidden; suppressed; transparent; isolate.
- **Mates (v1):** Coincident, Parallel, Perpendicular, Tangent, Concentric, Distance, Angle, Lock,
  each with aligned/anti-aligned and flip. Mates live in a *Mates* folder in the tree and under
  each component.
- **Mate solver:** our own on Eigen, with OndselSolver as a reference
  ([ADR 0005](adr/0005-assembly-solver.md)).
  - Each floating component has 7 parameters (position + unit quaternion).
  - Each mate becomes residual equations between referenced geometry (planes, axes, points).
  - Solve with Levenberg–Marquardt from the current positions (minimal movement = predictable
    behaviour). Split the assembly into independent rigid clusters first for speed.
  - The Jacobian rank gives remaining degrees of freedom: drive drag behaviour and
    *under/fully/over-defined* status per component.
  - Redundant or conflicting mates are detected and named in the error message.
- **Drag to move:** solve with the dragged point as a soft target so motion respects mates.
- **Interference detection:** pairwise bounding-box filter, then `BRepAlgoAPI_Common` volume;
  results listed and highlighted in red.
- **BOM data:** per component: part number, description, material, quantity, custom properties.
  Feeds drawing BOM tables.
- **Saving:** assembly saves only its own file; part edits save to their own files. Missing
  references open a "locate file" dialog.

### 6.9 Drawing subsystem and Auto Drawing

**Building blocks**

| Item | v1 contents |
|---|---|
| Sheets | ISO A4–A0 and ANSI A–E; landscape/portrait; multiple sheets per drawing |
| Templates | Sheet format (border, title block) as an editable file. Title-block fields bind to properties (`{PartNumber}`, `{Material}`, `{Scale}`, `{Sheet}`, `{Date}`, `{DrawnBy}`) |
| Standards | **ISO** (first-angle projection) and **ANSI** (third-angle). Line weights and types per standard: thick visible, thin dashed hidden, chain centrelines; text heights; arrow styles; decimal places |
| Views | Three-view layout, Model View (eight orientations, like the View Palette), Projected, Section (full; hatched cut faces), Detail (circle + scaled view), Isometric (HLR lines, optional shaded) |
| Engine | OCCT hidden-line removal: **`HLRBRep_PolyAlgo`** (fast, polygonal) for interactive preview, **`HLRBRep_Algo`** (exact) for final output. Extract visible sharp edges, optional tangent edges, hidden edges, silhouettes. Run per view on worker threads; cache by model revision |
| 2D↔3D link | Each projected 2D edge keeps the persistent name of its 3D source edge, so dimensions stay attached after model changes. **Phase 0 spike:** prove this mapping with HLR output |
| Annotations | Dimension (linear, aligned, angular, radius, diameter, ordinate), centre marks, centrelines, notes with property links, hole callouts (from Hole Builder data), cosmetic thread display, balloons |
| Tables | Bill of Materials (top-level, parts-only, indented), general tables |
| Associativity | Model change → views re-project → annotations re-attach via persistent names → anything that cannot re-attach is shown as *dangling* (distinct colour) and listed |
| Export | PDF (vector, via `QPdfWriter`), SVG (`QSvgGenerator`), DXF (own minimal R2000 ASCII writer; avoids GPL libraries), PNG; print via `QPrinter` |

**Auto Drawing pipeline (one click: "Make Drawing")**

1. **Pick settings:** template, standard (ISO/ANSI), sheet size. Defaults come from Options and
   the dialog is optional.
2. **Choose the base view:** score each of the six principal orientations by projected area,
   number of visible circular edges and number of feature dimensions that are true-length in that
   view. The best score becomes the Front view (user can override).
3. **Lay out views:** Front + Top + Right (or Left/Bottom per projection angle) + Isometric in the
   free corner. Add a section view automatically when the part has internal features
   (holes/cuts hidden in all three views).
4. **Choose a scale** from the preferred series (1:1, 1:2, 1:5, 1:10, 2:1, 5:1…) so all views fit
   inside the drawing area with margins for dimensions.
5. **Import model dimensions:** take sketch and feature dimensions, place each **once** in the view
   where its entities appear true-length and visible (no duplicate dimensions across views).
6. **Fill gaps:** always add overall length/width/height. Add baseline dimensions for any feature
   still not located.
7. **Add centre marks and centrelines** for holes and cylinders; **hole callouts** for Hole Builder
   holes (for example "4× Ø6.6 THRU, ⌴ Ø11 ↧ 6.4").
8. **Arrange annotations:** move dimensions outside the view outline, stagger parallel dimensions
   by standard spacing, remove overlaps (greedy placement + local repulsion pass).
9. **Assemblies:** isometric view, **BOM table** and **auto balloons** (sorted clockwise, leaders
   not crossing) in addition to the views.
10. **Title block:** fill from document properties; scale and sheet numbers automatic.
11. **Result:** an editable drawing, not a picture. Every view and dimension can be moved or
    changed afterwards.

**Success target:** for the benchmark parts (§12), at least 80% of auto drawings need no manual
change to be readable and fully dimensioned for manufacture of simple parts. Dimension placement
quality is an R&D item; expect iteration.

### 6.10 Rendering and viewport

- **Widget:** OCCT `V3d_View` in a `QOpenGLWidget`, MSAA anti-aliasing, gradient background.
- **Display styles** (the usual set): Shaded with Edges, Shaded, Hidden Lines
  Removed, Hidden Lines Visible, Wireframe. Perspective toggle.
- **Selection:** face/edge/vertex/body/component with pre-highlight on hover; selection filters;
  "Select Other" for hidden entities.
- **Helpers:** reference triad (bottom-left), origin markers, `AIS_ViewCube` (optional, off by
  default to keep the classic look), section clipping via `Graphic3d_ClipPlane`, temporary
  axes, dimension display in the model.
- **Previews:** translucent yellow preview for features before OK.
- **Performance:** tessellation quality tied to document "image quality" setting; instanced
  components; frustum culling handled by OCCT; target 60 fps (§11).
- **Fallback:** if OpenGL 3.3 is not available (old GPUs, VMs), warn and offer the software
  OpenGL fallback (Mesa `opengl32.dll`) bundled as an optional component.

### 6.11 Python API and headless use

```python
import brokenliquid as bl

part = bl.new_part(units="mm")
sk = part.sketch(on="Front")
rect = sk.rectangle_center((0, 0), width=100, height=60)
sk.dimension(rect.width, 100)
base = part.extrude(sk, depth=20)
part.fillet(base.edges(parallel_to="Z"), radius=5)
part.hole(face=base.end_cap, kind="counterbore", size="M6",
          positions=[(-35, -20), (35, -20), (-35, 20), (35, 20)])
part.save("plate.blpart")

dwg = bl.drawing.auto(part, standard="ISO", sheet="A3")
dwg.export("plate.pdf")
```

- **In-app:** Python console panel, macro run/record (record = v1.1), plugins.
- **Plugins:** folder per plugin in `%APPDATA%\BrokenLiquid\plugins\` with a `plugin.toml`
  manifest (name, version, min BL version, commands, panels). Loaded after startup; failures are
  isolated and reported, never fatal.
- **Headless CLI:** `BrokenLiquid.exe --headless script.py`, plus `blcli` verbs:
  `blcli convert in.step out.stl`, `blcli drawing part.blpart --pdf out.pdf`,
  `blcli massprops part.blpart`. Same core libraries, no window.

---

## 7. UI specification

### 7.1 Main window layout

```
┌──────────────────────────────────────────────────────────────────────────────────────┐
│ [BL▾]  New Open Save Print │ Undo Redo │ Rebuild Options     Plate.blpart   [⌕ Search commands…] [?] │
├──────────────────────────────────────────────────────────────────────────────────────┤
│  Features │ Sketch │ Evaluate                                         ← Command Bar tabs│
│  [Extrude][Revolve][Sweep][Loft] │ [Extr Cut][Rev Cut][Sweep Cut][Loft Cut][Hole]      │
│  [Fillet▾][Pattern▾][Shell][Draft] │ [Ref Geometry▾][Combine][Move/Copy] │ [Equations] │
├────────────────┬───────────────────────────────────────────────────────────┬─────────┤
│ [🌲][📝][⚙]    │            ⤢ ⬚ ↶ ◧ ⧉ ◐ 👁 🎨 ⚙      ← View HUD          │ Resource│
│ ⌕ filter…      │                                                  ┌──┬──┐  │  Pane   │
│ Plate          │                                                  │✓ │✗ │  │ ─────── │
│  ▸ History     │                                                  └──┴──┘  │ Home    │
│  ◆ Material    │                                   Confirm Corner ↗        │ Files   │
│  ▭ Front Plane │                                                           │ View    │
│  ▭ Top Plane   │                    GRAPHICS AREA                          │ Palette │
│  ▭ Right Plane │                                                           │ Appear- │
│  ⊥ Origin      │                                                           │ ances   │
│  ▣ Extrude1    │                                                           │ Proper- │
│  ◜ Fillet1     │                                                           │ ties    │
│  ━━ rollback ━━│   ⟂ triad                                                 │         │
├────────────────┴───────────────────────────────────────────────────────────┴─────────┤
│ Ready                Extrude depth: 20 mm      Fully Defined   X 12.0  Y 4.5     mm ▾ │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

Left panel tabs: **Feature Tree**, **Property Panel**, **Display/Properties**. When a command
opens the Property Panel it replaces the tree, and the tree flies out over the graphics area.
Panels can float and dock. Multiple documents open as tabs (or tiled windows).

### 7.2 Command Bar tabs per document type

| Document | Tab | Commands |
|---|---|---|
| Part | **Features** | Extrude · Revolve · Sweep · Loft │ Extrude Cut · Revolve Cut · Sweep Cut · Loft Cut · Hole Builder │ Fillet▾ (Fillet, Chamfer) · Pattern▾ (Linear, Circular, Mirror) · Shell · Draft │ Reference Geometry▾ (Plane, Axis, Point, Coordinate System) · Combine · Move/Copy Body │ Equations |
| Part | **Sketch** | Sketch (enter/exit) · Dimension │ Line▾ · Rectangle▾ · Slot▾ · Circle▾ · Arc▾ · Polygon · Point │ Fillet▾ · Offset · Project Edges · Trim▾ · Mirror · Transform▾ · Sketch Pattern▾ │ Add Relation · Show/Delete Relations · Construction · Sketch Doctor |
| Part | **Evaluate** | Measure · Mass Properties · Section View · Check Geometry · Rebuild Issues |
| Assembly | **Assembly** | Insert Components · Mate · Move Component · Rotate Component · Fix/Float · Hide/Show · Interference Detection · Make Drawing |
| Assembly | **Sketch / Evaluate** | As for parts (layout sketches, measure, mass properties of the assembly) |
| Drawing | **View Layout** | **Auto Drawing** ★ · Three Views · Model View · Projected View · Section View · Detail View · Update Views |
| Drawing | **Annotation** | Dimension · Model Dimensions · Auto Dimension · Note · Balloon · Auto Balloon · Centre Mark · Centreline · Hole Callout · BOM Table · Table |
| Drawing | **Sketch** | 2D sketch tools for sheet geometry |
| Drawing | **Sheet** | Sheet Properties · Edit Title Block · Add Sheet · Export PDF/DXF/SVG |

### 7.3 Property Panel specification

- **Header:** feature icon and name; buttons OK (✓, Enter), Cancel (✗, Esc), Preview toggle,
  Help, Pin (keep open for repeated use).
- **Message box:** one or two lines telling the user the next step.
- **Group boxes:** collapsible, with optional enable checkbox (e.g. *Direction 2*, *Draft*).
- **Selection boxes:** one active at a time (accent border + tint); picks from graphics area or
  tree; right-click → Delete / Clear Selections; auto-advance to the next box when filled;
  hovering an item highlights it in the model.
- **Inputs:** numeric spin boxes accept units and equations (`20mm`, `=Width/2`); mouse-wheel
  steps; values with a linked equation show an `Σ` marker.
- **Keyboard:** Tab through controls, Space toggles checkboxes, arrow keys move between options.
- **Live preview** updates on every change (debounced ~100 ms; heavy features preview on a
  worker thread).
- **Implementation:** each feature declares its panel in a small schema (groups, fields, selection
  boxes, validation). A generic builder creates the Qt widgets, so plugins get native-looking
  panels for free.

### 7.4 Feature Tree specification

- Node types: document, History folder, Material, planes, origin, sketches, features, bodies
  folder, mates folder, components, sheets, views.
- Icons with state overlays: error (red ✕), warning (yellow !), suppressed (grey), under-defined
  sketch prefix `(-)`, over-defined `(+)`, as is common in CAD.
- **Rollback bar:** drag up/down; right-click → Roll to End / Roll to Previous.
- **Context menu:** Edit Feature · Edit Sketch · Suppress/Unsuppress · Hide/Show · Rename (F2) ·
  Delete (with dependency warning) · Parent/Child… · Rollback to here · Add to new folder.
- Drag to reorder (blocked when it would break dependencies, with a message why).
- Filter box at the top; selection in the graphics area auto-expands and scrolls to the node.

### 7.5 Home screen

Shown after the splash unless a file was opened from Explorer.

- **Home:** large buttons for New Part, New Assembly, New Drawing, Open; a grid of recent files with
  thumbnails (read from the file's embedded `thumb.png`).
- **Recent:** recent files and folders, pin, remove from list, open containing folder.
- **Learn:** built-in interactive tutorials (first part, first assembly, first drawing), keyboard
  map, link to local documentation.
- First run only: choose **units** (mm / inch) and **drafting standard** (ISO / ANSI), theme.

### 7.6 Mouse and keyboard defaults (all rebindable)

| Action | Default |
|---|---|
| Rotate view | Middle-mouse drag (middle-click an edge/vertex first to rotate about it) |
| Pan | Ctrl + middle-mouse drag |
| Zoom | Mouse wheel (direction configurable), Shift + middle-mouse drag, Z / Shift+Z |
| Zoom to fit | F |
| Standard views | Ctrl+1 Front · Ctrl+2 Back · Ctrl+3 Left · Ctrl+4 Right · Ctrl+5 Top · Ctrl+6 Bottom · Ctrl+7 Isometric · Ctrl+8 Normal To |
| View orientation pop-up | Space |
| Rotate view in steps | Arrow keys (15°), Shift+Arrow (90°) |
| Quick Bar | S |
| Move Confirm Corner to pointer | D |
| Repeat last command | Enter |
| Rebuild / force rebuild | Ctrl+B / Ctrl+Q |
| Command search | Ctrl+K |
| Rename in tree | F2 |
| New / Open / Save / Undo / Redo | Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Z / Ctrl+Y |
| Next document | Ctrl+Tab |
| Sketch-mode single keys | L line, C circle, R rectangle, A arc, P point, D Dimension, X construction; T trim and O offset when those tools land. Enter/Esc end the current chain or value box, never the sketch |

### 7.7 Options

Options are split into application settings and document settings:

- **Application settings** (per user, `%APPDATA%\BrokenLiquid\settings.json`): general, colours
  (incl. colour-blind palette), display, performance, file locations, autosave/backup, mouse and
  keyboard, theme, update check (off by default), "Reset to defaults" per page and for all.
- **Document settings** (stored inside each file): drafting standard, units and precision,
  dimension/annotation fonts and arrows, image quality (tessellation), material.

### 7.8 Visual identity and themes

- **Name treatment:** wordmark "Broken**Liquid**"; logo = a liquid droplet split by a clean
  crack line, the two halves slightly offset (fluid → engineered).
- **Palette (draft):** Ink Navy `#0E1A2B` · Liquid Cyan `#22C3E6` (accent) · Mercury
  `#C9D3DE` · Signal Orange `#FF7A3D` (warnings) · Steel `#5B6B7F` (secondary text).
- **Themes:** *Light* (default; light-grey chrome, white-to-pale-blue viewport gradient, the
  familiar CAD look) and *Dark* (charcoal chrome, graphite gradient). Implemented with QSS +
  icon tinting; all icons are SVG for crisp HiDPI.
- **Icons:** original two-tone SVG set (outline + accent fill) at 16/24/32 px.
- **Accessibility:** full keyboard reach, Qt accessibility names on all controls, UI scale
  setting, colour-blind palettes for sketch status and interference, respect Windows
  "reduce motion".

---

## 8. Splash screen and startup

### 8.1 Splash design

- **Window:** 760 × 440 logical px (DPI-scaled), frameless, rounded 12 px corners, soft drop
  shadow, centred on the screen where the app was launched.
- **Artwork:** full-bleed render of a chrome/liquid-metal droplet frozen mid-splash. The left half
  is smooth fluid; the right half breaks into crisp faceted shards with thin cyan edge lines:
  *broken liquid*, fluid ideas turning into engineered solids.
  Deep navy background with subtle vignette.
- **Text:** bottom-left wordmark **BrokenLiquid** with tagline *Parametric CAD · Free · Local*.
  Bottom-right: `v1.0.0 · build 1234`. Small status line above a 2 px accent progress bar
  ("Loading geometry kernel…", "Loading plugins…", "Restoring workspace…").
- **Community models:** each release's splash can show a model made in BrokenLiquid, with a
  "Model by …" credit.
- **Motion:** 250 ms fade-and-scale-in (0.98 → 1.0); a soft highlight sweeps across the droplet
  every ~2.5 s; progress bar eases between steps; 200 ms cross-fade into the main window. All
  motion disabled when Windows "reduce motion"/animations are off.
- **Rules:** never artificially delay startup; minimum on-screen time ~700 ms only to avoid a
  flash; `--nosplash` flag and an Options toggle; never shown in headless mode.

### 8.2 Implementation notes

- Frameless `QWidget` with `Qt::SplashScreen | Qt::FramelessWindowHint` and translucent
  background (for rounded corners and shadow); artwork as 1× and 2× WebP/PNG; overlay painted
  with `QPainter`; animations with `QPropertyAnimation`.
- **First paint under 400 ms:** link the large OCCT DLLs with MSVC `/DELAYLOAD` so they load
  *after* the splash is visible; initialise Python lazily (first script or plugin use, or after
  the main window shows).
- Startup tasks run on a worker thread and report progress to the splash through signals.

### 8.3 Startup sequence

1. Process start → **single-instance check** (`QLocalServer`); if BL is already running, pass the
   file path to it and exit.
2. Show splash (first paint).
3. Load settings, theme, fonts; init logging and crash handler.
4. Load the kernel; create a test OpenGL context (fallback prompt if it fails).
5. Register commands, load icons and templates.
6. Load plugins (isolated; failures reported after startup).
7. Check for autosave files → offer recovery.
8. Show the main window with the Home screen, or open the file passed on the command line.
9. Cross-fade out the splash.

---

## 9. File formats and data exchange

### 9.1 Native files

| Type | Extension |
|---|---|
| Part | `.blpart` |
| Assembly | `.blasm` |
| Drawing | `.bldraw` |
| Templates | `.blpart-t`, `.blasm-t`, `.bldraw-t` (same container, flagged as template) |

**Container:** a ZIP file (like `.docx`) that anyone can unzip and inspect. The layout as built
is in [ADR 0008](adr/0008-native-file-format.md):

```
manifest.json        format id, document type, schema version, app version, build, save time
model.json           feature tree, sketches, parameters, equations, properties, appearances
shapes/<id>.brep     OCCT binary B-rep of imported bodies (built geometry is rebuilt on open)
thumb.png            256 × 256 preview (Home screen, Explorer thumbnails; planned)
```

- **Schema versioning** from day one: each release has migration functions `vN → vN+1`. Old files
  always open; newer files show a clear "made with a newer version" message.
- **Assemblies** store component references as relative path + absolute fallback + file UUID; a
  UUID match finds moved files.
- **Drawings** reference models the same way, and cache projected views for fast opening.
- **Safe save:** write to a temp file, flush, then atomic replace; keep one `.bak`.
- **Autosave** every N minutes (default 10) to `%LOCALAPPDATA%\BrokenLiquid\autosave\`.

### 9.2 Exchange formats (v1)

| Format | Import | Export | Via |
|---|---|---|---|
| STEP AP214 / AP242 | ✓ (names, colours, assembly structure) | ✓ | OCCT `STEPCAFControl` (XDE) |
| IGES | ✓ | ✓ | OCCT `IGESCAFControl` |
| OCCT BREP | ✓ | ✓ | OCCT `BRepTools` |
| STL | — | ✓ (quality settings) | OCCT `StlAPI` |
| OBJ | — | ✓ | OCCT `RWObj` |
| glTF 2.0 / GLB | — | ✓ | OCCT `RWGltf` |
| PDF / SVG / PNG (drawings) | — | ✓ | Qt |
| DXF (drawings) | — | ✓ | own R2000 writer |

Native files of commercial CAD tools are **not** supported: the formats are proprietary. Users
exchange models with other tools through STEP (see FUTURE_FEATURES.md for options).

---

## 10. Windows packaging and shipping

### 10.1 Build

- **Toolchain:** MSVC x64 (Visual Studio 2022 or newer), C++20, CMake + Ninja, vcpkg manifest
  mode with a pinned baseline. If vcpkg's `opencascade` port lags OCCT 8, build OCCT from source
  as a pinned CMake external project.
- **Release flags:** `/O2 /GL` + `/LTCG` link-time optimisation; PGO later. Keep PDB symbol files
  for every release (needed to read crash dumps).
- **Code quality:** clang-format, clang-tidy, MSVC `/W4`, AddressSanitizer builds in CI.

### 10.2 Deployment layout

```
%LOCALAPPDATA%\Programs\BrokenLiquid\     (per-user, no admin, the default)
  or C:\Program Files\BrokenLiquid\       (per-machine option)
├─ BrokenLiquid.exe · blcli.exe
├─ Qt6*.dll + platforms/, styles/, imageformats/   (via windeployqt)
├─ TK*.dll (OCCT) · tbb*.dll · freetype.dll
├─ python313.dll + python313.zip (embeddable stdlib) + site-packages/brokenliquid
├─ resources/ · templates/ · materials/
└─ licenses/  (all third-party licence texts)

%APPDATA%\BrokenLiquid\        settings.json, keymap.json, plugins\, user materials
%LOCALAPPDATA%\BrokenLiquid\   cache\, autosave\, logs\, crashdumps\
Documents\BrokenLiquid\        default save location, user templates
```

### 10.3 Installer

- **Inno Setup 6** script in `installer/`:
  - per-user install by default (no UAC prompt), per-machine optional;
  - Start-menu and optional desktop shortcut;
  - **file associations** for `.blpart`, `.blasm`, `.bldraw` with their own icons and ProgIDs;
  - optional "Open with BrokenLiquid" for `.step/.stp/.iges`;
  - optional software-OpenGL fallback component;
  - clean uninstall that leaves user files alone.
- **Portable ZIP** build (settings stored next to the exe when a `portable.flag` file exists).
- **Explorer thumbnails:** a small `IThumbnailProvider` COM DLL that reads `thumb.png` from the
  ZIP (Phase 6 stretch; otherwise v1.1).
- **Later:** WiX MSI for enterprise deployment; MSIX/Store after checking LGPL compatibility.

### 10.4 Code signing

Unsigned installers trigger SmartScreen "Windows protected your PC" warnings. Options:

- **Azure Artifact Signing** (formerly Trusted Signing): Basic and Premium tiers with a monthly
  fee and a signature quota; billing starts when the account is created; eligibility rules apply.
  Check current pricing and eligibility before committing.
- **OV code-signing certificate** from a public CA (hardware token or cloud HSM).

Sign `BrokenLiquid.exe`, `blcli.exe`, all BL DLLs and the installer with SignTool + RFC 3161
timestamp. SmartScreen reputation builds up over downloads.

### 10.5 Updates, crashes, privacy

- **Update check:** off by default, opt-in on first run. One HTTPS GET of a public JSON release
  feed; no identifiers sent. Shows release notes and a download link. WinSparkle (MIT) later for
  in-app install.
- **Crash handling:** unhandled-exception filter writes a minidump + last log lines to
  `%LOCALAPPDATA%\BrokenLiquid\crashdumps\`; next launch offers to open the folder. No automatic
  upload.
- **Versioning:** SemVer (`1.2.3`); stable and beta channels.

### 10.6 CI/CD (GitHub Actions or similar)

1. Windows runner, restore vcpkg binary cache.
2. Configure + build (Release and RelWithDebInfo), run unit and regression tests.
3. Run `windeployqt`, assemble layout, build installer + zip.
4. Sign (release tags only), upload installer, zip and PDBs as artifacts.

### 10.7 System requirements (target)

Windows 10 22H2 or Windows 11, x64; GPU with OpenGL 3.3+ (software fallback available);
8 GB RAM (16 GB recommended for large assemblies); 1.5 GB disk.

---

## 11. Quality: testing and performance budgets

### 11.1 Tests

| Kind | What it covers |
|---|---|
| Unit | Sketch solver cases, equations, units parsing, naming-map rules, file migrations |
| **Model regression** | ~50 Python scripts that build benchmark parts/assemblies; compare volume, area, face/edge counts, centre of mass to golden values. Catches kernel upgrades and feature regressions |
| **Naming torture tests** | Build a model, change upstream dimensions/sketches, assert downstream fillets, mates and drawing dimensions still hit the right geometry |
| Drawing golden files | Auto Drawing output exported to SVG and diffed against approved baselines (with tolerance) |
| File round-trip | Save → load → save produces identical `model.json`; STEP export → import keeps volume |
| Fuzz | Malformed native files and STEP files must fail cleanly, never crash |
| UI smoke | Qt Test: launch, create part, extrude, save, close; command registry consistency |

### 11.2 Performance budgets (mid-range laptop, integrated GPU)

| Metric | Budget |
|---|---|
| Splash first paint | < 400 ms |
| Launch → Home screen usable | < 3 s |
| Regenerate a 50-feature part | < 1 s |
| Feature preview update | < 150 ms typical |
| Viewport frame rate (500k triangles) | ≥ 60 fps |
| Open a 200-component assembly | < 10 s |
| Auto Drawing for a typical part (4 views) | < 3 s |
| Idle memory (empty part) | < 400 MB |

---

## 12. Roadmap

Effort ranges assume **one experienced full-time C++ developer**. Two or three developers can run
phases 2–5 partly in parallel once Phase 1 is done.

| Phase | Version | Duration | Deliverables | Exit criteria |
|---|---|---|---|---|
| **0. Foundations & spikes** | 0.0 | 3–5 wks | Repo, CMake/vcpkg, CI, logging, settings; OCCT viewer in a Qt window; splash + main-window skeleton. **Spikes:** persistent naming prototype vs OCAF; HLR projection of a box-with-holes to SVG with 2D↔3D edge mapping; two-part mate solver prototype (the sketch solver spike moved to Phase 2, ADR 0007) | A STEP file opens, rotates, pans and zooms in a BL window. Spike ADRs written |
| **1. App shell** | 0.1 "Viewer" | 4–6 wks | Command registry, Command Bar, Feature Tree pane, Property Panel framework, Resource Pane, View HUD, status bar, Home screen, themes, command search, undo/redo framework, Options dialogs, document tabs, STEP import as dumb solid, measure | A user can import, view, measure and section a STEP model with the full BL layout |
| **2. Sketcher** | 0.2 "Sketch" | 6–8 wks | Entities, inference/snaps, relations, dimensions, solver + status colours, trim/extend/offset/mirror/project edges, region finder, sketch on planes/faces, Sketch Doctor basics | A typical bracket profile can be drawn and fully defined quickly with keyboard + mouse |
| **3. Part modelling** | 0.3 "Part" | 8–12 wks | Part document, regeneration, all v1 features, reference geometry, persistent naming, rollback, edit/suppress/reorder, equations, materials, mass properties, appearances, native save/load with migrations, STEP/STL/OBJ/glTF export, autosave | All 10 benchmark parts modelled; upstream edits pass naming torture tests |
| **4. Assemblies** | 0.4 "Assembly" | 6–8 wks | Assembly document, components, instancing, mate solver, drag within DOF, mate errors, interference detection, BOM data, STEP assembly import/export | All 3 benchmark assemblies fully mated and moving correctly |
| **5. Drawings** | 0.5 "Drawing" | 8–12 wks | Sheets, templates, title blocks, all v1 views, HLR engine, annotations, model dimensions, Auto Dimension, hole callouts, BOM + auto balloons, **Auto Drawing**, PDF/DXF/SVG export, associative updates | Auto Drawing meets the 80% target on benchmarks; exported PDFs print correctly at scale |
| **6. Polish & ship** | 1.0 | 4–6 wks | Final splash art and icon set, Python console + docs, Learn tutorials, installer, signing, file associations, (thumbnails), update check, crash dumps, performance pass, public beta, bug-fix | Performance budgets met; zero known data-loss bugs; signed installer passes a clean-machine install test |

**Total:** roughly **39–57 weeks** (9–13 months) for one developer.

**Benchmark set (acceptance models)**

- *Parts:* L-bracket with holes and fillets · flanged shaft (revolve, chamfers) · shelled
  enclosure with draft and bosses · mounting plate with linear pattern of slots · flange with
  circular hole pattern · lofted handle · swept tube/rail · mirrored symmetrical bracket · part
  with counterbore/countersink holes · imported STEP part with added features.
- *Assemblies:* bench vice (~10 parts) · shaft + bearings + pulleys (concentric/distance) ·
  hinge (angle mate, moving).

---

## 13. Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Topological naming breaks references | Models silently wrong, and nobody trusts the tool again | Design in Phase 0 (§6.5); torture tests in CI; never silent repair; encourage plane-based sketches |
| OCCT boolean/fillet failures on edge cases | Features fail to build | Fuzzy booleans, healing, tolerance policy, clear errors, fall back to previous result; report minimal repro cases upstream |
| Auto-dimension quality | Drawings need manual clean-up | Scope as "good first draft"; benchmark-driven heuristics; easy manual editing; iterate after v1 |
| 2D↔3D edge mapping from HLR is harder than expected | Drawing dimensions not associative | Phase 0 spike; fallback: re-match projected edges to 3D edges geometrically |
| Scope creep toward a full commercial CAD suite | v1 never ships | FUTURE_FEATURES.md parking lot; feature freeze at Phase 6 start; ADR needed to add scope |
| Large assembly performance | Slow, unusable at scale | Instancing, background loading, cached shapes; lightweight mode later (FUTURE_FEATURES.md) |
| LGPL compliance mistakes | Legal exposure | Dynamic linking, licence texts shipped, third-party licence dialog, checklist in release process |
| Brand and naming confusion | Legal risk | Distinct name, terminology (§3.4), icons and artwork; get advice before public launch |
| GPU/OpenGL driver problems | Crashes or blank viewport | GL capability check at startup, software fallback, driver blacklist notes |
| Single-developer bus factor | Stalls | ADRs, this plan, tests as documentation, clean module boundaries |

---

## 14. Open decisions

| # | Decision | Recommendation |
|---|---|---|
| 1 | BrokenLiquid's own licence (open source? which licence?) | **Decided 2026-11-26: MIT** (`LICENSE`). Compatible with dynamically linked LGPL Qt/OCCT |
| 2 | Default drafting standard and units | ISO + millimetres, chosen again on first run |
| 3 | File extensions | `.blpart` / `.blasm` / `.bldraw` (check for clashes with common apps) |
| 4 | Assembly solver: own vs OndselSolver | Own (Eigen), confirmed by the Phase 0 spike |
| 5 | Installer technology | Inno Setup for v1 |
| 6 | Code-signing route and budget | Azure Artifact Signing if eligible, else OV certificate |
| 7 | Distribution channel | Own website / GitHub Releases for v1 |

---

## 15. Sources

**Technology**
- [OCCT 8.0.0 release announcement](https://dev.opencascade.org/content/open-cascade-technology-800-release)
- [OCCT 8.0.1 release discussion](https://github.com/Open-Cascade-SAS/OCCT/discussions/1415)
- [Open Cascade Technology (Wikipedia)](https://en.wikipedia.org/wiki/Open_Cascade_Technology)
- [OCCT HLRBRep_PolyAlgo reference](https://dev.opencascade.org/doc/refman/html/class_h_l_r_b_rep___poly_algo.html)
- [OCCT HLRBRep reference (7.9)](https://dev.opencascade.org/doc/occt-7.9.0/refman/html/class_h_l_r_b_rep.html)
- [planegcs (PlaneGCS WebAssembly port, LGPL)](https://github.com/Salusoft89/planegcs)
- [Qt: obligations of the GPL and LGPL](https://www.qt.io/licensing/open-source-lgpl-obligations)
- [Qt 6.10 released](https://www.qt.io/blog/qt-6.10-released)
- [SARibbon (MIT)](https://github.com/czyt1988/SARibbon)
- [Azure Artifact Signing pricing](https://azure.microsoft.com/pricing/details/artifact-signing/)
