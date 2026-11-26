"""Generate BrokenLiquid's two-tone SVG command icons into resources/icons/.

Style (PLAN.md 7.8): 24x24 grid, navy stroke #1F3A5A (swapped for a light tone in the dark
theme at runtime), Liquid Cyan #22C3E6 accent fills. Edit the shapes here and re-run:

    python scripts/make_icons.py
"""
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "resources" / "icons"
S = "#1F3A5A"   # stroke
A = "#22C3E6"   # accent
G = "#8FA3B8"   # grey fill

HEAD = ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="%s" '
        'stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">' % S)

# Isometric cube faces
TOP = "12,3 20,7.5 12,12 4,7.5"
LEFT = "4,7.5 12,12 12,21 4,16.5"
RIGHT = "12,12 20,7.5 20,16.5 12,21"
SHADES = {"top": ("#C9D3DE", 1), "left": (G, 1), "right": ("#6B7F94", 1)}
FOLDER = "M3 7a1.5 1.5 0 0 1 1.5-1.5H9l2 2h8.5A1.5 1.5 0 0 1 21 9v9a1.5 1.5 0 0 1-1.5 1.5h-15A1.5 1.5 0 0 1 3 18z"


def poly(pts, fill="none", op=1.0, dash=False, extra=""):
    d = ' stroke-dasharray="2 2"' if dash else ""
    return f'<polygon points="{pts}" fill="{fill}" fill-opacity="{op}"{d} {extra}/>'


def cube(accent=None, fills=None, dash=False, edges=True):
    out = ""
    for name, pts in (("top", TOP), ("left", LEFT), ("right", RIGHT)):
        fill, op = "none", 1.0
        if fills:
            fill, op = fills[name]
        if accent == name:
            fill, op = A, 0.55
        stroke = "" if edges else f'stroke="{fill}"'
        out += poly(pts, fill, op, dash and accent == name, stroke)
    return out


PLUS = f'<path d="M18 15v6M15 18h6" stroke="{A}" stroke-width="2"/>'
TOPTINT = {"top": (A, .35), "left": ("none", 1), "right": ("none", 1)}

