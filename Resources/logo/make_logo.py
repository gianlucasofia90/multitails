"""MULTITAILS logo set, built the same way as the SONIC GRAIN and DR. SEGG-UENCER sets:
a shaded disc, a two-colour ring, the motif inside and tilted -14 deg, one yellow accent,
and the wordmark in Rubik Black Italic converted to paths.

The motif is a multi-tool opened like a fan: a dark handle with its rivet, and five blades
that are fox tails, each with the white tip. One tip is yellow.

Usage: python3 make_logo.py <repo root>"""
import math, os, sys
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.boundsPen import BoundsPen

REPO = sys.argv[1]
OUT = os.path.join(REPO, "Resources/logo")
os.makedirs(OUT, exist_ok=True)
FONT = TTFont(os.path.join(REPO, "Resources/fonts/Rubik-BlackItalic.ttf"))

INK, WHITE, YOLK = "#0b1430", "#ffffff", "#ffd21f"
DEEP = "#3a1400"            # vignette and rim, a darker groundAlt
SKY = "#3aa3ea"             # the slab of the interface
FUR = "#ffd3a0"             # tail body on the disc
TIP = "#fdfcf8"             # tail tip, as foxTail in TailsUI.h
LINE = "#c45208"            # cut between overlapping blades: the disc colour where they sit

CX = CY = 256.0
TILT = -14


def lerp(a, b, t):
    a = [int(a[i:i + 2], 16) for i in (1, 3, 5)]; b = [int(b[i:i + 2], 16) for i in (1, 3, 5)]
    return "#" + "".join(f"{round(x + (y - x) * t):02x}" for x, y in zip(a, b))


def poly(pts):
    return "M" + " L".join(f"{x:.1f},{y:.1f}" for x, y in pts) + "Z"


# ---------- one blade: a fox tail along a bent line from the pivot ----------
def blade(px, py, ang, length, bend, maxw):
    a = math.radians(ang)
    ex, ey = px + length * math.sin(a), py - length * math.cos(a)
    nx, ny = math.cos(a), math.sin(a)                     # perpendicular, to the right of the blade
    mx, my = (px + ex) / 2 + bend * nx, (py + ey) / 2 + bend * ny

    def at(t, off):
        x = (1 - t) ** 2 * px + 2 * (1 - t) * t * mx + t * t * ex
        y = (1 - t) ** 2 * py + 2 * (1 - t) * t * my + t * t * ey
        dx = 2 * (1 - t) * (mx - px) + 2 * t * (ex - mx)
        dy = 2 * (1 - t) * (my - py) + 2 * t * (ey - my)
        d = math.hypot(dx, dy)
        return x - dy / d * off, y + dx / d * off

    def width(t):
        b = math.sin(math.pi * t ** 0.75)
        w = maxw * (0.30 + 0.70 * b)
        return w * (math.sqrt(max(0.0, 1 - ((t - 0.8) / 0.2) ** 2)) if t > 0.8 else 1.0)   # round, fluffy tip

    def outline(t0, t1, n=72):
        left = [at(t0 + (t1 - t0) * i / n, 0.5 * width(t0 + (t1 - t0) * i / n)) for i in range(n + 1)]
        right = [at(t1 - (t1 - t0) * i / n, -0.5 * width(t1 - (t1 - t0) * i / n)) for i in range(n + 1)]
        return left, right

    l, r = outline(0.0, 1.0)
    body = l + r
    tz = 0.64
    l, r = outline(tz, 1.0, 30)
    zig = []
    for k in range(7):                                    # the jagged edge of the white tip
        u = k / 6
        t = tz + (-0.05 if k % 2 else 0.0)
        zig.append(at(t, (0.5 - u) * width(t)))
    tip = l + r + zig
    return body, tip


BLADES = [   # angle from 12 o'clock (deg, clockwise), length, bend, width; back to front
    (-72, 178, 34, 60),
    (-38, 214, 40, 66),
    (-4, 232, 42, 68),
    (30, 214, 40, 66),
    (64, 178, 34, 60),
]
ACCENT = 2   # the blade with the yellow tip
PIVOT = (256.0, 330.0)
HANDLE = (196.0, 112.0, 60.0)   # angle, length, width


