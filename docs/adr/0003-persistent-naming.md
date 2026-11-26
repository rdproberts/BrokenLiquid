# 0003. Persistent naming: generative names built from OCCT modelling history

- **Status:** Accepted (Phase 0 spike passed; face and edge names, references and the naming
  torture suite implemented in Phase 3: `src/kernel/Naming.*`, `tests/test_naming.cpp`; vertices
  follow when a feature needs them)
- **Date:** 2026-11-25
- **Spike:** [`spikes/naming`](../../spikes/naming/main.cpp), run as CTest `spike_naming`

## Context

Downstream features, mates and drawing dimensions must keep pointing at "the same" face or edge
after an upstream edit. OCCT renumbers sub-shapes freely, so storing an index ("edge #5") breaks
silently. PLAN.md §6.5 proposed generative names from OCCT's `Generated` / `Modified` /
`IsDeleted` history, with a geometric fallback and no silent repairs.

## Spike

Feature tree: Sketch1 (rectangle, segment ids `s1..s4` owned by the sketch) → Extrude1 →
optional through-hole cut → Fillet1 (r = 3) on the vertical edge named
`Extrude1/Side(s2) ^ Extrude1/Side(s3)` (the edge shared by the side faces swept from `s2` and
`s3`). The name is resolved after each rebuild through the prism's and the boolean's history.
The same edge was also remembered by index, as naive code would.

| Variant (edit after v1) | Edges | Generative name | Index #5 |
|---|---|---|---|
| v1 original 40 × 30 × 10 | 12 | correct corner, fillet removes 19.314 mm³ (expected 19.314) | correct |
| v2 width 40 → 60, hole added upstream | 15 | correct | **wrong edge** (opposite corner) |
| v3 v2 + chamfer added at the opposite sketch corner | 18 | correct | **wrong edge** |
| v4 chamfer at the filleted corner itself (edge really gone) | 18 | **reported missing** | **silently filleted another edge** |

A plain dimension change plus one upstream feature was enough to break index naming. Generative
naming found the right edge every time and, when the geometry really disappeared, said so
instead of guessing. Resolution cost is negligible (whole spike < 0.1 s).

## Decision

1. Every feature records a **name map** from OCCT history: faces named by origin
   (`Feature/Side(sketchEntityId)`, `Feature/StartCap`, `Feature/EndCap`, tool faces from
   booleans, `Fillet/Blend(edgeName)`), edges as the sorted pair of adjacent face names,
   vertices as the sorted triple.
2. References store the **name plus a small geometric signature** (surface/curve type, centroid,
   normal or direction, size). The name decides; the signature breaks ties when a face was split
   and drives the fallback.
3. Resolution order: exact name → (several matches) pick by signature and **warn** → (none)
   geometric match above a confidence threshold, flagged as **repaired** → otherwise **missing**:
   the feature shows an error and the user re-picks. Never a silent guess.
4. Sketch entities get stable ids (UUIDs) at creation; ids never get reused or renumbered.
5. Our own map, not OCAF `TNaming`. `TNaming` was reviewed but not prototyped: it binds naming
   to the OCAF label tree and its selector logic is hard to debug or extend, while the history
   API we need (`BRepBuilderAPI_MakeShape`) is the same one it uses internally. Published
   element-map approaches follow the same "name by origin" idea and are our reference for edge
   cases.

## Consequences

- Each feature implementation must publish its history (one function per feature type). Features
  whose OCCT algorithm has incomplete history (some offset/shell cases) need a geometric pass.
- Imported STEP bodies have no history: references to them are signature-only, which is weaker,
  and the UI says so.
- Phase 3 adds a **naming torture suite** to CI: dimension changes, inserted/removed sketch
  segments, reordered features, split faces, patterns, shell. Each case asserts the referenced
  geometry, like the spike's ground-truth check.
- Mates (ADR 0005) and drawing dimensions (ADR 0004) reuse the same references.
- Repair thresholds (implementation): a face is accepted by signature alone below a mismatch of 2%
  of the model size, an edge below 0.5%. Edges need the tighter limit because a chamfer or fillet
  next to a lost edge leaves neighbours only about its own size away, and those must not pass for
  the lost edge (torture case "chamfer upstream replaces the filleted edge").