ICONS = {
    # --- File / edit -------------------------------------------------------------------------
    "open": f'<path d="{FOLDER}" fill="{A}" fill-opacity=".3"/><path d="M3 11h18"/>',
    "new-part": cube(fills=TOPTINT) + PLUS,
    "new-assembly": '<g transform="translate(-3 2) scale(.72)">' + cube(fills=TOPTINT)
    + '</g><g transform="translate(8 -1) scale(.72)">' + cube() + "</g>" + PLUS,
    "new-drawing": f'<path d="M5 3h10l4 4v14H5z"/><path d="M15 3v4h4"/><path d="M8 11h8M8 14h8M8 17h5" stroke="{A}"/>',
    "save": f'<path d="M4 4h13l3 3v13H4z"/><rect x="7" y="4" width="8" height="5" fill="{A}" fill-opacity=".4"/>'
    '<rect x="7" y="13" width="10" height="7"/>',
    "print": f'<path d="M7 9V3h10v6"/><rect x="3" y="9" width="18" height="8" rx="1.5" fill="{A}" fill-opacity=".25"/>'
    '<path d="M7 14h10v7H7z"/>',
    "undo": '<path d="M9 14 4 9l5-5"/><path d="M4 9h10a6 6 0 0 1 0 12h-3"/>',
    "redo": '<path d="m15 14 5-5-5-5"/><path d="M20 9H10a6 6 0 0 0 0 12h3"/>',
    "rebuild": '<path d="M20 11a8 8 0 0 0-14.3-4.6L4 8"/><path d="M4 3v5h5"/><path d="M4 13a8 8 0 0 0 14.3 4.6L20 16"/>'
    f'<path d="M20 21v-5h-5"/><circle cx="12" cy="12" r="2.2" fill="{A}" stroke="none"/>',
    "options": f'<circle cx="12" cy="12" r="3" fill="{A}" fill-opacity=".5"/>'
    '<path d="M12 2.5v3M12 18.5v3M2.5 12h3M18.5 12h3M5.3 5.3l2.1 2.1M16.6 16.6l2.1 2.1M5.3 18.7l2.1-2.1M16.6 7.4l2.1-2.1"/>'
    '<circle cx="12" cy="12" r="6.5"/>',
    "exit": '<path d="M14 4h5v16h-5"/><path d="M10 8l-4 4 4 4M6 12h10"/>',
    # --- View --------------------------------------------------------------------------------
    "view-fit": f'<path d="M3 8V3h5M16 3h5v5M21 16v5h-5M8 21H3v-5"/><rect x="8" y="8" width="8" height="8" fill="{A}" fill-opacity=".45"/>',
    "view-zoom-area": f'<rect x="3" y="3" width="11" height="11" stroke-dasharray="2 2"/>'
    f'<circle cx="14" cy="14" r="4.5" fill="{A}" fill-opacity=".3"/><path d="m17.5 17.5 3.5 3.5"/>',
    "view-previous": f'<rect x="9" y="6" width="12" height="12" fill="{A}" fill-opacity=".25"/><path d="M7 8 3 12l4 4M3 12h9"/>',
    "view-section": cube() + f'<polygon points="2,12 14,5 22,10 10,17" fill="{A}" fill-opacity=".5" stroke="{A}"/>',
    "view-orientation": cube(accent="left"),
    "view-style": cube(fills=SHADES),
    "view-front": cube(accent="left"),
    "view-top": cube(accent="top"),
    "view-right": cube(accent="right"),
    "view-back": cube(accent="right", dash=True),
    "view-left": cube(accent="left", dash=True),
    "view-bottom": cube(accent="top", dash=True),
    "view-iso": cube(fills={"top": (A, .25), "left": (A, .45), "right": (A, .65)}),
    "view-normal": f'<polygon points="3,9 15,9 15,21 3,21" fill="{A}" fill-opacity=".45"/><path d="M9 15 19 5M14 4h6v6"/>',
    "style-shaded-edges": cube(fills=SHADES),
    "style-shaded": cube(fills=SHADES, edges=False),
    "style-hlr": cube(),
    "style-hlv": cube() + '<path d="M4 16.5 12 12M12 12l8 4.5" stroke-dasharray="1.5 2" stroke-opacity=".6"/>',
    "style-wireframe": cube() + '<path d="M4 16.5 12 12l8 4.5M12 12V3" stroke-opacity=".7"/>',
    "theme-light": f'<circle cx="12" cy="12" r="4" fill="{A}" fill-opacity=".5"/>'
    '<path d="M12 2v2M12 20v2M2 12h2M20 12h2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>',
    "theme-dark": f'<path d="M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5z" fill="{A}" fill-opacity=".4"/>',
    # --- Features ----------------------------------------------------------------------------
    "extrude": f'<polygon points="3,17 13,17 20,13 10,13" fill="{A}" fill-opacity=".55" stroke="{A}"/>'
    f'<path d="M3 17V9l7-4h10v8M3 9h10l7-4M13 9v8"/><path d="M17 11V3m-2 2 2-2 2 2" stroke="{A}"/>',
    "revolve": f'<path d="M12 2v20" stroke-dasharray="3 2"/><rect x="13" y="7" width="5" height="10" fill="{A}" fill-opacity=".55" stroke="{A}"/>'
    '<path d="M7 17.5a7 3 0 0 0 10 0"/><path d="m15.5 16 1.6 1.6-1.6 1.6"/>',
    "sweep": '<path d="M4 20c0-8 6-8 8-12s4-4 8-4" stroke-dasharray="3 2"/>'
    f'<circle cx="5" cy="17" r="3" fill="{A}" fill-opacity=".55" stroke="{A}"/>'
    '<path d="M2.5 18.5c2 4 6 0 8-4s5-8 9.5-8M7.5 15.5c1.5-2.8 3-5 5-7.5s4-4 7.5-4"/>',
    "loft": f'<rect x="3" y="15" width="8" height="5" fill="{A}" fill-opacity=".55" stroke="{A}"/>'
    f'<circle cx="17" cy="6" r="3.5" fill="{A}" fill-opacity=".35" stroke="{A}"/><path d="M3 15 13.5 6M11 15l9.5-9M11 20l9-12.5"/>',
    "extrude-cut": '<path d="M3 9l7-4h11v9l-7 5H3z"/><path d="M3 9h11l7-4M14 9v10"/>'
    f'<polygon points="6,12 11,12 11,16 6,16" fill="{A}" fill-opacity=".7" stroke="{A}"/>',
    "revolve-cut": '<path d="M4 6h16v12H4z"/><path d="M12 3v18" stroke-dasharray="3 2"/>'
    f'<path d="M9 6v4a3 3 0 0 0 6 0V6" fill="{A}" fill-opacity=".6" stroke="{A}"/>',
    "sweep-cut": f'<path d="M3 6h18v12H3z"/><path d="M5 15c4 0 4-6 8-6s5 3 6 3" stroke="{A}" stroke-width="3" stroke-opacity=".7"/>',
    "loft-cut": f'<path d="M3 6h18v12H3z"/><path d="M7 15h4l3-6h3" stroke="{A}" stroke-width="3" stroke-opacity=".7"/>',
    "hole": '<path d="M2 10l6-4h14l-6 4zM2 10v7h14v-7M16 17l6-4V6"/>'
    f'<ellipse cx="11" cy="8" rx="3.2" ry="1.4" fill="{A}" fill-opacity=".7" stroke="{A}"/>'
    f'<path d="M8.6 8.4v6.6M13.4 8.4v6.6" stroke="{A}" stroke-dasharray="1.5 1.5"/>',
    "fillet": f'<path d="M4 20V10a6 6 0 0 1 6-6h10"/><path d="M4 10a6 6 0 0 1 6-6" stroke="{A}" stroke-width="2.6"/><path d="M4 20h16V4"/>',
    "chamfer": f'<path d="M4 20V9l5-5h11v16z"/><path d="M4 9l5-5" stroke="{A}" stroke-width="2.6"/>',
    "linear-pattern": f'<rect x="2.5" y="9" width="5" height="6" fill="{A}" fill-opacity=".6" stroke="{A}"/>'
    '<rect x="9.5" y="9" width="5" height="6"/><rect x="16.5" y="9" width="5" height="6"/>'
    '<path d="M3 19h18m-3-2 3 2-3 2" stroke-width="1.2"/>',
    "circular-pattern": f'<circle cx="12" cy="12" r="1.2" fill="{S}"/><circle cx="12" cy="4.5" r="2.2" fill="{A}" fill-opacity=".7" stroke="{A}"/>'
    '<circle cx="18.5" cy="8.3" r="2.2"/><circle cx="18.5" cy="15.7" r="2.2"/><circle cx="12" cy="19.5" r="2.2"/>'
    '<circle cx="5.5" cy="15.7" r="2.2"/><circle cx="5.5" cy="8.3" r="2.2"/>',
    "mirror": f'<path d="M12 2v20" stroke-dasharray="3 2"/><polygon points="3,18 9,18 9,6" fill="{A}" fill-opacity=".6" stroke="{A}"/>'
    '<polygon points="21,18 15,18 15,6"/>',
    "shell": f'<path d="M3 8l9-5 9 5v9l-9 5-9-5z"/><path d="M6.5 9.5 12 6.5l5.5 3v6L12 18.5l-5.5-3z" fill="{A}" fill-opacity=".45" stroke="{A}"/>',
    "draft": f'<path d="M7 4h10l3 16H4z" fill="{A}" fill-opacity=".3"/><path d="M7 4v16" stroke-dasharray="2 2"/>'
    f'<path d="M7 14a6 6 0 0 1-1.6 4.2" stroke="{A}"/>',
    "plane": f'<polygon points="2,17 7,7 22,7 17,17" fill="{A}" fill-opacity=".45" stroke="{A}"/>',
    "axis": f'<path d="M4 20 20 4" stroke-dasharray="4 2 1 2"/><path d="M15 4h5v5M9 20H4v-5" stroke="{A}"/>',
    "point": f'<path d="M12 4v5M12 15v5M4 12h5M15 12h5"/><circle cx="12" cy="12" r="2.4" fill="{A}" stroke="{A}"/>',
    "csys": f'<path d="M6 18V4M6 18h14M6 18l-3.5 3.5"/><path d="M4 6l2-2 2 2M18 16l2 2-2 2"/><circle cx="6" cy="18" r="1.8" fill="{A}" stroke="{A}"/>',
    "combine": f'<circle cx="9" cy="12" r="6" fill="{A}" fill-opacity=".35"/><circle cx="15" cy="12" r="6" fill="{A}" fill-opacity=".35"/>',
    "move-copy": f'<rect x="3" y="9" width="9" height="9" fill="{A}" fill-opacity=".4"/><path d="M12 6h6v6M18 6l-6 6"/>',
    "equations": f'<path d="M17 5H7l5 7-5 7h10" stroke-width="2"/><path d="M19 15h3M19 18h3" stroke="{A}" stroke-width="1.8"/>',
    # --- Sketch ------------------------------------------------------------------------------
    "sketch": f'<rect x="3" y="7" width="12" height="12" stroke-dasharray="2 2"/><path d="m14 14 7-7-3-3-7 7-1 4z" fill="{A}" fill-opacity=".5"/>',
    "dimension": f'<path d="M4 6v10M20 6v10"/><path d="M4 11h16" stroke="{A}"/><path d="m7 9-3 2 3 2M17 9l3 2-3 2" stroke="{A}"/><path d="M9 20h6"/>',
    "line": f'<path d="M5 19 19 5"/><circle cx="5" cy="19" r="2" fill="{A}" stroke="{A}"/><circle cx="19" cy="5" r="2" fill="{A}" stroke="{A}"/>',
    "centerline": '<path d="M3 21 21 3" stroke-dasharray="5 2 1 2"/>',
    "rectangle": f'<rect x="4" y="6" width="16" height="12" fill="{A}" fill-opacity=".2"/>'
    f'<circle cx="4" cy="6" r="1.8" fill="{A}" stroke="{A}"/><circle cx="20" cy="18" r="1.8" fill="{A}" stroke="{A}"/>',
    "center-rectangle": f'<rect x="4" y="6" width="16" height="12" fill="{A}" fill-opacity=".2"/><path d="M4 6l16 12" stroke-dasharray="2 2"/>'
    f'<circle cx="12" cy="12" r="1.8" fill="{A}" stroke="{A}"/>',
    "slot": f'<path d="M7 7h10a5 5 0 0 1 0 10H7A5 5 0 0 1 7 7z" fill="{A}" fill-opacity=".2"/><path d="M7 12h10" stroke-dasharray="2 2"/>',
    "circle": f'<circle cx="12" cy="12" r="8" fill="{A}" fill-opacity=".2"/><circle cx="12" cy="12" r="1.6" fill="{A}" stroke="{A}"/>',
    "arc": f'<path d="M4 16a8 8 0 0 1 16 0"/><circle cx="12" cy="16" r="1.6" fill="{A}" stroke="{A}"/><path d="M12 16 4 16" stroke-dasharray="2 2"/>',
    "tangent-arc": f'<path d="M3 18h7a6 6 0 0 0 6-6V5"/><circle cx="10" cy="18" r="1.6" fill="{A}" stroke="{A}"/>',
    "polygon": f'<polygon points="12,3 20,7.5 20,16.5 12,21 4,16.5 4,7.5" fill="{A}" fill-opacity=".2"/>',
    "sketch-point": f'<circle cx="12" cy="12" r="3" fill="{A}" stroke="{A}"/>',
    "sketch-fillet": f'<path d="M4 4v8a8 8 0 0 0 8 8h8"/><path d="M4 12a8 8 0 0 0 8 8" stroke="{A}" stroke-width="2.4"/>',
    "offset": f'<path d="M4 18c2-8 6-12 16-12"/><path d="M8 20c2-6 5-9 13-9" stroke="{A}"/>',
    "convert": f'<polygon points="3,9 11,5 21,9 13,13" fill="{A}" fill-opacity=".3"/><path d="M12 14v6m-2-2 2 2 2-2" stroke="{A}"/>',
    "trim": f'<path d="M3 12h18"/><path d="M12 3v18" stroke-opacity=".4"/><path d="m15 9 6 6M21 9l-6 6" stroke="{A}" stroke-width="2"/>',
    "extend": f'<path d="M20 3v18"/><path d="M3 12h9"/><path d="M12 12h6m-2-2 2 2-2 2" stroke="{A}"/>',
    "sketch-mirror": f'<path d="M12 2v20" stroke-dasharray="3 2"/><path d="M3 18 9 6M21 18 15 6"/><path d="M3 18 9 6" stroke="{A}"/>',
    "relation": f'<path d="M4 20 20 4"/><path d="M4 12l8 8"/><rect x="9" y="13" width="4" height="4" transform="rotate(-45 11 15)" stroke="{A}"/>',
    "construction": f'<path d="M3 20 21 4" stroke-dasharray="3 3" stroke="{A}"/>',
    # --- Evaluate ----------------------------------------------------------------------------
    "measure": f'<g transform="rotate(-30 12 12)"><rect x="2" y="8" width="20" height="8" rx="1" fill="{A}" fill-opacity=".3"/>'
    '<path d="M6 8v3M10 8v4M14 8v3M18 8v4"/></g>',
    "mass-properties": f'<path d="M6 8h12l2 12H4z" fill="{A}" fill-opacity=".35"/><circle cx="12" cy="5" r="2.2"/>',
    "check": f'<path d="M12 3 4 6v6c0 5 3.5 8 8 9 4.5-1 8-4 8-9V6z" fill="{A}" fill-opacity=".25"/><path d="m8.5 12 2.5 2.5 4.5-5"/>',
    "issues": '<path d="M12 3 2 20h20z" fill="#FF7A3D" fill-opacity=".35"/><path d="M12 9v5M12 17v.5"/>',
    # --- Tree / panes / help -----------------------------------------------------------------
    "tree-part": cube(fills=TOPTINT),
    "tree-plane": f'<polygon points="2,18 7,6 22,6 17,18" fill="{A}" fill-opacity=".35"/>',
    "tree-origin": f'<path d="M6 18V5M6 18h13"/><path d="M4 7l2-2 2 2M17 16l2 2-2 2" stroke="{A}"/>',
    "tree-body": cube(fills=SHADES),
    "tree-folder": f'<path d="{FOLDER}" fill="#F5C15C" fill-opacity=".5"/>',
    "tree-material": f'<circle cx="12" cy="12" r="8"/><path d="M6 17 17 6M9 19l10-10M5 13l8-8" stroke="{A}" stroke-width="1.2"/>',
    "tab-tree": f'<path d="M5 4v14M5 8h5M5 13h5M5 18h5"/><rect x="11" y="6" width="9" height="4" fill="{A}" fill-opacity=".4"/>'
    '<rect x="11" y="11" width="9" height="4"/><rect x="11" y="16" width="9" height="4"/>',
    "tab-property": f'<rect x="4" y="3" width="16" height="18" rx="1.5"/><path d="M8 8h8M8 12h8M8 16h5"/>'
    f'<path d="m14.5 16 1.5 1.5 3-3" stroke="{A}" stroke-width="2"/>',
    "pane-home": f'<path d="M3 11 12 4l9 7"/><path d="M5 10v10h14V10" fill="{A}" fill-opacity=".25"/><path d="M10 20v-5h4v5"/>',
    "pane-files": f'<path d="{FOLDER}" fill="{A}" fill-opacity=".3"/>',
    "pane-views": f'<rect x="3" y="3" width="8" height="8" fill="{A}" fill-opacity=".4"/><rect x="13" y="3" width="8" height="8"/>'
    '<rect x="3" y="13" width="8" height="8"/><rect x="13" y="13" width="8" height="8"/>',
    "pane-appearance": f'<circle cx="12" cy="12" r="8.5" fill="{A}" fill-opacity=".2"/><circle cx="9" cy="9" r="1.6" fill="#FF7A3D" stroke="none"/>'
    f'<circle cx="15" cy="9" r="1.6" fill="{A}" stroke="none"/><circle cx="8.5" cy="14.5" r="1.6" fill="{S}" stroke="none"/><circle cx="15" cy="15" r="2"/>',
    "pane-properties": f'<rect x="4" y="3" width="16" height="18" rx="1.5"/><path d="M8 8h3M13 8h3M8 12h3M13 12h3M8 16h3M13 16h3" stroke="{A}"/>',
    "about": f'<circle cx="12" cy="12" r="9" fill="{A}" fill-opacity=".2"/><path d="M12 11v6M12 7.5v.5"/>',
    "graphics": f'<rect x="3" y="4" width="18" height="12" rx="1.5" fill="{A}" fill-opacity=".2"/><path d="M8 20h8M12 16v4"/>',
    "logs": f'<path d="M5 3h10l4 4v14H5z"/><path d="M8 10h8M8 13h8M8 16h6" stroke="{A}"/>',
}