def rot(pts, ang, cx=CX, cy=CY):
    c, s_ = math.cos(math.radians(ang)), math.sin(math.radians(ang))
    return [(cx + (x - cx) * c - (y - cy) * s_, cy + (x - cx) * s_ + (y - cy) * c) for x, y in pts]


def motif(uid, pal):
    px, py = PIVOT
    ha, hl, hw = HANDLE
    a = math.radians(ha)
    hx, hy = px + hl * math.sin(a), py - hl * math.cos(a)
    shapes = [blade(px, py, *b) for b in BLADES]
    # centre the whole drawing (blades and handle, tilted) on the disc
    pts = [p for body, _ in shapes for p in body] + [(hx + hw / 2 * dx, hy + hw / 2 * dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))]
    pts = rot(pts, TILT)
    dx = CX - (min(p[0] for p in pts) + max(p[0] for p in pts)) / 2
    dy = CY - (min(p[1] for p in pts) + max(p[1] for p in pts)) / 2
    s = [f'<g transform="translate({dx:.2f} {dy:.2f}) rotate({TILT} {CX} {CY})">']
    deg = math.degrees(math.atan2(hy - py, hx - px))
    s.append(f'<g transform="rotate({deg:.2f} {px} {py})">'
             f'<rect x="{px - hw / 2}" y="{py - hw / 2}" width="{hl + hw}" height="{hw}" rx="{hw / 2}" fill="{pal["handle"]}"/>'
             f'<rect x="{px + 34}" y="{py - 5}" width="{hl - 46}" height="10" rx="5" fill="{pal["groove"]}"/>'
             f'<circle cx="{px + hl}" cy="{py}" r="8" fill="{pal["rivet"]}"/></g>')
    for i, (body, tip) in enumerate(shapes):
        s.append(f'<path d="{poly(body)}" fill="{pal["body"]}" stroke="{pal["cut"]}" stroke-width="7" stroke-linejoin="round"/>')
        s.append(f'<path d="{poly(tip)}" fill="{pal["accent"] if i == ACCENT else pal["tip"]}"/>')
    s.append(f'<circle cx="{px}" cy="{py}" r="30" fill="{pal["handle"]}"/>')
    s.append(f'<circle cx="{px}" cy="{py}" r="12" fill="{pal["rivet"]}"/>')
    s.append('</g>')
    return s


def disc(uid, cx, cy, R, kind):
    """kind: colour | ink | white. R = outer radius (240 in the 512 icon)."""
    k = R / 240.0
    s = [f'<g transform="translate({cx - CX * k:.2f} {cy - CY * k:.2f}) scale({k:.5f})">']
    if kind == "colour":
        s.append(f'<defs><linearGradient id="g{uid}" x1="0.15" y1="0" x2="0.85" y2="1">'
                 f'<stop offset="0" stop-color="#ffab3d"/><stop offset="0.45" stop-color="#ef7d0f"/><stop offset="1" stop-color="#a13a00"/></linearGradient>'
                 f'<radialGradient id="v{uid}" cx="0.5" cy="0.5" r="0.5">'
                 f'<stop offset="0.6" stop-color="{DEEP}" stop-opacity="0"/><stop offset="0.86" stop-color="{DEEP}" stop-opacity="0.28"/>'
                 f'<stop offset="1" stop-color="{DEEP}" stop-opacity="0.55"/></radialGradient></defs>')
        s.append(f'<circle cx="{CX}" cy="{CY}" r="240" fill="url(#g{uid})"/>')
        s.append(f'<circle cx="{CX}" cy="{CY}" r="240" fill="url(#v{uid})"/>')
        s.append(f'<circle cx="{CX}" cy="{CY}" r="237.1" fill="none" stroke="{DEEP}" stroke-opacity="0.55" stroke-width="5.8"/>')
        # ring like the siblings: short arcs, sky blue at the top fading to the white tip at the bottom, symmetric
        rr, n = 217.2, 120
        for i in range(n):
            a0, a1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1.4) / n
            t = (1 - math.cos(2 * math.pi * (i + 0.5) / n)) / 2          # 0 top, 1 bottom
            col = lerp(SKY, "#9fd3f6", t / 0.5) if t < 0.5 else lerp("#9fd3f6", TIP, (t - 0.5) / 0.5)
            p0 = (CX + rr * math.sin(a0), CY - rr * math.cos(a0))
            p1 = (CX + rr * math.sin(a1), CY - rr * math.cos(a1))
            s.append(f'<path d="M{p0[0]:.1f},{p0[1]:.1f} A{rr},{rr} 0 0 1 {p1[0]:.1f},{p1[1]:.1f}" fill="none" stroke="{col}" stroke-width="8.4"/>')
        pal = dict(body=FUR, tip=TIP, accent=YOLK, cut=LINE, handle=INK, groove="#24304f", rivet=WHITE)
    else:
        bg, fg = (INK, WHITE) if kind == "ink" else (WHITE, INK)
        s.append(f'<circle cx="{CX}" cy="{CY}" r="240" fill="{bg}"/>')
        pal = dict(body=fg, tip=fg, accent=fg, cut=bg, handle=fg, groove=bg, rivet=bg)
    s += motif(uid, pal)
    s.append('</g>')
    return s


