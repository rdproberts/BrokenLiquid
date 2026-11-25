# 0002. Dependencies from vcpkg (manifest mode, release-only dynamic triplet)

- **Status:** Accepted
- **Date:** 2026-11-25

## Context

Qt and OCCT are large. Building them by hand is slow and hard to reproduce; prebuilt installers
pin us to their compiler and feature choices. The developer machine's system drive is small, so
dependency builds, caches and build trees must be able to live on another drive. LGPL compliance
needs dynamic linking.

## Decision

- **vcpkg manifest mode** (`vcpkg.json`) with a pinned `builtin-baseline`. Every dependency
  version is reproducible from the repo.
- **Custom triplet `x64-windows-rel`** (`cmake/triplets/`): x64, dynamic CRT, dynamic libraries,
  **release-only** (`VCPKG_BUILD_TYPE release`). Halves dependency build time and disk use.
- `qtbase` with **default features off** and only what we use (gui, widgets, opengl, concurrent,
  network, png/jpeg, freetype/harfbuzz, testlib, windeployqt). Plus qtsvg, opencascade, eigen3,
  spdlog, nlohmann-json, gtest.
- Because deps are release-only, BrokenLiquid's everyday build type is **RelWithDebInfo**
  (`dev` preset) so the CRT (`/MD`) matches. No Debug configuration. To step through a file,
  add `/Od` to that target locally.
- **Machine-specific locations** go in the git-ignored `CMakeUserPresets.json` and
  `scripts/env.local.ps1`. On the main dev machine everything lives inside the project folder
  `E:\Coding\BrokenLiquid` so the small system drive stays clear: `.toolchain/vcpkg`
  (`VCPKG_ROOT`), `.toolchain/vcpkg-cache` (binary cache), `.toolchain/tmp` (`TEMP`),
  `vcpkg_installed/` (`VCPKG_INSTALLED_DIR`) and `build/`, all git-ignored. The shared
  `CMakePresets.json` stays machine-neutral.
- `scripts/build.ps1` enters the MSVC developer shell, fetches Ninja through vcpkg, then runs
  configure → build → (test) → (run).
- After each build, `windeployqt` copies Qt runtime DLLs and plugins next to the exe; vcpkg's
  app-local step copies OCCT and the rest. The `bin/` folder runs as is.

## Consequences

- First configure builds Qt + OCCT from source (about 1–2 hours on a laptop); later configures hit
  the binary cache in seconds. CI must keep the vcpkg binary cache between runs.
- No debug iterators or debug CRT checks in dependencies. Address/UB sanitizer runs, if wanted,
  need a separate full-debug triplet.
- Upgrading a dependency = moving the baseline (or an override) in one commit, built by CI first.
- The installer (Phase 6) ships the same DLL set `bin/` already holds, plus licence texts.