DROP = "M128 33 C150 80 196 108 196 154 A68 68 0 0 1 60 154 C60 108 106 80 128 33 Z"
CRACK = "128,0 124,60 133,110 123,160 135,210 126,256"

APP_SVG = f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#132A45"/><stop offset="1" stop-color="#0A1424"/></linearGradient>
    <radialGradient id="chrome" cx=".35" cy=".55" r=".7"><stop offset="0" stop-color="#F4FAFF"/><stop offset=".4" stop-color="#9FB6CC"/><stop offset="1" stop-color="#14283F"/></radialGradient>
    <linearGradient id="shard" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#D4E2EE"/><stop offset="1" stop-color="#1E3857"/></linearGradient>
    <clipPath id="left"><polygon points="0,0 {CRACK} 0,256"/></clipPath>
    <clipPath id="right"><polygon points="256,0 {CRACK} 256,256"/></clipPath>
  </defs>
  <rect width="256" height="256" rx="52" fill="url(#bg)"/>
  <path d="{DROP}" fill="url(#chrome)" clip-path="url(#left)"/>
  <g transform="translate(9 -5)">
    <g clip-path="url(#right)">
      <path d="{DROP}" fill="url(#shard)" stroke="{A}" stroke-width="3"/>
      <path d="M128 60 L180 120 M126 110 L196 150 M124 160 L170 210 M150 90 L140 220 M170 140 L130 200" stroke="{A}" stroke-width="2" stroke-opacity=".8"/>
    </g>
  </g>
  <polyline points="128,33 124,60 133,110 123,160 135,210 126,222" fill="none" stroke="#8FE9F8" stroke-width="4" stroke-linejoin="round"/>
</svg>
"""


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, body in ICONS.items():
        (OUT / f"{name}.svg").write_text(HEAD + body + "</svg>\n", encoding="utf-8")
    (OUT / "app.svg").write_text(APP_SVG, encoding="utf-8")
    print(f"wrote {len(ICONS) + 1} icons to {OUT}")


if __name__ == "__main__":
    main()
