# 0004. Drawings: OCCT hidden-line removal + geometric 2D↔3D edge mapping

- **Status:** Accepted (Phase 0 spike passed; scale testing in Phase 5)
- **Date:** 2026-11-25
- **Spike:** [`spikes/hlr_svg`](../../spikes/hlr_svg/main.cpp) — run as CTest `spike_hlr_svg`

## Context

Drawing views need clean visible, hidden and tangent lines, fast enough to regenerate on model
changes, and each drawn line must know which model edge it came from so dimensions stay attached
(PLAN.md §6.9). OCCT's HLR output (`HLRBRep_HLRToShape`) returns new 2D edges with no link back to
the model.

## Spike

An L-bracket (two holes, filleted inner corner; 11 faces, 27 edges, Y-up like the app) projected
into Front, Top, Right and Isometric views in third-angle layout, written to SVG. Each drawn line
was mapped to a model edge by projecting every model edge with the same `HLRAlgo_Projector` and
checking that all sample points of the drawn line lie on that projection (tolerance 0.087 mm =
0.1% of the part diagonal). When several edges project onto the same line (stacked along the
view direction), the visible line takes the nearest edge and a hidden line the farthest.

| View | Exact HLR | Poly HLR | Visible / tangent / hidden lines | Visible lines mapped |
|---|---|---|---|---|
| Front | 2.3 ms | 0.7 ms | 10 / 1 / 14 | 100% (20 depth ties) |
| Top | 1.4 ms | 0.6 ms | 10 / 1 / 12 | 100% (18 ties) |
| Right | 1.9 ms | 0.9 ms | 7 / 0 / 21 | 100% (24 ties) |
| Isometric | 1.7 ms | 0.9 ms | 23 / 2 / 14 | 100% (0 ties) |

The SVG is correct by inspection ([image](../images/spike-hlr-views.png)): projections align,
holes appear as circles or hidden lines as expected, the fillet's tangent edges come out as
separate "smooth" lines (drawn thin and grey).

## Decision

- **Engine:** `HLRBRep_Algo` (exact) for final views; `HLRBRep_PolyAlgo` (mesh-based) for
  interactive previews of large models. Both run per view on worker threads, cached by model
  revision.
- **2D→3D link:** geometric matching as in the spike, done right after projection, so each drawn
  line carries the **persistent name** (ADR 0003) of its source edge. Silhouette lines of curved
  faces have no source edge; they map to the face (needed for diameters dimensioned on a
  cylinder's outline).
- **Line classes:** visible sharp, tangent (optional, thin), hidden (dashed), silhouette.
- Rendering to sheet, PDF and DXF uses our own 2D line model, not OCCT presentation objects.

## Consequences / follow-ups for Phase 5

- **Remove hidden segments that coincide with visible ones.** HLR returns both; in the spike the
  dashed hidden lines are drawn on top of visible outlines. Add a 2D overlap-removal pass.
- The depth tie-break was not checked against ground truth. Many lines in principal views are
  ties, so Phase 5 tests must assert *which* edge a dimension attaches to.
- Timings are for an 11-face part. Benchmark the exact algorithm on the 10 benchmark parts and a
  2,000-face STEP import before fixing the preview/final split. Budget from PLAN.md §11.2.
- Matching is O(lines × edges) with a bounding-box filter. Fine at this size; add a spatial
  grid if large assemblies make it show up in profiles.
