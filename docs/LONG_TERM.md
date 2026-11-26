# BrokenLiquid: long-term ideas (after v2)

*Draft, November 2026. See also [PLAN.md](PLAN.md) for v1 and
[FUTURE_FEATURES.md](FUTURE_FEATURES.md) for v1.x and v2.*

v1 is about parts, assemblies and drawings. FUTURE_FEATURES.md covers the features that grow
those three (sheet metal, surfacing, configurations and so on). This file is about everything
else people do around a 3D model: simulation, electrical design, piping, 2D drafting, machining,
documentation and keeping track of versions.

None of this is promised. An idea only moves into a release once it has an ADR, a place on the
roadmap and a list of what it depends on.

## Contents

1. [Background](#1-background)
2. [Ground rules](#2-ground-rules)
3. [Overview](#3-overview)
4. [The tracks](#4-the-tracks)
   - [T1 Simulation](#t1-simulation)
   - [T2 Electrical and electronics](#t2-electrical-and-electronics)
   - [T3 Pipes, tubes, ducts and cables](#t3-pipes-tubes-ducts-and-cables)
   - [T4 2D drafting](#t4-2d-drafting)
   - [T5 Manufacturing](#t5-manufacturing)
   - [T6 Automation and reuse](#t6-automation-and-reuse)
   - [T7 Other ways to model](#t7-other-ways-to-model)
   - [T8 Sharing and presenting](#t8-sharing-and-presenting)
   - [T9 Versions and data](#t9-versions-and-data)
   - [T10 Buildings, plant and site](#t10-buildings-plant-and-site)
5. [Rough order](#5-rough-order)
6. [Open-source tools we could use](#6-open-source-tools-we-could-use)

## 1. Background

Most serious CAD setups are more than a modeller. Simulation, electrical design, piping,
documentation, inspection, rendering, machining and file management usually come as add-ons or
extra tiers, and they all work on the same parts, assemblies and drawings. That shared model is
what makes them useful together.

A few patterns stand out:

- Simulation is the most common add-on. The basic level is a guided stress check on a single part.
  Higher levels add vibration, buckling, heat, drop tests and fatigue, then nonlinear materials
  and dynamics. Flow and plastic moulding are usually separate products.
- Electrical work splits three ways: schematics, cables and harnesses routed through the 3D
  assembly, and exchanging board outlines with PCB software.
- Pipes, tubes, ducts and cables are really the same problem: a path through an assembly, filled
  with standard parts, with rules for bends and drawings generated from it.
- A lot of work is still done in 2D (layouts, schematics, floor plans, shop drawings), with
  layers, blocks, paper-space layouts and plenty of scripting.
- Newer tools stand out more for workflow than for any single feature: branching and merging
  designs, writing your own features, and having CAM or electronics in the same program.

Almost all the heavy engines (meshing, FEA, CFD, dynamics, toolpaths, PCB design, rendering)
already exist as open-source projects. What's missing is a friendly, local desktop that connects
them to a parametric model, and that's where BrokenLiquid could help.

## 2. Ground rules

These sit on top of the v1 principles in PLAN.md §1.

1. **Modules, not a monolith.** Each track is its own workspace that plugs its commands, document
   types and panels into the existing command registry. If one track stalls, the rest carry on.
2. **Work with good tools rather than rebuild them.** For PCB layout, 3D-printing slicers and
   film-quality rendering, we exchange files cleanly with existing open-source programs.
3. **Heavy engines run as separate programs.** Solvers and meshers talk to BrokenLiquid through
   files or pipes. That keeps GPL code out of our own executable (PLAN.md §5.5), and a solver crash
   can't take your model down with it.
4. **Offline always works.** Cloud solving, sharing or AI can be optional extras, never
   requirements.
5. **Results live in the tree.** A simulation study, a route or a toolpath is a feature like any
   other. It rebuilds when the model changes and turns red when its references break (ADR 0003).
6. **Be upfront about results.** Analysis tools show their assumptions, mesh quality and limits.
   A coloured plot is not a guarantee, and BrokenLiquid is not a certified analysis tool.
7. **One program, one feel.** Every workspace uses the same Feature Tree, Property Panel, previews
   and OK/Cancel flow, so new areas feel like part of the same app.

## 3. Overview

Tiers and sizes use the same scale as FUTURE_FEATURES.md (C = v3 or later, D = research;
L = 1 to 3 months, XL = more than 3 months for one developer).

| # | Track | First useful step | Needs | Tier | Size |
|---|---|---|---|---|---|
| T1 | Simulation | Stress check on a single part, results shown on the model | Parts, materials, mass properties | C | XL |
| T2 | Electrical and electronics | Bring in a PCB as a component and keep its outline in sync | Assemblies, STEP | C | L to XL |
| T3 | Pipes, tubes, ducts and cables | Tube along a 3D sketch with fittings and a cut list | Assemblies, 3D sketch, standard parts | C | XL |
| T4 | 2D drafting | A `.bldraft` document with layers, blocks, dimensions and DXF/DWG | Drawing code, 2D solver | C | XL |
| T5 | Manufacturing | 2.5D milling from model faces with a toolpath preview | Parts, face naming | C | XL |
| T6 | Automation and reuse | Python rules that drive parameters, and a shaft/gear/bolt calculator | Python API, equations | B to C | L |
| T7 | Other ways to model | Direct editing of imported STEP solids | Parts | B to D | XL |
| T8 | Sharing and presenting | Free read-only viewer and step-by-step assembly instructions | Exploded views, drawings | C | L |
| T9 | Versions and data | Branch and merge a part's history | `.blpart`, local history | C | XL |
| T10 | Buildings, plant and site | IFC export, nothing more for now | STEP/IFC | D | - |

## 4. The tracks

Each track below says what this kind of software usually does, then lists steps for us. Licences
are a starting point and need checking before we use anything.

### T1 Simulation

**Usually covers:** linear stress, vibration (modal), buckling, heat, drop tests and fatigue;
then nonlinear materials, dynamics and composites. Separate tools handle flow and heat transfer
(CFD), plastic moulding and motion. Newer tools add topology optimisation and generative design.

| Step | What | Size |
|---|---|---|
| S1 | Guided stress check on one part: fixtures, loads, material from the part, mesh, stress / displacement / safety-factor plots, PDF report | XL |
| S2 | Studies in the tree; vibration, buckling and steady heat; mesh controls; probes; comparing designs | L |
| S3 | Assemblies: bonded and contact faces between parts, bolts and pins as connectors | XL |
| S4 | Motion studies driven by mates and motors; rigid-body dynamics with gravity, springs and contact | XL |
| S5 | Fatigue from S-N curves; large deformation, plasticity, rubber-like materials | XL |
| S6 | Flow and heat transfer through a CFD engine, with the fluid volume taken from the model | XL |
| S7 | Parameter sweeps over equations and design tables; topology optimisation | XL |
| Later | Moulding, electromagnetics, acoustics | research |

**Possible engines:** Netgen (LGPL) or Gmsh (GPL, separate process) for meshing; CalculiX or
Code_Aster (both GPL, separate process) or Elmer for solving; MFEM or deal.II if we ever embed a
solver; OpenFOAM (GPL) or SU2 (LGPL) for flow; Project Chrono (BSD) for dynamics; VTK for
showing results.

**Watch out for:** results that look right but aren't, because of a bad mesh or wrong boundary
conditions. We'd keep a set of textbook cases (cantilever, plate with a hole, thick cylinder,
beam frequencies) with known answers in CI, and warn when the mesh hasn't converged.

### T2 Electrical and electronics

**Usually covers:** schematics with symbol libraries, wire numbering, terminal strips, PLC I/O,
cabinet layouts and reports. A 3D side that routes wires and harnesses through the assembly,
sends the lengths back to the schematic and flattens the harness for a drawing. Board outlines,
keep-outs and component heights going back and forth with PCB software (STEP, IDF, IDX).

| Step | What | Size |
|---|---|---|
| E1 | Bring in a PCB as a special component (STEP plus outline and holes), reload it when the board changes, and export the outline and keep-outs back as DXF/IDF | L |
| E2 | Simple cable and wire routing with clips, lengths in the BOM | L |
| E3 | A schematic document: symbols, wires, automatic numbering, terminals, reports | XL |
| E4 | Link the schematic to 3D: route harnesses from the connections and make flattened harness drawings | XL |
| No | Our own PCB layout or circuit simulator. Dedicated open-source PCB tools already do this well | - |

**Formats:** STEP, IDF 3.0 and IDX (check its terms). Circuit simulation, if needed, through
ngspice inside the PCB tool.

**Watch out for:** electrical standards (IEC 81346, IEC 60617, NFPA 79) and symbol libraries are
big jobs. Start with E1 and E2, which are mechanical problems.

### T3 Pipes, tubes, ducts and cables

**Usually covers:** a route is a special subassembly holding a path through the assembly, filled
with standard parts: pipes and fittings, bent tubes, ducts or cables. Fitting libraries, bend
radius rules, automatic elbows, spools, cut lists, bend tables and piping isometrics. Plant tools
add P&ID diagrams linked to the 3D model.

| Step | What | Size |
|---|---|---|
| R1 | Tube along a 3D sketch with a bend radius check, a cut list with lengths and angles, and a bend table for a tube bender | L |
| R2 | Pipe routes with fittings from a library, connection points on parts, automatic elbows | XL |
| R3 | Isometric piping drawings and spool sheets | L |
| R4 | A P&ID or schematic that drives the 3D route (shares the editor from E3) | XL |

**Needs:** 3D sketches, the standard parts library and assembly features from FUTURE_FEATURES.md.
Weldment profiles use the same "profile along a path" code.

### T4 2D drafting

**Usually covers:** model space and paper-space layouts, layers, blocks with attributes,
parametric blocks, external references, hatches, dimension and text styles, tables, sheet sets,
2D constraints, drawing compare and a lot of scripting. Industry versions add libraries for
mechanical, electrical, architecture, building services, plant and mapping work, plus tracing of
scanned drawings.

**Why it fits us:** the v1 drawing code already needs sheets, views, annotations, layers and DXF,
and the sketcher already has a 2D constraint solver (ADR 0007). A separate 2D document can reuse
both, and plenty of people still do their layouts and shop drawings in 2D.

| Step | What | Size |
|---|---|---|
| D1 | A `.bldraft` document: lines, arcs, polylines, text, layers, snaps, dimensions, hatches, DXF in and out, PDF printing | XL |
| D2 | Blocks with attributes, a block library, parametric blocks driven by the 2D solver | L |
| D3 | Paper-space layouts with viewports, external references, sheet sets with title-block fields | L |
| D4 | DWG read and write (ODA Drawings SDK, or LibreDWG as a separate GPL process) | M |
| D5 | Image underlays you can calibrate and trace, maybe with automatic tracing | L |

**Watch out for:** DWG can eat a lot of time, so DXF comes first. Scripting goes through the
existing Python API.

### T5 Manufacturing

**Usually covers:** CAM linked to the model (2.5D and 3-axis milling, turning, probing, toolpath
and stock simulation, tool libraries, post-processors, thread data carried over from the design).
3D printing setup (orientation, supports, lattices, slicer hand-off). Inspection drawings with a
balloon on every dimension and a list for inspection reports. Cost estimates, manufacturability
checks and environmental impact estimates.

| Step | What | Size |
|---|---|---|
| M1 | 3MF export, send to slicer, orientation and overhang check | S |
| M2 | Balloon every dimension on a drawing and export the list (CSV/XLSX) for inspection reports | M |
| M3 | 2.5D milling: setups, stock, tools; facing, pockets, contours, drilling, thread milling; toolpath preview; a few common post-processors | XL |
| M4 | Stock removal simulation and collision checks | L |
| M5 | Turning and 3-axis finishing | XL |
| M6 | Cost estimate from material and machine rates; carbon estimate from material and mass | M |
| Later | Lattices for 3D printing, nesting flat patterns | XL |

**Possible engines:** OpenCAMLib (LGPL) for toolpaths, libnest2d (LGPL) for nesting, lib3mf (BSD)
for 3MF, open-source slicers run as separate programs, OpenVDB (MPL) for volumes and lattices.

**Watch out for:** bad G-code can damage machines. Toolpaths always get simulated and are marked
as unchecked until someone has proved them on the machine.

### T6 Automation and reuse

**Usually covers:** rules that change parameters, suppress features and swap parts from a form.
Product configurators. Calculators that size and model shafts, gears, bearings, springs, bolted
joints, belts and keys. Frame generators. Standard parts libraries. Your own features, written in
code, that rebuild like built-in ones. Node-based visual programming.

| Step | What | Size |
|---|---|---|
| A1 | Python rules attached to a part or assembly that run when a parameter changes, with simple input forms | M |
| A2 | Your own features in Python: they take selections, rebuild like built-in features and use face naming | L |
| A3 | Calculators that check against standards and build the geometry: shaft, gear pair, bolted joint, spring, key | L each |
| A4 | A configurator that turns a design table plus rules into parts, drawings and exports in one go | L |
| Later | A node-based editor on top of the Python API | L |

**Needs:** the v1 Python API, equations and configurations. This track is cheap compared with
the others and gives a lot back, so it's a good one to start early.

### T7 Other ways to model

**Usually covers:** direct editing (push and pull faces) alongside the history, including on
imported solids. Freeform subdivision modelling for organic shapes. Mixing meshes with solids for
scans and optimised shapes. Turning point clouds into parts. Generative design from loads and
keep-out areas.

| Step | What | Size |
|---|---|---|
| N1 | Move, offset, delete and replace faces on any solid, saved as a feature | L |
| N2 | Mesh bodies in parts: import STL/OBJ/3MF, section, measure, reference and combine with solids | L |
| N3 | Subdivision bodies converted to B-spline surfaces | XL |
| N4 | Point cloud to part: split into planes, cylinders and freeform areas, and sketch from sections | XL |
| N5 | Topology optimisation in a design space using the T1 solver, cleaned up into a solid | XL |

**Possible engines:** OpenSubdiv (Apache), CGAL (check the licence per module), Open3D (MIT),
OpenVDB (MPL).

### T8 Sharing and presenting

**Usually covers:** exploded views and step-by-step instructions, photoreal rendering, a free
viewer for people without the CAD program, and 3D annotations (PMI) next to or instead of 2D
drawings.

| Step | What | Size |
|---|---|---|
| C1 | A free BrokenLiquid Viewer: open parts, assemblies and drawings read-only, measure, section, mark up | M |
| C2 | Step-by-step assembly instructions from exploded steps, saved as PDF or HTML | L |
| C3 | Path-traced renders with a material library, turntable animations | L |
| C4 | 3D annotations and STEP AP242 PMI export | L |
| C5 | A shareable interactive 3D web page (glTF plus a small viewer), no server needed | L |

### T9 Versions and data

**Usually covers:** vaults with check-in and check-out, revisions, release workflows and
where-used. Cloud tools add continuous versioning and branching and merging of designs.
Lifecycle tools add BOM management and change orders.

| Step | What | Size |
|---|---|---|
| V1 | Local version history per file, with visual compare | M |
| V2 | Branch a part, work on both copies, merge back feature by feature, with conflicts shown on the model | XL |
| V3 | A git-backed project vault with file locking, release states and where-used | L |
| V4 | Change requests and orders, BOM compare between revisions | L |

**Watch out for:** merging parametric models offline is fairly new ground. Start with whole
features (added, removed or edited) rather than changes inside sketches.

### T10 Buildings, plant and site

**Usually covers:** building design (walls, doors, floors, IFC), building services, plant layout,
mapping and site design. These are big, standards-heavy areas with plenty of dedicated software,
open-source included.

**Our position:** not a goal. We'll make sure parts and assemblies export cleanly to IFC (through
IfcOpenShell) so equipment can be dropped into building models, and the 2D workspace (T4) should
be fine for simple floor and site plans. We can revisit this if people ask for it.

## 5. Rough order

This carries on from the order at the end of FUTURE_FEATURES.md (v1.1 to v2.x).

| Version | Focus | Steps |
|---|---|---|
| v3 | Checking and automating designs | S1-S2 (stress, vibration, heat), A1-A3 (rules, custom features, first calculators), C1 (viewer), M1-M2 (3D printing, inspection balloons) |
| v4 | Things that run through assemblies | R1-R3 (tube, pipe, isometrics), E1-E2 (PCB in the enclosure, cables), S3-S4 (assembly contact, motion), V1-V2 (history, branches) |
| v5 | Making and showing | M3-M4 (2.5D CAM and simulation), C2-C3 (instructions, rendering), D1-D3 (2D drafting), N1-N2 (direct editing, mesh bodies) |
| Someday | Long shots | S5-S7 (fatigue, flow, optimisation), E3-E4 (schematics, harnesses), N3-N5 (freeform, scans, generative), V3-V4, AI features |

The order favours tracks that build on what BrokenLiquid will already have (the Python API, face
naming, assemblies and drawings) and that existing open-source engines make realistic.

## 6. Open-source tools we could use

Licences need checking before we adopt anything. GPL and AGPL tools only ever run as separate
programs (PLAN.md §5.4 and §5.5).

| Area | Project | Licence (to check) | Use |
|---|---|---|---|
| Meshing | Netgen | LGPL-2.1 | Tetrahedral meshes for FEA, can be embedded |
| Meshing | Gmsh | GPL-2+ | Meshing, separate process |
| FEA | CalculiX | GPL-2 | Stress and heat solver, separate process |
| FEA | Code_Aster | GPL-3 | Advanced stress solver, separate process |
| FEA | Elmer | LGPL / GPL | Multiphysics |
| FEA library | MFEM, deal.II | BSD-3, LGPL | If we ever embed a solver |
| CFD | OpenFOAM | GPL-3 | Flow and heat transfer, separate process |
| CFD | SU2 | LGPL-2.1 | Flow and shape optimisation |
| Multiphysics | openCFS | MIT | Another multiphysics option |
| Dynamics | Project Chrono | BSD-3 | Multibody dynamics and contact |
| Results | VTK | BSD-3 | Showing simulation results |
| CAM | OpenCAMLib | LGPL-2.1 | Toolpaths |
| Nesting | libnest2d | LGPL-3 | Nesting flat patterns |
| 3D printing | lib3mf | BSD-2 | Reading and writing 3MF |
| Volumes | OpenVDB | MPL-2.0 | Lattices, offsets, optimisation |
| Freeform | OpenSubdiv | Apache-2.0 | Subdivision surfaces |
| Geometry | CGAL | GPL / LGPL per module | Mesh processing |
| Point clouds | Open3D | MIT | Working with scans |
| Circuits | ngspice | BSD-3 | Circuit simulation, through PCB tools |
| Buildings | IfcOpenShell | LGPL-3 | IFC export |
| 2D | LibreDWG | GPL-3 | DWG, separate process (or the commercial ODA SDK) |
| Rendering | Embree, OSPRay, Cycles | Apache-2.0 | Path tracing |
| VR/AR | OpenXR | Apache-2.0 | Design reviews in VR/AR |