# ---------- wordmark ----------
GS = FONT.getGlyphSet(); CMAP = FONT.getBestCmap(); UPM = FONT["head"].unitsPerEm


def text_path(txt, size, track=-0.01):
    sc = size / UPM
    pen = SVGPathPen(GS)
    x = 0.0
    for ch in txt:
        g = CMAP[ord(ch)]
        GS[g].draw(TransformPen(pen, (sc, 0, 0, -sc, x, 0)))
        x += GS[g].width * sc + track * size
    return pen.getCommands(), x - track * size


def bbox_w(txt, size):
    sc = size / UPM; bp = BoundsPen(GS); x = 0.0
    for ch in txt:
        g = CMAP[ord(ch)]
        GS[g].draw(TransformPen(bp, (sc, 0, 0, -sc, x, 0)))
        x += GS[g].width * sc - 0.01 * size
    return bp.bounds


NAME = "MULTITAILS"


def svg(w, h, body, name):
    with open(os.path.join(OUT, name), "w") as f:
        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w:.0f} {h:.0f}" width="{w:.0f}" height="{h:.0f}">\n' + "\n".join(body) + "\n</svg>\n")


svg(512, 512, disc("i", 256, 256, 240, "colour"), "multitails-mark.svg")
svg(512, 512, disc("k", 256, 256, 240, "ink"), "multitails-icon-mono-ink.svg")
svg(512, 512, disc("w", 256, 256, 240, "white"), "multitails-icon-mono-white.svg")

d, adv = text_path(NAME, 120)
b = bbox_w(NAME, 120)
ww = b[2] - b[0] + 16
for col, nm in ((INK, "ink"), (WHITE, "white")):
    svg(ww, 100, [f'<path transform="translate({8 - b[0]:.1f},92.0)" d="{d}" fill="{col}"/>'], f"multitails-wordmark-{nm}.svg")

d, adv = text_path(NAME, 112)
b = bbox_w(NAME, 112)
lw = 228 - b[0] + b[2] + 10
for theme, col in (("dark", WHITE), ("light", INK)):
    svg(lw, 200, disc("l" + theme, 100, 100.0, 92, "colour") +
        [f'<path transform="translate({228 - b[0]:.1f},139.2)" d="{d}" fill="{col}"/>'], f"multitails-lockup-{theme}.svg")

l1, l2 = "MULTI", "TAILS"
d1, _ = text_path(l1, 120); b1 = bbox_w(l1, 120)
d2, _ = text_path(l2, 120); b2 = bbox_w(l2, 120)
sw = max(b1[2] - b1[0], b2[2] - b2[0]) + 60
sw = max(sw, 360)
for theme, col in (("dark", WHITE), ("light", INK)):
    svg(sw, 558, disc("s" + theme, sw / 2, 160, 150, "colour") +
        [f'<path transform="translate({sw / 2 - (b1[0] + b1[2]) / 2:.1f},434.0)" d="{d1}" fill="{col}"/>',
         f'<path transform="translate({sw / 2 - (b2[0] + b2[2]) / 2:.1f},542.0)" d="{d2}" fill="{col}"/>'], f"multitails-stacked-{theme}.svg")
print("ok", ww, lw, sw)
