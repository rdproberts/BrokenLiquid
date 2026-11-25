# 0001. Tech stack: C++20, Qt 6 Widgets, Open CASCADE 8, embedded Python

- **Status:** Accepted
- **Date:** 2026-11-25

## Context

BrokenLiquid needs a real B-rep kernel (booleans, fillets, shells, STEP, hidden-line removal),
native speed for regeneration and drawing projection, a desktop UI toolkit that handles dense
dockable panels, and a long-term path to scripting, plugins, AI features and other operating
systems. Everything must be free to ship. Options compared in [PLAN.md §5.2](../PLAN.md#52-options-considered):
C#/.NET + OCCT interop, Rust + young kernels, Electron + opencascade.js, Python + pythonOCC, and
forking FreeCAD.

## Decision

| Layer | Choice |
|---|---|
| Language | C++20 (MSVC 2022 on Windows) |
| Kernel | Open CASCADE Technology 8.0.x (8.0.1 at project start) |
| UI | Qt 6 Widgets; the viewport is OCCT AIS/V3d in a `QOpenGLWidget` |
| Scripting | Embedded CPython via pybind11 (Phase 6; API designed from Phase 1) |
| Build | CMake presets + Ninja + vcpkg manifest ([ADR 0002](0002-dependencies-via-vcpkg.md)) |
| Logging / settings | spdlog, nlohmann/json |
| Tests | GoogleTest + CTest; spikes run as CTest cases |

Qt and OCCT are linked dynamically (LGPL). GPL-only libraries stay out of the executable.

## Consequences

- Kernel, regeneration and projection run with no interop boundary; one language in the hot path.
- C++ raises the contribution bar. Python scripting is the answer for users and plugin authors.
- Phase 0 confirmed the OCCT + Qt pairing works: the viewer follows OCCT's own Qt sample
  (`AIS_ViewController` + wrapped Qt framebuffer), and STEP import runs off the UI thread.
- Qt, OCCT and the code base are cross-platform, so Linux/macOS later needs a build matrix and
  platform glue (crash handler, single instance, paths), not a rewrite.
