"""Genera la media de Nexus para Stellar Tool (header + galería + banner).
Paleta y estética alineadas con la app (Theme.qml). Autónomo: solo PIL."""
from pathlib import Path
import math
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "generated"
OUT.mkdir(parents=True, exist_ok=True)

VERSION = "0.2.0"
BG = (20, 22, 28)          # #14161c
PANEL = (29, 33, 41)       # #1d2129
PANEL2 = (35, 40, 52)      # #232834
BORDER = (50, 58, 73)      # #323a49
TEXT = (230, 233, 239)     # #e6e9ef
MUTED = (154, 163, 178)    # #9aa3b2
ACCENT = (90, 162, 255)    # #5aa2ff
GOLD = (230, 180, 85)      # #e6b455
OK = (127, 201, 127)       # #7fc97f
WARN = (230, 180, 85)


def font(size, bold=False):
    for c in (["C:/Windows/Fonts/segoeuib.ttf"] if bold else ["C:/Windows/Fonts/segoeui.ttf"]) + \
             (["C:/Windows/Fonts/arialbd.ttf"] if bold else ["C:/Windows/Fonts/arial.ttf"]):
        if Path(c).exists():
            return ImageFont.truetype(c, size)
    return ImageFont.load_default()


def emoji_font(size):
    p = "C:/Windows/Fonts/seguiemj.ttf"
    return ImageFont.truetype(p, size) if Path(p).exists() else font(size)


def tsize(d, t, f):
    b = d.textbbox((0, 0), t, font=f)
    return b[2] - b[0], b[3] - b[1]


def background(w, h):
    img = Image.new("RGBA", (w, h), BG + (255,))
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line((0, y, w, y), fill=(int(20 + t * 6), int(22 + t * 8), int(28 + t * 14)))
    glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse((-w * 0.2, -h * 0.6, w * 0.5, h * 1.2), fill=(90, 162, 255, 60))
    gd.ellipse((w * 0.55, h * 0.1, w * 1.25, h * 1.5), fill=(230, 180, 85, 40))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(60)))
    return img


def rrect(d, xy, r, fill=None, outline=None, width=1):
    d.rounded_rectangle(xy, radius=r, fill=fill, outline=outline, width=width)


def star(d, cx, cy, r1, r2, fill):
    pts = []
    for i in range(8):
        ang = math.pi / 4 * i - math.pi / 2
        r = r1 if i % 2 == 0 else r2
        pts.append((cx + r * math.cos(ang), cy + r * math.sin(ang)))
    d.polygon(pts, fill=fill)


def logo(d, cx, cy, s, star_gold=True):
    """Ícono de la app: dos ramas que se mergean bajo una estrella."""
    w = max(2, int(s * 0.14))
    a = s * 0.42
    top = cy + a * 0.7
    d.line((cx - a, cy + a * 1.5, cx - a, top), fill=ACCENT, width=w)
    d.line((cx + a, cy + a * 1.5, cx + a, top), fill=ACCENT, width=w)
    d.arc((cx - a, top - a * 1.0, cx + a, top + a * 1.0), start=180, end=360, fill=ACCENT, width=w)
    d.line((cx, top - a, cx, cy - a * 0.2), fill=ACCENT, width=w)
    star(d, cx, cy - a * 0.7, s * 0.42, s * 0.13, GOLD if star_gold else ACCENT)


# ---------- HEADER 1300x372 ----------
def make_header():
    w, h = 1300, 372
    img = background(w, h)
    d = ImageDraw.Draw(img)
    logo(d, 210, h // 2 - 6, 120)
    d.text((360, 96), "STELLAR TOOL", font=font(84, True), fill=TEXT)
    d.text((364, 196), "Mod merger for Stellar Blade", font=font(36), fill=ACCENT)
    d.text((366, 250), "See every change  •  resolve conflicts  •  one verified merged pak",
           font=font(26), fill=MUTED)

    bx1, by1, bx2, by2 = 980, 118, 1230, 254
    rrect(d, (bx1, by1, bx2, by2), 18, fill=PANEL, outline=ACCENT, width=3)
    for i, line in enumerate(["EASY", "MERGE"]):
        tw, _ = tsize(d, line, font(40, True))
        d.text(((bx1 + bx2 - tw) / 2, by1 + 26 + i * 48), line, font=font(40, True), fill=TEXT)
    img.convert("RGB").save(OUT / "stellartool_header_1300x372.jpg", quality=95)
    img.save(OUT / "stellartool_header_1300x372.png")


# ---------- CARD ----------
def card(d, xy, title, body, accent=ACCENT):
    x1, y1, x2, y2 = xy
    rrect(d, xy, 16, fill=PANEL, outline=BORDER, width=2)
    d.rectangle((x1 + 18, y1 + 18, x1 + 23, y2 - 18), fill=accent)
    d.text((x1 + 40, y1 + 22), title, font=font(27, True), fill=TEXT)
    y = y1 + 64
    for line in body:
        d.text((x1 + 40, y), line, font=font(21), fill=MUTED)
        y += 30


# ---------- GALLERY 1920x1080 ----------
def make_gallery():
    w, h = 1920, 1080
    img = background(w, h)
    d = ImageDraw.Draw(img)
    logo(d, 150, 168, 120)
    d.text((248, 96), "STELLAR TOOL", font=font(76, True), fill=TEXT)
    d.text((252, 188), "Merge Stellar Blade mods without losing anything", font=font(34), fill=ACCENT)
    d.text((252, 236), f"GUI + command line   •   open source   •   v{VERSION}", font=font(26), fill=MUTED)

    cards = [
        ("SEE EVERY CHANGE", ["Each modded row/property as a line", "Real vanilla -> modded values", "e.g. MaxHP 100000 -> 300000 (+200%)"], ACCENT),
        ("RESOLVE CONFLICTS", ["Two mods, same value = conflict", "Pick the winner side by side", "or 'prefer this mod for all'"], GOLD),
        ("READS ZEN MODS", ["Nexus .ucas/.utoc via CUE4Parse", "Legacy .pak, .zip and folders too", "Point it at your game once"], ACCENT),
        ("PICK OR EDIT", ["Checkbox per change", "Edit the final value by hand", "Duplicates auto-collapsed"], GOLD),
        ("VERIFIED OUTPUT", ["Native Zen container, re-checked", "Installable .zip for mod managers", "merge_report.txt included"], ACCENT),
        ("TWO MODES", ["Simple: drop mods, one click", "Advanced: full merge-mod workbench", "10 languages"], GOLD),
    ]
    x0, y0, cw, ch, gx, gy = 150, 360, 520, 190, 60, 46
    for i, (t, b, a) in enumerate(cards):
        x = x0 + (i % 3) * (cw + gx)
        y = y0 + (i // 3) * (ch + gy)
        card(d, (x, y, x + cw, y + ch), t, b, a)

    rrect(d, (150, 912, 1770, 1002), 16, fill=PANEL, outline=BORDER, width=2)
    d.text((182, 936), "Note:", font=font(26, True), fill=GOLD)
    d.text((268, 936),
           "Numeric changes from Zen mods (HP, damage...) merge and verify; non-numeric are shown and reported.",
           font=font(25), fill=(200, 205, 214))
    img.convert("RGB").save(OUT / "stellartool_gallery_1920x1080.jpg", quality=95)
    img.save(OUT / "stellartool_gallery_1920x1080.png")


# ---------- COVER 1280x720 (mock de la UI EasyMerge) ----------
def make_cover():
    w, h = 1280, 720
    img = background(w, h)
    d = ImageDraw.Draw(img)
    # ventana
    rrect(d, (40, 40, w - 40, h - 40), 14, fill=BG, outline=BORDER, width=2)
    # sidebar
    rrect(d, (40, 40, 250, h - 40), 14, fill=PANEL, outline=None)
    d.rectangle((240, 40, 252, h - 40), fill=BG)
    logo(d, 74, 88, 34, star_gold=True)
    d.text((104, 72), "Stellar Tool", font=font(22, True), fill=TEXT)
    nav = [("EasyMerge", True), ("Mods", False), ("Changes", False),
           ("Conflicts", False), ("Merge", False), ("Settings", False)]
    for i, (label, sel) in enumerate(nav):
        y = 150 + i * 46
        if sel:
            rrect(d, (58, y - 8, 232, y + 30), 8, fill=PANEL2, outline=BORDER, width=1)
        d.text((78, y), label, font=font(17), fill=TEXT if sel else MUTED)
    rrect(d, (58, h - 120, 232, h - 92), 14, fill=None, outline=OK, width=1)
    d.text((92, h - 116), "Game OK", font=font(14), fill=OK)

    # contenido
    d.text((290, 78), "EasyMerge", font=font(34, True), fill=TEXT)
    d.text((292, 128), "Drop all your mods and merge everything in one click.", font=font(18), fill=MUTED)
    rrect(d, (290, 168, w - 80, 300), 12, fill=PANEL, outline=ACCENT, width=2)
    d.text((520, 218), "Drop your .pak / .zip / folders here", font=font(20), fill=MUTED)

    # lista de cambios (diff)
    rows = [
        ("CharacterTable · ATL_M_Maelstrom_01 · MaxHP", "100000 -> 300000", "+200%"),
        ("CharacterTable · ATL_M_Maelstrom_01 · ShieldBlock", "5 -> 0", "-100%"),
        ("SkillTable · M_DroidTurret_Laser · AttackDamageRate", "1.5 -> 4.5", "+200%"),
        ("EffectTable · Item_HP_RPotion · CalculationValue", "60 -> 30", "-50%"),
    ]
    ry = 340
    for label, val, pct in rows:
        rrect(d, (290, ry, w - 80, ry + 58), 8, fill=PANEL, outline=BORDER, width=1)
        rrect(d, (306, ry + 18, 328, ry + 40), 4, fill=ACCENT, outline=None)  # check
        d.line((311, ry + 29, 316, ry + 35), fill=BG, width=3)
        d.line((316, ry + 35, 324, ry + 23), fill=BG, width=3)
        d.text((348, ry + 9), label, font=font(17), fill=TEXT)
        d.text((348, ry + 32), val, font=font(15), fill=MUTED)
        col = OK if pct.startswith("+") else GOLD
        tw, _ = tsize(d, pct, font(18, True))
        d.text((w - 110 - tw, ry + 18), pct, font=font(18, True), fill=col)
        ry += 70

    rrect(d, (290, ry + 6, 470, ry + 52), 8, fill=ACCENT, outline=None)
    d.text((322, ry + 18), "Merge everything", font=font(17, True), fill=BG)
    img.convert("RGB").save(OUT / "stellartool_cover_1280x720.jpg", quality=95)
    img.save(OUT / "stellartool_cover_1280x720.png")


if __name__ == "__main__":
    make_header()
    make_gallery()
    make_cover()
    print(f"Generated media in {OUT}")
